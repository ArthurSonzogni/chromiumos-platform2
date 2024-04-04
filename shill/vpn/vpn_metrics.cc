// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/vpn/vpn_metrics.h"

#include <base/logging.h>
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

  base::TimeDelta duration = SetConnectionState(ConnectionState::kReconnecting);
  metrics_->SendToUMA(vpn_metrics::kMetricTimeConnectedToDisconnectedSeconds,
                      vpn_type_, duration.InSeconds());
}

void VPNDriverMetrics::ReportDisconnected() {
  vpn_metrics::VPNHistogramMetric metric;
  bool metric_is_in_seconds = false;
  switch (connection_state_) {
    case ConnectionState::kConnecting:
      metric = vpn_metrics::kMetricTimeConnectToIdleMillis;
      break;
    case ConnectionState::kReconnecting:
      metric = vpn_metrics::kMetricTimeReconnectToIdleMillis;
      break;
    case ConnectionState::kConnected:
      metric = vpn_metrics::kMetricTimeConnectedToDisconnectedSeconds;
      metric_is_in_seconds = true;
      break;
    case ConnectionState::kIdle:
      LOG(ERROR) << __func__ << ": unexpected connection state "
                 << base::to_underlying(connection_state_);
      return;
  }

  base::TimeDelta duration = SetConnectionState(ConnectionState::kIdle);
  int value =
      metric_is_in_seconds ? duration.InSeconds() : duration.InMilliseconds();
  metrics_->SendToUMA(metric, vpn_type_, value);
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
