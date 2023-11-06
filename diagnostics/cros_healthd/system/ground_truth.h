// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_SYSTEM_GROUND_TRUTH_H_
#define DIAGNOSTICS_CROS_HEALTHD_SYSTEM_GROUND_TRUTH_H_

#include <string>

#include "diagnostics/mojom/public/cros_healthd.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_events.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_exception.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_routines.mojom.h"

namespace diagnostics {
class Context;
class CrosConfig;
class PathLiteral;

class GroundTruth final {
 public:
  explicit GroundTruth(Context* context);
  GroundTruth(const GroundTruth&) = delete;
  GroundTruth& operator=(const GroundTruth&) = delete;
  ~GroundTruth();

  void IsEventSupported(ash::cros_healthd::mojom::EventCategoryEnum category,
                        ash::cros_healthd::mojom::CrosHealthdEventService::
                            IsEventSupportedCallback callback);
  void IsRoutineArgumentSupported(
      ash::cros_healthd::mojom::RoutineArgumentPtr routine_arg,
      ash::cros_healthd::mojom::CrosHealthdRoutinesService::
          IsRoutineArgumentSupportedCallback callback);

  // These methods check if a routine is supported and prepare its parameters
  // from system configurations.
  // The naming should be `PrepareRoutine{RotuineName}`. They return
  // `mojom::SupportStatusPtr` and routine parameters, if any, via output
  // arguments.
  //
  // Please update docs/routine_supportability.md if the supportability
  // definition of a routine has changed. Add "NO_IFTTT=<reason>" in the commit
  // message if it's not applicable.
  //
  // LINT.IfChange
  ash::cros_healthd::mojom::SupportStatusPtr PrepareRoutineBatteryCapacity(
      std::optional<uint32_t>& low_mah,
      std::optional<uint32_t>& high_mah) const;

  ash::cros_healthd::mojom::SupportStatusPtr PrepareRoutineBatteryHealth(
      std::optional<uint32_t>& maximum_cycle_count,
      std::optional<uint8_t>& percent_battery_wear_allowed) const;
  // LINT.ThenChange(//diagnostics/docs/routine_supportability.md)

  // cros_config related functions.
  std::string FormFactor();
  std::string StylusCategory();
  std::string HasTouchscreen();
  std::string HasAudioJack();
  std::string HasSdReader();
  std::string HasSideVolumeButton();
  std::string StorageType();
  std::string FanCount();

 private:
  ash::cros_healthd::mojom::SupportStatusPtr GetEventSupportStatus(
      ash::cros_healthd::mojom::EventCategoryEnum category);

  std::string ReadCrosConfig(const PathLiteral& path);

  CrosConfig* cros_config() const;

  // Unowned. Should outlive this instance.
  Context* const context_ = nullptr;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_SYSTEM_GROUND_TRUTH_H_
