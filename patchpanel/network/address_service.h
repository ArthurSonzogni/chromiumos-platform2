// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_NETWORK_ADDRESS_SERVICE_H_
#define PATCHPANEL_NETWORK_ADDRESS_SERVICE_H_

#include <map>
#include <memory>
#include <optional>
#include <vector>

#include <base/no_destructor.h>
#include <chromeos/net-base/ip_address.h>
#include <chromeos/net-base/rtnl_handler.h>

namespace patchpanel {

class RoutingTable;

// A singleton providing capability to configure address onto kernel netdevice,
// and maintaining the address information currently configured.
class AddressService {
 public:
  virtual ~AddressService();

  explicit AddressService(RoutingTable* routing_table);
  AddressService(const AddressService&) = delete;
  AddressService& operator=(const AddressService&) = delete;

  // Helper factory function for test code with dependency injection.
  static std::unique_ptr<AddressService> CreateForTesting(
      net_base::RTNLHandler* rtnl_handler, RoutingTable* routing_table);

  // Removes all addresses previous configured onto |interface_index|.
  virtual void FlushAddress(int interface_index);

  // Configures |local| onto |interface_index| through kernel RTNL. A customized
  // |broadcast| address can be specified. If an IPv4 address was already set
  // through AddressService, the old address will be removed first.
  virtual void SetIPv4Address(
      int interface_index,
      const net_base::IPv4CIDR& local,
      const std::optional<net_base::IPv4Address>& broadcast = std::nullopt);

  // Removes the IPv4 address previously configured onto |interface_index|
  // through AddressService.
  virtual void ClearIPv4Address(int interface_index);

  // Configure |addresses| onto |interface_index| through kernel RTNL. All
  // previous IPv6 addresses set through AddressService but not in |addresses|
  // will be removed. The addresses added by other parties (e.g. kernel) will
  // not be affected.
  virtual void SetIPv6Addresses(
      int interface_index, const std::vector<net_base::IPv6CIDR>& addresses);

 private:
  friend class base::NoDestructor<AddressService>;

  // Cache for the addresses added earlier by us, keyed by the interface id.
  std::map<int, net_base::IPv4CIDR> added_ipv4_address_;
  std::map<int, std::vector<net_base::IPv6CIDR>> added_ipv6_addresses_;

  // Owned by the same NetworkApplier that owns the instance of AddressService.
  RoutingTable* routing_table_;

  // Cache singleton pointer for performance and test purposes.
  net_base::RTNLHandler* rtnl_handler_;
};
}  // namespace patchpanel

#endif  // PATCHPANEL_NETWORK_ADDRESS_SERVICE_H_
