// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/network_monitor.h"

#include <memory>
#include <utility>
#include <vector>

#include <base/functional/bind.h>
#include <base/test/task_environment.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <net-base/ipv4_address.h>

#include "shill/event_dispatcher.h"
#include "shill/mock_portal_detector.h"
#include "shill/portal_detector.h"

namespace shill {
namespace {

const std::vector<net_base::IPAddress> kDnsList = {
    net_base::IPAddress(net_base::IPv4Address(8, 8, 8, 8)),
    net_base::IPAddress(net_base::IPv4Address(8, 8, 4, 4)),
};
constexpr std::string_view kInterface = "wlan1";
constexpr std::string_view kLoggingTag = "logging_tag";

using ::testing::_;
using ::testing::Eq;
using ::testing::Return;
using ::testing::WithArg;

class MockClient {
 public:
  MOCK_METHOD(void,
              OnNetworkMonitorResult,
              (const NetworkMonitor::Result&),
              ());
};

class NetworkMonitorTest : public ::testing::Test {
 public:
  NetworkMonitorTest() {
    auto mock_portal_detector_factory =
        std::make_unique<MockPortalDetectorFactory>();
    mock_portal_detector_factory_ = mock_portal_detector_factory.get();

    network_monitor_ = std::make_unique<NetworkMonitor>(
        &dispatcher_, kInterface, probing_configuration_,
        base::BindRepeating(&MockClient::OnNetworkMonitorResult,
                            base::Unretained(&client_)),
        kLoggingTag, std::move(mock_portal_detector_factory));
  }

 protected:
  base::test::TaskEnvironment task_environment_;

  EventDispatcher dispatcher_;
  PortalDetector::ProbingConfiguration probing_configuration_;

  MockClient client_;
  std::unique_ptr<NetworkMonitor> network_monitor_;
  MockPortalDetectorFactory*
      mock_portal_detector_factory_;  // Owned by |network_monitor_|.
};

TEST_F(NetworkMonitorTest, StartWithImmediatelyTrigger) {
  // These reason trigger the legacy probe immediately.
  const NetworkMonitor::ValidationReason reasons[] = {
      NetworkMonitor::ValidationReason::kDBusRequest,
      NetworkMonitor::ValidationReason::kEthernetGatewayReachable,
      NetworkMonitor::ValidationReason::kNetworkConnectionUpdate,
      NetworkMonitor::ValidationReason::kServiceReorder,
  };

  EXPECT_CALL(*mock_portal_detector_factory_,
              Create(&dispatcher_, probing_configuration_, _))
      .WillRepeatedly([]() {
        auto portal_detector = std::make_unique<MockPortalDetector>();
        EXPECT_CALL(*portal_detector, IsInProgress).WillOnce(Return(false));
        EXPECT_CALL(*portal_detector, ResetAttemptDelays).Times(1);
        EXPECT_CALL(*portal_detector,
                    Start(Eq(kInterface), net_base::IPFamily::kIPv4, kDnsList,
                          Eq(kLoggingTag)))
            .Times(1);
        return portal_detector;
      });

  for (const auto reason : reasons) {
    EXPECT_TRUE(
        network_monitor_->Start(reason, net_base::IPFamily::kIPv4, kDnsList));
    network_monitor_->Stop();
  }
}

TEST_F(NetworkMonitorTest, StartWithoutDNS) {
  EXPECT_FALSE(
      network_monitor_->Start(NetworkMonitor::ValidationReason::kDBusRequest,
                              net_base::IPFamily::kIPv4, {}));
}

TEST_F(NetworkMonitorTest, StartWithoutResetPortalDetector) {
  // When the previous probing is running, these reasons don't trigger the
  // probing again.
  const NetworkMonitor::ValidationReason reasons[] = {
      NetworkMonitor::ValidationReason::kEthernetGatewayUnreachable,
      NetworkMonitor::ValidationReason::kManagerPropertyUpdate,
      NetworkMonitor::ValidationReason::kServicePropertyUpdate};

  auto portal_detector = std::make_unique<MockPortalDetector>();
  EXPECT_CALL(*portal_detector, IsInProgress).WillRepeatedly(Return(true));
  EXPECT_CALL(*portal_detector, Start).Times(0);
  EXPECT_CALL(*portal_detector, ResetAttemptDelays).Times(0);
  network_monitor_->set_portal_detector_for_testing(std::move(portal_detector));

  for (const auto reason : reasons) {
    EXPECT_TRUE(
        network_monitor_->Start(reason, net_base::IPFamily::kIPv4, kDnsList));
  }
}

TEST_F(NetworkMonitorTest, StartWithResetPortalDetector) {
  // These reasons reset the running portal detector.
  const NetworkMonitor::ValidationReason reasons[] = {
      NetworkMonitor::ValidationReason::kNetworkConnectionUpdate};

  auto portal_detector = std::make_unique<MockPortalDetector>();
  network_monitor_->set_portal_detector_for_testing(std::move(portal_detector));

  EXPECT_CALL(*mock_portal_detector_factory_,
              Create(&dispatcher_, probing_configuration_, _))
      .WillRepeatedly([]() {
        auto portal_detector = std::make_unique<MockPortalDetector>();
        EXPECT_CALL(*portal_detector, IsInProgress).WillOnce(Return(false));
        EXPECT_CALL(*portal_detector, ResetAttemptDelays).Times(1);
        EXPECT_CALL(*portal_detector,
                    Start(Eq(kInterface), net_base::IPFamily::kIPv4, kDnsList,
                          Eq(kLoggingTag)))
            .Times(1);
        return portal_detector;
      });

  for (const auto reason : reasons) {
    EXPECT_TRUE(
        network_monitor_->Start(reason, net_base::IPFamily::kIPv4, kDnsList));
  }
}

TEST_F(NetworkMonitorTest, StartWithResultReturned) {
  const PortalDetector::Result result{
      .http_result = PortalDetector::ProbeResult::kSuccess,
      .http_status_code = 204,
      .http_content_length = 0,
      .https_result = PortalDetector::ProbeResult::kHTTPTimeout,
  };

  EXPECT_CALL(*mock_portal_detector_factory_,
              Create(&dispatcher_, probing_configuration_, _))
      .WillRepeatedly(WithArg<2>(
          [&](base::RepeatingCallback<void(const PortalDetector::Result&)>
                  callback) {
            auto portal_detector = std::make_unique<MockPortalDetector>();
            EXPECT_CALL(*portal_detector, IsInProgress).WillOnce(Return(false));
            EXPECT_CALL(*portal_detector, ResetAttemptDelays).Times(1);
            EXPECT_CALL(*portal_detector,
                        Start(Eq(kInterface), net_base::IPFamily::kIPv4,
                              kDnsList, Eq(kLoggingTag)))
                .WillOnce([&, callback = std::move(callback)]() {
                  dispatcher_.PostTask(FROM_HERE,
                                       base::BindOnce(callback, result));
                });
            return portal_detector;
          }));
  EXPECT_CALL(client_, OnNetworkMonitorResult(result)).Times(1);

  EXPECT_TRUE(
      network_monitor_->Start(NetworkMonitor::ValidationReason::kDBusRequest,
                              net_base::IPFamily::kIPv4, kDnsList));
  task_environment_.RunUntilIdle();
}

}  // namespace
}  // namespace shill
