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

std::string WrapUnsupportedString(const PathLiteral& cros_config_property,
                                  const std::string& cros_config_value) {
  std::string msg = base::StringPrintf(
      "Not supported cros_config property [%s]: [%s]",
      cros_config_property.ToStr().c_str(), cros_config_value.c_str());
  return msg;
}

bool HasCrosEC() {
  return base::PathExists(GetRootedPath(kCrosEcSysPath));
}

mojom::SupportStatusPtr GetDiskReadArgSupportStatus(
    mojom::DiskReadRoutineArgumentPtr& arg) {
  if (arg->disk_read_duration.InSeconds() <= 0) {
    return mojom::SupportStatus::NewUnsupported(mojom::Unsupported::New(
        "Disk read duration should not be zero after rounding towards zero to "
        "the nearest second",
        nullptr));
  }

  if (arg->file_size_mib == 0) {
    return mojom::SupportStatus::NewUnsupported(
        mojom::Unsupported::New("Test file size should not be zero", nullptr));
  }

  if (arg->type == mojom::DiskReadTypeEnum::kUnmappedEnumField) {
    return mojom::SupportStatus::NewUnsupported(
        mojom::Unsupported::New("Unexpected disk read type", nullptr));
  }
  return mojom::SupportStatus::NewSupported(mojom::Supported::New());
}

void HandleFlossEnabledResponse(
    mojom::CrosHealthdRoutinesService::IsRoutineArgumentSupportedCallback
        callback,
    brillo::Error* error,
    bool enabled) {
  if (error) {
    LOG(ERROR) << "Failed to get floss enabled state, err: "
               << error->GetMessage();
    std::move(callback).Run(mojom::SupportStatus::NewException(
        mojom::Exception::New(mojom::Exception::Reason::kUnexpected,
                              "Got error when checking floss enabled state")));
    return;
  }
  if (!enabled) {
    std::move(callback).Run(mojom::SupportStatus::NewUnsupported(
        mojom::Unsupported::New("Floss is not enabled", nullptr)));
    return;
  }
  std::move(callback).Run(
      mojom::SupportStatus::NewSupported(mojom::Supported::New()));
}

void GetFlossSupportStatus(
    FlossController* floss_controller,
    mojom::CrosHealthdRoutinesService::IsRoutineArgumentSupportedCallback
        callback) {
  CHECK(floss_controller);
  auto manager = floss_controller->GetManager();
  if (!manager) {
    std::move(callback).Run(mojom::SupportStatus::NewUnsupported(
        mojom::Unsupported::New("Floss is not enabled", nullptr)));
    return;
  }

  auto [on_success, on_error] = SplitDbusCallback(
      base::BindOnce(&HandleFlossEnabledResponse, std::move(callback)));
  manager->GetFlossEnabledAsync(std::move(on_success), std::move(on_error));
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
    case mojom::EventCategoryEnum::kExternalDisplay:
      return mojom::SupportStatus::NewSupported(mojom::Supported::New());
    // Need to be determined by boxster/cros_config.
    case mojom::EventCategoryEnum::kKeyboardDiagnostic:
    case mojom::EventCategoryEnum::kTouchpad:
    case mojom::EventCategoryEnum::kLid: {
      std::vector<std::string> supported_form_factors = {
          cros_config_value::kClamshell,
          cros_config_value::kConvertible,
          cros_config_value::kDetachable,
      };
      auto form_factor = FormFactor();

      if (std::count(supported_form_factors.begin(),
                     supported_form_factors.end(), form_factor)) {
        return mojom::SupportStatus::NewSupported(mojom::Supported::New());
      }

      return mojom::SupportStatus::NewUnsupported(mojom::Unsupported::New(
          WrapUnsupportedString(cros_config_property::kFormFactor, form_factor),
          nullptr));
    }
    case mojom::EventCategoryEnum::kAudioJack: {
      auto has_audio_jack = HasAudioJack();
      if (has_audio_jack == "true") {
        return mojom::SupportStatus::NewSupported(mojom::Supported::New());
      }

      return mojom::SupportStatus::NewUnsupported(mojom::Unsupported::New(
          WrapUnsupportedString(cros_config_property::kHasAudioJack,
                                has_audio_jack),
          nullptr));
    }
    case mojom::EventCategoryEnum::kSdCard: {
      auto has_sd_reader = HasSdReader();
      if (has_sd_reader == "true") {
        return mojom::SupportStatus::NewSupported(mojom::Supported::New());
      }

      return mojom::SupportStatus::NewUnsupported(mojom::Unsupported::New(
          WrapUnsupportedString(cros_config_property::kHasSdReader,
                                has_sd_reader),
          nullptr));
    }
    case mojom::EventCategoryEnum::kTouchscreen: {
      auto has_touchscreen = HasTouchscreen();
      if (has_touchscreen == "true") {
        return mojom::SupportStatus::NewSupported(mojom::Supported::New());
      }

      return mojom::SupportStatus::NewUnsupported(mojom::Unsupported::New(
          WrapUnsupportedString(cros_config_property::kHasTouchscreen,
                                has_touchscreen),
          nullptr));
    }
    case mojom::EventCategoryEnum::kStylusGarage: {
      auto stylus_category = StylusCategory();
      if (stylus_category == cros_config_value::kStylusCategoryInternal) {
        return mojom::SupportStatus::NewSupported(mojom::Supported::New());
      }

      return mojom::SupportStatus::NewUnsupported(mojom::Unsupported::New(
          WrapUnsupportedString(cros_config_property::kStylusCategory,
                                stylus_category),
          nullptr));
    }
    case mojom::EventCategoryEnum::kStylus: {
      auto stylus_category = StylusCategory();
      if (stylus_category == cros_config_value::kStylusCategoryInternal ||
          stylus_category == cros_config_value::kStylusCategoryExternal) {
        return mojom::SupportStatus::NewSupported(mojom::Supported::New());
      }

      return mojom::SupportStatus::NewUnsupported(mojom::Unsupported::New(
          WrapUnsupportedString(cros_config_property::kStylusCategory,
                                stylus_category),
          nullptr));
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
    mojom::CrosHealthdRoutinesService::IsRoutineArgumentSupportedCallback
        callback) {
  // Please update docs/routine_supportability.md.
  // Add "NO_IFTTT=<reason>" in the commit message if it's not applicable.
  // LINT.IfChange
  mojom::SupportStatusPtr status;
  switch (routine_arg->which()) {
    // UnrecognizedArgument.
    case mojom::RoutineArgument::Tag::kUnrecognizedArgument:
      std::move(callback).Run(mojom::SupportStatus::NewException(
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
      status = mojom::SupportStatus::NewSupported(mojom::Supported::New());
      std::move(callback).Run(std::move(status));
      return;
    // Need to be determined by boxster/cros_config.
    case mojom::RoutineArgument::Tag::kUfsLifetime: {
      auto storage_type = StorageType();
      if (storage_type == cros_config_value::kStorageTypeUfs) {
        status = mojom::SupportStatus::NewSupported(mojom::Supported::New());
      } else {
        status = mojom::SupportStatus::NewUnsupported(mojom::Unsupported::New(
            WrapUnsupportedString(cros_config_property::kStorageType,
                                  storage_type),
            nullptr));
      }
      std::move(callback).Run(std::move(status));
      return;
    }
    case mojom::RoutineArgument::Tag::kFan: {
      auto fan_count = FanCount();
      // We support testing either when the fan count is larger than 0, or when
      // the cros config is not set. We intentionally allow empty cros config
      // since fan routine can handle both fans are present and absent cases.
      if (fan_count != "0") {
        status = mojom::SupportStatus::NewSupported(mojom::Supported::New());
        std::move(callback).Run(std::move(status));
        return;
      }

      status = mojom::SupportStatus::NewUnsupported(mojom::Unsupported::New(
          WrapUnsupportedString(cros_config_property::kFanCount, fan_count),
          nullptr));
      std::move(callback).Run(std::move(status));
      return;
    }
    // Need to check the routine arguments.
    case mojom::RoutineArgument::Tag::kDiskRead: {
      std::move(callback).Run(
          GetDiskReadArgSupportStatus(routine_arg->get_disk_read()));
      return;
    }
    case mojom::RoutineArgument::Tag::kVolumeButton: {
      auto has_side_volume_button = HasSideVolumeButton();
      if (has_side_volume_button == "true") {
        status = mojom::SupportStatus::NewSupported(mojom::Supported::New());
      } else {
        status = mojom::SupportStatus::NewUnsupported(mojom::Unsupported::New(
            WrapUnsupportedString(cros_config_property::kHasSideVolumeButton,
                                  has_side_volume_button),
            nullptr));
      }
      std::move(callback).Run(std::move(status));
      return;
    }
    case mojom::RoutineArgument::Tag::kLedLitUp: {
      if (HasCrosEC()) {
        status = mojom::SupportStatus::NewSupported(mojom::Supported::New());
      } else {
        status = mojom::SupportStatus::NewUnsupported(mojom::Unsupported::New(
            "Not supported on a non-CrosEC device", nullptr));
      }
      std::move(callback).Run(std::move(status));
      return;
    }
    case mojom::RoutineArgument::Tag::kBluetoothPower:
    case mojom::RoutineArgument::Tag::kBluetoothDiscovery:
    case mojom::RoutineArgument::Tag::kBluetoothPairing: {
      GetFlossSupportStatus(context_->floss_controller(), std::move(callback));
      return;
    }
    case mojom::RoutineArgument::Tag::kBluetoothScanning: {
      auto& arg = routine_arg->get_bluetooth_scanning();
      if (arg->exec_duration && !arg->exec_duration->is_positive()) {
        status = mojom::SupportStatus::NewUnsupported(mojom::Unsupported::New(
            "Execution duration should be strictly greater than zero",
            nullptr));
        std::move(callback).Run(std::move(status));
        return;
      }
      GetFlossSupportStatus(context_->floss_controller(), std::move(callback));
      return;
    }
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
// LINT.ThenChange(//diagnostics/docs/routine_supportability.md)

std::string GroundTruth::FormFactor() {
  return ReadCrosConfig(cros_config_property::kFormFactor);
}

std::string GroundTruth::StylusCategory() {
  return ReadCrosConfig(cros_config_property::kStylusCategory);
}

std::string GroundTruth::HasTouchscreen() {
  return ReadCrosConfig(cros_config_property::kHasTouchscreen);
}

std::string GroundTruth::HasAudioJack() {
  return ReadCrosConfig(cros_config_property::kHasAudioJack);
}

std::string GroundTruth::HasSdReader() {
  return ReadCrosConfig(cros_config_property::kHasSdReader);
}

std::string GroundTruth::HasSideVolumeButton() {
  return ReadCrosConfig(cros_config_property::kHasSideVolumeButton);
}

std::string GroundTruth::StorageType() {
  return ReadCrosConfig(cros_config_property::kStorageType);
}

std::string GroundTruth::FanCount() {
  return ReadCrosConfig(cros_config_property::kFanCount);
}

std::string GroundTruth::ReadCrosConfig(const PathLiteral& path) {
  auto value = cros_config()->Get(path);
  if (!value) {
    LOG(ERROR) << "Failed to read cros_config: " << path.ToStr();
    return "";
  }

  return value.value();
}

CrosConfig* GroundTruth::cros_config() const {
  return context_->cros_config();
}

}  // namespace diagnostics
