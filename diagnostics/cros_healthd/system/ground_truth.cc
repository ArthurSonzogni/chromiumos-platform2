// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/system/ground_truth.h"

#include <string>
#include <utility>
#include <vector>

#include <base/files/file_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/stringprintf.h>
#include <base/types/expected.h>
#include <brillo/errors/error.h>

#include "diagnostics/base/file_utils.h"
#include "diagnostics/base/path_utils.h"
#include "diagnostics/base/paths.h"
#include "diagnostics/cros_healthd/routines/fingerprint/fingerprint.h"
#include "diagnostics/cros_healthd/system/context.h"
#include "diagnostics/cros_healthd/system/cros_config.h"
#include "diagnostics/cros_healthd/system/cros_config_constants.h"
#include "diagnostics/cros_healthd/system/floss_controller.h"
#include "diagnostics/cros_healthd/system/ground_truth_constants.h"
#include "diagnostics/cros_healthd/utils/dbus_utils.h"
#include "diagnostics/mojom/public/cros_healthd.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_events.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_exception.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_routines.mojom.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;
namespace cros_config_property = paths::cros_config;
namespace fingerprint = paths::cros_config::fingerprint;

mojom::SupportStatusPtr MakeSupported() {
  return mojom::SupportStatus::NewSupported(mojom::Supported::New());
}

mojom::SupportStatusPtr MakeUnsupported(const std::string& debug_message) {
  return mojom::SupportStatus::NewUnsupported(
      mojom::Unsupported::New(debug_message, /*reason=*/nullptr));
}

mojom::SupportStatusPtr MakeException(const std::string& debug_message) {
  return mojom::SupportStatus::NewException(mojom::Exception::New(
      mojom::Exception::Reason::kUnexpected, debug_message));
}

mojom::SupportStatusPtr MakeSupportStatus(
    const base::expected<void, std::string>& expected) {
  return expected.has_value() ? MakeSupported()
                              : MakeUnsupported(expected.error());
}

template <typename T>
void AssignOrAppendError(const base::expected<T, std::string>& got,
                         T& out,
                         std::string& error) {
  if (got.has_value()) {
    out = got.value();
    return;
  }
  if (!error.empty()) {
    error += ' ';
  }
  error += got.error();
}

template <typename T>
void AssignAndDropError(const base::expected<T, std::string>& got,
                        std::optional<T>& out) {
  if (!got.has_value()) {
    out = std::nullopt;
    return;
  }
  out = got.value();
}

template <typename T>
void AssignWithDefaultAndDropError(const base::expected<T, std::string>& got,
                                   const T& default_value,
                                   T& out) {
  out = got.value_or(default_value);
}

bool HasCrosEC() {
  return base::PathExists(GetRootedPath(kCrosEcSysPath));
}

base::expected<void, std::string> CheckCrosEc() {
  if (HasCrosEC()) {
    return base::ok();
  }
  return base::unexpected("Not supported on a non-CrosEC device");
}

mojom::SupportStatusPtr HandleFlossEnabledResponse(brillo::Error* error,
                                                   bool enabled) {
  if (error) {
    LOG(ERROR) << "Failed to get floss enabled state, err: "
               << error->GetMessage();
    return MakeException("Got error when checking floss enabled state");
  }
  return enabled ? MakeSupported() : MakeUnsupported("Floss is not enabled");
}

}  // namespace

GroundTruth::GroundTruth(Context* context) : context_(context) {
  CHECK(context_);
}

GroundTruth::~GroundTruth() = default;

mojom::SupportStatusPtr GroundTruth::GetEventSupportStatus(
    mojom::EventCategoryEnum category) {
  // Please update docs/event_supportability.md.
  // Add "NO_IFTTT=<reason>" in the commit message if it's not applicable.
  // LINT.IfChange
  switch (category) {
    // UnmappedEnumField.
    case mojom::EventCategoryEnum::kUnmappedEnumField:
      return mojom::SupportStatus::NewException(mojom::Exception::New(
          mojom::Exception::Reason::kUnexpected, "Got kUnmappedEnumField"));
    // Currently not supported.
    case mojom::EventCategoryEnum::kNetwork:
      return mojom::SupportStatus::NewUnsupported(mojom::Unsupported::New(
          "Not implemented. Please contact cros_healthd team.", nullptr));
    // Always supported.
    case mojom::EventCategoryEnum::kUsb:
    case mojom::EventCategoryEnum::kThunderbolt:
    case mojom::EventCategoryEnum::kBluetooth:
    case mojom::EventCategoryEnum::kPower:
    case mojom::EventCategoryEnum::kAudio:
    case mojom::EventCategoryEnum::kCrash:
      return mojom::SupportStatus::NewSupported(mojom::Supported::New());
    // Need to be determined by boxster/cros_config.
    case mojom::EventCategoryEnum::kKeyboardDiagnostic:
    case mojom::EventCategoryEnum::kTouchpad:
    case mojom::EventCategoryEnum::kLid: {
      return MakeSupportStatus(cros_config()->CheckExpectedsCrosConfig(
          cros_config_property::kFormFactor,
          {
              cros_config_value::kClamshell,
              cros_config_value::kConvertible,
              cros_config_value::kDetachable,
          }));
    }
    case mojom::EventCategoryEnum::kAudioJack: {
      return MakeSupportStatus(cros_config()->CheckTrueCrosConfig(
          cros_config_property::kHasAudioJack));
    }
    case mojom::EventCategoryEnum::kSdCard: {
      return MakeSupportStatus(cros_config()->CheckTrueCrosConfig(
          cros_config_property::kHasSdReader));
    }
    // TODO(b/291902680): Currently external display event only supports HDMI.
    // Update ground truth check once we also support DP.
    case mojom::EventCategoryEnum::kExternalDisplay: {
      return MakeSupportStatus(
          cros_config()->CheckTrueCrosConfig(cros_config_property::kHasHdmi));
    }
    case mojom::EventCategoryEnum::kTouchscreen: {
      return MakeSupportStatus(cros_config()->CheckTrueCrosConfig(
          cros_config_property::kHasTouchscreen));
    }
    case mojom::EventCategoryEnum::kStylusGarage: {
      return MakeSupportStatus(cros_config()->CheckExpectedCrosConfig(
          cros_config_property::kStylusCategory,
          cros_config_value::kStylusCategoryInternal));
    }
    case mojom::EventCategoryEnum::kStylus: {
      return MakeSupportStatus(cros_config()->CheckExpectedsCrosConfig(
          cros_config_property::kStylusCategory,
          {cros_config_value::kStylusCategoryInternal,
           cros_config_value::kStylusCategoryExternal}));
    }
  }
  // LINT.ThenChange(//diagnostics/docs/event_supportability.md)
}

void GroundTruth::IsEventSupported(
    mojom::EventCategoryEnum category,
    mojom::CrosHealthdEventService::IsEventSupportedCallback callback) {
  auto status = GetEventSupportStatus(category);
  std::move(callback).Run(std::move(status));
}

void GroundTruth::IsRoutineArgumentSupported(
    mojom::RoutineArgumentPtr routine_arg,
    base::OnceCallback<void(mojom::RoutineArgumentPtr, mojom::SupportStatusPtr)>
        callback) {
  // TODO(b/309080271): Migrate this function to
  // RoutineService::CheckAndCreateRoutine and just return supported here. After
  // migrate all routine, remove this function.

  // Please update docs/routine_supportability.md.
  // Add "NO_IFTTT=<reason>" in the commit message if it's not applicable.
  // LINT.IfChange
  mojom::SupportStatusPtr status;
  switch (routine_arg->which()) {
    // UnrecognizedArgument.
    case mojom::RoutineArgument::Tag::kUnrecognizedArgument:
      std::move(callback).Run(
          std::move(routine_arg),
          mojom::SupportStatus::NewException(
              mojom::Exception::New(mojom::Exception::Reason::kUnexpected,
                                    "Got kUnrecognizedArgument")));
      return;
    // Always supported. There is no rule on the routine arguments.
    case mojom::RoutineArgument::Tag::kMemory:
    case mojom::RoutineArgument::Tag::kAudioDriver:
    case mojom::RoutineArgument::Tag::kCpuStress:
    case mojom::RoutineArgument::Tag::kCpuCache:
    case mojom::RoutineArgument::Tag::kPrimeSearch:
    case mojom::RoutineArgument::Tag::kFloatingPoint:
    case mojom::RoutineArgument::Tag::kUfsLifetime:
    case mojom::RoutineArgument::Tag::kFan:
    case mojom::RoutineArgument::Tag::kDiskRead:
    case mojom::RoutineArgument::Tag::kVolumeButton:
    case mojom::RoutineArgument::Tag::kLedLitUp:
    case mojom::RoutineArgument::Tag::kBluetoothPower:
    case mojom::RoutineArgument::Tag::kBluetoothDiscovery:
    case mojom::RoutineArgument::Tag::kBluetoothPairing:
    case mojom::RoutineArgument::Tag::kBluetoothScanning:
      status = mojom::SupportStatus::NewSupported(mojom::Supported::New());
      std::move(callback).Run(std::move(routine_arg), std::move(status));
      return;
  }
  // LINT.ThenChange(//diagnostics/docs/routine_supportability.md)
}

// Please update docs/routine_supportability.md.
// Add "NO_IFTTT=<reason>" in the commit message if it's not applicable.
// LINT.IfChange
mojom::SupportStatusPtr GroundTruth::PrepareRoutineBatteryCapacity(
    std::optional<uint32_t>& low_mah, std::optional<uint32_t>& high_mah) const {
  AssignAndDropError(cros_config()->GetU32CrosConfig(
                         cros_config_property::kBatteryCapacityLowMah),
                     low_mah);
  AssignAndDropError(cros_config()->GetU32CrosConfig(
                         cros_config_property::kBatteryCapacityHighMah),
                     high_mah);
  return MakeSupported();
}

mojom::SupportStatusPtr GroundTruth::PrepareRoutineBatteryHealth(
    std::optional<uint32_t>& maximum_cycle_count,
    std::optional<uint8_t>& percent_battery_wear_allowed) const {
  AssignAndDropError(cros_config()->GetU32CrosConfig(
                         cros_config_property::kBatteryHealthMaximumCycleCount),
                     maximum_cycle_count);
  AssignAndDropError(
      cros_config()->GetU8CrosConfig(
          cros_config_property::kBatteryHealthPercentBatteryWearAllowed),
      percent_battery_wear_allowed);
  return MakeSupported();
}

mojom::SupportStatusPtr GroundTruth::PrepareRoutinePrimeSearch(
    std::optional<uint64_t>& max_num) const {
  AssignAndDropError(
      cros_config()->GetU64CrosConfig(cros_config_property::kPrimeSearchMaxNum),
      max_num);
  return MakeSupported();
}

mojom::SupportStatusPtr GroundTruth::PrepareRoutineNvmeWearLevel(
    std::optional<uint32_t>& threshold) const {
  AssignAndDropError(cros_config()->GetU32CrosConfig(
                         cros_config_property::kNvmeWearLevelThreshold),
                     threshold);
  return MakeSupported();
}

mojom::SupportStatusPtr GroundTruth::PrepareRoutineFingerprint(
    FingerprintParameter& param) const {
  AssignWithDefaultAndDropError(
      cros_config()->GetU32CrosConfig(fingerprint::kMaxDeadPixels), 0u,
      param.max_dead_pixels);
  AssignWithDefaultAndDropError(
      cros_config()->GetU32CrosConfig(fingerprint::kMaxDeadPixelsInDetectZone),
      0u, param.max_dead_pixels_in_detect_zone);
  AssignWithDefaultAndDropError(
      cros_config()->GetU32CrosConfig(fingerprint::kMaxPixelDev), 0u,
      param.max_pixel_dev);
  AssignWithDefaultAndDropError(
      cros_config()->GetU32CrosConfig(fingerprint::kMaxErrorResetPixels), 0u,
      param.max_error_reset_pixels);
  AssignWithDefaultAndDropError(
      cros_config()->GetU32CrosConfig(fingerprint::kMaxResetPixelDev), 0u,
      param.max_reset_pixel_dev);

  AssignWithDefaultAndDropError(
      cros_config()->GetU8CrosConfig(fingerprint::kCbType1Lower),
      static_cast<uint8_t>(0), param.pixel_median.cb_type1_lower);
  AssignWithDefaultAndDropError(
      cros_config()->GetU8CrosConfig(fingerprint::kCbType1Upper),
      static_cast<uint8_t>(0), param.pixel_median.cb_type1_upper);
  AssignWithDefaultAndDropError(
      cros_config()->GetU8CrosConfig(fingerprint::kCbType2Lower),
      static_cast<uint8_t>(0), param.pixel_median.cb_type2_lower);
  AssignWithDefaultAndDropError(
      cros_config()->GetU8CrosConfig(fingerprint::kCbType2Upper),
      static_cast<uint8_t>(0), param.pixel_median.cb_type2_upper);
  AssignWithDefaultAndDropError(
      cros_config()->GetU8CrosConfig(fingerprint::kIcbType1Lower),
      static_cast<uint8_t>(0), param.pixel_median.icb_type1_lower);
  AssignWithDefaultAndDropError(
      cros_config()->GetU8CrosConfig(fingerprint::kIcbType1Upper),
      static_cast<uint8_t>(0), param.pixel_median.icb_type1_upper);
  AssignWithDefaultAndDropError(
      cros_config()->GetU8CrosConfig(fingerprint::kIcbType2Lower),
      static_cast<uint8_t>(0), param.pixel_median.icb_type2_lower);
  AssignWithDefaultAndDropError(
      cros_config()->GetU8CrosConfig(fingerprint::kIcbType2Upper),
      static_cast<uint8_t>(0), param.pixel_median.icb_type2_upper);

  // Fill |FingerprintZone| value;
  uint32_t num_detect_zone =
      cros_config()->GetU32CrosConfig(fingerprint::kNumDetectZone).value_or(0);
  for (int i = 0; i < num_detect_zone; ++i) {
    base::FilePath dir =
        fingerprint::kDetectZones.ToPath().Append(base::NumberToString(i));

    FingerprintZone zone;
    AssignWithDefaultAndDropError(
        cros_config()->GetU32CrosConfig(dir.Append(fingerprint::kX1)), 0u,
        zone.x1);
    AssignWithDefaultAndDropError(
        cros_config()->GetU32CrosConfig(dir.Append(fingerprint::kY1)), 0u,
        zone.y1);
    AssignWithDefaultAndDropError(
        cros_config()->GetU32CrosConfig(dir.Append(fingerprint::kX2)), 0u,
        zone.x2);
    AssignWithDefaultAndDropError(
        cros_config()->GetU32CrosConfig(dir.Append(fingerprint::kY2)), 0u,
        zone.y2);
    param.detect_zones.push_back(std::move(zone));
  }

  // TODO(chungsheng): Migrate SystemConfig::FingerprintDiagnosticSupported to
  // this function and return not supported status.
  return MakeSupported();
}

mojom::SupportStatusPtr GroundTruth::PrepareRoutineUfsLifetime() const {
  return MakeSupportStatus(cros_config()->CheckExpectedCrosConfig(
      cros_config_property::kStorageType, cros_config_value::kStorageTypeUfs));
}

mojom::SupportStatusPtr GroundTruth::PrepareRoutineFan(
    uint8_t& fan_count) const {
  std::string error;
  AssignOrAppendError(
      cros_config()->GetU8CrosConfig(cros_config_property::kFanCount),
      fan_count, error);

  if (!error.empty()) {
    return MakeUnsupported(error);
  }
  if (fan_count == 0) {
    return MakeUnsupported("Doesn't support device with no fan.");
  }
  return MakeSupported();
}

mojom::SupportStatusPtr GroundTruth::PrepareRoutineVolumeButton() const {
  return MakeSupportStatus(cros_config()->CheckTrueCrosConfig(
      cros_config_property::kHasSideVolumeButton));
}

mojom::SupportStatusPtr GroundTruth::PrepareRoutineLedLitUp() const {
  return MakeSupportStatus(CheckCrosEc());
}

void GroundTruth::PrepareRoutineBluetoothFloss(
    PrepareRoutineBluetoothFlossCallback callback) const {
  auto manager = context_->floss_controller()->GetManager();
  if (!manager) {
    std::move(callback).Run(MakeUnsupported("Floss is not enabled"));
    return;
  }

  auto [on_success, on_error] = SplitDbusCallback(
      base::BindOnce(&HandleFlossEnabledResponse).Then(std::move(callback)));
  manager->GetFlossEnabledAsync(std::move(on_success), std::move(on_error));
}
// LINT.ThenChange(//diagnostics/docs/routine_supportability.md)

CrosConfig* GroundTruth::cros_config() const {
  return context_->cros_config();
}

}  // namespace diagnostics
