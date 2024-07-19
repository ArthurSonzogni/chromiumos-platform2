// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/delegate/events/touchpad_evdev_delegate.h"

#include <linux/input-event-codes.h>
#include <linux/input.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <mojo/public/cpp/bindings/pending_remote.h>
#include <mojo/public/cpp/bindings/remote.h>

#include "diagnostics/cros_healthd/delegate/utils/evdev_utils.h"
#include "diagnostics/cros_healthd/delegate/utils/libevdev_wrapper.h"
#include "diagnostics/cros_healthd/mojom/executor.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_events.mojom.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

std::optional<mojom::InputTouchButton> EventCodeToInputTouchButton(
    unsigned int code) {
  switch (code) {
    case BTN_LEFT:
      return mojom::InputTouchButton::kLeft;
    case BTN_MIDDLE:
      return mojom::InputTouchButton::kMiddle;
    case BTN_RIGHT:
      return mojom::InputTouchButton::kRight;
    default:
      return std::nullopt;
  }
}

}  // namespace

TouchpadEvdevDelegate::TouchpadEvdevDelegate(
    mojo::PendingRemote<mojom::TouchpadObserver> observer)
    : observer_(std::move(observer)) {}

bool TouchpadEvdevDelegate::IsTarget(LibevdevWrapper* dev) {
  // - Typical pointer devices: touchpads, tablets, mice.
  // - Typical non-direct devices: touchpads, mice.
  // - Check for event type EV_ABS to exclude mice, which report movement with
  //   REL_{X,Y} instead of ABS_{X,Y}.
  return dev->HasProperty(INPUT_PROP_POINTER) &&
         !dev->HasProperty(INPUT_PROP_DIRECT) && dev->HasEventType(EV_ABS);
}

void TouchpadEvdevDelegate::FireEvent(const input_event& ev,
                                      LibevdevWrapper* dev) {
  if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
    observer_->OnTouch(mojom::TouchpadTouchEvent::New(FetchTouchPoints(dev)));
  } else if (ev.type == EV_KEY) {
    auto button = EventCodeToInputTouchButton(ev.code);
    if (button.has_value()) {
      bool pressed = (ev.value != 0);
      observer_->OnButton(
          mojom::TouchpadButtonEvent::New(button.value(), pressed));
    }
  }
}

void TouchpadEvdevDelegate::InitializationFail(uint32_t custom_reason,
                                               const std::string& description) {
  observer_.ResetWithReason(custom_reason, description);
}

void TouchpadEvdevDelegate::ReportProperties(LibevdevWrapper* dev) {
  auto connected_event = mojom::TouchpadConnectedEvent::New();
  connected_event->max_x = std::max(dev->GetAbsMaximum(ABS_X), 0);
  connected_event->max_y = std::max(dev->GetAbsMaximum(ABS_Y), 0);
  connected_event->max_pressure =
      std::max(dev->GetAbsMaximum(ABS_MT_PRESSURE), 0);
  if (dev->HasEventType(EV_KEY)) {
    std::vector<unsigned int> codes{BTN_LEFT, BTN_MIDDLE, BTN_RIGHT};
    for (const auto code : codes) {
      if (dev->HasEventCode(EV_KEY, code)) {
        auto button = EventCodeToInputTouchButton(code);
        if (button.has_value()) {
          connected_event->buttons.push_back(button.value());
        }
      }
    }
  }
  observer_->OnConnected(std::move(connected_event));
}

}  // namespace diagnostics
