// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/routine_observer_for_testing.h"

#include <optional>
#include <utility>

#include <base/check.h>
#include <base/functional/bind.h>
#include <base/functional/callback.h>
#include <base/test/test_future.h>

#include "diagnostics/mojom/public/cros_healthd_routines.mojom.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

bool IsFinishedState(const mojom::RoutineStatePtr& state) {
  return state->state_union->is_finished();
}

bool IsWaitingState(const mojom::RoutineStatePtr& state) {
  return state->state_union->is_waiting();
}

}  // namespace

RoutineObserverForTesting::RoutineObserverForTesting() = default;

RoutineObserverForTesting::~RoutineObserverForTesting() = default;

void RoutineObserverForTesting::OnRoutineStateChange(
    mojom::RoutineStatePtr state) {
  CHECK(state);
  state_ = std::move(state);
  if (state_action_) {
    if (state_action_->is_condition_satisfied.Run(state_)) {
      CHECK(state_action_->on_condition_satisfied);
      std::move(state_action_->on_condition_satisfied).Run();
      state_action_.reset();
    }
  }
}

void RoutineObserverForTesting::WaitUntilRoutineFinished() {
  if (!state_.is_null() && IsFinishedState(state_)) {
    return;
  }
  CHECK(!state_action_) << "Can set only one state action";
  base::test::TestFuture<void> signal;
  state_action_ = {
      .is_condition_satisfied = base::BindRepeating(&IsFinishedState),
      .on_condition_satisfied = signal.GetCallback()};
  EXPECT_TRUE(signal.Wait());
}

void RoutineObserverForTesting::WaitUntilRoutineWaiting() {
  if (!state_.is_null() && IsWaitingState(state_)) {
    return;
  }
  CHECK(!state_action_) << "Can set only one state action";
  base::test::TestFuture<void> signal;
  state_action_ = {
      .is_condition_satisfied = base::BindRepeating(&IsWaitingState),
      .on_condition_satisfied = signal.GetCallback()};
  EXPECT_TRUE(signal.Wait());
}

}  // namespace diagnostics
