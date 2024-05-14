// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "heartd/daemon/sheriffs/heartbeat_verifier.h"

#include <memory>

#include <base/test/task_environment.h>
#include <base/time/time.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "heartd/daemon/heartbeat_manager.h"

namespace heartd {

namespace {

class FakeHeartbeatManager : public HeartbeatManager {
 public:
  FakeHeartbeatManager() : HeartbeatManager(nullptr) {}
  FakeHeartbeatManager(const FakeHeartbeatManager&) = delete;
  FakeHeartbeatManager& operator=(const FakeHeartbeatManager&) = delete;
  ~FakeHeartbeatManager() override{};

  // heartd::HeartbeatManager override:
  bool AnyHeartbeatTracker() override { return has_heartbeat_tracker_; }
  void VerifyHeartbeatAndTakeAction() override {
    ++number_verify_heartbeat_and_take_action_called_;
  }

  int number_verify_heartbeat_and_take_action_called() {
    return number_verify_heartbeat_and_take_action_called_;
  }

  void SetHasHeartbeatTracker() { has_heartbeat_tracker_ = true; }

 private:
  bool has_heartbeat_tracker_ = false;
  int number_verify_heartbeat_and_take_action_called_ = 0;
};

class HeartbeatVerifierTest : public testing::Test {
 public:
  HeartbeatVerifierTest() {
    heartbeat_manager_ = std::make_unique<FakeHeartbeatManager>();
    heartbeat_verifier_ =
        std::make_unique<HeartbeatVerifier>(heartbeat_manager_.get());
  }

  ~HeartbeatVerifierTest() override = default;

 protected:
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  std::unique_ptr<FakeHeartbeatManager> heartbeat_manager_ = nullptr;
  std::unique_ptr<HeartbeatVerifier> heartbeat_verifier_ = nullptr;
};

TEST_F(HeartbeatVerifierTest, HasHeartbeatTracker) {
  heartbeat_manager_->SetHasHeartbeatTracker();

  heartbeat_verifier_->GetToWork();
  EXPECT_TRUE(heartbeat_verifier_->IsWorking());
  task_environment_.FastForwardBy(base::Minutes(120));
  EXPECT_EQ(
      heartbeat_manager_->number_verify_heartbeat_and_take_action_called(),
      120);
}

TEST_F(HeartbeatVerifierTest, NoHeartbeatTracker) {
  heartbeat_verifier_->GetToWork();
  task_environment_.FastForwardBy(base::Minutes(1));
  EXPECT_FALSE(heartbeat_verifier_->IsWorking());
}

}  // namespace

}  // namespace heartd
