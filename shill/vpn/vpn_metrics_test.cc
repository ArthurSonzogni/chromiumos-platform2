// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/vpn/vpn_metrics.h"

#include <memory>

#include <base/test/task_environment.h>
#include <gtest/gtest.h>

#include "shill/mock_metrics.h"
#include "shill/vpn/vpn_end_reason.h"
#include "shill/vpn/vpn_metrics_internal.h"
#include "shill/vpn/vpn_types.h"

namespace shill {

namespace vpn_metrics = vpn_metrics_internal;

using net_base::IPAddress;
using net_base::IPCIDR;
using net_base::IPv4CIDR;
using net_base::IPv6CIDR;
using net_base::NetworkConfig;

using testing::_;
using testing::AnyNumber;
using testing::Matcher;
using testing::Mock;
using testing::StrictMock;

namespace {

class VPNDriverMetricsTest : public ::testing::Test {
 protected:
  VPNDriverMetricsTest() : driver_metrics_(&metrics_, VPNType::kOpenVPN) {}

  MockMetrics metrics_;
  VPNDriverMetrics driver_metrics_;
};

TEST_F(VPNDriverMetricsTest, ReportIPType) {
  const IPv4CIDR ipv4_address = *IPv4CIDR::CreateFromCIDRString("0.0.0.0/16");
  const IPv6CIDR ipv6_address = *IPv6CIDR::CreateFromCIDRString("::/64");

  // Don't care about other metrics in this test.
  EXPECT_CALL(metrics_, SendEnumToUMA(_, Matcher<VPNType>(_), _))
      .Times(AnyNumber());

  NetworkConfig config_ipv4_only;
  config_ipv4_only.ipv4_address = ipv4_address;
  EXPECT_CALL(metrics_, SendEnumToUMA(vpn_metrics::kMetricIPType, _,
                                      Metrics::kIPTypeIPv4Only));
  driver_metrics_.ReportNetworkConfig(config_ipv4_only);

  NetworkConfig config_ipv6_only;
  config_ipv6_only.ipv6_addresses = {ipv6_address};
  EXPECT_CALL(metrics_, SendEnumToUMA(vpn_metrics::kMetricIPType, _,
                                      Metrics::kIPTypeIPv6Only));
  driver_metrics_.ReportNetworkConfig(config_ipv6_only);

  NetworkConfig config_dual_stack;
  config_dual_stack.ipv4_address = ipv4_address;
  config_dual_stack.ipv6_addresses = {ipv6_address};
  EXPECT_CALL(metrics_, SendEnumToUMA(vpn_metrics::kMetricIPType, _,
                                      Metrics::kIPTypeDualStack));
  driver_metrics_.ReportNetworkConfig(config_dual_stack);
}

TEST_F(VPNDriverMetricsTest, ReportRoutingFull) {
  // Don't care about other metrics in this test.
  EXPECT_CALL(metrics_, SendEnumToUMA(_, Matcher<VPNType>(_), _))
      .Times(AnyNumber());
  EXPECT_CALL(metrics_, SendToUMA(_, Matcher<VPNType>(_), _))
      .Times(AnyNumber());

  NetworkConfig config;
  config.ipv4_default_route = true;
  config.included_route_prefixes = {*IPCIDR::CreateFromCIDRString("::/0")};

  // Routing type.
  EXPECT_CALL(metrics_, SendEnumToUMA(vpn_metrics::kMetricIPv4RoutingType, _,
                                      vpn_metrics::kRoutingTypeFull));
  EXPECT_CALL(metrics_, SendEnumToUMA(vpn_metrics::kMetricIPv6RoutingType, _,
                                      vpn_metrics::kRoutingTypeFull));
  // Prefix number.
  EXPECT_CALL(metrics_,
              SendToUMA(vpn_metrics::kMetricIPv4IncludedRoutesNumber, _, 1));
  EXPECT_CALL(metrics_,
              SendToUMA(vpn_metrics::kMetricIPv6IncludedRoutesNumber, _, 1));
  EXPECT_CALL(metrics_,
              SendToUMA(vpn_metrics::kMetricIPv4ExcludedRoutesNumber, _, 0));
  EXPECT_CALL(metrics_,
              SendToUMA(vpn_metrics::kMetricIPv6ExcludedRoutesNumber, _, 0));

  // Largest prefix.
  EXPECT_CALL(
      metrics_,
      SendToUMA(vpn_metrics::kMetricIPv4IncludedRoutesLargestPrefix, _, 0));
  EXPECT_CALL(
      metrics_,
      SendToUMA(vpn_metrics::kMetricIPv6IncludedRoutesLargestPrefix, _, 0));
  EXPECT_CALL(
      metrics_,
      SendToUMA(vpn_metrics::kMetricIPv4ExcludedRoutesLargestPrefix, _, _))
      .Times(0);
  EXPECT_CALL(
      metrics_,
      SendToUMA(vpn_metrics::kMetricIPv6ExcludedRoutesLargestPrefix, _, _))
      .Times(0);

  driver_metrics_.ReportNetworkConfig(config);
}

TEST_F(VPNDriverMetricsTest, ReportRoutingSplit) {
  // Don't care about other metrics in this test.
  EXPECT_CALL(metrics_, SendEnumToUMA(_, Matcher<VPNType>(_), _))
      .Times(AnyNumber());
  EXPECT_CALL(metrics_, SendToUMA(_, Matcher<VPNType>(_), _))
      .Times(AnyNumber());

  // IPv4 has default route with excluded routes. IPv6 has both include and
  // excluded routes.
  NetworkConfig config;
  config.ipv4_default_route = true;
  config.included_route_prefixes = {*IPCIDR::CreateFromCIDRString("fd00::/64"),
                                    *IPCIDR::CreateFromCIDRString("fd01::/96")};
  config.excluded_route_prefixes = {
      *IPCIDR::CreateFromCIDRString("10.0.0.0/16"),
      *IPCIDR::CreateFromCIDRString("10.1.0.0/24"),
      *IPCIDR::CreateFromCIDRString("fd01::1/128")};

  // Routing type.
  EXPECT_CALL(metrics_, SendEnumToUMA(vpn_metrics::kMetricIPv4RoutingType, _,
                                      vpn_metrics::kRoutingTypeSplit));
  EXPECT_CALL(metrics_, SendEnumToUMA(vpn_metrics::kMetricIPv6RoutingType, _,
                                      vpn_metrics::kRoutingTypeSplit));
  // Prefix number.
  EXPECT_CALL(metrics_,
              SendToUMA(vpn_metrics::kMetricIPv4IncludedRoutesNumber, _, 1));
  EXPECT_CALL(metrics_,
              SendToUMA(vpn_metrics::kMetricIPv6IncludedRoutesNumber, _, 2));
  EXPECT_CALL(metrics_,
              SendToUMA(vpn_metrics::kMetricIPv4ExcludedRoutesNumber, _, 2));
  EXPECT_CALL(metrics_,
              SendToUMA(vpn_metrics::kMetricIPv6ExcludedRoutesNumber, _, 1));

  // Largest prefix.
  EXPECT_CALL(
      metrics_,
      SendToUMA(vpn_metrics::kMetricIPv4IncludedRoutesLargestPrefix, _, 0));
  EXPECT_CALL(
      metrics_,
      SendToUMA(vpn_metrics::kMetricIPv6IncludedRoutesLargestPrefix, _, 64));
  EXPECT_CALL(
      metrics_,
      SendToUMA(vpn_metrics::kMetricIPv4ExcludedRoutesLargestPrefix, _, 16));
  EXPECT_CALL(
      metrics_,
      SendToUMA(vpn_metrics::kMetricIPv6ExcludedRoutesLargestPrefix, _, 128));

  driver_metrics_.ReportNetworkConfig(config);
}

TEST_F(VPNDriverMetricsTest, ReportRoutingByPassAndBlocked) {
  // Don't care about other metrics in this test.
  EXPECT_CALL(metrics_, SendEnumToUMA(_, Matcher<VPNType>(_), _))
      .Times(AnyNumber());
  EXPECT_CALL(metrics_, SendToUMA(_, Matcher<VPNType>(_), _))
      .Times(AnyNumber());

  // IPv4 has default route with excluded routes. IPv6 has both include and
  // excluded routes.
  NetworkConfig config;
  config.ipv4_default_route = false;
  config.ipv6_blackhole_route = true;

  // Routing type.
  EXPECT_CALL(metrics_, SendEnumToUMA(vpn_metrics::kMetricIPv4RoutingType, _,
                                      vpn_metrics::kRoutingTypeBypass));
  EXPECT_CALL(metrics_, SendEnumToUMA(vpn_metrics::kMetricIPv6RoutingType, _,
                                      vpn_metrics::kRoutingTypeBlocked));
  // Prefix number.
  EXPECT_CALL(metrics_,
              SendToUMA(vpn_metrics::kMetricIPv4IncludedRoutesNumber, _, 0));
  EXPECT_CALL(metrics_,
              SendToUMA(vpn_metrics::kMetricIPv6IncludedRoutesNumber, _, 0));
  EXPECT_CALL(metrics_,
              SendToUMA(vpn_metrics::kMetricIPv4ExcludedRoutesNumber, _, 0));
  EXPECT_CALL(metrics_,
              SendToUMA(vpn_metrics::kMetricIPv6ExcludedRoutesNumber, _, 0));

  // Largest prefix.
  EXPECT_CALL(
      metrics_,
      SendToUMA(vpn_metrics::kMetricIPv4IncludedRoutesLargestPrefix, _, _))
      .Times(0);
  EXPECT_CALL(
      metrics_,
      SendToUMA(vpn_metrics::kMetricIPv6IncludedRoutesLargestPrefix, _, _))
      .Times(0);
  EXPECT_CALL(
      metrics_,
      SendToUMA(vpn_metrics::kMetricIPv4ExcludedRoutesLargestPrefix, _, _))
      .Times(0);
  EXPECT_CALL(
      metrics_,
      SendToUMA(vpn_metrics::kMetricIPv6ExcludedRoutesLargestPrefix, _, _))
      .Times(0);

  driver_metrics_.ReportNetworkConfig(config);
}

TEST_F(VPNDriverMetricsTest, ReportNameServers) {
  // Don't care about other metrics in this test.
  EXPECT_CALL(metrics_, SendEnumToUMA(_, Matcher<VPNType>(_), _))
      .Times(AnyNumber());

  const IPAddress kIPv4Addr = *IPAddress::CreateFromString("1.2.3.4");
  const IPAddress kIPv6Addr = *IPAddress::CreateFromString("::1");

  NetworkConfig config;

  EXPECT_CALL(metrics_, SendEnumToUMA(vpn_metrics::kMetricNameServers, _,
                                      vpn_metrics::kNameServerConfigNone));
  driver_metrics_.ReportNetworkConfig(config);

  config.dns_servers = {kIPv4Addr};
  EXPECT_CALL(metrics_, SendEnumToUMA(vpn_metrics::kMetricNameServers, _,
                                      vpn_metrics::kNameServerConfigIPv4Only));
  driver_metrics_.ReportNetworkConfig(config);

  config.dns_servers = {kIPv6Addr};
  EXPECT_CALL(metrics_, SendEnumToUMA(vpn_metrics::kMetricNameServers, _,
                                      vpn_metrics::kNameServerConfigIPv6Only));
  driver_metrics_.ReportNetworkConfig(config);

  config.dns_servers = {kIPv4Addr, kIPv6Addr};
  EXPECT_CALL(metrics_, SendEnumToUMA(vpn_metrics::kMetricNameServers, _,
                                      vpn_metrics::kNameServerConfigDualStack));
  driver_metrics_.ReportNetworkConfig(config);
}

TEST_F(VPNDriverMetricsTest, ReportMTU) {
  // Don't care about other metrics in this test.
  EXPECT_CALL(metrics_, SendToUMA(_, Matcher<VPNType>(_), _))
      .Times(AnyNumber());

  NetworkConfig config;

  // Report 0 if not set.
  EXPECT_CALL(metrics_, SendToUMA(vpn_metrics::kMetricMTU, _, 0));
  driver_metrics_.ReportNetworkConfig(config);

  config.mtu = 1500;
  EXPECT_CALL(metrics_, SendToUMA(vpn_metrics::kMetricMTU, _, 1500));
  driver_metrics_.ReportNetworkConfig(config);
}

TEST_F(VPNDriverMetricsTest, ReportDriverType) {
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

namespace {
class VPNMetricsStateMachineTest : public ::testing::Test {
 protected:
  static constexpr auto kVPNType = VPNType::kWireGuard;
  static constexpr auto kEndReason = VPNEndReason::kFailureUnknown;

  // The following functions create a VPNDriverMetrics object, set the
  // connection state to a desired state for start testing, and then disallow
  // calls to report VPN metrics. The caller of these function can set up
  // expectations after that.
  std::unique_ptr<VPNDriverMetrics> CreateInIdleState() {
    AcceptAllSendUMACallFromNow();
    auto ret = std::make_unique<VPNDriverMetrics>(&metrics_, kVPNType);
    ExpectNoSendUMACallFromNow();
    return ret;
  }

  std::unique_ptr<VPNDriverMetrics> CreateInConnectingState() {
    AcceptAllSendUMACallFromNow();
    auto ret = std::make_unique<VPNDriverMetrics>(&metrics_, kVPNType);
    ret->ReportConnecting();
    ExpectNoSendUMACallFromNow();
    return ret;
  }

  std::unique_ptr<VPNDriverMetrics> CreateInConnectedState() {
    AcceptAllSendUMACallFromNow();
    auto ret = std::make_unique<VPNDriverMetrics>(&metrics_, kVPNType);
    ret->ReportConnecting();
    ret->ReportConnected();
    ExpectNoSendUMACallFromNow();
    return ret;
  }

  std::unique_ptr<VPNDriverMetrics> CreateInReconnectingState() {
    AcceptAllSendUMACallFromNow();
    auto ret = std::make_unique<VPNDriverMetrics>(&metrics_, kVPNType);
    ret->ReportConnecting();
    ret->ReportConnected();
    ret->ReportReconnecting();
    ExpectNoSendUMACallFromNow();
    return ret;
  }

  void AcceptAllSendUMACallFromNow() {
    Mock::VerifyAndClearExpectations(&metrics_);
    // SendToUMA has overloads, so we need to hint the type here.
    EXPECT_CALL(metrics_, SendToUMA(_, Matcher<VPNType>(_), _))
        .Times(AnyNumber());
    EXPECT_CALL(metrics_, SendEnumToUMA(_, Matcher<VPNType>(_), _))
        .Times(AnyNumber());
  }

  void ExpectNoSendUMACallFromNow() {
    // SendToUMA has overloads, so we need to hint the type here.
    EXPECT_CALL(metrics_, SendToUMA(_, Matcher<VPNType>(_), _)).Times(0);
    EXPECT_CALL(metrics_, SendEnumToUMA(_, Matcher<VPNType>(_), _)).Times(0);
  }

  void ForwardTime(base::TimeDelta interval) {
    task_environment_.FastForwardBy(interval);
  }

  MockMetrics metrics_;

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
};

// No metrics will be reported for all functions calls from the Idle state (or
// invalid event on this state).
TEST_F(VPNMetricsStateMachineTest, Idle) {
  auto driver_metrics = CreateInIdleState();
  driver_metrics->ReportConnecting();

  driver_metrics = CreateInIdleState();
  driver_metrics->ReportConnected();

  driver_metrics = CreateInIdleState();
  driver_metrics->ReportReconnecting();

  driver_metrics = CreateInIdleState();
  driver_metrics->ReportDisconnected(kEndReason);
}

TEST_F(VPNMetricsStateMachineTest, Connecting) {
  // Invalid event.
  auto driver_metrics = CreateInConnectingState();
  driver_metrics->ReportConnecting();

  // Report connecting to connected.
  driver_metrics = CreateInConnectingState();
  ForwardTime(base::Seconds(3));
  EXPECT_CALL(metrics_,
              SendToUMA(vpn_metrics::kMetricTimeConnectToConnectedMillis,
                        kVPNType, 3000));
  driver_metrics->ReportConnected();

  // Invalid event.
  driver_metrics = CreateInConnectingState();
  driver_metrics->ReportReconnecting();

  // Report connecting to failure.
  driver_metrics = CreateInConnectingState();
  ForwardTime(base::Seconds(4));
  EXPECT_CALL(metrics_, SendToUMA(vpn_metrics::kMetricTimeConnectToIdleMillis,
                                  kVPNType, 4000));
  EXPECT_CALL(metrics_,
              SendEnumToUMA(vpn_metrics::kMetricConnectFailureReason, kVPNType,
                            vpn_metrics::kConnectFailureReasonUnknown));
  driver_metrics->ReportDisconnected(kEndReason);
}

TEST_F(VPNMetricsStateMachineTest, Connected) {
  // Invalid event.
  auto driver_metrics = CreateInConnectedState();
  driver_metrics->ReportConnecting();

  // Invalid event.
  driver_metrics = CreateInConnectedState();
  driver_metrics->ReportConnected();

  // Report connected to disconnected.
  driver_metrics = CreateInConnectedState();
  ForwardTime(base::Seconds(5));
  EXPECT_CALL(metrics_,
              SendToUMA(vpn_metrics::kMetricTimeConnectedToDisconnectedSeconds,
                        kVPNType, 5));
  EXPECT_CALL(metrics_,
              SendEnumToUMA(vpn_metrics::kMetricConnectionLostReason, kVPNType,
                            vpn_metrics::kConnectionLostReasonReconnect));
  driver_metrics->ReportReconnecting();

  // Report connected to disconnected.
  driver_metrics = CreateInConnectedState();
  ForwardTime(base::Seconds(6));
  EXPECT_CALL(metrics_,
              SendToUMA(vpn_metrics::kMetricTimeConnectedToDisconnectedSeconds,
                        kVPNType, 6));
  EXPECT_CALL(metrics_,
              SendEnumToUMA(vpn_metrics::kMetricConnectionLostReason, kVPNType,
                            vpn_metrics::kConnectionLostReasonUnknown));
  driver_metrics->ReportDisconnected(kEndReason);
}

TEST_F(VPNMetricsStateMachineTest, Reconnecting) {
  // Invalid event.
  auto driver_metrics = CreateInReconnectingState();
  driver_metrics->ReportConnecting();

  // Report reconnecting to connected.
  driver_metrics = CreateInReconnectingState();
  ForwardTime(base::Seconds(7));
  EXPECT_CALL(metrics_,
              SendToUMA(vpn_metrics::kMetricTimeReconnectToConnectedMillis,
                        kVPNType, 7000));
  driver_metrics->ReportConnected();

  // Invalid event.
  driver_metrics = CreateInReconnectingState();
  driver_metrics->ReportReconnecting();

  // Report reconnecting to failure.
  driver_metrics = CreateInReconnectingState();
  ForwardTime(base::Seconds(8));
  EXPECT_CALL(metrics_, SendToUMA(vpn_metrics::kMetricTimeReconnectToIdleMillis,
                                  kVPNType, 8000));
  EXPECT_CALL(metrics_,
              SendEnumToUMA(vpn_metrics::kMetricConnectFailureReason, kVPNType,
                            vpn_metrics::kConnectFailureReasonUnknown));
  driver_metrics->ReportDisconnected(kEndReason);
}

}  // namespace

}  // namespace shill
