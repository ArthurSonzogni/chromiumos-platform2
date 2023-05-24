// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <utility>
#include <vector>

#include <base/strings/stringprintf.h>

#include "diagnostics/cros_healthd/system/ground_truth.h"
#include "diagnostics/cros_healthd/system/ground_truth_constants.h"
#include "diagnostics/mojom/public/cros_healthd.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_events.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_exception.mojom.h"

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

}  // namespace

GroundTruth::GroundTruth(Context* context) : context_(context) {
  CHECK(context_);
}

GroundTruth::~GroundTruth() = default;

mojom::SupportStatusPtr GroundTruth::GetEventSupportStatus(
    mojom::EventCategoryEnum category) {
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
    case mojom::EventCategoryEnum::kSdCard:
      return mojom::SupportStatus::NewSupported(mojom::Supported::New());
    case mojom::EventCategoryEnum::kHdmi: {
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
}

void GroundTruth::IsEventSupported(
    mojom::EventCategoryEnum category,
    mojom::CrosHealthdEventService::IsEventSupportedCallback callback) {
  auto status = GetEventSupportStatus(category);
  std::move(callback).Run(std::move(status));
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
