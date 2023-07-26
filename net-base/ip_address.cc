// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net-base/ip_address.h"

#include <algorithm>

namespace net_base {

sa_family_t ToSAFamily(IPFamily family) {
  switch (family) {
    case IPFamily::kIPv4:
      return AF_INET;
    case IPFamily::kIPv6:
      return AF_INET6;
  }
}

std::optional<IPFamily> FromSAFamily(sa_family_t family) {
  switch (family) {
    case AF_INET:
      return IPFamily::kIPv4;
    case AF_INET6:
      return IPFamily::kIPv6;
    default:
      return std::nullopt;
  }
}
std::string ToString(IPFamily family) {
  switch (family) {
    case IPFamily::kIPv4:
      return "IPv4";
    case IPFamily::kIPv6:
      return "IPv6";
  }
}

// static
std::optional<IPAddress> IPAddress::CreateFromString(
    const std::string& address_string, std::optional<IPFamily> family) {
  return CreateFromString(address_string.c_str(), family);
}

// static
std::optional<IPAddress> IPAddress::CreateFromString(
    const char* address_string, std::optional<IPFamily> family) {
  if (family != net_base::IPFamily::kIPv6) {
    const auto ipv4 = IPv4Address::CreateFromString(address_string);
    if (ipv4) {
      return IPAddress(*ipv4);
    }
  }

  if (family != net_base::IPFamily::kIPv4) {
    const auto ipv6 = IPv6Address::CreateFromString(address_string);
    if (ipv6) {
      return IPAddress(*ipv6);
    }
  }

  return std::nullopt;
}

// static
std::optional<IPAddress> IPAddress::CreateFromBytes(
    base::span<const char> bytes, std::optional<IPFamily> family) {
  return CreateFromBytes(
      base::span<const uint8_t>(reinterpret_cast<const uint8_t*>(bytes.data()),
                                bytes.size()),
      family);
}

// static
std::optional<IPAddress> IPAddress::CreateFromBytes(
    base::span<const uint8_t> bytes, std::optional<IPFamily> family) {
  if (family != net_base::IPFamily::kIPv6) {
    const auto ipv4 = IPv4Address::CreateFromBytes(bytes);
    if (ipv4) {
      return IPAddress(*ipv4);
    }
  }

  if (family != net_base::IPFamily::kIPv4) {
    const auto ipv6 = IPv6Address::CreateFromBytes(bytes);
    if (ipv6) {
      return IPAddress(*ipv6);
    }
  }

  return std::nullopt;
}

bool IPAddress::IsZero() const {
  return std::visit([](auto&& address) -> bool { return address.IsZero(); },
                    address_);
}

bool IPAddress::operator==(const IPAddress& rhs) const {
  return address_ == rhs.address_;
}

bool IPAddress::operator!=(const IPAddress& rhs) const {
  return !(*this == rhs);
}

bool IPAddress::operator<(const IPAddress& rhs) const {
  return address_ < rhs.address_;
}

IPFamily IPAddress::GetFamily() const {
  if (const auto ipv4 = std::get_if<IPv4Address>(&address_)) {
    return IPFamily::kIPv4;
  }
  return IPFamily::kIPv6;
}

size_t IPAddress::GetAddressLength() const {
  switch (GetFamily()) {
    case IPFamily::kIPv4:
      return IPv4Address::kAddressLength;
    case IPFamily::kIPv6:
      return IPv6Address::kAddressLength;
  }
}

std::optional<IPv4Address> IPAddress::ToIPv4Address() const {
  if (const auto ipv4 = std::get_if<IPv4Address>(&address_)) {
    return *ipv4;
  }
  return std::nullopt;
}

std::optional<IPv6Address> IPAddress::ToIPv6Address() const {
  if (const auto ipv6 = std::get_if<IPv6Address>(&address_)) {
    return *ipv6;
  }
  return std::nullopt;
}

std::vector<uint8_t> IPAddress::ToBytes() const {
  return std::visit(
      [](auto&& address) -> std::vector<uint8_t> { return address.ToBytes(); },
      address_);
}

std::string IPAddress::ToByteString() const {
  return std::visit(
      [](auto&& address) -> std::string { return address.ToByteString(); },
      address_);
}

std::string IPAddress::ToString() const {
  return std::visit(
      [](auto&& address) -> std::string { return address.ToString(); },
      address_);
}

// static
int IPCIDR::GetMaxPrefixLength(IPFamily family) {
  switch (family) {
    case IPFamily::kIPv4:
      return IPv4CIDR::kMaxPrefixLength;
    case IPFamily::kIPv6:
      return IPv6CIDR::kMaxPrefixLength;
  }
}

// static
std::optional<IPCIDR> IPCIDR::CreateFromCIDRString(
    const std::string& cidr_string, std::optional<IPFamily> family) {
  if (family != net_base::IPFamily::kIPv6) {
    const auto ipv4 = IPv4CIDR::CreateFromCIDRString(cidr_string);
    if (ipv4) {
      return IPCIDR(*ipv4);
    }
  }

  if (family != net_base::IPFamily::kIPv4) {
    const auto ipv6 = IPv6CIDR::CreateFromCIDRString(cidr_string);
    if (ipv6) {
      return IPCIDR(*ipv6);
    }
  }

  return std::nullopt;
}

// static
std::optional<IPCIDR> IPCIDR::CreateFromStringAndPrefix(
    const std::string& address_string,
    int prefix_length,
    std::optional<IPFamily> family) {
  if (family != net_base::IPFamily::kIPv6) {
    const auto ipv4 =
        IPv4CIDR::CreateFromStringAndPrefix(address_string, prefix_length);
    if (ipv4) {
      return IPCIDR(*ipv4);
    }
  }

  if (family != net_base::IPFamily::kIPv4) {
    const auto ipv6 =
        IPv6CIDR::CreateFromStringAndPrefix(address_string, prefix_length);
    if (ipv6) {
      return IPCIDR(*ipv6);
    }
  }

  return std::nullopt;
}

// static
std::optional<IPCIDR> IPCIDR::CreateFromBytesAndPrefix(
    base::span<const char> bytes,
    int prefix_length,
    std::optional<IPFamily> family) {
  return CreateFromBytesAndPrefix(
      base::span<const uint8_t>(reinterpret_cast<const uint8_t*>(bytes.data()),
                                bytes.size()),
      prefix_length, family);
}

// static
std::optional<IPCIDR> IPCIDR::CreateFromBytesAndPrefix(
    base::span<const uint8_t> bytes,
    int prefix_length,
    std::optional<IPFamily> family) {
  if (family != net_base::IPFamily::kIPv6) {
    const auto ipv4 = IPv4CIDR::CreateFromBytesAndPrefix(bytes, prefix_length);
    if (ipv4) {
      return IPCIDR(*ipv4);
    }
  }

  if (family != net_base::IPFamily::kIPv4) {
    const auto ipv6 = IPv6CIDR::CreateFromBytesAndPrefix(bytes, prefix_length);
    if (ipv6) {
      return IPCIDR(*ipv6);
    }
  }

  return std::nullopt;
}

// static
std::optional<IPCIDR> IPCIDR::CreateFromAddressAndPrefix(
    const IPAddress& address, int prefix_length) {
  if (address.GetFamily() == IPFamily::kIPv4) {
    const auto ipv4 = IPv4CIDR::CreateFromAddressAndPrefix(
        *address.ToIPv4Address(), prefix_length);
    if (ipv4) {
      return IPCIDR(*ipv4);
    }
  }

  if (address.GetFamily() == IPFamily::kIPv6) {
    const auto ipv6 = IPv6CIDR::CreateFromAddressAndPrefix(
        *address.ToIPv6Address(), prefix_length);
    if (ipv6) {
      return IPCIDR(*ipv6);
    }
  }

  return std::nullopt;
}

IPAddress IPCIDR::address() const {
  return std::visit(
      [](auto&& cidr) -> IPAddress { return IPAddress(cidr.address()); },
      cidr_);
}

int IPCIDR::prefix_length() const {
  return std::visit([](auto&& cidr) -> int { return cidr.prefix_length(); },
                    cidr_);
}

bool IPCIDR::operator==(const IPCIDR& rhs) const {
  return cidr_ == rhs.cidr_;
}
bool IPCIDR::operator!=(const IPCIDR& rhs) const {
  return !(*this == rhs);
}

IPFamily IPCIDR::GetFamily() const {
  if (const auto ipv4 = std::get_if<IPv4CIDR>(&cidr_)) {
    return IPFamily::kIPv4;
  }
  return IPFamily::kIPv6;
}

std::optional<IPv4CIDR> IPCIDR::ToIPv4CIDR() const {
  if (const auto ipv4 = std::get_if<IPv4CIDR>(&cidr_)) {
    return *ipv4;
  }
  return std::nullopt;
}

std::optional<IPv6CIDR> IPCIDR::ToIPv6CIDR() const {
  if (const auto ipv6 = std::get_if<IPv6CIDR>(&cidr_)) {
    return *ipv6;
  }
  return std::nullopt;
}

IPAddress IPCIDR::ToNetmask() const {
  return std::visit(
      [](auto&& cidr) -> IPAddress { return IPAddress(cidr.ToNetmask()); },
      cidr_);
}

IPAddress IPCIDR::GetPrefixAddress() const {
  return std::visit(
      [](auto&& cidr) -> IPAddress {
        return IPAddress(cidr.GetPrefixAddress());
      },
      cidr_);
}

IPAddress IPCIDR::GetBroadcast() const {
  return std::visit(
      [](auto&& cidr) -> IPAddress { return IPAddress(cidr.GetBroadcast()); },
      cidr_);
}

bool IPCIDR::InSameSubnetWith(const IPAddress& b) const {
  if (const auto cidr = std::get_if<IPv4CIDR>(&cidr_)) {
    if (b.GetFamily() == IPFamily::kIPv4) {
      return cidr->InSameSubnetWith(*b.ToIPv4Address());
    }
  }
  if (const auto cidr = std::get_if<IPv6CIDR>(&cidr_)) {
    if (b.GetFamily() == IPFamily::kIPv6) {
      return cidr->InSameSubnetWith(*b.ToIPv6Address());
    }
  }

  return false;
}

std::string IPCIDR::ToString() const {
  return std::visit([](auto&& cidr) -> std::string { return cidr.ToString(); },
                    cidr_);
}

std::ostream& operator<<(std::ostream& os, IPFamily family) {
  os << ToString(family);
  return os;
}

std::ostream& operator<<(std::ostream& os, const IPAddress& address) {
  os << address.ToString();
  return os;
}

std::ostream& operator<<(std::ostream& os, const IPCIDR& cidr) {
  os << cidr.ToString();
  return os;
}

}  // namespace net_base
