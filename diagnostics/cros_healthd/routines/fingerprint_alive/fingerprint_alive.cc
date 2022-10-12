// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/fingerprint_alive/fingerprint_alive.h"

#include <utility>

#include <base/callback.h>
#include <base/logging.h>

#include "diagnostics/mojom/public/cros_healthd_diagnostics.mojom.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

}  // namespace

FingerprintAliveRoutine::FingerprintAliveRoutine(Context* context)
    : context_(context), status_(mojom::DiagnosticRoutineStatusEnum::kReady) {}

FingerprintAliveRoutine::~FingerprintAliveRoutine() = default;

void FingerprintAliveRoutine::Start() {
  status_ = mojom::DiagnosticRoutineStatusEnum::kRunning;
  context_->executor()->GetFingerprintInfo(base::BindOnce(
      &FingerprintAliveRoutine::ExamineInfo, base::Unretained(this)));
}

void FingerprintAliveRoutine::Resume() {}

void FingerprintAliveRoutine::Cancel() {}

void FingerprintAliveRoutine::PopulateStatusUpdate(
    mojom::RoutineUpdate* response, bool include_output) {
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

mojom::DiagnosticRoutineStatusEnum FingerprintAliveRoutine::GetStatus() {
  return status_;
}

void FingerprintAliveRoutine::ExamineInfo(
    mojom::FingerprintInfoResultPtr result,
    const std::optional<std::string>& err) {
  if (err.has_value()) {
    status_ = mojom::DiagnosticRoutineStatusEnum::kFailed;
    status_message_ = err.value();
    return;
  }

  if (!result) {
    status_ = mojom::DiagnosticRoutineStatusEnum::kFailed;
    status_message_ = "Failed to get fingerprint info.";
    return;
  }

  // The firmware copy should be RW in a normal state.
  if (!result->rw_fw) {
    status_ = mojom::DiagnosticRoutineStatusEnum::kFailed;
    status_message_ = "Fingerprint does not use a RW firmware copy.";
    return;
  }

  status_ = mojom::DiagnosticRoutineStatusEnum::kPassed;
}

}  // namespace diagnostics
