// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/simple_routine.h"

#include <cstdint>
#include <string>
#include <utility>

#include <base/check_op.h>
#include <base/json/json_writer.h>
#include <base/logging.h>
#include <base/strings/string_piece.h>
#include <base/strings/stringprintf.h>

#include "diagnostics/common/mojo_utils.h"
#include "diagnostics/mojom/public/cros_healthd_diagnostics.mojom.h"

namespace diagnostics {

namespace {

namespace mojo_ipc = ::ash::cros_healthd::mojom;

uint32_t CalculateProgressPercent(
    mojo_ipc::DiagnosticRoutineStatusEnum status) {
  // Since simple routines cannot be cancelled, the progress percent can only be
  // 0 or 100.
  if (status == mojo_ipc::DiagnosticRoutineStatusEnum::kPassed ||
      status == mojo_ipc::DiagnosticRoutineStatusEnum::kFailed ||
      status == mojo_ipc::DiagnosticRoutineStatusEnum::kError)
    return 100;
  return 0;
}

}  // namespace

SimpleRoutine::SimpleRoutine(Task task)
    : task_(std::move(task)),
      status_(mojo_ipc::DiagnosticRoutineStatusEnum::kReady) {}

SimpleRoutine::~SimpleRoutine() = default;

void SimpleRoutine::Start() {
  DCHECK_EQ(status_, mojo_ipc::DiagnosticRoutineStatusEnum::kReady);
  status_ = mojo_ipc::DiagnosticRoutineStatusEnum::kRunning;
  std::move(task_).Run(base::BindOnce(&SimpleRoutine::StoreRoutineResult,
                                      weak_ptr_factory_.GetWeakPtr()));
}

// Simple routines can only be started.
void SimpleRoutine::Resume() {}
void SimpleRoutine::Cancel() {}

void SimpleRoutine::PopulateStatusUpdate(mojo_ipc::RoutineUpdate* response,
                                         bool include_output) {
  // Because simple routines are non-interactive, we will never include a user
  // message.
  auto update = mojo_ipc::NonInteractiveRoutineUpdate::New();
  update->status = status_;
  update->status_message = status_message_;

  response->routine_update_union =
      mojo_ipc::RoutineUpdateUnion::NewNoninteractiveUpdate(std::move(update));
  response->progress_percent = CalculateProgressPercent(status_);

  if (include_output && !output_dict_.empty()) {
    std::string json;
    base::JSONWriter::Write(output_dict_, &json);
    response->output =
        CreateReadOnlySharedMemoryRegionMojoHandle(base::StringPiece(json));
  }
}

mojo_ipc::DiagnosticRoutineStatusEnum SimpleRoutine::GetStatus() {
  return status_;
}

void SimpleRoutine::StoreRoutineResult(RoutineResult result) {
  status_ = result.status;
  status_message_ = std::move(result.status_message);
  output_dict_ = std::move(result.output_dict);
}

}  // namespace diagnostics
