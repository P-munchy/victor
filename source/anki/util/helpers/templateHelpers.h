/**
 * File: templateHelpers.h
 *
 * Author: raul
 * Created: 05/02/2014
 *
 * Description: Some helper templates for general usage.
 *
 * Copyright: Anki, Inc. 2013
 *
 **/

#ifndef UTIL_TEMPLATE_HELPERS_H_
#define UTIL_TEMPLATE_HELPERS_H_

#include <cstddef>
#include <type_traits>

namespace Anki{ namespace Util {

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// SafeDelete: deletes and sets pointer to null
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

template<class T>
inline void SafeDelete(T* &pointerRef) {
  delete pointerRef;  // c++ does not require checking that !=0 (delete 0 is ok)
  pointerRef = nullptr;
}

template<class T>
inline void SafeDeleteArray(T* &pointerRef) {
  delete[] pointerRef;  // c++ does not require checking that !=0 (delete 0 is ok)
  pointerRef = nullptr;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// EnumHasher: allows using enums and enum classes in containers that require std::hash, such as unordered_map
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct EnumHasher
{
  template<typename EnumType>
  size_t operator()(EnumType e) const {
    return static_cast<std::size_t>( e );
  }
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// EnumToUnderlying: automatically cast to the underlying type of an enum class
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

template<typename EnumClass>
constexpr auto EnumToUnderlying(EnumClass e) -> typename std::underlying_type<EnumClass>::type
{
   return static_cast<typename std::underlying_type<EnumClass>::type>(e);
}

}; // namespace
}; // namespace

#endif