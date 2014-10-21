/**
File: filtering.cpp
Author: Peter Barnum
Created: 2013

Copyright Anki, Inc. 2013
For internal use only. No part of this code may be used without a signed non-disclosure agreement with Anki, inc.
**/

#include "anki/vision/robot/fiducialDetection.h"
#include "anki/vision/robot/imageProcessing.h"

#include "anki/common/constantsAndMacros.h"

#include "anki/common/robot/benchmarking.h"
#include "anki/common/robot/comparisons.h"
#include "anki/common/robot/hostIntrinsics_m4.h"

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

namespace Anki
{
  namespace Embedded
  {
    namespace ImageProcessing
    {

      template<> Result BinomialFilter<u8,u8,u8>(const Array<u8> &image, Array<u8> &imageFiltered, MemoryStack scratch)
      {
        const u8 kernel0 = 1;
        const u8 kernel1 = 4;
        const u8 kernel2 = 6;
        const u8 kernel3 = 4;
        const u8 kernel4 = 1;

        const s32 kernelShift = 4;

        const s32 imageHeight = image.get_size(0);
        const s32 imageWidth = image.get_size(1);

        AnkiConditionalErrorAndReturnValue(AreValid(image, imageFiltered, scratch),
          RESULT_FAIL_INVALID_OBJECT, "BinomialFilter", "Invalid objects");

        AnkiConditionalErrorAndReturnValue(imageHeight == imageFiltered.get_size(0) && imageWidth == imageFiltered.get_size(1),
          RESULT_FAIL_INVALID_SIZE, "BinomialFilter", "size(image) != size(imageFiltered) (%dx%d != %dx%d)", imageHeight, imageWidth, imageHeight, imageWidth);

        AnkiConditionalErrorAndReturnValue(NotAliased(image, imageFiltered),
          RESULT_FAIL_ALIASED_MEMORY, "BinomialFilter", "image and imageFiltered must be different");

        Array<u8> imageFilteredTmp(imageHeight, imageWidth, scratch);

        AnkiAssert(imageFilteredTmp.get_stride() % sizeof(u8) == 0);

        const s32 imageFilteredTmpStep = imageFilteredTmp.get_stride() / sizeof(u8);

        //% 1. Horizontally filter
        for(s32 y=0; y<imageHeight; y++) {
          //
          // First, filter horizontally
          //

          const u8 * restrict pImage = image.Pointer(y, 0);
          u8 * restrict pImageFilteredTmp = imageFilteredTmp.Pointer(y, 0);

          s32 x = 0;

          pImageFilteredTmp[x] = static_cast<u8>( (pImage[x]*kernel2 + pImage[x+1]*kernel3 + pImage[x+2]*kernel4 + pImage[x]*(kernel0+kernel1)) >> kernelShift );
          x++;
          pImageFilteredTmp[x] = static_cast<u8>( (pImage[x-1]*kernel1 + pImage[x]*kernel2   + pImage[x+1]*kernel3 + pImage[x+2]*kernel4 + pImage[x-1]*kernel0) >> kernelShift );
          x++;

#if ACCELERATION_TYPE == ACCELERATION_ARM_A7
/*          for(; x<(imageWidth-2-7); x+=8) {
            const uint8x8_t imageXm2 = vld1_u8(&pImage[x-2]);
            const uint8x8_t imageXm1 = vld1_u8(&pImage[x-1]);
            const uint8x8_t imageX0  = vld1_u8(&pImage[x]);
            const uint8x8_t imageXp1 = vld1_u8(&pImage[x+1]);
            const uint8x8_t imageXp2 = vld1_u8(&pImage[x+2]);

            const uint16x8_t imageXm2_wide = vmovl_u8(imageXm2);
            const uint16x8_t imageXm1_wide = vmovl_u8(imageXm1);
            const uint16x8_t imageX0_wide  = vmovl_u8(imageX0);
            const uint16x8_t imageXp1_wide = vmovl_u8(imageXp1);
            const uint16x8_t imageXp2_wide = vmovl_u8(imageXp2);

            const uint16x8_t dotXm2 = vmulq_n_u16(imageXm2_wide, kernel0);
            const uint16x8_t dotXm1 = vmulq_n_u16(imageXm1_wide, kernel1);
            const uint16x8_t dotX0  = vmulq_n_u16(imageX0_wide,  kernel2);
            const uint16x8_t dotXp1 = vmulq_n_u16(imageXp1_wide, kernel3);
            const uint16x8_t dotXp2 = vmulq_n_u16(imageXp2_wide, kernel4);

            const uint16x8_t sum = vaddq_u16(vaddq_u16(vaddq_u16(dotXm2, dotXm1), vaddq_u16(dotX0, dotXp1)), dotXp2);

            const uint8x8_t sumU8 = vmovn_u16( vshrq_n_u16(sum, kernelShift) );

            vst1_u8(&pImageFilteredTmp[x], sumU8);
          } // for(; x<(imageWidth-2-7); x+=8)
*/

/*
          for(; x<(imageWidth-2-7); x+=8) {
            const uint8x8_t imageXm2 = vld1_u8(&pImage[x-2]);
            const uint8x8_t imageXm1 = vld1_u8(&pImage[x-1]);
            const uint8x8_t imageX0  = vld1_u8(&pImage[x]);
            const uint8x8_t imageXp1 = vld1_u8(&pImage[x+1]);
            const uint8x8_t imageXp2 = vld1_u8(&pImage[x+2]);

            const uint16x8_t imageXm2_wide = vmovl_u8(imageXm2);
            const uint16x8_t imageXm1_wide = vmovl_u8(imageXm1);
            const uint16x8_t imageX0_wide  = vmovl_u8(imageX0);
            const uint16x8_t imageXp1_wide = vmovl_u8(imageXp1);
            const uint16x8_t imageXp2_wide = vmovl_u8(imageXp2);

            const uint16x8_t dotXm2 = imageXm2_wide;
            const uint16x8_t dotXm1 = vmulq_n_u16(imageXm1_wide, 4);
            const uint16x8_t dotX0  = vmulq_n_u16(imageX0_wide,  6);
            const uint16x8_t dotXp1 = vmulq_n_u16(imageXp1_wide, 4);
            const uint16x8_t dotXp2 = imageXp2_wide;

            const uint16x8_t sum = dotXm2 + dotXm1 + dotX0 + dotXp1 + dotXp2;

            const uint8x8_t sumU8 = vmovn_u16( vshrq_n_u16(sum, kernelShift) );

            vst1_u8(&pImageFilteredTmp[x], sumU8);
          } // for(; x<(imageWidth-2-7); x+=8)
*/

/*
        const uint16x8_t kernel0x8 = {kernel0, kernel0, kernel0, kernel0, kernel0, kernel0, kernel0, kernel0};
        const uint16x8_t kernel1x8 = {kernel1, kernel1, kernel1, kernel1, kernel1, kernel1, kernel1, kernel1};
        const uint16x8_t kernel2x8 = {kernel2, kernel2, kernel2, kernel2, kernel2, kernel2, kernel2, kernel2};
        const uint16x8_t kernel3x8 = {kernel3, kernel3, kernel3, kernel3, kernel3, kernel3, kernel3, kernel3};
        const uint16x8_t kernel4x8 = {kernel4, kernel4, kernel4, kernel4, kernel4, kernel4, kernel4, kernel4};
        for(; x<(imageWidth-2-7); x+=8) {
            const uint8x8_t imageXm2 = vld1_u8(&pImage[x-2]);
            const uint8x8_t imageXm1 = vld1_u8(&pImage[x-1]);
            const uint8x8_t imageX0  = vld1_u8(&pImage[x]);
            const uint8x8_t imageXp1 = vld1_u8(&pImage[x+1]);
            const uint8x8_t imageXp2 = vld1_u8(&pImage[x+2]);

            const uint16x8_t imageXm2_wide = vmovl_u8(imageXm2);
            const uint16x8_t imageXm1_wide = vmovl_u8(imageXm1);
            const uint16x8_t imageX0_wide  = vmovl_u8(imageX0);
            const uint16x8_t imageXp1_wide = vmovl_u8(imageXp1);
            const uint16x8_t imageXp2_wide = vmovl_u8(imageXp2);

            const uint16x8_t dotXm2 = vmulq_u16(imageXm2_wide, kernel0x8);
            const uint16x8_t dotXm1 = vmulq_u16(imageXm1_wide, kernel1x8);
            const uint16x8_t dotX0  = vmulq_u16(imageX0_wide,  kernel2x8);
            const uint16x8_t dotXp1 = vmulq_u16(imageXp1_wide, kernel3x8);
            const uint16x8_t dotXp2 = vmulq_u16(imageXp2_wide, kernel4x8);

            const uint16x8_t sum = vaddq_u16(vaddq_u16(vaddq_u16(dotXm2, dotXm1), vaddq_u16(dotX0, dotXp1)), dotXp2);

            const uint8x8_t sumU8 = vmovn_u16( vshrq_n_u16(sum, kernelShift) );

            vst1_u8(&pImageFilteredTmp[x], sumU8);
          } // for(; x<(imageWidth-2-7); x+=8)
*/

        const uint16x8_t kernel0x8 = {kernel0, kernel0, kernel0, kernel0, kernel0, kernel0, kernel0, kernel0};
        const uint16x8_t kernel1x8 = {kernel1, kernel1, kernel1, kernel1, kernel1, kernel1, kernel1, kernel1};
        const uint16x8_t kernel2x8 = {kernel2, kernel2, kernel2, kernel2, kernel2, kernel2, kernel2, kernel2};
        const uint16x8_t kernel3x8 = {kernel3, kernel3, kernel3, kernel3, kernel3, kernel3, kernel3, kernel3};
        const uint16x8_t kernel4x8 = {kernel4, kernel4, kernel4, kernel4, kernel4, kernel4, kernel4, kernel4};

        for(; x<(imageWidth-2-15); x+=16) {
            const uint8x8_t imageXm2_0 = vld1_u8(&pImage[x-2]);
            const uint8x8_t imageXm1_0 = vld1_u8(&pImage[x-1]);
            const uint8x8_t imageX0_0  = vld1_u8(&pImage[x]);
            const uint8x8_t imageXp1_0 = vld1_u8(&pImage[x+1]);
            const uint8x8_t imageXp2_0 = vld1_u8(&pImage[x+2]);

            const uint8x8_t imageXm2_1 = vld1_u8(&pImage[x-2+8]);
            const uint8x8_t imageXm1_1 = vld1_u8(&pImage[x-1+8]);
            const uint8x8_t imageX0_1  = vld1_u8(&pImage[x+8]);
            const uint8x8_t imageXp1_1 = vld1_u8(&pImage[x+1+8]);
            const uint8x8_t imageXp2_1 = vld1_u8(&pImage[x+2+8]);

            const uint16x8_t imageXm2_wide_0 = vmovl_u8(imageXm2_0);
            const uint16x8_t imageXm1_wide_0 = vmovl_u8(imageXm1_0);
            const uint16x8_t imageX0_wide_0  = vmovl_u8(imageX0_0);
            const uint16x8_t imageXp1_wide_0 = vmovl_u8(imageXp1_0);
            const uint16x8_t imageXp2_wide_0 = vmovl_u8(imageXp2_0);

            const uint16x8_t imageXm2_wide_1 = vmovl_u8(imageXm2_1);
            const uint16x8_t imageXm1_wide_1 = vmovl_u8(imageXm1_1);
            const uint16x8_t imageX0_wide_1  = vmovl_u8(imageX0_1);
            const uint16x8_t imageXp1_wide_1 = vmovl_u8(imageXp1_1);
            const uint16x8_t imageXp2_wide_1 = vmovl_u8(imageXp2_1);

            const uint16x8_t dotXm2_0 = vmulq_u16(imageXm2_wide_0, kernel0x8);
            const uint16x8_t dotXm1_0 = vmulq_u16(imageXm1_wide_0, kernel1x8);
            const uint16x8_t dotX0_0  = vmulq_u16(imageX0_wide_0,  kernel2x8);
            const uint16x8_t dotXp1_0 = vmulq_u16(imageXp1_wide_0, kernel3x8);
            const uint16x8_t dotXp2_0 = vmulq_u16(imageXp2_wide_0, kernel4x8);

            const uint16x8_t dotXm2_1 = vmulq_u16(imageXm2_wide_1, kernel0x8);
            const uint16x8_t dotXm1_1 = vmulq_u16(imageXm1_wide_1, kernel1x8);
            const uint16x8_t dotX0_1  = vmulq_u16(imageX0_wide_1,  kernel2x8);
            const uint16x8_t dotXp1_1 = vmulq_u16(imageXp1_wide_1, kernel3x8);
            const uint16x8_t dotXp2_1 = vmulq_u16(imageXp2_wide_1, kernel4x8);

            const uint16x8_t sum_0 = dotXm2_0 + dotXm1_0 + dotX0_0 + dotXp1_0 + dotXp2_0;
            const uint8x8_t sumU8_0 = vmovn_u16( vshrq_n_u16(sum_0, kernelShift) );

            const uint16x8_t sum_1 = dotXm2_1 + dotXm1_1 + dotX0_1 + dotXp1_1 + dotXp2_1;
            const uint8x8_t sumU8_1 = vmovn_u16( vshrq_n_u16(sum_1, kernelShift) );

            vst1_u8(&pImageFilteredTmp[x], sumU8_0);
            vst1_u8(&pImageFilteredTmp[x+8], sumU8_1);
          } // for(; x<(imageWidth-2-15); x+=16) 

/*        const uint16x8_t kernel0x8 = {kernel0, kernel0, kernel0, kernel0, kernel0, kernel0, kernel0, kernel0};
        const uint16x8_t kernel1x8 = {kernel1, kernel1, kernel1, kernel1, kernel1, kernel1, kernel1, kernel1};
        const uint16x8_t kernel2x8 = {kernel2, kernel2, kernel2, kernel2, kernel2, kernel2, kernel2, kernel2};
        const uint16x8_t kernel3x8 = {kernel3, kernel3, kernel3, kernel3, kernel3, kernel3, kernel3, kernel3};
        const uint16x8_t kernel4x8 = {kernel4, kernel4, kernel4, kernel4, kernel4, kernel4, kernel4, kernel4};

        for(; x<(imageWidth-2-31); x+=32) {
            const uint8x8_t imageXm2_0 = vld1_u8(&pImage[x-2]);
            const uint8x8_t imageXm1_0 = vld1_u8(&pImage[x-1]);
            const uint8x8_t imageX0_0  = vld1_u8(&pImage[x]);
            const uint8x8_t imageXp1_0 = vld1_u8(&pImage[x+1]);
            const uint8x8_t imageXp2_0 = vld1_u8(&pImage[x+2]);

            const uint16x8_t imageXm2_wide_0 = vmovl_u8(imageXm2_0);
            const uint16x8_t imageXm1_wide_0 = vmovl_u8(imageXm1_0);
            const uint16x8_t imageX0_wide_0  = vmovl_u8(imageX0_0);
            const uint16x8_t imageXp1_wide_0 = vmovl_u8(imageXp1_0);
            const uint16x8_t imageXp2_wide_0 = vmovl_u8(imageXp2_0);

            const uint16x8_t dotXm2_0 = vmulq_u16(imageXm2_wide_0, kernel0x8);
            const uint16x8_t dotXm1_0 = vmulq_u16(imageXm1_wide_0, kernel1x8);
            const uint16x8_t dotX0_0  = vmulq_u16(imageX0_wide_0,  kernel2x8);
            const uint16x8_t dotXp1_0 = vmulq_u16(imageXp1_wide_0, kernel3x8);
            const uint16x8_t dotXp2_0 = vmulq_u16(imageXp2_wide_0, kernel4x8);

            const uint16x8_t sum_0 = dotXm2_0 + dotXm1_0 + dotX0_0 + dotXp1_0 + dotXp2_0;
            const uint8x8_t sumU8_0 = vmovn_u16( vshrq_n_u16(sum_0, kernelShift) );

            vst1_u8(&pImageFilteredTmp[x], sumU8_0);

            const uint8x8_t imageXm2_1 = vld1_u8(&pImage[x-2+8]);
            const uint8x8_t imageXm1_1 = vld1_u8(&pImage[x-1+8]);
            const uint8x8_t imageX0_1  = vld1_u8(&pImage[x+8]);
            const uint8x8_t imageXp1_1 = vld1_u8(&pImage[x+1+8]);
            const uint8x8_t imageXp2_1 = vld1_u8(&pImage[x+2+8]);

            const uint16x8_t imageXm2_wide_1 = vmovl_u8(imageXm2_1);
            const uint16x8_t imageXm1_wide_1 = vmovl_u8(imageXm1_1);
            const uint16x8_t imageX0_wide_1  = vmovl_u8(imageX0_1);
            const uint16x8_t imageXp1_wide_1 = vmovl_u8(imageXp1_1);
            const uint16x8_t imageXp2_wide_1 = vmovl_u8(imageXp2_1);

            const uint16x8_t dotXm2_1 = vmulq_u16(imageXm2_wide_1, kernel0x8);
            const uint16x8_t dotXm1_1 = vmulq_u16(imageXm1_wide_1, kernel1x8);
            const uint16x8_t dotX0_1  = vmulq_u16(imageX0_wide_1,  kernel2x8);
            const uint16x8_t dotXp1_1 = vmulq_u16(imageXp1_wide_1, kernel3x8);
            const uint16x8_t dotXp2_1 = vmulq_u16(imageXp2_wide_1, kernel4x8);

            const uint16x8_t sum_1 = dotXm2_1 + dotXm1_1 + dotX0_1 + dotXp1_1 + dotXp2_1;
            const uint8x8_t sumU8_1 = vmovn_u16( vshrq_n_u16(sum_1, kernelShift) );

            vst1_u8(&pImageFilteredTmp[x+8], sumU8_1);

            const uint8x8_t imageXm2_2 = vld1_u8(&pImage[x-2+16]);
            const uint8x8_t imageXm1_2 = vld1_u8(&pImage[x-1+16]);
            const uint8x8_t imageX0_2  = vld1_u8(&pImage[x+16]);
            const uint8x8_t imageXp1_2 = vld1_u8(&pImage[x+1+16]);
            const uint8x8_t imageXp2_2 = vld1_u8(&pImage[x+2+16]);

            const uint16x8_t imageXm2_wide_2 = vmovl_u8(imageXm2_2);
            const uint16x8_t imageXm1_wide_2 = vmovl_u8(imageXm1_2);
            const uint16x8_t imageX0_wide_2  = vmovl_u8(imageX0_2);
            const uint16x8_t imageXp1_wide_2 = vmovl_u8(imageXp1_2);
            const uint16x8_t imageXp2_wide_2 = vmovl_u8(imageXp2_2);

            const uint16x8_t dotXm2_2 = vmulq_u16(imageXm2_wide_2, kernel0x8);
            const uint16x8_t dotXm1_2 = vmulq_u16(imageXm1_wide_2, kernel1x8);
            const uint16x8_t dotX0_2  = vmulq_u16(imageX0_wide_2,  kernel2x8);
            const uint16x8_t dotXp1_2 = vmulq_u16(imageXp1_wide_2, kernel3x8);
            const uint16x8_t dotXp2_2 = vmulq_u16(imageXp2_wide_2, kernel4x8);

            const uint16x8_t sum_2 = dotXm2_2 + dotXm1_2 + dotX0_2 + dotXp1_2 + dotXp2_2;
            const uint8x8_t sumU8_2 = vmovn_u16( vshrq_n_u16(sum_2, kernelShift) );

            vst1_u8(&pImageFilteredTmp[x+16], sumU8_2);

            const uint8x8_t imageXm2_3 = vld1_u8(&pImage[x-2+24]);
            const uint8x8_t imageXm1_3 = vld1_u8(&pImage[x-1+24]);
            const uint8x8_t imageX0_3  = vld1_u8(&pImage[x+24]);
            const uint8x8_t imageXp1_3 = vld1_u8(&pImage[x+1+24]);
            const uint8x8_t imageXp2_3 = vld1_u8(&pImage[x+2+24]);

            const uint16x8_t imageXm2_wide_3 = vmovl_u8(imageXm2_3);
            const uint16x8_t imageXm1_wide_3 = vmovl_u8(imageXm1_3);
            const uint16x8_t imageX0_wide_3  = vmovl_u8(imageX0_3);
            const uint16x8_t imageXp1_wide_3 = vmovl_u8(imageXp1_3);
            const uint16x8_t imageXp2_wide_3 = vmovl_u8(imageXp2_3);

            const uint16x8_t dotXm2_3 = vmulq_u16(imageXm2_wide_3, kernel0x8);
            const uint16x8_t dotXm1_3 = vmulq_u16(imageXm1_wide_3, kernel1x8);
            const uint16x8_t dotX0_3  = vmulq_u16(imageX0_wide_3,  kernel2x8);
            const uint16x8_t dotXp1_3 = vmulq_u16(imageXp1_wide_3, kernel3x8);
            const uint16x8_t dotXp2_3 = vmulq_u16(imageXp2_wide_3, kernel4x8);

            const uint16x8_t sum_3 = dotXm2_3 + dotXm1_3 + dotX0_3 + dotXp1_3 + dotXp2_3;
            const uint8x8_t sumU8_3 = vmovn_u16( vshrq_n_u16(sum_3, kernelShift) );

            vst1_u8(&pImageFilteredTmp[x+24], sumU8_3);
          } // for(; x<(imageWidth-2-31); x+=32) */
#endif

          for(; x<(imageWidth-2); x++) {
            pImageFilteredTmp[x] = static_cast<u8>( (pImage[x-2]*kernel0 + pImage[x-1]*kernel1 + pImage[x]*kernel2 + pImage[x+1]*kernel3 + pImage[x+2]*kernel4) >> kernelShift );            
          }

          pImageFilteredTmp[x] = static_cast<u8>( (pImage[x-2]*kernel0 + pImage[x-1]*kernel1 + pImage[x]*kernel2 + pImage[x+1]*kernel3 + pImage[x+1]*kernel4) >> kernelShift );
          x++;
          pImageFilteredTmp[x] = static_cast<u8>( (pImage[x-2]*kernel0 + pImage[x-1]*kernel1 + pImage[x]*kernel2 + pImage[x]*(kernel3+kernel4)) >> kernelShift );
          x++;

          //
          // At a delayed line, filter vertically
          //

          if(y > 1) {
            const u8 * restrict pImageFilteredTmpYm2 = pImageFilteredTmp - 4*imageFilteredTmpStep;
            const u8 * restrict pImageFilteredTmpYm1 = pImageFilteredTmp - 3*imageFilteredTmpStep;
            const u8 * restrict pImageFilteredTmpY0  = pImageFilteredTmp - 2*imageFilteredTmpStep;
            const u8 * restrict pImageFilteredTmpYp1 = pImageFilteredTmp -   imageFilteredTmpStep;
            const u8 * restrict pImageFilteredTmpYp2 = pImageFilteredTmp;

            u8 * restrict pImageFiltered = imageFiltered.Pointer(y-2, 0);

            if(y == 2) {
              for(s32 x=0; x<imageWidth; x++) {
                pImageFiltered[x] = static_cast<u8>( (pImageFilteredTmpY0[x]*(kernel0+kernel1+kernel2) + pImageFilteredTmpYp1[x]*kernel3 + pImageFilteredTmpYp2[x]*kernel4) >> kernelShift);
              }
            } else if(y == 3) {
              for(s32 x=0; x<imageWidth; x++) {
                pImageFiltered[x] = static_cast<u8>( (pImageFilteredTmpYm1[x]*(kernel0+kernel1) + pImageFilteredTmpY0[x]*kernel2 + pImageFilteredTmpYp1[x]*kernel3 + pImageFilteredTmpYp2[x]*kernel4) >> kernelShift);
              }
            } else { // y >= 4
              for(s32 x=0; x<imageWidth; x++) {
                pImageFiltered[x] = static_cast<u8>( (pImageFilteredTmpYm2[x]*kernel0 + pImageFilteredTmpYm1[x]*kernel1 + pImageFilteredTmpY0[x]*kernel2 + pImageFilteredTmpYp1[x]*kernel3 + pImageFilteredTmpYp2[x]*kernel4) >> kernelShift);
              }
            }
          } // if(y > 2)
        } // for(s32 y=0; y<imageHeight; y++)

        // Do final two rows

        const u8 * restrict pImageFilteredTmpYEndm4 = imageFilteredTmp.Pointer(imageFilteredTmp.get_size(0)-4, 0);;
        const u8 * restrict pImageFilteredTmpYEndm3 = pImageFilteredTmpYEndm4 + imageFilteredTmpStep;
        const u8 * restrict pImageFilteredTmpYEndm2 = pImageFilteredTmpYEndm4 + 2*imageFilteredTmpStep;
        const u8 * restrict pImageFilteredTmpYEndm1 = pImageFilteredTmpYEndm4 + 3*imageFilteredTmpStep;

        u8 * restrict pImageFilteredYEndm2 = imageFiltered.Pointer(imageFiltered.get_size(0)-2, 0);
        u8 * restrict pImageFilteredYEndm1 = imageFiltered.Pointer(imageFiltered.get_size(0)-1, 0);

        for(s32 x=0; x<imageWidth; x++) {
          pImageFilteredYEndm2[x] = static_cast<u8>( (pImageFilteredTmpYEndm4[x]*kernel0 + pImageFilteredTmpYEndm3[x]*kernel1 + pImageFilteredTmpYEndm2[x]*kernel2 + pImageFilteredTmpYEndm1[x]*(kernel3+kernel4))         >> kernelShift);
          pImageFilteredYEndm1[x] = static_cast<u8>( (                                     pImageFilteredTmpYEndm3[x]*kernel0 + pImageFilteredTmpYEndm2[x]*kernel1 + pImageFilteredTmpYEndm1[x]*(kernel2+kernel3+kernel4)) >> kernelShift);
        }

        return RESULT_OK;
      }

      void GetBitShiftDirectionAndMagnitude(
        const s32 in1_numFractionalBits,
        const s32 in2_numFractionalBit,
        const s32 out_numFractionalBit,
        s32 &shiftMagnitude,
        bool &shiftRight)
      {
        if((in1_numFractionalBits+in2_numFractionalBit) > out_numFractionalBit) {
          shiftRight = true;
          shiftMagnitude = in1_numFractionalBits + in2_numFractionalBit - out_numFractionalBit;
        } else if((in1_numFractionalBits+in2_numFractionalBit) < out_numFractionalBit) {
          shiftRight = false;
          shiftMagnitude = out_numFractionalBit - in1_numFractionalBits - in2_numFractionalBit;
        } else {
          shiftRight = false;
          shiftMagnitude = 0;
        }
      } // void GetBitShiftDirectionAndMagnitude()

      /*
      // This is a general function with specializations defined for relevant
      // types
      template<typename Type> Type GetImageTypeMean(void);

      // TODO: these should be in a different .cpp file (imageProcessing.cpp?)
      template<> f32 GetImageTypeMean<f32>() { return 0.5f; }
      template<> u8  GetImageTypeMean<u8>()  { return 128;  }
      template<> f64 GetImageTypeMean<f64>() { return 0.5f; }
      */

      Result BoxFilterNormalize(const Array<u8> &image, const s32 boxSize, const u8 padValue,
        Array<u8> &imageNorm, MemoryStack scratch)
      {
        Result lastResult = RESULT_OK;

        AnkiConditionalErrorAndReturnValue(image.IsValid(),
          RESULT_FAIL_INVALID_OBJECT,
          "BoxFilterNormalize",
          "Input image is invalid.");

        const s32 imageHeight = image.get_size(0);
        const s32 imageWidth  = image.get_size(1);

        AnkiConditionalErrorAndReturnValue(imageNorm.IsValid(),
          RESULT_FAIL_INVALID_OBJECT,
          "BoxFilterNormalize",
          "Output normalized image is invalid.");

        AnkiConditionalErrorAndReturnValue(AreEqualSize(image, imageNorm),
          RESULT_FAIL_INVALID_SIZE,
          "BoxFilterNormalize",
          "Output normalized image must match input image's size.");

        Array<f32> integralImage(imageHeight, imageWidth, scratch);

        AnkiConditionalErrorAndReturnValue(integralImage.IsValid(),
          RESULT_FAIL_OUT_OF_MEMORY,
          "BoxFilterNormalize",
          "Could not allocate integral image (out of memory?).");

        if((lastResult = CreateIntegralImage(image, integralImage)) != RESULT_OK) {
          return lastResult;
        }

        // Divide each input pixel by box filter results computed from integral image
        const s32 halfWidth = MIN(MIN(imageWidth,imageHeight)-1, boxSize)/2;
        const s32 boxWidth  = 2*halfWidth + 1;
        const f32 boxArea   = static_cast<f32>(boxWidth*boxWidth);
        const f32 outMean = 128.f; //static_cast<f32>(GetImageTypeMean<u8>());

        for(s32 y=0; y<imageHeight; y++) {
          // Input/Output pixel pointers:
          const u8 * restrict pImageRow     = image.Pointer(y,0);
          u8       * restrict pImageRowNorm = imageNorm.Pointer(y,0);

          // Integral image pointers for top and bottom of summing box
          s32 rowAhead  = y + halfWidth;
          s32 rowBehind = y - halfWidth - 1;
          s32 inBoundsHeight = boxWidth;
          if(rowAhead >= imageHeight) {
            inBoundsHeight = imageHeight - y + halfWidth;
            rowAhead = imageHeight - 1;
          }
          if(rowBehind < 0) {
            inBoundsHeight = y+halfWidth+1;
            rowBehind = 0;
          }

          const f32 * restrict pIntegralImageRowBehind = integralImage.Pointer(rowBehind,0);
          const f32 * restrict pIntegralImageRowAhead  = integralImage.Pointer(rowAhead, 0);

          // Left side
          for(s32 x=0; x<=halfWidth; x++) {
            f32 OutOfBoundsArea = static_cast<f32>(boxArea - (x+halfWidth+1)*inBoundsHeight);

            f32 boxSum = (pIntegralImageRowAhead[x+halfWidth] -
              pIntegralImageRowAhead[0] -
              pIntegralImageRowBehind[x+halfWidth] +
              pIntegralImageRowBehind[0] +
              OutOfBoundsArea*static_cast<f32>(padValue));

            pImageRowNorm[x] = static_cast<u8>(CLIP(outMean * (static_cast<f32>(pImageRow[x]) * boxArea) / boxSum, 0.f, 255.f));
          }

          // Middle
          const f32 OutOfBoundsArea = static_cast<f32>(boxArea - boxWidth*inBoundsHeight);
          const f32 paddingSum = OutOfBoundsArea * static_cast<f32>(padValue);
          for(s32 x=halfWidth+1; x<imageWidth-halfWidth; x++) {
            f32 boxSum = (pIntegralImageRowAhead[x+halfWidth] -
              pIntegralImageRowAhead[x-halfWidth-1] -
              pIntegralImageRowBehind[x+halfWidth] +
              pIntegralImageRowBehind[x-halfWidth-1] +
              paddingSum);

            pImageRowNorm[x] = static_cast<u8>(CLIP(outMean * (static_cast<f32>(pImageRow[x]) * boxArea) / boxSum, 0.f, 255.f));
          }

          // Right side
          for(s32 x=imageWidth-halfWidth; x<imageWidth; x++) {
            f32 OutOfBoundsArea = static_cast<f32>(boxArea - (imageWidth-x+halfWidth)*inBoundsHeight);

            f32 boxSum = (pIntegralImageRowAhead[imageWidth-1] -
              pIntegralImageRowAhead[x-halfWidth-1] -
              pIntegralImageRowBehind[imageWidth-1] +
              pIntegralImageRowBehind[x-halfWidth-1] +
              OutOfBoundsArea*static_cast<f32>(padValue));

            pImageRowNorm[x] = static_cast<u8>(CLIP(outMean * (static_cast<f32>(pImageRow[x]) * boxArea) / boxSum, 0.f, 255.f));
          }
        }

        return RESULT_OK;
      } // BoxFilterNormalize()

      template<> Result BoxFilter<u8,u16,u16>(const Array<u8> &image, const s32 boxHeight, const s32 boxWidth, Array<u16> &filtered, MemoryStack scratch)
      {
        AnkiConditionalErrorAndReturnValue(image.IsValid() && filtered.IsValid(),
          RESULT_FAIL_INVALID_OBJECT, "BoxFilter", "Image is invalid");

        const s32 imageHeight = image.get_size(0);
        const s32 imageWidth  = image.get_size(1);

        const s32 boxHeight2 = boxHeight / 2;
        const s32 boxWidth2 = boxWidth / 2;

        AnkiConditionalErrorAndReturnValue(AreEqualSize(image, filtered),
          RESULT_FAIL_INVALID_SIZE, "BoxFilter", "Output normalized image must match input image's size.");

        AnkiConditionalErrorAndReturnValue(imageWidth%8 == 0,
          RESULT_FAIL_INVALID_SIZE, "BoxFilter", "Image width must be divisible by 8");

        AnkiConditionalErrorAndReturnValue(boxHeight > 2 && boxWidth > 2 && IsOdd(boxWidth) && IsOdd(boxHeight),
          RESULT_FAIL_INVALID_SIZE, "BoxFilter", "Box filter must be greater than two and odd");

        AnkiConditionalWarn(boxHeight*boxWidth <= 256,
          "BoxFilter", "Filtering may overflow");

        s32 y;

        // Includes extra padding for simd
        u16 * restrict verticalAccumulator = reinterpret_cast<u16*>( scratch.Allocate(imageWidth*sizeof(u16) + 16) );
        memset(verticalAccumulator, 0, imageWidth*sizeof(u16));

        // Accumulate a whole boxHeight
        for(y=0; y<boxHeight; y++) {
          const u8 * restrict pImage = image.Pointer(y,0);
          for(s32 x=0; x<imageWidth; x+=8) {
            const u32 image3210 = *reinterpret_cast<const u32*>(pImage + x);
            const u32 image7654 = *reinterpret_cast<const u32*>(pImage + x + 4);

            const u32 toAdd10 =  (image3210 & 0xFF)            | ((image3210 & 0xFF00)     << 8);
            const u32 toAdd32 = ((image3210 & 0xFF0000) >> 16) | ((image3210 & 0xFF000000) >> 8);

            const u32 toAdd54 =  (image7654 & 0xFF)            | ((image7654 & 0xFF00)     << 8);
            const u32 toAdd76 = ((image7654 & 0xFF0000) >> 16) | ((image7654 & 0xFF000000) >> 8);

            *reinterpret_cast<u32*>(verticalAccumulator + x)     += toAdd10;
            *reinterpret_cast<u32*>(verticalAccumulator + x + 2) += toAdd32;
            *reinterpret_cast<u32*>(verticalAccumulator + x + 4) += toAdd54;
            *reinterpret_cast<u32*>(verticalAccumulator + x + 6) += toAdd76;
          }
        }

        //
        // Add the first row to the filtered image
        //

        filtered(0,boxHeight2-1,0,-1).Set(0);

        {
          // Grab the pointer to the horizontally negative-offset location in the filtered image
          u16 * restrict pFiltered = filtered.Pointer(boxHeight2,0) - boxWidth2;

          u16 horizontalAccumulator = 0;

          s32 x;
          for(x=0; x<boxWidth; x++) {
            horizontalAccumulator += verticalAccumulator[x];
          }

          filtered(boxHeight2,boxHeight2,0,boxWidth2-1).Set(0);

          pFiltered[x-1] = horizontalAccumulator;

          for(; x<imageWidth-3; x+=4) {
            const u32 toAdd10 = *reinterpret_cast<const u32*>(verticalAccumulator + x);
            const u32 toAdd32 = *reinterpret_cast<const u32*>(verticalAccumulator + x + 2);

            u32 toSub10 = *reinterpret_cast<const u32*>(verticalAccumulator + x - boxWidth);
            u32 toSub32 = *reinterpret_cast<const u32*>(verticalAccumulator + x - boxWidth + 2);

            // h is previous horizontal accumulator
            u32 total10 = toAdd10 + horizontalAccumulator; // [1, 0h]
            total10 += total10 << 16; // [10h, 0h]

            toSub10 += toSub10 << 16; // [10, 0]
            total10 -= toSub10;

            u32 total32 = toAdd32 + (total10 >> 16); // [3, 210h]
            total32 += total32 << 16; // [3210h, 210h]

            toSub32 += toSub32 << 16; // [32, 2]
            total32 -= toSub32;

            horizontalAccumulator = total32 >> 16;

            *reinterpret_cast<u32*>(pFiltered + x) = total10;
            *reinterpret_cast<u32*>(pFiltered + x + 2) = total32;
          }

          for(; x<imageWidth; x++) {
            horizontalAccumulator += verticalAccumulator[x] - verticalAccumulator[x-boxWidth];
            pFiltered[x] = horizontalAccumulator;
          }

          filtered(boxHeight2,boxHeight2,-boxWidth2,-1).Set(0);
        }

        //
        // Add the remaining rows to the filtered image
        //

        for(; y<imageHeight; y++) {
          // Grab the pointer to the horizontally negative-offset location in the filtered image
          u16 * restrict pFiltered = filtered.Pointer(y - boxHeight2,0) - boxWidth2;

          const u8 * restrict pImageOld = image.Pointer(y-boxHeight,0);
          const u8 * restrict pImageNew = image.Pointer(y,0);

          for(s32 x=0; x<imageWidth; x+=4) {
            const u32 imageNew3210 = *reinterpret_cast<const u32*>(pImageNew + x);

            const u32 imageOld3210 = *reinterpret_cast<const u32*>(pImageOld + x);

            const u32 toAdd10 =  (imageNew3210 & 0xFF)            | ((imageNew3210 & 0xFF00)     << 8);
            const u32 toAdd32 = ((imageNew3210 & 0xFF0000) >> 16) | ((imageNew3210 & 0xFF000000) >> 8);

            const u32 toSub10 =  (imageOld3210 & 0xFF)            | ((imageOld3210 & 0xFF00)     << 8);
            const u32 toSub32 = ((imageOld3210 & 0xFF0000) >> 16) | ((imageOld3210 & 0xFF000000) >> 8);

            *reinterpret_cast<u32*>(verticalAccumulator + x)     += toAdd10 - toSub10;
            *reinterpret_cast<u32*>(verticalAccumulator + x + 2) += toAdd32 - toSub32;
          }

          u16 horizontalAccumulator = 0;

          s32 x;
          for(x=0; x<boxWidth; x++) {
            horizontalAccumulator += verticalAccumulator[x];
          }

          filtered(y-boxHeight2,y-boxHeight2,0,boxWidth2-1).Set(0);

          pFiltered[x-1] = horizontalAccumulator;

          for(; x<imageWidth-3; x+=4) {
            const u32 toAdd10 = *reinterpret_cast<const u32*>(verticalAccumulator + x);
            const u32 toAdd32 = *reinterpret_cast<const u32*>(verticalAccumulator + x + 2);

            u32 toSub10 = *reinterpret_cast<const u32*>(verticalAccumulator + x - boxWidth);
            u32 toSub32 = *reinterpret_cast<const u32*>(verticalAccumulator + x - boxWidth + 2);

            // h is previous horizontal accumulator
            u32 total10 = toAdd10 + horizontalAccumulator; // [1, 0h]
            total10 += total10 << 16; // [10h, 0h]

            toSub10 += toSub10 << 16; // [10, 0]
            total10 -= toSub10;

            u32 total32 = toAdd32 + (total10 >> 16); // [3, 210h]
            total32 += total32 << 16; // [3210h, 210h]

            toSub32 += toSub32 << 16; // [32, 2]
            total32 -= toSub32;

            horizontalAccumulator = total32 >> 16;

            *reinterpret_cast<u32*>(pFiltered + x) = total10;
            *reinterpret_cast<u32*>(pFiltered + x + 2) = total32;
          }

          for(; x<imageWidth; x++) {
            horizontalAccumulator += verticalAccumulator[x] - verticalAccumulator[x-boxWidth];
            pFiltered[x] = horizontalAccumulator;
          }

          filtered(y-boxHeight2,y-boxHeight2,-boxWidth2,-1).Set(0);
        }

        filtered(-boxHeight2,-1,0,-1).Set(0);

        return RESULT_OK;
      } // Result BoxFilter()

      Result DownsampleBilinear(const Array<u8> &in, Array<u8> &out, MemoryStack scratch)
      {
        const s32 numSubpixelBits = 11;
        const u32 subpixelMultiplierU32 = 1 << numSubpixelBits;
        const f32 subpixelMultiplierF32 = static_cast<f32>(subpixelMultiplierU32);

        AnkiConditionalErrorAndReturnValue(AreValid(in, out, scratch),
          RESULT_FAIL_INVALID_OBJECT, "DownsampleBilinear", "Invalid objects");

        const s32 inHeight = in.get_size(0);
        const s32 inWidth  = in.get_size(1);

        const s32 outHeight = out.get_size(0);
        const s32 outWidth  = out.get_size(1);

        Point<f32> scale(
          static_cast<f32>(inWidth)  / static_cast<f32>(outWidth),
          static_cast<f32>(inHeight) / static_cast<f32>(outHeight));

        AnkiConditionalErrorAndReturnValue(scale.x > 1.0f && scale.y > 1.0f,
          RESULT_FAIL_INVALID_SIZE, "DownsampleBilinear", "out is larger than in");

        const f32 yInStart     = (0.5f * scale.y) - 0.5f;
        const f32 yInIncrement = scale.y;

        const f32 xInStart     = (0.5f * scale.x) - 0.5f;
        const f32 xInIncrement = scale.x;

        FixedLengthList<s32> inX0s_S32(outWidth, scratch);
        FixedLengthList<s32> inX1s_S32(outWidth, scratch);
        FixedLengthList<u32> alphaXs(outWidth, scratch);

        AnkiConditionalErrorAndReturnValue(AreValid(inX0s_S32, inX1s_S32, alphaXs),
          RESULT_FAIL_OUT_OF_MEMORY, "DownsampleBilinear", "Out of memory");

        // Compute the x coordinates
        {
          s32 * restrict pInX0s_S32 = inX0s_S32.Pointer(0);
          s32 * restrict pInX1s_S32 = inX1s_S32.Pointer(0);
          u32 * restrict pAlphaXs = alphaXs.Pointer(0);

          for(s32 x=0; x<outWidth; x++) {
            const f32 inX = xInStart + xInIncrement * static_cast<f32>(x);

            s32 inX0_S32 = FloorS32(inX);
            s32 inX1_S32 = CeilS32(inX);

            // Technically, we can't interpolate the borders. But this is a reasonable approximation
            if(inX0_S32 < 0)
              inX0_S32 = 0;

            if(inX1_S32 < 0)
              inX1_S32 = 0;

            if(inX0_S32 > (inWidth-1))
              inX0_S32 = inWidth-1;

            if(inX1_S32 > (inWidth-1))
              inX1_S32 = inWidth-1;

            const f32 inX0 = static_cast<f32>(inX0_S32);

            const f32 alphaX = inX - inX0;

            pInX0s_S32[x] = inX0_S32;
            pInX1s_S32[x] = inX1_S32;
            pAlphaXs[x] = saturate_cast<u32>(alphaX * subpixelMultiplierF32);
          } // for(s32 x=0; x<outWidth; x++)
        }

        const s32 * restrict pInX0s_S32 = inX0s_S32.Pointer(0);
        const s32 * restrict pInX1s_S32 = inX1s_S32.Pointer(0);
        const u32 * restrict pAlphaXs = alphaXs.Pointer(0);

        for(s32 y=0; y<outHeight; y++) {
          const f32 inY = yInStart + yInIncrement * static_cast<f32>(y);

          s32 inY0_S32 = FloorS32(inY);
          s32 inY1_S32 = CeilS32(inY);

          // Technically, we can't interpolate the borders. But this is a reasonable approximation
          if(inY0_S32 < 0)
            inY0_S32 = 0;

          if(inY1_S32 < 0)
            inY1_S32 = 0;

          if(inY0_S32 > (inHeight-1))
            inY0_S32 = inHeight-1;

          if(inY1_S32 > (inHeight-1))
            inY1_S32 = inHeight-1;

          const f32 inY0 = static_cast<f32>(inY0_S32);
          //const f32 inY1 = static_cast<f32>(inY1_S32);

          const f32 alphaYF32 = inY - inY0;
          //const f32 alphaYinverseF32 = 1.0f - alphaYF32;

          const u32 alphaYU32 = saturate_cast<u32>(alphaYF32 * subpixelMultiplierF32);
          const u32 alphaYinverseU32 = subpixelMultiplierU32 - alphaYU32;

          const u8 * restrict pIn_y0 = in.Pointer(inY0_S32, 0);
          const u8 * restrict pIn_y1 = in.Pointer(inY1_S32, 0);

          u8 * restrict pOut = out.Pointer(y, 0);

          s32 x = 0;

          for(; x<outWidth; x++) {
            const s32 inX0_S32 = pInX0s_S32[x];
            const s32 inX1_S32 = pInX1s_S32[x];
            const u32 alphaX = pAlphaXs[x];

            const u32 alphaXinverse = subpixelMultiplierU32 - alphaX;

            const u8 pixelTL = pIn_y0[inX0_S32];
            const u8 pixelTR = pIn_y0[inX1_S32];
            const u8 pixelBL = pIn_y1[inX0_S32];
            const u8 pixelBR = pIn_y1[inX1_S32];

            //const f32 interpolatedPixelValueF32 = InterpolateBilinear2d<f32>(pixelTL, pixelTR, pixelBL, pixelBR, alphaY, alphaYinverse, alphaX, alphaXinverse);

            const u32 interpolatedTop = alphaXinverse*pixelTL + alphaX*pixelTR;
            const u32 interpolatedBottom = alphaXinverse*pixelBL + alphaX*pixelBR;
            const u32 interpolatedPixelValue = alphaYinverseU32*interpolatedTop + alphaYU32*interpolatedBottom;
            const u32 interpolatedPixelValueScaled = interpolatedPixelValue >> (2*numSubpixelBits);

            pOut[x] = interpolatedPixelValueScaled & 0xFF;
          } // for(s32 x=0; x<outWidth; x++)
        } // for(s32 y=0; y<outHeight; y++)

        return RESULT_OK;
      } // Result DownsampleBilinear(const Array<u8> &in, Array<u8> &out, MemoryStack scratch)

      Result FastGradient(const Array<u8> &in, Array<s8> &dx, Array<s8> &dy, MemoryStack scratch)
      {
        const s32 imageHeight = in.get_size(0);
        const s32 imageWidth = in.get_size(1);

        AnkiConditionalErrorAndReturnValue(in.IsValid() && dy.IsValid() && dx.IsValid() && scratch.IsValid(),
          RESULT_FAIL_INVALID_OBJECT, "FastGradient", "Image is invalid");

        AnkiConditionalErrorAndReturnValue(
          imageHeight == dx.get_size(0) && imageHeight == dy.get_size(0) &&
          imageWidth == dx.get_size(1) && imageWidth == dy.get_size(1),
          RESULT_FAIL_INVALID_SIZE, "FastGradient", "Images must be the same size");

        AnkiConditionalErrorAndReturnValue(imageWidth%8 == 0,
          RESULT_FAIL_INVALID_SIZE, "FastGradient", "Image width must be divisible by 8");

        dx(0,0,0,-1).Set(0);

        for(s32 y=1; y<(imageHeight-1); y++) {
          const u8 * restrict pIn_y0  = in.Pointer(y,0);

          s8 * restrict pDx = dx.Pointer(y,0);

          s32 x;

          //#if !defined(USE_ARM_ACCELERATION)
          for(x=1; x<(imageWidth-1); x++) {
            pDx[x] = static_cast<s8>( (static_cast<s32>(pIn_y0[x+1]) >> 1) - (static_cast<s32>(pIn_y0[x-1]) >> 1) );
          }
          //#else // #if !defined(USE_ARM_ACCELERATION)
          //          // TODO: make work so Keil doesn't merge loads
          //          for(x = 0; x<(imageWidth-7); x+=8) {
          //            const u32 inM3210 = *reinterpret_cast<const u32*>(pIn_y0 + x - 1);
          //            const u32 inM7654 = *reinterpret_cast<const u32*>(pIn_y0 + x + 3);
          //
          //            const u32 inP3210 = *reinterpret_cast<const u32*>(pIn_y0 + x + 1);
          //            const u32 inP7654 = *reinterpret_cast<const u32*>(pIn_y0 + x + 5);
          //
          //            const u32 inM3210Half = (inM3210 >> 1) & 0x7f7f7f7f;
          //            const u32 inM7654Half = (inM7654 >> 1) & 0x7f7f7f7f;
          //
          //            const u32 inP3210Half = (inP3210 >> 1) & 0x7f7f7f7f;
          //            const u32 inP7654Half = (inP7654 >> 1) & 0x7f7f7f7f;
          //
          //            const u32 out3210 = __SSUB8(inP3210Half, inM3210Half);
          //            const u32 out7654 = __SSUB8(inP7654Half, inM7654Half);
          //
          //            *reinterpret_cast<u32*>(pDx + x) = out3210;
          //            *reinterpret_cast<u32*>(pDx + x + 4) = out7654;
          //          }
          //#endif // #if !defined(USE_ARM_ACCELERATION) ... #else

          pDx[0] = 0;
          pDx[imageWidth-1] = 0;

          const u8 * restrict pIn_ym1 = in.Pointer(y-1,0);
          const u8 * restrict pIn_yp1 = in.Pointer(y+1,0);

          s8 * restrict pDy = dy.Pointer(y,0);

#if USE_ARM_ACCELERATION == ACCELERATION_NONE || USE_ARM_ACCELERATION == ACCELERATION_ARM_A7
          for(x=1; x<(imageWidth-1); x++) {
            pDy[x] = static_cast<s8>( (static_cast<s32>(pIn_yp1[x]) >> 1) - (static_cast<s32>(pIn_ym1[x]) >> 1) );
          }
#elif USE_ARM_ACCELERATION == ACCELERATION_ARM_M4
          // pIn_ym1 and pIn_yp1 should always be aligned, so this is okay even with auto-load-merging
          for(x = 0; x<(imageWidth-7); x+=8) {
            const u32 inM3210 = *reinterpret_cast<const u32*>(pIn_ym1 + x);
            const u32 inM7654 = *reinterpret_cast<const u32*>(pIn_ym1 + x + 4);

            const u32 inP3210 = *reinterpret_cast<const u32*>(pIn_yp1 + x);
            const u32 inP7654 = *reinterpret_cast<const u32*>(pIn_yp1 + x + 4);

            const u32 inM3210Half = (inM3210 >> 1) & 0x7f7f7f7f;
            const u32 inM7654Half = (inM7654 >> 1) & 0x7f7f7f7f;

            const u32 inP3210Half = (inP3210 >> 1) & 0x7f7f7f7f;
            const u32 inP7654Half = (inP7654 >> 1) & 0x7f7f7f7f;

            const u32 out3210 = __SSUB8(inP3210Half, inM3210Half);
            const u32 out7654 = __SSUB8(inP7654Half, inM7654Half);

            *reinterpret_cast<u32*>(pDy + x) = out3210;
            *reinterpret_cast<u32*>(pDy + x + 4) = out7654;
          }
#endif // #elif USE_ARM_ACCELERATION == ACCELERATION_ARM_M4

          pDy[0] = 0;
          pDy[imageWidth-1] = 0;
        } // for(s32 y=1; y<(imageHeight-1); y++)

        dx(-1,-1,0,-1).Set(0);

        return RESULT_OK;
      } // Result FastGradient(const Array<u8> &in, Array<s8> &dx, Array<s8> &dy, MemoryStack scratch)
    } // namespace ImageProcessing
  } // namespace Embedded
} // namespace Anki

