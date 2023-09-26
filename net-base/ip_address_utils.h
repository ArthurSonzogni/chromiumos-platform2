// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_BASE_IP_ADDRESS_UTILS_H_
#define NET_BASE_IP_ADDRESS_UTILS_H_

#include <algorithm>
#include <bitset>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <base/check.h>
#include <base/containers/span.h>

#include "net-base/export.h"

namespace net_base {

// Splits the CIDR-notation string into the pair of the address and the prefix
// length. Returns std::nullopt if the format is invalid.
NET_BASE_EXPORT std::optional<std::pair<std::string_view, int>> SplitCIDRString(
    std::string_view address_string);

// Same as `inet_pton()` from the standard C library, but takes a `string_view`
// instead for the input.
int inet_pton_string_view(int af, std::string_view src, void* dst);

template <typename Address>
std::optional<Address> CreateAddressFromBytes(base::span<const uint8_t> bytes) {
  if (bytes.size() != Address::kAddressLength) {
    return std::nullopt;
  }

  typename Address::DataType data;
  std::copy_n(bytes.data(), Address::kAddressLength, data.begin());
  return Address(data);
}

// Represents the CIDR, that contains a IP address and a prefix length.
template <typename Address>
class NET_BASE_EXPORT CIDR {
 public:
  static constexpr int kBitsPerByte = 8;
  static constexpr int kMaxPrefixLength =
      static_cast<int>(Address::kAddressLength * kBitsPerByte);

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
    data[idx] = static_cast<uint8_t>(~((1 << (kBitsPerByte - bits)) - 1));

    return Address(data);
  }

  // Returns the prefix length given a netmask address. Returns std::nullopt if
  // |netmask| is not a valid netmask.
  static std::optional<int> GetPrefixLength(const Address& netmask) {
    bool saw_zero_bit = false;
    int prefix_length = 0;

    for (const uint8_t byte : netmask.data()) {
      const std::bitset<8> bits(byte);
      const auto count = bits.count();

      // The 1 bits should be continuously at the left side.
      if ((bits << count) != 0) {
        return std::nullopt;
      }
      if (saw_zero_bit && count != 0) {
        return std::nullopt;
      }

      if (count != 8) {
        saw_zero_bit = true;
      }
      prefix_length += count;
    }

    return prefix_length;
  }

  // Creates the CIDR from the CIDR notation.
  // Returns std::nullopt if the string format is invalid.
  static std::optional<CIDR> CreateFromCIDRString(
      std::string_view cidr_string) {
    const auto cidr_pair = SplitCIDRString(cidr_string);
    if (cidr_pair) {
      return CreateFromStringAndPrefix(cidr_pair->first, cidr_pair->second);
    }

    // If there is no prefix length in the string, then parse it as the address
    // and use kMaxPrefixLength as default prefix length.
    return CreateFromStringAndPrefix(cidr_string, kMaxPrefixLength);
  }

  // Creates the CIDR from the CIDR notation string and the prefix length.
  // Returns std::nullopt if the string format or the prefix length is invalid.
  static std::optional<CIDR> CreateFromStringAndPrefix(
      std::string_view address_string, int prefix_length) {
    const auto address = Address::CreateFromString(address_string);
    if (!address) {
      return std::nullopt;
    }
    return CreateFromAddressAndPrefix(*address, prefix_length);
  }

  // Creates the CIDR from the bytes and the prefix length.
  // Returns std::nullopt if the bytes length or prefix length is invalid.
  static std::optional<CIDR> CreateFromBytesAndPrefix(
      base::span<const char> bytes, int prefix_length) {
    return CreateFromBytesAndPrefix(
        base::span<const uint8_t>(
            reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()),
        prefix_length);
  }
  static std::optional<CIDR> CreateFromBytesAndPrefix(
      base::span<const uint8_t> bytes, int prefix_length) {
    const auto address = Address::CreateFromBytes(bytes);
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

  constexpr CIDR() : CIDR(Address()) {}
  constexpr explicit CIDR(const Address& address)
      : address_(address), prefix_length_(0) {}

  // Getter methods for the internal data.
  const Address& address() const { return address_; }
  int prefix_length() const { return prefix_length_; }

  bool operator==(const CIDR<Address>& b) const {
    return address_ == b.address_ && prefix_length_ == b.prefix_length_;
  }
  bool operator!=(const CIDR<Address>& b) const { return !(*this == b); }
  bool operator<(const CIDR<Address>& b) const {
    if (address_ != b.address_) {
      return address_ < b.address_;
    }
    return prefix_length_ < b.prefix_length_;
  }

  // Creates the Address that has all the high-order |prefix_length_| bits set.
  Address ToNetmask() const {
    // It's safe to dereference because |prefix_length_| is always valid.
    return *GetNetmask(prefix_length_);
  }

  // Returns an CIDR that represents the network-part of the address (i.e, the
  // address with all but the prefix bits masked out) and the same prefix length
  // as |this|.
  CIDR GetPrefixCIDR() const {
    return CIDR(BitwiseAnd(address_, ToNetmask()), prefix_length_);
  }

  // Returns the broadcast address for the IP address, by setting all of the
  // host-part bits to 1.
  Address GetBroadcast() const {
    const Address broadcast_mask = BitwiseNot(ToNetmask());
    return BitwiseOr(address_, broadcast_mask);
  }

  // Returns true is the address |b| is in the same subnet with |*this| CIDR.
  bool InSameSubnetWith(const Address& b) const {
    return GetPrefixCIDR() == CIDR(b, prefix_length_).GetPrefixCIDR();
  }

  // Returns true if address is all zero and prefix length equals to 0.
  bool IsDefault() const { return address().IsZero() && prefix_length_ == 0; }

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

  CIDR(const Address& address, int prefix_length)
      : address_(address), prefix_length_(prefix_length) {
    DCHECK(IsValidPrefixLength(prefix_length_));
  }

  Address address_;
  int prefix_length_;
};

}  // namespace net_base
#endif  // NET_BASE_IP_ADDRESS_UTILS_H_
