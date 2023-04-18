// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/net/ipv6_address.h"

#include <algorithm>

#include <arpa/inet.h>

#include <base/check.h>

namespace shill {

// static
std::optional<IPv6Address> IPv6Address::CreateFromString(
    const std::string& address_string) {
  DataType data;
  if (inet_pton(AF_INET6, address_string.c_str(), data.data()) <= 0) {
    return std::nullopt;
  }
  return IPv6Address(data);
}

IPv6Address::IPv6Address() : data_(DataType{}) {}
IPv6Address::IPv6Address(const DataType& data) : data_(data) {}

bool IPv6Address::IsZero() const {
  return std::all_of(data_.begin(), data_.end(),
                     [](uint8_t byte) { return byte == 0; });
}

bool IPv6Address::operator==(const IPv6Address& rhs) const {
  return data_ == rhs.data_;
}

bool IPv6Address::operator!=(const IPv6Address& rhs) const {
  return !(*this == rhs);
}

bool IPv6Address::operator<(const IPv6Address& rhs) const {
  return data_ < rhs.data_;
}

std::string IPv6Address::ToString() const {
  char address_buf[INET6_ADDRSTRLEN];
  const char* res =
      inet_ntop(AF_INET6, data_.data(), address_buf, sizeof(address_buf));
  DCHECK(res);
  return std::string(address_buf);
}

std::ostream& operator<<(std::ostream& os, const IPv6Address& address) {
  os << address.ToString();
  return os;
}

}  // namespace shill
