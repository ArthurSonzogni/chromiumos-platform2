// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net-base/mac_address.h"

#include <base/strings/string_number_conversions.h>
#include <base/strings/stringprintf.h>

namespace net_base {

// static
std::optional<MacAddress> MacAddress::CreateFromString(
    std::string_view address_string) {
  // The length of the string should be 6 bytes * 2 digit + 5 semicolon = 17.
  constexpr size_t kStringLength = 17;

  if (address_string.size() != kStringLength) {
    return std::nullopt;
  }

  // The character at index (3n + 2) should be ':'.
  for (size_t i = 2; i < kStringLength; i += 3) {
    if (address_string[i] != ':') {
      return std::nullopt;
    }
  }

  DataType data;
  for (size_t i = 0; i < kAddressLength; ++i) {
    uint32_t byte;
    if (!base::HexStringToUInt(
            std::string_view(address_string).substr(i * 3, 2), &byte)) {
      return std::nullopt;
    }
    data[i] = static_cast<uint8_t>(byte);
  }

  return std::make_optional<MacAddress>(data);
}

// static
std::optional<MacAddress> MacAddress::CreateFromBytes(
    base::span<const char> bytes) {
  return CreateFromBytes(base::span<const uint8_t>(
      reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()));
}

// static
std::optional<MacAddress> MacAddress::CreateFromBytes(
    base::span<const uint8_t> bytes) {
  if (bytes.size() != MacAddress::kAddressLength) {
    return std::nullopt;
  }
  return std::make_optional<MacAddress>(bytes[0], bytes[1], bytes[2], bytes[3],
                                        bytes[4], bytes[5]);
}

bool MacAddress::IsZero() const {
  return std::all_of(data_.begin(), data_.end(),
                     [](uint8_t byte) { return byte == 0; });
}

bool MacAddress::operator==(const MacAddress& rhs) const {
  return data_ == rhs.data_;
}

bool MacAddress::operator!=(const MacAddress& rhs) const {
  return !(*this == rhs);
}

bool MacAddress::operator<(const MacAddress& rhs) const {
  return data_ < rhs.data_;
}

std::vector<uint8_t> MacAddress::ToBytes() const {
  return {std::begin(data_), std::end(data_)};
}

std::string MacAddress::ToString() const {
  return base::StringPrintf("%02x:%02x:%02x:%02x:%02x:%02x", data_[0], data_[1],
                            data_[2], data_[3], data_[4], data_[5]);
}

std::string MacAddress::ToHexString() const {
  return base::StringPrintf("%02x%02x%02x%02x%02x%02x", data_[0], data_[1],
                            data_[2], data_[3], data_[4], data_[5]);
}

std::ostream& operator<<(std::ostream& os, const MacAddress& address) {
  os << address.ToString();
  return os;
}

}  // namespace net_base
