// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_MANTIS_PROCESSOR_H_
#define ODML_MANTIS_PROCESSOR_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/memory/weak_ptr.h>
#include <base/task/sequenced_task_runner.h>
#include <base/task/task_runner.h>
#include <mojo/public/cpp/bindings/receiver.h>
#include <mojo/public/cpp/bindings/receiver_set.h>

#include "odml/mantis/lib_api.h"
#include "odml/mojom/mantis_processor.mojom.h"

namespace mantis {

class MantisProcessor : public mojom::MantisProcessor {
 public:
  explicit MantisProcessor(
      MantisComponent component,
      const MantisAPI* api,
      mojo::PendingReceiver<mojom::MantisProcessor> receiver,
      base::OnceCallback<void()> on_disconnected);

  ~MantisProcessor();

  MantisProcessor(const MantisProcessor&) = delete;
  MantisProcessor& operator=(const MantisProcessor&) = delete;

  void AddReceiver(mojo::PendingReceiver<mojom::MantisProcessor> receiver) {
    receiver_set_.Add(this, std::move(receiver),
                      base::SequencedTaskRunner::GetCurrentDefault());
  }

  void Inpainting(const std::vector<uint8_t>& image,
                  const std::vector<uint8_t>& mask,
                  uint32_t seed,
                  InpaintingCallback callback) override;

  void GenerativeFill(const std::vector<uint8_t>& image,
                      const std::vector<uint8_t>& mask,
                      uint32_t seed,
                      const std::string& prompt,
                      GenerativeFillCallback callback) override;

  void Segmentation(const std::vector<uint8_t>& image,
                    const std::vector<uint8_t>& prior,
                    SegmentationCallback callback) override;

 private:
  void OnDisconnected();

  MantisComponent component_;

  const raw_ptr<const MantisAPI> api_;

  mojo::ReceiverSet<mojom::MantisProcessor> receiver_set_;

  base::WeakPtrFactory<MantisProcessor> weak_ptr_factory_{this};

  base::OnceClosure on_disconnected_;
};

}  // namespace mantis

#endif  // ODML_MANTIS_PROCESSOR_H_
