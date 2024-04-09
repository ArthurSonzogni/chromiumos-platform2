// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/vpn/vpn_metrics.h"

#include <base/logging.h>
#include <base/notreached.h>
#include <base/time/time.h>
#include <base/types/cxx23_to_underlying.h>
#include <net-base/network_config.h>

#include "shill/metrics.h"
#include "shill/vpn/vpn_metrics_internal.h"

namespace shill {

namespace vpn_metrics = vpn_metrics_internal;

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

void VPNDriverMetrics::ReportIPType(
    const net_base::NetworkConfig& network_config) const {
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
  metrics_->SendEnumToUMA(vpn_metrics::kMetricIPType, vpn_type_, ip_type);
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

}  // namespace shill
