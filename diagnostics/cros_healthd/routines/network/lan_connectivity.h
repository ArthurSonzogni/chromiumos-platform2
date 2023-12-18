// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_ROUTINES_NETWORK_LAN_CONNECTIVITY_H_
#define DIAGNOSTICS_CROS_HEALTHD_ROUTINES_NETWORK_LAN_CONNECTIVITY_H_

#include <memory>

#include "diagnostics/cros_healthd/routines/diag_routine.h"

namespace diagnostics {
class MojoService;

// Status messages reported by the LAN connectivity routine.
extern const char kLanConnectivityRoutineNoProblemMessage[];
extern const char kLanConnectivityRoutineProblemMessage[];
extern const char kLanConnectivityRoutineNotRunMessage[];

std::unique_ptr<DiagnosticRoutine> CreateLanConnectivityRoutine(
    MojoService* const mojo_service);

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINES_NETWORK_LAN_CONNECTIVITY_H_
