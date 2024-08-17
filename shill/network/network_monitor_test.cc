// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/network_monitor.h"

#include <memory>
#include <utility>
#include <vector>

#include <base/functional/bind.h>
#include <base/test/task_environment.h>
#include <base/time/time.h>
#include <chromeos/net-base/http_url.h>
#include <chromeos/net-base/ip_address.h>
#include <chromeos/net-base/ipv4_address.h>
#include <chromeos/net-base/network_config.h>
#include <chromeos/patchpanel/dbus/fake_client.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "shill/event_dispatcher.h"
#include "shill/mock_metrics.h"
#include "shill/network/mock_capport_proxy.h"
#include "shill/network/mock_connection_diagnostics.h"
#include "shill/network/mock_portal_detector.h"
#include "shill/network/mock_validation_log.h"
#include "shill/network/portal_detector.h"
#include "shill/network/trial_scheduler.h"
#include "shill/technology.h"

namespace shill {
namespace {

const std::vector<net_base::IPAddress> kIPv4DnsList = {
    net_base::IPAddress(net_base::IPv4Address(8, 8, 8, 8)),
    net_base::IPAddress(net_base::IPv4Address(8, 8, 4, 4)),
};
const std::vector<net_base::IPAddress> kIPv6DnsList = {
    *net_base::IPAddress::CreateFromString("2001:4860:4860::8888"),
    *net_base::IPAddress::CreateFromString("2001:4860:4860::8844"),
};
const net_base::IPv4Address kIPv4GatewayAddress =
    *net_base::IPv4Address::CreateFromString("192.168.1.1");
const net_base::IPv6Address kIPv6GatewayAddress =
    *net_base::IPv6Address::CreateFromString("fee2::11b2:53f:13be:125e");
constexpr int kInterfaceIndex = 1;
constexpr std::string_view kInterface = "wlan1";
constexpr std::string_view kLoggingTag = "logging_tag";
constexpr Technology kTechnology = Technology::kWiFi;
constexpr NetworkMonitor::ValidationMode kDefaultValidationMode =
    NetworkMonitor::ValidationMode::kFullValidation;
const net_base::HttpUrl kCapportAPI =
    *net_base::HttpUrl::CreateFromString("https://example.org/api");
const net_base::HttpUrl kUserPortalUrl =
    *net_base::HttpUrl::CreateFromString("https://example.org/portal.html");
const int kNumAttempts = 3;

using ::testing::_;
using ::testing::ElementsAreArray;
using ::testing::Eq;
using ::testing::Return;
using ::testing::WithArg;

class MockClient : public NetworkMonitor::ClientNetwork {
 public:
  MOCK_METHOD(const net_base::NetworkConfig&,
              GetCurrentConfig,
              (),
              (const, override));
  MOCK_METHOD(void,
              OnNetworkMonitorResult,
              (const NetworkMonitor::Result&),
              (override));
  MOCK_METHOD(void, OnValidationStarted, (bool), (override));
};

class NetworkMonitorTest : public ::testing::Test {
 public:
  NetworkMonitorTest() {
    auto portal_detector = std::make_unique<MockPortalDetector>();
    mock_portal_detector_ = portal_detector.get();

    auto mock_capport_proxy_factory =
        std::make_unique<MockCapportProxyFactory>();
    mock_capport_proxy_factory_ = mock_capport_proxy_factory.get();

    auto mock_validation_log = std::make_unique<MockValidationLog>();
    mock_validation_log_ = mock_validation_log.get();
    EXPECT_CALL(*mock_validation_log_, RecordMetrics).Times(1);

    auto mock_connection_diagnostics_factory =
        std::make_unique<MockConnectionDiagnosticsFactory>();
    mock_connection_diagnostics_factory_ =
        mock_connection_diagnostics_factory.get();

    network_monitor_ = std::make_unique<NetworkMonitor>(
        &dispatcher_, &metrics_, &client_, &patchpanel_client_, kTechnology,
        kInterfaceIndex, kInterface, probing_configuration_,
        kDefaultValidationMode, std::move(mock_validation_log), kLoggingTag,
        std::move(mock_capport_proxy_factory),
        std::move(mock_connection_diagnostics_factory));
    network_monitor_->set_portal_detector_for_testing(
        std::move(portal_detector));

    SetCurrentNetworkConfig(net_base::IPFamily::kIPv4, kIPv4DnsList);
  }

  void SetCurrentNetworkConfig(net_base::IPFamily ip_family,
                               std::vector<net_base::IPAddress> dns_servers) {
    switch (ip_family) {
      case net_base::IPFamily::kIPv4:
        config_.ipv4_address =
            *net_base::IPv4CIDR::CreateFromCIDRString("192.168.1.2/24");
        config_.ipv4_gateway = kIPv4GatewayAddress;
        break;
      case net_base::IPFamily::kIPv6:
        config_.ipv6_addresses.push_back(
            *net_base::IPv6CIDR::CreateFromCIDRString("fd00::2/64"));
        config_.ipv6_gateway = kIPv6GatewayAddress;
        break;
    }
    config_.dns_servers = std::move(dns_servers);

    ON_CALL(client_, GetCurrentConfig)
        .WillByDefault(testing::ReturnRef(config_));
  }

  void SetCurrentDualStackNetworkConfig(
      std::vector<net_base::IPAddress> dns_servers) {
    config_.ipv4_address =
        *net_base::IPv4CIDR::CreateFromCIDRString("192.168.1.2/24");
    config_.ipv4_gateway = kIPv4GatewayAddress;
    config_.ipv6_addresses.push_back(
        *net_base::IPv6CIDR::CreateFromCIDRString("fd00::2/64"));
    config_.ipv6_gateway = kIPv6GatewayAddress;
    config_.dns_servers = std::move(dns_servers);

    ON_CALL(client_, GetCurrentConfig)
        .WillByDefault(testing::ReturnRef(config_));
  }

  MockCapportProxy* SetCapportProxy() {
    auto capport_proxy = std::make_unique<MockCapportProxy>();
    MockCapportProxy* capport_proxy_p = capport_proxy.get();
    network_monitor_->set_capport_proxy_for_testing(std::move(capport_proxy));
    return capport_proxy_p;
  }

  // Runs the NetworkMonitor::Start() method and wait until the trial scheduled
  // by the method has been executed, then checks the callback with the expected
  // result |is_success| is called.
  void StartAndExpectResult(NetworkMonitor::ValidationReason reason,
                            bool is_success) {
    EXPECT_CALL(client_, OnValidationStarted(is_success)).Times(1);
    network_monitor_->Start(reason);
    task_environment_.RunUntilIdle();
  }

  // Starts NetworkMonitor and waits until PortalDetector returns |result|.
  void StartWithPortalDetectorResultReturned(
      bool expect_http_only, const PortalDetector::Result& result) {
    EXPECT_CALL(
        *mock_portal_detector_,
        Start(expect_http_only, net_base::IPFamily::kIPv4, kIPv4DnsList, _))
        .WillOnce(testing::WithArg<3>(
            [result](PortalDetector::ResultCallback callback) {
              std::move(callback).Run(result);
            }));
    EXPECT_CALL(*mock_validation_log_, AddPortalDetectorResult(result));
    EXPECT_CALL(client_,
                OnNetworkMonitorResult(
                    NetworkMonitor::Result::FromPortalDetectorResult(result)))
        .Times(1);

    StartAndExpectResult(NetworkMonitor::ValidationReason::kDBusRequest,
                         /*is_success=*/true);
  }

 protected:
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};

  EventDispatcher dispatcher_;
  MockMetrics metrics_;
  PortalDetector::ProbingConfiguration probing_configuration_;

  net_base::NetworkConfig config_;
  MockClient client_;
  patchpanel::FakeClient patchpanel_client_;
  std::unique_ptr<NetworkMonitor> network_monitor_;

  // These instance are owned by |network_monitor_|.
  MockPortalDetector* mock_portal_detector_;
  MockCapportProxyFactory* mock_capport_proxy_factory_;
  MockConnectionDiagnosticsFactory* mock_connection_diagnostics_factory_;
  MockValidationLog* mock_validation_log_;
};

TEST_F(NetworkMonitorTest, StartWithImmediatelyTrigger) {
  // These reason trigger the legacy probe immediately.
  const NetworkMonitor::ValidationReason reasons[] = {
      NetworkMonitor::ValidationReason::kDBusRequest,
      NetworkMonitor::ValidationReason::kEthernetGatewayReachable,
      NetworkMonitor::ValidationReason::kCapportTimeOver,
      NetworkMonitor::ValidationReason::kServiceReorder,
  };

  MockCapportProxy* mock_capport_proxy = SetCapportProxy();
  for (const auto reason : reasons) {
    EXPECT_CALL(
        *mock_portal_detector_,
        Start(/*http_only=*/false, net_base::IPFamily::kIPv4, kIPv4DnsList, _))
        .Times(1);
    EXPECT_CALL(*mock_capport_proxy, SendRequest).Times(1);
    EXPECT_CALL(client_, OnValidationStarted(true)).Times(1);

    // NetworkMonitor::Start() should schedule PortalDetector::Start()
    // immediately (i.e. delay=0).
    network_monitor_->Start(reason);
    EXPECT_TRUE(task_environment_.NextMainThreadPendingTaskDelay().is_zero());

    task_environment_.RunUntilIdle();
  }
}

TEST_F(NetworkMonitorTest, StartWithoutDNS) {
  SetCurrentNetworkConfig(net_base::IPFamily::kIPv4, {});
  MockCapportProxy* mock_capport_proxy = SetCapportProxy();

  EXPECT_CALL(*mock_portal_detector_, Start).Times(0);
  EXPECT_CALL(*mock_capport_proxy, SendRequest).Times(0);

  StartAndExpectResult(NetworkMonitor::ValidationReason::kDBusRequest, false);
}

TEST_F(NetworkMonitorTest, SetCapportEnabled) {
  SetCurrentNetworkConfig(net_base::IPFamily::kIPv4, kIPv4DnsList);
  MockCapportProxy* mock_capport_proxy = SetCapportProxy();

  // The capport_proxy should be called normally before CAPPORT is disabled.
  EXPECT_CALL(*mock_capport_proxy, SendRequest).Times(1);
  network_monitor_->Start(NetworkMonitor::ValidationReason::kDBusRequest);
  task_environment_.RunUntilIdle();
  testing::Mock::VerifyAndClearExpectations(mock_capport_proxy);

  // After CAPPORT is disabled, the capport_proxy should not be called.
  EXPECT_CALL(*mock_capport_proxy, SendRequest).Times(0);
  network_monitor_->SetCapportEnabled(false);
  network_monitor_->Start(NetworkMonitor::ValidationReason::kDBusRequest);
  task_environment_.RunUntilIdle();
  testing::Mock::VerifyAndClearExpectations(mock_capport_proxy);

  // After enabling CAPPORT again, the validation should be scheduled
  // automatically.
  EXPECT_CALL(*mock_capport_proxy, SendRequest).Times(1);
  network_monitor_->SetCapportEnabled(true);
  task_environment_.FastForwardBy(TrialScheduler::kBaseInterval);
  testing::Mock::VerifyAndClearExpectations(mock_capport_proxy);

  // The capport_proxy should be called normally after CAPPORT is enabled.
  EXPECT_CALL(*mock_capport_proxy, SendRequest).Times(1);
  network_monitor_->Start(NetworkMonitor::ValidationReason::kDBusRequest);
  task_environment_.RunUntilIdle();
  testing::Mock::VerifyAndClearExpectations(mock_capport_proxy);
}

TEST_F(NetworkMonitorTest, StartWithResultReturned) {
  const PortalDetector::Result result{
      .http_result = PortalDetector::ProbeResult::kSuccess,
      .http_status_code = 204,
      .http_content_length = 0,
      .https_result = PortalDetector::ProbeResult::kSuccess,
      .http_duration = base::Milliseconds(100),
      .https_duration = base::Milliseconds(200),
  };

  EXPECT_CALL(metrics_, SendToUMA(Metrics::kPortalDetectorHTTPProbeDuration,
                                  kTechnology, 100));
  EXPECT_CALL(metrics_, SendToUMA(Metrics::kPortalDetectorHTTPSProbeDuration,
                                  kTechnology, 200));
  EXPECT_CALL(metrics_,
              SendToUMA(Metrics::kPortalDetectorInternetValidationDuration,
                        kTechnology, 200));
  EXPECT_CALL(metrics_,
              SendSparseToUMA(Metrics::kPortalDetectorHTTPResponseCode,
                              kTechnology, 204));

  StartWithPortalDetectorResultReturned(/*expect_http_only=*/false, result);
}

TEST_F(NetworkMonitorTest, StartWithHTTPOnly) {
  const PortalDetector::Result result{
      .http_only = true,
      .http_result = PortalDetector::ProbeResult::kSuccess,
      .http_status_code = 204,
      .http_content_length = 0,
      .https_result = PortalDetector::ProbeResult::kNoResult,
      .http_duration = base::Milliseconds(100),
      .https_duration = base::Milliseconds(0),
  };

  EXPECT_CALL(metrics_, SendToUMA(Metrics::kPortalDetectorHTTPProbeDuration,
                                  kTechnology, 100));
  EXPECT_CALL(metrics_,
              SendToUMA(Metrics::kPortalDetectorHTTPSProbeDuration, _, _))
      .Times(0);
  EXPECT_CALL(metrics_,
              SendToUMA(Metrics::kPortalDetectorInternetValidationDuration,
                        kTechnology, 100));
  EXPECT_CALL(metrics_,
              SendSparseToUMA(Metrics::kPortalDetectorHTTPResponseCode,
                              kTechnology, 204));

  network_monitor_->SetValidationMode(
      NetworkMonitor::ValidationMode::kHTTPOnly);
  StartWithPortalDetectorResultReturned(/*expect_http_only=*/true, result);
}

TEST_F(NetworkMonitorTest, Stop) {
  MockCapportProxy* mock_capport_proxy = SetCapportProxy();

  EXPECT_CALL(*mock_capport_proxy, Stop).Times(1);
  EXPECT_CALL(*mock_portal_detector_, IsRunning).WillOnce(Return(true));
  EXPECT_CALL(*mock_portal_detector_, Reset).Times(1);

  EXPECT_TRUE(network_monitor_->Stop());
}

TEST_F(NetworkMonitorTest, IsRunning) {
  MockCapportProxy* mock_capport_proxy = SetCapportProxy();

  // Return true when either PortalDetector or CapportProxy is running.
  ON_CALL(*mock_capport_proxy, IsRunning).WillByDefault(Return(false));
  ON_CALL(*mock_portal_detector_, IsRunning).WillByDefault(Return(false));
  EXPECT_FALSE(network_monitor_->IsRunning());

  ON_CALL(*mock_capport_proxy, IsRunning).WillByDefault(Return(false));
  ON_CALL(*mock_portal_detector_, IsRunning).WillByDefault(Return(true));
  EXPECT_TRUE(network_monitor_->IsRunning());

  ON_CALL(*mock_capport_proxy, IsRunning).WillByDefault(Return(true));
  ON_CALL(*mock_portal_detector_, IsRunning).WillByDefault(Return(false));
  EXPECT_TRUE(network_monitor_->IsRunning());

  ON_CALL(*mock_capport_proxy, IsRunning).WillByDefault(Return(true));
  ON_CALL(*mock_portal_detector_, IsRunning).WillByDefault(Return(true));
  EXPECT_TRUE(network_monitor_->IsRunning());
}

TEST_F(NetworkMonitorTest, RetryWhenCapportTimeOver) {
  const base::TimeDelta seconds_remaining = base::Seconds(30);
  MockCapportProxy* mock_capport_proxy = SetCapportProxy();

  const CapportStatus capport_status{
      .is_captive = false,
      .user_portal_url = kUserPortalUrl,
      .seconds_remaining = seconds_remaining,
  };
  network_monitor_->OnCapportStatusReceivedForTesting(capport_status);

  // After receiving the CAPPORT status with seconds_remaining, NetworkMonitor
  // should query the CAPPORT server again after time is over.
  EXPECT_CALL(*mock_capport_proxy, SendRequest);
  task_environment_.FastForwardBy(seconds_remaining +
                                  NetworkMonitor::kCapportRemainingExtraDelay);
}

TEST_F(NetworkMonitorTest, MetricsWithPartialConnectivity) {
  const PortalDetector::Result result{
      .http_result = PortalDetector::ProbeResult::kSuccess,
      .http_status_code = 204,
      .http_content_length = 0,
      .https_result = PortalDetector::ProbeResult::kConnectionFailure,
      .http_duration = base::Milliseconds(100),
      .https_duration = base::Milliseconds(200),
  };
  ASSERT_EQ(PortalDetector::ValidationState::kNoConnectivity,
            result.GetValidationState());

  EXPECT_CALL(metrics_, SendToUMA(Metrics::kPortalDetectorHTTPProbeDuration,
                                  kTechnology, 100));
  EXPECT_CALL(metrics_, SendToUMA(Metrics::kPortalDetectorHTTPSProbeDuration,
                                  kTechnology, 200));
  EXPECT_CALL(metrics_,
              SendSparseToUMA(Metrics::kPortalDetectorHTTPResponseCode,
                              kTechnology, 204));
  EXPECT_CALL(
      metrics_,
      SendToUMA(Metrics::kPortalDetectorHTTPResponseContentLength, _, _))
      .Times(0);

  // ConnectionDiagnostics should be started when the result is kNoConnectivity.
  EXPECT_CALL(*mock_connection_diagnostics_factory_, Create).WillOnce([]() {
    auto mock_connection_diagnostics =
        std::make_unique<MockConnectionDiagnostics>();
    EXPECT_CALL(*mock_connection_diagnostics, Start).WillOnce(Return(true));
    return mock_connection_diagnostics;
  });

  StartWithPortalDetectorResultReturned(/*expect_http_only=*/false, result);
}

TEST_F(NetworkMonitorTest, MetricsWithNoConnectivity) {
  const PortalDetector::Result result{
      .http_result = PortalDetector::ProbeResult::kConnectionFailure,
      .https_result = PortalDetector::ProbeResult::kConnectionFailure,
      .http_duration = base::Milliseconds(0),
      .https_duration = base::Milliseconds(200),
  };
  ASSERT_EQ(PortalDetector::ValidationState::kNoConnectivity,
            result.GetValidationState());

  EXPECT_CALL(metrics_, SendToUMA(Metrics::kPortalDetectorHTTPSProbeDuration,
                                  kTechnology, 200));

  // ConnectionDiagnostics should be started when the result is kNoConnectivity.
  EXPECT_CALL(*mock_connection_diagnostics_factory_, Create).WillOnce([]() {
    auto mock_connection_diagnostics =
        std::make_unique<MockConnectionDiagnostics>();
    EXPECT_CALL(*mock_connection_diagnostics, Start).WillOnce(Return(true));
    return mock_connection_diagnostics;
  });

  StartWithPortalDetectorResultReturned(/*expect_http_only=*/false, result);
}

TEST_F(NetworkMonitorTest, MetricsWithInternetConnectivity) {
  const PortalDetector::Result result{
      .http_result = PortalDetector::ProbeResult::kSuccess,
      .http_status_code = 204,
      .http_content_length = 0,
      .https_result = PortalDetector::ProbeResult::kSuccess,
      .http_duration = base::Milliseconds(100),
      .https_duration = base::Milliseconds(200),
  };
  ASSERT_EQ(PortalDetector::ValidationState::kInternetConnectivity,
            result.GetValidationState());

  EXPECT_CALL(metrics_,
              SendToUMA(Metrics::kPortalDetectorInternetValidationDuration,
                        kTechnology, 200));
  EXPECT_CALL(metrics_, SendToUMA(Metrics::kPortalDetectorHTTPProbeDuration,
                                  kTechnology, 100));
  EXPECT_CALL(metrics_, SendToUMA(Metrics::kPortalDetectorHTTPSProbeDuration,
                                  kTechnology, 200));
  EXPECT_CALL(metrics_,
              SendSparseToUMA(Metrics::kPortalDetectorHTTPResponseCode,
                              kTechnology, 204));
  EXPECT_CALL(
      metrics_,
      SendToUMA(Metrics::kPortalDetectorHTTPResponseContentLength, _, _))
      .Times(0);

  StartWithPortalDetectorResultReturned(/*expect_http_only=*/false, result);
}

TEST_F(NetworkMonitorTest, MetricsWithPortalRedirect) {
  const PortalDetector::Result result{
      .http_result = PortalDetector::ProbeResult::kPortalRedirect,
      .http_status_code = 302,
      .http_content_length = 0,
      .redirect_url =
          net_base::HttpUrl::CreateFromString("https://portal.com/login"),
      .probe_url = net_base::HttpUrl::CreateFromString(
          "https://service.google.com/generate_204"),
      .http_duration = base::Milliseconds(100),
      .https_duration = base::Milliseconds(200),
  };
  ASSERT_EQ(PortalDetector::ValidationState::kPortalRedirect,
            result.GetValidationState());

  EXPECT_CALL(metrics_,
              SendToUMA(Metrics::kPortalDetectorPortalDiscoveryDuration,
                        kTechnology, 200));
  EXPECT_CALL(metrics_, SendToUMA(Metrics::kPortalDetectorHTTPProbeDuration,
                                  kTechnology, 100));
  EXPECT_CALL(metrics_, SendToUMA(Metrics::kPortalDetectorHTTPSProbeDuration,
                                  kTechnology, 200));
  EXPECT_CALL(metrics_,
              SendSparseToUMA(Metrics::kPortalDetectorHTTPResponseCode,
                              kTechnology, 302));
  EXPECT_CALL(
      metrics_,
      SendToUMA(Metrics::kPortalDetectorHTTPResponseContentLength, _, _))
      .Times(0);

  StartWithPortalDetectorResultReturned(/*expect_http_only=*/false, result);
}

TEST_F(NetworkMonitorTest, MetricsWithPortalInvalidRedirect) {
  const PortalDetector::Result result{
      .http_result = PortalDetector::ProbeResult::kPortalInvalidRedirect,
      .http_status_code = 302,
      .http_content_length = 0,
      .https_result = PortalDetector::ProbeResult::kConnectionFailure,
      .redirect_url = std::nullopt,
      .http_duration = base::Milliseconds(100),
      .https_duration = base::Milliseconds(200),
  };
  ASSERT_EQ(PortalDetector::ValidationState::kNoConnectivity,
            result.GetValidationState());

  EXPECT_CALL(metrics_, SendToUMA(Metrics::kPortalDetectorHTTPProbeDuration,
                                  Technology::kWiFi, 100));
  EXPECT_CALL(metrics_, SendToUMA(Metrics::kPortalDetectorHTTPSProbeDuration,
                                  Technology::kWiFi, 200));
  EXPECT_CALL(metrics_,
              SendSparseToUMA(
                  Metrics::kPortalDetectorHTTPResponseCode, Technology::kWiFi,
                  Metrics::kPortalDetectorHTTPResponseCodeIncompleteRedirect));

  // ConnectionDiagnostics should be started when the result is kNoConnectivity.
  EXPECT_CALL(*mock_connection_diagnostics_factory_, Create).WillOnce([]() {
    auto mock_connection_diagnostics =
        std::make_unique<MockConnectionDiagnostics>();
    EXPECT_CALL(*mock_connection_diagnostics, Start).WillOnce(Return(true));
    return mock_connection_diagnostics;
  });

  StartWithPortalDetectorResultReturned(/*expect_http_only=*/false, result);
}

TEST(NetworkMonitorResultTest, FromCapportStatusIsCaptive) {
  const CapportStatus status{
      .is_captive = true,
      .user_portal_url = kUserPortalUrl,
  };

  const NetworkMonitor::Result expected = {
      .origin = NetworkMonitor::ResultOrigin::kCapport,
      .num_attempts = kNumAttempts,
      .validation_state = PortalDetector::ValidationState::kPortalRedirect,
      .probe_result_metric = Metrics::kPortalDetectorResultRedirectFound,
      .target_url = kUserPortalUrl,
  };
  EXPECT_EQ(NetworkMonitor::Result::FromCapportStatus(status, kNumAttempts),
            expected);
}

TEST(NetworkMonitorResultTest, FromCapportStatusIsOpen) {
  const CapportStatus status = {
      .is_captive = false,
  };

  const NetworkMonitor::Result expected = {
      .origin = NetworkMonitor::ResultOrigin::kCapport,
      .num_attempts = kNumAttempts,
      .validation_state =
          PortalDetector::ValidationState::kInternetConnectivity,
      .probe_result_metric = Metrics::kPortalDetectorResultOnline,
      .target_url = std::nullopt,
  };
  EXPECT_EQ(NetworkMonitor::Result::FromCapportStatus(status, kNumAttempts),
            expected);
}

TEST_F(NetworkMonitorTest, IgnorePortalDetectorResult) {
  const CapportStatus capport_status = {
      .is_captive = false,
  };
  const PortalDetector::Result portal_result = {
      .http_result = PortalDetector::ProbeResult::kPortalRedirect,
      .http_status_code = 302,
      .http_content_length = 0,
      .redirect_url =
          net_base::HttpUrl::CreateFromString("https://portal.com/login"),
      .probe_url = net_base::HttpUrl::CreateFromString(
          "https://service.google.com/generate_204"),
      .http_duration = base::Milliseconds(100),
      .https_duration = base::Milliseconds(200),
  };

  // When CapportProxy sends the result prior than PortalDetector,
  // NetworkMonitor ignores the result from PortalDetector.
  ON_CALL(*mock_portal_detector_, attempt_count)
      .WillByDefault(Return(kNumAttempts));
  EXPECT_CALL(client_,
              OnNetworkMonitorResult(NetworkMonitor::Result::FromCapportStatus(
                  capport_status, kNumAttempts)))
      .Times(1);

  network_monitor_->OnCapportStatusReceivedForTesting(capport_status);
  network_monitor_->OnPortalDetectorResultForTesting(portal_result);
}

TEST_F(NetworkMonitorTest, SendBothResult) {
  const CapportStatus capport_status = {
      .is_captive = false,
  };
  const PortalDetector::Result portal_result = {
      .http_result = PortalDetector::ProbeResult::kPortalRedirect,
      .http_status_code = 302,
      .http_content_length = 0,
      .redirect_url =
          net_base::HttpUrl::CreateFromString("https://portal.com/login"),
      .probe_url = net_base::HttpUrl::CreateFromString(
          "https://service.google.com/generate_204"),
      .http_duration = base::Milliseconds(100),
      .https_duration = base::Milliseconds(200),
  };

  // When PortalDetector sends the result prior than CapportProxy,
  // NetworkMonitor sends both the results.
  ON_CALL(*mock_portal_detector_, attempt_count)
      .WillByDefault(Return(kNumAttempts));
  EXPECT_CALL(
      client_,
      OnNetworkMonitorResult(
          NetworkMonitor::Result::FromPortalDetectorResult(portal_result)))
      .Times(1);
  EXPECT_CALL(client_,
              OnNetworkMonitorResult(NetworkMonitor::Result::FromCapportStatus(
                  capport_status, kNumAttempts)))
      .Times(1);

  network_monitor_->OnPortalDetectorResultForTesting(portal_result);
  network_monitor_->OnCapportStatusReceivedForTesting(capport_status);
}

TEST_F(NetworkMonitorTest, SetCapportAPIWithDHCP) {
  EXPECT_CALL(*mock_validation_log_, SetCapportDHCPSupported);
  EXPECT_CALL(*mock_capport_proxy_factory_,
              Create(&metrics_, &patchpanel_client_, kInterface, kCapportAPI,
                     ElementsAreArray(kIPv4DnsList), _, _))
      .Times(1);
  network_monitor_->SetCapportURL(kCapportAPI, kIPv4DnsList,
                                  NetworkMonitor::CapportSource::kDHCP);
}

TEST_F(NetworkMonitorTest, SetCapportAPIWithRA) {
  EXPECT_CALL(*mock_validation_log_, SetCapportRASupported);
  EXPECT_CALL(*mock_capport_proxy_factory_,
              Create(&metrics_, &patchpanel_client_, kInterface, kCapportAPI,
                     ElementsAreArray(kIPv4DnsList), _, _))
      .Times(1);
  network_monitor_->SetCapportURL(kCapportAPI, kIPv4DnsList,
                                  NetworkMonitor::CapportSource::kRA);
}

TEST_F(NetworkMonitorTest, ConnectionDiagnosticsIsNotRestartedUntilFinished) {
  const PortalDetector::Result result{
      .http_result = PortalDetector::ProbeResult::kConnectionFailure,
      .https_result = PortalDetector::ProbeResult::kConnectionFailure,
      .http_duration = base::Milliseconds(0),
      .https_duration = base::Milliseconds(200),
  };
  ASSERT_EQ(PortalDetector::ValidationState::kNoConnectivity,
            result.GetValidationState());

  // ConnectionDiagnostics should be started when the result is kNoConnectivity.
  EXPECT_CALL(*mock_connection_diagnostics_factory_, Create).WillOnce([]() {
    auto mock_connection_diagnostics =
        std::make_unique<MockConnectionDiagnostics>();
    EXPECT_CALL(*mock_connection_diagnostics, Start).WillOnce(Return(true));
    ON_CALL(*mock_connection_diagnostics, IsRunning)
        .WillByDefault(Return(true));
    return mock_connection_diagnostics;
  });

  StartWithPortalDetectorResultReturned(/*expect_http_only=*/false, result);

  // A second network validation attempt does not retrigger a new
  // ConnectionDiagnostics if the previous one is still running.
  StartWithPortalDetectorResultReturned(/*expect_http_only=*/false, result);
}

TEST_F(NetworkMonitorTest, ConnectionDiagnosticsIsRestartedIfFinished) {
  const PortalDetector::Result result{
      .http_result = PortalDetector::ProbeResult::kConnectionFailure,
      .https_result = PortalDetector::ProbeResult::kConnectionFailure,
      .http_duration = base::Milliseconds(0),
      .https_duration = base::Milliseconds(200),
  };
  ASSERT_EQ(PortalDetector::ValidationState::kNoConnectivity,
            result.GetValidationState());

  // ConnectionDiagnostics should be started when the result is kNoConnectivity.
  EXPECT_CALL(*mock_connection_diagnostics_factory_, Create)
      .WillRepeatedly([]() {
        auto mock_connection_diagnostics =
            std::make_unique<MockConnectionDiagnostics>();
        EXPECT_CALL(*mock_connection_diagnostics, Start).WillOnce(Return(true));
        ON_CALL(*mock_connection_diagnostics, IsRunning)
            .WillByDefault(Return(false));
        return mock_connection_diagnostics;
      });

  StartWithPortalDetectorResultReturned(/*expect_http_only=*/false, result);

  // A second network validation attempt will retrigger a new
  // ConnectionDiagnostics if the previous one has finished.
  StartWithPortalDetectorResultReturned(/*expect_http_only=*/false, result);
}

TEST_F(NetworkMonitorTest, DualStackConnectionDiagnostics) {
  std::vector<net_base::IPAddress> dns;
  dns.insert(dns.end(), kIPv4DnsList.begin(), kIPv4DnsList.end());
  dns.insert(dns.end(), kIPv6DnsList.begin(), kIPv6DnsList.end());
  SetCurrentDualStackNetworkConfig(dns);

  const PortalDetector::Result result{
      .http_result = PortalDetector::ProbeResult::kConnectionFailure,
      .https_result = PortalDetector::ProbeResult::kConnectionFailure,
      .http_duration = base::Milliseconds(0),
      .https_duration = base::Milliseconds(200),
  };
  ASSERT_EQ(PortalDetector::ValidationState::kNoConnectivity,
            result.GetValidationState());

  // ConnectionDiagnostics should be started for both IPv4 and IPv6.
  EXPECT_CALL(*mock_connection_diagnostics_factory_,
              Create(kInterface, kInterfaceIndex, net_base::IPFamily::kIPv4,
                     net_base::IPAddress(kIPv4GatewayAddress), dns, _))
      .WillRepeatedly([]() {
        auto mock_connection_diagnostics =
            std::make_unique<MockConnectionDiagnostics>();
        EXPECT_CALL(*mock_connection_diagnostics, Start).WillOnce(Return(true));
        return mock_connection_diagnostics;
      });
  EXPECT_CALL(*mock_connection_diagnostics_factory_,
              Create(kInterface, kInterfaceIndex, net_base::IPFamily::kIPv6,
                     net_base::IPAddress(kIPv6GatewayAddress), dns, _))
      .WillRepeatedly([]() {
        auto mock_connection_diagnostics =
            std::make_unique<MockConnectionDiagnostics>();
        EXPECT_CALL(*mock_connection_diagnostics, Start).WillOnce(Return(true));
        return mock_connection_diagnostics;
      });

  StartWithPortalDetectorResultReturned(/*expect_http_only=*/false, result);
}

}  // namespace
}  // namespace shill
