// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modemfwd/heartbeat_task.h"

#include <memory>
#include <utility>

#include <base/run_loop.h>
#include <base/test/task_environment.h>
#include <base/time/time.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "modemfwd/mock_daemon_delegate.h"
#include "modemfwd/mock_metrics.h"
#include "modemfwd/mock_modem.h"

using testing::_;
using testing::InSequence;
using testing::Return;

namespace {
constexpr char kModemDeviceId[] = "modem_device_id";
}  // namespace

namespace modemfwd {

class HeartbeatTaskTest : public ::testing::Test {
 public:
  HeartbeatTaskTest() {
    delegate_ = std::make_unique<MockDelegate>();
    modem_ = std::make_unique<MockModem>();
    metrics_ = std::make_unique<MockMetrics>();

    ON_CALL(*modem_, GetDeviceId()).WillByDefault(Return(kModemDeviceId));
  }

 protected:
  std::unique_ptr<HeartbeatTask> GetTask(HeartbeatConfig config) {
    return std::make_unique<HeartbeatTask>(delegate_.get(), modem_.get(),
                                           metrics_.get(), config);
  }

  void RunFor(base::TimeDelta time) {
    task_environment_.GetMainThreadTaskRunner()->PostDelayedTask(
        FROM_HERE, task_environment_.QuitClosure(), time);
    task_environment_.RunUntilQuit();
  }

  // Must be the first member.
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};

  std::unique_ptr<MockDelegate> delegate_;
  std::unique_ptr<MockModem> modem_;
  std::unique_ptr<MockMetrics> metrics_;
};

TEST_F(HeartbeatTaskTest, HeartbeatSuccess) {
  constexpr base::TimeDelta kInterval = base::Seconds(10);
  constexpr int kChecks = 5;

  EXPECT_CALL(*modem_, SupportsHealthCheck()).WillRepeatedly(Return(true));
  EXPECT_CALL(*modem_, CheckHealth())
      .Times(kChecks)
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*delegate_, ResetModem(kModemDeviceId)).Times(0);

  auto task = GetTask(HeartbeatConfig{3, kInterval});
  task->Start();

  RunFor((kChecks + 0.5) * kInterval);
}

TEST_F(HeartbeatTaskTest, HeartbeatFailureResetSuccess) {
  constexpr base::TimeDelta kInterval = base::Seconds(10);
  constexpr int kNumFailures = 3;

  EXPECT_CALL(*modem_, SupportsHealthCheck()).WillRepeatedly(Return(true));
  EXPECT_CALL(*modem_, CheckHealth()).WillRepeatedly(Return(false));
  EXPECT_CALL(*delegate_, ResetModem(kModemDeviceId)).WillOnce(Return(true));
  EXPECT_CALL(*metrics_,
              SendModemRecoveryState(
                  metrics::ModemRecoveryState::kRecoveryStateSuccess));

  auto task = GetTask(HeartbeatConfig{kNumFailures, kInterval});
  task->Start();

  RunFor((kNumFailures + 0.5) * kInterval);
}

TEST_F(HeartbeatTaskTest, HeartbeatFailureResetFailure) {
  constexpr base::TimeDelta kInterval = base::Seconds(10);
  constexpr int kNumFailures = 3;

  EXPECT_CALL(*modem_, SupportsHealthCheck()).WillRepeatedly(Return(true));
  EXPECT_CALL(*modem_, CheckHealth()).WillRepeatedly(Return(false));
  EXPECT_CALL(*delegate_, ResetModem(kModemDeviceId)).WillOnce(Return(false));
  EXPECT_CALL(*metrics_,
              SendModemRecoveryState(
                  metrics::ModemRecoveryState::kRecoveryStateFailure));

  auto task = GetTask(HeartbeatConfig{kNumFailures, kInterval});
  task->Start();

  RunFor((kNumFailures + 0.5) * kInterval);
}

TEST_F(HeartbeatTaskTest, HeartbeatTemporaryFailureAndRecovery) {
  constexpr base::TimeDelta kInterval = base::Seconds(10);
  constexpr int kNumFailures = 3;

  EXPECT_CALL(*modem_, SupportsHealthCheck()).WillRepeatedly(Return(true));
  {
    InSequence seq;

    for (int i = 0; i < kNumFailures; i++) {
      EXPECT_CALL(*modem_, CheckHealth()).WillOnce(Return(false));
      EXPECT_CALL(*modem_, CheckHealth()).WillOnce(Return(true));
    }
  }
  EXPECT_CALL(*delegate_, ResetModem(kModemDeviceId)).Times(0);

  auto task = GetTask(HeartbeatConfig{kNumFailures, kInterval});
  task->Start();

  RunFor((2 * kNumFailures + 0.5) * kInterval);
}

TEST_F(HeartbeatTaskTest, HeartbeatStopLowPowerState) {
  constexpr base::TimeDelta kInterval = base::Seconds(10);
  constexpr int kChecks = 3;

  EXPECT_CALL(*modem_, GetPowerState())
      .WillOnce(Return(Modem::PowerState::LOW));
  ON_CALL(*modem_, SupportsHealthCheck()).WillByDefault(Return(true));
  EXPECT_CALL(*modem_, CheckHealth()).Times(0);

  auto task = GetTask(HeartbeatConfig{3, kInterval});
  task->Start();

  RunFor((kChecks + 0.5) * kInterval);
}

TEST_F(HeartbeatTaskTest, HeartbeatSuccessModemIdle) {
  constexpr base::TimeDelta kInterval = base::Seconds(10);
  constexpr base::TimeDelta kIntervalModemIdle = base::Seconds(100);
  constexpr int kChecks = 5;

  EXPECT_CALL(*modem_, GetPowerState())
      .WillRepeatedly(Return(Modem::PowerState::ON));
  EXPECT_CALL(*modem_, GetState())
      .WillRepeatedly(Return(Modem::State::REGISTERED));
  EXPECT_CALL(*modem_, SupportsHealthCheck()).WillRepeatedly(Return(true));
  EXPECT_CALL(*modem_, CheckHealth())
      .Times(kChecks)
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*delegate_, ResetModem(kModemDeviceId)).Times(0);

  auto task = GetTask(HeartbeatConfig{3, kInterval, kIntervalModemIdle});
  task->Start();

  RunFor((kChecks + 0.5) * kIntervalModemIdle);
}

}  // namespace modemfwd
