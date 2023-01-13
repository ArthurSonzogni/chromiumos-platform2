// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_UTILS_PROCESS_CONTROL_H_
#define DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_UTILS_PROCESS_CONTROL_H_

#include <memory>

#include <brillo/process/process.h>

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

  // Start the process.
  void Start();

 private:
  std::unique_ptr<brillo::Process> process_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_UTILS_PROCESS_CONTROL_H_
