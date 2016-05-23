/**
 * File: circularBuffer
 *
 * Author: Mark Wesley
 * Created: 10/19/15
 *
 * Description: A templated circular buffer of elements, backed by a std::vector
 *
 * Copyright: Anki, Inc. 2015
 *
 **/


#ifndef __Util_Container_CircularBuffer_H__
#define __Util_Container_CircularBuffer_H__


#include <assert.h>
#include <stdint.h>
#include <vector>


namespace Anki {
namespace Util {

  
template <class T>
class CircularBuffer
{
public:
  
  explicit CircularBuffer(size_t capacity=0)  { Reset(capacity); }
  ~CircularBuffer() {}
  
  CircularBuffer(const CircularBuffer& other) = default;
  CircularBuffer& operator=(const CircularBuffer& other) = default;
  
  CircularBuffer(CircularBuffer&& rhs) noexcept
    : _buffer(     std::move(rhs._buffer) )
    , _firstIndex( std::move(rhs._firstIndex) )
    , _numEntries( std::move(rhs._numEntries) )
    , _capacity(   std::move(rhs._capacity) )
  {
    // rhs._buffer is likely now empty (size and capacity == 0), but this not guarenteed by the C++ standard
    // we only need to reset the following to ensure the buffer is still in a valid and reliable state (empty with no capacity)
    rhs._firstIndex = 0;
    rhs._numEntries = 0;
    rhs._capacity   = 0;
  }
  
  CircularBuffer& operator=(CircularBuffer&& rhs) noexcept
  {
    if (this != &rhs)
    {
      _buffer     = std::move(rhs._buffer);
      _firstIndex = std::move(rhs._firstIndex);
      _numEntries = std::move(rhs._numEntries);
      _capacity   = std::move(rhs._capacity);

      // rhs._buffer is likely now empty (size and capacity == 0), but this not guarenteed by the C++ standard
      // we only need to reset the following to ensure the buffer is still in a valid and reliable state (empty with no capacity)
      rhs._firstIndex = 0;
      rhs._numEntries = 0;
      rhs._capacity   = 0;
    }
    return *this;
  }
  
  bool operator==(const Anki::Util::CircularBuffer<T>& rhs) const
  {

    if (size() != rhs.size()) {
      return false;
    }
    
    for (size_t idx = 0; idx < size(); ++idx) {
      if ((*this)[idx] != rhs[idx]) {
        return false;
      }
    }
    
    return true;
  }
  
  bool operator!=(const Anki::Util::CircularBuffer<T>& rhs) const
  {
    return !(*this == rhs);
  }

  
  void Reset(size_t capacity)
  {
    _buffer.clear();
    _buffer.reserve(capacity);
    _buffer.resize(capacity);
    _firstIndex = 0;
    _numEntries = 0;
    _capacity   = capacity;
  }
  
  void clear() { Reset(_capacity); }
  
  void pop_front()
  {
    // Remove oldest entry
    
    assert(_numEntries > 0);
    
    ++_firstIndex;
    --_numEntries;
    if (_firstIndex >= _capacity)
    {
      _firstIndex = 0;
    }
  }
  
  void pop_front( size_t popCount )
  {
    assert(_numEntries >= popCount);
    
    _firstIndex = ( _firstIndex + popCount ) % _capacity;
    _numEntries -= popCount;
  }
  
  void pop_back()
  {
    // Remove newest entry
    
    assert(_numEntries > 0);
    --_numEntries;
  }
  
  void push_front(const T& newEntry)
  {
    assert(_numEntries <= _capacity);
    if (_numEntries >= _capacity)
    {
      pop_back();
    }
    
    _firstIndex = (_firstIndex > 0) ? (_firstIndex - 1) : (_capacity - 1);
    
    const size_t newIndex = _firstIndex;

    _buffer[newIndex] = newEntry;
    ++_numEntries;
  }
  
  void push_back(const T& newEntry)
  {
    assert(_numEntries <= _capacity);
    if (_numEntries >= _capacity)
    {
      pop_front();
    }
    
    const size_t newIndex = ItemIndexToBufferIndex(_numEntries);

    _buffer[newIndex] = newEntry;
    ++_numEntries;
  }
  
  void push_back(const T* newEntryArray, size_t arraySize)
  {
    assert(arraySize <= _capacity);
    assert(_numEntries <= _capacity);
    
    const size_t newIndex = ItemIndexToBufferIndex(_numEntries % _capacity);
    // If the array is larger then the
    const size_t overflowSize = (newIndex + arraySize) % _capacity;
    // Pop entries from the front to make room for new entites
    if (_numEntries + arraySize >= _capacity) {
      _firstIndex = overflowSize;
    }
    
    // Check if the new entries can be added continuous memory in the buffer
    if (newIndex + arraySize <= _capacity) {
      // Copy entire array into buffer
      memcpy(&_buffer[newIndex], newEntryArray, sizeof(T) * arraySize);
    }
    else {
      // Copy 1st segment into buffer
      const size_t firstSegSize = arraySize - overflowSize;
      memcpy(&(_buffer[newIndex]), newEntryArray, firstSegSize * sizeof(T));
      // Copy 2nd segment into the front of the buffer
      memcpy(&(_buffer[0]), &newEntryArray[firstSegSize], overflowSize * sizeof(T));
    }
    _numEntries = std::min((_numEntries + arraySize), _capacity);
  }
  
  size_t size() const     { return _numEntries; }
  size_t capacity() const { return _capacity; }
  bool   empty() const    { return (_numEntries == 0); }
  
  const T& operator[](size_t itemIndex) const
  {
    assert(itemIndex < _numEntries);
    const size_t bufferIndex = ItemIndexToBufferIndex(itemIndex);
    return _buffer[bufferIndex];
  }
  
  T& operator[](size_t itemIndex)
  {
    assert(itemIndex < _numEntries);
    const size_t bufferIndex = ItemIndexToBufferIndex(itemIndex);
    return _buffer[bufferIndex];
  }
  
  const T& front() const
  {
    return (*this)[0];
  }
  
  T& front()
  {
    return (*this)[0];
  }
  
  size_t front( T* outArray, size_t arraySize )
  {
    assert(_numEntries >= arraySize);
    const size_t availableSize = std::min(arraySize, _numEntries);
    // Check if the data is in continuous memory
    if (_firstIndex + availableSize <= _capacity)
    {
      memcpy(outArray, &_buffer[_firstIndex], availableSize * sizeof(T));
    }
    else {
      const size_t overflowSize = (_firstIndex + availableSize) % _capacity;
      const size_t firstSegSize = availableSize - overflowSize;
      memcpy(outArray, &_buffer[_firstIndex], firstSegSize * sizeof(T));
      memcpy(&outArray[firstSegSize], &_buffer[0], overflowSize * sizeof(T));
    }
    return availableSize;
  }
  
  const T& back() const
  {
    return (*this)[size()-1];
  }
  
  T& back()
  {
    return (*this)[size()-1];
  }
  
private:
  
  size_t ItemIndexToBufferIndex(size_t itemIndex) const
  {
    assert(itemIndex < _capacity);
    const size_t offsetIndex = _firstIndex + itemIndex;
    const size_t bufferIndex = offsetIndex % _capacity;
    return bufferIndex;
  }
  
  std::vector<T>  _buffer;
  size_t          _firstIndex;
  size_t          _numEntries;
  size_t          _capacity; // not necessarily the same as _buffer.capacity() (e.g. if reduced, vector doesn't have to shrink)
};
  

} // end namespace Util
} // end namespace Anki

#endif // __Util_Container_CircularBuffer_H__
