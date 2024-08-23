// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/on_device_model/ml/gpu_blocklist.h"

namespace ml {

bool GpuBlocklist::IsGpuBlocked(const ChromeMLAPI& api) const {
  // We wouldn't block GPU on ChromeOS devices.
  return false;
}

}  // namespace ml
