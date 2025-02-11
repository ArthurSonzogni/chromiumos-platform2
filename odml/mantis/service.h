// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_MANTIS_SERVICE_H_
#define ODML_MANTIS_SERVICE_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/memory/raw_ref.h>
#include <base/memory/weak_ptr.h>
#include <base/task/sequenced_task_runner.h>
#include <base/task/task_runner.h>
#include <base/types/expected.h>
#include <metrics/metrics_library.h>
#include <mojo/public/cpp/bindings/pending_receiver.h>
#include <mojo/public/cpp/bindings/receiver.h>
#include <mojo/public/cpp/bindings/receiver_set.h>

#include "odml/cros_safety/safety_service_manager.h"
#include "odml/mantis/processor.h"
#include "odml/mojom/mantis_processor.mojom.h"
#include "odml/mojom/mantis_service.mojom.h"
#include "odml/utils/odml_shim_loader.h"
#include "odml/utils/performance_timer.h"

namespace mantis {

class MantisService : public mojom::MantisService {
 public:
  explicit MantisService(
      raw_ref<MetricsLibraryInterface> metrics_lib,
      raw_ref<odml::OdmlShimLoader> shim_loader,
      raw_ref<cros_safety::SafetyServiceManager> safety_service_manager);
  ~MantisService() = default;

  MantisService(const MantisService&) = delete;
  MantisService& operator=(const MantisService&) = delete;

  void AddReceiver(mojo::PendingReceiver<mojom::MantisService> receiver) {
    receiver_set_.Add(this, std::move(receiver),
                      base::SequencedTaskRunner::GetCurrentDefault());
  }

  raw_ref<MantisProcessor> processor() { return raw_ref(*processor_); }

  // mojom::MantisService:
  void Initialize(mojo::PendingRemote<mojom::PlatformModelProgressObserver>
                      progress_observer,
                  mojo::PendingReceiver<mojom::MantisProcessor> processor,
                  InitializeCallback callback) override;

  void GetMantisFeatureStatus(GetMantisFeatureStatusCallback callback) override;

  bool IsProcessorNullForTesting() { return processor_ == nullptr; }

 protected:
  virtual void CreateMantisProcessor(
      raw_ref<MetricsLibraryInterface> metrics_lib,
      scoped_refptr<base::SequencedTaskRunner> mantis_api_runner,
      const MantisAPI* api,
      mojo::PendingReceiver<mojom::MantisProcessor> receiver,
      raw_ref<cros_safety::SafetyServiceManager> safety_service_manager,
      base::OnceCallback<void()> on_disconnected,
      base::OnceCallback<void(mantis::mojom::InitializeResult)> callback,
      odml::PerformanceTimer::Ptr timer,
      MantisComponent component);

 private:
  // Stores request data to initialize processor while we already have an
  // ongoing one. We will use the data to send the response after we're done.
  struct PendingProcessor {
    mojo::PendingReceiver<mojom::MantisProcessor> processor;
    InitializeCallback callback;
  };

  // Duplicate from on_device_model_service.h
  // TODO(b/368261193): Move this function to a common place and reuse it here.
  template <typename FuncType,
            typename CallbackType,
            typename FailureType,
            typename... Args>
  bool RetryIfShimIsNotReady(FuncType func,
                             CallbackType& callback,
                             FailureType failure_result,
                             Args&... args);

  void DeleteProcessor();

  void OnInstallDlcComplete(
      mojo::PendingReceiver<mojom::MantisProcessor> processor,
      InitializeCallback callback,
      odml::PerformanceTimer::Ptr timer,
      base::expected<base::FilePath, std::string> result);

  void OnDlcProgress(
      std::shared_ptr<mojo::Remote<mojom::PlatformModelProgressObserver>>
          progress_observer,
      double progress);

  void NotifyPendingProcessors();

  const raw_ref<MetricsLibraryInterface> metrics_lib_;

  const scoped_refptr<base::SequencedTaskRunner> mantis_api_runner_;

  const raw_ref<odml::OdmlShimLoader> shim_loader_;

  raw_ref<cros_safety::SafetyServiceManager> safety_service_manager_;

  bool is_initializing_processor_ = false;
  std::vector<PendingProcessor> pending_processors_;
  std::unique_ptr<MantisProcessor> processor_;

  mojo::ReceiverSet<mojom::MantisService> receiver_set_;

  base::WeakPtrFactory<MantisService> weak_ptr_factory_{this};
};

}  // namespace mantis

#endif  // ODML_MANTIS_SERVICE_H_
