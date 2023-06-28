// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_ROUTINES_BATTERY_AND_POWER_BATTERY_CHARGE_H_
#define DIAGNOSTICS_CROS_HEALTHD_ROUTINES_BATTERY_AND_POWER_BATTERY_CHARGE_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include <base/cancelable_callback.h>
#include <base/memory/weak_ptr.h>
#include <base/time/default_tick_clock.h>
#include <base/time/tick_clock.h>
#include <base/time/time.h>
#include <base/values.h>

#include "diagnostics/cros_healthd/routines/diag_routine_with_status.h"
#include "diagnostics/cros_healthd/system/context.h"
#include "diagnostics/mojom/public/cros_healthd_diagnostics.mojom.h"

namespace diagnostics {

// Checks the charge rate of the battery.
class BatteryChargeRoutine final : public DiagnosticRoutineWithStatus {
 public:
  // |exec_duration| - length of time to run the routine for.
  // |minimum_charge_percent_required| - the routine will fail if the battery
  // charges less than this percentage during the execution of the routine.
  // Valid range: [0, 100].
  // Override |tick_clock| for testing only.
  BatteryChargeRoutine(Context* const context,
                       base::TimeDelta exec_duration,
                       uint32_t minimum_charge_percent_required,
                       const base::TickClock* tick_clock = nullptr);
  BatteryChargeRoutine(const BatteryChargeRoutine&) = delete;
  BatteryChargeRoutine& operator=(const BatteryChargeRoutine&) = delete;

  // DiagnosticRoutine overrides:
  ~BatteryChargeRoutine() override;
  void Start() override;
  void Resume() override;
  void Cancel() override;
  void PopulateStatusUpdate(ash::cros_healthd::mojom::RoutineUpdate* response,
                            bool include_output) override;

 private:
  // Calculates the progress percent based on the current status.
  void CalculateProgressPercent();
  // Checks the machine state against the input parameters.
  void RunBatteryChargeRoutine();
  // Determine success or failure for the routine.
  void DetermineRoutineResult(double beginning_discharge_percent);

  // Unowned pointer that outlives this routine instance.
  Context* const context_;
  // Details about the routine's execution. Reported in all status updates.
  base::Value::Dict output_;
  // Length of time to run the routine for.
  const base::TimeDelta exec_duration_;
  // Minimum charge percent required for the routine to pass.
  const uint32_t minimum_charge_percent_required_;
  // A measure of how far along the routine is, reported in all status updates.
  uint32_t progress_percent_ = 0;
  // When the routine started. Used to calculate |progress_percent_|.
  std::optional<base::TimeTicks> start_ticks_ = std::nullopt;
  // Tracks the passage of time.
  std::unique_ptr<base::DefaultTickClock> default_tick_clock_;
  // Unowned pointer which should outlive this instance. Allows the default tick
  // clock to be overridden for testing.
  const base::TickClock* tick_clock_;
  // Wraps DetermineRoutineResult in a cancellable callback.
  base::CancelableOnceClosure callback_;

  // Must be the last class member.
  base::WeakPtrFactory<BatteryChargeRoutine> weak_ptr_factory_{this};
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINES_BATTERY_AND_POWER_BATTERY_CHARGE_H_
