// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/mm/balloon_blocker.h"

#include <memory>
#include <utility>

#include <base/test/task_environment.h>
#include <base/time/time.h>
#include <gtest/gtest.h>
#include <metrics/metrics_library_mock.h>
#include <vm_applications/apps.pb.h>

#include "vm_tools/concierge/byte_unit.h"
#include "vm_tools/concierge/mm/balloon_metrics.h"
#include "vm_tools/concierge/mm/fake_balloon.h"

namespace vm_tools::concierge::mm {
namespace {

using testing::_;

class BalloonBlockerTest : public ::testing::Test {
 public:
  void SetUp() override {
    metrics_ = std::make_unique<MetricsLibraryMock>();
    std::unique_ptr<FakeBalloon> fake_balloon = std::make_unique<FakeBalloon>();
    leaked_fake_balloon_ = fake_balloon.get();

    balloon_blocker_ = std::make_unique<BalloonBlocker>(
        6, std::move(fake_balloon),
        std::make_unique<BalloonMetrics>(
            apps::VmType::ARCVM,
            raw_ref<MetricsLibraryInterface>::from_ptr(metrics_.get())),
        base::Milliseconds(1000), base::Milliseconds(100));
  }

 protected:
  void SetBlockPriorityTo(ResizePriority priority) {
    // Setting a specific block priority means working up to it 1 priority at a
    // time.
    for (size_t i = LowestResizePriority(); i >= priority; i--) {
      ResizePriority current_priority = static_cast<ResizePriority>(i);
      balloon_blocker_->TryResize({current_priority, 1});
      balloon_blocker_->TryResize({current_priority, -1});
    }
  }

  void AssertBalloonAdjustedBy(int64_t delta_bytes) {
    balloon_adjustment_count_++;
    ASSERT_EQ(leaked_fake_balloon_->resizes_.size(), balloon_adjustment_count_);
    ASSERT_EQ(leaked_fake_balloon_->resizes_.back(), delta_bytes);
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  FakeBalloon* leaked_fake_balloon_{};
  std::unique_ptr<BalloonBlocker> balloon_blocker_{};
  size_t balloon_adjustment_count_{};
  std::unique_ptr<MetricsLibraryMock> metrics_;
};

TEST_F(BalloonBlockerTest, TestBlockedDoesNotAdjustBalloon) {
  SetBlockPriorityTo(ResizePriority::kFocusedApp);

  size_t num_adjustments = leaked_fake_balloon_->resizes_.size();

  ASSERT_EQ(balloon_blocker_->TryResize({ResizePriority::kCachedTab, MiB(100)}),
            0);

  ASSERT_EQ(leaked_fake_balloon_->resizes_.size(), num_adjustments);
}

TEST_F(BalloonBlockerTest, TestClearBlockersUpToInclusive) {
  SetBlockPriorityTo(ResizePriority::kPerceptibleApp);
  ASSERT_LT(balloon_blocker_->LowestUnblockedPriority(ResizeDirection::kDeflate,
                                                      base::TimeTicks::Now()),
            ResizePriority::kPerceptibleApp);

  // Clearing the blockers at a lower priority should not clear the high
  // priority blocker.
  balloon_blocker_->ClearBlockersUpToInclusive(ResizePriority::kCachedApp);
  ASSERT_LT(balloon_blocker_->LowestUnblockedPriority(ResizeDirection::kDeflate,
                                                      base::TimeTicks::Now()),
            ResizePriority::kPerceptibleApp);

  // Clearing the blockers at the highest priority should clear everything.
  balloon_blocker_->ClearBlockersUpToInclusive(HighestResizePriority());
  ASSERT_EQ(balloon_blocker_->LowestUnblockedPriority(ResizeDirection::kDeflate,
                                                      base::TimeTicks::Now()),
            LowestResizePriority());
}

TEST_F(BalloonBlockerTest, TestLowestUnblockedPriorityStepByStep) {
  // An inflation should only block the lowest level at first, so the second
  // lowest priority should be unblocked.
  size_t expected_unblocked_priority_index = 1;

  while (expected_unblocked_priority_index < LowestResizePriority()) {
    balloon_blocker_->TryResize({ResizePriority::kNoKillCandidatesHost, 100});
    ASSERT_EQ(
        balloon_blocker_->LowestUnblockedPriority(ResizeDirection::kDeflate,
                                                  base::TimeTicks::Now()),
        kAllResizePrioritiesIncreasing[expected_unblocked_priority_index]);

    expected_unblocked_priority_index++;

    balloon_blocker_->TryResize({ResizePriority::kNoKillCandidatesHost, -100});
    ASSERT_EQ(
        balloon_blocker_->LowestUnblockedPriority(ResizeDirection::kInflate,
                                                  base::TimeTicks::Now()),
        kAllResizePrioritiesIncreasing[expected_unblocked_priority_index]);

    expected_unblocked_priority_index++;
  }
}

TEST_F(BalloonBlockerTest, TestLowPriorityBlockDuration) {
  ASSERT_EQ(balloon_blocker_->LowestUnblockedPriority(ResizeDirection::kDeflate,
                                                      base::TimeTicks::Now()),
            LowestResizePriority());

  SetBlockPriorityTo(ResizePriority::kCachedApp);

  ASSERT_LE(balloon_blocker_->LowestUnblockedPriority(ResizeDirection::kDeflate,
                                                      base::TimeTicks::Now()),
            ResizePriority::kCachedApp);

  // Deflations should be blocked at lowest priority before the block is
  // expired.
  task_environment_.FastForwardBy(base::Milliseconds(999));

  ASSERT_LE(balloon_blocker_->LowestUnblockedPriority(ResizeDirection::kDeflate,
                                                      base::TimeTicks::Now()),
            ResizePriority::kCachedApp);

  // And the block should be removed after the block is expired.
  task_environment_.FastForwardBy(base::Milliseconds(10));
  ASSERT_EQ(balloon_blocker_->LowestUnblockedPriority(ResizeDirection::kDeflate,
                                                      base::TimeTicks::Now()),
            LowestResizePriority());
}

TEST_F(BalloonBlockerTest, TestHighPriorityBlockDuration) {
  SetBlockPriorityTo(ResizePriority::kFocusedApp);

  ASSERT_LT(balloon_blocker_->LowestUnblockedPriority(ResizeDirection::kInflate,
                                                      base::TimeTicks::Now()),
            ResizePriority::kFocusedApp);

  // Deflations should be blocked before the block is expired.
  task_environment_.FastForwardBy(base::Milliseconds(99));
  ASSERT_LT(balloon_blocker_->LowestUnblockedPriority(ResizeDirection::kInflate,
                                                      base::TimeTicks::Now()),
            ResizePriority::kFocusedApp);

  // And the block should be removed after the block is expired, but the lower
  // priority block should still be in place.
  task_environment_.FastForwardBy(base::Milliseconds(10));
  ASSERT_EQ(balloon_blocker_->LowestUnblockedPriority(ResizeDirection::kInflate,
                                                      base::TimeTicks::Now()),
            ResizePriority::kPerceptibleApp);
}

TEST_F(BalloonBlockerTest, TestSuddenHighPriorityDoesNotBlockForLong) {
  for (size_t i = 0; i < 20; i++) {
    balloon_blocker_->TryResize({ResizePriority::kBalloonStall, 1});
    balloon_blocker_->TryResize({ResizePriority::kBalloonStall, -1});
  }

  ASSERT_EQ(balloon_blocker_->LowestUnblockedPriority(ResizeDirection::kDeflate,
                                                      base::TimeTicks::Now()),
            ResizePriority::kInvalid);

  // Since a series of high priority inflations and deflations were made, they
  // should only have blocked for the high priority block duration.
  task_environment_.FastForwardBy(base::Milliseconds(200));

  ASSERT_EQ(balloon_blocker_->LowestUnblockedPriority(ResizeDirection::kDeflate,
                                                      base::TimeTicks::Now()),
            LowestResizePriority());
}

TEST_F(BalloonBlockerTest, TestPriorityFallback) {
  SetBlockPriorityTo(ResizePriority::kFocusedTab);
  task_environment_.FastForwardBy(base::Milliseconds(20));

  // The focused tab block should still apply.
  ASSERT_EQ(balloon_blocker_->LowestUnblockedPriority(ResizeDirection::kDeflate,
                                                      base::TimeTicks::Now()),
            ResizePriority::kNoKillCandidatesGuest);

  // Set an additional cached tab block.
  balloon_blocker_->TryResize({ResizePriority::kCachedTab, 1});

  task_environment_.FastForwardBy(base::Milliseconds(90));

  // The focused tab block should be expired.
  ASSERT_EQ(balloon_blocker_->LowestUnblockedPriority(ResizeDirection::kDeflate,
                                                      base::TimeTicks::Now()),
            ResizePriority::kPerceptibleApp);

  task_environment_.FastForwardBy(base::Milliseconds(10000));

  // The cached block should also be expired now.
  ASSERT_EQ(balloon_blocker_->LowestUnblockedPriority(ResizeDirection::kDeflate,
                                                      base::TimeTicks::Now()),
            LowestResizePriority());
}

TEST_F(BalloonBlockerTest, TestLowPriorityClearsHighPriorityBlock) {
  SetBlockPriorityTo(ResizePriority::kFocusedTab);

  // Should be unblocked at RESIZE_PRIORITY_NO_KILL_CANDIDATES_GUEST
  ASSERT_EQ(balloon_blocker_->LowestUnblockedPriority(ResizeDirection::kDeflate,
                                                      base::TimeTicks::Now()),
            ResizePriority::kNoKillCandidatesGuest);

  // A lower priority inflation request should un-do the higher priority
  // deflation blocks.
  balloon_blocker_->TryResize({ResizePriority::kCachedTab, 1});
  ASSERT_EQ(balloon_blocker_->LowestUnblockedPriority(ResizeDirection::kDeflate,
                                                      base::TimeTicks::Now()),
            ResizePriority::kPerceptibleApp);
}

TEST_F(BalloonBlockerTest, TestBalloonStallSetsCorrectBlock) {
  // Nothing should be blocked by default.
  ASSERT_EQ(balloon_blocker_->LowestUnblockedPriority(ResizeDirection::kDeflate,
                                                      base::TimeTicks::Now()),
            LowestResizePriority());

  leaked_fake_balloon_->RunStallCallback({}, {
                                                 .success = true,
                                                 .actual_delta_bytes = -MiB(16),
                                                 .new_target = 0,
                                             });

  // After a balloon stall, inflations should be blocked at the highest
  // priority.
  ASSERT_EQ(balloon_blocker_->LowestUnblockedPriority(ResizeDirection::kInflate,
                                                      base::TimeTicks::Now()),
            ResizePriority::kInvalid);
}

TEST_F(BalloonBlockerTest, TestDeflateBelowZero) {
  // First inflate the balloon by some amount.
  ASSERT_EQ(
      balloon_blocker_->TryResize({ResizePriority::kMglruReclaim, MiB(128)}),
      MiB(128));

  // A deflation larger than the previous inflation should not deflate below 0.
  ASSERT_EQ(
      balloon_blocker_->TryResize({ResizePriority::kBalloonStall, -MiB(256)}),
      -MiB(128));
}

TEST_F(BalloonBlockerTest, TestStallMetrics) {
  const int deflate_mib = 16;
  EXPECT_CALL(*metrics_,
              SendToUMA("Memory.VMMMS.ARCVM.Deflate", deflate_mib, _, _, _))
      .Times(1);

  const base::TimeDelta resize_interval = base::Seconds(12);
  task_environment_.FastForwardBy(resize_interval);
  EXPECT_CALL(*metrics_, SendTimeToUMA("Memory.VMMMS.ARCVM.ResizeInterval",
                                       resize_interval, _, _, _))
      .Times(1);

  const int stall_throughput = 14;
  EXPECT_CALL(*metrics_, SendLinearToUMA("Memory.VMMMS.ARCVM.StallThroughput",
                                         stall_throughput, _))
      .Times(1);

  leaked_fake_balloon_->RunStallCallback(
      {
          .inflate_mb_per_s = stall_throughput,
      },
      {
          .success = true,
          .actual_delta_bytes = -MiB(deflate_mib),
          .new_target = 0,
      });
}

TEST_F(BalloonBlockerTest, TestResizeMetrics) {
  int size_mib = 0;
  const auto do_resize = [this, &size_mib](int delta_mib,
                                           base::TimeDelta resize_interval,
                                           int resize_count) {
    EXPECT_CALL(*metrics_, SendTimeToUMA("Memory.VMMMS.ARCVM.ResizeInterval",
                                         resize_interval, _, _, _))
        .Times(1);

    if (delta_mib >= 0) {
      EXPECT_CALL(*metrics_,
                  SendToUMA("Memory.VMMMS.ARCVM.Inflate", delta_mib, _, _, _))
          .Times(1);
    } else {
      EXPECT_CALL(*metrics_,
                  SendToUMA("Memory.VMMMS.ARCVM.Deflate", -delta_mib, _, _, _))
          .Times(1);
    }

    if (resize_count > 0) {
      EXPECT_CALL(*metrics_,
                  SendRepeatedToUMA("Memory.VMMMS.ARCVM.Size10Minutes",
                                    size_mib, _, _, _, resize_count))
          .Times(1);
    } else {
      EXPECT_CALL(*metrics_,
                  SendRepeatedToUMA("Memory.VMMMS.ARCVM.Size10Minutes",
                                    size_mib, _, _, _, _))
          .Times(0);
    }

    size_mib += delta_mib;
    leaked_fake_balloon_->do_resize_results_.push_back(Balloon::ResizeResult{
        .success = true,
        .actual_delta_bytes = MiB(delta_mib),
        .new_target = MiB(size_mib),
    });
    task_environment_.FastForwardBy(resize_interval);
    balloon_blocker_->TryResize(
        {ResizePriority::kMglruReclaim, MiB(delta_mib)});
    task_environment_.RunUntilIdle();
  };

  {
    SCOPED_TRACE("Inflate after 1 size reporting interval");
    do_resize(400, base::Minutes(10), 1);
  }
  {
    SCOPED_TRACE("Deflate after 17 size reporting intervals");
    do_resize(-200, base::Minutes(170), 17);
  }
  {
    SCOPED_TRACE("Inflate after 0.5 size reporting intervals");
    do_resize(800, base::Minutes(5), 0);
  }
  {
    SCOPED_TRACE("Inflate after 1 unaligned size reporting interval");
    do_resize(-600, base::Minutes(10), 1);
  }
}

}  // namespace
}  // namespace vm_tools::concierge::mm
