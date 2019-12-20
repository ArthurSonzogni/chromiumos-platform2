// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_ROUTINES_SUBPROC_ROUTINE_H_
#define DIAGNOSTICS_ROUTINES_SUBPROC_ROUTINE_H_

#include <cstdint>
#include <memory>
#include <string>

#include <base/command_line.h>
#include <base/macros.h>
#include <base/process/process.h>
#include <base/time/default_tick_clock.h>

#include "diagnostics/routines/diag_process_adapter.h"
#include "diagnostics/routines/diag_routine.h"

namespace diagnostics {

// Output messages for the routine when in various states.
extern const char kSubprocRoutineCancelledMessage[];
extern const char kSubprocRoutineErrorMessage[];
extern const char kSubprocRoutineFailedMessage[];
extern const char kSubprocRoutineFailedToLaunchProcessMessage[];
extern const char kSubprocRoutineFailedToStopMessage[];
extern const char kSubprocRoutineProcessCancellingMessage[];
extern const char kSubprocRoutineProcessRunningMessage[];
extern const char kSubprocRoutineReadyMessage[];
extern const char kSubprocRoutineSucceededMessage[];

// We don't always know when a SubprocRoutine should finish. Sometimes we have
// to fake our prediction of percent complete.
extern const uint32_t kSubprocRoutineFakeProgressPercentUnknown;

// The SubprocRoutine takes a command line to run. It is non-interactive, and
// this does not fully support Pause and Resume. Pause will simply kill the
// process. The exit code of the process is used to determine success or failure
// of the test. So, the "check" portion of the Routine must live inside the
// sub-process.
class SubprocRoutine final : public DiagnosticRoutine {
 public:
  // The state of the SubprocRoutine is modeled in the SubprocStatus enum.
  enum SubprocStatus {
    kSubprocStatusCancelled,
    kSubprocStatusCancelling,
    kSubprocStatusCompleteFailure,
    kSubprocStatusCompleteSuccess,
    kSubprocStatusError,
    kSubprocStatusLaunchFailed,
    kSubprocStatusReady,
    kSubprocStatusRunning,
  };

  SubprocRoutine(const base::CommandLine& command_line,
                 uint32_t predicted_duration_in_seconds);
  SubprocRoutine(std::unique_ptr<DiagProcessAdapter> process_adapter,
                 std::unique_ptr<base::TickClock> tick_clock,
                 const base::CommandLine& command_line,
                 uint32_t predicted_duration_in_seconds);

  // DiagnosticRoutine overrides:
  ~SubprocRoutine() override;
  void Start() override;
  void Resume() override;
  void Cancel() override;
  void PopulateStatusUpdate(
      chromeos::cros_healthd::mojom::RoutineUpdate* response,
      bool include_output) override;
  chromeos::cros_healthd::mojom::DiagnosticRoutineStatusEnum GetStatus()
      override;

 private:
  // Functions to manipulate the child process.
  void StartProcess();
  void KillProcess(bool from_dtor);
  // Handle state transitions due to process state within this object.
  void CheckProcessStatus();
  void CheckActiveProcessStatus();
  uint32_t CalculateProgressPercent();

  // |subproc_status_| is the state of the subproc as understood by the
  // SubprocRoutine object's state machine. Essentially, this variable stores
  // which state we are in.
  SubprocStatus subproc_status_;

  // |process_adapter_| is a dependency that is injected at object creation time
  // which enables swapping out process control functionality for the main
  // purpose of facilitating Unit tests.
  std::unique_ptr<DiagProcessAdapter> process_adapter_;

  // |tick_clock_| is a dependency that is injected at object creation time
  // which enables swapping out time-tracking functionality for the main
  // purpose of facilitating Unit tests.
  std::unique_ptr<base::TickClock> tick_clock_;

  // |command_line_| is the process which runs to test the diagnostic in
  // question.
  base::CommandLine command_line_;

  // |predicted_duration_in_seconds_| is used to calculate progress percentage
  // when it is non-zero.
  uint32_t predicted_duration_in_seconds_ = 0;

  // |last_reported_progress_percent_| is used to save the last reported
  // progress percentage for handling progress reported across status changes.
  uint32_t last_reported_progress_percent_ = 0;

  // |handle_| keeps track of the running process.
  base::ProcessHandle handle_ = base::kNullProcessHandle;

  // |start_ticks_| records the time when the routine began. This is used with
  // |predicted_duration_in_seconds_| to report on progress percentate.
  base::TimeTicks start_ticks_;

  DISALLOW_COPY_AND_ASSIGN(SubprocRoutine);
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_ROUTINES_SUBPROC_ROUTINE_H_
