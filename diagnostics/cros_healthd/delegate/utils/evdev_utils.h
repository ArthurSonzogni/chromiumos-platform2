// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_DELEGATE_UTILS_EVDEV_UTILS_H_
#define DIAGNOSTICS_CROS_HEALTHD_DELEGATE_UTILS_EVDEV_UTILS_H_

#include <vector>

#include "diagnostics/mojom/public/cros_healthd_events.mojom-forward.h"

namespace diagnostics {
class LibevdevWrapper;

// Returns touch points of the evdev device.
std::vector<ash::cros_healthd::mojom::TouchPointInfoPtr> FetchTouchPoints(
    LibevdevWrapper* dev);

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_DELEGATE_UTILS_EVDEV_UTILS_H_
