// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_ON_DEVICE_MODEL_FAKE_FAKE_CHROME_ML_API_H_
#define ODML_ON_DEVICE_MODEL_FAKE_FAKE_CHROME_ML_API_H_

#include "odml/on_device_model/ml/chrome_ml_api.h"

namespace fake_ml {

const ChromeMLAPI* GetFakeMlApi();

}  // namespace fake_ml

#endif  // ODML_ON_DEVICE_MODEL_FAKE_FAKE_CHROME_ML_API_H_
