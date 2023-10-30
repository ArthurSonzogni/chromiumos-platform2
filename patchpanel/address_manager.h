// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_ADDRESS_MANAGER_H_
#define PATCHPANEL_ADDRESS_MANAGER_H_

#include <map>
#include <memory>
#include <optional>

#include <base/containers/flat_set.h>
#include <base/functional/callback.h>
#include <base/memory/weak_ptr.h>
#include <brillo/brillo_export.h>
#include <net-base/ipv6_address.h>

#include "patchpanel/mac_address_generator.h"
#include "patchpanel/subnet.h"
#include "patchpanel/subnet_pool.h"

namespace patchpanel {

// Responsible for address provisioning for guest networks.
class BRILLO_EXPORT AddressManager {
 public:
  // Enum reprensenting the different types of downstream guests managed by
  // patchpanel that requires assignment of IPv4 subnets.
  enum class GuestType {
    // ARC++ or ARCVM management interface.
    kArc0,
    // ARC++ or ARCVM virtual networks connected to shill Devices.
    kArcNet,
    /// Crostini VM root namespace.
    kTerminaVM,
    // Parallels VMs.
    kParallelsVM,
    // Crostini VM user containers.
    kLXDContainer,
    // Other network namespaces hosting minijailed host processes.
    kNetns,
  };

  AddressManager();
  AddressManager(const AddressManager&) = delete;
  AddressManager& operator=(const AddressManager&) = delete;

  virtual ~AddressManager() = default;

  // Generates a MAC address guaranteed to be unique for the lifetime of this
  // object.
  // If |index| is provided, a MAC address will be returned that is stable
  // across all invocations and instantions.
  // Virtual for testing only.
  virtual MacAddress GenerateMacAddress(uint32_t index = kAnySubnetIndex);

  // Allocates a subnet from the specified guest network pool if available.
  // Returns nullptr if the guest was configured or no more subnets are
  // available for allocation.
  // |index| is used to acquire a particular subnet from the pool, if supported
  // for |guest|, it is 1-based, so 0 indicates no preference.
  std::unique_ptr<Subnet> AllocateIPv4Subnet(GuestType guest_type,
                                             uint32_t index = kAnySubnetIndex);

  // Allocates an IPv6 ULA subnet with a fixed prefix length of 64. The caller
  // is responsible to release the subnet through ReleaseIPv6Subnet().
  net_base::IPv6CIDR AllocateIPv6Subnet();

  // Releases previously allocated IPv6 subnet through AllocateIPv6Subnet().
  void ReleaseIPv6Subnet(const net_base::IPv6CIDR& subnet);

  // Gets randomized IPv6 address inside |subnet|. Caller is responsible to
  // handle possible duplicated addresses. This method guarantess that the base
  // address of |subnet| is not returned.
  static std::optional<net_base::IPv6CIDR> GetRandomizedIPv6Address(
      const net_base::IPv6CIDR& subnet);

  // Generates IPv6 subnet of |prefix_length| inside |net_block|. This method
  // guarantees that the subnet address created is not equal to the base
  // |net_block| address.
  std::optional<net_base::IPv6CIDR> GenerateIPv6Subnet(
      const net_base::IPv6CIDR& net_block, int prefix_length);

 private:
  MacAddressGenerator mac_addrs_;
  std::map<GuestType, std::unique_ptr<SubnetPool>> pools_;
  base::flat_set<net_base::IPv6CIDR> allocated_ipv6_subnets_;

  base::WeakPtrFactory<AddressManager> weak_ptr_factory_{this};
};

std::ostream& operator<<(std::ostream& stream,
                         const AddressManager::GuestType guest_type);

}  // namespace patchpanel

#endif  // PATCHPANEL_ADDRESS_MANAGER_H_
