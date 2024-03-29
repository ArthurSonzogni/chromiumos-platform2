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

}  // namespace
}  // namespace shill
