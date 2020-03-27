// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_ROUTINES_PRIME_SEARCH_PRIME_SEARCH_H_
#define DIAGNOSTICS_ROUTINES_PRIME_SEARCH_PRIME_SEARCH_H_

#include <memory>

#include "diagnostics/routines/diag_routine.h"

namespace diagnostics {

std::unique_ptr<DiagnosticRoutine> CreatePrimeSearchRoutine(
    const base::TimeDelta& exec_duration, uint64_t max_num);

}  // namespace diagnostics

#endif  // DIAGNOSTICS_ROUTINES_PRIME_SEARCH_PRIME_SEARCH_H_
