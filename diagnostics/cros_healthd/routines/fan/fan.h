// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_ROUTINES_FAN_FAN_H_
#define DIAGNOSTICS_CROS_HEALTHD_ROUTINES_FAN_FAN_H_

#include <optional>
#include <set>
#include <string>
#include <vector>

#include <base/functional/callback_helpers.h>
#include <base/memory/weak_ptr.h>

#include "diagnostics/cros_healthd/routines/base_routine_control.h"
#include "diagnostics/cros_healthd/system/context.h"
#include "diagnostics/mojom/public/cros_healthd_routines.mojom.h"

namespace diagnostics {

class FanRoutine final : public BaseRoutineControl {
 public:
  FanRoutine(Context* context,
             const ash::cros_healthd::mojom::FanRoutineArgumentPtr& arg);
  FanRoutine(const FanRoutine&) = delete;
  FanRoutine& operator=(const FanRoutine&) = delete;
  ~FanRoutine() override;

  // BaseRoutineControl overrides:
  void OnStart() override;

  // We increase or decrease the fan speed by `kFanRpmChange` to check whether
  // the fan is controllable.
  inline static constexpr uint16_t kFanRpmChange = 1000;

  // Since the fan speed naturally fluctuates, we verify a change has if it
  // changes by a nontrivial delta value.
  inline static constexpr uint16_t kFanRpmDelta = 100;

  // We will check whether the fan speed has been updated every
  // `kFanRoutineUpdatePeriod`.
  inline static constexpr base::TimeDelta kFanRoutineUpdatePeriod =
      base::Seconds(1);

 private:
  // An enum to describe what stage the fan routine is currently in.
  enum class Stage {
    // The initial stage, where the routine attempt to set the fan speed to be
    // higher than the original values.
    kSetIncrease,
    // The routine attempt to verify whether a higher fan speed is actually
    // achieved.
    kVerifyIncrease,
    // The routine attempt to set the fan speed to be lower than the original
    // values.
    kSetDecrease,
    // The routine attempt to verify whether a lower fan speed is actually
    // achieved.
    kVerifyDecrease,
  };

  // The |Run| function is added to the memory cpu resource queue as a callback
  // and will be called when resource is available.
  void Run(base::ScopedClosureRunner notify_resource_queue_finished);

  // A helper function to call executor's `GetAllFanSpeed` method.
  void GetFanSpeedHelper();

  // Handles the returned fan_speed and error from executor `GetAllFanSpeed`
  // function. This function will be in charge of stage transition and verifying
  // fan speed.
  void HandleGetFanSpeed(const std::vector<uint16_t>& fan_speed,
                         const std::optional<std::string>& error);

  // A helper function to call executor's `SetFanSpeed` method.
  void SetFanSpeedHelper(base::flat_map<uint8_t, uint16_t> set_fan_rpms);

  // Post a delay task to get fan speed.
  void DelayGetFanSpeed();

  // Handles the result from executor `GetAllFanSpeed` function.
  void HandleSetFanSpeed(const std::optional<std::string>& error);

  // Manually call the ScopedClosureRunners to ensure that resources are
  // released as early as possible.
  void ReleaseResources();

  // Enter the finish state and return the result.
  void TerminateFanRoutine();

  std::optional<uint8_t> GetExpectedFanCount();

  ash::cros_healthd::mojom::HardwarePresenceStatus CheckFanCount();

  // Context object used to communicate with the executor.
  Context* context_;
  // Records the current stage of the fan routine execution.
  Stage stage_;
  // Records how many times the fan speed has been verified at each stage.
  int verify_count_;
  // A callback that should be run regardless of the execution status. This
  // callback will set the fan control back to auto.
  base::ScopedClosureRunner reset_fan_control_;
  // A callback that should be run regardless of the execution status. This
  // callback will notify the resource queue that this job is finished.
  base::ScopedClosureRunner notify_resource_queue_finished_;
  // A set containing all the fans that have neither passed nor failed. Once the
  // routine reaches the finished state, any fan ids left here will be
  // considered as failed.
  std::set<uint8_t> remaining_fan_ids_;
  // A vector containing all the passed fan ids.
  std::vector<uint8_t> passed_fan_ids_;
  // A vector storing the original fan speed, which serves as a reference point
  // for whether the fan speed is changing.
  std::vector<uint16_t> original_fan_speed_;

  // Must be the last class member.
  base::WeakPtrFactory<FanRoutine> weak_ptr_factory_{this};
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINES_FAN_FAN_H_
