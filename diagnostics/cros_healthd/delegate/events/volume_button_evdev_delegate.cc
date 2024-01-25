// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/delegate/events/volume_button_evdev_delegate.h"

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

VolumeButtonEvdevDelegate::VolumeButtonEvdevDelegate(
    mojo::PendingRemote<mojom::VolumeButtonObserver> observer)
    : observer_(std::move(observer)) {}

bool VolumeButtonEvdevDelegate::IsTarget(LibevdevWrapper* dev) {
  return dev->HasEventCode(EV_KEY, KEY_VOLUMEDOWN) &&
         dev->HasEventCode(EV_KEY, KEY_VOLUMEUP);
}

void VolumeButtonEvdevDelegate::FireEvent(const input_event& ev,
                                          LibevdevWrapper* dev) {
  if (ev.type != EV_KEY) {
    return;
  }

  mojom::VolumeButtonObserver::Button button;
  if (ev.code == KEY_VOLUMEUP) {
    button = mojom::VolumeButtonObserver::Button::kVolumeUp;
  } else if (ev.code == KEY_VOLUMEDOWN) {
    button = mojom::VolumeButtonObserver::Button::kVolumeDown;
  } else {
    return;
  }

  mojom::VolumeButtonObserver::ButtonState button_state;
  if (ev.value == 0) {
    button_state = mojom::VolumeButtonObserver::ButtonState::kUp;
  } else if (ev.value == 1) {
    button_state = mojom::VolumeButtonObserver::ButtonState::kDown;
  } else if (ev.value == 2) {
    button_state = mojom::VolumeButtonObserver::ButtonState::kRepeat;
  } else {
    return;
  }

  observer_->OnEvent(button, button_state);
}

void VolumeButtonEvdevDelegate::InitializationFail(
    uint32_t custom_reason, const std::string& description) {
  observer_.ResetWithReason(custom_reason, description);
}

void VolumeButtonEvdevDelegate::ReportProperties(LibevdevWrapper* dev) {}

}  // namespace diagnostics
