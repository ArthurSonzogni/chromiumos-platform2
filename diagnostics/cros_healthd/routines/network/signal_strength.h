// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_ROUTINES_NETWORK_SIGNAL_STRENGTH_H_
#define DIAGNOSTICS_CROS_HEALTHD_ROUTINES_NETWORK_SIGNAL_STRENGTH_H_

#include <memory>

#include "diagnostics/cros_healthd/routines/diag_routine.h"

namespace diagnostics {
class MojoService;

// Status messages reported by the signal strength routine.
extern const char kSignalStrengthRoutineNoProblemMessage[];
extern const char kSignalStrengthRoutineWeakSignalProblemMessage[];
extern const char kSignalStrengthRoutineNotRunMessage[];

// Creates the signal strength connetivity routine.
std::unique_ptr<DiagnosticRoutine> CreateSignalStrengthRoutine(
    MojoService* const mojo_service);

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINES_NETWORK_SIGNAL_STRENGTH_H_
