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
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <net-base/http_url.h>
#include <net-base/ip_address.h>
#include <net-base/ipv4_address.h>
#include <net-base/network_config.h>

#include "shill/event_dispatcher.h"
#include "shill/mock_metrics.h"
#include "shill/mock_portal_detector.h"
#include "shill/network/mock_connection_diagnostics.h"
#include "shill/network/mock_validation_log.h"
#include "shill/portal_detector.h"
#include "shill/technology.h"

namespace shill {
namespace {

const std::vector<net_base::IPAddress> kDnsList = {
    net_base::IPAddress(net_base::IPv4Address(8, 8, 8, 8)),
    net_base::IPAddress(net_base::IPv4Address(8, 8, 4, 4)),
};
constexpr int kInterfaceIndex = 1;
constexpr std::string_view kInterface = "wlan1";
constexpr std::string_view kLoggingTag = "logging_tag";
constexpr Technology kTechnology = Technology::kWiFi;
const net_base::HttpUrl kCapportAPI =
    *net_base::HttpUrl::CreateFromString("https://example.org/api");

using ::testing::_;
using ::testing::Eq;
using ::testing::Return;
using ::testing::StrictMock;
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
    auto mock_portal_detector_factory =
        std::make_unique<MockPortalDetectorFactory>();
    mock_portal_detector_factory_ = mock_portal_detector_factory.get();

    auto mock_validation_log = std::make_unique<MockValidationLog>();
    mock_validation_log_ = mock_validation_log.get();
    EXPECT_CALL(*mock_validation_log_, RecordMetrics).Times(1);

    auto mock_connection_diagnostics_factory =
        std::make_unique<MockConnectionDiagnosticsFactory>();
    mock_connection_diagnostics_factory_ =
        mock_connection_diagnostics_factory.get();

    network_monitor_ = std::make_unique<NetworkMonitor>(
        &dispatcher_, &metrics_, &client_, kTechnology, kInterfaceIndex,
        kInterface, probing_configuration_, std::move(mock_validation_log),
        kLoggingTag, std::move(mock_portal_detector_factory),
        std::move(mock_connection_diagnostics_factory));

    SetCurrentNetworkConfig(net_base::IPFamily::kIPv4, kDnsList);
  }

  void SetCurrentNetworkConfig(net_base::IPFamily ip_family,
                               std::vector<net_base::IPAddress> dns_servers) {
    switch (ip_family) {
      case net_base::IPFamily::kIPv4:
        config_.ipv4_address =
            *net_base::IPv4CIDR::CreateFromCIDRString("192.168.1.2/24");
        config_.ipv4_gateway =
            *net_base::IPv4Address::CreateFromString("192.168.1.1");
        break;
      case net_base::IPFamily::kIPv6:
        config_.ipv6_addresses.push_back(
            *net_base::IPv6CIDR::CreateFromCIDRString("fd00::2/64"));
        config_.ipv6_gateway =
            *net_base::IPv6Address::CreateFromString("fd00::1");
        break;
    }
    config_.dns_servers = std::move(dns_servers);

    ON_CALL(client_, GetCurrentConfig)
        .WillByDefault(testing::ReturnRef(config_));
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
      const PortalDetector::Result& result) {
    EXPECT_CALL(*mock_portal_detector_factory_,
                Create(&dispatcher_, probing_configuration_, _))
        .WillOnce(WithArg<2>([&](base::RepeatingCallback<void(
                                     const PortalDetector::Result&)> callback) {
          auto portal_detector = std::make_unique<MockPortalDetector>();
          EXPECT_CALL(*portal_detector,
                      Start(Eq(kInterface), net_base::IPFamily::kIPv4, kDnsList,
                            Eq(kLoggingTag)))
              .WillOnce([&, callback = std::move(callback)]() {
                dispatcher_.PostTask(FROM_HERE,
                                     base::BindOnce(callback, result));
              });
          return portal_detector;
        }));
    EXPECT_CALL(*mock_validation_log_, AddResult(result));
    EXPECT_CALL(client_,
                OnNetworkMonitorResult(
                    NetworkMonitor::Result::FromPortalDetectorResult(result)))
        .Times(1);

    StartAndExpectResult(NetworkMonitor::ValidationReason::kDBusRequest, true);
  }

 protected:
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};

  EventDispatcher dispatcher_;
  StrictMock<MockMetrics> metrics_;
  PortalDetector::ProbingConfiguration probing_configuration_;

  net_base::NetworkConfig config_;
  MockClient client_;
  std::unique_ptr<NetworkMonitor> network_monitor_;
  MockPortalDetectorFactory*
      mock_portal_detector_factory_;  // Owned by |network_monitor_|.
  MockConnectionDiagnosticsFactory*
      mock_connection_diagnostics_factory_;  // Owned by |network_monitor_|.
  MockValidationLog* mock_validation_log_;   // Owned by |network_monitor_|.
};

TEST_F(NetworkMonitorTest, StartWithImmediatelyTrigger) {
  // These reason trigger the legacy probe immediately.
  const NetworkMonitor::ValidationReason reasons[] = {
      NetworkMonitor::ValidationReason::kDBusRequest,
      NetworkMonitor::ValidationReason::kEthernetGatewayReachable,
      NetworkMonitor::ValidationReason::kNetworkConnectionUpdate,
      NetworkMonitor::ValidationReason::kServiceReorder,
  };

  for (const auto reason : reasons) {
    EXPECT_CALL(*mock_portal_detector_factory_,
                Create(&dispatcher_, probing_configuration_, _))
        .WillOnce([]() {
          auto portal_detector = std::make_unique<MockPortalDetector>();
          EXPECT_CALL(*portal_detector,
                      Start(Eq(kInterface), net_base::IPFamily::kIPv4, kDnsList,
                            Eq(kLoggingTag)))
              .Times(1);
          return portal_detector;
        });

    StartAndExpectResult(reason, true);
    network_monitor_->Stop();
  }
}

TEST_F(NetworkMonitorTest, StartWithoutDNS) {
  SetCurrentNetworkConfig(net_base::IPFamily::kIPv4, {});

  StartAndExpectResult(NetworkMonitor::ValidationReason::kDBusRequest, false);
}

TEST_F(NetworkMonitorTest, StartWithResetPortalDetector) {
  // These reasons reset the running portal detector.
  const NetworkMonitor::ValidationReason reasons[] = {
      NetworkMonitor::ValidationReason::kNetworkConnectionUpdate};

  auto portal_detector = std::make_unique<MockPortalDetector>();
  network_monitor_->set_portal_detector_for_testing(std::move(portal_detector));

  for (const auto reason : reasons) {
    EXPECT_CALL(*mock_portal_detector_factory_,
                Create(&dispatcher_, probing_configuration_, _))
        .WillOnce([]() {
          auto portal_detector = std::make_unique<MockPortalDetector>();
          EXPECT_CALL(*portal_detector,
                      Start(Eq(kInterface), net_base::IPFamily::kIPv4, kDnsList,
                            Eq(kLoggingTag)))
              .Times(1);
          return portal_detector;
        });

    StartAndExpectResult(reason, true);
  }
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

  StartWithPortalDetectorResultReturned(result);
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

  StartWithPortalDetectorResultReturned(result);
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

  StartWithPortalDetectorResultReturned(result);
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

  StartWithPortalDetectorResultReturned(result);
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

  StartWithPortalDetectorResultReturned(result);
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
  ASSERT_EQ(PortalDetector::ValidationState::kPortalSuspected,
            result.GetValidationState());

  EXPECT_CALL(metrics_, SendToUMA(Metrics::kPortalDetectorHTTPProbeDuration,
                                  Technology::kWiFi, 100));
  EXPECT_CALL(metrics_, SendToUMA(Metrics::kPortalDetectorHTTPSProbeDuration,
                                  Technology::kWiFi, 200));
  EXPECT_CALL(metrics_,
              SendSparseToUMA(
                  Metrics::kPortalDetectorHTTPResponseCode, Technology::kWiFi,
                  Metrics::kPortalDetectorHTTPResponseCodeIncompleteRedirect));
  EXPECT_CALL(
      metrics_,
      SendToUMA(Metrics::kPortalDetectorHTTPResponseContentLength, _, _))
      .Times(0);

  StartWithPortalDetectorResultReturned(result);
}

TEST_F(NetworkMonitorTest, SetCapportAPIWithDHCP) {
  EXPECT_CALL(*mock_validation_log_, SetCapportDHCPSupported);
  network_monitor_->SetCapportAPI(kCapportAPI,
                                  NetworkMonitor::CapportSource::kDHCP);
}

TEST_F(NetworkMonitorTest, SetCapportAPIWithRA) {
  EXPECT_CALL(*mock_validation_log_, SetCapportRASupported);
  network_monitor_->SetCapportAPI(kCapportAPI,
                                  NetworkMonitor::CapportSource::kRA);
}

}  // namespace
}  // namespace shill
