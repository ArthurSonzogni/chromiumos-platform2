// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/delegate/events/touchscreen_evdev_delegate.h"

#include <linux/input.h>
#include <linux/input-event-codes.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>

#include <mojo/public/cpp/bindings/pending_remote.h>
#include <mojo/public/cpp/bindings/remote.h>

#include "diagnostics/cros_healthd/delegate/utils/evdev_utils.h"
#include "diagnostics/cros_healthd/delegate/utils/libevdev_wrapper.h"
#include "diagnostics/cros_healthd/mojom/executor.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_events.mojom.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

}  // namespace

TouchscreenEvdevDelegate::TouchscreenEvdevDelegate(
    mojo::PendingRemote<mojom::TouchscreenObserver> observer)
    : observer_(std::move(observer)) {}

bool TouchscreenEvdevDelegate::IsTarget(LibevdevWrapper* dev) {
  // - Typical non-pointer devices: touchscreens.
  // - Typical direct devices: touchscreens, drawing tablets.
  // - Use ABS_MT_TRACKING_ID to filter out stylus.
  return !dev->HasProperty(INPUT_PROP_POINTER) &&
         dev->HasProperty(INPUT_PROP_DIRECT) &&
         dev->HasEventCode(EV_ABS, ABS_MT_TRACKING_ID);
}

void TouchscreenEvdevDelegate::FireEvent(const input_event& ev,
                                         LibevdevWrapper* dev) {
  if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
    observer_->OnTouch(
        mojom::TouchscreenTouchEvent::New(FetchTouchPoints(dev)));
  }
}

void TouchscreenEvdevDelegate::InitializationFail(
    uint32_t custom_reason, const std::string& description) {
  observer_.ResetWithReason(custom_reason, description);
}

void TouchscreenEvdevDelegate::ReportProperties(LibevdevWrapper* dev) {
  auto connected_event = mojom::TouchscreenConnectedEvent::New();
  connected_event->max_x = std::max(dev->GetAbsMaximum(ABS_X), 0);
  connected_event->max_y = std::max(dev->GetAbsMaximum(ABS_Y), 0);
  connected_event->max_pressure =
      std::max(dev->GetAbsMaximum(ABS_MT_PRESSURE), 0);
  observer_->OnConnected(std::move(connected_event));
}

}  // namespace diagnostics
