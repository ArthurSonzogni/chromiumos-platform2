// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_ON_DEVICE_MODEL_ON_DEVICE_MODEL_FACTORY_IMPL_H_
#define ODML_ON_DEVICE_MODEL_ON_DEVICE_MODEL_FACTORY_IMPL_H_

#include "odml/on_device_model/on_device_model_factory.h"

#include <base/functional/callback.h>
#include <base/memory/raw_ref.h>
#include <base/types/expected.h>
#include <metrics/metrics_library.h>

#include <memory>

#include "odml/mojom/on_device_model.mojom.h"
#include "odml/mojom/on_device_model_service.mojom.h"
#include "odml/on_device_model/public/cpp/on_device_model.h"
#include "odml/utils/odml_shim_loader.h"

namespace on_device_model {

class OndeviceModelFactoryImpl : public OndeviceModelFactory {
 public:
  OndeviceModelFactoryImpl() = default;
  ~OndeviceModelFactoryImpl() override = default;

  base::expected<std::unique_ptr<OnDeviceModel>, mojom::LoadModelResult>
  CreateModel(raw_ref<MetricsLibraryInterface> metrics,
              raw_ref<odml::OdmlShimLoader> shim_loader,
              mojom::LoadModelParamsPtr params,
              base::OnceClosure on_complete) override;

  mojom::PerformanceClass GetEstimatedPerformanceClass(
      raw_ref<MetricsLibraryInterface> metrics,
      raw_ref<odml::OdmlShimLoader> shim_loader) override;
};

}  // namespace on_device_model

#endif  // ODML_ON_DEVICE_MODEL_ON_DEVICE_MODEL_FACTORY_IMPL_H_
