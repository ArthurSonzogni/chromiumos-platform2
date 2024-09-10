// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_MANTIS_PROCESSOR_H_
#define ODML_MANTIS_PROCESSOR_H_

#include <string>
#include <utility>
#include <vector>

#include <base/memory/weak_ptr.h>
#include <base/task/sequenced_task_runner.h>
#include <base/task/task_runner.h>
#include <mojo/public/cpp/bindings/receiver.h>
#include <mojo/public/cpp/bindings/receiver_set.h>

#include "odml/mojom/mantis_service.mojom.h"

namespace mantis {

class MantisProcessor : public mojom::MantisProcessor {
 public:
  MantisProcessor() = default;
  ~MantisProcessor() = default;

  MantisProcessor(const MantisProcessor&) = delete;
  MantisProcessor& operator=(const MantisProcessor&) = delete;

  void AddReceiver(mojo::PendingReceiver<mojom::MantisProcessor> receiver) {
    receiver_set_.Add(this, std::move(receiver),
                      base::SequencedTaskRunner::GetCurrentDefault());
  }

  void Inpainting(std::vector<uint8> image,
                  std::vector<unit8> mask,
                  int seed,
                  InpaintingCallback callback) override;

  void GenerativeFill(std::vector<uint8> image,
                      std::vector<unit8> mask,
                      int seed,
                      std::string prompt,
                      GenerativeFillCallback callback) override;

 private:
  mojo::ReceiverSet<mojom::MantisProcessor> receiver_set_;

  base::WeakPtrFactory<MantisProcessor> weak_ptr_factory_{this};
};

}  // namespace mantis

#endif  // ODML_MANTIS_PROCESSOR_H_
