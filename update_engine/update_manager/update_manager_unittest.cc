//
// Copyright (C) 2014 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include "update_engine/update_manager/update_manager.h"

#include <unistd.h>

#include <algorithm>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <base/functional/bind.h>
#include <base/test/simple_test_clock.h>
#include <base/time/time.h>
#include <brillo/message_loops/fake_message_loop.h>
#include <brillo/message_loops/message_loop.h>
#include <brillo/message_loops/message_loop_utils.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "update_engine/cros/fake_system_state.h"
#include "update_engine/update_manager/fake_state.h"
#include "update_engine/update_manager/umtest_utils.h"

using base::Time;
using brillo::MessageLoop;
using brillo::MessageLoopRunMaxIterations;
using chromeos_update_engine::ErrorCode;
using chromeos_update_engine::FakeClock;
using chromeos_update_engine::FakeSystemState;
using std::pair;
using std::string;
using std::tuple;
using std::unique_ptr;
using std::vector;

namespace {

class FakeUpdateTimeRestrictionsMonitorDelegate
    : public chromeos_update_manager::UpdateTimeRestrictionsMonitor::Delegate {
  void OnRestrictedIntervalStarts() override {}
};

}  // namespace

namespace chromeos_update_manager {

class UmUpdateManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    loop_.SetAsCurrent();
    FakeSystemState::CreateInstance();
    fake_state_ = new FakeState();
    umut_.reset(
        new UpdateManager(base::Seconds(5), base::Seconds(1), fake_state_));
  }

  void TearDown() override { EXPECT_FALSE(loop_.PendingTasks()); }

  base::SimpleTestClock test_clock_;
  brillo::FakeMessageLoop loop_{&test_clock_};
  FakeState* fake_state_;  // Owned by the umut_.
  unique_ptr<UpdateManager> umut_;
};

class SimplePolicy : public PolicyInterface {
 public:
  SimplePolicy() = default;
  EvalStatus Evaluate(EvaluationContext* ec,
                      State* state,
                      string* error,
                      PolicyDataInterface* data) const override {
    return EvalStatus::kSucceeded;
  }

 protected:
  string PolicyName() const override { return "SimplePolicy"; }
};

// The FailingPolicy implements a single method and make it always fail. This
// class extends the DefaultPolicy class to allow extensions of the Policy
// class without extending nor changing this test.
class FailingPolicy : public PolicyInterface {
 public:
  explicit FailingPolicy(int* num_called_p) : num_called_p_(num_called_p) {}
  FailingPolicy() : FailingPolicy(nullptr) {}
  EvalStatus Evaluate(EvaluationContext* ec,
                      State* state,
                      string* error,
                      PolicyDataInterface* data) const override {
    if (num_called_p_)
      (*num_called_p_)++;
    *error = "FailingPolicy failed.";
    return EvalStatus::kFailed;
  }

 protected:
  string PolicyName() const override { return "FailingPolicy"; }

 private:
  int* num_called_p_;
};

// The LazyPolicy always returns EvalStatus::kAskMeAgainLater.
class LazyPolicy : public PolicyInterface {
  EvalStatus Evaluate(EvaluationContext* ec,
                      State* state,
                      string* error,
                      PolicyDataInterface* result) const override {
    return EvalStatus::kAskMeAgainLater;
  }

 protected:
  string PolicyName() const override { return "LazyPolicy"; }
};

// A policy that sleeps for a predetermined amount of time, then checks for a
// wallclock-based time threshold (if given) and returns
// EvalStatus::kAskMeAgainLater if not passed; otherwise, returns
// EvalStatus::kSucceeded. Increments a counter every time it is being queried,
// if a pointer to it is provided.
class DelayPolicy : public PolicyInterface {
 public:
  DelayPolicy(int sleep_secs, Time time_threshold, int* num_called_p)
      : sleep_secs_(sleep_secs),
        time_threshold_(time_threshold),
        num_called_p_(num_called_p) {}
  EvalStatus Evaluate(EvaluationContext* ec,
                      State* state,
                      string* error,
                      PolicyDataInterface* data) const override {
    if (num_called_p_)
      (*num_called_p_)++;

    // Sleep for a predetermined amount of time.
    if (sleep_secs_ > 0)
      sleep(sleep_secs_);

    // Check for a time threshold. This can be used to ensure that the policy
    // has some non-constant dependency.
    if (time_threshold_ < Time::Max() &&
        ec->IsWallclockTimeGreaterThan(time_threshold_))
      return EvalStatus::kSucceeded;

    return EvalStatus::kAskMeAgainLater;
  }

 protected:
  string PolicyName() const override { return "DelayPolicy"; }

 private:
  int sleep_secs_;
  Time time_threshold_;
  int* num_called_p_;
};

// AccumulateCallsCallback() adds to the passed |acc| accumulator vector pairs
// of EvalStatus and T instances. This allows to create a callback that keeps
// track of when it is called and the arguments passed to it, to be used with
// the UpdateManager::AsyncPolicyRequest().
static void AccumulateCallsCallback(vector<EvalStatus>* acc,
                                    EvalStatus status) {
  acc->push_back(status);
}

// Tests that policy requests are completed successfully. It is important that
// this tests cover all policy requests as defined in Policy.
TEST_F(UmUpdateManagerTest, PolicyRequestCallUpdateCheckAllowed) {
  EXPECT_EQ(EvalStatus::kSucceeded,
            umut_->PolicyRequest(std::make_unique<SimplePolicy>(),
                                 std::make_shared<PolicyDataInterface>()));
}

TEST_F(UmUpdateManagerTest, PolicyRequestCallsDefaultOnError) {
  // Tests that the default evaluation is called when the method fails, which
  // will set this as true.
  EXPECT_EQ(EvalStatus::kSucceeded,
            umut_->PolicyRequest(std::make_unique<FailingPolicy>(),
                                 std::make_shared<PolicyDataInterface>()));
}

// This test only applies to debug builds where DCHECK is enabled.
#if DCHECK_IS_ON
TEST_F(UmUpdateManagerTest, PolicyRequestDoesntBlockDeathTest) {
  // The update manager should die (DCHECK) if a policy called synchronously
  // returns a kAskMeAgainLater value.
  PolicyDataInterface data;
  EXPECT_DEATH(umut_->PolicyRequest(std::make_unique<LazyPolicy>(), &data), "");
}
#endif  // DCHECK_IS_ON

TEST_F(UmUpdateManagerTest, AsyncPolicyRequestDelaysEvaluation) {
  // To avoid differences in code execution order between an AsyncPolicyRequest
  // call on a policy that returns AskMeAgainLater the first time and one that
  // succeeds the first time, we ensure that the passed callback is called from
  // the main loop in both cases even when we could evaluate it right now.
  vector<EvalStatus> calls;
  auto callback = base::BindOnce(AccumulateCallsCallback, &calls);

  umut_->PolicyRequest(std::make_unique<FailingPolicy>(),
                       std::make_shared<PolicyDataInterface>(),
                       std::move(callback));
  // The callback should wait until we run the main loop for it to be executed.
  EXPECT_EQ(0U, calls.size());
  MessageLoopRunMaxIterations(MessageLoop::current(), 100);
  EXPECT_EQ(1U, calls.size());
}

TEST_F(UmUpdateManagerTest, AsyncPolicyRequestTimeoutDoesNotFire) {
  // Set up an async policy call to return immediately, then wait a little and
  // ensure that the timeout event does not fire.
  vector<EvalStatus> calls;
  auto callback = base::BindOnce(AccumulateCallsCallback, &calls);

  int num_called = 0;
  umut_->PolicyRequest(std::make_unique<FailingPolicy>(&num_called),
                       std::make_shared<PolicyDataInterface>(),
                       std::move(callback));
  // Run the main loop, ensure that policy was attempted once before deferring
  // to the default.
  MessageLoopRunMaxIterations(MessageLoop::current(), 100);
  EXPECT_EQ(1, num_called);
  ASSERT_EQ(1U, calls.size());
  EXPECT_EQ(EvalStatus::kSucceeded, calls[0]);
  // Wait for the timeout to expire, run the main loop again, ensure that
  // nothing happened.
  test_clock_.Advance(base::Seconds(2));
  MessageLoopRunMaxIterations(MessageLoop::current(), 10);
  EXPECT_EQ(1, num_called);
  EXPECT_EQ(1U, calls.size());
}

TEST_F(UmUpdateManagerTest, AsyncPolicyRequestTimesOut) {
  auto* fake_clock = FakeSystemState::Get()->fake_clock();
  // Set up an async policy call to exceed its expiration timeout, make sure
  // that the default policy was not used (no callback) and that evaluation is
  // reattempted.
  vector<EvalStatus> calls;
  auto callback = base::BindOnce(AccumulateCallsCallback, &calls);

  int num_called = 0;
  auto policy = std::make_unique<DelayPolicy>(
      0, fake_clock->GetWallclockTime() + base::Seconds(3), &num_called);
  umut_->PolicyRequest(std::move(policy),
                       std::make_shared<PolicyDataInterface>(),
                       std::move(callback));
  // Run the main loop, ensure that policy was attempted once but the callback
  // was not invoked.
  MessageLoopRunMaxIterations(MessageLoop::current(), 100);
  EXPECT_EQ(1, num_called);
  EXPECT_EQ(0U, calls.size());
  // Wait for the expiration timeout to expire, run the main loop again,
  // ensure that reevaluation occurred but callback was not invoked (i.e.
  // default policy was not consulted).
  test_clock_.Advance(base::Seconds(2));
  fake_clock->SetWallclockTime(fake_clock->GetWallclockTime() +
                               base::Seconds(2));
  MessageLoopRunMaxIterations(MessageLoop::current(), 10);
  EXPECT_EQ(2, num_called);
  EXPECT_EQ(0U, calls.size());
  // Wait for reevaluation due to delay to happen, ensure that it occurs and
  // that the callback is invoked.
  test_clock_.Advance(base::Seconds(2));
  fake_clock->SetWallclockTime(fake_clock->GetWallclockTime() +
                               base::Seconds(2));
  MessageLoopRunMaxIterations(MessageLoop::current(), 10);
  EXPECT_EQ(3, num_called);
  ASSERT_EQ(1U, calls.size());
  EXPECT_EQ(EvalStatus::kSucceeded, calls[0]);
}

TEST_F(UmUpdateManagerTest, AsyncPolicyRequestIsAddedToList) {
  umut_->PolicyRequest(std::make_unique<SimplePolicy>(),
                       std::make_shared<PolicyDataInterface>(),
                       base::BindOnce([](EvalStatus) {}));
  EXPECT_EQ(1, umut_->evaluators_.size());

  MessageLoopRunMaxIterations(MessageLoop::current(), 10);
  // It should released from the list after the policy is evaluated.
  EXPECT_EQ(0, umut_->evaluators_.size());
}

TEST_F(UmUpdateManagerTest, UpdateTimeRestrictionsMonitorIsNotNeeded) {
  FakeUpdateTimeRestrictionsMonitorDelegate delegate;
  chromeos_update_engine::InstallPlan install_plan;
  EXPECT_FALSE(umut_->BuildUpdateTimeRestrictionsMonitorIfNeeded(install_plan,
                                                                 &delegate));
}

TEST_F(UmUpdateManagerTest, UpdateTimeRestrictionsMonitorIsNeeded) {
  FakeUpdateTimeRestrictionsMonitorDelegate delegate;
  chromeos_update_engine::InstallPlan install_plan;
  install_plan.can_download_be_canceled = true;
  EXPECT_TRUE(umut_->BuildUpdateTimeRestrictionsMonitorIfNeeded(install_plan,
                                                                &delegate));
}

}  // namespace chromeos_update_manager
