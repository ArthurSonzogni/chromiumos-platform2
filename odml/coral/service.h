// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_CORAL_SERVICE_H_
#define ODML_CORAL_SERVICE_H_

#include <memory>
#include <utility>

#include <base/memory/raw_ref.h>
#include <base/memory/weak_ptr.h>
#include <base/task/sequenced_task_runner.h>
#include <base/task/task_runner.h>
#include <metrics/metrics_library.h>
#include <mojo/public/cpp/bindings/receiver.h>
#include <mojo/public/cpp/bindings/receiver_set.h>

#include "ml/mojom/machine_learning_service.mojom.h"
#include "odml/coral/clustering/engine.h"
#include "odml/coral/common.h"
#include "odml/coral/embedding/engine.h"
#include "odml/coral/metrics.h"
#include "odml/coral/title_generation/engine.h"
#include "odml/cros_safety/safety_service_manager.h"
#include "odml/mojom/coral_service.mojom.h"
#include "odml/mojom/embedding_model.mojom.h"
#include "odml/mojom/on_device_model_service.mojom.h"
#include "odml/session_state_manager/session_state_manager.h"
#include "odml/utils/performance_timer.h"

namespace coral {

class CoralService : public mojom::CoralService, public mojom::CoralProcessor {
 public:
  CoralService(
      raw_ref<MetricsLibraryInterface> metrics,
      raw_ref<on_device_model::mojom::OnDeviceModelPlatformService>
          on_device_model_service,
      raw_ref<embedding_model::mojom::OnDeviceEmbeddingModelService>
          embedding_model_service,
      odml::SessionStateManagerInterface* session_state_manager,
      raw_ref<cros_safety::SafetyServiceManager> safety_service_manager);

  // For test, where engine objects are passed in directly.
  CoralService(
      raw_ref<MetricsLibraryInterface> metrics,
      std::unique_ptr<EmbeddingEngineInterface> embedding_engine,
      std::unique_ptr<ClusteringEngineInterface> clustering_engine,
      std::unique_ptr<TitleGenerationEngineInterface> title_generation_engine);
  ~CoralService() = default;

  CoralService(const CoralService&) = delete;
  CoralService& operator=(const CoralService&) = delete;

  void AddReceiver(mojo::PendingReceiver<mojom::CoralService> receiver) {
    service_receiver_set_.Add(this, std::move(receiver),
                              base::SequencedTaskRunner::GetCurrentDefault());
  }

  // mojom::CoralProcessor:
  void Group(mojom::GroupRequestPtr request,
             mojo::PendingRemote<mojom::TitleObserver> observer,
             GroupCallback callback) override;
  void CacheEmbeddings(mojom::CacheEmbeddingsRequestPtr request,
                       CacheEmbeddingsCallback callback) override;

  // mojom::CoralService:
  void GroupDeprecated(mojom::GroupRequestPtr request,
                       mojo::PendingRemote<mojom::TitleObserver> observer,
                       GroupDeprecatedCallback callback) override;
  void CacheEmbeddingsDeprecated(
      mojom::CacheEmbeddingsRequestPtr request,
      CacheEmbeddingsDeprecatedCallback callback) override;
  void PrepareResource() override;
  void Initialize(
      mojo::PendingRemote<
          chromeos::machine_learning::mojom::MachineLearningService> ml_service,
      mojo::PendingReceiver<mojom::CoralProcessor> receiver) override;

 private:
  // These callbacks are used for asynchronous Engine::Process calls, performs
  // error handling then calls the next step.
  void OnEmbeddingResult(GroupCallback callback,
                         mojo::PendingRemote<mojom::TitleObserver> observer,
                         mojom::GroupRequestPtr request,
                         CoralResult<EmbeddingResponse> result);
  void OnClusteringResult(GroupCallback callback,
                          mojo::PendingRemote<mojom::TitleObserver> observer,
                          mojom::GroupRequestPtr request,
                          CoralResult<ClusteringResponse> result);
  void OnTitleGenerationResult(GroupCallback callback,
                               CoralResult<TitleGenerationResponse> result);

  // Send metrics and return result to callback.
  void HandleGroupResult(odml::PerformanceTimer::Ptr timer,
                         GroupCallback callback,
                         mojom::GroupResultPtr result);
  void HandleCacheEmbeddingsResult(odml::PerformanceTimer::Ptr timer,
                                   CacheEmbeddingsCallback callback,
                                   mojom::GroupRequestPtr request,
                                   CoralResult<EmbeddingResponse> embed_result);

  CoralMetrics metrics_;

  mojo::Remote<chromeos::machine_learning::mojom::MachineLearningService>
      ml_service_;

  std::unique_ptr<EmbeddingEngineInterface> embedding_engine_;
  std::unique_ptr<ClusteringEngineInterface> clustering_engine_;
  std::unique_ptr<TitleGenerationEngineInterface> title_generation_engine_;

  mojo::ReceiverSet<mojom::CoralService> service_receiver_set_;
  mojo::ReceiverSet<mojom::CoralProcessor> processor_receiver_set_;

  base::WeakPtrFactory<CoralService> weak_ptr_factory_{this};
};

}  // namespace coral

#endif  // ODML_CORAL_SERVICE_H_
