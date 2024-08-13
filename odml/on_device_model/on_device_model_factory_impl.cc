// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/on_device_model/on_device_model_factory_impl.h"

#include <memory>
#include <utility>

#include <base/functional/callback.h>
#include <base/memory/raw_ref.h>
#include <base/types/expected.h>
#include <metrics/metrics_library.h>

#include "odml/mojom/on_device_model.mojom.h"
#include "odml/mojom/on_device_model_service.mojom.h"
#include "odml/on_device_model/ml/chrome_ml.h"
#include "odml/on_device_model/ml/on_device_model_executor.h"
#include "odml/on_device_model/ml/utils.h"
#include "odml/on_device_model/public/cpp/on_device_model.h"
#include "odml/utils/odml_shim_loader.h"

namespace on_device_model {

// static
base::expected<std::unique_ptr<OnDeviceModel>, mojom::LoadModelResult>
OndeviceModelFactoryImpl::CreateModel(raw_ref<MetricsLibraryInterface> metrics,
                                      raw_ref<odml::OdmlShimLoader> shim_loader,
                                      mojom::LoadModelParamsPtr params,
                                      base::OnceClosure on_complete) {
  auto* chrome_ml = ml::ChromeML::Get(metrics, shim_loader);
  if (!chrome_ml) {
    return base::unexpected(mojom::LoadModelResult::kFailedToLoadLibrary);
  }

  return ml::OnDeviceModelExecutor::CreateWithResult(
      metrics, *chrome_ml, std::move(params), std::move(on_complete));
}

// static
mojom::PerformanceClass OndeviceModelFactoryImpl::GetEstimatedPerformanceClass(
    raw_ref<MetricsLibraryInterface> metrics,
    raw_ref<odml::OdmlShimLoader> shim_loader) {
  auto is_apu_available = shim_loader->Get<bool (*)()>("IsApuAvailable");
  if (is_apu_available && is_apu_available()) {
    return mojom::PerformanceClass::kHigh;
  }
  auto* chrome_ml = ml::ChromeML::Get(metrics, shim_loader);
  if (!chrome_ml) {
    return mojom::PerformanceClass::kFailedToLoadLibrary;
  }
  if (chrome_ml->IsGpuBlocked()) {
    return mojom::PerformanceClass::kGpuBlocked;
  }
  return ml::GetEstimatedPerformanceClass(metrics, *chrome_ml);
}

}  // namespace on_device_model
