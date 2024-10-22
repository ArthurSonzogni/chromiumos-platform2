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
#include <chromeos/mojo/service_constants.h>
#include <mojo/public/cpp/bindings/receiver.h>
#include <mojo/public/cpp/bindings/receiver_set.h>
#include <mojo_service_manager/lib/mojom/service_manager.mojom.h>

#include "odml/mantis/lib_api.h"
#include "odml/mojom/cros_safety.mojom.h"
#include "odml/mojom/cros_safety_service.mojom.h"
#include "odml/mojom/mantis_processor.mojom.h"
#include "odml/mojom/mantis_service.mojom.h"

namespace mantis {

class MantisProcessor : public mojom::MantisProcessor {
 public:
  explicit MantisProcessor(
      MantisComponent component,
      const MantisAPI* api,
      mojo::PendingReceiver<mojom::MantisProcessor> receiver,
      raw_ref<
          mojo::Remote<chromeos::mojo_service_manager::mojom::ServiceManager>>
          service_manager,
      base::OnceCallback<void()> on_disconnected,
      base::OnceCallback<void(mantis::mojom::InitializeResult)> callback);

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

  void ClassifyImageSafety(const std::vector<uint8_t>& image,
                           ClassifyImageSafetyCallback callback) override;

 private:
  void OnDisconnected();

  void OnCreateCloudSafetySessionComplete(
      base::OnceCallback<void(mojom::InitializeResult)> callback,
      cros_safety::mojom::GetCloudSafetySessionResult result);

  void ClassifyImageSafetyInternal(
      const std::vector<uint8_t>& image,
      const std::string& text,
      base::OnceCallback<void(mojom::SafetyClassifierVerdict)> callback);

  MantisComponent component_;

  const raw_ptr<const MantisAPI> api_;

  mojo::Remote<cros_safety::mojom::CrosSafetyService> safety_service_;

  mojo::Remote<cros_safety::mojom::CloudSafetySession> cloud_safety_session_;

  mojo::ReceiverSet<mojom::MantisProcessor> receiver_set_;

  base::WeakPtrFactory<MantisProcessor> weak_ptr_factory_{this};

  base::OnceClosure on_disconnected_;
};

}  // namespace mantis

#endif  // ODML_MANTIS_PROCESSOR_H_
