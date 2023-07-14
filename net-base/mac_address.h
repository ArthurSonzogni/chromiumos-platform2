// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_BASE_MAC_ADDRESS_H_
#define NET_BASE_MAC_ADDRESS_H_

#include <net/ethernet.h>

#include <array>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include <base/containers/span.h>

#include "net-base/export.h"

namespace net_base {

// Represents an EUI-48 address.
class NET_BASE_EXPORT MacAddress {
 public:
  // The length in bytes of addresses.
  static constexpr size_t kAddressLength = ETHER_ADDR_LEN;
  // The type of the internal address data.
  using DataType = std::array<uint8_t, kAddressLength>;

  // Creates the MacAddress from colon-separated format.
  // e.g. "aa:bb:cc:dd:ee:ff" => MacAddress(0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff)
  static std::optional<MacAddress> CreateFromString(
      std::string_view address_string);

  // Creates the MacAddress from the raw byte buffer |bytes|.
  // Returns std::nullopt if |bytes|'s size is not the same as kAddressLength.
  static std::optional<MacAddress> CreateFromBytes(
      base::span<const char> bytes);
  static std::optional<MacAddress> CreateFromBytes(
      base::span<const uint8_t> bytes);

  // Constructs an instance that all the bytes are zero.
  constexpr MacAddress() : data_(DataType{}) {}

  // Constructs an instance by bytes.
  constexpr MacAddress(
      uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3, uint8_t b4, uint8_t b5)
      : MacAddress(DataType{b0, b1, b2, b3, b4, b5}) {}
  constexpr explicit MacAddress(const DataType& data) : data_(data) {}

  // Returns true if the address is "00:00:00:00:00:00".
  bool IsZero() const;

  // Compares the byte value of |data_| with |rhs|.
  bool operator==(const MacAddress& rhs) const;
  bool operator!=(const MacAddress& rhs) const;
  bool operator<(const MacAddress& rhs) const;

  // Returns the address in byte.
  std::vector<uint8_t> ToBytes() const;

  // Returns the address in the colon-separated format.
  // e.g. MacAddress(0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff) => "aa:bb:cc:dd:ee:ff"
  std::string ToString() const;

  // Same as ToString() but without colons.
  std::string ToHexString() const;

 private:
  // Stores the raw byte of address in network order.
  DataType data_;
};

NET_BASE_EXPORT std::ostream& operator<<(std::ostream& os,
                                         const MacAddress& address);

}  // namespace net_base
#endif  // NET_BASE_MAC_ADDRESS_H_
