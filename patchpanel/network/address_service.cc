// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/network/address_service.h"

#include <map>
#include <optional>
#include <set>

#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <chromeos/net-base/ip_address.h>

#include "patchpanel/network/routing_table.h"

namespace patchpanel {

AddressService::AddressService(RoutingTable* routing_table)
    : routing_table_(routing_table),
      rtnl_handler_(net_base::RTNLHandler::GetInstance()) {}

AddressService::~AddressService() = default;

// static
std::unique_ptr<AddressService> AddressService::CreateForTesting(
    net_base::RTNLHandler* rtnl_handler, RoutingTable* routing_table) {
  // Using `new` to access a non-public constructor.
  auto ptr = base::WrapUnique(new AddressService(routing_table));
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

  auto route = RoutingTableEntry(net_base::IPFamily::kIPv4);
  route.dst = net_base::IPCIDR(current->second.GetPrefixCIDR());
  route.pref_src = net_base::IPAddress(current->second.address());
  route.scope = RT_SCOPE_LINK;
  route.table = RoutingTable::GetInterfaceTableId(interface_index);
  routing_table_->RemoveRoute(interface_index, route);
  added_ipv4_address_.erase(interface_index);
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

    auto route = RoutingTableEntry(net_base::IPFamily::kIPv4);
    route.dst = net_base::IPCIDR(current->second.GetPrefixCIDR());
    route.pref_src = net_base::IPAddress(current->second.address());
    route.scope = RT_SCOPE_LINK;
    route.table = RoutingTable::GetInterfaceTableId(interface_index);
    routing_table_->RemoveRoute(interface_index, route);
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

  // Move kernel-added local IPv4 route from main table to per-network table.
  // Note that for IPv6 kernel directly adds those routes into per-device table
  // thanks to accept_ra_rt_table.
  auto route = RoutingTableEntry(net_base::IPFamily::kIPv4);
  route.dst = net_base::IPCIDR(local.GetPrefixCIDR());
  route.pref_src = net_base::IPAddress(local.address());
  route.scope = RT_SCOPE_LINK;
  route.table = RoutingTable::GetInterfaceTableId(interface_index);
  if (!routing_table_->AddRoute(interface_index, route)) {
    LOG(ERROR) << __func__ << ": fail to add local route " << route
               << " to per-network table, keeping the kernel-added route in "
                  "main table";
    return;
  }
  route.protocol = RTPROT_KERNEL;
  route.table = RT_TABLE_MAIN;
  routing_table_->RemoveRoute(interface_index, route);
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
