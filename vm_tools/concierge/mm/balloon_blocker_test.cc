// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/mm/balloon_blocker.h"

#include <memory>
#include <utility>

#include <base/test/task_environment.h>
#include <gtest/gtest.h>

#include "vm_tools/concierge/byte_unit.h"
#include "vm_tools/concierge/mm/fake_balloon.h"

namespace vm_tools::concierge::mm {
namespace {

class BalloonBlockerTest : public ::testing::Test {
 public:
  void SetUp() override {
    std::unique_ptr<FakeBalloon> fake_balloon = std::make_unique<FakeBalloon>();
    leaked_fake_balloon_ = fake_balloon.get();

    balloon_blocker_ = std::make_unique<BalloonBlocker>(
        6, std::move(fake_balloon), base::Milliseconds(1000),
        base::Milliseconds(100),
        base::BindRepeating(&BalloonBlockerTest::Now, base::Unretained(this)));
    now_ = base::TimeTicks::Now();
  }

 protected:
  void SetBlockPriorityTo(ResizePriority priority) {
    // Setting a specific block priority means working up to it 1 priority at a
    // time.
    for (size_t i = 0; i < 20; i++) {
      balloon_blocker_->TryResize({priority, 1});
      balloon_blocker_->TryResize({priority, -1});
    }
  }

  void AssertBalloonAdjustedBy(int64_t delta_bytes) {
    balloon_adjustment_count_++;
    ASSERT_EQ(leaked_fake_balloon_->resizes_.size(), balloon_adjustment_count_);
    ASSERT_EQ(leaked_fake_balloon_->resizes_.back(), delta_bytes);
  }

  base::TimeTicks Now() { return now_; }

  base::test::TaskEnvironment task_environment_{};
  FakeBalloon* leaked_fake_balloon_{};
  std::unique_ptr<BalloonBlocker> balloon_blocker_{};
  size_t balloon_adjustment_count_{};
  base::TimeTicks now_{};
};

TEST_F(BalloonBlockerTest, TestBlockedDoesNotAdjustBalloon) {
  SetBlockPriorityTo(ResizePriority::RESIZE_PRIORITY_FOCUSED_APP);

  size_t num_adjustments = leaked_fake_balloon_->resizes_.size();

  ASSERT_EQ(balloon_blocker_->TryResize(
                {ResizePriority::RESIZE_PRIORITY_CACHED_TAB, MiB(100)}),
            0);

  ASSERT_EQ(leaked_fake_balloon_->resizes_.size(), num_adjustments);
}

TEST_F(BalloonBlockerTest, TestLowestUnblockedPriorityStepByStep) {
  // An inflation should only block at the lowest level at first.
  balloon_blocker_->TryResize(
      {ResizePriority::RESIZE_PRIORITY_NO_KILL_CANDIDATES_HOST, 100});
  ASSERT_EQ(balloon_blocker_->LowestUnblockedPriority(ResizeDirection::kDeflate,
                                                      now_),
            ResizePriority::RESIZE_PRIORITY_CACHED_APP);

  // Next, a deflation should cause an inflation block at cached app meaning
  // that CACHED_TAB is unblocked.
  balloon_blocker_->TryResize(
      {ResizePriority::RESIZE_PRIORITY_NO_KILL_CANDIDATES_GUEST, -100});
  ASSERT_EQ(balloon_blocker_->LowestUnblockedPriority(ResizeDirection::kInflate,
                                                      now_),
            ResizePriority::RESIZE_PRIORITY_CACHED_TAB);

  // Another inflation should cause the unblocked priority to increase to
  // PERCEPTIBLE_APP
  balloon_blocker_->TryResize(
      {ResizePriority::RESIZE_PRIORITY_NO_KILL_CANDIDATES_HOST, 100});
  ASSERT_EQ(balloon_blocker_->LowestUnblockedPriority(ResizeDirection::kDeflate,
                                                      now_),
            ResizePriority::RESIZE_PRIORITY_PERCEPTIBLE_APP);

  // And another deflation should cause the unblocked priority to increase to
  // PERCEPTIBLE_TAB.
  balloon_blocker_->TryResize(
      {ResizePriority::RESIZE_PRIORITY_NO_KILL_CANDIDATES_GUEST, -100});
  ASSERT_EQ(balloon_blocker_->LowestUnblockedPriority(ResizeDirection::kInflate,
                                                      now_),
            ResizePriority::RESIZE_PRIORITY_PERCEPTIBLE_TAB);

  // Repeat for increasing priorities...

  // FOCUSED_APP
  balloon_blocker_->TryResize(
      {ResizePriority::RESIZE_PRIORITY_NO_KILL_CANDIDATES_HOST, 100});
  ASSERT_EQ(balloon_blocker_->LowestUnblockedPriority(ResizeDirection::kDeflate,
                                                      now_),
            ResizePriority::RESIZE_PRIORITY_FOCUSED_APP);

  // FOCUSED_TAB
  balloon_blocker_->TryResize(
      {ResizePriority::RESIZE_PRIORITY_NO_KILL_CANDIDATES_GUEST, -100});
  ASSERT_EQ(balloon_blocker_->LowestUnblockedPriority(ResizeDirection::kInflate,
                                                      now_),
            ResizePriority::RESIZE_PRIORITY_FOCUSED_TAB);

  // NO_KILL_CANDIDATES_GUEST
  balloon_blocker_->TryResize(
      {ResizePriority::RESIZE_PRIORITY_NO_KILL_CANDIDATES_HOST, 100});
  ASSERT_EQ(balloon_blocker_->LowestUnblockedPriority(ResizeDirection::kDeflate,
                                                      now_),
            ResizePriority::RESIZE_PRIORITY_NO_KILL_CANDIDATES_GUEST);

  // After a balloon stall deflation, the balloon should not be unblocked for
  // anything.
  balloon_blocker_->TryResize(
      {ResizePriority::RESIZE_PRIORITY_BALLOON_STALL, -100});
  ASSERT_EQ(balloon_blocker_->LowestUnblockedPriority(ResizeDirection::kInflate,
                                                      now_),
            ResizePriority::RESIZE_PRIORITY_UNSPECIFIED);
}

TEST_F(BalloonBlockerTest, TestLowPriorityBlockDuration) {
  SetBlockPriorityTo(ResizePriority::RESIZE_PRIORITY_CACHED_APP);

  ASSERT_EQ(balloon_blocker_->LowestUnblockedPriority(ResizeDirection::kDeflate,
                                                      now_),
            ResizePriority::RESIZE_PRIORITY_CACHED_TAB);

  // Deflations should be blocked at lowest priority before the block is
  // expired.
  now_ += base::Milliseconds(999);
  ASSERT_EQ(balloon_blocker_->LowestUnblockedPriority(ResizeDirection::kDeflate,
                                                      now_),
            ResizePriority::RESIZE_PRIORITY_CACHED_TAB);

  // And the block should be removed after the block is expired.
  now_ += base::Milliseconds(10);
  ASSERT_EQ(balloon_blocker_->LowestUnblockedPriority(ResizeDirection::kDeflate,
                                                      now_),
            ResizePriority::RESIZE_PRIORITY_LOWEST);
}

TEST_F(BalloonBlockerTest, TestHighPriorityBlockDuration) {
  SetBlockPriorityTo(ResizePriority::RESIZE_PRIORITY_FOCUSED_APP);

  ASSERT_EQ(balloon_blocker_->LowestUnblockedPriority(ResizeDirection::kInflate,
                                                      now_),
            ResizePriority::RESIZE_PRIORITY_FOCUSED_TAB);

  // Deflations should be blocked at lowest priority before the block is
  // expired.
  now_ += base::Milliseconds(99);
  ASSERT_EQ(balloon_blocker_->LowestUnblockedPriority(ResizeDirection::kInflate,
                                                      now_),
            ResizePriority::RESIZE_PRIORITY_FOCUSED_TAB);

  // And the block should be removed after the block is expired.
  now_ += base::Milliseconds(10);
  ASSERT_EQ(balloon_blocker_->LowestUnblockedPriority(ResizeDirection::kInflate,
                                                      now_),
            ResizePriority::RESIZE_PRIORITY_CACHED_TAB);
}

TEST_F(BalloonBlockerTest, TestPriorityFallback) {
  SetBlockPriorityTo(ResizePriority::RESIZE_PRIORITY_FOCUSED_TAB);
  now_ += base::Milliseconds(20);

  // The focused tab block should still apply.
  ASSERT_EQ(balloon_blocker_->LowestUnblockedPriority(ResizeDirection::kDeflate,
                                                      now_),
            ResizePriority::RESIZE_PRIORITY_NO_KILL_CANDIDATES_GUEST);

  // Set an additional cached tab block.
  balloon_blocker_->TryResize({ResizePriority::RESIZE_PRIORITY_CACHED_TAB, 1});

  now_ += base::Milliseconds(90);

  // The focused tab block should be expired.
  ASSERT_EQ(balloon_blocker_->LowestUnblockedPriority(ResizeDirection::kDeflate,
                                                      now_),
            ResizePriority::RESIZE_PRIORITY_PERCEPTIBLE_APP);

  now_ += base::Milliseconds(10000);

  // The cached block should also be expired now.
  ASSERT_EQ(balloon_blocker_->LowestUnblockedPriority(ResizeDirection::kDeflate,
                                                      now_),
            ResizePriority::RESIZE_PRIORITY_LOWEST);
}

TEST_F(BalloonBlockerTest, TestLowPriorityClearsHighPriorityBlock) {
  SetBlockPriorityTo(ResizePriority::RESIZE_PRIORITY_FOCUSED_TAB);

  // Should be unblocked at RESIZE_PRIORITY_NO_KILL_CANDIDATES_GUEST
  ASSERT_EQ(balloon_blocker_->LowestUnblockedPriority(ResizeDirection::kDeflate,
                                                      now_),
            ResizePriority::RESIZE_PRIORITY_NO_KILL_CANDIDATES_GUEST);

  // A lower priority inflation request should un-do the higher priority
  // deflation blocks.
  balloon_blocker_->TryResize({ResizePriority::RESIZE_PRIORITY_CACHED_TAB, 1});
  ASSERT_EQ(balloon_blocker_->LowestUnblockedPriority(ResizeDirection::kDeflate,
                                                      now_),
            ResizePriority::RESIZE_PRIORITY_PERCEPTIBLE_APP);
}

TEST_F(BalloonBlockerTest, TestBalloonStallSetsCorrectBlock) {
  // Nothing should be blocked by default.
  ASSERT_EQ(balloon_blocker_->LowestUnblockedPriority(ResizeDirection::kDeflate,
                                                      now_),
            ResizePriority::RESIZE_PRIORITY_LOWEST);

  leaked_fake_balloon_->RunStallCallback({
      .success = true,
      .actual_delta_bytes = -MiB(16),
      .new_target = 0,
  });

  // After a balloon stall, inflations should be blocked at the highest
  // priority.
  ASSERT_EQ(balloon_blocker_->LowestUnblockedPriority(ResizeDirection::kInflate,
                                                      now_),
            ResizePriority::RESIZE_PRIORITY_UNSPECIFIED);
}

TEST_F(BalloonBlockerTest, TestUnderContentionSizeLimit) {
  SetBlockPriorityTo(ResizePriority::RESIZE_PRIORITY_FOCUSED_TAB);

  // Since the balloon is blocked at a high level, the balloon should
  // be under contention and a large request should be capped at 256MB.
  ASSERT_EQ(
      balloon_blocker_->TryResize(
          {ResizePriority::RESIZE_PRIORITY_NO_KILL_CANDIDATES_HOST, MiB(512)}),
      MiB(256));
}

TEST_F(BalloonBlockerTest, TestDeflateBelowZero) {
  // First inflate the balloon by some amount.
  ASSERT_EQ(balloon_blocker_->TryResize(
                {ResizePriority::RESIZE_PRIORITY_LOWEST, MiB(128)}),
            MiB(128));

  // A deflation larger than the previous inflation should not deflate below 0.
  ASSERT_EQ(balloon_blocker_->TryResize(
                {ResizePriority::RESIZE_PRIORITY_HIGHEST, -MiB(256)}),
            -MiB(128));
}

}  // namespace
}  // namespace vm_tools::concierge::mm
