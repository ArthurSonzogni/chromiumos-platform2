// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "federated/device_status_monitor.h"

namespace federated {
namespace {

using ::testing::Return;
using ::testing::StrictMock;
using ::testing::Test;

class MockTrainingCondition : public TrainingCondition {
 public:
  MockTrainingCondition() = default;
  ~MockTrainingCondition() override = default;
  MOCK_METHOD(bool, IsTrainingConditionSatisfiedToStart, (), (const, override));
  MOCK_METHOD(bool,
              IsTrainingConditionSatisfiedToContinue,
              (),
              (const, override));
};
}  // namespace

class DeviceStatusMonitorTest : public Test {
 public:
  DeviceStatusMonitorTest() = default;
  DeviceStatusMonitorTest(const DeviceStatusMonitorTest&) = delete;
  DeviceStatusMonitorTest& operator=(const DeviceStatusMonitorTest&) = delete;
};

TEST_F(DeviceStatusMonitorTest, OneTrainingCondition) {
  auto training_condition1 =
      std::make_unique<StrictMock<MockTrainingCondition>>();
  auto training_condition_1 = training_condition1.get();

  std::vector<std::unique_ptr<TrainingCondition>> training_conditions;
  training_conditions.push_back(std::move(training_condition1));
  auto device_status_monitor =
      DeviceStatusMonitor(std::move(training_conditions));

  EXPECT_CALL(*training_condition_1, IsTrainingConditionSatisfiedToStart())
      .WillOnce(Return(false));
  EXPECT_FALSE(device_status_monitor.TrainingConditionsSatisfiedToStart());

  EXPECT_CALL(*training_condition_1, IsTrainingConditionSatisfiedToStart())
      .WillOnce(Return(true));
  EXPECT_TRUE(device_status_monitor.TrainingConditionsSatisfiedToStart());
}

TEST_F(DeviceStatusMonitorTest, TwoTrainingConditions) {
  auto training_condition1 =
      std::make_unique<StrictMock<MockTrainingCondition>>();
  auto training_condition_1 = training_condition1.get();

  auto training_condition2 =
      std::make_unique<StrictMock<MockTrainingCondition>>();
  auto training_condition_2 = training_condition2.get();

  std::vector<std::unique_ptr<TrainingCondition>> training_conditions;
  training_conditions.push_back(std::move(training_condition1));
  training_conditions.push_back(std::move(training_condition2));
  auto device_status_monitor =
      DeviceStatusMonitor(std::move(training_conditions));

  // false, any -> false
  EXPECT_CALL(*training_condition_1, IsTrainingConditionSatisfiedToContinue())
      .WillOnce(Return(false));
  // Expect tc2 not to be called
  EXPECT_FALSE(device_status_monitor.TrainingConditionsSatisfiedToContinue());

  // true, false -> false
  EXPECT_CALL(*training_condition_1, IsTrainingConditionSatisfiedToContinue())
      .WillOnce(Return(true));
  EXPECT_CALL(*training_condition_2, IsTrainingConditionSatisfiedToContinue())
      .WillOnce(Return(false));
  EXPECT_FALSE(device_status_monitor.TrainingConditionsSatisfiedToContinue());

  // true, true -> true
  EXPECT_CALL(*training_condition_1, IsTrainingConditionSatisfiedToContinue())
      .WillOnce(Return(true));
  EXPECT_CALL(*training_condition_2, IsTrainingConditionSatisfiedToContinue())
      .WillOnce(Return(true));
  EXPECT_TRUE(device_status_monitor.TrainingConditionsSatisfiedToContinue());
}
}  // namespace federated
