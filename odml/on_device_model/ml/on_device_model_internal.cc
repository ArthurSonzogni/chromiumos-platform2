// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/on_device_model/ml/on_device_model_internal.h"

#include <memory>
#include <utility>

#include <base/no_destructor.h>
#include <mojo/public/cpp/bindings/remote.h>

#include "odml/on_device_model/ml/chrome_ml.h"
#include "odml/on_device_model/ml/gpu_blocklist.h"
#include "odml/on_device_model/ml/on_device_model_executor.h"
#include "odml/on_device_model/ml/utils.h"

namespace ml {

base::expected<std::unique_ptr<OnDeviceModelExecutor>,
               on_device_model::mojom::LoadModelResult>
OnDeviceModelInternalImpl::CreateModel(
    on_device_model::mojom::LoadModelParamsPtr params,
    base::OnceClosure on_complete) const {
  auto* chrome_ml = ml::ChromeML::Get(metrics_, shim_loader_);
  if (!chrome_ml) {
    return base::unexpected(
        on_device_model::mojom::LoadModelResult::kFailedToLoadLibrary);
  }
  if (gpu_blocklist_.IsGpuBlocked(chrome_ml->api())) {
    return base::unexpected(
        on_device_model::mojom::LoadModelResult::kGpuBlocked);
  }

  return ml::OnDeviceModelExecutor::CreateWithResult(
      metrics_, *chrome_ml, std::move(params), std::move(on_complete));
}

on_device_model::mojom::PerformanceClass
OnDeviceModelInternalImpl::GetEstimatedPerformanceClass() const {
  auto is_apu_available = shim_loader_->Get<bool (*)()>("IsApuAvailable");
  if (is_apu_available && is_apu_available()) {
    return on_device_model::mojom::PerformanceClass::kHigh;
  }

  auto* chrome_ml = ml::ChromeML::Get(metrics_, shim_loader_);
  if (!chrome_ml) {
    return on_device_model::mojom::PerformanceClass::kFailedToLoadLibrary;
  }
  if (gpu_blocklist_.IsGpuBlocked(chrome_ml->api())) {
    return on_device_model::mojom::PerformanceClass::kGpuBlocked;
  }
  return ml::GetEstimatedPerformanceClass(metrics_, *chrome_ml);
}

OnDeviceModelInternalImpl::OnDeviceModelInternalImpl(
    raw_ref<MetricsLibraryInterface> metrics,
    raw_ref<odml::OdmlShimLoader> shim_loader,
    GpuBlocklist gpu_blocklist)
    : metrics_(metrics), shim_loader_(shim_loader) {}

OnDeviceModelInternalImpl::~OnDeviceModelInternalImpl() = default;

std::unique_ptr<const ml::OnDeviceModelInternalImpl>
GetOnDeviceModelInternalImpl(raw_ref<MetricsLibraryInterface> metrics,
                             raw_ref<odml::OdmlShimLoader> shim_loader) {
  return std::make_unique<OnDeviceModelInternalImpl>(metrics, shim_loader,
                                                     GpuBlocklist{});
}

}  // namespace ml
