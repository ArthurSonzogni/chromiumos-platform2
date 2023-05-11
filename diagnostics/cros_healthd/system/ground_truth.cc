// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <utility>
#include <vector>

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
          "Not supported form factor: " + form_factor, nullptr));
    }
    case mojom::EventCategoryEnum::kAudioJack:
      return mojom::SupportStatus::NewSupported(mojom::Supported::New());
    case mojom::EventCategoryEnum::kSdCard:
      return mojom::SupportStatus::NewSupported(mojom::Supported::New());
    case mojom::EventCategoryEnum::kKeyboardDiagnostic:
      return mojom::SupportStatus::NewSupported(mojom::Supported::New());
    case mojom::EventCategoryEnum::kTouchpad:
      return mojom::SupportStatus::NewSupported(mojom::Supported::New());
    case mojom::EventCategoryEnum::kHdmi:
      return mojom::SupportStatus::NewSupported(mojom::Supported::New());
    case mojom::EventCategoryEnum::kTouchscreen:
      return mojom::SupportStatus::NewSupported(mojom::Supported::New());
    case mojom::EventCategoryEnum::kStylusGarage:
      return mojom::SupportStatus::NewSupported(mojom::Supported::New());
    case mojom::EventCategoryEnum::kStylus:
      return mojom::SupportStatus::NewSupported(mojom::Supported::New());
  }
}

void GroundTruth::IsEventSupported(
    mojom::EventCategoryEnum category,
    mojom::CrosHealthdEventService::IsEventSupportedCallback callback) {
  auto status = GetEventSupportStatus(category);
  std::move(callback).Run(std::move(status));
}

std::string GroundTruth::FormFactor() {
  std::string form_factor;
  if (!context_->cros_config()->GetString(cros_config_path::kHardwareProperties,
                                          cros_config_property::kFormFactor,
                                          &form_factor)) {
    LogCrosConfigFail(cros_config_path::kHardwareProperties,
                      cros_config_property::kFormFactor);
    return "";
  }

  return form_factor;
}

}  // namespace diagnostics
