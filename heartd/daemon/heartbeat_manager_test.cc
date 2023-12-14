// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "heartd/daemon/heartbeat_manager.h"

#include <memory>
#include <utility>

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

using ::testing::_;
using ::testing::Exactly;

class HeartbeatManagerTest : public testing::Test {
 public:
  HeartbeatManagerTest() {
    heartbeat_manager_ = std::make_unique<HeartbeatManager>();
  }
  ~HeartbeatManagerTest() override = default;

  void EstablishHeartbeatTracker() {
    auto argument = mojom::HeartbeatServiceArgument::New();

    heartbeat_manager_->EstablishHeartbeatTracker(
        mojom::ServiceName::kKiosk, pacemaker_.BindNewPipeAndPassReceiver(),
        std::move(argument));
  }

  void SendHeartbeatSync() {
    base::test::TestFuture<void> test_future;
    pacemaker_->SendHeartbeat(test_future.GetCallback());
    if (!test_future.Wait()) {
      NOTREACHED_NORETURN();
    }
    return;
  }

 protected:
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  mojo::Remote<mojom::Pacemaker> pacemaker_;
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

}  // namespace

}  // namespace heartd
