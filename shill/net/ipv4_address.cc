// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/net/ipv4_address.h"

#include <algorithm>

#include <arpa/inet.h>

#include <base/check.h>

namespace shill {

// static
std::optional<IPv4Address> IPv4Address::CreateFromString(
    const std::string& address_string) {
  DataType data;
  if (inet_pton(AF_INET, address_string.c_str(), data.data()) <= 0) {
    return std::nullopt;
  }
  return IPv4Address(data);
}

// static
std::optional<IPv4Address> IPv4Address::CreateFromBytes(const uint8_t* bytes,
                                                        size_t byte_length) {
  return CreateAddressFromBytes<IPv4Address>(bytes, byte_length);
}

IPv4Address::IPv4Address() : data_(DataType{}) {}
IPv4Address::IPv4Address(uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3)
    : IPv4Address(DataType{b0, b1, b2, b3}) {}
IPv4Address::IPv4Address(const DataType& data) : data_(data) {}

bool IPv4Address::IsZero() const {
  return std::all_of(data_.begin(), data_.end(),
                     [](uint8_t byte) { return byte == 0; });
}

bool IPv4Address::operator==(const IPv4Address& rhs) const {
  return data_ == rhs.data_;
}

bool IPv4Address::operator!=(const IPv4Address& rhs) const {
  return !(*this == rhs);
}

bool IPv4Address::operator<(const IPv4Address& rhs) const {
  return data_ < rhs.data_;
}

std::string IPv4Address::ToByteString() const {
  return {reinterpret_cast<const char*>(data_.data()), kAddressLength};
}

std::string IPv4Address::ToString() const {
  char address_buf[INET_ADDRSTRLEN];
  const char* res =
      inet_ntop(AF_INET, data_.data(), address_buf, sizeof(address_buf));
  DCHECK(res);
  return std::string(address_buf);
}

std::ostream& operator<<(std::ostream& os, const IPv4Address& address) {
  os << address.ToString();
  return os;
}

}  // namespace shill
