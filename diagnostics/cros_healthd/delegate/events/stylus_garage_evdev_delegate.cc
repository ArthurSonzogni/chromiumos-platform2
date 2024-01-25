// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/delegate/events/stylus_garage_evdev_delegate.h"

#include <linux/input.h>
#include <linux/input-event-codes.h>

#include <cstdint>
#include <string>
#include <utility>

#include <mojo/public/cpp/bindings/pending_remote.h>
#include <mojo/public/cpp/bindings/remote.h>

#include "diagnostics/cros_healthd/delegate/utils/libevdev_wrapper.h"
#include "diagnostics/cros_healthd/mojom/executor.mojom.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

}  // namespace

StylusGarageEvdevDelegate::StylusGarageEvdevDelegate(
    mojo::PendingRemote<mojom::StylusGarageObserver> observer)
    : observer_(std::move(observer)) {}

bool StylusGarageEvdevDelegate::IsTarget(LibevdevWrapper* dev) {
  return dev->HasEventCode(EV_SW, SW_PEN_INSERTED);
}

void StylusGarageEvdevDelegate::FireEvent(const input_event& ev,
                                          LibevdevWrapper* dev) {
  if (ev.type != EV_SW) {
    return;
  }

  if (ev.code == SW_PEN_INSERTED) {
    if (ev.value == 1) {
      observer_->OnInsert();
    } else {
      observer_->OnRemove();
    }
  }
}

void StylusGarageEvdevDelegate::InitializationFail(
    uint32_t custom_reason, const std::string& description) {
  observer_.ResetWithReason(custom_reason, description);
}

void StylusGarageEvdevDelegate::ReportProperties(LibevdevWrapper* dev) {}

}  // namespace diagnostics
