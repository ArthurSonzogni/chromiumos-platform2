// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_BASE_IPV6_ADDRESS_H_
#define NET_BASE_IPV6_ADDRESS_H_

#include <netinet/in.h>

#include <array>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <base/containers/span.h>
#include <base/hash/hash.h>

#include "net-base/export.h"
#include "net-base/ip_address_utils.h"

namespace net_base {

// Represents an IPv6 address.
class NET_BASE_EXPORT IPv6Address {
 public:
  // The length in bytes of addresses.
  static constexpr size_t kAddressLength = sizeof(struct in6_addr);
  // The type of the internal address data. The address is stored in network
  // order (i.e. big endian).
  using DataType = std::array<uint8_t, kAddressLength>;

  // Creates the IPv6Address from IPv6 network address format.
  // TODO(b/269983153): Add a fuzzer test for this method.
  static std::optional<IPv6Address> CreateFromString(
      std::string_view address_string);

  // Creates the IPv6Address from the raw byte buffer |bytes|.
  // Returns std::nullopt if |bytes|'s size is not the same as kAddressLength.
  static std::optional<IPv6Address> CreateFromBytes(
      base::span<const char> bytes);
  static std::optional<IPv6Address> CreateFromBytes(
      base::span<const uint8_t> bytes);

  // Constructs an instance with the "::" address.
  constexpr IPv6Address() : data_(DataType{}) {}

  // Constructs an instance by the list of uint8_t, in network order.
  constexpr IPv6Address(uint8_t b0,
                        uint8_t b1,
                        uint8_t b2,
                        uint8_t b3,
                        uint8_t b4,
                        uint8_t b5,
                        uint8_t b6,
                        uint8_t b7,
                        uint8_t b8,
                        uint8_t b9,
                        uint8_t b10,
                        uint8_t b11,
                        uint8_t b12,
                        uint8_t b13,
                        uint8_t b14,
                        uint8_t b15)
      : data_({b0, b1, b2, b3, b4, b5, b6, b7, b8, b9, b10, b11, b12, b13, b14,
               b15}) {}

  constexpr explicit IPv6Address(const DataType& data) : data_(data) {}
  explicit IPv6Address(const struct in6_addr& addr);

  // Returns true if the address is "::".
  bool IsZero() const;

  // Compares the byte value of |data_| with |rhs|.
  bool operator==(const IPv6Address& rhs) const;
  bool operator!=(const IPv6Address& rhs) const;
  bool operator<(const IPv6Address& rhs) const;

  // Returns the internal data.
  const DataType& data() const { return data_; }

  // Returns the address in byte, stored in network order (i.e. big endian).
  std::vector<uint8_t> ToBytes() const;
  std::string ToByteString() const;

  // Returns the address in the in6_addr type.
  struct in6_addr ToIn6Addr() const;

  // Returns the address in the IPv6 network address format.
  std::string ToString() const;

 private:
  // Stores the raw byte of address in network order.
  DataType data_;
};

NET_BASE_EXPORT std::ostream& operator<<(std::ostream& os,
                                         const IPv6Address& address);

// Represents the IPv6 CIDR, that contains a IPv6 address and a prefix length.
using IPv6CIDR = CIDR<IPv6Address>;

}  // namespace net_base

namespace std {

template <>
struct hash<net_base::IPv6Address> {
  size_t operator()(const net_base::IPv6Address& addr) const {
    return base::FastHash(addr.data());
  }
};

template <>
struct hash<net_base::IPv6CIDR> {
  size_t operator()(const net_base::IPv6CIDR& cidr) const {
    return base::FastHash(cidr.address().data()) ^
           std::hash<int>()(cidr.prefix_length());
  }
};

}  // namespace std

#endif  // NET_BASE_IPV6_ADDRESS_H_
