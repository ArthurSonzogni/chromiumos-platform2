// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/vpn/vpn_metrics.h"

#include <algorithm>
#include <vector>

#include <base/logging.h>
#include <base/notreached.h>
#include <base/time/time.h>
#include <base/types/cxx23_to_underlying.h>
#include <net-base/network_config.h>

#include "shill/metrics.h"
#include "shill/vpn/vpn_metrics_internal.h"

namespace shill {

namespace vpn_metrics = vpn_metrics_internal;

using net_base::NetworkConfig;

namespace {

void ReportDriverType(Metrics* metrics, VPNType vpn_type) {
  vpn_metrics::VpnDriver metrics_driver_type;
  switch (vpn_type) {
    case VPNType::kARC:
      metrics_driver_type = vpn_metrics::kVpnDriverArc;
      break;
    case VPNType::kIKEv2:
      metrics_driver_type = vpn_metrics::kVpnDriverIKEv2;
      break;
    case VPNType::kL2TPIPsec:
      metrics_driver_type = vpn_metrics::kVpnDriverL2tpIpsec;
      break;
    case VPNType::kOpenVPN:
      metrics_driver_type = vpn_metrics::kVpnDriverOpenVpn;
      break;
    case VPNType::kThirdParty:
      metrics_driver_type = vpn_metrics::kVpnDriverThirdParty;
      break;
    case VPNType::kWireGuard:
      metrics_driver_type = vpn_metrics::kVpnDriverWireGuard;
      break;
  }

  metrics->SendEnumToUMA(vpn_metrics::kMetricVpnDriver, metrics_driver_type);
}

void ReportIPType(Metrics* metrics,
                  VPNType vpn_type,
                  const NetworkConfig& network_config) {
  Metrics::IPType ip_type = Metrics::kIPTypeUnknown;
  bool has_ipv4 = network_config.ipv4_address.has_value();
  bool has_ipv6 = !network_config.ipv6_addresses.empty();
  // Note that ARC VPN will be reported as kIPTypeUnknown here, as its
  // GetNetworkConfig will not have any address.
  if (has_ipv4 && has_ipv6) {
    ip_type = Metrics::kIPTypeDualStack;
  } else if (has_ipv4) {
    ip_type = Metrics::kIPTypeIPv4Only;
  } else if (has_ipv6) {
    ip_type = Metrics::kIPTypeIPv6Only;
  }
  metrics->SendEnumToUMA(vpn_metrics::kMetricIPType, vpn_type, ip_type);
}

void ReportRoutingSetup(Metrics* metrics,
                        VPNType vpn_type,
                        net_base::IPFamily family,
                        const NetworkConfig& network_config) {
  using Prefixes = std::vector<net_base::IPCIDR>;

  const bool is_ipv4 = family == net_base::IPFamily::kIPv4;

  struct PrefixesInfo {
    bool has_default = false;
    std::optional<int> shortest_len = std::nullopt;
    int count = 0;
  };
  auto scan_prefixes = [family](const Prefixes& prefixes) -> PrefixesInfo {
    PrefixesInfo ret;
    for (const auto& prefix : prefixes) {
      int len = prefix.prefix_length();
      if (prefix.GetFamily() != family) {
        continue;
      }
      ret.count++;
      if (len == 0) {
        ret.has_default = true;
      }
      if (!ret.shortest_len.has_value() || *ret.shortest_len > len) {
        ret.shortest_len = len;
      }
    }
    return ret;
  };

  // Default IPv4 route needs special handling.
  Prefixes included_prefixes = network_config.included_route_prefixes;
  if (is_ipv4 && network_config.ipv4_default_route) {
    included_prefixes.push_back(net_base::IPCIDR(family));
  }

  auto included_info = scan_prefixes(included_prefixes);
  auto excluded_info = scan_prefixes(network_config.excluded_route_prefixes);

  vpn_metrics::RoutingType routing_type;
  if (!is_ipv4 && network_config.ipv6_blackhole_route) {
    routing_type = vpn_metrics::kRoutingTypeBlocked;
  } else if (included_info.has_default && excluded_info.count == 0) {
    routing_type = vpn_metrics::kRoutingTypeFull;
  } else if (included_info.count == 0) {
    routing_type = vpn_metrics::kRoutingTypeBypass;
  } else {
    routing_type = vpn_metrics::kRoutingTypeSplit;
  }

  const auto& metric_routing_type = is_ipv4
                                        ? vpn_metrics::kMetricIPv4RoutingType
                                        : vpn_metrics::kMetricIPv6RoutingType;
  metrics->SendEnumToUMA(metric_routing_type, vpn_type, routing_type);

  const auto& metric_included_routes_number =
      is_ipv4 ? vpn_metrics::kMetricIPv4IncludedRoutesNumber
              : vpn_metrics::kMetricIPv6IncludedRoutesNumber;
  const auto& metric_excluded_routes_number =
      is_ipv4 ? vpn_metrics::kMetricIPv4ExcludedRoutesNumber
              : vpn_metrics::kMetricIPv6ExcludedRoutesNumber;
  metrics->SendToUMA(metric_included_routes_number, vpn_type,
                     included_info.count);
  metrics->SendToUMA(metric_excluded_routes_number, vpn_type,
                     excluded_info.count);

  if (included_info.shortest_len.has_value()) {
    const auto& metric_included_routes_largest_prefix =
        is_ipv4 ? vpn_metrics::kMetricIPv4IncludedRoutesLargestPrefix
                : vpn_metrics::kMetricIPv6IncludedRoutesLargestPrefix;
    metrics->SendToUMA(metric_included_routes_largest_prefix, vpn_type,
                       *included_info.shortest_len);
  }
  if (excluded_info.shortest_len.has_value()) {
    const auto& metric_excluded_routes_largest_prefix =
        is_ipv4 ? vpn_metrics::kMetricIPv4ExcludedRoutesLargestPrefix
                : vpn_metrics::kMetricIPv6ExcludedRoutesLargestPrefix;
    metrics->SendToUMA(metric_excluded_routes_largest_prefix, vpn_type,
                       *excluded_info.shortest_len);
  }
}

void ReportNameServers(Metrics* metrics,
                       VPNType vpn_type,
                       const NetworkConfig& network_config) {
  bool has_ipv4 = false;
  bool has_ipv6 = false;
  for (const auto& server : network_config.dns_servers) {
    switch (server.GetFamily()) {
      case net_base::IPFamily::kIPv4:
        has_ipv4 = true;
        break;
      case net_base::IPFamily::kIPv6:
        has_ipv6 = true;
        break;
    }
  }
  vpn_metrics::NameServerConfig metric_value =
      vpn_metrics::kNameServerConfigNone;
  if (has_ipv4 && has_ipv6) {
    metric_value = vpn_metrics::kNameServerConfigDualStack;
  } else if (has_ipv4) {
    metric_value = vpn_metrics::kNameServerConfigIPv4Only;
  } else if (has_ipv6) {
    metric_value = vpn_metrics::kNameServerConfigIPv6Only;
  }
  metrics->SendEnumToUMA(vpn_metrics::kMetricNameServers, vpn_type,
                         metric_value);
}

vpn_metrics::ConnectFailureReason InterpretEndReasonAsConnectFailure(
    VPNEndReason reason) {
  switch (reason) {
    case VPNEndReason::kDisconnectRequest:
      return vpn_metrics::kConnectFailureReasonDisconnectRequest;
    case VPNEndReason::kNetworkChange:
      return vpn_metrics::kConnectFailureReasonNetworkChange;
    case VPNEndReason::kConnectFailureAuthPPP:
    case VPNEndReason::kConnectFailureAuthCert:
    case VPNEndReason::kConnectFailureAuthUserPassword:
      return vpn_metrics::kConnectFailureReasonAuth;
    case VPNEndReason::kConnectFailureDNSLookup:
      return vpn_metrics::kConnectFailureReasonDNSLookup;
    case VPNEndReason::kConnectTimeout:
      return vpn_metrics::kConnectFailureReasonConnectTimeout;
    case VPNEndReason::kInvalidConfig:
      return vpn_metrics::kConnectFailureReasonInvalidConfig;
    case VPNEndReason::kFailureInternal:
      return vpn_metrics::kConnectFailureReasonInternal;
    case VPNEndReason::kFailureUnknown:
      return vpn_metrics::kConnectFailureReasonUnknown;
  }
}

vpn_metrics::ConnectionLostReason InterpretEndReasonAsConnectionLost(
    VPNEndReason reason) {
  switch (reason) {
    case VPNEndReason::kDisconnectRequest:
      return vpn_metrics::kConnectionLostReasonDisconnectRequest;
    case VPNEndReason::kNetworkChange:
      return vpn_metrics::kConnectionLostReasonNetworkChange;
    case VPNEndReason::kConnectFailureAuthPPP:
    case VPNEndReason::kConnectFailureAuthCert:
    case VPNEndReason::kConnectFailureAuthUserPassword:
    case VPNEndReason::kConnectFailureDNSLookup:
    case VPNEndReason::kConnectTimeout:
    case VPNEndReason::kInvalidConfig:
      NOTREACHED() << __func__ << ": unexpected reason "
                   << VPNEndReasonToString(reason);
      return vpn_metrics::kConnectionLostReasonInternal;
    case VPNEndReason::kFailureInternal:
      return vpn_metrics::kConnectionLostReasonInternal;
    case VPNEndReason::kFailureUnknown:
      return vpn_metrics::kConnectionLostReasonUnknown;
  }
}

}  // namespace

VPNDriverMetrics::VPNDriverMetrics(Metrics* metrics, VPNType vpn_type)
    : metrics_(metrics), vpn_type_(vpn_type) {
  SetConnectionState(ConnectionState::kIdle);
}

VPNDriverMetrics::~VPNDriverMetrics() = default;

void VPNDriverMetrics::ReportNetworkConfig(
    const NetworkConfig& network_config) const {
  ReportIPType(metrics_, vpn_type_, network_config);
  ReportRoutingSetup(metrics_, vpn_type_, net_base::IPFamily::kIPv4,
                     network_config);
  ReportRoutingSetup(metrics_, vpn_type_, net_base::IPFamily::kIPv6,
                     network_config);
  ReportNameServers(metrics_, vpn_type_, network_config);
  metrics_->SendToUMA(vpn_metrics::kMetricMTU, vpn_type_,
                      network_config.mtu.has_value() ? *network_config.mtu : 0);
}

void VPNDriverMetrics::ReportConnecting() {
  if (connection_state_ != ConnectionState::kIdle) {
    LOG(ERROR) << __func__ << ": unexpected connection state "
               << base::to_underlying(connection_state_);
    return;
  }

  SetConnectionState(ConnectionState::kConnecting);
}

void VPNDriverMetrics::ReportConnected() {
  // TODO(b/331743444): Move this after the check below after we verify that the
  // error log below won't be hit.
  ReportDriverType(metrics_, vpn_type_);

  vpn_metrics::VPNHistogramMetric metric;
  switch (connection_state_) {
    case ConnectionState::kConnecting:
      metric = vpn_metrics::kMetricTimeConnectToConnectedMillis;
      break;
    case ConnectionState::kReconnecting:
      metric = vpn_metrics::kMetricTimeReconnectToConnectedMillis;
      break;
    case ConnectionState::kConnected:
    case ConnectionState::kIdle:
      LOG(ERROR) << __func__ << ": unexpected connection state "
                 << base::to_underlying(connection_state_);
      return;
  }

  base::TimeDelta duration = SetConnectionState(ConnectionState::kConnected);
  metrics_->SendToUMA(metric, vpn_type_, duration.InMilliseconds());
}

void VPNDriverMetrics::ReportReconnecting() {
  if (connection_state_ != ConnectionState::kConnected) {
    LOG(ERROR) << __func__ << ": unexpected connection state "
               << base::to_underlying(connection_state_);
    return;
  }

  metrics_->SendEnumToUMA(vpn_metrics::kMetricConnectionLostReason, vpn_type_,
                          vpn_metrics::kConnectionLostReasonReconnect);

  base::TimeDelta duration = SetConnectionState(ConnectionState::kReconnecting);
  metrics_->SendToUMA(vpn_metrics::kMetricTimeConnectedToDisconnectedSeconds,
                      vpn_type_, duration.InSeconds());
}

void VPNDriverMetrics::ReportDisconnected(VPNEndReason reason) {
  // Report connection end reason metric.
  switch (connection_state_) {
    case ConnectionState::kConnecting:
    case ConnectionState::kReconnecting:
      metrics_->SendEnumToUMA(vpn_metrics::kMetricConnectFailureReason,
                              vpn_type_,
                              InterpretEndReasonAsConnectFailure(reason));
      break;
    case ConnectionState::kConnected:
      metrics_->SendEnumToUMA(vpn_metrics::kMetricConnectionLostReason,
                              vpn_type_,
                              InterpretEndReasonAsConnectionLost(reason));
      break;
    case ConnectionState::kIdle:
      LOG(ERROR) << __func__ << ": unexpected connection state "
                 << base::to_underlying(connection_state_);
      return;
  }

  // Report timer metric.
  vpn_metrics::VPNHistogramMetric timer_metric;
  bool metric_is_in_seconds = false;
  switch (connection_state_) {
    case ConnectionState::kConnecting:
      timer_metric = vpn_metrics::kMetricTimeConnectToIdleMillis;
      break;
    case ConnectionState::kReconnecting:
      timer_metric = vpn_metrics::kMetricTimeReconnectToIdleMillis;
      break;
    case ConnectionState::kConnected:
      timer_metric = vpn_metrics::kMetricTimeConnectedToDisconnectedSeconds;
      metric_is_in_seconds = true;
      break;
    case ConnectionState::kIdle:
      NOTREACHED();  // Already checked above.
      return;
  }

  base::TimeDelta duration = SetConnectionState(ConnectionState::kIdle);
  int value =
      metric_is_in_seconds ? duration.InSeconds() : duration.InMilliseconds();
  metrics_->SendToUMA(timer_metric, vpn_type_, value);
}

base::TimeDelta VPNDriverMetrics::SetConnectionState(
    ConnectionState new_state) {
  base::TimeTicks last_changed = connection_state_last_changed_at_;
  base::TimeTicks now = base::TimeTicks::Now();
  base::TimeDelta diff = now - last_changed;

  connection_state_ = new_state;
  connection_state_last_changed_at_ = now;

  return diff;
}

void VPNGeneralMetrics::ReportServicesNumber(int num) {
  metrics_->SendToUMA(vpn_metrics::kMetricServicesNumber, num);
}

}  // namespace shill
