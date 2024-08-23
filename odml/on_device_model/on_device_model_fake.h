// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_ON_DEVICE_MODEL_ON_DEVICE_MODEL_FAKE_H_
#define ODML_ON_DEVICE_MODEL_ON_DEVICE_MODEL_FAKE_H_

#include <memory>

#include "odml/on_device_model/ml/on_device_model_internal.h"
#include "odml/utils/odml_shim_loader_mock.h"

namespace on_device_model {

std::unique_ptr<const ml::OnDeviceModelInternalImpl> GetOnDeviceModelFakeImpl(
    raw_ref<MetricsLibraryInterface> metrics,
    raw_ref<odml::OdmlShimLoaderMock> shim_loader);

}  // namespace on_device_model

#endif  // ODML_ON_DEVICE_MODEL_ON_DEVICE_MODEL_FAKE_H_
