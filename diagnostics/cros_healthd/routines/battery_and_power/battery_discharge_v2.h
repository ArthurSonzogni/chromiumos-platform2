// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_ROUTINES_BATTERY_AND_POWER_BATTERY_DISCHARGE_V2_H_
#define DIAGNOSTICS_CROS_HEALTHD_ROUTINES_BATTERY_AND_POWER_BATTERY_DISCHARGE_V2_H_

#include <cstdint>
#include <memory>

#include <base/memory/weak_ptr.h>
#include <base/types/expected.h>
#include <base/time/time.h>

#include "diagnostics/cros_healthd/routines/interactive_routine_control.h"
#include "diagnostics/mojom/public/cros_healthd_routines.mojom-forward.h"

namespace diagnostics {
class Context;

// Checks the discharge rate of the battery.
class BatteryDischargeRoutineV2 final : public InteractiveRoutineControl {
 public:
  static base::expected<std::unique_ptr<BaseRoutineControl>,
                        ash::cros_healthd::mojom::SupportStatusPtr>
  Create(
      Context* context,
      const ash::cros_healthd::mojom::BatteryDischargeRoutineArgumentPtr& arg);
  BatteryDischargeRoutineV2(const BatteryDischargeRoutineV2&) = delete;
  BatteryDischargeRoutineV2& operator=(const BatteryDischargeRoutineV2&) =
      delete;
  ~BatteryDischargeRoutineV2() override;

  // BaseRoutineControl overrides:
  void OnStart() override;

  // InteractiveRoutineControl overrides:
  void OnReplyInquiry(
      ash::cros_healthd::mojom::RoutineInquiryReplyPtr reply) override;

 protected:
  BatteryDischargeRoutineV2(
      Context* const context,
      const ash::cros_healthd::mojom::BatteryDischargeRoutineArgumentPtr& arg);

 private:
  // Finishes the routine after running the routine for `exec_duration` time.
  // Check and report on battery charge values.
  void Finish();

  // Updates the percentage progress of the routine.
  void UpdatePercentage();

  // Unowned. Should outlive this instance.
  Context* const context_ = nullptr;
  // The execution duration of the battery discharge routine.
  base::TimeDelta exec_duration_;
  // `start_ticks_` records the time when the routine began. This is used with
  // `exec_duration_` to report on progress percentage.
  base::TimeTicks start_ticks_;
  // Maximum discharge percent allowed for the routine to pass.
  const uint8_t maximum_discharge_percent_allowed_;
  // Stores the battery charge at the beginning of the routine.
  double beginning_charge_percent_;

  // Must be the last class member.
  base::WeakPtrFactory<BatteryDischargeRoutineV2> weak_ptr_factory_{this};
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINES_BATTERY_AND_POWER_BATTERY_DISCHARGE_V2_H_
