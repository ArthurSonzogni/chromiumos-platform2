// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/mantis/processor.h"

#include <vector>

#include <base/check.h>
#include <base/containers/fixed_flat_map.h>
#include <base/run_loop.h>
#include <mojo_service_manager/lib/connect.h>
#include <mojo_service_manager/lib/mojom/service_manager.mojom.h>

#include "odml/mantis/lib_api.h"
#include "odml/mojom/big_buffer.mojom.h"
#include "odml/mojom/cros_safety.mojom.h"
#include "odml/mojom/cros_safety_service.mojom.h"
#include "odml/mojom/mantis_processor.mojom.h"
#include "odml/mojom/mantis_service.mojom.h"

namespace mantis {

namespace {
using mojom::InitializeResult;
using mojom::MantisError;
using mojom::MantisResult;
using mojom::SafetyClassifierVerdict;

constexpr auto kMapStatusToError =
    base::MakeFixedFlatMap<MantisStatus, MantisError>({
        {MantisStatus::kProcessorNotInitialized,
         MantisError::kProcessorNotInitialized},
        {MantisStatus::kInputError, MantisError::kInputError},
        {MantisStatus::kProcessFailed, MantisError::kProcessFailed},
        {MantisStatus::kMissingSegmenter, MantisError::kMissingSegmenter},
    });
}  // namespace

MantisProcessor::MantisProcessor(
    MantisComponent component,
    const MantisAPI* api,
    mojo::PendingReceiver<mojom::MantisProcessor> receiver,
    raw_ref<mojo::Remote<chromeos::mojo_service_manager::mojom::ServiceManager>>
        service_manager,
    base::OnceCallback<void()> on_disconnected,
    base::OnceCallback<void(InitializeResult)> callback)
    : component_(component),
      api_(api),
      on_disconnected_(std::move(on_disconnected)) {
  CHECK(api_);
  if (!component_.processor) {
    LOG(ERROR) << "Processor is missing";
  }
  receiver_set_.Add(this, std::move(receiver));
  receiver_set_.set_disconnect_handler(base::BindRepeating(
      &MantisProcessor::OnDisconnected, base::Unretained(this)));

  (*service_manager)
      ->Request(
          /*service_name=*/chromeos::mojo_services::kCrosSafetyService,
          /*timeout=*/std::nullopt,
          safety_service_.BindNewPipeAndPassReceiver().PassPipe());

  safety_service_->CreateCloudSafetySession(
      cloud_safety_session_.BindNewPipeAndPassReceiver(),
      base::BindOnce(&MantisProcessor::OnCreateCloudSafetySessionComplete,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

MantisProcessor::~MantisProcessor() {
  api_->DestroyMantisComponent(component_);
}

void MantisProcessor::OnDisconnected() {
  if (!receiver_set_.empty()) {
    return;
  }
  if (on_disconnected_.is_null()) {
    return;
  }

  base::OnceClosure closure = std::move(on_disconnected_);

  // Don't use any member function or variable after this line, because the
  // MantisProcessor may be destroyed inside the callback.
  std::move(closure).Run();
}

void MantisProcessor::OnCreateCloudSafetySessionComplete(
    base::OnceCallback<void(InitializeResult)> callback,
    cros_safety::mojom::GetCloudSafetySessionResult result) {
  if (result != cros_safety::mojom::GetCloudSafetySessionResult::kOk) {
    LOG(ERROR) << "Can't initialize CloudSafetySession " << result;
    std::move(callback).Run(
        mojom::InitializeResult::kFailedToLoadSafetyService);
    return;
  }

  std::move(callback).Run(mojom::InitializeResult::kSuccess);
}

void MantisProcessor::Inpainting(const std::vector<uint8_t>& image,
                                 const std::vector<uint8_t>& mask,
                                 uint32_t seed,
                                 InpaintingCallback callback) {
  if (!component_.processor) {
    LOG(ERROR) << "Processor is missing";
    std::move(callback).Run(
        MantisResult::NewError(MantisError::kProcessorNotInitialized));
    return;
  }
  InpaintingResult lib_result =
      api_->Inpainting(component_.processor, image, mask, seed);
  if (lib_result.status != MantisStatus::kOk) {
    std::move(callback).Run(
        MantisResult::NewError(kMapStatusToError.at(lib_result.status)));
    return;
  }

  std::move(callback).Run(MantisResult::NewResultImage(lib_result.image));
}

void MantisProcessor::GenerativeFill(const std::vector<uint8_t>& image,
                                     const std::vector<uint8_t>& mask,
                                     uint32_t seed,
                                     const std::string& prompt,
                                     GenerativeFillCallback callback) {
  if (!component_.processor) {
    LOG(ERROR) << "Processor is missing";
    std::move(callback).Run(
        MantisResult::NewError(MantisError::kProcessorNotInitialized));
    return;
  }
  GenerativeFillResult lib_result =
      api_->GenerativeFill(component_.processor, image, mask, seed, prompt);
  if (lib_result.status != MantisStatus::kOk) {
    std::move(callback).Run(
        MantisResult::NewError(kMapStatusToError.at(lib_result.status)));
    return;
  }

  std::move(callback).Run(MantisResult::NewResultImage(lib_result.image));
}

void MantisProcessor::Segmentation(const std::vector<uint8_t>& image,
                                   const std::vector<uint8_t>& prior,
                                   SegmentationCallback callback) {
  if (!component_.segmenter) {
    std::move(callback).Run(
        MantisResult::NewError(MantisError::kMissingSegmenter));
    return;
  }

  SegmentationResult lib_result =
      api_->Segmentation(component_.segmenter, image, prior);
  if (lib_result.status != MantisStatus::kOk) {
    std::move(callback).Run(
        MantisResult::NewError(kMapStatusToError.at(lib_result.status)));
    return;
  }

  std::move(callback).Run(MantisResult::NewResultImage(lib_result.image));
}

void MantisProcessor::ClassifyImageSafety(
    const std::vector<uint8_t>& image, ClassifyImageSafetyCallback callback) {
  ClassifyImageSafetyInternal(image, "", std::move(callback));
}

// Verifies that the input image complies with Google's T&S policy. The text
// input is optional and is typically used when the input image is AI-generated
// based on a specific prompt.
void MantisProcessor::ClassifyImageSafetyInternal(
    const std::vector<uint8_t>& image,
    const std::string& text,
    base::OnceCallback<void(SafetyClassifierVerdict)> callback) {
  cloud_safety_session_->ClassifyImageSafety(
      cros_safety::mojom::SafetyRuleset::kMantis, text,
      mojo_base::mojom::BigBuffer::NewBytes(image),
      base::BindOnce(
          [](ClassifyImageSafetyCallback callback,
             cros_safety::mojom::SafetyClassifierVerdict result) {
            if (result != cros_safety::mojom::SafetyClassifierVerdict::kPass) {
              std::move(callback).Run(SafetyClassifierVerdict::kFail);
              return;
            }
            std::move(callback).Run(SafetyClassifierVerdict::kPass);
          },
          std::move(callback)));
}

}  // namespace mantis
