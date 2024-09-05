// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_DELEGATE_ROUTINES_CPU_ROUTINE_TASK_DELEGATE_H_
#define DIAGNOSTICS_CROS_HEALTHD_DELEGATE_ROUTINES_CPU_ROUTINE_TASK_DELEGATE_H_

namespace diagnostics {

class CpuRoutineTaskDelegate {
 public:
  virtual ~CpuRoutineTaskDelegate() = default;

  // Executes a task used by CPU routines. Returns true if the task is completed
  // without any error, false otherwise.
  virtual bool Run() = 0;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_DELEGATE_ROUTINES_CPU_ROUTINE_TASK_DELEGATE_H_
