// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/base_routine_control.h"

#include <utility>

#include <base/check.h>
#include <mojo/public/cpp/bindings/pending_remote.h>

#include "diagnostics/mojom/public/cros_healthd_exception.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_routines.mojom.h"

namespace {

namespace mojom = ash::cros_healthd::mojom;

}

namespace diagnostics {

BaseRoutineControl::BaseRoutineControl() {
  state_ = mojom::RoutineState::New();
  state_->percentage = 0;
  state_->state_union = mojom::RoutineStateUnion::NewInitialized(
      mojom::RoutineStateInitialized::New());
}

BaseRoutineControl::~BaseRoutineControl() = default;

void BaseRoutineControl::Start() {
  CHECK(!on_exception_.is_null())
      << "Must call SetOnExceptionCallback before starting the routine, and "
         "exception can only be raised once";
  // The routine should only be started once.
  if (!state_->state_union->is_initialized()) {
    LOG(ERROR) << "Routine Control is started more than once";
    return;
  }
  state_->state_union =
      mojom::RoutineStateUnion::NewRunning(mojom::RoutineStateRunning::New());
  NotifyObserver();
  OnStart();
}

void BaseRoutineControl::GetState(
    BaseRoutineControl::GetStateCallback callback) {
  std::move(callback).Run(state_.Clone());
}

void BaseRoutineControl::SetOnExceptionCallback(
    ExceptionCallback on_exception) {
  on_exception_ = std::move(on_exception);
}

void BaseRoutineControl::SetObserver(
    mojo::PendingRemote<ash::cros_healthd::mojom::RoutineObserver> observer) {
  observer_.Bind(std::move(observer));
  NotifyObserver();
}

const mojom::RoutineStatePtr& BaseRoutineControl::state() {
  return state_;
}

void BaseRoutineControl::NotifyObserver() {
  if (observer_.is_bound()) {
    observer_->OnRoutineStateChange(state_->Clone());
  }
}

void BaseRoutineControl::RaiseException(const std::string& reason) {
  CHECK(!on_exception_.is_null())
      << "Must call SetOnExceptionCallback before starting the routine, and "
         "exception can only be raised once";
  std::move(on_exception_)
      .Run(static_cast<uint32_t>(mojom::Exception::Reason::kUnexpected),
           reason);
}

void BaseRoutineControl::SetPercentage(uint8_t percentage) {
  CHECK(percentage > state_->percentage && percentage < 100 &&
        state_->state_union->is_running())
      << "Percentage should only increase, is between 0 and 99, and can only "
         "be changed in running state";
  state_->percentage = percentage;
  NotifyObserver();
}

void BaseRoutineControl::SetRunningState() {
  CHECK(state_->state_union->is_waiting() || state_->state_union->is_running())
      << "Can only set running state from waiting state or running state";
  state_->state_union =
      mojom::RoutineStateUnion::NewRunning(mojom::RoutineStateRunning::New());
  NotifyObserver();
}

void BaseRoutineControl::SetWaitingState(
    mojom::RoutineStateWaiting::Reason reason, const std::string& message) {
  CHECK(state_->state_union->is_running())
      << "Can only set waiting state from running state";
  state_->state_union = mojom::RoutineStateUnion::NewWaiting(
      mojom::RoutineStateWaiting::New(reason, message));
  NotifyObserver();
}

void BaseRoutineControl::SetFinishedState(bool has_passed,
                                          mojom::RoutineDetailPtr detail) {
  CHECK(state_->state_union->is_running())
      << "Can only set finished state from running state";
  state_->percentage = 100;
  state_->state_union = mojom::RoutineStateUnion::NewFinished(
      mojom::RoutineStateFinished::New(has_passed, std::move(detail)));
  NotifyObserver();
}
}  // namespace diagnostics
