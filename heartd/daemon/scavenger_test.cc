// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "heartd/daemon/scavenger.h"

#include <memory>
#include <utility>

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

class ScavengerTest : public testing::Test {
 public:
  ScavengerTest() {
    heartbeat_manager_ = std::make_unique<HeartbeatManager>(&action_runner_);
    scavenger_ = std::make_unique<Scavenger>(
        base::BindOnce(&ScavengerTest::QuitCallback, base::Unretained(this)),
        heartbeat_manager_.get());
  }
  ~ScavengerTest() override = default;

  void QuitCallback() { quit_callback_is_called_ = true; }

  bool IsQuitCallbackCalled() { return quit_callback_is_called_; }

  void EstablishHeartbeatTracker() {
    auto argument = mojom::HeartbeatServiceArgument::New();
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
  std::unique_ptr<Scavenger> scavenger_ = nullptr;

 private:
  bool quit_callback_is_called_ = false;
};

TEST_F(ScavengerTest, CallbackIsCalledAfterAnHour) {
  EXPECT_FALSE(IsQuitCallbackCalled());

  EstablishHeartbeatTracker();
  scavenger_->Start();
  EXPECT_FALSE(IsQuitCallbackCalled());

  base::test::TestFuture<void> test_future;
  pacemaker_->StopMonitor(test_future.GetCallback());
  if (!test_future.Wait()) {
    NOTREACHED_NORETURN();
  }

  task_environment_.FastForwardBy(kScavengerPeriod);
  task_environment_.RunUntilIdle();
  EXPECT_TRUE(IsQuitCallbackCalled());
}

}  // namespace

}  // namespace heartd
