// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/diag_routine_with_status.h"

#include <string>
#include <utility>

namespace diagnostics {
namespace {
namespace mojom = ::ash::cros_healthd::mojom;
}  // namespace

mojom::DiagnosticRoutineStatusEnum DiagnosticRoutineWithStatus::GetStatus() {
  return status_;
}

const std::string& DiagnosticRoutineWithStatus::GetStatusMessage() const {
  return status_message_;
}

void DiagnosticRoutineWithStatus::UpdateStatus(
    mojom::DiagnosticRoutineStatusEnum status, std::string message) {
  status_ = status;
  status_message_ = std::move(message);
}

}  // namespace diagnostics
