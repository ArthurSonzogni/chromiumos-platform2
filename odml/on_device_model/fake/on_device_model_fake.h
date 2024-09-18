// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_ON_DEVICE_MODEL_FAKE_ON_DEVICE_MODEL_FAKE_H_
#define ODML_ON_DEVICE_MODEL_FAKE_ON_DEVICE_MODEL_FAKE_H_

#include <metrics/metrics_library.h>

#include "odml/utils/odml_shim_loader_mock.h"

namespace fake_ml {

void SetupFakeChromeML(raw_ref<MetricsLibraryInterface> metrics,
                       raw_ref<odml::OdmlShimLoaderMock> shim_loader);

}  // namespace fake_ml

#endif  // ODML_ON_DEVICE_MODEL_FAKE_ON_DEVICE_MODEL_FAKE_H_
