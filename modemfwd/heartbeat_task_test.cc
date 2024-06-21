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
#include "modemfwd/mock_modem_helper.h"
#include "modemfwd/modem_helper_directory_stub.h"

using testing::_;
using testing::AtLeast;
using testing::InSequence;
using testing::IsNull;
using testing::NotNull;
using testing::Return;
using testing::SaveArg;
using testing::WithArg;

namespace {
constexpr char kModemDeviceId[] = "modem_device_id";
}  // namespace

namespace modemfwd {

class HeartbeatTaskTest : public ::testing::Test {
 public:
  HeartbeatTaskTest() {
    ON_CALL(modem_, GetDeviceId()).WillByDefault(Return(kModemDeviceId));
    helper_directory_.AddHelper(kModemDeviceId, &helper_);
  }

 protected:
  std::unique_ptr<HeartbeatTask> GetTask(HeartbeatConfig config) {
    ON_CALL(helper_, GetHeartbeatConfig()).WillByDefault(Return(config));
    return HeartbeatTask::Create(&delegate_, &modem_, &helper_directory_,
                                 &metrics_);
  }

  void RunFor(base::TimeDelta time) {
    task_environment_.GetMainThreadTaskRunner()->PostDelayedTask(
        FROM_HERE, task_environment_.QuitClosure(), time);
    task_environment_.RunUntilQuit();
  }

  // Must be the first member.
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};

  MockDelegate delegate_;
  MockModem modem_;
  MockMetrics metrics_;

  ModemHelperDirectoryStub helper_directory_;
  MockModemHelper helper_;
};

TEST_F(HeartbeatTaskTest, HeartbeatSuccess) {
  constexpr base::TimeDelta kInterval = base::Seconds(10);
  constexpr int kChecks = 5;

  EXPECT_CALL(modem_, SupportsHealthCheck()).WillRepeatedly(Return(true));
  EXPECT_CALL(modem_, CheckHealth())
      .Times(kChecks)
      .WillRepeatedly(Return(true));
  EXPECT_CALL(delegate_, ResetModem(kModemDeviceId)).Times(0);

  auto task = GetTask(HeartbeatConfig{3, kInterval});
  task->Start();

  RunFor((kChecks + 0.5) * kInterval);
}

TEST_F(HeartbeatTaskTest, HeartbeatFailureResetSuccess) {
  constexpr base::TimeDelta kInterval = base::Seconds(10);
  constexpr int kNumFailures = 3;

  EXPECT_CALL(modem_, SupportsHealthCheck()).WillRepeatedly(Return(true));
  EXPECT_CALL(modem_, CheckHealth()).WillRepeatedly(Return(false));
  EXPECT_CALL(delegate_, ResetModem(kModemDeviceId)).WillOnce(Return(true));

  auto task = GetTask(HeartbeatConfig{kNumFailures, kInterval});
  task->Start();

  EXPECT_CALL(delegate_, FinishTask(task.get(), IsNull()));
  RunFor((kNumFailures + 0.5) * kInterval);
}

TEST_F(HeartbeatTaskTest, HeartbeatFailureResetFailure) {
  constexpr base::TimeDelta kInterval = base::Seconds(10);
  constexpr int kNumFailures = 3;

  EXPECT_CALL(modem_, SupportsHealthCheck()).WillRepeatedly(Return(true));
  EXPECT_CALL(modem_, CheckHealth()).WillRepeatedly(Return(false));
  EXPECT_CALL(delegate_, ResetModem(kModemDeviceId)).WillOnce(Return(false));
  EXPECT_CALL(metrics_,
              SendModemRecoveryState(
                  metrics::ModemRecoveryState::kRecoveryStateFailure));

  auto task = GetTask(HeartbeatConfig{kNumFailures, kInterval});
  task->Start();

  EXPECT_CALL(delegate_, FinishTask(task.get(), NotNull()));
  RunFor((kNumFailures + 0.5) * kInterval);
}

TEST_F(HeartbeatTaskTest, HeartbeatTemporaryFailureAndRecovery) {
  constexpr base::TimeDelta kInterval = base::Seconds(10);
  constexpr int kNumFailures = 3;

  EXPECT_CALL(modem_, SupportsHealthCheck()).WillRepeatedly(Return(true));
  {
    InSequence seq;

    for (int i = 0; i < kNumFailures; i++) {
      EXPECT_CALL(modem_, CheckHealth()).WillOnce(Return(false));
      EXPECT_CALL(modem_, CheckHealth()).WillOnce(Return(true));
    }
  }
  EXPECT_CALL(delegate_, ResetModem(kModemDeviceId)).Times(0);

  auto task = GetTask(HeartbeatConfig{kNumFailures, kInterval});
  task->Start();

  RunFor((2 * kNumFailures + 0.5) * kInterval);
}

TEST_F(HeartbeatTaskTest, HeartbeatStopLowPowerState) {
  constexpr base::TimeDelta kInterval = base::Seconds(10);
  constexpr int kChecks = 3;

  EXPECT_CALL(modem_, GetPowerState())
      .WillRepeatedly(Return(Modem::PowerState::LOW));
  EXPECT_CALL(modem_, SupportsHealthCheck()).WillRepeatedly(Return(true));
  EXPECT_CALL(modem_, CheckHealth()).Times(0);

  auto task = GetTask(HeartbeatConfig{3, kInterval});
  task->Start();

  RunFor((kChecks + 0.5) * kInterval);
}

TEST_F(HeartbeatTaskTest, HeartbeatSuccessModemIdle) {
  constexpr base::TimeDelta kInterval = base::Seconds(10);
  constexpr base::TimeDelta kIntervalModemIdle = base::Seconds(100);
  constexpr int kChecks = 5;

  EXPECT_CALL(modem_, GetPowerState())
      .WillRepeatedly(Return(Modem::PowerState::ON));
  EXPECT_CALL(modem_, GetState())
      .WillRepeatedly(Return(Modem::State::REGISTERED));
  EXPECT_CALL(modem_, SupportsHealthCheck()).WillRepeatedly(Return(true));
  EXPECT_CALL(modem_, CheckHealth())
      .Times(kChecks)
      .WillRepeatedly(Return(true));
  EXPECT_CALL(delegate_, ResetModem(kModemDeviceId)).Times(0);

  auto task = GetTask(HeartbeatConfig{3, kInterval, kIntervalModemIdle});
  task->Start();

  RunFor((kChecks + 0.5) * kIntervalModemIdle);
}

TEST_F(HeartbeatTaskTest, HeartbeatStopOnLowPowerStateUpdate) {
  constexpr base::TimeDelta kInterval = base::Seconds(10);
  constexpr int kChecks = 3;

  EXPECT_CALL(modem_, SupportsHealthCheck()).WillRepeatedly(Return(true));
  EXPECT_CALL(modem_, CheckHealth()).WillRepeatedly(Return(true));

  base::RepeatingCallback<void(Modem*)> power_state_cb;
  EXPECT_CALL(delegate_, RegisterOnModemPowerStateChangedCallback(_, _))
      .WillOnce(SaveArg<1>(&power_state_cb));
  EXPECT_CALL(delegate_, ResetModem(kModemDeviceId)).Times(0);

  EXPECT_CALL(modem_, GetPowerState())
      .WillRepeatedly(Return(Modem::PowerState::ON));
  auto task = GetTask(HeartbeatConfig{3, kInterval});
  task->Start();
  RunFor((kChecks + 0.5) * kInterval);

  EXPECT_CALL(modem_, CheckHealth()).Times(0);
  EXPECT_CALL(modem_, GetPowerState())
      .WillRepeatedly(Return(Modem::PowerState::LOW));
  power_state_cb.Run(&modem_);
  RunFor((kChecks + 0.5) * kInterval);

  EXPECT_CALL(modem_, CheckHealth())
      .Times(AtLeast(1))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(modem_, GetPowerState())
      .WillRepeatedly(Return(Modem::PowerState::ON));
  power_state_cb.Run(&modem_);
  RunFor((kChecks + 0.5) * kInterval);
}

TEST_F(HeartbeatTaskTest, HeartbeatIdleStateUpdate) {
  constexpr base::TimeDelta kInterval = base::Seconds(10);
  constexpr base::TimeDelta kIntervalModemIdle = base::Seconds(100);
  constexpr int kChecks = 3;

  EXPECT_CALL(modem_, GetPowerState())
      .WillRepeatedly(Return(Modem::PowerState::ON));
  EXPECT_CALL(modem_, SupportsHealthCheck()).WillRepeatedly(Return(true));
  EXPECT_CALL(modem_, CheckHealth()).WillRepeatedly(Return(true));

  base::RepeatingCallback<void(Modem*)> state_cb;
  EXPECT_CALL(delegate_, RegisterOnModemStateChangedCallback(_, _))
      .WillOnce(SaveArg<1>(&state_cb));
  EXPECT_CALL(delegate_, ResetModem(kModemDeviceId)).Times(0);

  EXPECT_CALL(modem_, GetState())
      .WillRepeatedly(Return(Modem::State::CONNECTED));
  auto task = GetTask(HeartbeatConfig{3, kInterval, kIntervalModemIdle});
  task->Start();
  RunFor((kChecks + 0.5) * kInterval);

  EXPECT_CALL(modem_, GetState())
      .WillRepeatedly(Return(Modem::State::REGISTERED));
  state_cb.Run(&modem_);
  RunFor((kChecks + 0.5) * kIntervalModemIdle);

  EXPECT_CALL(modem_, GetState())
      .WillRepeatedly(Return(Modem::State::CONNECTED));
  state_cb.Run(&modem_);
  RunFor((kChecks + 0.5) * kInterval);
}

TEST_F(HeartbeatTaskTest, HeartbeatStopOnFlash) {
  constexpr base::TimeDelta kInterval = base::Seconds(10);
  constexpr int kChecks = 3;
  EXPECT_CALL(modem_, SupportsHealthCheck()).WillRepeatedly(Return(true));

  base::OnceClosure start_flashing_cb;
  EXPECT_CALL(delegate_, RegisterOnStartFlashingCallback(_, _))
      .WillOnce(WithArg<1>([&start_flashing_cb](auto cb) {
        start_flashing_cb = std::move(cb);
      }));
  auto task = GetTask(HeartbeatConfig{3, kInterval});
  task->Start();
  // Simulate that we started flashing this modem
  std::move(start_flashing_cb).Run();

  EXPECT_CALL(modem_, CheckHealth()).Times(0);
  RunFor((kChecks + 0.5) * kInterval);
}

}  // namespace modemfwd
