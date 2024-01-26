// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "heartd/daemon/heartbeat_tracker.h"

#include <memory>
#include <utility>
#include <vector>

#include "base/functional/callback_forward.h"
#include <base/notreached.h>
#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <base/time/time.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <mojo/public/cpp/bindings/remote.h>

#include "heartd/mojom/heartd.mojom.h"

namespace heartd {

namespace {

namespace mojom = ::ash::heartd::mojom;

class HeartbeatTrackerTest : public testing::Test {
 public:
  HeartbeatTrackerTest() {
    heartbeat_tracker_ = std::make_unique<HeartbeatTracker>(
        mojom::ServiceName::kKiosk, pacemaker_.BindNewPipeAndPassReceiver());
  }
  ~HeartbeatTrackerTest() override = default;

  mojom::HeartbeatResponse SendHeartbeatSync() {
    base::test::TestFuture<mojom::HeartbeatResponse> test_future;
    pacemaker_->SendHeartbeat(test_future.GetCallback());
    if (!test_future.Wait()) {
      NOTREACHED_NORETURN();
    }
    return test_future.Get();
  }

 protected:
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  mojo::Remote<mojom::Pacemaker> pacemaker_;
  std::unique_ptr<HeartbeatTracker> heartbeat_tracker_ = nullptr;
};

TEST_F(HeartbeatTrackerTest, DefaultValueAfterCreation) {
  EXPECT_TRUE(heartbeat_tracker_->IsPacemakerBound());
  EXPECT_FALSE(heartbeat_tracker_->IsStopMonitor());
  EXPECT_EQ(heartbeat_tracker_->GetFailureCount(), 0);
}

TEST_F(HeartbeatTrackerTest, PacemakerStopMonitor) {
  base::test::TestFuture<void> test_future;

  pacemaker_->StopMonitor(test_future.GetCallback());
  if (!test_future.Wait()) {
    NOTREACHED_NORETURN();
  }

  EXPECT_TRUE(heartbeat_tracker_->IsStopMonitor());
}

TEST_F(HeartbeatTrackerTest, PacemakerDisconnect) {
  pacemaker_.reset();
  task_environment_.RunUntilIdle();
  EXPECT_FALSE(heartbeat_tracker_->IsPacemakerBound());
}

TEST_F(HeartbeatTrackerTest, RebindPacemaker) {
  EXPECT_TRUE(heartbeat_tracker_->IsPacemakerBound());

  pacemaker_.reset();
  task_environment_.RunUntilIdle();
  EXPECT_FALSE(heartbeat_tracker_->IsPacemakerBound());

  heartbeat_tracker_->RebindPacemaker(pacemaker_.BindNewPipeAndPassReceiver());
  EXPECT_TRUE(heartbeat_tracker_->IsPacemakerBound());
}

TEST_F(HeartbeatTrackerTest, VerifyTimeGap) {
  mojom::HeartbeatResponse resp = SendHeartbeatSync();
  EXPECT_EQ(resp, mojom::HeartbeatResponse::kSuccess);
  task_environment_.FastForwardBy(base::Seconds(10));
  EXPECT_TRUE(heartbeat_tracker_->VerifyTimeGap(base::Time().Now()));

  // Without sending heartbeat and move on kMinVerificationWindow seconds.
  task_environment_.FastForwardBy(kMinVerificationWindow);
  EXPECT_FALSE(heartbeat_tracker_->VerifyTimeGap(base::Time().Now()));
}

TEST_F(HeartbeatTrackerTest, SetVerificationWindowArgument) {
  auto argument = mojom::HeartbeatServiceArgument::New();
  auto verification_window_seconds = kMinVerificationWindow.InSeconds() + 10;
  argument->verification_window_seconds = verification_window_seconds;

  heartbeat_tracker_->SetupArgument(std::move(argument));
  mojom::HeartbeatResponse resp = SendHeartbeatSync();
  EXPECT_EQ(resp, mojom::HeartbeatResponse::kSuccess);
  task_environment_.FastForwardBy(base::Seconds(verification_window_seconds));
  EXPECT_TRUE(heartbeat_tracker_->VerifyTimeGap(base::Time().Now()));

  task_environment_.FastForwardBy(base::Seconds(1));
  EXPECT_FALSE(heartbeat_tracker_->VerifyTimeGap(base::Time().Now()));
}

TEST_F(HeartbeatTrackerTest, SetFailureCountActionArgument) {
  auto action_noop = mojom::Action::New(
      /*failure_count = */ 2, mojom::ActionType::kNoOperation);
  auto action_reboot = mojom::Action::New(
      /*failure_count = */ 3, mojom::ActionType::kNormalReboot);

  auto argument = mojom::HeartbeatServiceArgument::New();
  argument->actions.push_back(action_noop->Clone());
  argument->actions.push_back(std::move(action_noop));
  argument->actions.push_back(std::move(action_reboot));

  heartbeat_tracker_->SetupArgument(std::move(argument));
  mojom::HeartbeatResponse resp = SendHeartbeatSync();
  EXPECT_EQ(resp, mojom::HeartbeatResponse::kSuccess);
  task_environment_.FastForwardBy(kMinVerificationWindow + base::Seconds(1));

  // VerifyTimeGap increases the failure count to 1.
  EXPECT_FALSE(heartbeat_tracker_->VerifyTimeGap(base::Time().Now()));
  auto actions = heartbeat_tracker_->GetFailureCountActions();
  std::vector<mojom::ActionType> expected_actions;
  EXPECT_EQ(actions, expected_actions);

  // VerifyTimeGap increases the failure count to 2.
  EXPECT_FALSE(heartbeat_tracker_->VerifyTimeGap(base::Time().Now()));
  actions = heartbeat_tracker_->GetFailureCountActions();
  expected_actions = {mojom::ActionType::kNoOperation,
                      mojom::ActionType::kNoOperation};
  EXPECT_EQ(actions, expected_actions);

  // VerifyTimeGap increases the failure count to 3.
  EXPECT_FALSE(heartbeat_tracker_->VerifyTimeGap(base::Time().Now()));
  actions = heartbeat_tracker_->GetFailureCountActions();
  expected_actions = {mojom::ActionType::kNormalReboot};
  EXPECT_EQ(actions, expected_actions);

  // VerifyTimeGap increases the failure count to 4.
  // We should still get the reboot action.
  EXPECT_FALSE(heartbeat_tracker_->VerifyTimeGap(base::Time().Now()));
  actions = heartbeat_tracker_->GetFailureCountActions();
  expected_actions = {mojom::ActionType::kNormalReboot};
  EXPECT_EQ(actions, expected_actions);
}

TEST_F(HeartbeatTrackerTest, ResetFailureCount) {
  mojom::HeartbeatResponse resp = SendHeartbeatSync();
  EXPECT_EQ(resp, mojom::HeartbeatResponse::kSuccess);
  task_environment_.FastForwardBy(kMinVerificationWindow + base::Seconds(1));
  EXPECT_FALSE(heartbeat_tracker_->VerifyTimeGap(base::Time().Now()));
  EXPECT_EQ(static_cast<int>(heartbeat_tracker_->GetFailureCount()), 1);

  resp = SendHeartbeatSync();
  EXPECT_EQ(resp, mojom::HeartbeatResponse::kSuccess);
  EXPECT_TRUE(heartbeat_tracker_->VerifyTimeGap(base::Time().Now()));
  EXPECT_EQ(static_cast<int>(heartbeat_tracker_->GetFailureCount()), 0);
}

}  // namespace

}  // namespace heartd
