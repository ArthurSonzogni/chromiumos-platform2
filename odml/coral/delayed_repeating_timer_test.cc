// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/coral/delayed_repeating_timer.h"

#include <memory>

#include <base/test/task_environment.h>
#include <base/time/time.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace coral {

namespace {
using base::test::TaskEnvironment;
}  // namespace

class DelayRepeatingTimerTest : public testing::Test {
 public:
  void SetUp() override { count_ = 0; };

  void TearDown() override { timer_.reset(); };

  void CreateTimer(base::TimeDelta start_delay, base::TimeDelta repeat_delay) {
    timer_ = std::make_unique<DelayedRepeatingTimer>(
        start_delay, repeat_delay,
        base::BindRepeating(&DelayRepeatingTimerTest::OnTimesUp,
                            base::Unretained(this)));
  }

 protected:
  void OnTimesUp() { count_++; }

  int count_;
  std::unique_ptr<DelayedRepeatingTimer> timer_;
};

TEST_F(DelayRepeatingTimerTest, Success) {
  // Tests a successful execution of the timer with a start delay and repeat
  // delay.
  TaskEnvironment task_environment(TaskEnvironment::TimeSource::MOCK_TIME,
                                   TaskEnvironment::MainThreadType::DEFAULT);
  CreateTimer(base::Minutes(60), base::Minutes(11));

  timer_->Start();
  task_environment.FastForwardBy(base::Minutes(59));
  EXPECT_EQ(count_, 0);
  task_environment.FastForwardBy(base::Minutes(2));
  EXPECT_EQ(count_, 1);
  task_environment.FastForwardBy(base::Minutes(11 * 5));
  EXPECT_EQ(count_, 6);
}

TEST_F(DelayRepeatingTimerTest, StopBeforeStartDelay) {
  // Tests stopping the timer before the start delay expires.
  TaskEnvironment task_environment(TaskEnvironment::TimeSource::MOCK_TIME,
                                   TaskEnvironment::MainThreadType::DEFAULT);
  CreateTimer(base::Minutes(10), base::Minutes(5));

  timer_->Start();
  task_environment.FastForwardBy(base::Minutes(5));
  timer_->Stop();
  task_environment.FastForwardBy(base::Minutes(100));
  EXPECT_EQ(count_, 0);
}

TEST_F(DelayRepeatingTimerTest, StopDuringRepeat) {
  // Tests stopping the timer during the repeating phase.
  TaskEnvironment task_environment(TaskEnvironment::TimeSource::MOCK_TIME,
                                   TaskEnvironment::MainThreadType::DEFAULT);
  CreateTimer(base::Minutes(5), base::Minutes(14));

  EXPECT_EQ(count_, 0);
  timer_->Start();
  task_environment.FastForwardBy(base::Minutes(6));
  EXPECT_EQ(count_, 1);
  task_environment.FastForwardBy(base::Minutes(15));
  EXPECT_EQ(count_, 2);
  timer_->Stop();
  task_environment.FastForwardBy(base::Minutes(100));
  EXPECT_EQ(count_, 2);
}

TEST_F(DelayRepeatingTimerTest, RestartTimer) {
  // Tests restarting the timer after it has been stopped.
  TaskEnvironment task_environment(TaskEnvironment::TimeSource::MOCK_TIME,
                                   TaskEnvironment::MainThreadType::DEFAULT);
  CreateTimer(base::Minutes(15), base::Minutes(3));

  timer_->Start();
  task_environment.FastForwardBy(base::Minutes(16));
  EXPECT_EQ(count_, 1);
  timer_->Stop();
  task_environment.FastForwardBy(base::Minutes(5));
  EXPECT_EQ(count_, 1);

  timer_->Start();
  task_environment.FastForwardBy(base::Minutes(16));
  EXPECT_EQ(count_, 2);
  task_environment.FastForwardBy(base::Minutes(3));
  EXPECT_EQ(count_, 3);
}

TEST_F(DelayRepeatingTimerTest, RestartTimerBeforeInitialDelay) {
  // Tests restarting the timer before the initial delay has finished.
  TaskEnvironment task_environment(TaskEnvironment::TimeSource::MOCK_TIME,
                                   TaskEnvironment::MainThreadType::DEFAULT);
  CreateTimer(base::Minutes(16), base::Minutes(5));
  timer_->Start();
  task_environment.FastForwardBy(base::Minutes(6));
  EXPECT_EQ(count_, 0);
  timer_->Start();
  task_environment.FastForwardBy(base::Minutes(15));
  EXPECT_EQ(count_, 0);
  task_environment.FastForwardBy(base::Minutes(2));
  EXPECT_EQ(count_, 1);
}

}  // namespace coral
