// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NET_IPV6_ADDRESS_H_
#define SHILL_NET_IPV6_ADDRESS_H_

#include <netinet/in.h>

#include <array>
#include <optional>
#include <string>
#include <utility>

#include "shill/net/ip_address_utils.h"
#include "shill/net/shill_export.h"

namespace shill {

// Represents an IPv6 address.
class SHILL_EXPORT IPv6Address {
 public:
  // The length in bytes of addresses.
  static constexpr size_t kAddressLength = sizeof(in6_addr);
  // The type of the internal address data. The address is stored in network
  // order (i.e. big endian).
  using DataType = std::array<uint8_t, kAddressLength>;

  // Creates the IPv6Address from IPv6 network address format.
  // TODO(b/269983153): Add a fuzzer test for this method.
  static std::optional<IPv6Address> CreateFromString(
      const std::string& address_string);

  // Creates the IPv6Address from the raw byte buffer. |bytes| points to the
  // front of the byte buffer, and |bytes_length| is the length of the buffer.
  // The caller should guarantee the data between [bytes, bytes + bytes_length)
  // is valid memory.
  // Returns std::nullopt if |bytes_length| is not the same as kAddressLength.
  static std::optional<IPv6Address> CreateFromBytes(const char* bytes,
                                                    size_t bytes_length) {
    return CreateFromBytes(reinterpret_cast<const uint8_t*>(bytes),
                           bytes_length);
  }
  static std::optional<IPv6Address> CreateFromBytes(const uint8_t* bytes,
                                                    size_t bytes_length);

  // Constructs an instance with the "::" address.
  IPv6Address();

  explicit IPv6Address(const DataType& data);

  // Returns true if the address is "::".
  bool IsZero() const;

  // Compares the byte value of |data_| with |rhs|.
  bool operator==(const IPv6Address& rhs) const;
  bool operator!=(const IPv6Address& rhs) const;
  bool operator<(const IPv6Address& rhs) const;

  // Returns the internal data.
  const DataType& data() const { return data_; }

  // Returns the address in byte, stored in network order (i.e. big endian).
  std::string ToByteString() const;

  // Returns the address in the IPv6 network address format.
  std::string ToString() const;

 private:
  // Stores the raw byte of address in network order.
  DataType data_;
};

SHILL_EXPORT std::ostream& operator<<(std::ostream& os,
                                      const IPv6Address& address);

// Represents the IPv6 CIDR, that contains a IPv6 address and a prefix length.
using IPv6CIDR = CIDR<IPv6Address>;

}  // namespace shill
#endif  // SHILL_NET_IPV6_ADDRESS_H_
