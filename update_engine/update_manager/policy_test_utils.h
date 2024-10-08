// Copyright 2017 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_UPDATE_MANAGER_POLICY_TEST_UTILS_H_
#define UPDATE_ENGINE_UPDATE_MANAGER_POLICY_TEST_UTILS_H_

#include <memory>
#include <string>

#include <base/time/time.h>
#include <brillo/message_loops/fake_message_loop.h>
#include <gtest/gtest.h>

#include "update_engine/common/fake_clock.h"
#include "update_engine/update_manager/evaluation_context.h"
#include "update_engine/update_manager/fake_state.h"
#include "update_engine/update_manager/policy_evaluator.h"
#include "update_engine/update_manager/policy_utils.h"

namespace chromeos_update_manager {

class UmPolicyTestBase : public ::testing::Test {
 protected:
  UmPolicyTestBase() = default;

  void SetUp() override;

  void TearDown() override;

  // Sets the clock to fixed values.
  virtual void SetUpDefaultClock();

  // Sets the fake time provider to the time given by the fake clock.
  virtual void SetUpDefaultTimeProvider();

  // Sets up the default state in fake_state_.  override to add Policy-specific
  // items, but only after calling this class's implementation.
  virtual void SetUpDefaultState();

  // Runs the passed |method| after resetting the EvaluationContext and expects
  // it to return the |expected| return value.
  template <typename T, typename R, typename... Args>
  void ExpectStatus(EvalStatus expected, T method, R* result, Args... args) {
    std::string error = "<None>";
    eval_ctx_->ResetEvaluation();
    EXPECT_EQ(expected,
              (*method)(eval_ctx_.get(), &fake_state_, &error, result, args...))
        << "Returned error: " << error
        << "\nEvaluation context: " << eval_ctx_->DumpContext();
  }

  // Runs the passed |method| after resetting the EvaluationContext, in order
  // to use the method to get a value for other testing (doesn't validate the
  // return value, just returns it).
  template <typename T, typename R, typename... Args>
  EvalStatus CallMethodWithContext(T method, R* result, Args... args) {
    std::string error = "<None>";
    eval_ctx_->ResetEvaluation();
    return (*method)(eval_ctx_.get(), &fake_state_, &error, result, args...);
  }

  brillo::FakeMessageLoop loop_{nullptr};
  chromeos_update_engine::FakeClock* fake_clock_;
  FakeState fake_state_;
  std::shared_ptr<EvaluationContext> eval_ctx_;

  std::unique_ptr<PolicyEvaluator> evaluator_;
  std::unique_ptr<PolicyInterface> policy_2_;
  std::shared_ptr<PolicyDataInterface> policy_data_;
};

}  // namespace chromeos_update_manager

#endif  // UPDATE_ENGINE_UPDATE_MANAGER_POLICY_TEST_UTILS_H_
