// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_health_tool/event/thunderbolt_subscriber.h"

#include <iostream>
#include <string>
#include <utility>

#include <base/check.h>

namespace diagnostics {
namespace {

const char kHumanReadableOnDeviceAddedEvent[] = "Device added";
const char kHumanReadableOnDeviceRemovedEvent[] = "Device removed";
const char kHumanReadableOnDeviceAuthorizedEvent[] = "Device Authorized";
const char kHumanReadableOnDeviceUnAuthorizedEvent[] = "Device UnAuthorized";

// Enumeration of the different Thunderbolt event types.
enum class ThunderboltEventType {
  kOnDeviceAdded,
  kOnDeviceRemoved,
  kOnDeviceAuthorized,
  kOnDeviceUnAuthorized,
};

std::string GetHumanReadableThunderboltEvent(ThunderboltEventType event) {
  switch (event) {
    case ThunderboltEventType::kOnDeviceAdded:
      return kHumanReadableOnDeviceAddedEvent;
    case ThunderboltEventType::kOnDeviceRemoved:
      return kHumanReadableOnDeviceRemovedEvent;
    case ThunderboltEventType::kOnDeviceAuthorized:
      return kHumanReadableOnDeviceAuthorizedEvent;
    case ThunderboltEventType::kOnDeviceUnAuthorized:
      return kHumanReadableOnDeviceUnAuthorizedEvent;
  }
}

void PrintThunderboltEvent(ThunderboltEventType event) {
  std::cout << "Thunderbolt event received: "
            << GetHumanReadableThunderboltEvent(event) << std::endl;
}

}  // namespace

ThunderboltSubscriber::ThunderboltSubscriber(
    mojo::PendingReceiver<
        chromeos::cros_healthd::mojom::CrosHealthdThunderboltObserver> receiver)
    : receiver_{this /* impl */, std::move(receiver)} {
  DCHECK(receiver_.is_bound());
}

ThunderboltSubscriber::~ThunderboltSubscriber() = default;

void ThunderboltSubscriber::OnAdd() {
  PrintThunderboltEvent(ThunderboltEventType::kOnDeviceAdded);
}

void ThunderboltSubscriber::OnRemove() {
  PrintThunderboltEvent(ThunderboltEventType::kOnDeviceRemoved);
}

void ThunderboltSubscriber::OnAuthorized() {
  PrintThunderboltEvent(ThunderboltEventType::kOnDeviceAuthorized);
}

void ThunderboltSubscriber::OnUnAuthorized() {
  PrintThunderboltEvent(ThunderboltEventType::kOnDeviceUnAuthorized);
}

}  // namespace diagnostics
