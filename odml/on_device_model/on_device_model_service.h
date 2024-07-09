// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_ON_DEVICE_MODEL_ON_DEVICE_MODEL_SERVICE_H_
#define ODML_ON_DEVICE_MODEL_ON_DEVICE_MODEL_SERVICE_H_

#include <base/containers/unique_ptr_adapters.h>
#include <base/memory/raw_ref.h>
#include <base/memory/scoped_refptr.h>
#include <base/memory/weak_ptr.h>
#include <base/task/sequenced_task_runner.h>
#include <base/task/task_runner.h>
#include <base/types/expected.h>
#include <base/uuid.h>
#include <build/build_config.h>
#include <metrics/metrics_library.h>
#include <mojo/public/cpp/bindings/receiver.h>
#include <mojo/public/cpp/bindings/receiver_set.h>

#include <memory>
#include <set>
#include <utility>

#include "odml/mojom/on_device_model.mojom.h"
#include "odml/mojom/on_device_model_service.mojom.h"
#include "odml/on_device_model/platform_model_loader.h"
#include "odml/on_device_model/public/cpp/on_device_model.h"
#include "odml/utils/odml_shim_loader.h"

namespace on_device_model {

class OnDeviceModelService : public mojom::OnDeviceModelPlatformService {
 public:
  static mojom::PerformanceClass GetEstimatedPerformanceClass(
      raw_ref<MetricsLibraryInterface> metrics,
      raw_ref<odml::OdmlShimLoader> shim_loader);

  OnDeviceModelService(raw_ref<MetricsLibraryInterface> metrics,
                       raw_ref<odml::OdmlShimLoader> shim_loader);
  ~OnDeviceModelService() override;

  OnDeviceModelService(const OnDeviceModelService&) = delete;
  OnDeviceModelService& operator=(const OnDeviceModelService&) = delete;

  void AddReceiver(
      mojo::PendingReceiver<mojom::OnDeviceModelPlatformService> receiver) {
    receiver_set_.Add(this, std::move(receiver),
                      base::SequencedTaskRunner::GetCurrentDefault());
  }

  // mojom::OnDeviceModelPlatformService:
  void LoadPlatformModel(
      const base::Uuid& uuid,
      mojo::PendingReceiver<mojom::OnDeviceModel> model,
      mojo::PendingRemote<mojom::PlatformModelProgressObserver>
          progress_observer,
      LoadPlatformModelCallback callback) override;

  void GetPlatformModelState(const base::Uuid& uuid,
                             GetPlatformModelStateCallback callback) override;

  void LoadModel(mojom::LoadModelParamsPtr params,
                 mojo::PendingReceiver<mojom::OnDeviceModel> model,
                 LoadPlatformModelCallback callback);

  size_t NumModelsForTesting() const { return models_.size(); }

 private:
  static base::expected<std::unique_ptr<OnDeviceModel>, mojom::LoadModelResult>
  CreateModel(raw_ref<MetricsLibraryInterface> metrics,
              raw_ref<odml::OdmlShimLoader> shim_loader,
              mojom::LoadModelParamsPtr params);

  void DeleteModel(base::WeakPtr<mojom::OnDeviceModel> model);

  const raw_ref<MetricsLibraryInterface> metrics_;
  const raw_ref<odml::OdmlShimLoader> shim_loader_;
  mojo::ReceiverSet<mojom::OnDeviceModelPlatformService> receiver_set_;
  std::set<std::unique_ptr<mojom::OnDeviceModel>, base::UniquePtrComparator>
      models_;
  std::unique_ptr<PlatformModelLoader> platform_model_loader_;

  base::WeakPtrFactory<OnDeviceModelService> weak_ptr_factory_{this};
};

}  // namespace on_device_model

#endif  // ODML_ON_DEVICE_MODEL_ON_DEVICE_MODEL_SERVICE_H_
