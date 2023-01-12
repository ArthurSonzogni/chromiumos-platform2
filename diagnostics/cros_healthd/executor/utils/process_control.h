// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_UTILS_PROCESS_CONTROL_H_
#define DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_UTILS_PROCESS_CONTROL_H_

#include <bits/types/siginfo_t.h>

#include <memory>
#include <string>
#include <vector>

#include <base/functional/callback_forward.h>
#include <brillo/process/process.h>
#include <brillo/process/process_reaper.h>

#include "diagnostics/cros_healthd/mojom/executor.mojom.h"

namespace diagnostics {

// Used for child process lifecycle control.
//
// This object holds a pointer of child process, and then this object will be
// added into a mojo::UniqueReceiverSet. So the routine in cros_healthd can use
// a mojo connection to control the lifecycle of this object, that is, the
// lifecycle of child process.
class ProcessControl : public ash::cros_healthd::mojom::ProcessControl {
 public:
  explicit ProcessControl(std::unique_ptr<brillo::Process> process);
  ProcessControl(const ProcessControl&) = delete;
  ProcessControl& operator=(const ProcessControl&) = delete;
  ~ProcessControl() override;

  // Whether to redirect the stdout and stderr of the process into a memory
  // file.
  void RedirectOutputToMemory(bool combine_stdout_and_stderr);
  // Start the process and wait for it to end.
  void StartAndWait(brillo::ProcessReaper* process_reaper);

  // ash::cros_healthd::mojom::ProcessControl overrides
  void GetStdout(GetStdoutCallback callback) override;
  void GetStderr(GetStderrCallback callback) override;
  void GetReturnCode(GetReturnCodeCallback callback) override;

 private:
  // Set the process as finished and run any pending callbacks.
  void SetProcessFinished(const siginfo_t& exit_status);

  // Helper function to cast a file descriptor into mojo::ScopedHandle.
  mojo::ScopedHandle GetMojoScopedHandle(int file_no);

  std::unique_ptr<brillo::Process> process_;
  // The return code of the process.
  int return_code_ = -1;
  // Queue for storing pending callbacks before the process has finished
  // running.
  std::vector<GetReturnCodeCallback> get_return_code_callback_queue_;

  // Must be the last member of the class.
  base::WeakPtrFactory<ProcessControl> weak_factory_{this};
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_UTILS_PROCESS_CONTROL_H_
