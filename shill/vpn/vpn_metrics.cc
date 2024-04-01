// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <net-base/network_config.h>

#include "shill/metrics.h"
#include "shill/vpn/vpn_metrics.h"
#include "shill/vpn/vpn_metrics_internal.h"

namespace shill {

namespace vpn_metrics = vpn_metrics_internal;

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

void VPNDriverMetrics::ReportConnected() const {
  vpn_metrics::VpnDriver metrics_driver_type;
  switch (vpn_type_) {
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

  metrics_->SendEnumToUMA(vpn_metrics::kMetricVpnDriver, metrics_driver_type);
}

}  // namespace shill
