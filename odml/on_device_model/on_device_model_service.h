// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_ON_DEVICE_MODEL_ON_DEVICE_MODEL_SERVICE_H_
#define ODML_ON_DEVICE_MODEL_ON_DEVICE_MODEL_SERVICE_H_

#include <memory>
#include <set>
#include <string>
#include <utility>

#include <base/containers/unique_ptr_adapters.h>
#include <base/memory/raw_ref.h>
#include <base/memory/scoped_refptr.h>
#include <base/memory/weak_ptr.h>
#include <base/task/sequenced_task_runner.h>
#include <base/task/task_runner.h>
#include <base/threading/sequence_bound.h>
#include <base/types/expected.h>
#include <base/uuid.h>
#include <build/build_config.h>
#include <metrics/metrics_library.h>
#include <mojo/public/cpp/bindings/receiver.h>
#include <mojo/public/cpp/bindings/receiver_set.h>
#include <mojo/public/cpp/bindings/unique_receiver_set.h>

#include "odml/mojom/on_device_model.mojom.h"
#include "odml/mojom/on_device_model_service.mojom.h"
#include "odml/on_device_model/ml/chrome_ml.h"
#include "odml/on_device_model/ml/ts_model.h"
#include "odml/on_device_model/platform_model_loader.h"
#include "odml/utils/odml_shim_loader.h"

namespace ml {
class TsHolder;
}

namespace on_device_model {

class OnDeviceModelService : public mojom::OnDeviceModelPlatformService {
 public:
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

  void GetEstimatedPerformanceClass(
      GetEstimatedPerformanceClassCallback callback) override;

  void FormatInput(const base::Uuid& uuid,
                   mojom::FormatFeature feature,
                   const base::flat_map<std::string, std::string>& fields,
                   FormatInputCallback callback) override;

  void ValidateSafetyResult(mojom::SafetyFeature safety_feature,
                            const std::string& text,
                            mojom::SafetyInfoPtr safety_info,
                            ValidateSafetyResultCallback callback) override;

  void LoadPlatformTextSafetyModel(
      const base::Uuid& uuid,
      mojo::PendingReceiver<mojom::TextSafetyModel> model,
      mojo::PendingRemote<mojom::PlatformModelProgressObserver>
          progress_observer,
      LoadPlatformModelCallback callback) override;

  void LoadModel(mojom::LoadModelParamsPtr params,
                 mojo::PendingReceiver<mojom::OnDeviceModel> model,
                 LoadPlatformModelCallback callback);

  void LoadModel(mojom::LoadModelParamsPtr params,
                 mojo::PendingReceiver<mojom::OnDeviceModel> model,
                 LoadPlatformModelCallback callback,
                 mojom::TextSafetyModel* ts_model);

  void LoadTextSafetyModel(mojom::TextSafetyModelParamsPtr params,
                           mojo::PendingReceiver<mojom::TextSafetyModel> model);

  size_t NumModelsForTesting() const { return models_.size(); }

 private:
  void DeleteModel(base::WeakPtr<mojom::OnDeviceModel> model);

  // If the shim is not ready, this function will retry the function with the
  // given arguments after the shim is ready, and the ownership of callback &
  // args will be taken in this kind of case.
  // Returns true if the function will be retried.
  template <typename FuncType,
            typename CallbackType,
            typename FailureType,
            typename... Args>
  bool RetryIfShimIsNotReady(FuncType func,
                             CallbackType& callback,
                             FailureType failure_result,
                             Args&... args);

  const raw_ref<MetricsLibraryInterface> metrics_;
  const raw_ref<odml::OdmlShimLoader> shim_loader_;
  mojo::ReceiverSet<mojom::OnDeviceModelPlatformService> receiver_set_;
  std::set<std::unique_ptr<mojom::OnDeviceModel>, base::UniquePtrComparator>
      models_;
  std::unique_ptr<PlatformModelLoader> platform_model_loader_;
  base::SequenceBound<ml::TsHolder> ts_holder_;

  base::WeakPtrFactory<OnDeviceModelService> weak_ptr_factory_{this};
};

}  // namespace on_device_model

#endif  // ODML_ON_DEVICE_MODEL_ON_DEVICE_MODEL_SERVICE_H_
