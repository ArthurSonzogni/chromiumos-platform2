// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/address_manager.h"

#include <base/logging.h>

#include "patchpanel/net_util.h"

namespace patchpanel {

namespace {

// The 100.115.92.0/24 subnet is reserved and not publicly routable. This subnet
// is sliced into the following IP pools for use among the various usages:
// +---------------+------------+----------------------------------------------+
// |   IP Range    |    Guest   |                                              |
// +---------------+------------+----------------------------------------------+
// | 0       (/30) | ARC/ARCVM  | Used for ARC management interface arc0       |
// | 4-20    (/30) | ARC/ARCVM  | Used to expose multiple host networks to ARC |
// | 24-124  (/30) | Termina VM | Used by Crostini                             |
// | 128-188 (/30) | Host netns | Used for netns hosting minijailed services   |
// | 192-252 (/28) | Containers | Used by Crostini LXD user containers         |
// +---------------+------------+----------------------------------------------+
//
// The 100.115.93.0/24 subnet is reserved for Parallels VMs.

}  // namespace

AddressManager::AddressManager() {
  for (auto g :
       {GuestType::kArc0, GuestType::kArcNet, GuestType::kTerminaVM,
        GuestType::kParallelsVM, GuestType::kLXDContainer, GuestType::kNetns}) {
    uint32_t base_addr;
    uint32_t prefix_length = 30;
    uint32_t subnets = 1;
    switch (g) {
      case GuestType::kArc0:
        base_addr = Ipv4Addr(100, 115, 92, 0);
        break;
      case GuestType::kArcNet:
        base_addr = Ipv4Addr(100, 115, 92, 4);
        subnets = 5;
        break;
      case GuestType::kTerminaVM:
        base_addr = Ipv4Addr(100, 115, 92, 24);
        subnets = 26;
        break;
      case GuestType::kNetns:
        base_addr = Ipv4Addr(100, 115, 92, 128);
        prefix_length = 30;
        subnets = 16;
        break;
      case GuestType::kLXDContainer:
        base_addr = Ipv4Addr(100, 115, 92, 192);
        prefix_length = 28;
        subnets = 4;
        break;
      case GuestType::kParallelsVM:
        base_addr = Ipv4Addr(100, 115, 93, 0);
        prefix_length = 29;
        subnets = 32;
        break;
    }
    pools_.emplace(g, SubnetPool::New(base_addr, prefix_length, subnets));
  }
}

MacAddress AddressManager::GenerateMacAddress(uint32_t index) {
  return index == kAnySubnetIndex ? mac_addrs_.Generate()
                                  : mac_addrs_.GetStable(index);
}

std::unique_ptr<Subnet> AddressManager::AllocateIPv4Subnet(GuestType guest,
                                                           uint32_t index) {
  if (index > 0 && guest != GuestType::kParallelsVM) {
    LOG(ERROR) << "Subnet indexing not supported for guest";
    return nullptr;
  }
  const auto it = pools_.find(guest);
  return (it != pools_.end()) ? it->second->Allocate(index) : nullptr;
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
