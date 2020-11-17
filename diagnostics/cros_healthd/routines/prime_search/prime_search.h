// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_ROUTINES_PRIME_SEARCH_PRIME_SEARCH_H_
#define DIAGNOSTICS_CROS_HEALTHD_ROUTINES_PRIME_SEARCH_PRIME_SEARCH_H_

#include <cstdint>
#include <memory>

#include <base/optional.h>
#include <base/time/time.h>

#include "diagnostics/cros_healthd/routines/diag_routine.h"

namespace diagnostics {

// Fleet-wide default value for the |max_num| parameter of
// CreatePrimeSearchRoutine(). Can be overridden in cros_config.
extern const uint64_t kPrimeSearchDefaultMaxNum;

std::unique_ptr<DiagnosticRoutine> CreatePrimeSearchRoutine(
    const base::Optional<base::TimeDelta>& exec_duration,
    const base::Optional<uint64_t>& max_num);

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINES_PRIME_SEARCH_PRIME_SEARCH_H_
