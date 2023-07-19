// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_BASE_IP_ADDRESS_H_
#define NET_BASE_IP_ADDRESS_H_

#include <sys/socket.h>

#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <base/containers/span.h>

#include "net-base/export.h"
#include "net-base/ipv4_address.h"
#include "net-base/ipv6_address.h"

namespace net_base {

// Represents the family of the IP protocol.
enum class NET_BASE_EXPORT IPFamily {
  kIPv4,
  kIPv6,
};

// Helper const for iterating through both IP families.
constexpr std::initializer_list<IPFamily> kIPFamilies = {IPFamily::kIPv4,
                                                         IPFamily::kIPv6};

// Converts from IPFamily enum to sa_family_t.
NET_BASE_EXPORT sa_family_t ToSAFamily(IPFamily family);

// Converts from sa_family_t to IPFamily enum.
// Returns std::nullopt if the value cannot be converted.
NET_BASE_EXPORT std::optional<IPFamily> FromSAFamily(sa_family_t family);

// Converts from IPFamily enum to std::String.
NET_BASE_EXPORT std::string ToString(IPFamily family);

// Represents an family-agnostic IP address, either a IPv4 or a IPv6 address.
class NET_BASE_EXPORT IPAddress {
 public:
  // Creates the IPAddress from IPv4 dotted-decimal notation or IPv6 network
  // address format.
  static std::optional<IPAddress> CreateFromString(
      const std::string& address_string);
  static std::optional<IPAddress> CreateFromString(const char* address_string);

  // Creates the IPAddress from the raw byte buffer |bytes|.
  // Returns std::nullopt if |bytes|'s size is not the same as
  // IPv4Address::kAddressLength or IPv6Address::kAddressLength.
  static std::optional<IPAddress> CreateFromBytes(base::span<const char> bytes);
  static std::optional<IPAddress> CreateFromBytes(
      base::span<const uint8_t> bytes);

  // Creates the IPAddress by the family. The created address is all-zero.
  // (i.e. "0.0.0.0" for IPv4, "::" for IPv6)
  explicit constexpr IPAddress(IPFamily family)
      : address_(family == IPFamily::kIPv4
                     ? std::variant<IPv4Address, IPv6Address>(IPv4Address())
                     : std::variant<IPv4Address, IPv6Address>(IPv6Address())) {}

  explicit constexpr IPAddress(const IPv4Address& address)
      : address_(address) {}
  explicit constexpr IPAddress(const IPv6Address& address)
      : address_(address) {}

  // Returns true if the address is "0.0.0.0" or "::".
  bool IsZero() const;

  // Compares with |rhs|. The comparation rule follows IPv4Address and
  // IPv6Address if the family of |rhs| is the same. Otherwise, the IPv4Address
  // is less than IPv6Address.
  bool operator==(const IPAddress& rhs) const;
  bool operator!=(const IPAddress& rhs) const;
  bool operator<(const IPAddress& rhs) const;

  // Returns the family of the IP address.
  IPFamily GetFamily() const;

  // Returns the length in bytes of the address.
  size_t GetAddressLength() const;

  // Converts to the family-specific classes. Returns std::nullopt if the IP
  // family is not the same.
  std::optional<IPv4Address> ToIPv4Address() const;
  std::optional<IPv6Address> ToIPv6Address() const;

  // Returns the address in byte, stored in network order (i.e. big endian).
  std::vector<uint8_t> ToBytes() const;
  std::string ToByteString() const;

  // Returns the address in the IPv4 dotted-decimal notation or IPv6 network
  // address format.
  std::string ToString() const;

 private:
  std::variant<IPv4Address, IPv6Address> address_;
};

// Represents an family-agnostic IP CIDR, either a IPv4 or a IPv6 CIDR.
class NET_BASE_EXPORT IPCIDR {
 public:
  // Creates the CIDR from either IPv4 or IPv6 CIDR notation.
  // Returns std::nullopt if the string format is invalid.
  static std::optional<IPCIDR> CreateFromCIDRString(
      const std::string& cidr_string);

  // Creates the CIDR from the IP address notation string and the prefix length.
  // Returns std::nullopt if the string format or the prefix length is invalid.
  static std::optional<IPCIDR> CreateFromStringAndPrefix(
      const std::string& address_string, int prefix_length);

  // Creates the CIDR from the Address and the prefix length. Returns
  // std::nullopt if the prefix length is invalid.
  static std::optional<IPCIDR> CreateFromAddressAndPrefix(
      const IPAddress& address, int prefix_length);

  // Creates the IPCIDR by the family. The created CIDR is all-zero.
  // (i.e. "0.0.0.0/0" for IPv4, "::/0" for IPv6)
  explicit constexpr IPCIDR(IPFamily family)
      : cidr_(family == IPFamily::kIPv4
                  ? std::variant<IPv4CIDR, IPv6CIDR>(IPv4CIDR())
                  : std::variant<IPv4CIDR, IPv6CIDR>(IPv6CIDR())) {}

  explicit constexpr IPCIDR(const IPv4CIDR& cidr) : cidr_(cidr) {}
  explicit constexpr IPCIDR(const IPv6CIDR& cidr) : cidr_(cidr) {}
  explicit constexpr IPCIDR(const IPv4Address& addr) : cidr_(IPv4CIDR(addr)) {}
  explicit constexpr IPCIDR(const IPv6Address& addr) : cidr_(IPv6CIDR(addr)) {}

  // Getter methods for the internal data.
  IPAddress address() const;
  int prefix_length() const;

  bool operator==(const IPCIDR& rhs) const;
  bool operator!=(const IPCIDR& rhs) const;

  // Returns the family of the CIDR.
  IPFamily GetFamily() const;

  // Converts to the family-specific classes. Returns std::nullopt if the IP
  // family is not the same.
  std::optional<IPv4CIDR> ToIPv4CIDR() const;
  std::optional<IPv6CIDR> ToIPv6CIDR() const;

  // Creates the Address that has all the high-order of prefix length bits set.
  IPAddress ToNetmask() const;

  // Returns an address that represents the network-part of the address,
  // i.e, the address with all but the prefix bits masked out.
  IPAddress GetPrefixAddress() const;

  // Returns the broadcast address for the IP address, by setting all of the
  // host-part bits to 1.
  IPAddress GetBroadcast() const;

  // Returns true is the address |b| is in the same subnet with |*this| CIDR.
  bool InSameSubnetWith(const IPAddress& b) const;

  // Returns the string in the CIDR notation.
  std::string ToString() const;

 private:
  std::variant<IPv4CIDR, IPv6CIDR> cidr_;
};

NET_BASE_EXPORT std::ostream& operator<<(std::ostream& os, IPFamily family);

NET_BASE_EXPORT std::ostream& operator<<(std::ostream& os,
                                         const IPAddress& address);

NET_BASE_EXPORT std::ostream& operator<<(std::ostream& os, const IPCIDR& cidr);

}  // namespace net_base
#endif  // NET_BASE_IP_ADDRESS_H_
