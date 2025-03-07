// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/address_manager.h"

#include <climits>
#include <optional>
#include <vector>

#include <base/containers/contains.h>
#include <base/logging.h>
#include <base/rand_util.h>
#include <chromeos/net-base/ipv6_address.h>
#include <chromeos/net-base/mac_address.h>

#include "patchpanel/net_util.h"

namespace patchpanel {

namespace {

// The 100.115.92.0/24 subnet is reserved and not publicly routable. This subnet
// is sliced into the following IP pools for use among the various usages:
// +---------------+------------+----------------------------------------------+
// |   IP Range    |    Guest   |                                              |
// +---------------+------------+----------------------------------------------+
// | 0-3     (/30) | ARC        | Used for ARC management interface arc0       |
// | 4-23    (/30) | ARC        | Used to expose multiple host networks to ARC |
// | 24-63   (/30) | Termina VM | Used by Crostini, Bruschetta and Borealis    |
// | 64-127  (/28) | Containers | 2nd range used by Crostini LXD containers    |
// | 128-191 (/30) | Host netns | Used for netns hosting minijailed services   |
// | 192-255 (/28) | Containers | 1st range Used by Crostini LXD containers    |
// +---------------+------------+----------------------------------------------+
//
// The 100.115.93.0/24 subnet is reserved for Parallels VMs.

// Prefix length of allocated subnet for static ULA IPv6 addresses.
constexpr int kStaticIPv6PrefixLength = 64;

// RFC4193: IPv6 prefix of fd00::/8 is defined for locally assigned unique local
// addresses (ULA).
const net_base::IPv6CIDR kULASubnet =
    *net_base::IPv6CIDR::CreateFromStringAndPrefix("fd00::", 8);

}  // namespace

AddressManager::AddressManager() {
  pools_.emplace(
      GuestType::kArc0,
      SubnetPool::New(
          *net_base::IPv4CIDR::CreateFromCIDRString("100.115.92.0/30"), 1));
  pools_.emplace(
      GuestType::kArcNet,
      SubnetPool::New(
          *net_base::IPv4CIDR::CreateFromCIDRString("100.115.92.4/30"), 5));
  pools_.emplace(
      GuestType::kTerminaVM,
      SubnetPool::New(
          *net_base::IPv4CIDR::CreateFromCIDRString("100.115.92.24/30"), 10));
  pools_.emplace(
      GuestType::kNetns,
      SubnetPool::New(
          *net_base::IPv4CIDR::CreateFromCIDRString("100.115.92.128/30"), 16));
  pools_.emplace(
      GuestType::kLXDContainer,
      SubnetPool::New(
          *net_base::IPv4CIDR::CreateFromCIDRString("100.115.92.192/28"), 4));
  parallels_pool_ = SubnetPool::New(
      *net_base::IPv4CIDR::CreateFromCIDRString("100.115.93.0/29"), 32);
  lxd_fallback_pool_ = SubnetPool::New(
      *net_base::IPv4CIDR::CreateFromCIDRString("100.115.92.64/28"), 4);
  allocated_ipv6_subnets_.insert(kDnsProxySubnet);
}

net_base::MacAddress AddressManager::GenerateMacAddress(uint32_t index) {
  return index == kAnySubnetIndex ? mac_addrs_.Generate()
                                  : mac_addrs_.GetStable(index);
}

std::unique_ptr<Subnet> AddressManager::AllocateIPv4Subnet(GuestType guest,
                                                           uint32_t index) {
  if (guest == GuestType::kParallelsVM) {
    return parallels_pool_->Allocate(index);
  }
  if (index > 0) {
    LOG(ERROR) << ": Subnet indexing is not supported for guest " << guest;
    return nullptr;
  }
  auto it = pools_.find(guest);
  if (it == pools_.end()) {
    LOG(ERROR) << "Unsupported guest type " << guest;
    return nullptr;
  }
  auto subnet = it->second->Allocate();
  // b/385834987: If there is no more available subnets for the Crostini LXD
  // container, try to allocate a subnet from the fallback pool instead.
  if (subnet == nullptr && guest == GuestType::kLXDContainer) {
    LOG(INFO) << "Allocating LXD container subnet from fallback subnet pool";
    subnet = lxd_fallback_pool_->Allocate();
  }
  return subnet;
}

net_base::IPv6CIDR AddressManager::AllocateIPv6Subnet() {
  net_base::IPv6CIDR subnet;
  do {
    subnet = *GenerateIPv6Subnet(kULASubnet, kStaticIPv6PrefixLength);
  } while (base::Contains(allocated_ipv6_subnets_, subnet));
  allocated_ipv6_subnets_.insert(subnet);

  return subnet;
}

void AddressManager::ReleaseIPv6Subnet(const net_base::IPv6CIDR& subnet) {
  if (allocated_ipv6_subnets_.erase(subnet) == 0) {
    LOG(ERROR) << "Releasing unallocated subnet: " << subnet;
  }
}

std::optional<net_base::IPv6CIDR> AddressManager::GetRandomizedIPv6Address(
    const net_base::IPv6CIDR& subnet) {
  if (subnet.prefix_length() >= 128) {
    LOG(ERROR) << "Subnet must at least holds 1 IPv6 address, excluding the "
                  "base address. Got "
               << subnet;
    return std::nullopt;
  }

  net_base::IPv6Address::DataType addr = {};
  do {
    base::RandBytes(addr);
    std::vector<uint8_t> mask =
        net_base::IPv6CIDR::GetNetmask(subnet.prefix_length())->ToBytes();
    std::vector<uint8_t> subnet_addr = subnet.address().ToBytes();
    for (size_t i = 0; i < net_base::IPv6Address::kAddressLength; ++i) {
      addr[i] = subnet_addr[i] | (~mask[i] & addr[i]);
    }
  } while (net_base::IPv6Address(addr) == subnet.address());

  return net_base::IPv6CIDR::CreateFromAddressAndPrefix(
      net_base::IPv6Address(addr), subnet.prefix_length());
}

std::optional<net_base::IPv6CIDR> AddressManager::GenerateIPv6Subnet(
    const net_base::IPv6CIDR& net_block, int prefix_length) {
  // Avoid invalid |net_block| and |prefix_length| combination.
  if (prefix_length <= net_block.prefix_length() || prefix_length > 128) {
    LOG(ERROR) << "Given prefix length " << prefix_length
               << " must be less than network block prefix length "
               << net_block.prefix_length();
    return std::nullopt;
  }

  // Generates randomized subnet that is not equal to the base |net_block|
  // address.
  net_base::IPv6Address::DataType addr = {};
  do {
    base::RandBytes(addr);
    std::vector<uint8_t> mask =
        net_base::IPv6CIDR::GetNetmask(net_block.prefix_length())->ToBytes();
    std::vector<uint8_t> net_block_addr = net_block.address().ToBytes();
    for (size_t i = 0; i < net_base::IPv6Address::kAddressLength; ++i) {
      addr[i] = net_block_addr[i] | (~mask[i] & addr[i]);
    }
  } while (net_base::IPv6Address(addr) == net_block.address());

  return net_base::IPv6CIDR::CreateFromAddressAndPrefix(
             net_base::IPv6Address(addr), prefix_length)
      ->GetPrefixCIDR();
}

std::ostream& operator<<(std::ostream& stream,
                         const AddressManager::GuestType guest_type) {
  switch (guest_type) {
    case AddressManager::GuestType::kArc0:
      return stream << "ARC0";
    case AddressManager::GuestType::kArcNet:
      return stream << "ARC_NET";
    case AddressManager::GuestType::kTerminaVM:
      return stream << "TERMINA_VM";
    case AddressManager::GuestType::kParallelsVM:
      return stream << "PARALLELS_VM";
    case AddressManager::GuestType::kLXDContainer:
      return stream << "LXD_CONTAINER";
    case AddressManager::GuestType::kNetns:
      return stream << "MINIJAIL_NETNS";
  }
}

}  // namespace patchpanel
