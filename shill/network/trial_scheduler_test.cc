// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/trial_scheduler.h"

#include <utility>

#include <base/functional/bind.h>
#include <base/functional/callback_forward.h>
#include <base/test/task_environment.h>
#include <base/time/time.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "shill/mock_event_dispatcher.h"

namespace shill {
namespace {

using ::testing::_;
using ::testing::Eq;

class MockClosure {
 public:
  MOCK_METHOD(void, Run, ());
};

class TrialSchedulerTest : public testing::Test {
 protected:
  TrialSchedulerTest() : scheduler_(&dispatcher_) {}
  ~TrialSchedulerTest() override = default;

  // Schedules a trial and returns the delay of the PostDelayedTask().
  base::TimeDelta GetDelayOfScheduledTrial() {
    base::TimeDelta trial_delay;
    EXPECT_CALL(closure_, Run).Times(1);
    EXPECT_CALL(dispatcher_, PostDelayedTask)
        .WillOnce([this, &trial_delay](const base::Location& location,
                                       base::OnceClosure closure,
                                       base::TimeDelta delay) {
          EXPECT_TRUE(scheduler_.IsTrialScheduled());
          task_environment_.AdvanceClock(delay);
          trial_delay = delay;
          std::move(closure).Run();
        });
    scheduler_.ScheduleTrial(
        base::BindOnce(&MockClosure::Run, base::Unretained(&closure_)));
    return trial_delay;
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  MockClosure closure_;
  MockEventDispatcher dispatcher_;
  TrialScheduler scheduler_;
};

TEST_F(TrialSchedulerTest, ScheduleTrial) {
  EXPECT_FALSE(scheduler_.IsTrialScheduled());

  // The 1st delay should be zero.
  const base::TimeDelta delay1 = GetDelayOfScheduledTrial();
  EXPECT_TRUE(delay1.is_zero());

  // The 2nd delay should be greater than zero.
  const base::TimeDelta delay2 = GetDelayOfScheduledTrial();
  EXPECT_TRUE(delay2.is_positive());

  // The 3rd delay should be twice of 2nd delay.
  const base::TimeDelta delay3 = GetDelayOfScheduledTrial();
  EXPECT_EQ(delay3, delay2 * 2);

  // With time goes awhile, the 4th delay plus the period should be twice of 3rd
  // delay.
  const base::TimeDelta elapsed_time = base::Milliseconds(5);
  task_environment_.AdvanceClock(elapsed_time);
  const base::TimeDelta delay4 = GetDelayOfScheduledTrial();
  EXPECT_EQ(elapsed_time + delay4, delay3 * 2);

  // After resetting the delay, the 5th delay should be zero.
  scheduler_.ResetInterval();
  const base::TimeDelta delay5 = GetDelayOfScheduledTrial();
  EXPECT_TRUE(delay5.is_zero());
}

TEST_F(TrialSchedulerTest, CancelTrial) {
  // Execute the trials twice to make the delay positive.
  GetDelayOfScheduledTrial();
  const base::TimeDelta delay1 = GetDelayOfScheduledTrial();
  EXPECT_TRUE(delay1.is_positive());

  // Cancal the trial before the trial is executed.
  EXPECT_CALL(dispatcher_, PostDelayedTask(_, _, Eq(delay1 * 2))).Times(1);
  scheduler_.ScheduleTrial(
      base::BindOnce(&MockClosure::Run, base::Unretained(&closure_)));
  EXPECT_TRUE(scheduler_.IsTrialScheduled());
  const base::TimeDelta elapsed_time = base::Milliseconds(3);
  task_environment_.AdvanceClock(elapsed_time);
  scheduler_.CancelTrial();
  EXPECT_FALSE(scheduler_.IsTrialScheduled());

  // Schedule the trial again, the elapsed time plus delay should be twice of
  // 1st delay.
  const base::TimeDelta delay2 = GetDelayOfScheduledTrial();
  EXPECT_EQ(elapsed_time + delay2, delay1 * 2);
}

TEST_F(TrialSchedulerTest, ScheduleImmediately) {
  // Execute the trials twice to make the delay positive.
  GetDelayOfScheduledTrial();
  const base::TimeDelta delay1 = GetDelayOfScheduledTrial();
  EXPECT_TRUE(delay1.is_positive());

  // After time goes more than the delay, the next trial should be scheduled
  // immediately.
  const base::TimeDelta elapsed_time = delay1 * 5;
  task_environment_.AdvanceClock(elapsed_time);
  const base::TimeDelta delay2 = GetDelayOfScheduledTrial();
  EXPECT_TRUE(delay2.is_zero());
}

TEST_F(TrialSchedulerTest, MaximumDelay) {
  for (int i = 0; i < 64; i++) {
    GetDelayOfScheduledTrial();
  }

  const base::TimeDelta delay = GetDelayOfScheduledTrial();
  EXPECT_EQ(delay, TrialScheduler::kMaxInterval);
}

}  // namespace
}  // namespace shill
