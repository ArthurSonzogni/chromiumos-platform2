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
#include <chromeos/net-base/ip_address.h>
#include <chromeos/net-base/ipv4_address.h>
#include <chromeos/net-base/ipv6_address.h>
#include <chromeos/net-base/network_config.h>
#include <chromeos/net-base/network_priority.h>
#include <chromeos/net-base/proc_fs_stub.h>
#include <chromeos/patchpanel/dbus/client.h>

#include "base/memory/ptr_util.h"
#include "shill/event_dispatcher.h"
#include "shill/ipconfig.h"
#include "shill/logging.h"
#include "shill/metrics.h"
#include "shill/network/compound_network_config.h"
#include "shill/network/dhcp_controller.h"
#include "shill/network/dhcpv4_config.h"
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

patchpanel::Client::NetworkTechnology
ShillTechnologyToPatchpanelClientTechnology(Technology technology) {
  switch (technology) {
    case Technology::kCellular:
      return patchpanel::Client::NetworkTechnology::kCellular;
    case Technology::kWiFi:
      return patchpanel::Client::NetworkTechnology::kWiFi;
    case Technology::kVPN:
      return patchpanel::Client::NetworkTechnology::kVPN;
    case Technology::kEthernet:
    case Technology::kEthernetEap:
      return patchpanel::Client::NetworkTechnology::kEthernet;
    default:
      LOG(ERROR)
          << "Patchpanel-unaware shill Technology, treating as Ethernet.";
      return patchpanel::Client::NetworkTechnology::kEthernet;
  }
}

}  // namespace

int Network::next_network_id_ = 1;

std::unique_ptr<Network> Network::CreateForTesting(
    int interface_index,
    std::string_view interface_name,
    Technology technology,
    bool fixed_ip_params,
    ControlInterface* control_interface,
    EventDispatcher* dispatcher,
    Metrics* metrics,
    patchpanel::Client* patchpanel_client) {
  return base::WrapUnique(new Network(
      interface_index, std::string(interface_name), technology, fixed_ip_params,
      control_interface, dispatcher, metrics, patchpanel_client,
      /*legacy_dhcp_controller_factory=*/nullptr,
      /*dhcp_controller_factory=*/nullptr));
}

Network::Network(
    int interface_index,
    std::string_view interface_name,
    Technology technology,
    bool fixed_ip_params,
    ControlInterface* control_interface,
    EventDispatcher* dispatcher,
    Metrics* metrics,
    patchpanel::Client* patchpanel_client,
    std::unique_ptr<DHCPControllerFactory> legacy_dhcp_controller_factory,
    std::unique_ptr<DHCPControllerFactory> dhcp_controller_factory,
    Resolver* resolver,
    std::unique_ptr<NetworkMonitorFactory> network_monitor_factory)
    : network_id_(next_network_id_++),
      interface_index_(interface_index),
      interface_name_(interface_name),
      technology_(technology),
      logging_tag_(interface_name),
      fixed_ip_params_(fixed_ip_params),
      proc_fs_(std::make_unique<net_base::ProcFsStub>(interface_name_)),
      legacy_dhcp_controller_factory_(
          std::move(legacy_dhcp_controller_factory)),
      dhcp_controller_factory_(std::move(dhcp_controller_factory)),
      config_(logging_tag_),
      network_monitor_factory_(std::move(network_monitor_factory)),
      control_interface_(control_interface),
      dispatcher_(dispatcher),
      metrics_(metrics),
      patchpanel_client_(patchpanel_client),
      rtnl_handler_(net_base::RTNLHandler::GetInstance()),
      resolver_(resolver) {}

Network::~Network() {
  for (auto& ev : event_handlers_) {
    ev.OnNetworkDestroyed(network_id_, interface_index_);
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
  // TODO(b/232177767): Log the StartOptions and other parameters.
  if (state_ != State::kIdle) {
    LOG(WARNING)
        << *this
        << ": Network has been started, stop it before starting with the "
           "new options";
    StopInternal(/*is_failure=*/false, /*trigger_callback=*/false);
  }

  // If the execution of this function fails, StopInternal() will be called and
  // turn the state to kIdle.
  state_ = State::kConfiguring;

  ignore_link_monitoring_ = opts.ignore_link_monitoring;
  ipv4_gateway_found_ = false;
  ipv6_gateway_found_ = false;

  probing_configuration_ = opts.probing_configuration;
  network_monitor_ = network_monitor_factory_->Create(
      dispatcher_, metrics_, this, patchpanel_client_, technology_,
      interface_index_, interface_name_, probing_configuration_,
      opts.validation_mode,
      std::make_unique<ValidationLog>(technology_, metrics_), logging_tag_);
  network_monitor_->SetCapportEnabled(capport_enabled_);

  EnableARPFiltering();

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
    DHCPController::Options dhcp_opts = *opts.dhcp;
    if (config_.GetStatic().ipv4_address) {
      dhcp_opts.use_arp_gateway = false;
    }

    // Keep the legacy behavior that Network has a empty IPConfig if DHCP has
    // started but not succeeded/failed yet.
    ipconfig_ = std::make_unique<IPConfig>(control_interface_, interface_name_,
                                           kTypeDHCP);
    dhcp_controller_ =
        (dhcp_opts.use_legacy_dhcpcd ? legacy_dhcp_controller_factory_
                                     : dhcp_controller_factory_)
            ->Create(interface_name_, technology_, dhcp_opts,
                     base::BindRepeating(&Network::OnIPConfigUpdatedFromDHCP,
                                         AsWeakPtr()),
                     base::BindRepeating(&Network::OnDHCPDrop, AsWeakPtr()));
    dhcp_started = dhcp_controller_->RenewIP();
    if (!dhcp_started) {
      LOG(ERROR) << "Failed to request DHCP IP";
    }
  }
  if (opts.dhcp_pd && !opts.accept_ra) {
    LOG(ERROR) << "DHCP-PD needs accept_ra to function correctly";
  }
  if (opts.dhcp_pd && opts.accept_ra) {
    dhcp_pd_controller_ = dhcp_controller_factory_->Create(
        interface_name_, technology_, DHCPController::Options(),
        base::BindRepeating(&Network::OnNetworkConfigUpdatedFromDHCPv6,
                            AsWeakPtr()),
        base::BindRepeating(&Network::OnDHCPv6Drop, AsWeakPtr()),
        net_base::IPFamily::kIPv6);
    if (!dhcp_pd_controller_) {
      LOG(ERROR) << "Failed to create DHCPv6-PD controller";
    } else if (!dhcp_pd_controller_->RenewIP()) {
      LOG(ERROR) << "Failed to start DHCPv6-PD";
    }
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
    return;
  }

  // Preliminary set up routing policy to enable basic local connectivity
  // (needed for DHCPv6). Note that priority is not assigned until Network
  // became Connected, so here the rules are set up with default (lowest)
  // priority.
  ApplyNetworkConfig(NetworkConfigArea::kRoutingPolicy);

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

  if (state_ == State::kIdle) {
    LOG(ERROR) << *this << ": Unexpected " << __func__
               << " call when state is idle";
    return;
  }

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
  LOG(INFO) << *this << ": " << __func__ << "(success: " << std::boolalpha
            << success << ")";
  if (!success) {
    StopInternal(/*is_failure=*/true,
                 /*trigger_callback=*/state_ == State::kConnected);
    return;
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

  // Subtle: Start portal detection after transitioning the service to the
  // Connected state because this call may immediately transition to the Online
  // state. Always ignore any on-going portal detection such that the latest
  // network layer properties are used to restart portal detection. This ensures
  // that network validation over IPv4 is prioritized on dual stack networks
  // when IPv4 provisioning completes after IPv6 provisioning. Note that
  // currently SetupConnection() is never called a second time if IPv6
  // provisioning completes after IPv4 provisioning.
  RequestNetworkValidation(
      NetworkMonitor::ValidationReason::kNetworkConnectionUpdate);

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
  StopPortalDetection(/*is_failure=*/false);
  network_monitor_.reset();
  network_monitor_was_running_ = false;

  const bool should_trigger_callback =
      state_ != State::kIdle && trigger_callback;
  bool ipconfig_changed = false;
  if (dhcp_controller_) {
    dhcp_controller_->ReleaseIP(DHCPController::ReleaseReason::kDisconnect);
    dhcp_controller_ = nullptr;
  }
  if (ipconfig_) {
    ipconfig_ = nullptr;
    ipconfig_changed = true;
  }
  if (slaac_controller_) {
    slaac_controller_->Stop();
    slaac_controller_ = nullptr;
  }
  if (ip6config_) {
    ip6config_ = nullptr;
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
  priority_ = net_base::NetworkPriority{};
  CallPatchpanelDestroyNetwork();
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
    network_monitor_->SetCapportURL(*network_config.captive_portal_uri,
                                    network_config.dns_servers,
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
    // DHCPv4 reports to prefer v6 only. Continue to wait for SLAAC. Note that
    // if SLAAC is not available (usually a network configuration error) the
    // Network could stay in Connecting state forever.
    LOG(WARNING) << *this
                 << ": DHCP option 108 received but no valid IPv6 network is "
                    "usable yet. Continue to wait for SLAAC.";
  } else {
    StopInternal(/*is_failure=*/true, /*trigger_callback=*/true);
  }
}

void Network::OnNetworkConfigUpdatedFromDHCPv6(
    const net_base::NetworkConfig& network_config,
    const DHCPv4Config::Data& /*dhcp_data*/,
    bool /*new_lease_acquired*/) {
  // |dhcp_pd_controller_| cannot be empty when the callback is invoked.
  DCHECK(dhcp_pd_controller_);
  LOG(INFO) << *this << ": " << __func__ << ": " << network_config;
  // TODO(b/350884946): Implement this callback - merge the NetworkConfig from
  // DHCPv6-PD into CompoundNetworkConfig and stop generating address from
  // SLAAC.
}

void Network::OnDHCPv6Drop(bool /*is_voluntary*/) {
  LOG(INFO) << *this << ": " << __func__;
  // TODO(b/350884946): Implement this callback.
}

bool Network::RenewDHCPLease() {
  if (!dhcp_controller_) {
    return false;
  }
  SLOG(2) << *this << ": renewing DHCP lease";
  // If RenewIP() fails, LegacyDHCPController will output a ERROR log.
  return dhcp_controller_->RenewIP();
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
    network_monitor_->SetCapportURL(*slaac_network_config.captive_portal_uri,
                                    slaac_network_config.dns_servers,
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
    ipconfig_ = nullptr;
  } else {
    if (!ipconfig_) {
      ipconfig_ =
          std::make_unique<IPConfig>(control_interface_, interface_name_);
    }
    ipconfig_->ApplyNetworkConfig(combined_network_config,
                                  net_base::IPFamily::kIPv4, dhcp_data_);
  }
  // Keep the historical behavior that ip6config is only created when both IP
  // address and DNS servers are available.
  if (combined_network_config.ipv6_addresses.empty() ||
      combined_network_config.dns_servers.empty()) {
    ip6config_ = nullptr;
  } else {
    if (!ip6config_) {
      ip6config_ =
          std::make_unique<IPConfig>(control_interface_, interface_name_);
    }
    ip6config_->ApplyNetworkConfig(combined_network_config,
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

// TODO(jiejiang): Add unit test for this function.
void Network::SetPriority(net_base::NetworkPriority priority) {
  if (!primary_family_) {
    LOG(WARNING) << *this << ": " << __func__
                 << " called but no connection exists";
    return;
  }
  if (priority_ == priority) {
    return;
  }
  auto area = NetworkConfigArea::kDNS;
  // Skip applying kRoutingPolicy is the routing priority does not change.
  // kRoutingPolicy will partially reset rule tables, which may cause transient
  // networking issue, so we want to skip this operation as much as possible.
  if (!net_base::NetworkPriority::HaveSameRoutingPriority(priority_,
                                                          priority)) {
    area |= NetworkConfigArea::kRoutingPolicy;
  }
  priority_ = priority;
  ApplyNetworkConfig(area);
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

void Network::UpdateNetworkValidationMode(NetworkMonitor::ValidationMode mode) {
  if (!IsConnected()) {
    LOG(INFO) << *this << ": " << __func__ << ": not possible to set to "
              << mode << " if the network is not connected";
    return;
  }
  // TODO(b/314693271): Define OnValidationStopped and move this logic inside
  // NetworkMonitor.
  const NetworkMonitor::ValidationMode previous_mode =
      network_monitor_->GetValidationMode();
  if (previous_mode == mode) {
    return;
  }
  network_monitor_->SetValidationMode(mode);
  if (previous_mode == NetworkMonitor::ValidationMode::kDisabled) {
    network_monitor_was_running_ = network_monitor_->IsRunning();
    network_monitor_->Start(
        NetworkMonitor::ValidationReason::kServicePropertyUpdate);
  } else if (mode == NetworkMonitor::ValidationMode::kDisabled) {
    StopPortalDetection(/*is_failure=*/false);
  }
}

void Network::SetCapportEnabled(bool enabled) {
  if (capport_enabled_ == enabled) {
    return;
  }

  capport_enabled_ = enabled;
  if (network_monitor_) {
    network_monitor_->SetCapportEnabled(enabled);
  }
}

void Network::RequestNetworkValidation(
    NetworkMonitor::ValidationReason reason) {
  if (!IsConnected()) {
    LOG(INFO) << *this << ": " << __func__ << "(" << reason
              << "): Network is not connected";
    return;
  }

  if (network_monitor_->GetValidationMode() ==
      NetworkMonitor::ValidationMode::kDisabled) {
    LOG(INFO) << *this << ": " << __func__ << "(" << reason
              << "): Network validation is disabled";
    return;
  }
  network_monitor_was_running_ = network_monitor_->IsRunning();
  network_monitor_->Start(reason);
}

void Network::OnValidationStarted(bool is_success) {
  // b/211000413: If network validation could not start, the network is either
  // misconfigured (no DNS) or not provisioned correctly. In either case,
  // notify listeners to assume that the network has no Internet connectivity.
  if (!network_monitor_was_running_) {
    for (auto& ev : event_handlers_) {
      ev.OnNetworkValidationStart(interface_index_, /*is_failure=*/!is_success);
    }
  } else if (!is_success) {
    StopPortalDetection(/*is_failure=*/true);
  }
}

void Network::StopPortalDetection(bool is_failure) {
  if (network_monitor_ && network_monitor_->Stop()) {
    for (auto& ev : event_handlers_) {
      ev.OnNetworkValidationStop(interface_index_, is_failure);
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

void Network::OnNetworkMonitorResult(const NetworkMonitor::Result& result) {
  std::string previous_validation_state = "unevaluated";
  if (network_validation_result_) {
    previous_validation_state = PortalDetector::ValidationStateToString(
        network_validation_result_->validation_state);
  }
  LOG(INFO) << *this << ": " << __func__ << ": " << previous_validation_state
            << " -> " << result.validation_state;

  if (!IsConnected()) {
    LOG(INFO) << *this
              << ": Portal detection completed but Network is not connected";
    return;
  }

  network_validation_result_ = result;
  for (auto& ev : event_handlers_) {
    ev.OnNetworkValidationResult(interface_index_, result);
  }

  if (result.validation_state ==
      PortalDetector::ValidationState::kInternetConnectivity) {
    // Conclusive result that allows the Service to transition to the
    // "online" state. Stop portal detection.
    StopPortalDetection(/*is_failure=*/false);
  } else {
    // Restart the next network validation attempt.
    network_monitor_was_running_ = true;
    network_monitor_->Start(NetworkMonitor::ValidationReason::kRetryValidation);
  }
}

void Network::StartConnectivityTest(
    PortalDetector::ProbingConfiguration probe_config) {
  LOG(INFO) << *this << " " << __func__
            << ": Starting Internet connectivity test";

  if (network_monitor_) {
    network_monitor_->StartConnectionDiagnostics();
  }

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
  connectivity_test_portal_detector_ = std::make_unique<PortalDetector>(
      dispatcher_, patchpanel_client_, interface_name_, probe_config,
      logging_tag_);
  connectivity_test_portal_detector_->Start(
      /*http_only=*/false, *family, dns_list,
      base::BindOnce(&Network::ConnectivityTestCallback,
                     weak_factory_.GetWeakPtr(), logging_tag_));
}

void Network::ConnectivityTestCallback(const std::string& device_logging_tag,
                                       const PortalDetector::Result& result) {
  LOG(INFO) << device_logging_tag
            << ": Completed connectivity test: " << result;
  connectivity_test_portal_detector_.reset();
}

RpcIdentifiers Network::AvailableIPConfigIdentifiers() const {
  RpcIdentifiers ret;
  if (ipconfig_) {
    ret.push_back(ipconfig_->GetRpcIdentifier());
  }
  if (ip6config_) {
    ret.push_back(ip6config_->GetRpcIdentifier());
  }
  return ret;
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
  if (!IsConnected()) {
    return false;
  }
  if (network_monitor_->GetValidationMode() ==
      NetworkMonitor::ValidationMode::kDisabled) {
    // If network validation is disabled, assume we have the Internet
    // connectivity.
    return true;
  }
  return network_validation_result_.has_value() &&
         network_validation_result_->validation_state ==
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
  patchpanel_client_->ConfigureNetwork(
      interface_index, interface_name, static_cast<uint32_t>(area),
      network_config, priority,
      ShillTechnologyToPatchpanelClientTechnology(technology),
      std::move(callback));
}

void Network::CallPatchpanelDestroyNetwork() {
  // TODO(b/273742756): Connect with patchpanel DestroyNetwork API.
  if (!patchpanel_client_) {
    LOG(ERROR) << __func__ << ": " << *this << ": missing patchpanel client.";
    return;
  }
  // Note that we cannot use RegisterOnAvailableCallback here, as it is very
  // possible that the Network object get destroyed immediately after this and
  // the callback won't fire. That's particularlly observable for the case of
  // VPN. Directly calling patchpanel dbus here as the possibility of patchpanel
  // service not ready when a Network is being destroyed is very low.
  patchpanel_client_->ConfigureNetwork(
      interface_index_, interface_name_,
      static_cast<uint32_t>(NetworkConfigArea::kClear), {}, {},
      ShillTechnologyToPatchpanelClientTechnology(technology_),
      base::DoNothing());
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

void Network::OnTermsAndConditions(const net_base::HttpUrl& url) {
  // TODO(b/319632165)
  if (network_monitor_) {
    network_monitor_->SetTermsAndConditions(url);
  }
}

std::ostream& operator<<(std::ostream& stream, const Network& network) {
  return stream << network.interface_name() << " " << network.logging_tag();
}

}  // namespace shill
