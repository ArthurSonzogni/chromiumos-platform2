// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_CORAL_SERVICE_H_
#define ODML_CORAL_SERVICE_H_

#include <utility>

#include <base/memory/weak_ptr.h>
#include <base/task/sequenced_task_runner.h>
#include <base/task/task_runner.h>
#include <mojo/public/cpp/bindings/receiver.h>
#include <mojo/public/cpp/bindings/receiver_set.h>

#include "odml/mojom/coral_service.mojom.h"

namespace coral {

class CoralService : public mojom::CoralService {
 public:
  CoralService() = default;
  ~CoralService() = default;

  CoralService(const CoralService&) = delete;
  CoralService& operator=(const CoralService&) = delete;

  void AddReceiver(mojo::PendingReceiver<mojom::CoralService> receiver) {
    receiver_set_.Add(this, std::move(receiver),
                      base::SequencedTaskRunner::GetCurrentDefault());
  }

  // mojom::CoralService:
  void Group(mojom::GroupRequestPtr request, GroupCallback callback) override;
  void CacheEmbeddings(mojom::CacheEmbeddingsRequestPtr request,
                       CacheEmbeddingsCallback callback) override;

 private:
  mojo::ReceiverSet<mojom::CoralService> receiver_set_;

  base::WeakPtrFactory<CoralService> weak_ptr_factory_{this};
};

}  // namespace coral

#endif  // ODML_CORAL_SERVICE_H_
