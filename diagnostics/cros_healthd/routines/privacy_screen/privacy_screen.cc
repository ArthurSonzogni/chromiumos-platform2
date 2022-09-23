// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/privacy_screen/privacy_screen.h"

#include <utility>

#include <base/threading/sequenced_task_runner_handle.h>
#include <base/time/time.h>

#include "diagnostics/mojom/external/cros_healthd_internal.mojom.h"

namespace diagnostics {

PrivacyScreenRoutine::PrivacyScreenRoutine(Context* context, bool target_state)
    : context_(context), target_state_(target_state) {}

PrivacyScreenRoutine::~PrivacyScreenRoutine() = default;

void PrivacyScreenRoutine::Start() {
  DCHECK_EQ(status_, mojom::DiagnosticRoutineStatusEnum::kReady);
  status_ = mojom::DiagnosticRoutineStatusEnum::kRunning;

  if (!Initialize()) {
    // Routine status and failure message are already set. Directly exit.
    return;
  }

  // Send a request to browser to set privacy screen state.
  context_->mojo_service()->GetChromiumDataCollector()->SetPrivacyScreenState(
      target_state_, base::BindOnce(&PrivacyScreenRoutine::OnReceiveResponse,
                                    base::Unretained(this)));

  base::SequencedTaskRunnerHandle::Get()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&PrivacyScreenRoutine::ValidateState,
                     base::Unretained(this)),
      // This delay is working as a timeout. The timeout is concerning two
      // checks, failing either of which leads to the failure of routine.
      //
      // - Browser must response before timeout exceeded.
      // - Privacy screen state must have been refreshed before timeout
      //   exceeded.
      base::Milliseconds(1000));
}

void PrivacyScreenRoutine::Resume() {
  // This routine cannot be resumed.
}

void PrivacyScreenRoutine::Cancel() {
  // This routine cannot be cancelled.
}

void PrivacyScreenRoutine::PopulateStatusUpdate(mojom::RoutineUpdate* response,
                                                bool include_output) {
  auto update = mojom::NonInteractiveRoutineUpdate::New();
  update->status = status_;
  update->status_message = status_message_;
  response->routine_update_union =
      mojom::RoutineUpdateUnion::NewNoninteractiveUpdate(std::move(update));
  if (status_ == mojom::DiagnosticRoutineStatusEnum::kReady ||
      status_ == mojom::DiagnosticRoutineStatusEnum::kRunning) {
    response->progress_percent = 0;
  } else {
    response->progress_percent = 100;
  }
}

mojom::DiagnosticRoutineStatusEnum PrivacyScreenRoutine::GetStatus() {
  return status_;
}

bool PrivacyScreenRoutine::Initialize() {
  libdrm_util_ = context_->CreateLibdrmUtil();
  if (!libdrm_util_->Initialize()) {
    // Failing to initialize libdrm_util is an internal error. It is not related
    // to privacy screen.
    status_ = mojom::DiagnosticRoutineStatusEnum::kError;
    status_message_ = kPrivacyScreenRoutineFailedToInitializeLibdrmUtilMessage;
    return false;
  }

  connector_id_ = libdrm_util_->GetEmbeddedDisplayConnectorID();
  return true;
}

void PrivacyScreenRoutine::OnReceiveResponse(bool success) {
  request_processed_ = success;
}

void PrivacyScreenRoutine::ValidateState() {
  if (request_processed_ == std::nullopt) {
    status_ = mojom::DiagnosticRoutineStatusEnum::kFailed;
    status_message_ =
        kPrivacyScreenRoutineBrowserResponseTimeoutExceededMessage;
    return;
  }

  if (!request_processed_) {
    status_ = mojom::DiagnosticRoutineStatusEnum::kFailed;
    status_message_ = kPrivacyScreenRoutineRequestRejectedMessage;
    return;
  }

  [[maybe_unused]] bool privacy_screen_supported;
  bool current_state;
  libdrm_util_->FillPrivacyScreenInfo(connector_id_, &privacy_screen_supported,
                                      &current_state);

  if (current_state != target_state_) {
    status_ = mojom::DiagnosticRoutineStatusEnum::kFailed;
    status_message_ =
        target_state_
            ? kPrivacyScreenRoutineFailedToTurnPrivacyScreenOnMessage
            : kPrivacyScreenRoutineFailedToTurnPrivacyScreenOffMessage;
    return;
  }

  status_ = mojom::DiagnosticRoutineStatusEnum::kPassed;
  status_message_ = kPrivacyScreenRoutineSucceededMessage;
}

}  // namespace diagnostics
