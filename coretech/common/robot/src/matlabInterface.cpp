/**
File: matlabInterface.cpp
Author: Peter Barnum
Created: 2013

Copyright Anki, Inc. 2013
For internal use only. No part of this code may be used without a signed non-disclosure agreement with Anki, inc.
**/

#include "anki/common/robot/matlabInterface.h"
#include "anki/common/robot/errorHandling.h"

namespace Anki
{
  namespace Embedded
  {
#if ANKICORETECH_EMBEDDED_USE_MATLAB

#define TEXT_BUFFER_SIZE 1024

    /*
    // TODO: Remove
    mxClassID ConvertToMatlabType(const char * const typeName, const size_t byteDepth)
    {
    //mexPrintf("typename %s\n", typeName);
    #if defined(__APPLE_CC__) // Apple Xcode
    if(typeName[0] == 'h') {
    return mxUINT8_CLASS;
    } else if(typeName[0] == 'a') {
    return mxINT8_CLASS;
    } else if(typeName[0] == 't') {
    return mxUINT16_CLASS;
    } else if(typeName[0] == 's') {
    return mxINT16_CLASS;
    } else if(typeName[0] == 'j') {
    return mxUINT32_CLASS;
    } else if(typeName[0] == 'i') {
    return mxINT32_CLASS;
    } else if(typeName[0] == 'y') {
    return mxUINT64_CLASS;
    } else if(typeName[0] == 'x') {
    return mxINT64_CLASS;
    } else if(typeName[0] == 'f') {
    return mxSINGLE_CLASS;
    } else if(typeName[0] == 'd') {
    return mxDOUBLE_CLASS;
    }
    #else // #if defined(__APPLE_CC__) // Apple Xcode
    if(typeName[0] == 'u') { //unsigned
    if(byteDepth == 1) {
    return mxUINT8_CLASS;
    } else if(byteDepth == 2) {
    return mxUINT16_CLASS;
    }else if(byteDepth == 4) {
    return mxUINT32_CLASS;
    }else if(byteDepth == 8) {
    return mxUINT64_CLASS;
    }
    } else if(typeName[0] == 'f' && byteDepth == 4) { //float
    return mxSINGLE_CLASS;
    } else if(typeName[0] == 'd' && byteDepth == 8) { //double
    return mxDOUBLE_CLASS;
    } else { // signed
    if(byteDepth == 1) {
    return mxINT8_CLASS;
    } else if(byteDepth == 2) {
    return mxINT16_CLASS;
    }else if(byteDepth == 4) {
    return mxINT32_CLASS;
    }else if(byteDepth == 8) {
    return mxINT64_CLASS;
    }
    }
    #endif // #if defined(__APPLE_CC__) // Apple Xcode ... #else

    return mxUNKNOWN_CLASS;
    } // mxClassID convertToMatlabType(const char * const typeName, const size_t byteDepth)
    */

    std::string ConvertToMatlabTypeString(const char *typeName, size_t byteDepth)
    {
#if defined(__APPLE_CC__) // Apple Xcode
      if(typeName[0] == 'h') {
        return std::string("uint8");
      } else if(typeName[0] == 'a') {
        return std::string("int8");
      } else if(typeName[0] == 't') {
        return std::string("uint16");
      } else if(typeName[0] == 's') {
        return std::string("int16");
      } else if(typeName[0] == 'j') {
        return std::string("uint32");
      } else if(typeName[0] == 'i') {
        return std::string("int32");
      } else if(typeName[0] == 'y') {
        return std::string("uint64");
      } else if(typeName[0] == 'x') {
        return std::string("int64");
      } else if(typeName[0] == 'f') {
        return std::string("single");
      } else if(typeName[0] == 'd') {
        return std::string("double");
      }
#else // #if defined(__APPLE_CC__) // Apple Xcode
      if(typeName[0] == 'u') { //unsigned
        if(byteDepth == 1) {
          return std::string("uint8");
        } else if(byteDepth == 2) {
          return std::string("uint16");
        }else if(byteDepth == 4) {
          return std::string("uint32");
        }else if(byteDepth == 8) {
          return std::string("uint64");
        }
      } else if(typeName[0] == 'f' && byteDepth == 4) { //float
        return std::string("single");
      } else if(typeName[0] == 'd' && byteDepth == 8) { //double
        return std::string("double");
      } else { // signed
        if(byteDepth == 1) {
          return std::string("int8");
        } else if(byteDepth == 2) {
          return std::string("int16");
        }else if(byteDepth == 4) {
          return std::string("int32");
        }else if(byteDepth == 8) {
          return std::string("int64");
        }
      }
#endif // #if defined(__APPLE_CC__) // Apple Xcode ... #else

      return std::string("unknown");
    } // std::string convertToMatlabTypeString(const char *typeName, size_t byteDepth)

    Matlab::Matlab(bool clearWorkspace)
    {
      // Multithreading under Windows requires this command
      // CoInitializeEx(NULL, COINIT_MULTITHREADED);

      this->ep = engOpen(NULL);

      if(clearWorkspace){
        EvalString("clear all");
      }

      EvalString("lastAnkiCommandBuffer=cell(0, 1);");
    }

    std::string Matlab::EvalString(const char * const format, ...)
    {
      AnkiConditionalErrorAndReturnValue(this->ep, "", "Anki.", "Matlab engine is not started/connected");

      va_list args;
      char *buffer;

      va_start( args, format );

#ifdef _MSC_VER
      const u32 len = _vscprintf( format, args ) + 1;
#else
      const u32 len = 1024;
#endif

      buffer = (char*) malloc( len * sizeof(char) );
      vsnprintf( buffer, len, format, args );

      engEvalString(this->ep, buffer);

      std::string toReturn = std::string(buffer);
      free( buffer );

      return toReturn;
    }

    std::string Matlab::EvalStringEcho(const char * const format, ...)
    {
      AnkiConditionalErrorAndReturnValue(this->ep, "", "Anki.", "Matlab engine is not started/connected");

      va_list args;
      char *buffer;

      va_start( args, format );

#ifdef _MSC_VER
      const u32 len = _vscprintf( format, args ) + 1;
#else
      const u32 len = 1024;
#endif

      buffer = (char*) malloc( len * sizeof(char) );
      vsnprintf( buffer, len, format, args );

      engEvalString(this->ep, buffer);

      std::string toReturn = std::string(buffer);
      PutString(buffer, (int)toReturn.size(), "lastAnkiCommand");

      EvalString("if length(lastAnkiCommandBuffer)==%d lastAnkiCommandBuffer=lastAnkiCommandBuffer(2:end); end; lastAnkiCommandBuffer{end+1}=lastAnkiCommand;", Matlab::COMMAND_BUFFER_SIZE);

      free( buffer );

      return toReturn;
    }

    std::string Matlab::EvalStringExplicit(const char * buffer)
    {
      AnkiConditionalErrorAndReturnValue(this->ep, "", "Anki.", "Matlab engine is not started/connected");

      engEvalString(this->ep, buffer);

      std::string toReturn = std::string(buffer);

      return toReturn;
    }

    std::string Matlab::EvalStringExplicitEcho(const char * buffer)
    {
      AnkiConditionalErrorAndReturnValue(this->ep, "", "Anki.", "Matlab engine is not started/connected");

      engEvalString(this->ep, buffer);

      std::string toReturn = std::string(buffer);
      PutString(buffer, (int)toReturn.size(), "lastAnkiCommand");

      return toReturn;
    }

    mxArray* Matlab::GetArray(const std::string name)
    {
      AnkiConditionalErrorAndReturnValue(this->ep, NULL, "Anki.", "Matlab engine is not started/connected");

      mxArray *arr = engGetVariable(this->ep, name.data());
      return arr;
    }

    MatlabVariableType Matlab::GetType(const std::string name)
    {
      AnkiConditionalErrorAndReturnValue(this->ep, TYPE_UNKNOWN, "Anki.", "Matlab engine is not started/connected");

      char typeName[TEXT_BUFFER_SIZE];
      snprintf(typeName, TEXT_BUFFER_SIZE, "%s_types", name.data());
      EvalStringEcho("%s=int32([isa(%s, 'int8'), isa(%s, 'u8'), isa(%s, 'int16'), isa(%s, 'u16'), isa(%s, 'int32'), isa(%s, 'u32'), isa(%s, 'int64'), isa(%s, 'u64'), isa(%s, 'single'), isa(%s, 'double')]);", typeName, name.data(), name.data(), name.data(), name.data(), name.data(), name.data(), name.data(), name.data(), name.data(), name.data());
      int *types = Get<s32>(typeName);
      EvalStringEcho("clear %s;", typeName);

      MatlabVariableType type;

      if(types[0] == 1){type=TYPE_INT8;}
      else if(types[1] == 1){type=TYPE_UINT8;}
      else if(types[2] == 1){type=TYPE_INT16;}
      else if(types[3] == 1){type=TYPE_UINT16;}
      else if(types[4] == 1){type=TYPE_INT32;}
      else if(types[5] == 1){type=TYPE_UINT32;}
      else if(types[6] == 1){type=TYPE_INT64;}
      else if(types[7] == 1){type=TYPE_UINT64;}
      else if(types[8] == 1){type=TYPE_SINGLE;}
      else if(types[9] == 1){type=TYPE_DOUBLE;}
      else{type = TYPE_UNKNOWN;}

      types = 0; free(types);

      return type;
    }

#if defined(ANKI_USE_OPENCV)
    Result Matlab::GetCvMat(const CvMat *matrix, std::string name)
    {
      AnkiConditionalErrorAndReturnValue(this->ep, RESULT_FAIL, "Anki.", "Matlab engine is not started/connected");

      AnkiConditionalErrorAndReturnValue(matrix != NULL, RESULT_FAIL, "Error: CvMat is not initialized for %s\n", name.data());

      char tmpName[TEXT_BUFFER_SIZE];
      snprintf(tmpName, TEXT_BUFFER_SIZE, "%s_AnkiTMP", name.data());
      EvalString("%s=%s';", tmpName, name.data());
      mxArray *arrayTmp = GetArray(tmpName);

      mxClassID classId = mxGetClassID(arrayTmp);

      mwSize size = mxGetNumberOfElements(arrayTmp);

      bool mismatch = true;
      if(classId == mxDOUBLE_CLASS) {
        //if(mat->type == CV_64F)
        {
          double *valTmp = (double*)mxGetPr(arrayTmp);
          for(s32 i = 0; i<size; i++) {
            matrix->data.db[i] = valTmp[i];
          }
          mismatch = false;
        }
      }else if(classId == mxSINGLE_CLASS) {
        //if(mat->type == CV_32F)
        {
          float *valTmp = (float*)mxGetPr(arrayTmp);
          for(int i = 0; i<(s32)size; i++) {
            matrix->data.fl[i] = valTmp[i];
          }
          mismatch = false;
        }
      }else if(classId == mxINT16_CLASS) {
        //if(mat->type == CV_16S)
        {
          s16 *valTmp = (s16*)mxGetPr(arrayTmp);
          for(int i = 0; i<(s32)size; i++) {
            matrix->data.s[i] = valTmp[i];
          }
          mismatch = false;
        }
      }else if(classId == mxUINT16_CLASS) {
        //if(mat->type == CV_16U)
        {
          u16 *valTmp = (u16*)mxGetPr(arrayTmp);
          for(int i = 0; i<(s32)size; i++) {
            matrix->data.s[i] = valTmp[i];
          }
          mismatch = false;
        }
      }else if(classId == mxINT32_CLASS) {
        //if(mat->type == CV_32S)
        {
          s32 *valTmp = (s32*)mxGetPr(arrayTmp);
          for(int i = 0; i<(s32)size; i++) {
            matrix->data.i[i] = valTmp[i];
          }
          mismatch = false;
        }
      } else {
        printf("Error: Class ID not supported for %s\n", name.data());
        EvalString("clear %s;", tmpName);
        return -1;
      }

      mxDestroyArray(arrayTmp);

      if(mismatch) {
        printf("Error: Class mismatch for %s\n", name.data());
        EvalString("clear %s;", tmpName);
        return -1;
      }

      EvalString("clear %s;", tmpName);
      return 0;
    }

    //like GetCvMat, but also allocates the CvMat* from the heap
    CvMat* Matlab::GetCvMat(const std::string name)
    {
      CvMat* matrix = 0;

      char tmpName[TEXT_BUFFER_SIZE];
      snprintf(tmpName, TEXT_BUFFER_SIZE, "%s_AnkiTMP", name.data());
      EvalString("%s=%s';", tmpName, name.data());
      mxArray *arrayTmp = GetArray(tmpName);

      mxClassID classId = mxGetClassID(arrayTmp);
      mwSize nDims = mxGetNumberOfDimensions(arrayTmp);

      if(nDims != 2) {
        printf("Error: Matrix %s must be 2D\n", name.data());
        return matrix;
      }

      const mwSize *dims = mxGetDimensions(arrayTmp);
      mwSize size = mxGetNumberOfElements(arrayTmp);

      if(classId == mxDOUBLE_CLASS) {
        double *valTmp = (double*)mxGetPr(arrayTmp);
        matrix = cvCreateMat((int)dims[1], (int)dims[0], CV_64F);
        for(int i = 0; i<(int)size; i++) { matrix->data.db[i] = valTmp[i]; }
      }else if(classId == mxSINGLE_CLASS) {
        float *valTmp = (float*)mxGetPr(arrayTmp);
        matrix = cvCreateMat((int)dims[1], (int)dims[0], CV_32F);
        for(int i = 0; i<(int)size; i++) { matrix->data.fl[i] = valTmp[i]; }
      }else if(classId == mxINT16_CLASS) {
        s16 *valTmp = (s16*)mxGetPr(arrayTmp);
        matrix = cvCreateMat((int)dims[1], (int)dims[0], CV_16S);
        for(int i = 0; i<(int)size; i++) { matrix->data.s[i] = valTmp[i]; }
      }else if(classId == mxUINT16_CLASS) {
        u16 *valTmp = (u16*)mxGetPr(arrayTmp);
        matrix = cvCreateMat((int)dims[1], (int)dims[0], CV_16U);
        for(int i = 0; i<(int)size; i++) {matrix->data.s[i] = valTmp[i]; }
      }else if(classId == mxINT32_CLASS) {
        s32 *valTmp = (s32*)mxGetPr(arrayTmp);
        matrix = cvCreateMat((int)dims[1], (int)dims[0], CV_32S);
        for(int i = 0; i<(int)size; i++) { matrix->data.i[i] = valTmp[i]; }
      } else {
        printf("Error: Class ID not supported for %s\n", name.data());
        EvalString("clear %s;", tmpName);
        return matrix;
      }

      mxDestroyArray(arrayTmp);

      EvalString("clear %s;", tmpName);
      return matrix;
    }

    Result Matlab::PutCvMat(const CvMat *matrix, std::string name)
    {
      AnkiConditionalErrorAndReturnValue(this->ep, RESULT_FAIL, "Anki.", "Matlab engine is not started/connected");

      AnkiConditionalErrorAndReturnValue(matrix != NULL, RESULT_FAIL, "Error: CvMat is not initialized for %s\n", name.data());

      char tmpName[TEXT_BUFFER_SIZE];
      snprintf(tmpName, TEXT_BUFFER_SIZE, "%s_AnkiTMP", name.data());

      bool mismatch = true;
      if((matrix->type&CV_MAT_DEPTH_MASK) == CV_64F) {
        //if(mat->type == CV_64F)
        {
          Put<double>(matrix->data.db, matrix->rows*matrix->cols, tmpName);
          mismatch = false;
        }
      }else if((matrix->type&CV_MAT_DEPTH_MASK) == CV_32F) {
        //if(mat->type == CV_32F)
        {
          Put<float>(matrix->data.fl, matrix->rows*matrix->cols, tmpName);
          mismatch = false;
        }
      }else if((matrix->type&CV_MAT_DEPTH_MASK) == CV_16S) {
        //if(mat->type == CV_16S)
        {
          Put<s16>(matrix->data.s, matrix->rows*matrix->cols, tmpName);
          mismatch = false;
        }
      }else if((matrix->type&CV_MAT_DEPTH_MASK) == CV_16U) {
        //if(mat->type == CV_16U)
        {
          Put<u16>((const unsigned short*)matrix->data.s, matrix->rows*matrix->cols, tmpName);
          mismatch = false;
        }
      }else if((matrix->type&CV_MAT_DEPTH_MASK) == CV_32S) {
        //if(mat->type == CV_32S)
        {
          Put<s32>(matrix->data.i, matrix->rows*matrix->cols, tmpName);
          mismatch = false;
        }
      } else {
        printf("Error: Class ID not supported for %s\n", name.data());
        EvalString("clear %s;", tmpName);
        return -1;
      }

      if(mismatch) {
        printf("Error: Class mismatch for %s\n", name.data());
        EvalString("clear %s;", tmpName);
        return -1;
      }

      EvalString("%s=reshape(%s, [%d, %d])';", name.data(), tmpName, matrix->cols, matrix->rows);
      EvalString("clear %s;", tmpName);

      return 0;
    }
#endif // #if defined(ANKI_USE_OPENCV)

    Result Matlab::PutString(const char * characters, s32 nValues, const std::string name)
    {
      AnkiConditionalErrorAndReturnValue(this->ep, RESULT_FAIL, "Anki.", "Matlab engine is not started/connected");

      Result returnVal = Put<s8>((s8*)characters, nValues, name);
      EvalString("%s=char(%s');", name.data(), name.data());

      return returnVal;
    }

    s32 Matlab::SetVisible(bool isVisible)
    {
      AnkiConditionalErrorAndReturnValue(this->ep, -1, "Anki.", "Matlab engine is not started/connected");

      s32 returnVal = engSetVisible(this->ep, isVisible);

      return returnVal;
    }

    bool Matlab::DoesVariableExist(const std::string name)
    {
      AnkiConditionalErrorAndReturnValue(this->ep, "", "Anki.", "Matlab engine is not started/connected");

      EvalString("ans=exist('%s', 'var');", name.data());

      double *ans = Get<double>("ans");
      double ansVal = *ans;
      free(ans); ans = 0;
      if(ansVal<.5)
        return false;
      else
        return true;
    }

    template<> Result Matlab::Put<Point<s16> >(const Point<s16> * values, s32 nValues, const std::string name)
    {
      AnkiConditionalErrorAndReturnValue(this->ep, RESULT_FAIL, "Anki.Put", "Matlab engine is not started/connected");

      const mwSize dims[2] = {2, static_cast<mwSize>(nValues)};
      const mxClassID matlabType = getMatlabClassID<s16>();
      mxArray *arrayTmp = mxCreateNumericArray(2, &dims[0], matlabType, mxREAL);
      s16 *matlabBufferTmp = (s16*) mxGetPr(arrayTmp);
      for(s32 i = 0, ci=0; i<nValues; i++) {
        matlabBufferTmp[ci++] = values[i].x;
        matlabBufferTmp[ci++] = values[i].y;
      }
      engPutVariable(ep, name.data(), arrayTmp);
      mxDestroyArray(arrayTmp);

      return RESULT_OK;
    }

#endif // #if ANKICORETECH_EMBEDDED_USE_MATLAB
  } // namespace Embedded
} // namespace Anki