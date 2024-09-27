// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/mantis/processor.h"

#include <fstream>
#include <vector>

#include <base/check.h>
#include <base/containers/fixed_flat_map.h>

#include "odml/mantis/lib_api.h"
#include "odml/mojom/mantis_processor.mojom.h"

namespace mantis {

namespace {
using mojom::MantisError;
using mojom::MantisResult;

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
    base::OnceCallback<void()> on_disconnected)
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

}  // namespace mantis
