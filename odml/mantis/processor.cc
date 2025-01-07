// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/mantis/processor.h"

#include <memory>
#include <vector>

#include <base/check.h>
#include <base/containers/fixed_flat_map.h>
#include <base/logging.h>
#include <base/run_loop.h>
#include <mojo_service_manager/lib/connect.h>
#include <mojo_service_manager/lib/mojom/service_manager.mojom.h>

#include "odml/mantis/lib_api.h"
#include "odml/mojom/big_buffer.mojom.h"
#include "odml/mojom/cros_safety.mojom.h"
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

constexpr auto kMapSafetyResult =
    base::MakeFixedFlatMap<cros_safety::mojom::SafetyClassifierVerdict,
                           SafetyClassifierVerdict>({
        {cros_safety::mojom::SafetyClassifierVerdict::kPass,
         SafetyClassifierVerdict::kPass},
        {cros_safety::mojom::SafetyClassifierVerdict::kGenericError,
         SafetyClassifierVerdict::kFail},
        {cros_safety::mojom::SafetyClassifierVerdict::kFailedText,
         SafetyClassifierVerdict::kFailedText},
        {cros_safety::mojom::SafetyClassifierVerdict::kFailedImage,
         SafetyClassifierVerdict::kFailedImage},
        {cros_safety::mojom::SafetyClassifierVerdict::kServiceNotAvailable,
         SafetyClassifierVerdict::kServiceNotAvailable},
        {cros_safety::mojom::SafetyClassifierVerdict::kBackendFailure,
         SafetyClassifierVerdict::kBackendFailure},
    });
}  // namespace

MantisProcessor::MantisProcessor(
    MantisComponent component,
    const MantisAPI* api,
    mojo::PendingReceiver<mojom::MantisProcessor> receiver,
    raw_ref<cros_safety::SafetyServiceManager> safety_service_manager,
    base::OnceCallback<void()> on_disconnected,
    base::OnceCallback<void(InitializeResult)> callback)
    : component_(component),
      api_(api),
      safety_service_manager_(safety_service_manager),
      on_disconnected_(std::move(on_disconnected)) {
  CHECK(api_);
  if (!component_.processor) {
    LOG(ERROR) << "Processor is missing";
  }
  receiver_set_.Add(this, std::move(receiver));
  receiver_set_.set_disconnect_handler(base::BindRepeating(
      &MantisProcessor::OnDisconnected, base::Unretained(this)));

  safety_service_manager_->PrepareImageSafetyClassifier(base::BindOnce(
      [](base::OnceCallback<void(InitializeResult)> callback, bool is_enabled) {
        std::move(callback).Run(
            is_enabled ? mojom::InitializeResult::kSuccess
                       : mojom::InitializeResult::kFailedToLoadSafetyService);
      },
      std::move(callback)));
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
  ProcessImage(std::make_unique<MantisProcess>(MantisProcess{
      .image = image,
      .mask = mask,
      .seed = seed,
      .prompt = "",
      .callback = std::move(callback),
      .process_func = base::BindOnce(
          [](const MantisAPI* api, MantisComponent component,
             const std::vector<uint8_t>& image,
             const std::vector<uint8_t>& mask,
             uint32_t seed) -> mojom::MantisResultPtr {
            InpaintingResult lib_result =
                api->Inpainting(component.processor, image, mask, seed);
            if (lib_result.status != MantisStatus::kOk) {
              return MantisResult::NewError(
                  kMapStatusToError.at(lib_result.status));
            }

            return MantisResult::NewResultImage(lib_result.image);
          },
          api_, component_, image, mask, seed),
  }));
}

void MantisProcessor::GenerativeFill(const std::vector<uint8_t>& image,
                                     const std::vector<uint8_t>& mask,
                                     uint32_t seed,
                                     const std::string& prompt,
                                     GenerativeFillCallback callback) {
  ProcessImage(std::make_unique<MantisProcess>(MantisProcess{
      .image = image,
      .mask = mask,
      .seed = seed,
      .prompt = prompt,
      .callback = std::move(callback),
      .process_func = base::BindOnce(
          [](const MantisAPI* api, MantisComponent component,
             const std::vector<uint8_t>& image,
             const std::vector<uint8_t>& mask, uint32_t seed,
             const std::string& prompt) -> mojom::MantisResultPtr {
            GenerativeFillResult lib_result = api->GenerativeFill(
                component.processor, image, mask, seed, prompt);
            if (lib_result.status != MantisStatus::kOk) {
              return MantisResult::NewError(
                  kMapStatusToError.at(lib_result.status));
            }

            return MantisResult::NewResultImage(lib_result.image);
          },
          api_, component_, image, mask, seed, prompt),
  }));
}

void MantisProcessor::Segmentation(const std::vector<uint8_t>& image,
                                   const std::vector<uint8_t>& prior,
                                   SegmentationCallback callback) {
  if (!component_.segmenter) {
    std::move(callback).Run(
        MantisResult::NewError(MantisError::kMissingSegmenter));
    return;
  }

  ClassifyImageSafetyInternal(
      // Input image checking doesn't require a prompt
      image, /*text=*/"",
      base::BindOnce(
          [](SegmentationCallback callback, const MantisAPI* api,
             MantisComponent component, const std::vector<uint8_t>& image,
             const std::vector<uint8_t>& prior,
             SafetyClassifierVerdict result) {
            if (result != SafetyClassifierVerdict::kPass) {
              std::move(callback).Run(
                  MantisResult::NewError(MantisError::kInputSafetyError));
              return;
            }

            SegmentationResult lib_result =
                api->Segmentation(component.segmenter, image, prior);
            if (lib_result.status != MantisStatus::kOk) {
              std::move(callback).Run(MantisResult::NewError(
                  kMapStatusToError.at(lib_result.status)));
              return;
            }

            std::move(callback).Run(
                MantisResult::NewResultImage(lib_result.image));
          },
          std::move(callback), api_, component_, image, prior));
}

void MantisProcessor::ProcessImage(std::unique_ptr<MantisProcess> process) {
  if (!component_.processor) {
    LOG(ERROR) << "Processor is missing";
    std::move(process->callback)
        .Run(MantisResult::NewError(MantisError::kProcessorNotInitialized));
    return;
  }
  ClassifyImageSafetyInternal(
      // Input image checking doesn't require a prompt
      process->image, /*text=*/"",
      base::BindOnce(&MantisProcessor::OnClassifyImageInputDone,
                     weak_ptr_factory_.GetWeakPtr(), std::move(process)));
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
  safety_service_manager_->ClassifyImageSafety(
      cros_safety::mojom::SafetyRuleset::kMantis, text,
      mojo_base::mojom::BigBuffer::NewBytes(image),
      base::BindOnce(
          [](ClassifyImageSafetyCallback callback,
             cros_safety::mojom::SafetyClassifierVerdict result) {
            if (kMapSafetyResult.contains(result)) {
              std::move(callback).Run(kMapSafetyResult.at(result));
            } else {
              std::move(callback).Run(SafetyClassifierVerdict::kFail);
            }
          },
          std::move(callback)));
}

void MantisProcessor::OnClassifyImageInputDone(
    std::unique_ptr<MantisProcess> process, SafetyClassifierVerdict result) {
  if (result != SafetyClassifierVerdict::kPass) {
    std::move(process->callback)
        .Run(MantisResult::NewError(MantisError::kInputSafetyError));
    return;
  }

  mojom::MantisResultPtr lib_result = std::move(process->process_func).Run();
  if (lib_result->is_error()) {
    std::move(process->callback)
        .Run(MantisResult::NewError(lib_result->get_error()));
    return;
  }
  std::vector<uint8_t> image_result = lib_result->get_result_image();

  std::string prompt =
      process->prompt.has_value() ? process->prompt.value() : "";
  ClassifyImageSafetyInternal(
      image_result, prompt,
      base::BindOnce(&MantisProcessor::OnClassifyImageOutputDone,
                     weak_ptr_factory_.GetWeakPtr(), image_result,
                     std::move(process->callback)));
}

void MantisProcessor::OnClassifyImageOutputDone(
    const std::vector<uint8_t>& image,
    base::OnceCallback<void(mojom::MantisResultPtr)> callback,
    SafetyClassifierVerdict result) {
  if (result == SafetyClassifierVerdict::kFailedText) {
    std::move(callback).Run(
        MantisResult::NewError(MantisError::kPromptSafetyError));
    return;
  }
  if (result == SafetyClassifierVerdict::kFailedImage ||
      result == SafetyClassifierVerdict::kFail) {
    std::move(callback).Run(
        MantisResult::NewError(MantisError::kOutputSafetyError));
    return;
  }

  std::move(callback).Run(MantisResult::NewResultImage(image));
}

}  // namespace mantis
