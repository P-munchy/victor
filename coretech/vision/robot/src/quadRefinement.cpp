#include "anki/common/robot/geometry.h"
#include "anki/common/robot/interpolate.h"
#include "anki/common/robot/benchmarking.h"
#include "anki/common/robot/hostIntrinsics_m4.h"

#include "anki/vision/robot/fiducialDetection.h"

#define DEBUG_QUAD_REFINEMENT 0

#define VISUALIZE_WITH_MATLAB 0

#if VISUALIZE_WITH_MATLAB
#include "anki/common/robot/matlabInterface.h"
static Anki::Embedded::Matlab matlab(false);
#endif

#define ACCELERATION_NONE 0
#define ACCELERATION_ARM_M4 1
#define ACCELERATION_ARM_A7 2

#if defined(__ARM_ARCH_7A__)
#define ACCELERATION_TYPE ACCELERATION_ARM_A7
#else
#define ACCELERATION_TYPE ACCELERATION_ARM_M4
#endif

#if ACCELERATION_TYPE == ACCELERATION_NONE
#warning not using ARM acceleration
#endif

#if ACCELERATION_TYPE == ACCELERATION_ARM_A7
#include <arm_neon.h>
#endif

namespace Anki {
  namespace Embedded {
    static f32 MaxCornerChange(const Array<f32>& currentHomography,
      Quadrilateral<f32>& currentQuad)
    {
      // Compute the current refined corners
      const f32 h00 = currentHomography[0][0];
      const f32 h01 = currentHomography[0][1];
      const f32 h02 = currentHomography[0][2];

      const f32 h10 = currentHomography[1][0];
      const f32 h11 = currentHomography[1][1];
      const f32 h12 = currentHomography[1][2];

      const f32 h20 = currentHomography[2][0];
      const f32 h21 = currentHomography[2][1];
      const f32 h22 = currentHomography[2][2];

      // Make a copy of the current position before we overwrite it, so we can
      // compare to it to see how much we changed
      Quadrilateral<f32> prevQuad = currentQuad;

      // Homography is always mapping from canonical quad (0,0), (0,1), (1,0), (1,1)
      f32 normalization = 1.f / h22;
      currentQuad[0].x = h02 * normalization;
      currentQuad[0].y = h12 * normalization;

      normalization = 1.f / (h21 + h22);
      currentQuad[1].x = (h01 + h02) * normalization;
      currentQuad[1].y = (h11 + h12) * normalization;

      normalization = 1.f / (h20 + h22);
      currentQuad[2].x = (h00 + h02) * normalization;
      currentQuad[2].y = (h10 + h12) * normalization;

      normalization = 1.f / (h20 + h21 + h22);
      currentQuad[3].x = (h00 + h01 + h02) * normalization;
      currentQuad[3].y = (h10 + h11 + h12) * normalization;

      // See how different the new corner locations are from the originals we
      // stored above
      f32 maxChange = 0.f;
      for(s32 i=0; i<4; ++i) {
        const f32 cornerChange = (currentQuad[i] - prevQuad[i]).Length();
        if(cornerChange > maxChange) {
          maxChange = cornerChange;
        }
      }

      return maxChange;
    } // MaxCornerChange()

    Result RefineQuadrilateral(
      const Quadrilateral<f32>& initialQuad,
      const Array<f32>& initialHomography,
      const Array<u8> &image,
      const f32 squareWidthFraction,
      const s32 maxIterations,
      const f32 darkGray,
      const f32 brightGray,
      const s32 numSamples,
      const f32 maxCornerChange,
      const f32 minCornerChange,
      Quadrilateral<f32>& refinedQuad,
      Array<f32>& refinedHomography,
      MemoryStack scratch)
    {
      BeginBenchmark("vme_quadrefine_init");

      Result lastResult = RESULT_OK;

      AnkiConditionalErrorAndReturnValue(AreEqualSize(3, 3, refinedHomography, initialHomography),
        RESULT_FAIL_INVALID_SIZE,
        "RefineQuadrilateral",
        "Input and Output refined homography array must be valid and 3x3.");

      AnkiConditionalErrorAndReturnValue(NotAliased(initialHomography, refinedHomography),
        RESULT_FAIL_ALIASED_MEMORY, "RefineQuadrilateral", "initialHomography and refinedHomography are aliased");

#     if VISUALIZE_WITH_MATLAB
      CoreTechPrint("Initial quad: ");
      initialQuad.Print();
      CoreTechPrint("\n");

      matlab.PutArray(image, "img");
      matlab.PutQuad(initialQuad, "initialQuad");
      matlab.EvalStringEcho("initialQuad = double(initialQuad); "
        "imagesc(img), axis image, hold on, colormap(gray), "
        "plot(initialQuad([1 2 4 3 1],1)+1, "
        "     initialQuad([1 2 4 3 1],2)+1, "
        "     'r', 'LineWidth', 2, "
        "     'Tag', 'initialQuad'); drawnow");
#     endif

      // Use the size of the initial quad to establish the resolution and thus
      // the scale of the derivatives of the implicit template model
      //
      //diagonal = sqrt(max( sum((this.corners(1,:)-this.corners(4,:)).^2), ...
      //    sum((this.corners(2,:)-this.corners(3,:)).^2))) / sqrt(2);
      const Point<f32> diff03 = initialQuad[0] - initialQuad[3];
      const Point<f32> diff12 = initialQuad[1] - initialQuad[2];
      const f32 diagonal = MAX(diff03.Length(), diff12.Length()) / sqrtf(2.f);

      // Set up the coordinate samples for the inner and outer squares:
      //
      // xSquareOuter = [linspace(0,1,N) linspace(0,1,N) zeros(1,N)      ones(1,N)];
      // ySquareOuter = [zeros(1,N)      ones(1,N)       linspace(0,1,N) linspace(0,1,N)];
      //
      // xSquareInner = [linspace(this.SquareWidthFraction, 1-this.SquareWidthFraction, N) ...
      //    linspace(this.SquareWidthFraction, 1-this.SquareWidthFraction, N) ...
      //    this.SquareWidthFraction*ones(1,N) ...
      //    (1-this.SquareWidthFraction)*ones(1,N)];
      //
      // ySquareInner = [this.SquareWidthFraction*ones(1,N) ...
      //    (1-this.SquareWidthFraction)*ones(1,N) ...
      //    linspace(this.SquareWidthFraction, 1-this.SquareWidthFraction, N) ...
      //    linspace(this.SquareWidthFraction, 1-this.SquareWidthFraction, N)];
      //
      // xsquare = [xSquareInner xSquareOuter]';
      // ysquare = [ySquareInner ySquareOuter]';
      //
      // TxOuter = [-1 zeros(1,N-2) 1, -1 zeros(1,N-2) 1, -ones(1,N), ones(1,N)];
      // TyOuter = [-ones(1,N), ones(1,N), -1 zeros(1,N-2) 1, -1 zeros(1,N-2) 1];
      // TxInner = -TxOuter;
      // TyInner = -TyOuter;
      //
      // Tx = Contrast/2 * diagonal*[TxInner TxOuter]';
      // Ty = Contrast/2 * diagonal*[TyInner TyOuter]';

      const f32 contrast = (brightGray - darkGray)/255.f;
      const f32 derivMagnitude = 0.5f * contrast * diagonal;

      // N = ceil(NumSamples/8);
      const s32 N = CeilS32(static_cast<f32>(numSamples)/8.f);
      const s32 actualNumSamples = 8*N;

      const f32 outerInc = 1.f / static_cast<f32>(N-1);
      //LinearSequence<f32> OuterOneToN = LinearSequence<f32>(0.f, outerInc, 1.f);

      const f32 innerInc = (1.f - 2.f*squareWidthFraction) / static_cast<f32>(N-1);
      //LinearSequence<f32> InnerOneToN = LinearSequence<f32>(squareWidthFraction, innerInc, 1.f-squareWidthFraction);

      // Template coordinates
      Array<f32> xSquare(1, actualNumSamples, scratch);
      Array<f32> ySquare(1, actualNumSamples, scratch);

      // Template derivatives
      Array<f32> Tx(1, actualNumSamples, scratch);
      Array<f32> Ty(1, actualNumSamples, scratch);

      AnkiConditionalErrorAndReturnValue(xSquare.IsValid(), RESULT_FAIL_MEMORY,
        "RefineQuadrilateral",
        "Failed to allocate xSquare Array.");

      AnkiConditionalErrorAndReturnValue(ySquare.IsValid(), RESULT_FAIL_MEMORY,
        "RefineQuadrilateral",
        "Failed to allocate ySquare Array.");

      AnkiConditionalErrorAndReturnValue(Tx.IsValid(), RESULT_FAIL_MEMORY,
        "RefineQuadrilateral",
        "Failed to allocate Tx Array.");

      AnkiConditionalErrorAndReturnValue(Ty.IsValid(), RESULT_FAIL_MEMORY,
        "RefineQuadrilateral",
        "Failed to allocate Ty Array.");

      //
      // Fill in the top and bottom coordinates and derivatives
      //

      // Outer
      {
        f32 * restrict pXtop = xSquare.Pointer(0,0);
        f32 * restrict pYtop = ySquare.Pointer(0,0);

        f32 * restrict pTXtop = Tx.Pointer(0,0);
        f32 * restrict pTYtop = Ty.Pointer(0,0);

        f32 * restrict pXbtm = xSquare.Pointer(0,N);
        f32 * restrict pYbtm = ySquare.Pointer(0,N);

        f32 * restrict pTXbtm = Tx.Pointer(0,N);
        f32 * restrict pTYbtm = Ty.Pointer(0,N);

        f32 x=0.f;
        for(s32 index = 0; index<N; x += outerInc, ++index)
        {
          pXtop[index] = x;
          pYtop[index] = 0.f;

          pXbtm[index] = x;
          pYbtm[index] = 1.f;

          pTXtop[index] =  0.f;
          pTYtop[index] = -derivMagnitude;

          pTXbtm[index] = 0.f;
          pTYbtm[index] = derivMagnitude;
        }

        pTXtop[0]   = -derivMagnitude;
        pTXtop[N-1] =  derivMagnitude;

        pTXbtm[0]   = -derivMagnitude;
        pTXbtm[N-1] =  derivMagnitude;
      } // Outer Top / Bottom

      // Inner
      {
        f32 * restrict pXtop = xSquare.Pointer(0,4*N);
        f32 * restrict pYtop = ySquare.Pointer(0,4*N);

        f32 * restrict pTXtop = Tx.Pointer(0,4*N);
        f32 * restrict pTYtop = Ty.Pointer(0,4*N);

        f32 * restrict pXbtm = xSquare.Pointer(0,5*N);
        f32 * restrict pYbtm = ySquare.Pointer(0,5*N);

        f32 * restrict pTXbtm = Tx.Pointer(0,5*N);
        f32 * restrict pTYbtm = Ty.Pointer(0,5*N);

        f32 x = squareWidthFraction;
        for(s32 index = 0; index < N; x += innerInc, ++index)
        {
          pXtop[index] = x;
          pYtop[index] = squareWidthFraction;

          pXbtm[index] = x;
          pYbtm[index] = 1.f - squareWidthFraction;

          pTXtop[index] = 0.f;
          pTYtop[index] = derivMagnitude;

          pTXbtm[index] =  0.f;
          pTYbtm[index] = -derivMagnitude;
        }

        pTXtop[0]   =  derivMagnitude;
        pTXtop[N-1] = -derivMagnitude;

        pTXbtm[0]   =  derivMagnitude;
        pTXbtm[N-1] = -derivMagnitude;
      } // Inner Top / Bottom

      //
      // Fill in the left and right coordinates and derivatives
      //

      // Outer
      {
        f32 * restrict pXleft = xSquare.Pointer(0,2*N);
        f32 * restrict pYleft = ySquare.Pointer(0,2*N);

        f32 * restrict pTXleft = Tx.Pointer(0,2*N);
        f32 * restrict pTYleft = Ty.Pointer(0,2*N);

        f32 * restrict pXright = xSquare.Pointer(0,3*N);
        f32 * restrict pYright = ySquare.Pointer(0,3*N);

        f32 * restrict pTXright = Tx.Pointer(0,3*N);
        f32 * restrict pTYright = Ty.Pointer(0,3*N);

        f32 y = 0.f;
        for(s32 index = 0; index < N; y += outerInc, ++index)
        {
          pXleft[index] = 0.f;
          pYleft[index] = y;

          pXright[index] = 1.f;
          pYright[index] = y;

          pTXleft[index] = -derivMagnitude;
          pTYleft[index] =  0.f;

          pTXright[index] = derivMagnitude;
          pTYright[index] = 0.f;
        }

        pTYleft[0]   = -derivMagnitude;
        pTYleft[N-1] =  derivMagnitude;

        pTYright[0]   = -derivMagnitude;
        pTYright[N-1] =  derivMagnitude;
      } // Outer Left / Right

      // Inner
      {
        f32 * restrict pXleft = xSquare.Pointer(0,6*N);
        f32 * restrict pYleft = ySquare.Pointer(0,6*N);

        f32 * restrict pTXleft = Tx.Pointer(0,6*N);
        f32 * restrict pTYleft = Ty.Pointer(0,6*N);

        f32 * restrict pXright = xSquare.Pointer(0,7*N);
        f32 * restrict pYright = ySquare.Pointer(0,7*N);

        f32 * restrict pTXright = Tx.Pointer(0,7*N);
        f32 * restrict pTYright = Ty.Pointer(0,7*N);

        f32 y = squareWidthFraction;
        for(s32 index = 0; index < N; y += innerInc, ++index)
        {
          pXleft[index] = squareWidthFraction;
          pYleft[index] = y;

          pXright[index] = 1.f - squareWidthFraction;
          pYright[index] = y;

          pTXleft[index] = derivMagnitude;
          pTYleft[index] = 0.f;

          pTXright[index] = -derivMagnitude;
          pTYright[index] =  0.f;
        }

        pTYleft[0]   =  derivMagnitude;
        pTYleft[N-1] = -derivMagnitude;

        pTYright[0]   =  derivMagnitude;
        pTYright[N-1] = -derivMagnitude;
      } // Inner Left / Right

      /* Less efficient?

      // Outer Square, Top Side:
      ArraySlice<f32> xSide = xSquare(0,0,0,N-1);
      ArraySlice<f32> ySide = ySquare(0,0,0,N-1);
      OuterOneToN.Evaluate(xSide);
      ySide.Set(0.f);

      // Outer Square, Bottom Side:
      xSide = xSquare(0,0,N,2*N-1);
      ySide = ySquare(0,0,N,2*N-1);
      OuterOneToN.Evaluate(xSide);
      ySide.Set(1.f);

      // Outer Square, Left Side:
      xSide = xSquare(0,0,2*N,3*N-1);
      ySide = ySquare(0,0,2*N,3*N-1);
      xSide.Set(0.f);
      OuterOneToN.Evaluate(ySide);

      // Outer Square, Right Side:
      xSide = xSquare(0,0,3*N,4*N-1);
      ySide = ySquare(0,0,3*N,4*N-1);
      xSide.Set(1.f);
      OuterOneToN.Evaluate(ySide);

      // Inner Square, Top Side:
      xSide = xSquare(0,0,4*N,5*N-1);
      ySide = ySquare(0,0,4*N,5*N-1);
      InnerOneToN.Evaluate(xSide);
      ySide.Set(0.f);

      // Inner Square, Bottom Side:
      xSide = xSquare(0,0,5*N,6*N-1);
      ySide = ySquare(0,0,5*N,6*N-1);
      InnerOneToN.Evaluate(xSide);
      ySide.Set(1.f);

      // Inner Square, Left Side:
      xSide = xSquare(0,0,6*N,7*N-1);
      ySide = ySquare(0,0,6*N,7*N-1);
      xSide.Set(0.f);
      InnerOneToN.Evaluate(ySide);

      // Inner Square, Right Side:
      xSide = xSquare(0,0,7*N,8*N-1);
      ySide = ySquare(0,0,7*N,8*N-1);
      xSide.Set(1.f);
      InnerOneToN.Evaluate(ySide);

      // Outer Square, Top Side:
      ArraySlice<f32> xSide = xSquare(0,0,0,N-1);
      ArraySlice<f32> ySide = ySquare(0,0,0,N-1);
      OuterOneToN.Evaluate(xSide);
      ySide.Set(0.f);

      // Outer Square, Bottom Side:
      xSide = xSquare(0,0,N,2*N-1);
      ySide = ySquare(0,0,N,2*N-1);
      OuterOneToN.Evaluate(xSide);
      ySide.Set(1.f);

      // Outer Square, Left Side:
      xSide = xSquare(0,0,2*N,3*N-1);
      ySide = ySquare(0,0,2*N,3*N-1);
      xSide.Set(0.f);
      OuterOneToN.Evaluate(ySide);

      // Outer Square, Right Side:
      xSide = xSquare(0,0,3*N,4*N-1);
      ySide = ySquare(0,0,3*N,4*N-1);
      xSide.Set(1.f);
      OuterOneToN.Evaluate(ySide);

      // Inner Square, Top Side:
      xSide = xSquare(0,0,4*N,5*N-1);
      ySide = ySquare(0,0,4*N,5*N-1);
      InnerOneToN.Evaluate(xSide);
      ySide.Set(0.f);

      // Inner Square, Bottom Side:
      xSide = xSquare(0,0,5*N,6*N-1);
      ySide = ySquare(0,0,5*N,6*N-1);
      InnerOneToN.Evaluate(xSide);
      ySide.Set(1.f);

      // Inner Square, Left Side:
      xSide = xSquare(0,0,6*N,7*N-1);
      ySide = ySquare(0,0,6*N,7*N-1);
      xSide.Set(0.f);
      InnerOneToN.Evaluate(ySide);

      // Inner Square, Right Side:
      xSide = xSquare(0,0,7*N,8*N-1);
      ySide = ySquare(0,0,7*N,8*N-1);
      xSide.Set(1.f);
      InnerOneToN.Evaluate(ySide);
      */

      // A = [ xsquare.*Tx  ysquare.*Tx  Tx  ...
      //       xsquare.*Ty  ysquare.*Ty  Ty ...
      //       (-xsquare.^2.*Tx-xsquare.*ysquare.*Ty) ...
      //       (-xsquare.*ysquare.*Tx-ysquare.^2.*Ty)];

      const f32 * restrict pX = xSquare.Pointer(0,0);
      const f32 * restrict pY = ySquare.Pointer(0,0);

      const f32 * restrict pTX = Tx.Pointer(0,0);
      const f32 * restrict pTY = Ty.Pointer(0,0);

      Array<f32> A(8, actualNumSamples, scratch);
      AnkiConditionalErrorAndReturnValue(A.IsValid(), RESULT_FAIL_MEMORY,
        "RefineQuadrilateral",
        "Failed to allocate A matrix.");

      // Get a pointer to each row of A:
      f32 * restrict Arow[8];
      for(s32 i=0; i<8; ++i) {
        Arow[i] = A.Pointer(i,0); // TODO: do i need "restrict" here?
      }

      // Create A matrix of Jacobians
      for(s32 iSample=0; iSample<actualNumSamples; iSample++) {
        const f32 x = pX[iSample];
        const f32 y = pY[iSample];

        const f32 tx = pTX[iSample];
        const f32 ty = pTY[iSample];

        Arow[0][iSample] = x*tx;
        Arow[1][iSample] = y*tx;
        Arow[2][iSample] = tx;

        Arow[3][iSample] = x*ty;
        Arow[4][iSample] = y*ty;
        Arow[5][iSample] = ty;

        Arow[6][iSample] = -x*x*tx - x*y*ty;
        Arow[7][iSample] = -x*y*tx - y*y*ty;
      } // for each sample

      // NOTE: We don't need Tx or Ty from here on.  Can we pop them somehow?

      // template = (Contrast/2)*ones(size(xsquare));
      const f32 templatePixelValue = 0.5f*(darkGray + brightGray);

      const s32 imageHeight = image.get_size(0);
      const s32 imageWidth = image.get_size(1);

      const f32 xyReferenceMin = 0.0f;
      const f32 xReferenceMax = static_cast<f32>(imageWidth) - 1.0f;
      const f32 yReferenceMax = static_cast<f32>(imageHeight) - 1.0f;

      const f32 oneOverTwoFiftyFive = 1.0f / 255.0f;

      refinedHomography.Set(initialHomography);

      Array<f32> AWAt(8, 8, scratch);
      Array<f32> b(1, 8, scratch);

      Array<f32> homographyUpdate(3,3,scratch);
      Array<f32> newHomography(3,3,scratch);

      AnkiConditionalErrorAndReturnValue(AWAt.IsValid(), RESULT_FAIL_MEMORY,
        "RefineQuadrilateral",
        "Failed to allocate AWAt matrix.");

      AnkiConditionalErrorAndReturnValue(b.IsValid(), RESULT_FAIL_MEMORY,
        "RefineQuadrilateral",
        "Failed to allocate b vector.");

      AnkiConditionalErrorAndReturnValue(homographyUpdate.IsValid(), RESULT_FAIL_MEMORY,
        "RefineQuadrilateral",
        "Failed to allocate homographyUpdate matrix.");

      AnkiConditionalErrorAndReturnValue(newHomography.IsValid(), RESULT_FAIL_MEMORY,
        "RefineQuadrilateral",
        "Failed to allocate newHomography matrix.");

      // These addresses should be known at compile time, so should be faster
      f32 AWAt_raw[8][8];
      f32 b_raw[8];

      EndBenchmark("vme_quadrefine_init");

      BeginBenchmark("vme_quadrefine_mainLoop");
      bool restoreOriginal = false;

      for(s32 iteration=0; iteration<maxIterations && !restoreOriginal; iteration++) {
        BeginBenchmark("vme_quadrefine_mainLoop_init");

        const f32 h00 = refinedHomography[0][0]; const f32 h01 = refinedHomography[0][1]; const f32 h02 = refinedHomography[0][2];
        const f32 h10 = refinedHomography[1][0]; const f32 h11 = refinedHomography[1][1]; const f32 h12 = refinedHomography[1][2];
        const f32 h20 = refinedHomography[2][0]; const f32 h21 = refinedHomography[2][1]; const f32 h22 = refinedHomography[2][2];

        //AWAt.SetZero();
        //b.SetZero();

        for(s32 ia=0; ia<8; ia++) {
          for(s32 ja=0; ja<8; ja++) {
            AWAt_raw[ia][ja] = 0;
          }
          b_raw[ia] = 0;
        }

        s32 numInBounds = 0;

        EndBenchmark("vme_quadrefine_mainLoop_init");

        BeginBenchmark("vme_quadrefine_mainLoop_samples");
        for(s32 iSample=0; iSample<actualNumSamples && !restoreOriginal; iSample++) {
          //BeginBenchmark("vme_quadrefine_mainLoop_samples1");

          const f32 xOriginal = pX[iSample];
          const f32 yOriginal = pY[iSample];

          // TODO: These two could be strength reduced
          const f32 xTransformedRaw = h00*xOriginal + h01*yOriginal + h02;
          const f32 yTransformedRaw = h10*xOriginal + h11*yOriginal + h12;

          const f32 normalization = 1.f / (h20*xOriginal + h21*yOriginal + h22);

          const f32 xTransformed = (xTransformedRaw * normalization);
          const f32 yTransformed = (yTransformedRaw * normalization);

          // DEBUG!
          //xTransformedArray[0][iSample] = xTransformed;
          //yTransformedArray[0][iSample] = yTransformed;

          const f32 x0 = FLT_FLOOR(xTransformed);
          const f32 x1 = ceilf(xTransformed); // x0 + 1.0f;

          const f32 y0 = FLT_FLOOR(yTransformed);
          const f32 y1 = ceilf(yTransformed); // y0 + 1.0f;

          // If out of bounds, continue
          if(x0 < xyReferenceMin || x1 > xReferenceMax || y0 < xyReferenceMin || y1 > yReferenceMax) {
            //EndBenchmark("vme_quadrefine_mainLoop_samples1");
            continue;
          }

          //EndBenchmark("vme_quadrefine_mainLoop_samples1");

          //BeginBenchmark("vme_quadrefine_mainLoop_samples2");

          numInBounds++;

          const f32 alphaX = xTransformed - x0;
          const f32 alphaXinverse = 1.0f - alphaX;

          const f32 alphaY = yTransformed - y0;
          const f32 alphaYinverse = 1.0f - alphaY;

          const s32 y0S32 = Round<s32>(y0);
          const s32 y1S32 = Round<s32>(y1);
          const s32 x0S32 = Round<s32>(x0);

          const u8 * restrict pReference_y0 = image.Pointer(y0S32, x0S32);
          const u8 * restrict pReference_y1 = image.Pointer(y1S32, x0S32);

          const f32 pixelTL = *pReference_y0;
          const f32 pixelTR = *(pReference_y0+1);
          const f32 pixelBL = *pReference_y1;
          const f32 pixelBR = *(pReference_y1+1);

          const f32 interpolatedPixelF32 = InterpolateBilinear2d<f32>(pixelTL, pixelTR, pixelBL, pixelBR,
            alphaY, alphaYinverse, alphaX, alphaXinverse);

          const f32 tGradientValue = oneOverTwoFiftyFive * (interpolatedPixelF32 - templatePixelValue);

          //EndBenchmark("vme_quadrefine_mainLoop_samples2");

          //BeginBenchmark("vme_quadrefine_mainLoop_samples3");

#         if ACCELERATION_TYPE == ACCELERATION_NONE
          for(s32 ia=0; ia<8; ia++) {
            for(s32 ja=ia; ja<8; ja++) {
              AWAt_raw[ia][ja] += Arow[ia][iSample] * Arow[ja][iSample];
            }
            b_raw[ia] += Arow[ia][iSample] * tGradientValue;
          }
#         else // #if ACCELERATION_TYPE == ACCELERATION_NONE
          const f32 a0 = Arow[0][iSample];
          const f32 a1 = Arow[1][iSample];
          const f32 a2 = Arow[2][iSample];
          const f32 a3 = Arow[3][iSample];
          const f32 a4 = Arow[4][iSample];
          const f32 a5 = Arow[5][iSample];
          const f32 a6 = Arow[6][iSample];
          const f32 a7 = Arow[7][iSample];

          AWAt_raw[0][0] += a0 * a0;
          AWAt_raw[0][1] += a0 * a1;
          AWAt_raw[0][2] += a0 * a2;
          AWAt_raw[0][3] += a0 * a3;
          AWAt_raw[0][4] += a0 * a4;
          AWAt_raw[0][5] += a0 * a5;
          AWAt_raw[0][6] += a0 * a6;
          AWAt_raw[0][7] += a0 * a7;
          b_raw[0] += a0 * tGradientValue;

          AWAt_raw[1][1] += a1 * a1;
          AWAt_raw[1][2] += a1 * a2;
          AWAt_raw[1][3] += a1 * a3;
          AWAt_raw[1][4] += a1 * a4;
          AWAt_raw[1][5] += a1 * a5;
          AWAt_raw[1][6] += a1 * a6;
          AWAt_raw[1][7] += a1 * a7;
          b_raw[1] += a1 * tGradientValue;

          AWAt_raw[2][2] += a2 * a2;
          AWAt_raw[2][3] += a2 * a3;
          AWAt_raw[2][4] += a2 * a4;
          AWAt_raw[2][5] += a2 * a5;
          AWAt_raw[2][6] += a2 * a6;
          AWAt_raw[2][7] += a2 * a7;
          b_raw[2] += a2 * tGradientValue;

          AWAt_raw[3][3] += a3 * a3;
          AWAt_raw[3][4] += a3 * a4;
          AWAt_raw[3][5] += a3 * a5;
          AWAt_raw[3][6] += a3 * a6;
          AWAt_raw[3][7] += a3 * a7;
          b_raw[3] += a3 * tGradientValue;

          AWAt_raw[4][4] += a4 * a4;
          AWAt_raw[4][5] += a4 * a5;
          AWAt_raw[4][6] += a4 * a6;
          AWAt_raw[4][7] += a4 * a7;
          b_raw[4] += a4 * tGradientValue;

          AWAt_raw[5][5] += a5 * a5;
          AWAt_raw[5][6] += a5 * a6;
          AWAt_raw[5][7] += a5 * a7;
          b_raw[5] += a5 * tGradientValue;

          AWAt_raw[6][6] += a6 * a6;
          AWAt_raw[6][7] += a6 * a7;
          b_raw[6] += a6 * tGradientValue;

          AWAt_raw[7][7] += a7 * a7;
          b_raw[7] += a7 * tGradientValue;
#         endif // #if ACCELERATION_TYPE == ACCELERATION_NONE ... #else

          //EndBenchmark("vme_quadrefine_mainLoop_samples3");
        } // for each sample

        EndBenchmark("vme_quadrefine_mainLoop_samples");

        BeginBenchmark("vme_quadrefine_mainLoop_finalize");

        // Put the raw A and b matrices into the Array containers
        for(s32 ia=0; ia<8; ia++) {
          for(s32 ja=ia; ja<8; ja++) {
            AWAt[ia][ja] = AWAt_raw[ia][ja];
          }
          b[0][ia] = b_raw[ia];
        }

        Matrix::MakeSymmetric(AWAt, false);

        // Solve for the update
        bool numericalFailure = false;
        if((lastResult = Matrix::SolveLeastSquaresWithCholesky(AWAt, b, false, numericalFailure)) != RESULT_OK) {
          return lastResult;
        }

        if(numericalFailure){
          AnkiWarn("RefineQuadrilateral", "numericalFailure\n");
          restoreOriginal = true;
        }

        // Update the homography
        // tformUpdate = eye(3) + [update(1:3)'; update(4:6)'; update(7:8)' 0];
        const f32 * restrict pUpdate = b.Pointer(0,0);
        homographyUpdate[0][0] = 1.0f + pUpdate[0]; homographyUpdate[0][1] = pUpdate[1];        homographyUpdate[0][2] = pUpdate[2];
        homographyUpdate[1][0] = pUpdate[3];        homographyUpdate[1][1] = 1.0f + pUpdate[4]; homographyUpdate[1][2] = pUpdate[5];
        homographyUpdate[2][0] = pUpdate[6];        homographyUpdate[2][1] = pUpdate[7];        homographyUpdate[2][2] = 1.0f;

        // this.tform = this.tform*inv(tformUpdate);
        Invert3x3(homographyUpdate[0][0], homographyUpdate[0][1], homographyUpdate[0][2],
          homographyUpdate[1][0], homographyUpdate[1][1], homographyUpdate[1][2],
          homographyUpdate[2][0], homographyUpdate[2][1], homographyUpdate[2][2]);

        Matrix::Multiply(refinedHomography, homographyUpdate, newHomography);

        if(!FLT_NEAR(newHomography[2][2], 1.0f)) {
          Matrix::DotDivide<f32,f32,f32>(newHomography, newHomography[2][2], newHomography);
        }

        refinedHomography.Set(newHomography);

        const f32 currentCornerChange = MaxCornerChange(refinedHomography, refinedQuad);
        if(currentCornerChange < minCornerChange) {
          // Converged!  Stop iterating.
          //AnkiWarn("QuadRefinement.CornersConverged",
          //printf("Corner change at iteration %d = %f which is less than "
          //         "convergence tolerance of %f. Stopping iterations.\n",
          //         iteration, currentCornerChange, CornerConvergenceTolerance);

          EndBenchmark("vme_quadrefine_mainLoop_finalize");
          break;
        }

#       if VISUALIZE_WITH_MATLAB
        {
          matlab.PutQuad(refinedQuad, "refinedQuad");
          matlab.EvalStringEcho("delete(findobj(gcf, 'Tag', 'refinedQuad')); "
            "refinedQuad = double(refinedQuad); "
            "plot(refinedQuad([1 2 4 3 1],1)+1, "
            "     refinedQuad([1 2 4 3 1],2)+1, "
            "     'b', 'LineWidth', 1, "
            "     'Tag', 'refinedQuad'); drawnow");
        }
#       endif

        EndBenchmark("vme_quadrefine_mainLoop_finalize");
      } // for each iteration

      EndBenchmark("vme_quadrefine_mainLoop");

      BeginBenchmark("vme_quadrefine_finalize");

#     if VISUALIZE_WITH_MATLAB
      CoreTechPrint("Final quad: ");
      refinedQuad.Print();
      CoreTechPrint("\n");
      {
        matlab.PutQuad(refinedQuad, "refinedQuad");
        matlab.EvalStringEcho("refinedQuad = double(refinedQuad); "
          "plot(refinedQuad([1 2 4 3 1],1)+1, "
          "     refinedQuad([1 2 4 3 1],2)+1, "
          "     'g', 'LineWidth', 1, "
          "     'Tag', 'refinedQuad'); drawnow");
      }
#     endif

      Quadrilateral<f32> initialQuadF32;
      initialQuadF32.SetCast(initialQuad);

      // If corner change check or numerical failure triggered a restoreOriginal
      // do so now.
      if(restoreOriginal) {
#       if DEBUG_QUAD_REFINEMENT
        AnkiWarn("RefineQuadrilateral", "Restoring original quad.\n");
#       endif
        refinedQuad = initialQuadF32;
        refinedHomography.Set(initialHomography);
      } else {
        // Check to make sure the refined quad isn't too different from the intitial one.
        // If it is, restore the original.
        const f32 finalCornerChange = MaxCornerChange(refinedHomography, initialQuadF32);
        if(finalCornerChange > maxCornerChange) {
#         if DEBUG_QUAD_REFINEMENT
          AnkiWarn("RefineQuadrilateral", "Quad changed too much.\n");
#         endif
          lastResult = RESULT_FAIL;
        }
      }

      EndBenchmark("vme_quadrefine_finalize");

      return lastResult;
    } // RefineQuadrilateral
  }
}
