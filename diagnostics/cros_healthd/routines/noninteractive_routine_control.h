// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_ROUTINES_NONINTERACTIVE_ROUTINE_CONTROL_H_
#define DIAGNOSTICS_CROS_HEALTHD_ROUTINES_NONINTERACTIVE_ROUTINE_CONTROL_H_

#include "diagnostics/cros_healthd/routines/base_routine_control.h"

namespace diagnostics {

// Implements the RoutineControl interface for routines that never wait for
// interactions.
class NoninteractiveRoutineControl : public BaseRoutineControl {
 public:
  NoninteractiveRoutineControl();
  NoninteractiveRoutineControl(const NoninteractiveRoutineControl&) = delete;
  NoninteractiveRoutineControl& operator=(const NoninteractiveRoutineControl&) =
      delete;
  ~NoninteractiveRoutineControl() override;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINES_NONINTERACTIVE_ROUTINE_CONTROL_H_
