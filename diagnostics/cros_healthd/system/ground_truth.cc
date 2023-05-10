// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include "diagnostics/cros_healthd/system/ground_truth.h"
#include "diagnostics/mojom/public/cros_healthd.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_events.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_exception.mojom.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

}  // namespace

GroundTruth::GroundTruth() = default;

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
      return mojom::SupportStatus::NewSupported(mojom::Supported::New());
    // Need to be determined by boxster/cros_config.
    case mojom::EventCategoryEnum::kLid:
      return mojom::SupportStatus::NewSupported(mojom::Supported::New());
    case mojom::EventCategoryEnum::kAudio:
      return mojom::SupportStatus::NewSupported(mojom::Supported::New());
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

}  // namespace diagnostics
