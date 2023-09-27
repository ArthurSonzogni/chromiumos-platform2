// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/routine_v2_test_utils.h"

#include <cstdint>
#include <string>

#include <base/check.h>
#include <base/functional/bind.h>

namespace diagnostics {

BaseRoutineControl::ExceptionCallback UnexpectedRoutineExceptionCallback() {
  return base::BindOnce([](uint32_t error, const std::string& reason) {
    CHECK(false) << "An unexpected routine exception has occurred; error="
                 << error << ", reason=" << reason;
  });
}

}  // namespace diagnostics
