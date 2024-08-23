// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_ON_DEVICE_MODEL_ML_ON_DEVICE_MODEL_INTERNAL_H_
#define ODML_ON_DEVICE_MODEL_ML_ON_DEVICE_MODEL_INTERNAL_H_

#include <memory>

#include "odml/on_device_model/ml/gpu_blocklist.h"
#include "odml/on_device_model/ml/on_device_model_executor.h"

namespace ml {

class OnDeviceModelInternalImpl final {
 public:
  explicit OnDeviceModelInternalImpl(raw_ref<MetricsLibraryInterface> metrics,
                                     raw_ref<odml::OdmlShimLoader> shim_loader,
                                     GpuBlocklist gpu_blocklist);
  ~OnDeviceModelInternalImpl();

  base::expected<std::unique_ptr<OnDeviceModelExecutor>,
                 on_device_model::mojom::LoadModelResult>
  CreateModel(on_device_model::mojom::LoadModelParamsPtr params,
              base::OnceClosure on_complete) const;

  on_device_model::mojom::PerformanceClass GetEstimatedPerformanceClass() const;

 private:
  raw_ref<MetricsLibraryInterface> metrics_;
  raw_ref<odml::OdmlShimLoader> shim_loader_;
  GpuBlocklist gpu_blocklist_;
};

std::unique_ptr<const ml::OnDeviceModelInternalImpl>
GetOnDeviceModelInternalImpl(raw_ref<MetricsLibraryInterface> metrics,
                             raw_ref<odml::OdmlShimLoader> shim_loader);

}  // namespace ml

#endif  // ODML_ON_DEVICE_MODEL_ML_ON_DEVICE_MODEL_INTERNAL_H_
