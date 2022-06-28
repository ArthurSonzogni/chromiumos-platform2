// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/network.h"

#include <string>
#include <vector>

#include "shill/connection.h"

namespace shill {

Network::Network(int interface_index,
                 const std::string& interface_name,
                 Technology technology,
                 bool fixed_ip_params,
                 EventHandler* event_handler,
                 DeviceInfo* device_info)
    : interface_index_(interface_index),
      interface_name_(interface_name),
      technology_(technology),
      fixed_ip_params_(fixed_ip_params),
      event_handler_(event_handler),
      device_info_(device_info) {}

void Network::SetupConnection(IPConfig* ipconfig) {
  DCHECK(ipconfig);
  if (connection_ == nullptr) {
    connection_ = std::make_unique<Connection>(
        interface_index_, interface_name_, fixed_ip_params_, technology_,
        device_info_);
  }
  const auto& blackhole_uids = event_handler_->GetBlackholedUids();
  ipconfig->SetBlackholedUids(blackhole_uids);
  connection_->UpdateFromIPConfig(ipconfig->properties());
  event_handler_->OnConnectionUpdated(ipconfig);
}

void Network::DestroyConnection() {
  connection_ = nullptr;
}

bool Network::HasConnectionObject() const {
  return connection_ != nullptr;
}

void Network::SetPriority(uint32_t priority, bool is_primary_physical) {
  CHECK(connection_) << __func__ << " called but no connection exists";
  connection_->SetPriority(priority, is_primary_physical);
}

bool Network::IsDefault() const {
  CHECK(connection_) << __func__ << " called but no connection exists";
  return connection_->IsDefault();
}

void Network::SetUseDNS(bool enable) {
  CHECK(connection_) << __func__ << " called but no connection exists";
  connection_->SetUseDNS(enable);
}

void Network::UpdateDNSServers(const std::vector<std::string>& dns_servers) {
  CHECK(connection_) << __func__ << " called but no connection exists";
  connection_->UpdateDNSServers(dns_servers);
}

void Network::UpdateRoutingPolicy() {
  CHECK(connection_) << __func__ << " called but no connection exists";
  connection_->UpdateRoutingPolicy();
}

std::string Network::GetSubnetName() const {
  CHECK(connection_) << __func__ << " called but no connection exists";
  return connection_->GetSubnetName();
}

bool Network::IsIPv6() const {
  CHECK(connection_) << __func__ << " called but no connection exists";
  return connection_->IsIPv6();
}

const std::vector<std::string>& Network::dns_servers() const {
  CHECK(connection_) << __func__ << " called but no connection exists";
  return connection_->dns_servers();
}

const IPAddress& Network::local() const {
  CHECK(connection_) << __func__ << " called but no connection exists";
  return connection_->local();
}

const IPAddress& Network::gateway() const {
  CHECK(connection_) << __func__ << " called but no connection exists";
  return connection_->gateway();
}

}  // namespace shill
