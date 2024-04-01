// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/vpn/vpn_metrics.h"

#include <gtest/gtest.h>

#include "shill/mock_metrics.h"
#include "shill/vpn/vpn_metrics_internal.h"
#include "shill/vpn/vpn_types.h"

namespace shill {
namespace {

namespace vpn_metrics = vpn_metrics_internal;

using testing::_;
using testing::Mock;

class VPNDriverMetricsTest : public ::testing::Test {
 protected:
  VPNDriverMetricsTest() : driver_metrics_(&metrics_, VPNType::kOpenVPN) {}

  MockMetrics metrics_;
  VPNDriverMetrics driver_metrics_;
};

TEST_F(VPNDriverMetricsTest, ReportIPType) {
  using net_base::NetworkConfig;
  const net_base::IPv4CIDR ipv4_address =
      *net_base::IPv4CIDR::CreateFromCIDRString("0.0.0.0/16");
  const net_base::IPv6CIDR ipv6_address =
      *net_base::IPv6CIDR::CreateFromCIDRString("::/64");

  NetworkConfig config_ipv4_only;
  config_ipv4_only.ipv4_address = ipv4_address;
  EXPECT_CALL(metrics_, SendEnumToUMA(vpn_metrics::kMetricIPType, _,
                                      Metrics::kIPTypeIPv4Only));
  driver_metrics_.ReportIPType(config_ipv4_only);

  NetworkConfig config_ipv6_only;
  config_ipv6_only.ipv6_addresses = {ipv6_address};
  EXPECT_CALL(metrics_, SendEnumToUMA(vpn_metrics::kMetricIPType, _,
                                      Metrics::kIPTypeIPv6Only));
  driver_metrics_.ReportIPType(config_ipv6_only);

  NetworkConfig config_dual_stack;
  config_dual_stack.ipv4_address = ipv4_address;
  config_dual_stack.ipv6_addresses = {ipv6_address};
  EXPECT_CALL(metrics_, SendEnumToUMA(vpn_metrics::kMetricIPType, _,
                                      Metrics::kIPTypeDualStack));
  driver_metrics_.ReportIPType(config_dual_stack);
}

TEST_F(VPNDriverMetricsTest, ReportConnected) {
  struct {
    VPNType type;
    vpn_metrics::VpnDriver metric_val;
  } tcs[] = {
      {VPNType::kARC, vpn_metrics::kVpnDriverArc},
      {VPNType::kIKEv2, vpn_metrics::kVpnDriverIKEv2},
      {VPNType::kL2TPIPsec, vpn_metrics::kVpnDriverL2tpIpsec},
      {VPNType::kOpenVPN, vpn_metrics::kVpnDriverOpenVpn},
      {VPNType::kThirdParty, vpn_metrics::kVpnDriverThirdParty},
      {VPNType::kWireGuard, vpn_metrics::kVpnDriverWireGuard},
  };

  for (const auto tc : tcs) {
    EXPECT_CALL(metrics_,
                SendEnumToUMA(vpn_metrics::kMetricVpnDriver, tc.metric_val));
    VPNDriverMetrics driver_metrics(&metrics_, tc.type);
    driver_metrics.ReportConnected();
    Mock::VerifyAndClearExpectations(&metrics_);
  }
}

}  // namespace
}  // namespace shill
