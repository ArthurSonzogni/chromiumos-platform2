// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_ROUTINES_ROUTINE_V2_TEST_UTILS_H_
#define DIAGNOSTICS_CROS_HEALTHD_ROUTINES_ROUTINE_V2_TEST_UTILS_H_

#include "diagnostics/cros_healthd/routines/base_routine_control.h"

namespace diagnostics {

// All of the utilities in this file are for use in testing only.

// Returns a callback that, when invoked, generates a fatal failure in
// GoogleTest. Designed for |BaseRoutineControl::SetOnExceptionCallback|.
BaseRoutineControl::ExceptionCallback UnexpectedRoutineExceptionCallback();

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINES_ROUTINE_V2_TEST_UTILS_H_
