// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_ON_DEVICE_MODEL_ML_UTILS_H_
#define ODML_ON_DEVICE_MODEL_ML_UTILS_H_

#include "odml/mojom/on_device_model.mojom.h"
#include "odml/on_device_model/ml/chrome_ml.h"

namespace ml {

// Returns the estimated performance class of this device based on a small
// benchmark.
on_device_model::mojom::PerformanceClass GetEstimatedPerformanceClass(
    const ChromeML& chrome_ml);

}  // namespace ml

#endif  // ODML_ON_DEVICE_MODEL_ML_UTILS_H_
