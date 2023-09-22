// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/system/ground_truth.h"

#include <string>
#include <utility>
#include <vector>

#include <base/files/file_util.h>
#include <base/strings/stringprintf.h>

#include "diagnostics/base/file_utils.h"
#include "diagnostics/cros_healthd/system/ground_truth_constants.h"
#include "diagnostics/mojom/public/cros_healthd.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_events.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_exception.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_routines.mojom.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

void LogCrosConfigFail(const std::string& path, const std::string& property) {
  LOG(ERROR) << "Failed to read cros_config: " << path << "/" << property;
}

std::string WrapUnsupportedString(const std::string& cros_config_property,
                                  const std::string& cros_config_value) {
  std::string msg = base::StringPrintf(
      "Not supported cros_config property [%s]: [%s]",
      cros_config_property.c_str(), cros_config_value.c_str());
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
    // TODO(b/291902680): Currently external display event only supports HDMI.
    // Update ground truth check once we also support DP.
    case mojom::EventCategoryEnum::kExternalDisplay: {
      auto has_hdmi = HasHdmi();
      if (has_hdmi == "true") {
        return mojom::SupportStatus::NewSupported(mojom::Supported::New());
      }

      return mojom::SupportStatus::NewUnsupported(mojom::Unsupported::New(
          WrapUnsupportedString(cros_config_property::kHasHdmi, has_hdmi),
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
  }
  // LINT.ThenChange(//diagnostics/docs/routine_supportability.md)
}

std::string GroundTruth::FormFactor() {
  return ReadCrosConfig(cros_config_path::kHardwareProperties,
                        cros_config_property::kFormFactor);
}

std::string GroundTruth::StylusCategory() {
  return ReadCrosConfig(cros_config_path::kHardwareProperties,
                        cros_config_property::kStylusCategory);
}

std::string GroundTruth::HasTouchscreen() {
  return ReadCrosConfig(cros_config_path::kHardwareProperties,
                        cros_config_property::kHasTouchscreen);
}

std::string GroundTruth::HasHdmi() {
  return ReadCrosConfig(cros_config_path::kHardwareProperties,
                        cros_config_property::kHasHdmi);
}

std::string GroundTruth::HasAudioJack() {
  return ReadCrosConfig(cros_config_path::kHardwareProperties,
                        cros_config_property::kHasAudioJack);
}

std::string GroundTruth::HasSdReader() {
  return ReadCrosConfig(cros_config_path::kHardwareProperties,
                        cros_config_property::kHasSdReader);
}

std::string GroundTruth::HasSideVolumeButton() {
  return ReadCrosConfig(cros_config_path::kHardwareProperties,
                        cros_config_property::kHasSideVolumeButton);
}

std::string GroundTruth::StorageType() {
  return ReadCrosConfig(cros_config_path::kHardwareProperties,
                        cros_config_property::kStorageType);
}

std::string GroundTruth::ReadCrosConfig(const std::string& path,
                                        const std::string& property) {
  std::string value;
  if (!context_->cros_config()->GetString(path, property, &value)) {
    LogCrosConfigFail(path, property);
    return "";
  }

  return value;
}

}  // namespace diagnostics
