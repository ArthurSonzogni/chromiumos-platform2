// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/network.h"

#include <algorithm>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <base/containers/fixed_flat_map.h>
#include <base/containers/flat_set.h>
#include <base/files/file_util.h>
#include <base/functional/bind.h>
#include <base/functional/callback_helpers.h>
#include <base/notreached.h>
#include <base/observer_list.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/types/cxx23_to_underlying.h>
#include <brillo/http/http_request.h>
#include <chromeos/dbus/service_constants.h>
#include <chromeos/net-base/ip_address.h>
#include <chromeos/net-base/ipv4_address.h>
#include <chromeos/net-base/ipv6_address.h>
#include <chromeos/net-base/netlink_sock_diag.h>
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
#include "shill/network/dhcp_provision_reasons.h"
#include "shill/network/dhcpv4_config.h"
#include "shill/network/network_context.h"
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
          << __func__
          << ": Patchpanel-unaware shill Technology, treating as Ethernet: "
          << technology;
      return patchpanel::Client::NetworkTechnology::kEthernet;
  }
}

}  // namespace

std::string Network::StartOptions::ToString() const {
  std::ostringstream oss;
  oss << "{";
  if (dhcp.has_value()) {
    oss << "dhcp=" << dhcp.value().ToString() << ", ";
  }
  oss << "accept_ra=" << accept_ra << ", ";
  oss << "dhcp_pd=" << dhcp_pd << ", ";
  if (link_local_address.has_value()) {
    oss << "link_local_address=" << link_local_address.value().ToString()
        << ", ";
  }
  oss << "ignore_link_monitoring=" << ignore_link_monitoring << ", ";
  // Skip probing_configuration which is too long.
  oss << "probing_configuration="
      << (probing_configuration == PortalDetector::DefaultProbingConfiguration()
              ? "default, "
              : "customized, ");
  oss << "validation_mode=" << base::to_underlying(validation_mode) << ", ";
  if (link_protocol_network_config) {
    oss << "link_protocol_network_config=" << *link_protocol_network_config;
  }
  oss << "}";
  return oss.str();
}

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
      fixed_ip_params_(fixed_ip_params),
      context_(interface_name_),
      proc_fs_(std::make_unique<net_base::ProcFsStub>(interface_name_)),
      legacy_dhcp_controller_factory_(
          std::move(legacy_dhcp_controller_factory)),
      dhcp_controller_factory_(std::move(dhcp_controller_factory)),
      config_(interface_name_),
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
  if (state_ != State::kIdle) {
    LOG(WARNING)
        << *this << " " << __func__
        << ": Network has been started, stop it before starting with the "
           "new options";
    StopInternal(/*is_failure=*/false, /*trigger_callback=*/false);
  }

  // Update session_id at the beginning of Start() so that logs after this can
  // contain the proper session_id.
  context_.UpdateSessionId();

  LOG(INFO) << *this << " " << __func__ << ": options=" << opts.ToString();

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
      std::make_unique<ValidationLog>(technology_, metrics_),
      context_.logging_tag());
  network_monitor_->SetCapportEnabled(capport_enabled_);

  // Cannot avoid a copy here since |opts| is a const ref.
  if (opts.link_protocol_network_config) {
    config_.SetFromLinkProtocol(std::make_unique<net_base::NetworkConfig>(
        *opts.link_protocol_network_config));
  }

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
                     base::BindRepeating(&Network::OnDHCPDrop, AsWeakPtr()),
                     context_.logging_tag());
    dhcp_started = dhcp_controller_->RenewIP(DHCPProvisionReason::kConnect);
    if (!dhcp_started) {
      LOG(ERROR) << *this << " " << __func__ << ": Failed to request DHCP IP";
    }
  }
  if (opts.dhcp_pd && !opts.accept_ra) {
    LOG(ERROR) << *this << " " << __func__
               << ": DHCP-PD needs accept_ra to function correctly";
  }
  if (opts.dhcp_pd && opts.accept_ra) {
    StartDHCPPD();
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
    LOG(WARNING) << *this << " " << __func__
                 << ": Failed to start IP provisioning";
    dispatcher_->PostTask(
        FROM_HERE,
        base::BindOnce(&Network::StopInternal, AsWeakPtr(),
                       /*is_failure=*/true, /*trigger_callback=*/true));
    return;
  }

  // For VPN, if IPv6 is not set up, make sure that blackhole IPv6 routes are
  // installed properly.
  if (!ipv6_started && config_.Get().ipv6_blackhole_route) {
    dispatcher_->PostTask(
        FROM_HERE,
        base::BindOnce(&Network::ApplyNetworkConfig,
                       weak_factory_for_connection_.GetWeakPtr(),
                       NetworkConfigArea::kIPv6Route, base::DoNothing()));
  }

  RequestTrafficCounters(base::BindOnce(
      &Network::InitializeTrafficCounterSnapshot, weak_factory_.GetWeakPtr()));

  // Preliminary set up routing policy to enable basic local connectivity
  // (needed for DHCPv6). Note that priority is not assigned until Network
  // became Connected, so here the rules are set up with default (lowest)
  // priority.
  ApplyNetworkConfig(NetworkConfigArea::kRoutingPolicy);

  LOG(INFO) << *this << " " << __func__ << ": Started IP provisioning, dhcp: "
            << (dhcp_started ? "started" : "no")
            << ", accept_ra: " << std::boolalpha << opts.accept_ra
            << ",  initial config: " << config_;
}

std::unique_ptr<SLAACController> Network::CreateSLAACController() {
  auto slaac_controller = std::make_unique<SLAACController>(
      interface_index_, proc_fs_.get(), rtnl_handler_, dispatcher_,
      context_.logging_tag());
  return slaac_controller;
}

void Network::StartDHCPPD() {
  dhcp_pd_controller_ = dhcp_controller_factory_->Create(
      interface_name_, technology_, DHCPController::Options(),
      base::BindRepeating(&Network::OnNetworkConfigUpdatedFromDHCPv6,
                          AsWeakPtr()),
      base::BindRepeating(&Network::OnDHCPv6Drop, AsWeakPtr()),
      context_.logging_tag(), net_base::IPFamily::kIPv6);
  if (!dhcp_pd_controller_) {
    LOG(ERROR) << *this << " " << __func__
               << ": Failed to create DHCPv6-PD controller";
  } else if (!dhcp_pd_controller_->RenewIP(DHCPProvisionReason::kConnect)) {
    LOG(ERROR) << *this << " " << __func__ << ": Failed to start DHCPv6-PD";
  }
}

void Network::SetupConnection(net_base::IPFamily family, bool is_slaac) {
  LOG(INFO) << *this << " " << __func__ << ": family: " << family
            << ", is_slaac: " << is_slaac;

  if (state_ == State::kIdle) {
    LOG(ERROR) << *this << " " << __func__ << ": Unexpected call while idle";
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
  LOG(INFO) << *this << " " << __func__ << ": success: " << std::boolalpha
            << success;
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
  if (state_ == State::kIdle) {
    return;
  }
  StopInternal(/*is_failure=*/false, /*trigger_callback=*/true);
}

void Network::StopInternal(bool is_failure, bool trigger_callback) {
  LOG(INFO) << *this << " " << __func__ << ": is_failure: " << is_failure
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
  // Static config is from Service but not per-connection, so it shouldn't be
  // reset in Network::Stop().
  config_.ClearNonStaticConfigs();
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

  RequestTrafficCounters(
      base::BindOnce(&Network::LogTrafficCounter, weak_factory_.GetWeakPtr(),
                     context_.logging_tag(), raw_traffic_counter_snapshot_));

  // Clear session_id at the end so that logs before this can contain the proper
  // session_id.
  context_.ClearSessionId();
}

void Network::InvalidateIPv6Config() {
  SLOG(2) << *this << " " << __func__;
  if (config_.Get().ipv6_addresses.empty()) {
    return;
  }

  SLOG(2) << *this << " " << __func__ << ": Waiting for new IPv6 configuration";
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

  LOG(INFO) << *this << " " << __func__ << ": " << config;
  UpdateIPConfigDBusObject();
  if (config_.Get().ipv4_address) {
    dispatcher_->PostTask(
        FROM_HERE, base::BindOnce(&Network::OnIPv4ConfigUpdated, AsWeakPtr()));
  }

  if (dhcp_controller_ && !config.ipv4_address) {
    // Trigger DHCP renew.
    dhcp_controller_->RenewIP(DHCPProvisionReason::kConnect);
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
  LOG(INFO) << *this << " " << __func__ << ": DHCP lease "
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
  LOG(INFO) << *this << " " << __func__ << ": is_voluntary: " << is_voluntary;
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
    LOG(INFO) << *this << " " << __func__
              << ": operating in IPv6-only because of "
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
    LOG(WARNING) << *this << " " << __func__
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
  LOG(INFO) << *this << " " << __func__ << ": " << network_config;

  // Filter all prefixes longer than /64, and use ::2 in each prefix as ChromeOS
  // host's own address.
  auto edited_config =
      std::make_unique<net_base::NetworkConfig>(network_config);
  for (auto iter = edited_config->ipv6_delegated_prefixes.begin();
       iter != edited_config->ipv6_delegated_prefixes.end();) {
    if (iter->prefix_length() > 64) {
      LOG(WARNING) << *this << " " << __func__ << ": Ignoring too-long prefix "
                   << *iter << " from DHCP-PD.";
      iter = edited_config->ipv6_delegated_prefixes.erase(iter);
      continue;
    }
    auto bytes = iter->address().data();
    bytes[15] = 2;
    edited_config->ipv6_addresses.push_back(
        *net_base::IPv6CIDR::CreateFromBytesAndPrefix(bytes, 128));
    ++iter;
  }

  if (config_.SetFromDHCPv6(std::move(edited_config))) {
    UpdateIPConfigDBusObject();
    ApplyNetworkConfig(NetworkConfigArea::kMTU |
                       NetworkConfigArea::kIPv6Address |
                       NetworkConfigArea::kRoutingPolicy);
    OnIPv6ConfigUpdated();
  }
}

void Network::OnDHCPv6Drop(bool /*is_voluntary*/) {
  LOG(INFO) << *this << " " << __func__;

  if (!config_.SetFromDHCPv6(nullptr)) {
    // If config does not change it means we never got any lease from DHCPv6.
    // Don't need to do anything here.
    return;
  }
  if (config_.Get().ipv4_address || !config_.Get().ipv6_addresses.empty()) {
    // If there is still a working v4 or v6 address, just update the Network.
    UpdateIPConfigDBusObject();
    OnIPv6ConfigUpdated();
    return;
  }
  StopInternal(/*is_failure=*/true, /*trigger_callback=*/true);
}

bool Network::RenewDHCPLease(DHCPProvisionReason reason) {
  if (!dhcp_controller_) {
    return false;
  }
  SLOG(2) << *this << " " << __func__;
  // If RenewIP() fails, LegacyDHCPController will output a ERROR log.
  return dhcp_controller_->RenewIP(reason);
}

std::optional<base::TimeDelta> Network::TimeToNextDHCPLeaseRenewal() {
  if (!dhcp_controller_) {
    return std::nullopt;
  }
  return dhcp_controller_->TimeToLeaseExpiry();
}

void Network::OnUpdateFromSLAAC(SLAACController::UpdateType update_type) {
  if (update_type == SLAACController::UpdateType::kPFlag ||
      update_type == SLAACController::UpdateType::kNoPrefix) {
    if (!dhcp_pd_controller_) {
      LOG(INFO) << *this << " " << __func__ << ": "
                << (update_type == SLAACController::UpdateType::kPFlag
                        ? "P-flag detected. "
                        : "Received RA without PIO. ")
                << "Starting DHCPv6-PD.";
      StartDHCPPD();
    }
    return;
  }

  const auto slaac_network_config = slaac_controller_->GetNetworkConfig();
  LOG(INFO) << *this << " " << __func__ << "(" << update_type
            << "): " << slaac_network_config;

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
    // Count the number of different prefixes. There might be a connectivity
    // issue if there are multiple prefixes.
    base::flat_set<net_base::IPv6CIDR> prefixes;
    for (const auto& address : new_network_config.ipv6_addresses) {
      prefixes.insert(address.GetPrefixCIDR());
    }
    if (prefixes.size() > 1) {
      LOG(WARNING) << *this << " " << __func__
                   << ": SLAAC addresses from different prefixes are "
                      "configured, # prefixes = "
                   << prefixes.size();
    }

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
      SLOG(2) << *this << " " << __func__ << ": primary address for "
              << interface_name_ << " is unchanged";
      return;
    }
  } else if (update_type == SLAACController::UpdateType::kRDNSS) {
    if (old_network_config.dns_servers == new_network_config.dns_servers) {
      SLOG(2) << *this << " " << __func__ << ": DNS server list is unchanged.";
      return;
    }
  } else if (update_type == SLAACController::UpdateType::kDNSSL) {
    if (old_network_config.dns_search_domains ==
        new_network_config.dns_search_domains) {
      SLOG(2) << *this << " " << __func__
              << ": DNS search domain list is unchanged.";
      return;
    }
  } else if (update_type == SLAACController::UpdateType::kDefaultRoute) {
    // Nothing to do except updating IPConfig.
    return;
  } else if (update_type == SLAACController::UpdateType::kPref64) {
    if (old_network_config.pref64 == new_network_config.pref64) {
      SLOG(2) << *this << " " << __func__ << ": Pref64 unchanged.";
      return;
    }
  }

  OnIPv6ConfigUpdated();

  if (update_type == SLAACController::UpdateType::kAddress) {
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

void Network::DestroySockets(std::optional<uid_t> uid) {
  // Logging since this is a blocking call, we may care about its execution
  // time. Also this affects connectivity perceived by the user directly. Make
  // it clearer in the log.
  LOG(INFO) << *this << " " << __func__ << ": Start, uid="
            << (uid.has_value() ? std::to_string(*uid) : "empty");

  // Notes:
  // - TODO(jiejiang): We are querying sockets from the kernel multiple times.
  //   There is room for improvement by merging some of them.
  // - Creating a diag socket for each DestroySockets() call since it's observed
  //   that the second call may fail if the same socket is used ("Operation not
  //   supported"). The reason is unclear.
  for (const auto& address : GetAddresses()) {
    if (!net_base::NetlinkSockDiag::Create()->DestroySockets(
            IPPROTO_TCP, address.address(), uid)) {
      LOG(ERROR) << *this << " " << __func__
                 << ": failed to destroy tcp sockets for " << address;
    }
    if (!net_base::NetlinkSockDiag::Create()->DestroySockets(
            IPPROTO_UDP, address.address(), uid)) {
      LOG(ERROR) << *this << " " << __func__
                 << ": failed to destroy udp sockets for " << address;
    }
  }

  LOG(INFO) << *this << " " << __func__ << ": Done, uid="
            << (uid.has_value() ? std::to_string(*uid) : "empty");
}

// TODO(jiejiang): Add unit test for this function.
void Network::SetPriority(net_base::NetworkPriority priority) {
  if (!primary_family_) {
    LOG(WARNING) << *this << " " << __func__ << ": No connection exists";
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
    LOG(ERROR) << *this << " " << __func__ << ": invalid IP address "
               << event.ip_addr;
    return;
  }

  switch (event.status) {
    case Status::kFailed:
    case Status::kReachable:
      break;
    default:
      LOG(ERROR) << *this << " " << __func__ << ": invalid event " << event;
      return;
  }

  if (event.status == Status::kFailed) {
    ReportNeighborLinkMonitorFailure(technology_, ip_address->GetFamily(),
                                     event.role);
  }

  if (state_ == State::kIdle) {
    LOG(INFO) << *this << " " << __func__ << ": Idle state, ignoring " << event;
    return;
  }

  if (ignore_link_monitoring_) {
    LOG(INFO) << *this << " " << __func__
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
          LOG(INFO) << *this << " " << __func__ << ": "
                    << ip_address->GetFamily()
                    << " not configured, ignoring neighbor reachability event"
                    << event;
          return;
        }
        // Ignore reachability events related to a prior connection.
        if (network_config.ipv4_gateway != ip_address->ToIPv4Address()) {
          LOG(INFO) << *this << " " << __func__
                    << ": ignored neighbor reachability event with conflicting "
                       "gateway address "
                    << event;
          return;
        }
        ipv4_gateway_found_ = true;
        break;
      case net_base::IPFamily::kIPv6:
        if (network_config.ipv6_addresses.empty()) {
          LOG(INFO) << *this << " " << __func__ << ": "
                    << ip_address->GetFamily()
                    << " not configured, ignoring neighbor reachability event"
                    << event;
          return;
        }
        // Ignore reachability events related to a prior connection.
        if (network_config.ipv6_gateway != ip_address->ToIPv6Address()) {
          LOG(INFO) << *this << " " << __func__
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
    LOG(INFO) << *this << " " << __func__ << ": not possible to set to " << mode
              << " if the network is not connected";
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
    LOG(INFO) << *this << " " << __func__ << "(" << reason
              << "): Network is not connected";
    return;
  }

  if (network_monitor_->GetValidationMode() ==
      NetworkMonitor::ValidationMode::kDisabled) {
    LOG(INFO) << *this << " " << __func__ << "(" << reason
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
  LOG(INFO) << *this << " " << __func__ << ": " << previous_validation_state
            << " -> " << result.validation_state;

  if (!IsConnected()) {
    LOG(INFO) << *this << " " << __func__
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

void Network::StartConnectivityTest() {
  if (network_monitor_) {
    network_monitor_->StartConnectivityTest();
  }

  RequestTrafficCounters(
      base::BindOnce(&Network::LogTrafficCounter, weak_factory_.GetWeakPtr(),
                     context_.logging_tag(), raw_traffic_counter_snapshot_));
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

  // This function should only be called when network is not idle, so empty
  // session_id is unexpected.
  if (context_.session_id() == std::nullopt) {
    LOG(ERROR) << *this << " " << __func__ << ": missing session_id";
  }

  CHECK(patchpanel_client_);
  patchpanel_client_->RegisterOnAvailableCallback(base::BindOnce(
      &Network::CallPatchpanelConfigureNetwork, weak_factory_.GetWeakPtr(),
      interface_index_, interface_name_, area, network_config, priority_,
      technology_, context_.session_id().value_or(0), std::move(callback)));
}

void Network::CallPatchpanelConfigureNetwork(
    int interface_index,
    const std::string& interface_name,
    NetworkConfigArea area,
    const net_base::NetworkConfig& network_config,
    net_base::NetworkPriority priority,
    Technology technology,
    int session_id,
    base::OnceCallback<void(bool)> callback,
    bool is_service_ready) {
  if (!is_service_ready) {
    LOG(ERROR)
        << *this << " " << __func__
        << ": missing patchpanel service. Network setup might be partial.";
    return;
  }
  VLOG(2) << *this << " " << __func__;
  CHECK(patchpanel_client_);
  patchpanel_client_->ConfigureNetwork(
      interface_index, interface_name, static_cast<uint32_t>(area),
      network_config, priority,
      ShillTechnologyToPatchpanelClientTechnology(technology), session_id,
      std::move(callback));
}

void Network::CallPatchpanelDestroyNetwork() {
  // TODO(b/273742756): Connect with patchpanel DestroyNetwork API.
  if (!patchpanel_client_) {
    LOG(ERROR) << *this << " " << __func__ << ": missing patchpanel client.";
    return;
  }

  // This function should only be called when network is not idle, so empty
  // session_id is unexpected.
  if (context_.session_id() == std::nullopt) {
    LOG(ERROR) << *this << " " << __func__ << ": missing session_id";
  }

  // Note that we cannot use RegisterOnAvailableCallback here, as it is very
  // possible that the Network object get destroyed immediately after this and
  // the callback won't fire. That's particularly observable for the case of
  // VPN. Directly calling patchpanel dbus here as the possibility of patchpanel
  // service not ready when a Network is being destroyed is very low.
  patchpanel_client_->ConfigureNetwork(
      interface_index_, interface_name_,
      static_cast<uint32_t>(NetworkConfigArea::kClear), {}, {},
      ShillTechnologyToPatchpanelClientTechnology(technology_),
      context_.session_id().value_or(0), base::DoNothing());
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

bool Network::IsTrafficCounterRequestInFlight() {
  return !traffic_counter_request_callbacks_.empty();
}

void Network::RequestTrafficCounters(
    Network::GetTrafficCountersCallback callback) {
  bool is_request_in_flight = IsTrafficCounterRequestInFlight();
  traffic_counter_request_callbacks_.push_back(std::move(callback));
  if (is_request_in_flight) {
    return;
  }
  if (!patchpanel_client_) {
    LOG(ERROR) << *this << " " << __func__ << ": no patchpanel client";
    return;
  }
  patchpanel_client_->GetTrafficCounters(
      {interface_name_}, base::BindOnce(&Network::OnGetTrafficCountersResponse,
                                        weak_factory_.GetWeakPtr()));
}

void Network::OnGetTrafficCountersResponse(
    const std::vector<patchpanel::Client::TrafficCounter>& raw_counters) {
  // Group raw counters by source over all other dimensions (IP family, ...).
  TrafficCounterMap grouped_counters;
  for (const auto& counter : raw_counters) {
    grouped_counters[counter.source] += counter.traffic;
  }

  // Update all listeners.
  for (auto& ev : event_handlers_) {
    ev.OnTrafficCountersUpdate(interface_index_, grouped_counters);
  }
  for (auto& cb : traffic_counter_request_callbacks_) {
    std::move(cb).Run(grouped_counters);
  }
  traffic_counter_request_callbacks_.clear();
}

void Network::InitializeTrafficCounterSnapshot(
    const Network::TrafficCounterMap& raw_traffic_counters) {
  raw_traffic_counter_snapshot_ = raw_traffic_counters;
}

void Network::LogTrafficCounter(
    const std::string& logging_tag,
    const Network::TrafficCounterMap& initial_raw_traffic_counters,
    const Network::TrafficCounterMap& final_raw_traffic_counters) {
  const auto diff = DiffTrafficCounters(final_raw_traffic_counters,
                                        initial_raw_traffic_counters);
  for (const auto& [source, vec] : diff) {
    if (vec.rx_bytes == 0 && vec.tx_bytes == 0) {
      continue;
    }
    LOG(INFO) << logging_tag << " " << __func__ << " " << source
              << ": rx=" << ByteCountToString(vec.rx_bytes)
              << ", tx=" << ByteCountToString(vec.tx_bytes);
  }
}

// static
Network::TrafficCounterMap Network::AddTrafficCounters(
    const Network::TrafficCounterMap& in1,
    const Network::TrafficCounterMap& in2) {
  TrafficCounterMap out = in1;
  for (const auto& [source, traffic] : in2) {
    out[source] += traffic;
  }
  return out;
}

// static
Network::TrafficCounterMap Network::DiffTrafficCounters(
    const Network::TrafficCounterMap& new_map,
    const Network::TrafficCounterMap& old_map) {
  TrafficCounterMap out = new_map;
  // If any counter decreased it means that there has been a counter reset,
  // maybe because of a patchpanel restart. If that's the case simply take the
  // new snapshot instead of computing delta. A source found in the previous
  // snapshot but not found in the new snapshot also indicates that a reset
  // happened. See b/324992164.
  for (const auto& [source, traffic] : old_map) {
    const auto it = out.find(source);
    if (it == out.end()) {
      return new_map;
    }
    if (it->second.rx_bytes < traffic.rx_bytes ||
        it->second.tx_bytes < traffic.tx_bytes ||
        it->second.rx_packets < traffic.rx_packets ||
        it->second.tx_packets < traffic.tx_packets) {
      return new_map;
    }
    it->second -= traffic;
  }
  return out;
}

const std::string& Network::LoggingTag() const {
  return context_.logging_tag();
}

std::ostream& operator<<(std::ostream& stream, const Network& network) {
  return stream << network.context_.logging_tag();
}

// static
std::string Network::ByteCountToString(uint64_t b) {
  // Adds 2 digits precision but avoid rounding up errors with floating point
  // precision which may lead to print "1023.99KiB" as "1024KiB" instead of
  // "1MiB".
  b = b * 100;
  std::string unit = "B";
  if (b >= 102400) {
    unit = "KiB";
    b = b / 1024;
  }
  if (b >= 102400) {
    unit = "MiB";
    b = b / 1024;
  }
  if (b >= 102400) {
    unit = "GiB";
    b = b / 1024;
  }
  if (b % 100 == 0) {
    return std::to_string(b / 100) + unit;
  } else {
    double d = static_cast<double>(b) / 100L;
    return base::StringPrintf("%.2lf%s", d, unit.c_str());
  }
}

}  // namespace shill
