// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBIPP_IPP_ENCODING_H_
#define LIBIPP_IPP_ENCODING_H_

#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

// Internal constants and functions used during parsing & building IPP frames.
// You probably do not want to use it directly. See ipp.h for information how to
// use this library.

namespace ipp {

// Some constants from the IPP specification [rfc8010] (section 3.2).
// begin-attribute-group-tag is from the range 0x00-0x0f without 0x03.
// value-tag is from the range 0x10-0xff.
constexpr uint8_t end_of_attributes_tag = 0x03;
constexpr uint8_t max_begin_attribute_group_tag = 0x0f;
constexpr uint8_t begCollection_value_tag = 0x34;
constexpr uint8_t endCollection_value_tag = 0x37;
constexpr uint8_t memberAttrName_value_tag = 0x4a;

// Helper to match signed integer type basing on number of bytes.
template <size_t BytesCount>
struct IntegerBySize;
template <>
struct IntegerBySize<1> {
  typedef int8_t type;
};
template <>
struct IntegerBySize<2> {
  typedef int16_t type;
};
template <>
struct IntegerBySize<4> {
  typedef int32_t type;
};

// Helper for reading unsigned integer from given address.
template <typename UnsignedInt>
inline UnsignedInt ReadAsBytes(const uint8_t* ptr) {
  UnsignedInt uval = 0;
  for (auto ptr_end = ptr + sizeof(UnsignedInt); ptr < ptr_end; ++ptr) {
    uval <<= 8;
    uval += *ptr;
  }
  return uval;
}
template <>
inline uint8_t ReadAsBytes<uint8_t>(const uint8_t* ptr) {
  return *ptr;
}

// Reads signed integer saved on sizeof(Integer) bytes at position ptr with
// two's-complement binary encoding and returns it.
// The following types can be set as Integer: int8_t, int16_t, int32_t, int64_t.
// Input parameter cannot be nullptr.
template <typename Integer>
Integer ReadInteger(const uint8_t* const ptr) {
  // given type must by signed integer
  static_assert(std::is_integral<Integer>::value, "integral expected");
  static_assert(std::is_signed<Integer>::value, "signed integral expected");
  // finds corresponding unsigned type
  typedef typename std::make_unsigned<Integer>::type UnsignedInteger;
  // parse two's-complement binary encoding
  UnsignedInteger uval = ReadAsBytes<UnsignedInteger>(ptr);
  if ((*ptr >> 7) == 0) {
    // first bit = 0: positive value or zero
    return static_cast<Integer>(uval);
  } else if ((uval << 1) == 0) {
    // first bit = 1 and the rest are zeroes: minimal possible value
    return std::numeric_limits<Integer>::min();
  } else {
    // first bit = 1 and at least one other 1: decode from two complement
    // conversion
    --uval;
    return -(static_cast<Integer>(~uval));
  }
}

// Parses signed integer saved on BytesCount bytes at position ptr and saves it
// to given variable. The pointer ptr is shifted by BytesCount.
// Two's-complement binary encoding is assumed.
// Allowed values of BytesCount: 1, 2, 4.
// Input parameters cannot be nullptr.
template <size_t BytesCount, typename OutInt>
void ParseSignedInteger(const uint8_t** ptr, OutInt* out_val) {
  // output type must by integer and must be long enough
  static_assert(std::is_integral<OutInt>::value, "integral expected");
  static_assert(std::is_signed<OutInt>::value, "signed integral expected");
  static_assert(BytesCount <= sizeof(OutInt), "given variable is too short");
  // parse, verify and return
  typedef typename IntegerBySize<BytesCount>::type Integer;
  *out_val = ReadInteger<Integer>(*ptr);
  *ptr += BytesCount;
}

// Parses unsigned integer saved on BytesCount bytes at position ptr and saves
// it to given variable. The pointer ptr is shifted by BytesCount. If parsed
// value is negative, no changes are made to out_val.
// Two's-complement binary encoding is assumed.
// returns false <=> parsed value is negative (ptr is shifted anyway)
// returns true <=> parsed value is >= 0
// Allowed values of BytesCount: 1, 2, 4.
// Input parameters cannot be nullptr.
template <size_t BytesCount, typename OutInt>
bool ParseUnsignedInteger(const uint8_t** ptr, OutInt* out_val) {
  // output type must by integer and must be long enough
  static_assert(std::is_integral<OutInt>::value, "integral expected");
  static_assert(BytesCount <= sizeof(OutInt), "given variable is too short");
  // parse, verify and return
  typedef typename IntegerBySize<BytesCount>::type Integer;
  const Integer val = ReadInteger<Integer>(*ptr);
  *ptr += BytesCount;
  if (val < 0) {
    return false;
  }
  *out_val = val;
  return true;
}

// Write unsigned integer to next BytesCount bytes with the most
// significant byte coming first and shifts |ptr| accordingly.
// BytesCount must equal sizeof(Integer).
// Integer must be one of the following: uint8_t, uint16_t, uint32_t,
// uint64_t.
template <size_t BytesCount, typename Integer>
inline void WriteUnsigned(uint8_t** ptr, Integer uval) {
  // given type must by unsigned integer
  static_assert(std::is_integral<Integer>::value, "integral expected");
  static_assert(std::is_unsigned<Integer>::value, "unsigned integral expected");
  static_assert(sizeof(Integer) == BytesCount, "invalid size of integer");
  // save as a sequence of bytes starting from the last one
  for (auto ptr2 = *ptr + sizeof(Integer) - 1; ptr2 >= *ptr; --ptr2) {
    *ptr2 = uval & 0xffu;
    uval >>= 8;
  }
  // move the pointer
  *ptr += sizeof(Integer);
}
template <>
inline void WriteUnsigned<1, uint8_t>(uint8_t** ptr, uint8_t uval) {
  **ptr = uval;
  ++*ptr;
}

// Write signed integer to next BytesCount bytes with two's-complement
// binary encoding and shifts |ptr| accordingly.
// BytesCount must equal sizeof(Integer).
// Integer must be one of the following: int8_t, int16_t, int32_t, int64_t.
template <size_t BytesCount, typename Integer>
void WriteInteger(uint8_t** ptr, Integer val) {
  // given type must by signed integer
  static_assert(std::is_integral<Integer>::value, "integral expected");
  static_assert(std::is_signed<Integer>::value, "signed integral expected");
  static_assert(sizeof(Integer) == BytesCount, "invalid size of integer");
  // finds corresponding unsigned type
  typedef typename std::make_unsigned<Integer>::type UnsignedInteger;
  // save as two's-complement binary encoding
  UnsignedInteger uval;
  if (val >= 0) {
    uval = val;
  } else if (val == std::numeric_limits<Integer>::min()) {
    // minimal possible value: first bit = 1 and the rest are zeroes
    uval = 1;
    uval <<= (8 * sizeof(Integer) - 1);
  } else {
    // decode from two complement conversion
    uval = -val;
    uval = ~uval;
    ++uval;
  }
  // write value starting from the last byte
  WriteUnsigned<BytesCount>(ptr, uval);
}

}  // namespace ipp

#endif  //  LIBIPP_IPP_ENCODING_H_
