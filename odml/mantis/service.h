// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_MANTIS_SERVICE_H_
#define ODML_MANTIS_SERVICE_H_

#include <utility>

#include <base/memory/weak_ptr.h>
#include <base/task/sequenced_task_runner.h>
#include <base/task/task_runner.h>
#include <mojo/public/cpp/bindings/receiver.h>
#include <mojo/public/cpp/bindings/receiver_set.h>

#include "odml/mojom/mantis_processor.mojom.h"
#include "odml/mojom/mantis_service.mojom.h"
#include "odml/mojom/on_device_model_service.mojom.h"
#include "odml/utils/odml_shim_loader.h"

namespace mantis {

class MantisService : public mojom::MantisService {
 public:
  explicit MantisService(raw_ref<odml::OdmlShimLoader> shim_loader);
  ~MantisService() = default;

  MantisService(const MantisService&) = delete;
  MantisService& operator=(const MantisService&) = delete;

  void AddReceiver(mojo::PendingReceiver<mojom::MantisService> receiver) {
    receiver_set_.Add(this, std::move(receiver),
                      base::SequencedTaskRunner::GetCurrentDefault());
  }

  // mojom::MantisService:
  void Initialize(
      mojo::PendingRemote<on_device_model::mojom::PlatformModelProgressObserver>
          progress_observer,
      mojo::PendingReceiver<mojom::MantisProcessor> processor,
      InitializeCallback callback);

 private:
  const raw_ref<odml::OdmlShimLoader> shim_loader_;

  mojo::ReceiverSet<mojom::MantisService> receiver_set_;

  base::WeakPtrFactory<MantisService> weak_ptr_factory_{this};
};

}  // namespace mantis

#endif  // ODML_MANTIS_SERVICE_H_
