// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/network/address_service.h"

#include <map>
#include <optional>
#include <set>

#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <net-base/ip_address.h>

namespace patchpanel {

AddressService::AddressService()
    : rtnl_handler_(net_base::RTNLHandler::GetInstance()) {}

AddressService::~AddressService() = default;

// static
std::unique_ptr<AddressService> AddressService::CreateForTesting(
    net_base::RTNLHandler* rtnl_handler) {
  // Using `new` to access a non-public constructor.
  auto ptr = base::WrapUnique(new AddressService());
  ptr->rtnl_handler_ = rtnl_handler;
  return ptr;
}

void AddressService::FlushAddress(int interface_index) {
  ClearIPv4Address(interface_index);
  SetIPv6Addresses(interface_index, {});
}

void AddressService::ClearIPv4Address(int interface_index) {
  auto current = added_ipv4_address_.find(interface_index);
  if (current == added_ipv4_address_.end()) {
    return;
  }
  rtnl_handler_->RemoveInterfaceAddress(interface_index,
                                        net_base::IPCIDR(current->second));
  added_ipv4_address_.erase(current);
}

void AddressService::SetIPv4Address(
    int interface_index,
    const net_base::IPv4CIDR& local,
    const std::optional<net_base::IPv4Address>& broadcast) {
  auto current = added_ipv4_address_.find(interface_index);
  if (current != added_ipv4_address_.end()) {
    if (current->second == local) {
      return;
    }
    LOG(INFO) << __func__ << ": removing existing address " << current->second;
    rtnl_handler_->RemoveInterfaceAddress(interface_index,
                                          net_base::IPCIDR(current->second));
  }

  if (!rtnl_handler_->AddInterfaceAddress(interface_index,
                                          net_base::IPCIDR(local), broadcast)) {
    LOG(ERROR) << __func__ << ": fail to add " << local.ToString()
               << ", broadcast: "
               << (broadcast.has_value() ? broadcast->ToString() : "default");
  } else {
    LOG(INFO) << __func__ << ": adding new address " << local;
  }
  added_ipv4_address_[interface_index] = local;
}

void AddressService::SetIPv6Addresses(
    int interface_index, const std::vector<net_base::IPv6CIDR>& addresses) {
  std::set<net_base::IPv6CIDR> to_add(addresses.begin(), addresses.end());
  auto current_addresses = added_ipv6_addresses_.find(interface_index);
  if (current_addresses != added_ipv6_addresses_.end()) {
    for (auto iter = current_addresses->second.begin();
         iter != current_addresses->second.end();
         /*no-op*/) {
      if (to_add.contains(*iter)) {
        to_add.erase(*iter);
        ++iter;
      } else {
        LOG(INFO) << __func__ << ": removing existing address " << *iter;
        rtnl_handler_->RemoveInterfaceAddress(interface_index,
                                              net_base::IPCIDR(*iter));
        iter = current_addresses->second.erase(iter);
      }
    }
  }
  for (const auto& address : to_add) {
    if (!rtnl_handler_->AddInterfaceAddress(interface_index,
                                            net_base::IPCIDR(address),
                                            /*broadcast=*/std::nullopt)) {
      LOG(ERROR) << __func__ << ": fail to add " << address.ToString();
    } else {
      LOG(INFO) << __func__ << ": adding new address " << address;
    }
    added_ipv6_addresses_[interface_index].push_back(address);
  }
}

}  // namespace patchpanel
