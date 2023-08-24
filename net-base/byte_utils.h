// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_BASE_BYTE_UTILS_H_
#define NET_BASE_BYTE_UTILS_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <base/containers/span.h>

#include "net-base/export.h"

namespace net_base::byte_utils {

// Converts a plain old data (e.g. uint32_t, struct) to a byte buffer stored
// in host order.
template <typename T>
std::vector<uint8_t> ToBytes(const T& val) {
  static_assert(std::is_pod<T>::value);

  return {reinterpret_cast<const uint8_t*>(&val),
          reinterpret_cast<const uint8_t*>(&val) + sizeof(T)};
}

// Converts the byte buffer stored in host order to a plain old data.
// Returns std::nullopt if the buffer size is not the size of the type.
template <typename T>
std::optional<T> FromBytes(base::span<const uint8_t> bytes) {
  static_assert(std::is_pod<T>::value);

  if (bytes.size() != sizeof(T)) {
    return std::nullopt;
  }

  T val;
  memcpy(&val, bytes.data(), sizeof(T));
  return val;
}

// Gets the view of immutable byte buffer in host order from an immutable plain
// old data (e.g. uint32_t, struct).
template <typename T>
base::span<const uint8_t> AsBytes(const T& val) {
  static_assert(std::is_pod<T>::value);
  return {reinterpret_cast<const uint8_t*>(&val), sizeof(val)};
}

// Gets the view of the mutable byte buffer in host order from a mutable plain
// old data (e.g. uint32_t, struct).
template <typename T>
base::span<uint8_t> AsMutBytes(T& val) {
  static_assert(std::is_pod<T>::value);
  return {reinterpret_cast<uint8_t*>(&val), sizeof(val)};
}

// Converts a string to a byte buffer with the trailing null character. If the
// null character exists inside the string, then only the characters before the
// null character will be copied to the buffer. e.g.
// std::string("ab")       => {'a', 'b', '\0'}
// std::string("ab\0", 3)  => {'a', 'b', '\0'}
// std::string("ab\0c", 4) => {'a', 'b', '\0'}
NET_BASE_EXPORT std::vector<uint8_t> StringToCStringBytes(std::string_view str);

// Converts a byte buffer to a std::string copying all bytes until a null
// character is found or until the end of the buffer. The returned string
// doesn't contain a null character inside the std::string's data. e.g.
// {'a', 'b'}            => std::string("ab")
// {'a', 'b', '\0'}      => std::string("ab")
// {'a', 'b', '\0', 'c'} => std::string("ab")
NET_BASE_EXPORT std::string StringFromCStringBytes(
    base::span<const uint8_t> bytes);

// Converts a string to a byte buffer. The size of the returned byte buffer is
// the same as the input string, even if the input string contains null
// character. e.g.
// std::string("abc", 3)    => {'a', 'b', 'c'}
// std::string("abc\0", 4)  => {'a', 'b', 'c', '\0'}
// std::string("abc\0d", 5) => {'a', 'b', 'c', '\0', 'd'}
NET_BASE_EXPORT std::vector<uint8_t> ByteStringToBytes(std::string_view bytes);

// Converts a byte buffer to a std:string. The size of the returned string is
// the same as the input byte buffer, even if the buffer contains null
// character. e.g.
// {'a', 'b', 'c'}            => std::string("abc", 3)
// {'a', 'b', 'c', '\0'}      => std::string("abc\0", 4)
// {'a', 'b', 'c', '\0', 'd'} => std::string("abc\0d", 5)
NET_BASE_EXPORT std::string ByteStringFromBytes(
    base::span<const uint8_t> bytes);

}  // namespace net_base::byte_utils
#endif  // NET_BASE_BYTE_UTILS_H_
