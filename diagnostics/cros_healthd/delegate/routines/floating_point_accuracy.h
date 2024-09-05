// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_DELEGATE_ROUTINES_FLOATING_POINT_ACCURACY_H_
#define DIAGNOSTICS_CROS_HEALTHD_DELEGATE_ROUTINES_FLOATING_POINT_ACCURACY_H_

#include "diagnostics/cros_healthd/delegate/routines/cpu_routine_task_delegate.h"

namespace diagnostics {

class FloatingPointAccuracyDelegate : public CpuRoutineTaskDelegate {
 public:
  FloatingPointAccuracyDelegate();
  FloatingPointAccuracyDelegate(const FloatingPointAccuracyDelegate&) = delete;
  FloatingPointAccuracyDelegate& operator=(
      const FloatingPointAccuracyDelegate&) = delete;
  ~FloatingPointAccuracyDelegate();

  // Executes floating point accuracy task. Returns true if test is completed
  // without any error, false otherwise.
  bool Run() override;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_DELEGATE_ROUTINES_FLOATING_POINT_ACCURACY_H_
