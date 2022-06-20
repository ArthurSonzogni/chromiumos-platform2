
// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/network.h"

#include <string>
#include <vector>

#include "shill/connection.h"

namespace shill {

Network::Network() {}

void Network::CreateConnection(int interface_index,
                               const std::string& interface_name,
                               bool fixed_ip_params,
                               Technology technology,
                               const DeviceInfo* device_info) {
  if (connection_ != nullptr) {
    return;
  }
  connection_ =
      std::make_unique<Connection>(interface_index, interface_name,
                                   fixed_ip_params, technology, device_info);
}

void Network::DestroyConnection() {
  connection_ = nullptr;
}

bool Network::HasConnectionObject() const {
  return connection_ != nullptr;
}

void Network::UpdateFromIPConfig(const IPConfig::Properties& config) {
  CHECK(connection_) << __func__ << " called but no connection exists";
  connection_->UpdateFromIPConfig(config);
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

std::string Network::interface_name() const {
  CHECK(connection_) << __func__ << " called but no connection exists";
  return connection_->interface_name();
}

int Network::interface_index() const {
  CHECK(connection_) << __func__ << " called but no connection exists";
  return connection_->interface_index();
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
