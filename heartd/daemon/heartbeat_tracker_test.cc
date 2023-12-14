// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "heartd/daemon/heartbeat_tracker.h"

#include <memory>
#include <utility>
#include <vector>

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

 protected:
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  mojo::Remote<mojom::Pacemaker> pacemaker_;
  std::unique_ptr<HeartbeatTracker> heartbeat_tracker_ = nullptr;
};

TEST_F(HeartbeatTrackerTest, DefaultValueAfterCreation) {
  EXPECT_TRUE(heartbeat_tracker_->IsPacemakerBound());
  EXPECT_FALSE(heartbeat_tracker_->IsStopMonitor());
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

}  // namespace

}  // namespace heartd
