// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NET_IPV4_ADDRESS_H_
#define SHILL_NET_IPV4_ADDRESS_H_

#include <netinet/in.h>

#include <array>
#include <optional>
#include <string>
#include <utility>

#include "shill/net/ip_address_utils.h"
#include "shill/net/shill_export.h"

namespace shill {

// Represents an IPv4 address.
class SHILL_EXPORT IPv4Address {
 public:
  // The length in bytes of addresses.
  static constexpr size_t kAddressLength = sizeof(in_addr);
  // The type of the internal address data. The address is stored in network
  // order (i.e. big endian).
  using DataType = std::array<uint8_t, kAddressLength>;

  // Creates the IPv4Address from IPv4 dotted-decimal notation.
  // TODO(b/269983153): Add a fuzzer test for this method.
  static std::optional<IPv4Address> CreateFromString(
      const std::string& address_string);

  // Constructs an instance with the "0.0.0.0" address.
  IPv4Address();

  // Constructs an instance by bytes in network order.
  // i.e. |b0| is the MSB and |b3| is the LSB.
  IPv4Address(uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3);
  explicit IPv4Address(const DataType& data);

  // Returns true if the address is "0.0.0.0".
  bool IsZero() const;

  // Compares the byte value of |data_| with |rhs|.
  bool operator==(const IPv4Address& rhs) const;
  bool operator!=(const IPv4Address& rhs) const;
  bool operator<(const IPv4Address& rhs) const;

  // Returns the internal data.
  const DataType& data() const { return data_; }

  // Returns the address in the IPv4 dotted-decimal notation.
  std::string ToString() const;

 private:
  // Stores the raw byte of address in network order.
  DataType data_;
};

SHILL_EXPORT std::ostream& operator<<(std::ostream& os,
                                      const IPv4Address& address);

// Represents the IPv4 CIDR, that contains a IPv4 address and a prefix length.
using IPv4CIDR = CIDR<IPv4Address>;

}  // namespace shill
#endif  // SHILL_NET_IPV4_ADDRESS_H_
