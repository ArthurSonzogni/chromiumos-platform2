// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/network.h"

#include <string>
#include <vector>

#include "shill/connection.h"
#include "shill/event_dispatcher.h"
#include "shill/service.h"

namespace shill {

Network::Network(int interface_index,
                 const std::string& interface_name,
                 Technology technology,
                 bool fixed_ip_params,
                 EventHandler* event_handler,
                 DeviceInfo* device_info,
                 EventDispatcher* dispatcher)
    : interface_index_(interface_index),
      interface_name_(interface_name),
      technology_(technology),
      fixed_ip_params_(fixed_ip_params),
      event_handler_(event_handler),
      device_info_(device_info),
      dispatcher_(dispatcher) {}

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

  const bool ipconfig_changed = current_ipconfig_ == ipconfig;
  current_ipconfig_ = ipconfig;
  if (ipconfig_changed && !current_ipconfig_change_handler_.is_null()) {
    current_ipconfig_change_handler_.Run();
  }
}

void Network::DestroyConnection() {
  connection_ = nullptr;
}

bool Network::HasConnectionObject() const {
  return connection_ != nullptr;
}

void Network::OnIPv4ConfigUpdated() {
  auto* selected_service = event_handler_->GetSelectedService();
  if (selected_service) {
    ipconfig()->ApplyStaticIPParameters(
        selected_service->mutable_static_ip_parameters());
    if (selected_service->HasStaticIPAddress() && dhcp_controller()) {
      // If we are using a statically configured IP address instead
      // of a leased IP address, release any acquired lease so it may
      // be used by others.  This allows us to merge other non-leased
      // parameters (like DNS) when they're available from a DHCP server
      // and not overridden by static parameters, but at the same time
      // we avoid taking up a dynamic IP address the DHCP server could
      // assign to someone else who might actually use it.
      dhcp_controller()->ReleaseIP(DHCPController::kReleaseReasonStaticIP);
    }
  }
  SetupConnection(ipconfig());
  event_handler_->OnIPConfigsPropertyUpdated();
}

void Network::ConfigureStaticIPTask() {
  auto* selected_service = event_handler_->GetSelectedService();
  if (!selected_service || !ipconfig()) {
    return;
  }
  if (!selected_service->HasStaticIPAddress()) {
    return;
  }
  // If the parameters contain an IP address, apply them now and bring
  // the interface up.  When DHCP information arrives, it will supplement
  // the static information.
  OnIPv4ConfigUpdated();
}

void Network::OnStaticIPConfigChanged() {
  auto* selected_service = event_handler_->GetSelectedService();
  if (!ipconfig() || !selected_service) {
    LOG(ERROR) << __func__ << " called but "
               << (!ipconfig() ? "no IPv4 config" : "no selected service");
    return;
  }

  // Clear the previously applied static IP parameters.
  ipconfig()->RestoreSavedIPParameters(
      selected_service->mutable_static_ip_parameters());

  dispatcher_->PostTask(
      FROM_HERE, base::BindOnce(&Network::ConfigureStaticIPTask, AsWeakPtr()));

  if (dhcp_controller()) {
    // Trigger DHCP renew.
    dhcp_controller()->RenewIP();
  }
}

void Network::RegisterCurrentIPConfigChangeHandler(
    base::RepeatingClosure handler) {
  current_ipconfig_change_handler_ = handler;
}

IPConfig* Network::GetCurrentIPConfig() const {
  // Make sure that the |current_ipconfig_| is still valid.
  if (current_ipconfig_ == ipconfig_.get()) {
    return current_ipconfig_;
  }
  if (current_ipconfig_ == ip6config_.get()) {
    return current_ipconfig_;
  }
  return nullptr;
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
