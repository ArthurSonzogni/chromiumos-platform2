// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/delegate/events/stylus_evdev_delegate.h"

#include <linux/input.h>
#include <linux/input-event-codes.h>

#include <algorithm>
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

StylusEvdevDelegate::StylusEvdevDelegate(
    mojo::PendingRemote<mojom::StylusObserver> observer)
    : observer_(std::move(observer)) {}

bool StylusEvdevDelegate::IsTarget(LibevdevWrapper* dev) {
  // - Typical non-pointer devices: touchscreens.
  // - Typical direct devices: touchscreens, drawing tablets.
  // - Use ABS_MT_TRACKING_ID to filter out touchscreen.
  return !dev->HasProperty(INPUT_PROP_POINTER) &&
         dev->HasProperty(INPUT_PROP_DIRECT) &&
         !dev->HasEventCode(EV_ABS, ABS_MT_TRACKING_ID) &&
         (dev->HasEventCode(EV_KEY, BTN_TOOL_PEN) ||
          dev->HasEventCode(EV_KEY, BTN_STYLUS) ||
          dev->HasEventCode(EV_KEY, BTN_STYLUS2));
}

void StylusEvdevDelegate::FireEvent(const input_event& ev,
                                    LibevdevWrapper* dev) {
  if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
    bool is_stylus_in_contact = dev->GetEventValue(EV_KEY, BTN_TOUCH);
    if (is_stylus_in_contact) {
      auto point_info = mojom::StylusTouchPointInfo::New();
      point_info->x = dev->GetEventValue(EV_ABS, ABS_X);
      point_info->y = dev->GetEventValue(EV_ABS, ABS_Y);
      point_info->pressure =
          mojom::NullableUint32::New(dev->GetEventValue(EV_ABS, ABS_PRESSURE));

      observer_->OnTouch(mojom::StylusTouchEvent::New(std::move(point_info)));
      last_event_has_touch_point_ = true;
    } else {
      // Don't repeatedly report events without the touch point.
      if (last_event_has_touch_point_) {
        observer_->OnTouch(mojom::StylusTouchEvent::New());
        last_event_has_touch_point_ = false;
      }
    }
  }
}

void StylusEvdevDelegate::InitializationFail(uint32_t custom_reason,
                                             const std::string& description) {
  observer_.ResetWithReason(custom_reason, description);
}

void StylusEvdevDelegate::ReportProperties(LibevdevWrapper* dev) {
  auto connected_event = mojom::StylusConnectedEvent::New();
  connected_event->max_x = std::max(dev->GetAbsMaximum(ABS_X), 0);
  connected_event->max_y = std::max(dev->GetAbsMaximum(ABS_Y), 0);
  connected_event->max_pressure = std::max(dev->GetAbsMaximum(ABS_PRESSURE), 0);
  observer_->OnConnected(std::move(connected_event));
}

}  // namespace diagnostics
