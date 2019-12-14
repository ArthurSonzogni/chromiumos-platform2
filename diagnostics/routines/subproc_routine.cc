// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/routines/subproc_routine.h"

#include <algorithm>
#include <utility>
#include <vector>

#include <base/logging.h>
#include <base/strings/string_util.h>
#include <base/process/process_handle.h>

#include "diagnostics/routines/diag_process_adapter_impl.h"

namespace diagnostics {
namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

constexpr char kSubprocRoutineCancelled[] = "The test was canceled.";
constexpr char kSubprocRoutineErrorMessage[] =
    "The test crashed or was killed.";
constexpr char kSubprocRoutineFailedMessage[] = "Test failed.";
constexpr char kSubprocRoutineFailedToLaunchProcessMessage[] =
    "Could not launch the process.";
constexpr char kSubprocRoutineFailedToStopMessage[] =
    "Failed to stop the routine.";
constexpr char kSubprocRoutineInvalidParametersMessage[] =
    "The test could not be run due to invalid parameters.";
constexpr char kSubprocRoutineProcessCancellingMessage[] =
    "Cancelled test. Waiting for cleanup...";
constexpr char kSubprocRoutineProcessRunningMessage[] =
    "Test is still running.";
constexpr char kSubprocRoutineReadyMessage[] = "Routine is ready.";
constexpr char kSubprocRoutineSucceededMessage[] = "Test passed.";

constexpr int kSubprocRoutineFakeProgressPercentUnknown = 33;
constexpr int kSubprocRoutineFakeProgressPercentCancelling = 99;

mojo_ipc::DiagnosticRoutineStatusEnum
GetDiagnosticRoutineStatusFromSubprocRoutineStatus(
    SubprocRoutine::SubprocStatus subproc_status) {
  switch (subproc_status) {
    case SubprocRoutine::kSubprocStatusReady:
      return mojo_ipc::DiagnosticRoutineStatusEnum::kReady;
    case SubprocRoutine::kSubprocStatusLaunchFailed:
      return mojo_ipc::DiagnosticRoutineStatusEnum::kFailedToStart;
    case SubprocRoutine::kSubprocStatusRunning:
      return mojo_ipc::DiagnosticRoutineStatusEnum::kRunning;
    case SubprocRoutine::kSubprocStatusCancelling:
      return mojo_ipc::DiagnosticRoutineStatusEnum::kCancelling;
    case SubprocRoutine::kSubprocStatusCompleteSuccess:
      return mojo_ipc::DiagnosticRoutineStatusEnum::kPassed;
    case SubprocRoutine::kSubprocStatusCompleteFailure:
      return mojo_ipc::DiagnosticRoutineStatusEnum::kFailed;
    case SubprocRoutine::kSubprocStatusError:
      return mojo_ipc::DiagnosticRoutineStatusEnum::kError;
    case SubprocRoutine::kSubprocStatusCancelled:
      return mojo_ipc::DiagnosticRoutineStatusEnum::kCancelled;
  }
}

std::string GetStatusMessageFromSubprocRoutineStatus(
    SubprocRoutine::SubprocStatus subproc_status) {
  switch (subproc_status) {
    case SubprocRoutine::kSubprocStatusReady:
      return kSubprocRoutineReadyMessage;
    case SubprocRoutine::kSubprocStatusLaunchFailed:
      return kSubprocRoutineFailedToLaunchProcessMessage;
    case SubprocRoutine::kSubprocStatusRunning:
      return kSubprocRoutineProcessRunningMessage;
    case SubprocRoutine::kSubprocStatusCancelling:
      return kSubprocRoutineProcessCancellingMessage;
    case SubprocRoutine::kSubprocStatusCompleteSuccess:
      return kSubprocRoutineSucceededMessage;
    case SubprocRoutine::kSubprocStatusCompleteFailure:
      return kSubprocRoutineFailedMessage;
    case SubprocRoutine::kSubprocStatusError:
      return kSubprocRoutineErrorMessage;
    case SubprocRoutine::kSubprocStatusCancelled:
      return kSubprocRoutineCancelled;
  }
}

SubprocRoutine::SubprocRoutine(const base::CommandLine& command_line,
                               int predicted_duration_in_seconds)
    : SubprocRoutine(std::make_unique<DiagProcessAdapterImpl>(),
                     std::make_unique<base::DefaultTickClock>(),
                     command_line,
                     predicted_duration_in_seconds) {}

SubprocRoutine::SubprocRoutine(
    std::unique_ptr<DiagProcessAdapter> process_adapter,
    std::unique_ptr<base::TickClock> tick_clock,
    const base::CommandLine& command_line,
    int predicted_duration_in_seconds)
    : subproc_status_(kSubprocStatusReady),
      process_adapter_(std::move(process_adapter)),
      tick_clock_(std::move(tick_clock)),
      command_line_(command_line),
      predicted_duration_in_seconds_(predicted_duration_in_seconds) {}

SubprocRoutine::~SubprocRoutine() {
  // If the routine is still running, make sure to stop it so we aren't left
  // with a zombie process.
  KillProcess(true /*from_dtor*/);
}

void SubprocRoutine::Start() {
  DCHECK_EQ(subproc_status_, kSubprocStatusReady);
  DCHECK_EQ(handle_, base::kNullProcessHandle);

  if (predicted_duration_in_seconds_ < 0) {
    subproc_status_ = kSubprocStatusLaunchFailed;
    LOG(ERROR) << kSubprocRoutineInvalidParametersMessage;
    return;
  }

  StartProcess();
}

void SubprocRoutine::Resume() {
  // Resume functionality is intended to be used by interactive routines.
  // Subprocess routines are non-interactive.
  LOG(ERROR) << "SubprocRoutine::Resume : subprocess diagnostic routines "
                "cannot be resumed";
}

void SubprocRoutine::Cancel() {
  KillProcess(false /*from_dtor*/);
}

void SubprocRoutine::PopulateStatusUpdate(mojo_ipc::RoutineUpdate* response,
                                          bool include_output) {
  // Because the subproc_routine routine is non-interactive, we will never
  // include a user message.
  CheckProcessStatus();

  mojo_ipc::NonInteractiveRoutineUpdate update;
  update.status =
      GetDiagnosticRoutineStatusFromSubprocRoutineStatus(subproc_status_);
  update.status_message =
      GetStatusMessageFromSubprocRoutineStatus(subproc_status_);

  response->routine_update_union->set_noninteractive_update(update.Clone());
  response->progress_percent = CalculateProgressPercent();
}

mojo_ipc::DiagnosticRoutineStatusEnum SubprocRoutine::GetStatus() {
  CheckProcessStatus();
  return GetDiagnosticRoutineStatusFromSubprocRoutineStatus(subproc_status_);
}

void SubprocRoutine::StartProcess() {
  if (subproc_status_ == kSubprocStatusReady) {
    subproc_status_ = kSubprocStatusRunning;
    // Don't bother joining the command_line_ if we aren't in verbose mode.
    if (VLOG_IS_ON(1)) {
      VLOG(1) << "Starting command "
              << base::JoinString(command_line_.argv(), " ");
    }

    if (!process_adapter_->StartProcess(command_line_.argv(), &handle_)) {
      subproc_status_ = kSubprocStatusLaunchFailed;
      LOG(ERROR) << kSubprocRoutineFailedToLaunchProcessMessage;
    }
    // Keep track of when we began the routine, in case we need to predict
    // progress.
    start_ticks_ = tick_clock_->NowTicks();
  } else {
    LOG(ERROR) << "An attempt was made to start a SubprocRoutine, but it is "
                  "not ready.";
  }
}

void SubprocRoutine::KillProcess(bool from_dtor) {
  CheckProcessStatus();

  switch (subproc_status_) {
    case kSubprocStatusRunning:
      DCHECK(handle_ != base::kNullProcessHandle);
      if (from_dtor) {
        // We will not be able to keep track of this child process.
        LOG(ERROR) << "Cancelling process " << handle_
                   << " from diagnostics::SubprocRoutine destructor, cannot "
                      "guarantee process will die.";
      }
      subproc_status_ = kSubprocStatusCancelling;
      process_adapter_->KillProcess(handle_);
      break;
    case kSubprocStatusCancelling:
      // The process is already being killed. Do nothing.
      DCHECK_NE(handle_, base::kNullProcessHandle);
      break;
    case kSubprocStatusCancelled:
    case kSubprocStatusCompleteFailure:
    case kSubprocStatusCompleteSuccess:
    case kSubprocStatusError:
    case kSubprocStatusLaunchFailed:
    case kSubprocStatusReady:
      // If the process has already exited, is exiting, or never started,
      // there's no need to kill it.
      DCHECK_EQ(handle_, base::kNullProcessHandle);
      break;
  }
}

void SubprocRoutine::CheckActiveProcessStatus() {
  DCHECK_NE(handle_, base::kNullProcessHandle);
  switch (process_adapter_->GetStatus(handle_)) {
    case base::TERMINATION_STATUS_STILL_RUNNING:
      DCHECK(subproc_status_ == kSubprocStatusCancelling ||
             subproc_status_ == kSubprocStatusRunning);
      break;
    case base::TERMINATION_STATUS_NORMAL_TERMINATION:
      // The process is gone.
      handle_ = base::kNullProcessHandle;
      if (subproc_status_ == kSubprocStatusCancelling) {
        subproc_status_ = kSubprocStatusCancelled;
      } else {
        subproc_status_ = kSubprocStatusCompleteSuccess;
      }
      break;
    case base::TERMINATION_STATUS_ABNORMAL_TERMINATION:
      // The process is gone.
      handle_ = base::kNullProcessHandle;

      subproc_status_ = (subproc_status_ == kSubprocStatusCancelling)
                            ? kSubprocStatusCancelled
                            : kSubprocStatusCompleteFailure;
      break;
    case base::TERMINATION_STATUS_LAUNCH_FAILED:
      // The process never really was.
      handle_ = base::kNullProcessHandle;

      subproc_status_ = kSubprocStatusLaunchFailed;
      break;
    default:
      // The process is mysteriously just missing.
      handle_ = base::kNullProcessHandle;

      subproc_status_ = (subproc_status_ == kSubprocStatusCancelling)
                            ? kSubprocStatusCancelled
                            : kSubprocStatusError;
      break;
  }
}

void SubprocRoutine::CheckProcessStatus() {
  switch (subproc_status_) {
    case kSubprocStatusCancelled:
    case kSubprocStatusCompleteFailure:
    case kSubprocStatusCompleteSuccess:
    case kSubprocStatusError:
    case kSubprocStatusLaunchFailed:
    case kSubprocStatusReady:
      DCHECK_EQ(handle_, base::kNullProcessHandle);
      break;
    case kSubprocStatusCancelling:
    case kSubprocStatusRunning:
      CheckActiveProcessStatus();
      break;
  }
}

int SubprocRoutine::CalculateProgressPercent() {
  switch (subproc_status_) {
    case kSubprocStatusCompleteSuccess:
    case kSubprocStatusCompleteFailure:
      last_reported_progress_percent_ = 100;
      break;
    case kSubprocStatusRunning:
      if (predicted_duration_in_seconds_ <= 0) {
        /* when we don't know the progress, we fake at a low percentage */
        last_reported_progress_percent_ =
            kSubprocRoutineFakeProgressPercentUnknown;
      } else {
        last_reported_progress_percent_ = std::min(
            100,
            std::max(0, static_cast<int>(
                            100 * (tick_clock_->NowTicks() - start_ticks_) /
                            base::TimeDelta::FromSeconds(
                                predicted_duration_in_seconds_))));
      }
      break;
    case kSubprocStatusCancelled:
    case kSubprocStatusCancelling:
    case kSubprocStatusError:
    case kSubprocStatusLaunchFailed:
    case kSubprocStatusReady:
      break;
  }
  return last_reported_progress_percent_;
}

}  // namespace diagnostics
