// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_ON_DEVICE_MODEL_ON_DEVICE_MODEL_SERVICE_H_
#define ODML_ON_DEVICE_MODEL_ON_DEVICE_MODEL_SERVICE_H_

#include <base/containers/unique_ptr_adapters.h>
#include <base/task/sequenced_task_runner.h>
#include <base/task/task_runner.h>
#include <base/types/expected.h>
#include <base/uuid.h>
#include <build/build_config.h>
#include <mojo/public/cpp/bindings/receiver.h>
#include <mojo/public/cpp/bindings/receiver_set.h>

#include <memory>
#include <set>
#include <utility>

#include "odml/mojom/on_device_model.mojom.h"
#include "odml/mojom/on_device_model_service.mojom.h"
#include "odml/on_device_model/platform_model_loader.h"
#include "odml/on_device_model/public/cpp/on_device_model.h"

namespace on_device_model {

class OnDeviceModelService : public mojom::OnDeviceModelPlatformService {
 public:
  static mojom::PerformanceClass GetEstimatedPerformanceClass();

  OnDeviceModelService();
  explicit OnDeviceModelService(
      mojo::PendingReceiver<mojom::OnDeviceModelPlatformService> receiver);
  ~OnDeviceModelService() override;

  OnDeviceModelService(const OnDeviceModelService&) = delete;
  OnDeviceModelService& operator=(const OnDeviceModelService&) = delete;

  void AddReceiver(
      mojo::PendingReceiver<mojom::OnDeviceModelPlatformService> receiver) {
    receiver_set_.Add(this, std::move(receiver),
                      base::SequencedTaskRunner::GetCurrentDefault());
  }

  // mojom::OnDeviceModelPlatformService:
  void LoadPlatformModel(const base::Uuid& uuid,
                         mojo::PendingReceiver<mojom::OnDeviceModel> model,
                         LoadPlatformModelCallback callback) override;

  void LoadModel(mojom::LoadModelParamsPtr params,
                 mojo::PendingReceiver<mojom::OnDeviceModel> model,
                 LoadPlatformModelCallback callback);

  size_t NumModelsForTesting() const { return models_.size(); }

 private:
  static base::expected<std::unique_ptr<OnDeviceModel>, mojom::LoadModelResult>
  CreateModel(mojom::LoadModelParamsPtr params);

  void DeleteModel(base::WeakPtr<mojom::OnDeviceModel> model);

  mojo::ReceiverSet<mojom::OnDeviceModelPlatformService> receiver_set_;
  std::set<std::unique_ptr<mojom::OnDeviceModel>, base::UniquePtrComparator>
      models_;
  std::unique_ptr<PlatformModelLoader> platform_model_loader_;
};

}  // namespace on_device_model

#endif  // ODML_ON_DEVICE_MODEL_ON_DEVICE_MODEL_SERVICE_H_