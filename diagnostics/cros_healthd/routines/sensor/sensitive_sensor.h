// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_ROUTINES_SENSOR_SENSITIVE_SENSOR_H_
#define DIAGNOSTICS_CROS_HEALTHD_ROUTINES_SENSOR_SENSITIVE_SENSOR_H_

#include "diagnostics/cros_healthd/routines/diag_routine.h"
#include "diagnostics/mojom/public/cros_healthd_diagnostics.mojom.h"

namespace diagnostics {

// The sensitive sensor routine checks that the device's sensors are working
// correctly by acquiring dynamic sensor sample data without user interaction.
class SensitiveSensorRoutine final : public DiagnosticRoutine {
 public:
  SensitiveSensorRoutine();
  SensitiveSensorRoutine(const SensitiveSensorRoutine&) = delete;
  SensitiveSensorRoutine& operator=(const SensitiveSensorRoutine&) = delete;
  ~SensitiveSensorRoutine() override;

  // DiagnosticRoutine overrides:
  void Start() override;
  void Resume() override;
  void Cancel() override;
  void PopulateStatusUpdate(ash::cros_healthd::mojom::RoutineUpdate* response,
                            bool include_output) override;
  ash::cros_healthd::mojom::DiagnosticRoutineStatusEnum GetStatus() override;

 private:
  // Status of the routine, reported by GetStatus() or routine updates.
  ash::cros_healthd::mojom::DiagnosticRoutineStatusEnum status_ =
      ash::cros_healthd::mojom::DiagnosticRoutineStatusEnum::kReady;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINES_SENSOR_SENSITIVE_SENSOR_H_
