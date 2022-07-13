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
                 ControlInterface* control_interface,
                 DeviceInfo* device_info,
                 EventDispatcher* dispatcher)
    : interface_index_(interface_index),
      interface_name_(interface_name),
      technology_(technology),
      fixed_ip_params_(fixed_ip_params),
      event_handler_(event_handler),
      control_interface_(control_interface),
      device_info_(device_info),
      dispatcher_(dispatcher),
      dhcp_provider_(DHCPProvider::GetInstance()) {}

void Network::Start(const Network::StartOptions& opts) {
  Stop();

  if (opts.accept_ra) {
    StartIPv6();
  }

  // Note that currently, the existence of ipconfig_ indicates if the IPv4 part
  // of Network has been started.
  bool dhcp_started = false;
  if (opts.dhcp) {
    set_dhcp_controller(dhcp_provider_->CreateController(
        interface_name_, opts.dhcp.value(), technology_));
    dhcp_controller_->RegisterCallbacks(
        base::BindRepeating(&Network::OnIPConfigUpdatedFromDHCP, AsWeakPtr()),
        base::BindRepeating(&Network::OnDHCPFailure, AsWeakPtr()));
    set_ipconfig(std::make_unique<IPConfig>(control_interface_, interface_name_,
                                            IPConfig::kTypeDHCP));
    dhcp_started = dhcp_controller_->RequestIP();
  } else if (link_protocol_ipv4_properties_) {
    set_ipconfig(
        std::make_unique<IPConfig>(control_interface_, interface_name_));
    ipconfig_->set_properties(*link_protocol_ipv4_properties_);
  } else {
    NOTREACHED();
  }

  if (link_protocol_ipv4_properties_ ||
      static_network_config_.ipv4_address_cidr) {
    // If the parameters contain an IP address, apply them now and bring the
    // interface up.  When DHCP information arrives, it will supplement the
    // static information.
    dispatcher_->PostTask(
        FROM_HERE, base::BindOnce(&Network::OnIPv4ConfigUpdated, AsWeakPtr()));
    return;
  } else if (!dhcp_started) {
    // We don't have any kind of IPv4 config, and DHCP failed immediately.
    // Triggers a failure here.
    dispatcher_->PostTask(FROM_HERE,
                          base::BindOnce(&Network::StopInternal, AsWeakPtr(),
                                         /*is_failure*/ true));
  }
}

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

void Network::Stop() {
  StopInternal(/*is_failure=*/false);
}

void Network::StopInternal(bool is_failure) {
  StopIPv6();
  bool ipconfig_changed = false;
  if (dhcp_controller_) {
    dhcp_controller_->ReleaseIP(DHCPController::kReleaseReasonDisconnect);
    set_dhcp_controller(nullptr);
  }
  if (ipconfig()) {
    set_ipconfig(nullptr);
    link_protocol_ipv4_properties_ = {};
    ipconfig_changed = true;
  }
  // Make sure the timer is stopped regardless of the ip6config state. Just in
  // case that they are not synced.
  StopIPv6DNSServerTimer();
  if (ip6config()) {
    set_ip6config(nullptr);
    ipconfig_changed = true;
  }
  // Emit updated IP configs if there are any changes.
  if (ipconfig_changed) {
    event_handler_->OnIPConfigsPropertyUpdated();
  }
  connection_ = nullptr;
  event_handler_->OnNetworkStopped(is_failure);
}

bool Network::HasConnectionObject() const {
  return connection_ != nullptr;
}

void Network::OnIPv4ConfigUpdated() {
  if (!ipconfig()) {
    return;
  }
  saved_network_config_ =
      ipconfig()->ApplyNetworkConfig(static_network_config_);
  if (static_network_config_.ipv4_address_cidr.has_value() &&
      dhcp_controller_) {
    // If we are using a statically configured IP address instead of a leased IP
    // address, release any acquired lease so it may be used by others.  This
    // allows us to merge other non-leased parameters (like DNS) when they're
    // available from a DHCP server and not overridden by static parameters, but
    // at the same time we avoid taking up a dynamic IP address the DHCP server
    // could assign to someone else who might actually use it.
    dhcp_controller_->ReleaseIP(DHCPController::kReleaseReasonStaticIP);
  }
  SetupConnection(ipconfig());
  event_handler_->OnIPConfigsPropertyUpdated();
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

  // TODO(b/232177767): Apply the static IP parameters no matter if there is a
  // valid IPv4 in it.
  if (config.ipv4_address_cidr.has_value()) {
    dispatcher_->PostTask(
        FROM_HERE, base::BindOnce(&Network::OnIPv4ConfigUpdated, AsWeakPtr()));
  }

  if (dhcp_controller_) {
    // Trigger DHCP renew.
    dhcp_controller_->RenewIP();
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

void Network::OnIPConfigUpdatedFromDHCP(const IPConfig::Properties& properties,
                                        bool new_lease_acquired) {
  // |dhcp_controller_| cannot be empty when the callback is invoked.
  DCHECK(dhcp_controller_);
  DCHECK(ipconfig());
  ipconfig()->UpdateProperties(properties);
  OnIPv4ConfigUpdated();
  if (new_lease_acquired) {
    event_handler_->OnGetDHCPLease();
  }
}

void Network::OnDHCPFailure() {
  event_handler_->OnGetDHCPFailure();

  // |dhcp_controller_| cannot be empty when the callback is invoked.
  DCHECK(dhcp_controller_);
  DCHECK(ipconfig());
  if (static_network_config_.ipv4_address_cidr.has_value()) {
    // Consider three cases:
    //
    // 1. We're here because DHCP failed while starting up. There
    //    are two subcases:
    //    a. DHCP has failed, and Static IP config has _not yet_
    //       completed. It's fine to do nothing, because we'll
    //       apply the static config shortly.
    //    b. DHCP has failed, and Static IP config has _already_
    //       completed. It's fine to do nothing, because we can
    //       continue to use the static config that's already
    //       been applied.
    //
    // 2. We're here because a previously valid DHCP configuration
    //    is no longer valid. There's still a static IP config,
    //    because the condition in the if clause evaluated to true.
    //    Furthermore, the static config includes an IP address for
    //    us to use.
    //
    //    The current configuration may include some DHCP
    //    parameters, overridden by any static parameters
    //    provided. We continue to use this configuration, because
    //    the only configuration element that is leased to us (IP
    //    address) will be overridden by a static parameter.
    return;
  }

  ipconfig()->ResetProperties();
  event_handler_->OnIPConfigsPropertyUpdated();

  // Fallback to IPv6 if possible.
  if (ip6config() && ip6config()->properties().HasIPAddressAndDNS()) {
    if (!connection_ || !connection_->IsIPv6()) {
      // Setup IPv6 connection is there isn's one.
      SetupConnection(ip6config());
    }
    return;
  }

  StopInternal(/*is_failure=*/true);
}

bool Network::RenewDHCPLease() {
  if (!dhcp_controller_) {
    return false;
  }
  return dhcp_controller_->RenewIP();
}

void Network::DestroyDHCPLease(const std::string& name) {
  dhcp_provider_->DestroyLease(name);
}

std::optional<base::TimeDelta> Network::TimeToNextDHCPLeaseRenewal() {
  if (!dhcp_controller_) {
    return std::nullopt;
  }
  return dhcp_controller_->TimeToLeaseExpiry();
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
