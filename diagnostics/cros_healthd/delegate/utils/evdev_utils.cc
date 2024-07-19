// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/delegate/utils/evdev_utils.h"

#include <linux/input-event-codes.h>

#include <utility>
#include <vector>

#include <base/logging.h>

#include "diagnostics/cros_healthd/delegate/utils/libevdev_wrapper.h"
#include "diagnostics/mojom/public/cros_healthd_events.mojom.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

mojom::NullableUint32Ptr FetchOptionalUnsignedSlotValue(LibevdevWrapper* dev,
                                                        unsigned int slot,
                                                        unsigned int code) {
  int out_value;
  if (dev->FetchSlotValue(slot, code, &out_value) && out_value >= 0) {
    return mojom::NullableUint32::New(out_value);
  }
  return nullptr;
}

}  // namespace

std::vector<mojom::TouchPointInfoPtr> FetchTouchPoints(LibevdevWrapper* dev) {
  int num_slot = dev->GetNumSlots();
  if (num_slot < 0) {
    LOG(ERROR) << "The evdev device does not provide any slots.";
    return {};
  }
  std::vector<mojom::TouchPointInfoPtr> points;
  for (int slot = 0; slot < num_slot; ++slot) {
    int value_x, value_y, id;
    if (dev->FetchSlotValue(slot, ABS_MT_POSITION_X, &value_x) &&
        dev->FetchSlotValue(slot, ABS_MT_POSITION_Y, &value_y) &&
        dev->FetchSlotValue(slot, ABS_MT_TRACKING_ID, &id)) {
      // A non-negative tracking id is interpreted as a contact, and the value
      // -1 denotes an unused slot.
      if (id >= 0 && value_x >= 0 && value_y >= 0) {
        auto point_info = mojom::TouchPointInfo::New();
        point_info->tracking_id = id;
        point_info->x = value_x;
        point_info->y = value_y;
        point_info->pressure =
            FetchOptionalUnsignedSlotValue(dev, slot, ABS_MT_PRESSURE);
        point_info->touch_major =
            FetchOptionalUnsignedSlotValue(dev, slot, ABS_MT_TOUCH_MAJOR);
        point_info->touch_minor =
            FetchOptionalUnsignedSlotValue(dev, slot, ABS_MT_TOUCH_MINOR);
        points.push_back(std::move(point_info));
      }
    }
  }
  return points;
}

}  // namespace diagnostics
