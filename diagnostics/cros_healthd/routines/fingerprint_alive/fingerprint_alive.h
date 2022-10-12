// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_ROUTINES_FINGERPRINT_ALIVE_FINGERPRINT_ALIVE_H_
#define DIAGNOSTICS_CROS_HEALTHD_ROUTINES_FINGERPRINT_ALIVE_FINGERPRINT_ALIVE_H_

#include <string>

#include "diagnostics/cros_healthd/executor/mojom/executor.mojom.h"
#include "diagnostics/cros_healthd/routines/diag_routine.h"
#include "diagnostics/cros_healthd/system/context.h"

namespace diagnostics {

class FingerprintAliveRoutine final : public DiagnosticRoutine {
 public:
  explicit FingerprintAliveRoutine(Context* context);
  FingerprintAliveRoutine(const FingerprintAliveRoutine&) = delete;
  FingerprintAliveRoutine& operator=(const FingerprintAliveRoutine&) = delete;

  // DiagnosticRoutine overrides:
  ~FingerprintAliveRoutine() override;
  void Start() override;
  void Resume() override;
  void Cancel() override;
  void PopulateStatusUpdate(ash::cros_healthd::mojom::RoutineUpdate* response,
                            bool include_output) override;
  ash::cros_healthd::mojom::DiagnosticRoutineStatusEnum GetStatus() override;

 private:
  void ExamineInfo(ash::cros_healthd::mojom::FingerprintInfoResultPtr result,
                   const std::optional<std::string>& err);

  // Context object used to communicate with the executor.
  Context* context_;

  // Status of the routine, reported by GetStatus() or noninteractive routine
  // updates.
  ash::cros_healthd::mojom::DiagnosticRoutineStatusEnum status_;

  // Details of the routine's status, reported in non-interactive status
  // updates.
  std::string status_message_ = "";
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINES_FINGERPRINT_ALIVE_FINGERPRINT_ALIVE_H_
