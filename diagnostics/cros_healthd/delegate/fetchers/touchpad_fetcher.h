// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_DELEGATE_FETCHERS_TOUCHPAD_FETCHER_H_
#define DIAGNOSTICS_CROS_HEALTHD_DELEGATE_FETCHERS_TOUCHPAD_FETCHER_H_

#include <memory>
#include <string>
#include <vector>

#include <base/types/expected.h>
#include <brillo/udev/udev.h>

#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics {
base::expected<std::vector<ash::cros_healthd::mojom::TouchpadDevicePtr>,
               std::string>
PopulateTouchpadDevices(std::unique_ptr<brillo::Udev> udev,
                        std::string root_path);
}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_DELEGATE_FETCHERS_TOUCHPAD_FETCHER_H_
