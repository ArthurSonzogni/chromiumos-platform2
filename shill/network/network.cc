// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/network.h"

#include <algorithm>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <base/containers/fixed_flat_map.h>
#include <base/files/file_util.h>
#include <base/functional/bind.h>
#include <base/functional/callback_helpers.h>
#include <base/notreached.h>
#include <base/observer_list.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <brillo/http/http_request.h>
#include <chromeos/dbus/service_constants.h>
#include <chromeos/patchpanel/dbus/client.h>
#include <net-base/ip_address.h>
#include <net-base/ipv4_address.h>
#include <net-base/ipv6_address.h>
#include <net-base/network_config.h>
#include <net-base/network_priority.h>
#include <net-base/proc_fs_stub.h>

#include "shill/event_dispatcher.h"
#include "shill/ipconfig.h"
#include "shill/logging.h"
#include "shill/metrics.h"
#include "shill/network/compound_network_config.h"
#include "shill/network/dhcpv4_config.h"
#include "shill/network/network_applier.h"
#include "shill/network/network_monitor.h"
#include "shill/network/slaac_controller.h"
#include "shill/network/validation_log.h"
#include "shill/service.h"
#include "shill/technology.h"

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kDevice;
}  // namespace Logging

namespace {
// Constant string advertised in DHCP Vendor option 43 by Android devices
// sharing a metered network (typically a Cellular network) via tethering
// over a WiFi hotspot or a USB ethernet connection.
constexpr char kAndroidMeteredHotspotVendorOption[] = "ANDROID_METERED";
}  // namespace

Network::Network(int interface_index,
                 const std::string& interface_name,
                 Technology technology,
                 bool fixed_ip_params,
                 ControlInterface* control_interface,
                 EventDispatcher* dispatcher,
                 Metrics* metrics,
                 patchpanel::Client* patchpanel_client,
                 NetworkApplier* network_applier,
                 Resolver* resolver,
                 std::unique_ptr<NetworkMonitorFactory> network_monitor_factory)
    : interface_index_(interface_index),
      interface_name_(interface_name),
      technology_(technology),
      logging_tag_(interface_name),
      fixed_ip_params_(fixed_ip_params),
      proc_fs_(std::make_unique<net_base::ProcFsStub>(interface_name_)),
      config_(logging_tag_),
      network_monitor_factory_(std::move(network_monitor_factory)),
      control_interface_(control_interface),
      dispatcher_(dispatcher),
      metrics_(metrics),
      dhcp_provider_(DHCPProvider::GetInstance()),
      rtnl_handler_(net_base::RTNLHandler::GetInstance()),
      patchpanel_client_(patchpanel_client),
      network_applier_(network_applier),
      resolver_(resolver) {}

Network::~Network() {
  for (auto& ev : event_handlers_) {
    ev.OnNetworkDestroyed(interface_index_);
  }
}

void Network::RegisterEventHandler(EventHandler* handler) {
  if (event_handlers_.HasObserver(handler)) {
    return;
  }
  event_handlers_.AddObserver(handler);
}

void Network::UnregisterEventHandler(EventHandler* handler) {
  event_handlers_.RemoveObserver(handler);
}

void Network::Start(const Network::StartOptions& opts) {
  ignore_link_monitoring_ = opts.ignore_link_monitoring;
  ipv4_gateway_found_ = false;
  ipv6_gateway_found_ = false;

  probing_configuration_ = opts.probing_configuration;
  network_monitor_ = network_monitor_factory_->Create(
      dispatcher_, metrics_, technology_, interface_name_,
      probing_configuration_,
      base::BindRepeating(&Network::OnPortalDetectorResult, AsWeakPtr()),
      std::make_unique<ValidationLog>(technology_, metrics_), logging_tag_);

  // TODO(b/232177767): Log the StartOptions and other parameters.
  if (state_ != State::kIdle) {
    LOG(INFO) << *this
              << ": Network has been started, stop it before starting with the "
                 "new options";
    StopInternal(/*is_failure=*/false, /*trigger_callback=*/false);
  }

  network_applier_->Register(interface_index_, interface_name_);
  EnableARPFiltering();

  // If the execution of this function fails, StopInternal() will be called and
  // turn the state to kIdle.
  state_ = State::kConfiguring;

  bool ipv6_started = false;
  if (opts.accept_ra) {
    slaac_controller_ = CreateSLAACController();
    slaac_controller_->RegisterCallback(
        base::BindRepeating(&Network::OnUpdateFromSLAAC, AsWeakPtr()));
    slaac_controller_->Start(opts.link_local_address);
    ipv6_started = true;
  } else if (config_.GetLinkProtocol() &&
             !config_.GetLinkProtocol()->ipv6_addresses.empty()) {
    proc_fs_->SetIPFlag(net_base::IPFamily::kIPv6,
                        net_base::ProcFsStub::kIPFlagDisableIPv6, "0");
    UpdateIPConfigDBusObject();
    dispatcher_->PostTask(
        FROM_HERE,
        base::BindOnce(&Network::SetupConnection, AsWeakPtr(),
                       net_base::IPFamily::kIPv6, /*is_slaac=*/false));
    ipv6_started = true;
  }

  // Note that currently, the existence of ipconfig_ indicates if the IPv4 part
  // of Network has been started.
  bool dhcp_started = false;
  if (opts.dhcp) {
    auto dhcp_opts = *opts.dhcp;
    if (config_.GetStatic().ipv4_address) {
      dhcp_opts.use_arp_gateway = false;
    }
    dhcp_controller_ = dhcp_provider_->CreateController(interface_name_,
                                                        dhcp_opts, technology_);
    dhcp_controller_->RegisterCallbacks(
        base::BindRepeating(&Network::OnIPConfigUpdatedFromDHCP, AsWeakPtr()),
        base::BindRepeating(&Network::OnDHCPDrop, AsWeakPtr()));
    // Keep the legacy behavior that Network has a empty IPConfig if DHCP has
    // started but not succeeded/failed yet.
    set_ipconfig(std::make_unique<IPConfig>(control_interface_, interface_name_,
                                            IPConfig::kTypeDHCP));
    dhcp_started = dhcp_controller_->RequestIP();
  }

  if ((config_.GetLinkProtocol() && config_.GetLinkProtocol()->ipv4_address) ||
      config_.GetStatic().ipv4_address) {
    UpdateIPConfigDBusObject();
    // If the parameters contain an IP address, apply them now and bring the
    // interface up.  When DHCP information arrives, it will supplement the
    // static information.
    dispatcher_->PostTask(
        FROM_HERE, base::BindOnce(&Network::OnIPv4ConfigUpdated, AsWeakPtr()));
  } else if (!dhcp_started && !ipv6_started) {
    // Neither v4 nor v6 is running, trigger the failure callback directly.
    LOG(WARNING) << *this << ": Failed to start IP provisioning";
    dispatcher_->PostTask(
        FROM_HERE,
        base::BindOnce(&Network::StopInternal, AsWeakPtr(),
                       /*is_failure=*/true, /*trigger_callback=*/true));
  }

  LOG(INFO) << *this << ": Started IP provisioning, dhcp: "
            << (dhcp_started ? "started" : "no")
            << ", accept_ra: " << std::boolalpha << opts.accept_ra;
  LOG(INFO) << *this << " initial config: " << config_;
}

std::unique_ptr<SLAACController> Network::CreateSLAACController() {
  auto slaac_controller = std::make_unique<SLAACController>(
      interface_index_, proc_fs_.get(), rtnl_handler_, dispatcher_);
  return slaac_controller;
}

void Network::SetupConnection(net_base::IPFamily family, bool is_slaac) {
  LOG(INFO) << *this << ": Setting " << family << " connection";
  NetworkConfigArea to_apply = NetworkConfigArea::kRoutingPolicy |
                               NetworkConfigArea::kDNS |
                               NetworkConfigArea::kMTU;
  if (family == net_base::IPFamily::kIPv4) {
    if (!fixed_ip_params_) {
      to_apply |= NetworkConfigArea::kIPv4Address;
    }
    to_apply |= NetworkConfigArea::kIPv4Route;
    to_apply |= NetworkConfigArea::kIPv4DefaultRoute;
  } else {
    if (!fixed_ip_params_ && !is_slaac) {
      to_apply |= NetworkConfigArea::kIPv6Address;
    }
    to_apply |= NetworkConfigArea::kIPv6Route;
    if (!is_slaac) {
      to_apply |= NetworkConfigArea::kIPv6DefaultRoute;
    }
  }
  if (family == net_base::IPFamily::kIPv6 &&
      primary_family_ == net_base::IPFamily::kIPv4) {
    // This means network loses IPv4 so we need to clear the old configuration
    // from kernel first.
    to_apply |= NetworkConfigArea::kClear;
  }

  bool current_ipconfig_changed = primary_family_ != family;
  primary_family_ = family;
  if (current_ipconfig_changed && !current_ipconfig_change_handler_.is_null()) {
    current_ipconfig_change_handler_.Run();
  }
  ApplyNetworkConfig(to_apply,
                     base::BindOnce(&Network::OnSetupConnectionFinished,
                                    weak_factory_for_connection_.GetWeakPtr()));
}

void Network::OnSetupConnectionFinished(bool success) {
  LOG(INFO) << *this << ": " << __func__;
  if (!success) {
    // TODO(b/293997937): Properly handle RPC failure case.
  }

  if (state_ != State::kConnected && technology_ != Technology::kVPN) {
    // The Network becomes connected, wait for 30 seconds to report its IP type.
    // Skip VPN since it's already reported separately in VPNService.
    dispatcher_->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&Network::ReportIPType,
                       weak_factory_for_connection_.GetWeakPtr()),
        base::Seconds(30));
  }
  state_ = State::kConnected;

  for (auto& ev : event_handlers_) {
    ev.OnConnectionUpdated(interface_index_);
  }
}

void Network::Stop() {
  StopInternal(/*is_failure=*/false, /*trigger_callback=*/true);
}

void Network::StopInternal(bool is_failure, bool trigger_callback) {
  LOG(INFO) << *this << ": Stopping "
            << (is_failure ? "after failure" : "normally")
            << ", network config: " << config_.Get();

  weak_factory_for_connection_.InvalidateWeakPtrs();

  network_validation_result_.reset();
  StopPortalDetection();
  network_monitor_.reset();
  StopConnectionDiagnostics();

  const bool should_trigger_callback =
      state_ != State::kIdle && trigger_callback;
  bool ipconfig_changed = false;
  if (dhcp_controller_) {
    dhcp_controller_->ReleaseIP(DHCPController::ReleaseReason::kDisconnect);
    dhcp_controller_ = nullptr;
  }
  if (ipconfig()) {
    set_ipconfig(nullptr);
    ipconfig_changed = true;
  }
  if (slaac_controller_) {
    slaac_controller_->Stop();
    slaac_controller_ = nullptr;
  }
  if (ip6config()) {
    set_ip6config(nullptr);
    ipconfig_changed = true;
  }
  config_.Clear();
  dhcp_data_ = std::nullopt;
  // Emit updated IP configs if there are any changes.
  if (ipconfig_changed) {
    for (auto& ev : event_handlers_) {
      ev.OnIPConfigsPropertyUpdated(interface_index_);
    }
  }
  if (primary_family_) {
    primary_family_ = std::nullopt;
    if (!current_ipconfig_change_handler_.is_null()) {
      current_ipconfig_change_handler_.Run();
    }
  }
  state_ = State::kIdle;
  network_applier_->Release(interface_index_, interface_name_);
  priority_ = net_base::NetworkPriority{};
  if (should_trigger_callback) {
    for (auto& ev : event_handlers_) {
      ev.OnNetworkStopped(interface_index_, is_failure);
    }
  }
}

void Network::InvalidateIPv6Config() {
  SLOG(2) << *this << ": " << __func__;
  if (config_.Get().ipv6_addresses.empty()) {
    return;
  }

  SLOG(2) << *this << "Waiting for new IPv6 configuration";
  if (slaac_controller_) {
    slaac_controller_->Stop();
    config_.SetFromSLAAC(nullptr);
    slaac_controller_->Start();
  }

  UpdateIPConfigDBusObject();
  for (auto& ev : event_handlers_) {
    ev.OnIPConfigsPropertyUpdated(interface_index_);
  }
}

void Network::OnIPv4ConfigUpdated() {
  if (config_.GetStatic().ipv4_address.has_value() && dhcp_controller_) {
    // If we are using a statically configured IP address instead of a leased IP
    // address, release any acquired lease so it may be used by others.  This
    // allows us to merge other non-leased parameters (like DNS) when they're
    // available from a DHCP server and not overridden by static parameters, but
    // at the same time we avoid taking up a dynamic IP address the DHCP server
    // could assign to someone else who might actually use it.
    dhcp_controller_->ReleaseIP(DHCPController::ReleaseReason::kStaticIP);
  }
  if (config_.Get().ipv4_address) {
    SetupConnection(net_base::IPFamily::kIPv4, /*is_slaac=*/false);
  }
}

void Network::OnStaticIPConfigChanged(const net_base::NetworkConfig& config) {
  config_.SetFromStatic(config);
  if (state_ == State::kIdle) {
    // This can happen after service is selected but before the Network starts.
    return;
  }

  LOG(INFO) << *this << ": static IPv4 config update " << config;
  UpdateIPConfigDBusObject();
  if (config_.Get().ipv4_address) {
    dispatcher_->PostTask(
        FROM_HERE, base::BindOnce(&Network::OnIPv4ConfigUpdated, AsWeakPtr()));
  }

  if (dhcp_controller_ && !config.ipv4_address) {
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
  if (primary_family_ == net_base::IPFamily::kIPv4) {
    return ipconfig_.get();
  }
  if (primary_family_ == net_base::IPFamily::kIPv6) {
    return ip6config_.get();
  }
  return nullptr;
}

const net_base::NetworkConfig* Network::GetSavedIPConfig() const {
  return config_.GetLegacySavedIPConfig();
}

void Network::OnIPConfigUpdatedFromDHCP(
    const net_base::NetworkConfig& network_config,
    const DHCPv4Config::Data& dhcp_data,
    bool new_lease_acquired) {
  // |dhcp_controller_| cannot be empty when the callback is invoked.
  DCHECK(dhcp_controller_);
  LOG(INFO) << *this << ": DHCP lease "
            << (new_lease_acquired ? "acquired " : "update ") << network_config;
  if (new_lease_acquired) {
    for (auto& ev : event_handlers_) {
      ev.OnGetDHCPLease(interface_index_);
    }
  }
  dhcp_data_ = dhcp_data;
  if (config_.SetFromDHCP(
          std::make_unique<net_base::NetworkConfig>(network_config))) {
    UpdateIPConfigDBusObject();
  }

  OnIPv4ConfigUpdated();
  // TODO(b/232177767): OnIPv4ConfiguredWithDHCPLease() should be called inside
  // Network::OnIPv4ConfigUpdated() and only if SetupConnection() happened as a
  // result of the new lease. The current call pattern reproduces the same
  // conditions as before crrev/c/3840983.
  if (new_lease_acquired) {
    for (auto& ev : event_handlers_) {
      ev.OnIPv4ConfiguredWithDHCPLease(interface_index_);
    }
  }

  // Report DHCP provision duration metric.
  std::optional<base::TimeDelta> dhcp_duration =
      dhcp_controller_->GetAndResetLastProvisionDuration();
  if (dhcp_duration.has_value()) {
    metrics_->SendToUMA(Metrics::kMetricDHCPv4ProvisionDurationMillis,
                        technology_, dhcp_duration->InMilliseconds());
  }

  if (network_config.captive_portal_uri.has_value()) {
    network_monitor_->SetCapportAPI(*network_config.captive_portal_uri,
                                    NetworkMonitor::CapportSource::kDHCP);
  }
}

void Network::OnDHCPDrop(bool is_voluntary) {
  LOG(INFO) << *this << ": " << __func__ << ": is_voluntary = " << is_voluntary;
  if (!is_voluntary) {
    for (auto& ev : event_handlers_) {
      ev.OnGetDHCPFailure(interface_index_);
    }
  }

  // |dhcp_controller_| cannot be empty when the callback is invoked.
  DCHECK(dhcp_controller_);

  dhcp_data_ = std::nullopt;
  bool config_changed = config_.SetFromDHCP(nullptr);
  UpdateIPConfigDBusObject();
  if (config_.Get().ipv4_address) {
    if (config_changed) {
      // When this function is triggered by a renew failure, the current
      // IPConfig can be a mix of DHCP and static IP. We need to revert the DHCP
      // part.
      OnIPv4ConfigUpdated();
    }
    return;
  }

  // Fallback to IPv6 if possible.
  const auto combined_network_config = config_.Get();
  if (!combined_network_config.ipv6_addresses.empty() &&
      !combined_network_config.dns_servers.empty()) {
    LOG(INFO) << *this << ": operating in IPv6-only because of "
              << (is_voluntary ? "receiving DHCP option 108" : "DHCP failure");
    if (primary_family_ == net_base::IPFamily::kIPv4) {
      SetupConnection(net_base::IPFamily::kIPv6, config_.HasSLAAC());
    }
    return;
  }

  if (is_voluntary) {
    if (state_ == State::kConfiguring) {
      // DHCPv4 reports to prefer v6 only. Continue to wait for SLAAC.
      return;
    } else {
      LOG(ERROR) << *this
                 << ": DHCP option 108 received but no valid IPv6 network is "
                    "usable. Likely a network configuration error.";
    }
  }

  StopInternal(/*is_failure=*/true, /*trigger_callback=*/true);
}

bool Network::RenewDHCPLease() {
  if (!dhcp_controller_) {
    return false;
  }
  SLOG(2) << *this << ": renewing DHCP lease";
  // If RenewIP() fails, DHCPController will output a ERROR log.
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

void Network::OnUpdateFromSLAAC(SLAACController::UpdateType update_type) {
  const auto slaac_network_config = slaac_controller_->GetNetworkConfig();
  LOG(INFO) << *this << ": Updating SLAAC config to " << slaac_network_config;

  if (slaac_network_config.captive_portal_uri.has_value()) {
    network_monitor_->SetCapportAPI(*slaac_network_config.captive_portal_uri,
                                    NetworkMonitor::CapportSource::kRA);
  }

  auto old_network_config = config_.Get();
  if (config_.SetFromSLAAC(
          std::make_unique<net_base::NetworkConfig>(slaac_network_config))) {
    UpdateIPConfigDBusObject();
  }
  auto new_network_config = config_.Get();

  if (update_type == SLAACController::UpdateType::kAddress) {
    for (auto& ev : event_handlers_) {
      ev.OnGetSLAACAddress(interface_index_);
    }
    // No matter whether the primary address changes, any address change will
    // need to trigger address-based routing rule to be updated.
    if (primary_family_) {
      ApplyNetworkConfig(NetworkConfigArea::kRoutingPolicy);
    }
    if (old_network_config.ipv6_addresses.size() >= 1 &&
        new_network_config.ipv6_addresses.size() >= 1 &&
        old_network_config.ipv6_addresses[0] ==
            new_network_config.ipv6_addresses[0] &&
        old_network_config.ipv6_gateway == new_network_config.ipv6_gateway) {
      SLOG(2) << *this << ": " << __func__ << ": primary address for "
              << interface_name_ << " is unchanged";
      return;
    }
  } else if (update_type == SLAACController::UpdateType::kRDNSS) {
    if (old_network_config.dns_servers == new_network_config.dns_servers) {
      SLOG(2) << *this << ": " << __func__ << " DNS server list is unchanged.";
      return;
    }
  } else if (update_type == SLAACController::UpdateType::kDNSSL) {
    if (old_network_config.dns_search_domains ==
        new_network_config.dns_search_domains) {
      SLOG(2) << *this << ": " << __func__
              << " DNS search domain list is unchanged.";
      return;
    }
  } else if (update_type == SLAACController::UpdateType::kDefaultRoute) {
    // Nothing to do except updating IPConfig.
    return;
  }

  OnIPv6ConfigUpdated();

  if (update_type == SLAACController::UpdateType::kAddress) {
    // TODO(b/232177767): OnIPv6ConfiguredWithSLAACAddress() should be called
    // inside Network::OnIPv6ConfigUpdated() and only if SetupConnection()
    // happened as a result of the new address (ignoring IPv4 and assuming
    // Network is fully dual-stack). The current call pattern reproduces the
    // same conditions as before crrev/c/3840983.
    for (auto& ev : event_handlers_) {
      ev.OnIPv6ConfiguredWithSLAACAddress(interface_index_);
    }
    std::optional<base::TimeDelta> slaac_duration =
        slaac_controller_->GetAndResetLastProvisionDuration();
    if (slaac_duration.has_value()) {
      metrics_->SendToUMA(Metrics::kMetricSLAACProvisionDurationMillis,
                          technology_, slaac_duration->InMilliseconds());
    }
  }
}

void Network::OnIPv6ConfigUpdated() {
  if (!config_.Get().ipv6_addresses.empty() &&
      !config_.Get().dns_servers.empty()) {
    // Setup connection using IPv6 configuration only if the IPv6 configuration
    // is ready for connection (contained both IP address and DNS servers), and
    // there is no existing IPv4 connection. We always prefer IPv4 configuration
    // over IPv6.
    if (primary_family_ != net_base::IPFamily::kIPv4) {
      SetupConnection(net_base::IPFamily::kIPv6, config_.HasSLAAC());
    } else {
      // Still apply IPv6 DNS even if the Connection is setup with IPv4.
      ApplyNetworkConfig(NetworkConfigArea::kDNS);
    }
  }
}

void Network::UpdateIPConfigDBusObject() {
  auto combined_network_config = config_.Get();
  if (!combined_network_config.ipv4_address) {
    set_ipconfig(nullptr);
  } else {
    if (!ipconfig()) {
      set_ipconfig(
          std::make_unique<IPConfig>(control_interface_, interface_name_));
    }
    ipconfig()->ApplyNetworkConfig(combined_network_config,
                                   net_base::IPFamily::kIPv4, dhcp_data_);
  }
  // Keep the historical behavior that ip6config is only created when both IP
  // address and DNS servers are available.
  if (combined_network_config.ipv6_addresses.empty() ||
      combined_network_config.dns_servers.empty()) {
    set_ip6config(nullptr);
  } else {
    if (!ip6config()) {
      set_ip6config(
          std::make_unique<IPConfig>(control_interface_, interface_name_));
    }
    ip6config()->ApplyNetworkConfig(combined_network_config,
                                    net_base::IPFamily::kIPv6);
  }
  for (auto& ev : event_handlers_) {
    ev.OnIPConfigsPropertyUpdated(interface_index_);
  }
}

void Network::EnableARPFiltering() {
  proc_fs_->SetIPFlag(net_base::IPFamily::kIPv4,
                      net_base::ProcFsStub::kIPFlagArpAnnounce,
                      net_base::ProcFsStub::kIPFlagArpAnnounceBestLocal);
  proc_fs_->SetIPFlag(net_base::IPFamily::kIPv4,
                      net_base::ProcFsStub::kIPFlagArpIgnore,
                      net_base::ProcFsStub::kIPFlagArpIgnoreLocalOnly);
}

void Network::SetPriority(net_base::NetworkPriority priority) {
  if (!primary_family_) {
    LOG(WARNING) << *this << ": " << __func__
                 << " called but no connection exists";
    return;
  }
  if (priority_ == priority) {
    return;
  }
  priority_ = priority;
  ApplyNetworkConfig(NetworkConfigArea::kRoutingPolicy |
                     NetworkConfigArea::kDNS);
}

net_base::NetworkPriority Network::GetPriority() {
  return priority_;
}

const net_base::NetworkConfig& Network::GetNetworkConfig() const {
  return config_.Get();
}

std::vector<net_base::IPCIDR> Network::GetAddresses() const {
  std::vector<net_base::IPCIDR> result;
  // Addresses are returned in the order of IPv4 -> IPv6 to ensure
  // backward-compatibility that callers can use result[0] to match legacy
  // local() result.
  auto network_config = GetNetworkConfig();
  if (network_config.ipv4_address) {
    result.emplace_back(*network_config.ipv4_address);
  }
  for (const auto& ipv6_addr : network_config.ipv6_addresses) {
    result.emplace_back(ipv6_addr);
  }
  return result;
}

std::vector<net_base::IPAddress> Network::GetDNSServers() const {
  return GetNetworkConfig().dns_servers;
}

void Network::OnNeighborReachabilityEvent(
    const patchpanel::Client::NeighborReachabilityEvent& event) {
  using Role = patchpanel::Client::NeighborRole;
  using Status = patchpanel::Client::NeighborStatus;

  const auto ip_address = net_base::IPAddress::CreateFromString(event.ip_addr);
  if (!ip_address) {
    LOG(ERROR) << *this << ": " << __func__ << ": invalid IP address "
               << event.ip_addr;
    return;
  }

  switch (event.status) {
    case Status::kFailed:
    case Status::kReachable:
      break;
    default:
      LOG(ERROR) << *this << ": " << __func__ << ": invalid event " << event;
      return;
  }

  if (event.status == Status::kFailed) {
    ReportNeighborLinkMonitorFailure(technology_, ip_address->GetFamily(),
                                     event.role);
  }

  if (state_ == State::kIdle) {
    LOG(INFO) << *this << ": " << __func__ << ": Idle state, ignoring "
              << event;
    return;
  }

  if (ignore_link_monitoring_) {
    LOG(INFO) << *this << ": " << __func__
              << " link monitor events ignored, ignoring " << event;
    return;
  }

  if (event.role == Role::kGateway ||
      event.role == Role::kGatewayAndDnsServer) {
    const net_base::NetworkConfig& network_config = GetNetworkConfig();
    switch (ip_address->GetFamily()) {
      case net_base::IPFamily::kIPv4:
        // It is impossible to observe a reachability event for the current
        // gateway before Network knows the net_base::NetworkConfig for the
        // current connection: patchpanel would not emit reachability event for
        // the correct connection yet.
        if (!network_config.ipv4_address) {
          LOG(INFO) << *this << ": " << __func__ << ": "
                    << ip_address->GetFamily()
                    << " not configured, ignoring neighbor reachability event"
                    << event;
          return;
        }
        // Ignore reachability events related to a prior connection.
        if (network_config.ipv4_gateway != ip_address->ToIPv4Address()) {
          LOG(INFO) << *this << ": " << __func__
                    << ": ignored neighbor reachability event with conflicting "
                       "gateway address "
                    << event;
          return;
        }
        ipv4_gateway_found_ = true;
        break;
      case net_base::IPFamily::kIPv6:
        if (network_config.ipv6_addresses.empty()) {
          LOG(INFO) << *this << ": " << __func__ << ": "
                    << ip_address->GetFamily()
                    << " not configured, ignoring neighbor reachability event"
                    << event;
          return;
        }
        // Ignore reachability events related to a prior connection.
        if (network_config.ipv6_gateway != ip_address->ToIPv6Address()) {
          LOG(INFO) << *this << ": " << __func__
                    << ": ignored neighbor reachability event with conflicting "
                       "gateway address "
                    << event;
          return;
        }
        ipv6_gateway_found_ = true;
        break;
    }
  }

  for (auto& ev : event_handlers_) {
    ev.OnNeighborReachabilityEvent(interface_index_, *ip_address, event.role,
                                   event.status);
  }
}

bool Network::StartPortalDetection(NetworkMonitor::ValidationReason reason) {
  if (!IsConnected()) {
    LOG(INFO) << *this << ": " << __func__ << "(" << reason
              << "): Cannot start portal detection: Network is not connected";
    return false;
  }

  const std::optional<net_base::IPFamily> family =
      GetNetworkValidationIPFamily();
  if (!family) {
    LOG(ERROR) << *this << ": " << __func__ << "(" << reason
               << "): Cannot start portal detection: No valid IP address";
    return false;
  }
  const std::vector<net_base::IPAddress> dns_list =
      GetNetworkValidationDNSServers(*family);
  if (dns_list.empty()) {
    LOG(ERROR) << *this << ": " << __func__ << "(" << reason
               << "): Cannot start portal detection: No DNS servers";
    return false;
  }
  if (!network_monitor_->Start(reason, *family, dns_list)) {
    LOG(ERROR) << *this << ": " << __func__ << "(" << reason
               << "): Failed to start portal detection";
    return false;
  }

  for (auto& ev : event_handlers_) {
    ev.OnNetworkValidationStart(interface_index_);
  }
  return true;
}

void Network::StopPortalDetection() {
  if (network_monitor_ && network_monitor_->Stop()) {
    for (auto& ev : event_handlers_) {
      ev.OnNetworkValidationStop(interface_index_);
    }
  }
}

std::optional<net_base::IPFamily> Network::GetNetworkValidationIPFamily()
    const {
  auto network_config = GetNetworkConfig();
  if (network_config.ipv4_address) {
    return net_base::IPFamily::kIPv4;
  }
  if (!network_config.ipv6_addresses.empty()) {
    return net_base::IPFamily::kIPv6;
  }
  return std::nullopt;
}

std::vector<net_base::IPAddress> Network::GetNetworkValidationDNSServers(
    net_base::IPFamily family) const {
  std::vector<net_base::IPAddress> dns_list;
  for (const auto& addr : GetNetworkConfig().dns_servers) {
    if (addr.GetFamily() == family) {
      dns_list.push_back(addr);
    }
  }
  return dns_list;
}

void Network::OnPortalDetectorResult(const NetworkMonitor::Result& result) {
  std::string previous_validation_state = "unevaluated";
  if (network_validation_result_) {
    previous_validation_state = PortalDetector::ValidationStateToString(
        network_validation_result_->GetValidationState());
  }
  LOG(INFO) << *this
            << ": OnPortalDetectorResult: " << previous_validation_state
            << " -> " << result.GetValidationState();

  if (!IsConnected()) {
    LOG(INFO) << *this
              << ": Portal detection completed but Network is not connected";
    return;
  }

  network_validation_result_ = result;

  for (auto& ev : event_handlers_) {
    ev.OnNetworkValidationResult(interface_index_, result);
  }

  switch (result.GetValidationState()) {
    case PortalDetector::ValidationState::kNoConnectivity:
      // If network validation failed, also start additional connection
      // diagnostics for the current network connection.
      StartConnectionDiagnostics();
      break;
    case PortalDetector::ValidationState::kInternetConnectivity:
      // Conclusive result that allows the Service to transition to the
      // "online" state. Stop portal detection.
      StopPortalDetection();
      break;
    case PortalDetector::ValidationState::kPortalRedirect:
      // Conclusive result that allows to start the portal detection sign-in
      // flow.
      break;
    case PortalDetector::ValidationState::kPortalSuspected:
      // b/309175584: the "portal-suspected" also starts the portal detection
      // sign-in flow and is considered conclusive. Do not run additional
      // connection diagnostics.
      break;
  }
}

void Network::StartConnectionDiagnostics() {
  if (!IsConnected()) {
    LOG(INFO) << *this
              << ": Not connected, cannot start connection diagnostics";
    return;
  }
  DCHECK(primary_family_);

  std::optional<net_base::IPAddress> local_address = std::nullopt;
  std::optional<net_base::IPAddress> gateway_address = std::nullopt;
  const net_base::NetworkConfig& config = GetNetworkConfig();
  if (config.ipv4_address) {
    local_address = net_base::IPAddress(config.ipv4_address->address());
    gateway_address =
        config.ipv4_gateway
            ? std::make_optional(net_base::IPAddress(*config.ipv4_gateway))
            : std::nullopt;
  } else if (!config.ipv6_addresses.empty()) {
    local_address = net_base::IPAddress(config.ipv6_addresses[0].address());
    gateway_address =
        config.ipv6_gateway
            ? std::make_optional(net_base::IPAddress(*config.ipv6_gateway))
            : std::nullopt;
  }

  if (!local_address) {
    LOG(ERROR)
        << *this
        << ": Local address unavailable, aborting connection diagnostics";
    return;
  }
  if (!gateway_address) {
    LOG(ERROR) << *this
               << ": Gateway unavailable, aborting connection diagnostics";
    return;
  }

  connection_diagnostics_ = CreateConnectionDiagnostics(
      *local_address, *gateway_address, GetDNSServers());
  if (!connection_diagnostics_->Start(probing_configuration_.portal_http_url)) {
    connection_diagnostics_.reset();
    LOG(WARNING) << *this << ": Failed to start connection diagnostics";
    return;
  }
  LOG(INFO) << *this << ": Connection diagnostics started";
}

void Network::StopConnectionDiagnostics() {
  LOG(INFO) << *this << ": Connection diagnostics stopping";
  connection_diagnostics_.reset();
}

std::unique_ptr<ConnectionDiagnostics> Network::CreateConnectionDiagnostics(
    const net_base::IPAddress& ip_address,
    const net_base::IPAddress& gateway,
    const std::vector<net_base::IPAddress>& dns_list) {
  return std::make_unique<ConnectionDiagnostics>(
      interface_name(), interface_index(), ip_address, gateway, dns_list,
      dispatcher_, metrics_, base::DoNothing());
}

void Network::StartConnectivityTest(
    PortalDetector::ProbingConfiguration probe_config) {
  auto family = GetNetworkValidationIPFamily();
  if (!family) {
    LOG(ERROR) << *this << " " << __func__ << ": No valid IP address";
    return;
  }
  auto dns_list = GetNetworkValidationDNSServers(*family);
  if (dns_list.empty()) {
    LOG(ERROR) << *this << " " << __func__ << ": No DNS servers";
    return;
  }
  LOG(INFO) << *this << ": Starting Internet connectivity test";
  connectivity_test_portal_detector_ = std::make_unique<PortalDetector>(
      dispatcher_, probe_config,
      base::BindRepeating(&Network::ConnectivityTestCallback,
                          weak_factory_.GetWeakPtr(), logging_tag_));
  connectivity_test_portal_detector_->Start(interface_name_, *family, dns_list,
                                            logging_tag_);
}

void Network::ConnectivityTestCallback(const std::string& device_logging_tag,
                                       const PortalDetector::Result& result) {
  LOG(INFO) << device_logging_tag
            << ": Completed connectivity test: " << result;
  connectivity_test_portal_detector_.reset();
}

bool Network::IsConnectedViaTether() const {
  if (!dhcp_data_) {
    return false;
  }
  const auto& vendor_option = dhcp_data_->vendor_encapsulated_options;
  if (vendor_option.size() != strlen(kAndroidMeteredHotspotVendorOption)) {
    return false;
  }
  return memcmp(kAndroidMeteredHotspotVendorOption, vendor_option.data(),
                vendor_option.size()) == 0;
}

bool Network::HasInternetConnectivity() const {
  return network_validation_result_.has_value() &&
         network_validation_result_->GetValidationState() ==
             PortalDetector::ValidationState::kInternetConnectivity;
}

void Network::ReportIPType() {
  auto network_config = GetNetworkConfig();
  const bool has_ipv4 = network_config.ipv4_address.has_value();
  const bool has_ipv6 = !network_config.ipv6_addresses.empty();
  Metrics::IPType ip_type = Metrics::kIPTypeUnknown;
  if (has_ipv4 && has_ipv6) {
    ip_type = Metrics::kIPTypeDualStack;
  } else if (has_ipv4) {
    ip_type = Metrics::kIPTypeIPv4Only;
  } else if (has_ipv6) {
    ip_type = Metrics::kIPTypeIPv6Only;
  }
  metrics_->SendEnumToUMA(Metrics::kMetricIPType, technology_, ip_type);
}

void Network::ApplyNetworkConfig(NetworkConfigArea area,
                                 base::OnceCallback<void(bool)> callback) {
  const auto& network_config = GetNetworkConfig();
  network_applier_->ApplyNetworkConfig(interface_index_, interface_name_, area,
                                       network_config, priority_, technology_);

  // TODO(b/240871320): /etc/resolv.conf is now managed by dnsproxy. This code
  // is to be deprecated.
  if ((area & NetworkConfigArea::kDNS) && priority_.is_primary_for_dns) {
    std::vector<std::string> dns_strs;
    std::transform(network_config.dns_servers.begin(),
                   network_config.dns_servers.end(),
                   std::back_inserter(dns_strs),
                   [](net_base::IPAddress dns) { return dns.ToString(); });
    resolver_->SetDNSFromLists(dns_strs, network_config.dns_search_domains);
  }

  CHECK(patchpanel_client_);
  patchpanel_client_->RegisterOnAvailableCallback(base::BindOnce(
      &Network::CallPatchpanelConfigureNetwork, weak_factory_.GetWeakPtr(),
      interface_index_, interface_name_, area, network_config, priority_,
      technology_, std::move(callback)));
}

void Network::CallPatchpanelConfigureNetwork(
    int interface_index,
    const std::string& interface_name,
    NetworkConfigArea area,
    const net_base::NetworkConfig& network_config,
    net_base::NetworkPriority priority,
    Technology technology,
    base::OnceCallback<void(bool)> callback,
    bool is_service_ready) {
  if (!is_service_ready) {
    LOG(ERROR)
        << *this
        << ": missing patchpanel service. Network setup might be partial.";
    return;
  }
  VLOG(2) << __func__ << ": " << *this;
  CHECK(patchpanel_client_);
  patchpanel::Client::NetworkTechnology dbus_technology;
  switch (technology) {
    case Technology::kCellular:
      dbus_technology = patchpanel::Client::NetworkTechnology::kCellular;
      break;
    case Technology::kWiFi:
      dbus_technology = patchpanel::Client::NetworkTechnology::kWiFi;
      break;
    case Technology::kVPN:
      dbus_technology = patchpanel::Client::NetworkTechnology::kVPN;
      break;
    case Technology::kEthernet:
    case Technology::kEthernetEap:
      dbus_technology = patchpanel::Client::NetworkTechnology::kVPN;
      break;
    default:
      LOG(WARNING)
          << "Patchpanel-unaware shill Technology, treating as Ethernet.";
      dbus_technology = patchpanel::Client::NetworkTechnology::kEthernet;
  }
  patchpanel_client_->ConfigureNetwork(
      interface_index, interface_name, static_cast<uint32_t>(area),
      network_config, priority, dbus_technology, std::move(callback));
}

void Network::ReportNeighborLinkMonitorFailure(
    Technology tech,
    net_base::IPFamily family,
    patchpanel::Client::NeighborRole role) {
  using Role = patchpanel::Client::NeighborRole;
  static constexpr auto failure_table =
      base::MakeFixedFlatMap<std::pair<net_base::IPFamily, Role>,
                             Metrics::NeighborLinkMonitorFailure>({
          {{net_base::IPFamily::kIPv4, Role::kGateway},
           Metrics::kNeighborIPv4GatewayFailure},
          {{net_base::IPFamily::kIPv4, Role::kDnsServer},
           Metrics::kNeighborIPv4DNSServerFailure},
          {{net_base::IPFamily::kIPv4, Role::kGatewayAndDnsServer},
           Metrics::kNeighborIPv4GatewayAndDNSServerFailure},
          {{net_base::IPFamily::kIPv6, Role::kGateway},
           Metrics::kNeighborIPv6GatewayFailure},
          {{net_base::IPFamily::kIPv6, Role::kDnsServer},
           Metrics::kNeighborIPv6DNSServerFailure},
          {{net_base::IPFamily::kIPv6, Role::kGatewayAndDnsServer},
           Metrics::kNeighborIPv6GatewayAndDNSServerFailure},
      });

  Metrics::NeighborLinkMonitorFailure failure =
      Metrics::kNeighborLinkMonitorFailureUnknown;
  const auto it = failure_table.find({family, role});
  if (it != failure_table.end()) {
    failure = it->second;
  }

  metrics_->SendEnumToUMA(Metrics::kMetricNeighborLinkMonitorFailure, tech,
                          failure);
}

std::ostream& operator<<(std::ostream& stream, const Network& network) {
  return stream << network.logging_tag();
}

}  // namespace shill
