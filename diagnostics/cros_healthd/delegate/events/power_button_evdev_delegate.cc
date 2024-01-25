// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/delegate/events/power_button_evdev_delegate.h"

#include <linux/input.h>
#include <linux/input-event-codes.h>

#include <cstdint>
#include <string>
#include <utility>

#include <mojo/public/cpp/bindings/pending_remote.h>
#include <mojo/public/cpp/bindings/remote.h>

#include "diagnostics/cros_healthd/delegate/utils/libevdev_wrapper.h"
#include "diagnostics/cros_healthd/mojom/executor.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_events.mojom.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

}  // namespace

PowerButtonEvdevDelegate::PowerButtonEvdevDelegate(
    mojo::PendingRemote<mojom::PowerButtonObserver> observer)
    : observer_(std::move(observer)) {}

bool PowerButtonEvdevDelegate::IsTarget(LibevdevWrapper* dev) {
  // Only internal power button is desired. Filter out USB devices to exclude
  // external power buttons.
  return dev->HasEventCode(EV_KEY, KEY_POWER) && dev->GetIdBustype() != BUS_USB;
}

void PowerButtonEvdevDelegate::FireEvent(const input_event& ev,
                                         LibevdevWrapper* dev) {
  if (ev.type == EV_KEY && ev.code == KEY_POWER) {
    if (ev.value == 0) {
      observer_->OnEvent(mojom::PowerButtonObserver::ButtonState::kUp);
    } else if (ev.value == 1) {
      observer_->OnEvent(mojom::PowerButtonObserver::ButtonState::kDown);
    } else if (ev.value == 2) {
      observer_->OnEvent(mojom::PowerButtonObserver::ButtonState::kRepeat);
    }
  }
}

void PowerButtonEvdevDelegate::InitializationFail(
    uint32_t custom_reason, const std::string& description) {
  observer_.ResetWithReason(custom_reason, description);
}

void PowerButtonEvdevDelegate::ReportProperties(LibevdevWrapper* dev) {
  observer_->OnConnectedToEventNode();
}

}  // namespace diagnostics
