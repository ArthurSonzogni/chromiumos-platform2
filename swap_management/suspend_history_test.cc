// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "swap_management/suspend_history.h"

#include <base/time/time.h>
#include <gtest/gtest.h>

namespace swap_management {
class MockSuspendHistory : public swap_management::SuspendHistory {
 public:
  MockSuspendHistory() = default;
  MockSuspendHistory& operator=(const MockSuspendHistory&) = delete;
  MockSuspendHistory(const MockSuspendHistory&) = delete;

  int GetBufferSize() { return suspend_history_.size(); }
};

class SuspendHistoryTest : public ::testing::Test {
 public:
  void SetUp() override {}

  void TearDown() override { UpdateBoottimeForTesting(std::nullopt); }

 protected:
  void FastForwardBy(base::TimeDelta delta) {
    now_ += delta;
    UpdateBoottimeForTesting(now_);
  }

 private:
  base::TimeTicks now_ = base::TimeTicks::Now();
};

TEST_F(SuspendHistoryTest, IsSuspend) {
  MockSuspendHistory history;
  EXPECT_FALSE(history.IsSuspended());
  history.OnSuspendImminent();
  EXPECT_TRUE(history.IsSuspended());
  history.OnSuspendDone(base::TimeDelta());
  EXPECT_FALSE(history.IsSuspended());
}

TEST_F(SuspendHistoryTest, CalculateTotalSuspendedDuration) {
  MockSuspendHistory history;
  history.SetMaxIdleDuration(base::Hours(25));

  history.OnSuspendImminent();
  FastForwardBy(base::Hours(1));
  history.OnSuspendDone(base::Hours(2));
  FastForwardBy(base::Hours(2));
  history.OnSuspendImminent();
  FastForwardBy(base::Hours(5));
  history.OnSuspendDone(base::Hours(5));
  FastForwardBy(base::Hours(1));
  history.OnSuspendImminent();
  FastForwardBy(base::Hours(2));
  history.OnSuspendDone(base::Hours(2));
  FastForwardBy(base::Hours(1));
  history.OnSuspendImminent();
  FastForwardBy(base::Hours(1));
  history.OnSuspendDone(base::Hours(1));
  FastForwardBy(base::Hours(1));

  EXPECT_EQ(history.CalculateTotalSuspendedDuration(base::Hours(4)),
            base::Hours(8));
}

TEST_F(SuspendHistoryTest, GCEntries) {
  MockSuspendHistory history;
  history.SetMaxIdleDuration(base::Hours(25));
  ASSERT_EQ(history.GetBufferSize(), 1);

  // awake for 26 hours.
  FastForwardBy(base::Hours(26));
  history.OnSuspendImminent();
  FastForwardBy(base::Hours(1));
  history.OnSuspendDone(base::Hours(1));
  // Does not pop entry if there was only 1 entry.
  EXPECT_EQ(history.GetBufferSize(), 2);

  // awake for 1 hour.
  FastForwardBy(base::Hours(1));
  history.OnSuspendImminent();
  FastForwardBy(base::Hours(1));
  history.OnSuspendDone(base::Hours(1));
  // The first entry is GC-ed.
  EXPECT_EQ(history.GetBufferSize(), 2);

  // awake for 2 hours.
  FastForwardBy(base::Hours(2));
  history.OnSuspendImminent();
  FastForwardBy(base::Hours(1));
  history.OnSuspendDone(base::Hours(1));
  EXPECT_EQ(history.GetBufferSize(), 3);

  // awake for 10 hours.
  FastForwardBy(base::Hours(10));
  history.OnSuspendImminent();
  FastForwardBy(base::Hours(11));
  history.OnSuspendDone(base::Hours(11));
  EXPECT_EQ(history.GetBufferSize(), 4);

  // awake for 20 hours.
  FastForwardBy(base::Hours(20));
  history.OnSuspendImminent();
  FastForwardBy(base::Hours(12));
  history.OnSuspendDone(base::Hours(12));
  // The entries except last 2 entries are GC-ed.
  EXPECT_EQ(history.GetBufferSize(), 2);
  EXPECT_EQ(history.CalculateTotalSuspendedDuration(base::Hours(25)),
            base::Hours(23));
}

}  // namespace swap_management
