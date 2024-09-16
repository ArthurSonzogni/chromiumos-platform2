// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_EMBEDDING_MODEL_EMBEDDING_MODEL_SERVICE_H_
#define ODML_EMBEDDING_MODEL_EMBEDDING_MODEL_SERVICE_H_

#include <utility>

#include <metrics/metrics_library.h>
#include <mojo/public/cpp/bindings/receiver.h>
#include <mojo/public/cpp/bindings/receiver_set.h>

#include "odml/embedding_model/model_factory.h"
#include "odml/mojom/embedding_model.mojom.h"
#include "odml/mojom/on_device_model_service.mojom.h"

namespace embedding_model {

// EmbeddingModelService provides the service that loads embedding model, which
// can be used for inference, that is converting a string into vector
// representation.
class EmbeddingModelService : public mojom::OnDeviceEmbeddingModelService {
 public:
  EmbeddingModelService(raw_ref<MetricsLibraryInterface> metrics,
                        raw_ref<ModelFactory> factory);
  ~EmbeddingModelService() override;

  EmbeddingModelService(const EmbeddingModelService&) = delete;
  EmbeddingModelService& operator=(const EmbeddingModelService&) = delete;

  void AddReceiver(
      mojo::PendingReceiver<mojom::OnDeviceEmbeddingModelService> receiver) {
    receiver_set_.Add(this, std::move(receiver),
                      base::SequencedTaskRunner::GetCurrentDefault());
  }

  void LoadEmbeddingModel(
      const base::Uuid& uuid,
      mojo::PendingReceiver<mojom::OnDeviceEmbeddingModel> model,
      mojo::PendingRemote<on_device_model::mojom::PlatformModelProgressObserver>
          progress_observer,
      LoadEmbeddingModelCallback callback) override;

 private:
  const raw_ref<MetricsLibraryInterface> metrics_;
  const raw_ref<ModelFactory> factory_;

  mojo::ReceiverSet<mojom::OnDeviceEmbeddingModelService> receiver_set_;

  base::WeakPtrFactory<EmbeddingModelService> weak_ptr_factory_{this};
};

}  // namespace embedding_model

#endif  // ODML_EMBEDDING_MODEL_EMBEDDING_MODEL_SERVICE_H_
