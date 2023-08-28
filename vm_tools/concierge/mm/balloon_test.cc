// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/mm/balloon.h"

#include <memory>
#include <vector>

#include <base/test/task_environment.h>
#include <gtest/gtest.h>

#include "vm_tools/concierge/byte_unit.h"
#include "vm_tools/concierge/fake_crosvm_control.h"

namespace vm_tools::concierge::mm {
namespace {

class BalloonTest : public ::testing::Test {
 public:
  void SetUp() override {
    FakeCrosvmControl::Init();
    FakeCrosvmControl::Get()->set_balloon_size_wait_for_result_ = false;

    balloon_ = std::make_unique<Balloon>(
        6, kTestSocket, base::SequencedTaskRunner::GetCurrentDefault(),
        base::BindRepeating(&BalloonTest::Now, base::Unretained(this)));

    balloon_->SetStallCallback(base::BindRepeating(&BalloonTest::OnBalloonStall,
                                                   base::Unretained(this)));

    now_ = base::TimeTicks::Now();
  }

  void TearDown() override { CrosvmControl::Reset(); }

 protected:
  static constexpr char kTestSocket[] = "/run/test-socket";

  void OnBalloonStall(Balloon::ResizeResult result) {
    balloon_stall_results_.emplace_back(result);
  }

  void ReturnBalloonSize(uint64_t size) {
    FakeCrosvmControl::Get()->actual_balloon_size_ = size;
  }

  void AssertBalloonSizedTo(uint64_t size) {
    ASSERT_EQ(FakeCrosvmControl::Get()->target_balloon_size_, size);
    ASSERT_EQ(FakeCrosvmControl::Get()->count_set_balloon_size_,
              balloon_op_count_);
    ASSERT_STREQ(FakeCrosvmControl::Get()->target_socket_path_.c_str(),
                 kTestSocket);
    balloon_op_count_++;
  }

  void DoResize(int64_t delta_bytes) {
    balloon_->DoResize(delta_bytes, base::BindOnce(&BalloonTest::OnResizeResult,
                                                   base::Unretained(this)));
    task_environment_.RunUntilIdle();
  }

  void OnResizeResult(Balloon::ResizeResult result) {
    resize_results_.emplace_back(result);
  }

  void FastForwardBy(base::TimeDelta duration) {
    now_ += duration;
    task_environment_.FastForwardBy(duration);
  }

  base::TimeTicks Now() { return now_; }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  std::unique_ptr<Balloon> balloon_{};
  std::vector<Balloon::ResizeResult> resize_results_{};
  std::vector<Balloon::ResizeResult> balloon_stall_results_{};
  base::TimeTicks now_{};
  int balloon_op_count_ = 1;
};

TEST_F(BalloonTest, TestResizeFailureFails) {
  FakeCrosvmControl::Get()->result_set_balloon_size_ = false;

  DoResize(100);

  ASSERT_EQ(resize_results_.size(), 1);
  ASSERT_FALSE(resize_results_.back().success);
}

TEST_F(BalloonTest, TestDeflateFromZero) {
  ReturnBalloonSize(0);

  DoResize(-10);

  ASSERT_EQ(resize_results_.size(), 1);
  ASSERT_TRUE(resize_results_.back().success);
  ASSERT_EQ(resize_results_.back().actual_delta_bytes, 0);
}

TEST_F(BalloonTest, TestDeflateBelowZero) {
  ReturnBalloonSize(0);

  DoResize(64);

  AssertBalloonSizedTo(64);
  ASSERT_EQ(resize_results_.size(), 1);
  ASSERT_TRUE(resize_results_.back().success);
  ASSERT_EQ(resize_results_.back().actual_delta_bytes, 64);

  ReturnBalloonSize(64);

  // A deflate that would take the balloon below 0 bytes should only deflate
  // down to 0.
  DoResize(-128);
  AssertBalloonSizedTo(0);
  ASSERT_EQ(resize_results_.size(), 2);
  ASSERT_TRUE(resize_results_.back().success);
  ASSERT_EQ(resize_results_.back().actual_delta_bytes, -64);
}

TEST_F(BalloonTest, TestResizeWhenBalloonIsBehindSchedule) {
  ReturnBalloonSize(0);

  DoResize(128);

  AssertBalloonSizedTo(128);

  // Return 120 as the current balloon size even though it was inflated to 128
  // before. This can happen if several resize requests are made in quick
  // succession.
  ReturnBalloonSize(120);

  DoResize(128);

  // Even though the balloon did not complete the previous inflation, it should
  // still be inflated to the sum of the two operations.
  AssertBalloonSizedTo(256);
}

TEST_F(BalloonTest, TestBalloonStallIgnoredForShortTime) {
  ReturnBalloonSize(0);

  DoResize(MiB(128));
  AssertBalloonSizedTo(MiB(128));

  now_ += base::Milliseconds(1);

  ReturnBalloonSize(MiB(1));

  // Even if the balloon isn't the correct size, it has only been 1ms so a
  // balloon stall can't be confirmed.
  DoResize(MiB(128));

  // Balloon should be sized to the actual size plus 128 MIB.
  AssertBalloonSizedTo(MiB(256));

  // Only two resize operations should have been performed. If a balloon stall
  // was detected there would have been 3.
  ASSERT_EQ(FakeCrosvmControl::Get()->count_set_balloon_size_, 2);

  ASSERT_EQ(balloon_stall_results_.size(), 0);
}

TEST_F(BalloonTest, TestBalloonStallDetectedAndCorrected) {
  DoResize(MiB(256));
  AssertBalloonSizedTo(MiB(256));

  // The previous inflation should have queued a stall check for 5 seconds in
  // the future. Fast forward to run the stall check. At this point the
  // inflation rate is still above the target so the stall should not be
  // triggered.
  ReturnBalloonSize(MiB(128));
  FastForwardBy(base::Seconds(6));
  ASSERT_EQ(balloon_stall_results_.size(), 0);

  // 100 more seconds in the future and the balloon has not inflated any more.
  // This should be detected as a stall.
  FastForwardBy(base::Seconds(100));

  // The current stall back off is 128 MiB, so since the balloon stalled at 128
  // MiB it should be deflated down to 0.
  AssertBalloonSizedTo(0);

  ASSERT_EQ(balloon_stall_results_.size(), 1);
}

TEST_F(BalloonTest, TestBalloonStallDetectionOnlyRunsOnce) {
  // Perform 2 back-to-back inflations.
  DoResize(MiB(256));
  AssertBalloonSizedTo(MiB(256));
  ReturnBalloonSize(MiB(128));
  DoResize(MiB(128));
  AssertBalloonSizedTo(MiB(384));

  int initial_stats_count = FakeCrosvmControl::Get()->count_balloon_stats_;

  // Even though two inflations were performed, only one balloon stall check
  // should have been run.
  FastForwardBy(base::Seconds(6));
  ASSERT_EQ(initial_stats_count + 1,
            FakeCrosvmControl::Get()->count_balloon_stats_);
}

TEST_F(BalloonTest, TestBalloonInflationsAreBasedOnTargetSize) {
  ReturnBalloonSize(0);

  DoResize(MiB(512));
  AssertBalloonSizedTo(MiB(512));

  // Even though the balloon was sized to 512MB earlier, pretend is has not
  // caught up and only return 256MB as the current size.
  ReturnBalloonSize(MiB(256));

  DoResize(MiB(512));

  // Even though the actual balloon size is only 256, the balloon should be
  // sized based off of the target size.
  AssertBalloonSizedTo(MiB(1024));
}

TEST_F(BalloonTest, TestBalloonInflationsAreBasedOnActualSizeWhenDeflating) {
  ReturnBalloonSize(0);

  DoResize(MiB(512));

  AssertBalloonSizedTo(MiB(512));

  ReturnBalloonSize(MiB(512));

  DoResize(-MiB(512));

  AssertBalloonSizedTo(0);

  // Currently the balloon is deflating down to 0, but hasn't made it yet. In
  // this case when an inflation is requested it should be based off of the
  // actual size not the target size.
  ReturnBalloonSize(MiB(256));

  DoResize(MiB(16));

  AssertBalloonSizedTo(MiB(272));
}

TEST_F(BalloonTest, TestBalloonDeflationsAreAlwaysBasedOffActualSize) {
  ReturnBalloonSize(0);

  DoResize(MiB(512));
  AssertBalloonSizedTo(MiB(512));

  ReturnBalloonSize(MiB(256));
  DoResize(-MiB(64));
  // A deflation when the balloon is inflating should be based off the current
  // size (256MB - 64MB)
  AssertBalloonSizedTo(MiB(192));

  ReturnBalloonSize(MiB(200));
  DoResize(-MiB(64));
  // And a deflation when the balloon is already deflating should also be based
  // off the current size (200MB - 64MB).
  AssertBalloonSizedTo(MiB(136));
}

TEST_F(BalloonTest, TestGetBalloonSizeIsNotCalledSynchronously) {
  balloon_->DoResize(500, base::DoNothing());
  // A call to DoResize() should not synchronously get the balloon stats.
  ASSERT_EQ(FakeCrosvmControl::Get()->count_balloon_stats_, 0);

  // Getting the target balloon size should also not call into crosvm for the
  // actual size.
  balloon_->GetTargetSize();
  ASSERT_EQ(FakeCrosvmControl::Get()->count_balloon_stats_, 0);
}

}  // namespace
}  // namespace vm_tools::concierge::mm
