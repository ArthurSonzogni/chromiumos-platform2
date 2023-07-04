// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NETWORK_ADDRESS_SERVICE_H_
#define SHILL_NETWORK_ADDRESS_SERVICE_H_

#include <cstdint>
#include <map>
#include <optional>
#include <vector>

#include <base/no_destructor.h>
#include <net-base/ip_address.h>

#include "shill/mockable.h"
#include "shill/net/rtnl_handler.h"

namespace shill {

// A singleton providing capability to configure address onto kernel netdevice,
// and maintaining the address information currently configured.
class AddressService {
 public:
  virtual ~AddressService();

  static AddressService* GetInstance();

  // Removes all addresses previous configured onto |interface_index|.
  mockable void FlushAddress(int interface_index);

  // Removes all configured addresses that shares a family with |local|, but not
  // |local| itself. Return true if any address removed that way.
  mockable bool RemoveAddressOtherThan(int interface_index,
                                       const net_base::IPCIDR& local);

  // Configures |local| onto |interface_index| through kernel RTNL. If |local|
  // is IPv4, a customized |broadcast| address can be specified.
  mockable void AddAddress(
      int interface_index,
      const net_base::IPCIDR& local,
      const std::optional<net_base::IPv4Address>& broadcast = std::nullopt);

 protected:
  AddressService();
  AddressService(const AddressService&) = delete;
  AddressService& operator=(const AddressService&) = delete;

 private:
  friend class base::NoDestructor<AddressService>;

  // Cache for the addresses added earlier by us, keyed by the interface id.
  std::map<uint32_t, std::vector<net_base::IPCIDR>> added_addresses_;

  // Cache singleton pointer for performance and test purposes.
  RTNLHandler* rtnl_handler_;
};
}  // namespace shill

#endif  // SHILL_NETWORK_ADDRESS_SERVICE_H_
