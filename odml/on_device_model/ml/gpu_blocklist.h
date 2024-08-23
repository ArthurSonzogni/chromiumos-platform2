// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_ON_DEVICE_MODEL_ML_GPU_BLOCKLIST_H_
#define ODML_ON_DEVICE_MODEL_ML_GPU_BLOCKLIST_H_

#include "odml/on_device_model/ml/chrome_ml_api.h"

namespace ml {

// A policy controlling what kinds of GPUs are allowed to run the service.
struct GpuBlocklist final {
  bool skip_for_testing = false;

  // Checks if the GPU is on the blocklist.
  bool IsGpuBlocked(const ChromeMLAPI& api) const;
};

}  // namespace ml

#endif  // ODML_ON_DEVICE_MODEL_ML_GPU_BLOCKLIST_H_
