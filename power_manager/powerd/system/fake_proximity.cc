// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/system/fake_proximity.h"

#include <utility>
#include <vector>

namespace power_manager {
namespace system {

FakeProximity::FakeProximity() = default;

cros::mojom::DeviceType FakeProximity::GetDeviceType() const {
  return cros::mojom::DeviceType::PROXIMITY;
}

void FakeProximity::GetAllEvents(GetAllEventsCallback callback) {
  std::vector<cros::mojom::IioEventPtr> events;
  events.push_back(cros::mojom::IioEvent::New(
      cros::mojom::IioChanType::IIO_PROXIMITY,
      cros::mojom::IioEventType::IIO_EV_TYPE_THRESH,
      cros::mojom::IioEventDirection::IIO_EV_DIR_EITHER, 0 /* channel */,
      0 /* timestamp */));

  events.push_back(cros::mojom::IioEvent::New(
      cros::mojom::IioChanType::IIO_PROXIMITY,
      cros::mojom::IioEventType::IIO_EV_TYPE_THRESH,
      cros::mojom::IioEventDirection::IIO_EV_DIR_EITHER, 1 /* channel */,
      0 /* timestamp */));

  std::move(callback).Run(std::move(events));
}

}  // namespace system
}  // namespace power_manager
