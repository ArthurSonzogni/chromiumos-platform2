// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "heartd/daemon/heartbeat_manager.h"

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

#include "heartd/daemon/test_utils/mock_dbus_connector.h"
#include "heartd/mojom/heartd.mojom.h"

namespace heartd {

namespace {

namespace mojom = ::ash::heartd::mojom;

using ::testing::_;
using ::testing::Exactly;

class HeartbeatManagerTest : public testing::Test {
 public:
  HeartbeatManagerTest() {
    heartbeat_manager_ = std::make_unique<HeartbeatManager>(&action_runner_);
  }
  ~HeartbeatManagerTest() override = default;

  void EstablishHeartbeatTracker() {
    auto action_reboot = mojom::Action::New(
        /*failure_count = */ 2, mojom::ActionType::kNormalReboot);
    auto argument = mojom::HeartbeatServiceArgument::New();

    argument->actions.push_back(std::move(action_reboot));
    heartbeat_manager_->EstablishHeartbeatTracker(
        mojom::ServiceName::kKiosk, pacemaker_.BindNewPipeAndPassReceiver(),
        std::move(argument));
  }

 protected:
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  mojo::Remote<mojom::Pacemaker> pacemaker_;
  MockDbusConnector mock_dbus_connector_;
  ActionRunner action_runner_{&mock_dbus_connector_};
  std::unique_ptr<HeartbeatManager> heartbeat_manager_ = nullptr;
};

TEST_F(HeartbeatManagerTest, IsPacemakerBound) {
  EXPECT_FALSE(
      heartbeat_manager_->IsPacemakerBound(mojom::ServiceName::kKiosk));

  EstablishHeartbeatTracker();

  EXPECT_TRUE(heartbeat_manager_->IsPacemakerBound(mojom::ServiceName::kKiosk));
}

TEST_F(HeartbeatManagerTest, RebindPacemaker) {
  EstablishHeartbeatTracker();

  pacemaker_.reset();
  task_environment_.RunUntilIdle();
  EXPECT_FALSE(
      heartbeat_manager_->IsPacemakerBound(mojom::ServiceName::kKiosk));

  // Rebind pacemaker.
  EstablishHeartbeatTracker();

  EXPECT_TRUE(heartbeat_manager_->IsPacemakerBound(mojom::ServiceName::kKiosk));
}

TEST_F(HeartbeatManagerTest, RemoveUnusedHeartbeatTrackers) {
  EXPECT_FALSE(heartbeat_manager_->AnyHeartbeatTracker());
  EstablishHeartbeatTracker();
  EXPECT_TRUE(heartbeat_manager_->AnyHeartbeatTracker());

  base::test::TestFuture<void> test_future;
  pacemaker_->StopMonitor(test_future.GetCallback());
  if (!test_future.Wait()) {
    NOTREACHED_NORETURN();
  }

  // This should remove the unused heartbeat trackers.
  heartbeat_manager_->VerifyHeartbeatAndTakeAction();
  EXPECT_FALSE(
      heartbeat_manager_->IsPacemakerBound(mojom::ServiceName::kKiosk));
  EXPECT_FALSE(heartbeat_manager_->AnyHeartbeatTracker());
}

TEST_F(HeartbeatManagerTest, TakeRebootAction) {
  action_runner_.EnableNormalRebootAction();
  EstablishHeartbeatTracker();
  EXPECT_CALL(*mock_dbus_connector_.power_manager_proxy(),
              RequestRestartAsync(_, _, _, _, _))
      .Times(Exactly(1));

  // Forward the time so that the gap of the current time and the previous
  // heartbeat will exceed the threshold.
  task_environment_.FastForwardBy(base::Minutes(3));
  heartbeat_manager_->VerifyHeartbeatAndTakeAction();
  heartbeat_manager_->VerifyHeartbeatAndTakeAction();
}

TEST_F(HeartbeatManagerTest, NotEnableRebootAction) {
  EstablishHeartbeatTracker();
  EXPECT_CALL(*mock_dbus_connector_.power_manager_proxy(),
              RequestRestartAsync(_, _, _, _, _))
      .Times(Exactly(0));

  // Forward the time so that the gap of the current time and the previous
  // heartbeat will exceed the threshold.
  task_environment_.FastForwardBy(base::Minutes(3));
  heartbeat_manager_->VerifyHeartbeatAndTakeAction();
  heartbeat_manager_->VerifyHeartbeatAndTakeAction();
}

}  // namespace

}  // namespace heartd
