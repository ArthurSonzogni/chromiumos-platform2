// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBBRILLO_BRILLO_UDEV_UTILS_H_
#define LIBBRILLO_BRILLO_UDEV_UTILS_H_

#include "brillo/udev/udev_device.h"

namespace brillo {

// Check if a device is removable. True if removable, false otherwise.
BRILLO_EXPORT bool IsRemovable(const brillo::UdevDevice& device);

}  // namespace brillo

#endif  // LIBBRILLO_BRILLO_UDEV_UTILS_H_
