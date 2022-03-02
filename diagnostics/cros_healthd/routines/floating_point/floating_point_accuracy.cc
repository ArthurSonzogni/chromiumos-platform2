// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/floating_point/floating_point_accuracy.h"

#include <memory>
#include <string>
#include <vector>

#include <base/time/time.h>

#include "diagnostics/cros_healthd/routines/shared_defaults.h"
#include "diagnostics/cros_healthd/routines/subproc_routine.h"

namespace diagnostics {

namespace {

constexpr char kFloatingPointAccuracyTestExePath[] =
    "/usr/libexec/diagnostics/floating-point-accuracy";

}  // namespace

std::unique_ptr<DiagnosticRoutine> CreateFloatingPointAccuracyRoutine(
    const base::Optional<base::TimeDelta>& exec_duration) {
  base::TimeDelta duration = exec_duration.value_or(kDefaultCpuStressRuntime);
  return std::make_unique<SubprocRoutine>(
      base::CommandLine(std::vector<std::string>{
          kFloatingPointAccuracyTestExePath,
          "--duration=" + std::to_string(duration.InSeconds())}),
      duration);
}

}  // namespace diagnostics
