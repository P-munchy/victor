/**
File: serialize.h
Author: Peter Barnum
Created: 2013

Definitions for serialize_declarations.h

Copyright Anki, Inc. 2013
For internal use only. No part of this code may be used without a signed non-disclosure agreement with Anki, inc.
**/

#ifndef _ANKICORETECHEMBEDDED_COMMON_SERIALIZE_H_
#define _ANKICORETECHEMBEDDED_COMMON_SERIALIZE_H_

#include "anki/common/robot/serialize_declarations.h"
#include "anki/common/robot/flags.h"
#include "anki/common/robot/array2d.h"

namespace Anki
{
  namespace Embedded
  {
    // #pragma mark

    template<typename Type> Result SerializedBuffer::EncodedBasicTypeBuffer::Serialize(const bool updateBufferPointer, const s32 numElements, void ** buffer, s32 &bufferLength)
    {
      if(bufferLength < SerializedBuffer::EncodedBasicTypeBuffer::CODE_LENGTH) {
        return RESULT_FAIL_OUT_OF_MEMORY;
      }

      u32 firstElement = 0;

      if(Flags::TypeCharacteristics<Type>::isBasicType) {
        firstElement |= 1;
      }

      if(Flags::TypeCharacteristics<Type>::isInteger) {
        firstElement |= 2;
      }

      if(Flags::TypeCharacteristics<Type>::isSigned) {
        firstElement |= 4;
      }

      if(Flags::TypeCharacteristics<Type>::isFloat) {
        firstElement |= 8;
      }

      firstElement |= sizeof(Type) << 16;

      reinterpret_cast<u32*>(*buffer)[0] = firstElement;
      reinterpret_cast<u32*>(*buffer)[1] = numElements;

      if(updateBufferPointer) {
        *buffer = reinterpret_cast<u8*>(*buffer) + SerializedBuffer::EncodedBasicTypeBuffer::CODE_LENGTH;
        bufferLength -= SerializedBuffer::EncodedBasicTypeBuffer::CODE_LENGTH;;
      }

      return RESULT_OK;
    }

    template<typename Type> Result SerializedBuffer::EncodedArray::Serialize(const bool updateBufferPointer, const Array<Type> &in, void ** buffer, s32 &bufferLength)
    {
      if(bufferLength < SerializedBuffer::EncodedArray::CODE_LENGTH) {
        return RESULT_FAIL_OUT_OF_MEMORY;
      }

      AnkiConditionalErrorAndReturnValue(in.IsValid(),
        RESULT_FAIL, "SerializedBuffer::EncodedArray", "in Array is invalid");

      const s32 numElements = in.get_size(0) * in.get_size(1);

      EncodedBasicTypeBuffer::Serialize<Type>(false, numElements, buffer, bufferLength);

      reinterpret_cast<u32*>(*buffer)[2] = in.get_size(0);
      reinterpret_cast<u32*>(*buffer)[3] = in.get_size(1);
      reinterpret_cast<u32*>(*buffer)[4] = in.get_stride();
      reinterpret_cast<u32*>(*buffer)[5] = in.get_flags().get_rawFlags();

      if(updateBufferPointer) {
        *buffer = reinterpret_cast<u8*>(*buffer) + SerializedBuffer::EncodedArray::CODE_LENGTH;
        bufferLength -= SerializedBuffer::EncodedArray::CODE_LENGTH;
      }

      return RESULT_OK;
    }

    template<typename Type> Result SerializedBuffer::EncodedArraySlice::Serialize(const bool updateBufferPointer, const ConstArraySlice<Type> &in, void ** buffer, s32 &bufferLength)
    {
      if(bufferLength < SerializedBuffer::EncodedArraySlice::CODE_LENGTH) {
        return RESULT_FAIL_OUT_OF_MEMORY;
      }

      AnkiConditionalErrorAndReturnValue(in.get_array().IsValid(),
        RESULT_FAIL, "SerializedBuffer::EncodedArraySlice", "in Array is invalid");

      const LinearSequence<s32>& ySlice = in.get_ySlice();
      const LinearSequence<s32>& xSlice = in.get_xSlice();

      const s32 yStart = ySlice.get_start();
      const s32 yIncrement = ySlice.get_increment();
      const s32 yEnd = ySlice.get_end();
      const s32 ySize = ySlice.get_size();

      const s32 xStart = xSlice.get_start();
      const s32 xIncrement = xSlice.get_increment();
      const s32 xEnd = xSlice.get_end();
      const s32 xSize = xSlice.get_size();

      EncodedArray::Serialize<Type>(false, in.get_array(), buffer, bufferLength);

      reinterpret_cast<u32*>(*buffer)[6] = *reinterpret_cast<const u32*>(&yStart);
      reinterpret_cast<u32*>(*buffer)[7] = *reinterpret_cast<const u32*>(&yIncrement);
      reinterpret_cast<u32*>(*buffer)[8] = *reinterpret_cast<const u32*>(&yEnd);

      reinterpret_cast<u32*>(*buffer)[9] = *reinterpret_cast<const u32*>(&xStart);
      reinterpret_cast<u32*>(*buffer)[10] = *reinterpret_cast<const u32*>(&xIncrement);
      reinterpret_cast<u32*>(*buffer)[11]= *reinterpret_cast<const u32*>(&xEnd);

      if(updateBufferPointer) {
        *buffer = reinterpret_cast<u8*>(*buffer) + SerializedBuffer::EncodedArraySlice::CODE_LENGTH;
        bufferLength -= SerializedBuffer::EncodedArraySlice::CODE_LENGTH;
      }

      return RESULT_OK;
    }

    template<typename Type> Result SerializedBuffer::SerializeRawBasicType(const char *objectName, const Type &in, void ** buffer, s32 &bufferLength)
    {
      return SerializeRawBasicType(objectName, &in, 1, buffer, bufferLength);
    }

    template<typename Type> Result SerializedBuffer::SerializeRawBasicType(const char *objectName, const Type *in, const s32 numElements, void ** buffer, s32 &bufferLength)
    {
      if(SerializeDescriptionStrings("Basic Type Buffer", objectName, buffer, bufferLength) != RESULT_OK)
        return RESULT_FAIL;

      SerializedBuffer::EncodedBasicTypeBuffer::Serialize<Type>(true, numElements, buffer, bufferLength);

      memcpy(*buffer, in, numElements*sizeof(Type));

      *buffer = reinterpret_cast<u8*>(*buffer) + numElements*sizeof(Type);
      bufferLength -= numElements*sizeof(Type);

      return RESULT_OK;
    }

    template<typename Type> Result SerializedBuffer::SerializeRawArray(const char *objectName, const Array<Type> &in, void ** buffer, s32 &bufferLength)
    {
      AnkiConditionalErrorAndReturnValue(in.IsValid(),
        RESULT_FAIL, "SerializedBuffer::SerializeRawArraySlice", "in ArraySlice is not Valid");

      if(SerializeDescriptionStrings("Array", objectName, buffer, bufferLength) != RESULT_OK)
        return RESULT_FAIL;

      SerializedBuffer::EncodedArray::Serialize<Type>(true, in, buffer, bufferLength);

      memcpy(*buffer, in.Pointer(0,0), in.get_stride()*in.get_size(0));

      *buffer = reinterpret_cast<u8*>(*buffer) + in.get_stride()*in.get_size(0);
      bufferLength -= in.get_stride()*in.get_size(0);

      return RESULT_OK;
    }

    template<typename Type> Result SerializedBuffer::SerializeRawArraySlice(const char *objectName, const ConstArraySlice<Type> &in, void ** buffer, s32 &bufferLength)
    {
      AnkiConditionalErrorAndReturnValue(in.get_array().IsValid(),
        RESULT_FAIL, "SerializedBuffer::SerializeRawArraySlice", "in ArraySlice is not Valid");

      if(SerializeDescriptionStrings("ArraySlice", objectName, buffer, bufferLength) != RESULT_OK)
        return RESULT_FAIL;

      // NOTE: these parameters are the size that will be transmitted, not the original size
      const u32 height = in.get_ySlice().get_size();
      const u32 width = in.get_xSlice().get_size();
      const u32 stride = width*sizeof(Type);

      const s32 numRequiredBytes = height*stride + SerializedBuffer::EncodedArraySlice::CODE_LENGTH;

      AnkiConditionalErrorAndReturnValue(bufferLength >= numRequiredBytes,
        RESULT_FAIL, "SerializedBuffer::SerializeRawArraySlice", "buffer needs at least %d bytes", numRequiredBytes);

      SerializedBuffer::EncodedArraySlice::Serialize<Type>(true, in, buffer, bufferLength);

      // TODO: this could be done more efficiently
      Type * restrict pDataType = reinterpret_cast<Type*>(*buffer);
      s32 iData = 0;

      const LinearSequence<s32>& ySlice = in.get_ySlice();
      const LinearSequence<s32>& xSlice = in.get_xSlice();

      const s32 yStart = ySlice.get_start();
      const s32 yIncrement = ySlice.get_increment();
      const s32 yEnd = ySlice.get_end();

      const s32 xStart = xSlice.get_start();
      const s32 xIncrement = xSlice.get_increment();
      const s32 xEnd = xSlice.get_end();

      for(s32 y=yStart; y<=yEnd; y+=yIncrement) {
        const Type * restrict pInData = in.get_array().Pointer(y, 0);
        for(s32 x=xStart; x<=xEnd; x+=xIncrement) {
          pDataType[iData] = pInData[x];
          iData++;
        }
      }

      AnkiAssert(iData == stride*height);

      *buffer = reinterpret_cast<u8*>(*buffer) + stride*height;
      bufferLength -= stride*height;

      return RESULT_OK;
    }

    template<typename Type> Result SerializedBuffer::SerializeRawFixedLengthList(const char *objectName, const FixedLengthList<Type> &in, void ** buffer, s32 &bufferLength)
    {
      return SerializeRawArraySlice<Type>(objectName, *static_cast<const ConstArraySlice<Type>*>(&in), buffer, bufferLength);
    }

    template<typename Type> Type SerializedBuffer::DeserializeRawBasicType(char *objectName, void ** buffer, s32 &bufferLength)
    {
      // TODO: check if description is valid
      DeserializeDescriptionStrings(NULL, objectName, buffer, bufferLength);

      // TODO: check if encoded type is valid
      u16 size;
      bool isBasicType;
      bool isInteger;
      bool isSigned;
      bool isFloat;
      s32 numElements;
      EncodedBasicTypeBuffer::Deserialize(true, size, isBasicType, isInteger, isSigned, isFloat, numElements, buffer, bufferLength);

      const Type var = *reinterpret_cast<Type*>(*buffer);

      // Hack, to prevent corrupted input from causing a memory access error
      if(size > 256 || numElements <= 0 || numElements >= 1000000) {
        return var;
      }

      *buffer = reinterpret_cast<u8*>(*buffer) + sizeof(Type)*numElements;
      bufferLength -= sizeof(Type)*numElements;

      return var;
    }

    template<typename Type> Type* SerializedBuffer::DeserializeRawBasicType(char *objectName, void ** buffer, s32 &bufferLength, MemoryStack &memory)
    {
      // TODO: check if description is valid
      DeserializeDescriptionStrings(NULL, objectName, buffer, bufferLength);

      // TODO: check if encoded type is valid
      u16 size;
      bool isBasicType;
      bool isInteger;
      bool isSigned;
      bool isFloat;
      s32 numElements;
      EncodedBasicTypeBuffer::Deserialize(true, size, isBasicType, isInteger, isSigned, isFloat, numElements, buffer, bufferLength);

      AnkiConditionalErrorAndReturnValue(numElements > 0 && numElements < 1000000,
        NULL, "SerializedBuffer::DeserializeRawBasicType", "numElements is not reasonable");

      const s32 numBytes = numElements*sizeof(Type);
      Type *var = reinterpret_cast<Type*>( memory.Allocate(numBytes) );

      memcpy(var, *buffer, numBytes);

      *buffer = reinterpret_cast<u8*>(*buffer) + numBytes;
      bufferLength -= numBytes;

      return var;
    }

    template<typename Type> Array<Type> SerializedBuffer::DeserializeRawArray(char *objectName, void ** buffer, s32 &bufferLength, MemoryStack &memory)
    {
      // TODO: check if description is valid
      DeserializeDescriptionStrings(NULL, objectName, buffer, bufferLength);

      // TODO: check if encoded type is valid
      s32 height;
      s32 width;
      s32 stride;
      Flags::Buffer flags;
      u16 basicType_size;
      bool basicType_isBasicType;
      bool basicType_isInteger;
      bool basicType_isSigned;
      bool basicType_isFloat;
      s32 basicType_numElements;
      EncodedArray::Deserialize(true, height, width, stride, flags, basicType_size, basicType_isBasicType, basicType_isInteger, basicType_isSigned, basicType_isFloat, basicType_numElements, buffer, bufferLength);

      AnkiConditionalErrorAndReturnValue(stride == RoundUp(width*sizeof(Type), MEMORY_ALIGNMENT),
        Array<Type>(), "SerializedBuffer::DeserializeRawArray", "Parsed stride is not reasonable");

      AnkiConditionalErrorAndReturnValue(bufferLength >= (height*stride),
        Array<Type>(), "SerializedBuffer::DeserializeRawArray", "Not enought bytes left to set the array");

      Array<Type> out = Array<Type>(height, width, memory);

      memcpy(out.Pointer(0,0), *buffer, height*stride);

      *buffer = reinterpret_cast<u8*>(*buffer) + height*stride;
      bufferLength -= height*stride;

      return out;
    }

    template<typename Type> ArraySlice<Type> SerializedBuffer::DeserializeRawArraySlice(char *objectName, void ** buffer, s32 &bufferLength, MemoryStack &memory)
    {
      // TODO: check if description is valid
      DeserializeDescriptionStrings(NULL, objectName, buffer, bufferLength);

      // TODO: check if encoded type is valid
      s32 height;
      s32 width;
      s32 stride;
      Flags::Buffer flags;
      s32 ySlice_start;
      s32 ySlice_increment;
      s32 ySlice_end;
      s32 xSlice_start;
      s32 xSlice_increment;
      s32 xSlice_end;
      u16 basicType_size;
      bool basicType_isBasicType;
      bool basicType_isInteger;
      bool basicType_isSigned;
      bool basicType_isFloat;
      s32 basicType_numElements;
      EncodedArraySlice::Deserialize(true, height, width, stride, flags, ySlice_start, ySlice_increment, ySlice_end, xSlice_start, xSlice_increment, xSlice_end, basicType_size, basicType_isBasicType, basicType_isInteger, basicType_isSigned, basicType_isFloat, basicType_numElements, buffer, bufferLength);

      const LinearSequence<s32> ySlice(ySlice_start, ySlice_increment, ySlice_end);
      const LinearSequence<s32> xSlice(xSlice_start, xSlice_increment, xSlice_end);

      AnkiConditionalErrorAndReturnValue(stride == RoundUp(width*sizeof(Type), MEMORY_ALIGNMENT),
        ArraySlice<Type>(), "SerializedBuffer::DeserializeRawArraySlice", "Parsed stride is not reasonable");

      AnkiConditionalErrorAndReturnValue(bufferLength >= static_cast<s32>(xSlice.get_size()*ySlice.get_size()*sizeof(Type)),
        ArraySlice<Type>(), "SerializedBuffer::DeserializeRawArraySlice", "Not enought bytes left to set the array");

      Array<Type> array(height, width, memory);

      // TODO: this could be done more efficiently

      Type * restrict pDataType = reinterpret_cast<Type*>(*buffer);
      s32 iData = 0;

      for(s32 y=ySlice_start; y<=ySlice_end; y+=ySlice_increment) {
        Type * restrict pArrayData = array.Pointer(y, 0);
        for(s32 x=xSlice_start; x<=xSlice_end; x+=xSlice_increment) {
          pArrayData[x] = pDataType[iData];
          iData++;
        }
      }

      const s32 numElements = xSlice.get_size()*ySlice.get_size();

      AnkiAssert(iData == numElements);

      ArraySlice<Type> out = ArraySlice<Type>(array, ySlice, xSlice);

      *buffer = reinterpret_cast<u8*>(*buffer) + numElements*sizeof(Type);
      bufferLength -= numElements*sizeof(Type);

      return out;
    }

    template<typename Type> FixedLengthList<Type> SerializedBuffer::DeserializeRawFixedLengthList(char *objectName, void ** buffer, s32 &bufferLength, MemoryStack &memory)
    {
      ArraySlice<Type> arraySlice = SerializedBuffer::DeserializeRawArraySlice<Type>(objectName, buffer, bufferLength, memory);

      if(!arraySlice.get_array().IsValid())
        return FixedLengthList<Type>();

      FixedLengthList<Type> out;

      out.ySlice = arraySlice.get_ySlice();
      out.xSlice = arraySlice.get_xSlice();
      out.array = arraySlice.get_array();
      out.arrayData = out.array.Pointer(0,0);;

      return out;
    }

    template<typename Type> void* SerializedBuffer::PushBack(const char *objectName, const Type *data, const s32 numElements)
    {
      s32 totalDataLength = numElements*sizeof(Type) + SerializedBuffer::EncodedBasicTypeBuffer::CODE_LENGTH + 2*SerializedBuffer::DESCRIPTION_STRING_LENGTH;

      void * const segmentStart = Allocate("Basic Type Buffer", objectName, totalDataLength);
      void * segment = reinterpret_cast<u8*>(segmentStart);

      AnkiConditionalErrorAndReturnValue(segment != NULL,
        NULL, "SerializedBuffer::PushBack", "Could not add data");

      SerializeRawBasicType<Type>(objectName, data, numElements, &segment, totalDataLength);

      return segmentStart;
    }

    template<typename Type> void* SerializedBuffer::PushBack(const char *objectName, const Array<Type> &in)
    {
      s32 totalDataLength = in.get_stride() * in.get_size(1) + SerializedBuffer::EncodedArray::CODE_LENGTH + 2*SerializedBuffer::DESCRIPTION_STRING_LENGTH;

      void * const segmentStart = Allocate("Array", objectName, totalDataLength);
      void * segment = segmentStart;

      AnkiConditionalErrorAndReturnValue(segment != NULL,
        NULL, "SerializedBuffer::PushBack", "Could not add data");

      SerializeRawArray<Type>(objectName, in, &segment, totalDataLength);

      return segmentStart;
    }

    template<typename Type> void* SerializedBuffer::PushBack(const char *objectName, const ArraySlice<Type> &in)
    {
      const u32 height = in.get_ySlice().get_size();
      const u32 width = in.get_xSlice().get_size();
      const u32 stride = width*sizeof(Type);

      s32 totalDataLength = height * stride + SerializedBuffer::EncodedArraySlice::CODE_LENGTH + 2*SerializedBuffer::DESCRIPTION_STRING_LENGTH;

      void * const segmentStart = Allocate("ArraySlice", objectName, totalDataLength);
      void * segment = segmentStart;

      AnkiConditionalErrorAndReturnValue(segment != NULL,
        NULL, "SerializedBuffer::PushBack", "Could not add data");

      SerializeRawArraySlice<Type>(objectName, in, &segment, totalDataLength);

      return segmentStart;
    }
  } // namespace Embedded
} //namespace Anki

#endif // #ifndef _ANKICORETECHEMBEDDED_COMMON_SERIALIZE_H_
