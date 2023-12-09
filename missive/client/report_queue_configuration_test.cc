// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/client/report_queue_configuration.h"

#include <utility>

#include <base/functional/bind.h>
#include <base/functional/callback.h>
#include <base/test/task_environment.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "missive/analytics/metrics.h"
#include "missive/analytics/metrics_test_util.h"
#include "missive/proto/record_constants.pb.h"
#include "missive/util/status.h"
#include "missive/util/status_macros.h"
#include "missive/util/statusor.h"

using ::testing::_;
using ::testing::Eq;
using ::testing::Invoke;
using ::testing::Return;
using ::testing::WithArgs;

namespace reporting {
namespace {

constexpr char kDmToken[] = "dm_token";

class ReportQueueConfigurationTest : public ::testing::Test {
 protected:
  using PolicyCheckCallback = ReportQueueConfiguration::PolicyCheckCallback;

  const Destination kInvalidDestination = Destination::UNDEFINED_DESTINATION;
  const Destination kValidDestination = Destination::UPLOAD_EVENTS;
  const PolicyCheckCallback kValidCallback = GetSuccessfulCallback();
  const PolicyCheckCallback kInvalidCallback = GetInvalidCallback();

  static PolicyCheckCallback GetSuccessfulCallback() {
    return base::BindRepeating([]() { return Status::StatusOK(); });
  }

  static PolicyCheckCallback GetInvalidCallback() {
    return base::RepeatingCallback<Status(void)>();
  }

  ReportQueueConfigurationTest() = default;

  base::test::TaskEnvironment task_environment_;
  // Replace the metrics library instance with a mock one
  analytics::Metrics::TestEnvironment metrics_test_environment_;
};

// Tests to ensure that only valid parameters are used to generate a
// ReportQueueConfiguration.
TEST_F(ReportQueueConfigurationTest,
       ValidateConfigurationWithInvalidDestination) {
  EXPECT_FALSE(ReportQueueConfiguration::Create(kDmToken, kInvalidDestination,
                                                kValidCallback)
                   .has_value());
}

TEST_F(ReportQueueConfigurationTest,
       ValidateConfigurationWithInvalidDestinationInvalidCallback) {
  EXPECT_FALSE(ReportQueueConfiguration::Create(
                   /*dm_token=*/kDmToken, kInvalidDestination, kInvalidCallback)
                   .has_value());
}

TEST_F(ReportQueueConfigurationTest, ValidateConfigurationWithValidParams) {
  EXPECT_OK(ReportQueueConfiguration::Create(
      /*dm_token*=*/kDmToken, kValidDestination, kValidCallback));
}

TEST_F(ReportQueueConfigurationTest, ValidateConfigurationWithNoDMToken) {
  EXPECT_OK(ReportQueueConfiguration::Create(
      /*dm_token*=*/"", kValidDestination, kValidCallback));
}

TEST_F(ReportQueueConfigurationTest,
       ValidateConfigurationWithNoDMTokenInvalidDestination) {
  EXPECT_FALSE(ReportQueueConfiguration::Create(
                   /*dm_token*=*/"", kInvalidDestination, kValidCallback)
                   .has_value());
}

TEST_F(ReportQueueConfigurationTest,
       ValidateConfigurationWithNoDMTokenInvalidCallback) {
  EXPECT_FALSE(ReportQueueConfiguration::Create(
                   /*dm_token=*/"", kValidDestination, kInvalidCallback)
                   .has_value());
}

TEST_F(ReportQueueConfigurationTest,
       ValidateConfigurationWithNoDMTokenInvalidDestinationInvalidCallback) {
  EXPECT_FALSE(ReportQueueConfiguration::Create(
                   /*dm_token*=*/"", kInvalidDestination, kInvalidCallback)
                   .has_value());
}

TEST_F(ReportQueueConfigurationTest, ValidateConfigurationWithDeviceEventType) {
  EXPECT_OK(ReportQueueConfiguration::Create(
      EventType::kDevice, kValidDestination, kValidCallback));
}

TEST_F(ReportQueueConfigurationTest, ValidateConfigurationWithUserEventType) {
  EXPECT_OK(ReportQueueConfiguration::Create(
      EventType::kUser, kValidDestination, kValidCallback));
}

TEST_F(ReportQueueConfigurationTest,
       ValidateConfigurationWithEventTypeInvalidDestination) {
  EXPECT_FALSE(ReportQueueConfiguration::Create(
                   EventType::kDevice, kInvalidDestination, kValidCallback)
                   .has_value());
}

TEST_F(ReportQueueConfigurationTest,
       ValidateConfigurationWithEventTypeInvalidCallback) {
  EXPECT_FALSE(ReportQueueConfiguration::Create(
                   EventType::kDevice, kValidDestination, kInvalidCallback)
                   .has_value());
}

TEST_F(ReportQueueConfigurationTest,
       ValidateConfigurationWithEventTypeInvalidReservedSpace) {
  EXPECT_FALSE(ReportQueueConfiguration::Create(
                   EventType::kDevice, kValidDestination, kValidCallback,
                   /*reserved_space=*/-1L)
                   .has_value());
}

TEST_F(ReportQueueConfigurationTest, UsesProvidedPolicyCheckCallback) {
  const Destination destination = Destination::UPLOAD_EVENTS;

  testing::MockFunction<Status(void)> mock_handler;
  EXPECT_CALL(mock_handler, Call()).WillOnce(Return(Status::StatusOK()));

  auto config_result = ReportQueueConfiguration::Create(
      kDmToken, destination,
      base::BindRepeating(&::testing::MockFunction<Status(void)>::Call,
                          base::Unretained(&mock_handler)));
  EXPECT_OK(config_result);

  auto config = std::move(config_result.value());
  EXPECT_OK(config->CheckPolicy());
  EXPECT_THAT(config->reserved_space(), Eq(0L));
}

TEST_F(ReportQueueConfigurationTest,
       ValidateConfigurationWithReservedSpaceSetting) {
  static constexpr int64_t kReservedSpace = 12345L;
  auto config_result = ReportQueueConfiguration::Create(
      EventType::kDevice, kValidDestination, kValidCallback, kReservedSpace);
  EXPECT_OK(config_result) << config_result.status();

  auto config = std::move(config_result.value());
  EXPECT_THAT(config->reserved_space(), Eq(kReservedSpace));
}
}  // namespace
}  // namespace reporting
