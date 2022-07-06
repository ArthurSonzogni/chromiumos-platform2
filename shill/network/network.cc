// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/network.h"

#include <string>
#include <vector>

#include <base/containers/contains.h>
#include <base/files/file_util.h>
#include <base/notreached.h>
#include <base/strings/stringprintf.h>

#include "shill/connection.h"
#include "shill/event_dispatcher.h"
#include "shill/service.h"

namespace shill {

namespace {

constexpr char kIPFlagTemplate[] = "/proc/sys/net/%s/conf/%s/%s";
constexpr char kIPFlagVersion4[] = "ipv4";
constexpr char kIPFlagVersion6[] = "ipv6";

constexpr char kIPFlagAcceptDuplicateAddressDetection[] = "accept_dad";
constexpr char kIPFlagAcceptDuplicateAddressDetectionEnabled[] = "1";
constexpr char kIPFlagAcceptRouterAdvertisements[] = "accept_ra";
constexpr char kIPFlagAcceptRouterAdvertisementsAlways[] = "2";
constexpr char kIPFlagDisableIPv6[] = "disable_ipv6";
constexpr char kIPFlagUseTempAddr[] = "use_tempaddr";
constexpr char kIPFlagUseTempAddrUsedAndDefault[] = "2";

}  // namespace

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
  saved_network_config_ =
      ipconfig()->ApplyNetworkConfig(static_network_config_);
  if (static_network_config_.ipv4_address_cidr.has_value() &&
      dhcp_controller()) {
    // If we are using a statically configured IP address instead of a leased IP
    // address, release any acquired lease so it may be used by others.  This
    // allows us to merge other non-leased parameters (like DNS) when they're
    // available from a DHCP server and not overridden by static parameters, but
    // at the same time we avoid taking up a dynamic IP address the DHCP server
    // could assign to someone else who might actually use it.
    dhcp_controller()->ReleaseIP(DHCPController::kReleaseReasonStaticIP);
  }
  SetupConnection(ipconfig());
  event_handler_->OnIPConfigsPropertyUpdated();
}

void Network::ConfigureStaticIPTask() {
  if (!ipconfig()) {
    return;
  }
  if (!static_network_config_.ipv4_address_cidr.has_value()) {
    return;
  }
  // If the parameters contain an IP address, apply them now and bring
  // the interface up.  When DHCP information arrives, it will supplement
  // the static information.
  OnIPv4ConfigUpdated();
}

void Network::OnStaticIPConfigChanged(const NetworkConfig& config) {
  static_network_config_ = config;
  if (!ipconfig()) {
    // This can happen after service is selected but before the Network is
    // "started" (e.g., by starting DHCP).
    return;
  }

  // Clear the previously applied static IP parameters. The new config will be
  // applied in ConfigureStaticIPTask().
  ipconfig()->ApplyNetworkConfig(saved_network_config_);
  saved_network_config_ = {};

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

void Network::StopIPv6() {
  SetIPFlag(IPAddress::kFamilyIPv6, kIPFlagDisableIPv6, "1");
}

void Network::StartIPv6() {
  SetIPFlag(IPAddress::kFamilyIPv6, kIPFlagDisableIPv6, "0");

  SetIPFlag(IPAddress::kFamilyIPv6, kIPFlagAcceptDuplicateAddressDetection,
            kIPFlagAcceptDuplicateAddressDetectionEnabled);

  // Force the kernel to accept RAs even when global IPv6 forwarding is
  // enabled.  Unfortunately this needs to be set on a per-interface basis.
  SetIPFlag(IPAddress::kFamilyIPv6, kIPFlagAcceptRouterAdvertisements,
            kIPFlagAcceptRouterAdvertisementsAlways);
}

void Network::EnableIPv6Privacy() {
  SetIPFlag(IPAddress::kFamilyIPv6, kIPFlagUseTempAddr,
            kIPFlagUseTempAddrUsedAndDefault);
}

void Network::StartIPv6DNSServerTimer(base::TimeDelta delay) {
  ipv6_dns_server_expired_callback_.Reset(base::BindOnce(
      &Network::IPv6DNSServerExpired, weak_factory_.GetWeakPtr()));
  dispatcher_->PostDelayedTask(
      FROM_HERE, ipv6_dns_server_expired_callback_.callback(), delay);
}

void Network::StopIPv6DNSServerTimer() {
  ipv6_dns_server_expired_callback_.Cancel();
}

void Network::IPv6DNSServerExpired() {
  if (!ip6config()) {
    return;
  }
  ip6config()->UpdateDNSServers(std::vector<std::string>());
  event_handler_->OnIPConfigsPropertyUpdated();
}

bool Network::SetIPFlag(IPAddress::Family family,
                        const std::string& flag,
                        const std::string& value) {
  std::string ip_version;
  if (family == IPAddress::kFamilyIPv4) {
    ip_version = kIPFlagVersion4;
  } else if (family == IPAddress::kFamilyIPv6) {
    ip_version = kIPFlagVersion6;
  } else {
    NOTIMPLEMENTED();
  }
  base::FilePath flag_file(
      base::StringPrintf(kIPFlagTemplate, ip_version.c_str(),
                         interface_name_.c_str(), flag.c_str()));
  if (base::WriteFile(flag_file, value.c_str(), value.length()) != 1) {
    const auto message =
        base::StringPrintf("IP flag write failed: %s to %s", value.c_str(),
                           flag_file.value().c_str());
    if (base::PathExists(flag_file) ||
        !base::Contains(written_flags_, flag_file.value())) {
      // Leave a log if the file is there or this is the first time we try to
      // write it on a failure.
      LOG(ERROR) << interface_name_ << ": " << message;
    }
    return false;
  } else {
    written_flags_.insert(flag_file.value());
  }
  return true;
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
