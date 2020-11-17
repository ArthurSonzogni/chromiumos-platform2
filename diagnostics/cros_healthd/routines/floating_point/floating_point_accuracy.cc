// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/floating_point/floating_point_accuracy.h"

#include <memory>
#include <string>
#include <vector>

#include "diagnostics/cros_healthd/routines/shared_defaults.h"
#include "diagnostics/cros_healthd/routines/subproc_routine.h"

namespace diagnostics {

namespace {

constexpr char kFloatingPointAccuracyTestExePath[] =
    "/usr/libexec/diagnostics/floating-point-accuracy";

}  // namespace

std::unique_ptr<DiagnosticRoutine> CreateFloatingPointAccuracyRoutine(
    const base::Optional<base::TimeDelta>& exec_duration) {
  uint32_t duration_in_seconds =
      exec_duration.value_or(kDefaultCpuStressRuntime).InSeconds();
  return std::make_unique<SubprocRoutine>(
      base::CommandLine(std::vector<std::string>{
          kFloatingPointAccuracyTestExePath,
          "--duration=" + std::to_string(duration_in_seconds)}),
      duration_in_seconds);
}

}  // namespace diagnostics
