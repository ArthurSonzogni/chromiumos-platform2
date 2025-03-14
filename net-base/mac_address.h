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
#include <unordered_set>
#include <vector>

#include <base/containers/span.h>
#include <base/hash/hash.h>
#include <brillo/brillo_export.h>

namespace net_base {

// Represents an EUI-48 address.
class BRILLO_EXPORT MacAddress {
 public:
  struct Hash {
    size_t operator()(const net_base::MacAddress& mac) const {
      return base::FastHash(mac.ToBytes());
    }
  };
  using UnorderedSet = std::unordered_set<MacAddress, Hash>;

  // The length in bytes of addresses.
  static constexpr size_t kAddressLength = ETHER_ADDR_LEN;
  // The type of the internal address data.
  using DataType = std::array<uint8_t, kAddressLength>;

  // Multicast address bit.
  static constexpr uint8_t kMulicastMacBit = 0x01;
  // Locally administered bit.
  static constexpr uint8_t kLocallyAdministratedMacBit = 0x02;

  // Create a random unicast locally administered MAC address.
  static MacAddress CreateRandom();

  // Creates the MacAddress from colon-separated format.
  // e.g. "aa:bb:cc:dd:ee:ff" => MacAddress(0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff)
  static std::optional<MacAddress> CreateFromString(
      std::string_view address_string);

  // Creates the MacAddress from hex format string.
  // e.g. "aabbccddeeff" => MacAddress(0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff)
  static std::optional<MacAddress> CreateFromHexString(
      std::string_view hex_string);

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

  // Returns whether the address is a locally-administered address, as
  // opposed to a unique IEEE-issued address.
  bool IsLocallyAdministered() const;

  // Compares the byte value of |data_| with |rhs|.
  bool operator==(const MacAddress& rhs) const;
  bool operator!=(const MacAddress& rhs) const;
  bool operator<(const MacAddress& rhs) const;

  // Returns the pointer to the underlying byte.
  const uint8_t* data() const { return data_.data(); }

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

BRILLO_EXPORT std::ostream& operator<<(std::ostream& os,
                                       const MacAddress& address);

}  // namespace net_base

#endif  // NET_BASE_MAC_ADDRESS_H_
