// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NET_IP_ADDRESS_UTILS_H_
#define SHILL_NET_IP_ADDRESS_UTILS_H_

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <base/check.h>

#include "shill/net/shill_export.h"

namespace shill {

// Splits the CIDR-notation string into the pair of the address and the prefix
// length. Returns std::nullopt if the format is invalid.
SHILL_EXPORT std::optional<std::pair<std::string, int>> SplitCIDRString(
    const std::string& address_string);

// Represents the CIDR, that contains a IP address and a prefix length.
template <typename Address>
class SHILL_EXPORT CIDR {
 public:
  // Creates the Address that has all the high-order |prefix_length| bits set.
  // Returns std::nullopt if the prefix_length is invalid.
  static std::optional<Address> GetNetmask(int prefix_length) {
    if (!IsValidPrefixLength(prefix_length)) {
      return std::nullopt;
    }

    DataType data = {};
    size_t idx = 0;
    int bits = prefix_length;
    while (bits > kBitsPerByte) {
      bits -= kBitsPerByte;
      data[idx] = 0xff;
      ++idx;
    }

    // We are guaranteed to be before the end of the address data since even
    // if the prefix length is the maximum, the loop above will end before we
    // assign and increment past the last byte.
    data[idx] = ~((1 << (kBitsPerByte - bits)) - 1);

    return Address(data);
  }

  // Creates the CIDR from the CIDR notation.
  // Returns std::nullopt if the string format is invalid.
  static std::optional<CIDR> CreateFromCIDRString(
      const std::string& cidr_string) {
    const auto cidr_pair = SplitCIDRString(cidr_string);
    if (!cidr_pair) {
      return std::nullopt;
    }

    return CreateFromStringAndPrefix(cidr_pair->first, cidr_pair->second);
  }

  // Creates the CIDR from the CIDR notation string and the prefix length.
  // Returns std::nullopt if the string format or the prefix length is invalid.
  static std::optional<CIDR> CreateFromStringAndPrefix(
      const std::string& address_string, int prefix_length) {
    const auto address = Address::CreateFromString(address_string);
    if (!address) {
      return std::nullopt;
    }
    return CreateFromAddressAndPrefix(*address, prefix_length);
  }

  // Creates the CIDR from the Address and the prefix length. Returns
  // std::nullopt if the prefix length is invalid.
  static std::optional<CIDR> CreateFromAddressAndPrefix(const Address& address,
                                                        int prefix_length) {
    if (!IsValidPrefixLength(prefix_length)) {
      return std::nullopt;
    }
    return CIDR(address, prefix_length);
  }

  CIDR(const Address& address, int prefix_length)
      : address_(address), prefix_length_(prefix_length) {
    DCHECK(IsValidPrefixLength(prefix_length_));
  }

  // Getter methods for the internal data.
  const Address& address() const { return address_; }
  int prefix_length() const { return prefix_length_; }

  bool operator==(const CIDR<Address>& b) const {
    return address_ == b.address_ && prefix_length_ == b.prefix_length_;
  }
  bool operator!=(const CIDR<Address>& b) const { return !(*this == b); }

  // Returns an address that represents the network-part of the address,
  // i.e, the address with all but the prefix bits masked out.
  Address GetPrefixAddress() const {
    const Address subnet_mask = *GetNetmask(prefix_length_);
    return BitwiseAnd(address_, subnet_mask);
  }

  // Returns the broadcast address for the IP address, by setting all of the
  // host-part bits to 1.
  Address GetBroadcast() const {
    const Address broadcast_mask = BitwiseNot(*GetNetmask(prefix_length_));
    return BitwiseOr(address_, broadcast_mask);
  }

  // Returns true is the address |b| is contained in |*this| CIDR.
  bool ContainsAddress(const Address& b) const {
    return GetPrefixAddress() == CIDR(b, prefix_length_).GetPrefixAddress();
  }

  // Returns the string in the CIDR notation.
  std::string ToString() const {
    return address_.ToString() + "/" + std::to_string(prefix_length_);
  }

  friend std::ostream& operator<<(std::ostream& os, const CIDR& cidr) {
    os << cidr.ToString();
    return os;
  }

 private:
  using DataType = typename Address::DataType;

  static constexpr int kBitsPerByte = 8;
  static constexpr int kMaxPrefixLength =
      static_cast<int>(Address::kAddressLength * kBitsPerByte);

  static bool IsValidPrefixLength(int prefix_length) {
    return 0 <= prefix_length && prefix_length <= kMaxPrefixLength;
  }

  static Address BitwiseAnd(const Address& a, const Address& b) {
    DataType data;
    for (size_t i = 0; i < Address::kAddressLength; ++i) {
      data[i] = a.data()[i] & b.data()[i];
    }
    return Address(data);
  }

  static Address BitwiseOr(const Address& a, const Address& b) {
    DataType data;
    for (size_t i = 0; i < Address::kAddressLength; ++i) {
      data[i] = a.data()[i] | b.data()[i];
    }
    return Address(data);
  }

  static Address BitwiseNot(const Address& a) {
    DataType data = a.data();
    for (auto& byte : data) {
      byte = ~byte;
    }
    return Address(data);
  }

  Address address_;
  int prefix_length_;
};

}  // namespace shill
#endif  // SHILL_NET_IP_ADDRESS_UTILS_H_
