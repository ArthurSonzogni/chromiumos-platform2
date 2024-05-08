// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/util/server_configuration_controller.h"

#include <base/functional/callback.h>
#include <base/memory/scoped_refptr.h>
#include <base/task/sequenced_task_runner.h>
#include <base/test/task_environment.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "missive/analytics/metrics.h"
#include "missive/analytics/metrics_test_util.h"
#include "missive/proto/record.pb.h"
#include "missive/proto/record_constants.pb.h"

using ::testing::_;
using ::testing::Eq;
using ::testing::Return;
using ::testing::StrEq;

namespace reporting {
namespace {

class ServerConfigurationControllerTest : public ::testing::Test {
 protected:
  ServerConfigurationControllerTest() = default;

  void SetUp() override {
    server_configuration_controller_ =
        ServerConfigurationController::Create(/*is_enabled=*/true);
  }

  void TearDown() override {
    task_environment_.RunUntilIdle();  // To make Metrics function be called.
  }

  // Expect UMA to be reported.
  void ExpectUMACalled(Destination destination, int times) const {
    EXPECT_CALL(
        analytics::Metrics::TestEnvironment::GetMockMetricsLibrary(),
        SendEnumToUMA(
            StrEq(ServerConfigurationController::kConfigFileRecordBlocked),
            Eq(destination), Eq(Destination_ARRAYSIZE)))
        .Times(times);
  }

  // Asserts that for all the destinations we are not blocking them and we are
  // not recording an UMA metric.
  void AssertListReturnsFalse() {
    for (int i = 0; i < Destination_ARRAYSIZE; ++i) {
      Destination current_destination = Destination(i);
      ASSERT_FALSE(server_configuration_controller_->IsDestinationBlocked(
          current_destination));
      ExpectUMACalled(current_destination, 0);
    }
  }

  scoped_refptr<ServerConfigurationController> server_configuration_controller_;
  base::test::TaskEnvironment task_environment_;  // needed by Metrics mock.
  // Replace the metrics library instance with a mock one.
  analytics::Metrics::TestEnvironment metrics_test_environment_;
};

TEST_F(ServerConfigurationControllerTest, EmptyListAtCreationTime) {
  AssertListReturnsFalse();
}

TEST_F(ServerConfigurationControllerTest, EmptyListAfterUpdatedConfigReceived) {
  server_configuration_controller_->UpdateConfiguration(
      ListOfBlockedDestinations(), HealthModule::Recorder(nullptr));

  AssertListReturnsFalse();
}

TEST_F(ServerConfigurationControllerTest, SingleDestinationBlocked) {
  // Create list of blocked destinations and update it.
  ListOfBlockedDestinations blocked_list;
  blocked_list.add_destinations(LOCK_UNLOCK_EVENTS);
  server_configuration_controller_->UpdateConfiguration(
      blocked_list, HealthModule::Recorder(nullptr));

  // Assert that the destination was blocked and the UMA metric is recorded.
  ASSERT_TRUE(server_configuration_controller_->IsDestinationBlocked(
      LOCK_UNLOCK_EVENTS));
  ExpectUMACalled(LOCK_UNLOCK_EVENTS, 1);
}

TEST_F(ServerConfigurationControllerTest, MultipleDestinationsBlocked) {
  // Create list of blocked destinations and update it.
  ListOfBlockedDestinations blocked_list;
  blocked_list.add_destinations(OS_EVENTS);
  blocked_list.add_destinations(CROS_SECURITY_PROCESS);
  blocked_list.add_destinations(LOG_UPLOAD);
  server_configuration_controller_->UpdateConfiguration(
      blocked_list, HealthModule::Recorder(nullptr));

  // Assert that the destinations were blocked and the UMA metrics are recorded.
  ASSERT_TRUE(
      server_configuration_controller_->IsDestinationBlocked(OS_EVENTS));
  ASSERT_TRUE(server_configuration_controller_->IsDestinationBlocked(
      CROS_SECURITY_PROCESS));
  ASSERT_TRUE(
      server_configuration_controller_->IsDestinationBlocked(LOG_UPLOAD));

  ExpectUMACalled(OS_EVENTS, 1);
  ExpectUMACalled(CROS_SECURITY_PROCESS, 1);
  ExpectUMACalled(LOG_UPLOAD, 1);
}

TEST_F(ServerConfigurationControllerTest, SingleDestinationNotBlocked) {
  // Create list of blocked destinations and update it.
  ListOfBlockedDestinations blocked_list;
  blocked_list.add_destinations(LOCK_UNLOCK_EVENTS);
  server_configuration_controller_->UpdateConfiguration(
      blocked_list, HealthModule::Recorder(nullptr));

  // Assert that the destination was not blocked and no UMA metric was recorded.
  ASSERT_FALSE(
      server_configuration_controller_->IsDestinationBlocked(UPLOAD_EVENTS));
  ExpectUMACalled(CROS_SECURITY_PROCESS, 0);
}

TEST_F(ServerConfigurationControllerTest, MultipleDestinationsNotBlocked) {
  // Create list of blocked destinations and update it.
  ListOfBlockedDestinations blocked_list;
  blocked_list.add_destinations(OS_EVENTS);
  blocked_list.add_destinations(CROS_SECURITY_PROCESS);
  blocked_list.add_destinations(LOG_UPLOAD);
  server_configuration_controller_->UpdateConfiguration(
      blocked_list, HealthModule::Recorder(nullptr));

  // Assert that the destinations were not blocked and no UMA metrics were
  // recorded.
  ASSERT_FALSE(
      server_configuration_controller_->IsDestinationBlocked(LEGACY_TECH));
  ASSERT_FALSE(server_configuration_controller_->IsDestinationBlocked(
      LOGIN_LOGOUT_EVENTS));
  ASSERT_FALSE(
      server_configuration_controller_->IsDestinationBlocked(TELEMETRY_METRIC));

  ExpectUMACalled(LEGACY_TECH, 0);
  ExpectUMACalled(LOGIN_LOGOUT_EVENTS, 0);
  ExpectUMACalled(TELEMETRY_METRIC, 0);
}

TEST_F(ServerConfigurationControllerTest, MultipleDestinationsMixed) {
  // Create list of blocked destinations and update it.
  ListOfBlockedDestinations blocked_list;
  blocked_list.add_destinations(CRD_EVENTS);
  blocked_list.add_destinations(CROS_SECURITY_NETWORK);
  server_configuration_controller_->UpdateConfiguration(
      blocked_list, HealthModule::Recorder(nullptr));

  // Assert that the destinations were not blocked and no UMA metrics were
  // recorded.
  ASSERT_FALSE(
      server_configuration_controller_->IsDestinationBlocked(OS_EVENTS));
  ASSERT_FALSE(server_configuration_controller_->IsDestinationBlocked(
      PERIPHERAL_EVENTS));
  ASSERT_FALSE(server_configuration_controller_->IsDestinationBlocked(
      EXTENSION_INSTALL));

  ExpectUMACalled(OS_EVENTS, 0);
  ExpectUMACalled(PERIPHERAL_EVENTS, 0);
  ExpectUMACalled(EXTENSION_INSTALL, 0);

  // Assert that the destinations were blocked and UMA metrics were recorded.
  ASSERT_TRUE(
      server_configuration_controller_->IsDestinationBlocked(CRD_EVENTS));
  ASSERT_TRUE(server_configuration_controller_->IsDestinationBlocked(
      CROS_SECURITY_NETWORK));

  ExpectUMACalled(CRD_EVENTS, 1);
  ExpectUMACalled(CROS_SECURITY_NETWORK, 1);
}

TEST_F(ServerConfigurationControllerTest,
       AlwaysNotBlockedWhenExperimentDisabled) {
  // Create an object with the flag disabled.
  server_configuration_controller_ =
      ServerConfigurationController::Create(/*is_enabled=*/
                                            false);

  AssertListReturnsFalse();

  // Create list of blocked destinations and update it.
  ListOfBlockedDestinations blocked_list;
  blocked_list.add_destinations(OS_EVENTS);
  blocked_list.add_destinations(CROS_SECURITY_PROCESS);
  blocked_list.add_destinations(LOG_UPLOAD);
  server_configuration_controller_->UpdateConfiguration(
      blocked_list, HealthModule::Recorder(nullptr));

  AssertListReturnsFalse();
}

}  // namespace
}  // namespace reporting
