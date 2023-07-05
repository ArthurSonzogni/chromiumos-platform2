// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/address_service.h"

#include <memory>

#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <net-base/ip_address.h>

namespace shill {

AddressService::AddressService() : rtnl_handler_(RTNLHandler::GetInstance()) {}

AddressService::~AddressService() = default;

// static
AddressService* AddressService::GetInstance() {
  static base::NoDestructor<AddressService> instance;
  return instance.get();
}

// static
std::unique_ptr<AddressService> AddressService::CreateForTesting(
    RTNLHandler* rtnl_handler) {
  // Using `new` to access a non-public constructor.
  auto ptr = base::WrapUnique(new AddressService());
  ptr->rtnl_handler_ = rtnl_handler;
  return ptr;
}

void AddressService::FlushAddress(int interface_index) {
  auto interface_addresses = added_addresses_.find(interface_index);
  if (interface_addresses == added_addresses_.end()) {
    return;
  }
  for (const auto& item : interface_addresses->second) {
    rtnl_handler_->RemoveInterfaceAddress(interface_index, item);
  }
  added_addresses_.erase(interface_addresses);
}

bool AddressService::RemoveAddressOtherThan(int interface_index,
                                            const net_base::IPCIDR& local) {
  auto interface_addresses = added_addresses_.find(interface_index);
  if (interface_addresses == added_addresses_.end()) {
    return false;
  }
  bool removed = false;
  for (auto iter = interface_addresses->second.begin();
       iter != interface_addresses->second.end();
       /*no-op*/) {
    if (iter->GetFamily() == local.GetFamily() && *iter != local) {
      removed = true;
      rtnl_handler_->RemoveInterfaceAddress(interface_index, *iter);
      iter = interface_addresses->second.erase(iter);
    } else {
      ++iter;
    }
  }
  return removed;
}

void AddressService::AddAddress(
    int interface_index,
    const net_base::IPCIDR& local,
    const std::optional<net_base::IPv4Address>& broadcast) {
  bool has_broadcast = broadcast != std::nullopt;
  if (local.GetFamily() == net_base::IPFamily::kIPv6 && has_broadcast) {
    LOG(WARNING) << "IPv6 does not support customized broadcase address, "
                    "using default instead.";
    has_broadcast = false;
  }
  if (!rtnl_handler_->AddInterfaceAddress(
          interface_index, local, has_broadcast ? broadcast : std::nullopt)) {
    LOG(ERROR) << __func__ << ": fail to add " << local.ToString()
               << ", broadcast: "
               << (has_broadcast ? broadcast->ToString() : "default");
  }
  added_addresses_[interface_index].push_back(local);
}

}  // namespace shill
