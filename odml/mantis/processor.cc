// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/mantis/processor.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <base/barrier_callback.h>
#include <base/check.h>
#include <base/containers/fixed_flat_map.h>
#include <base/functional/bind.h>
#include <base/functional/callback_forward.h>
#include <base/logging.h>
#include <base/memory/raw_ref.h>
#include <base/run_loop.h>
#include <base/task/sequenced_task_runner.h>
#include <metrics/metrics_library.h>
#include <mojo_service_manager/lib/connect.h>
#include <mojo_service_manager/lib/mojom/service_manager.mojom.h>

#include "odml/i18n/language_detector.h"
#include "odml/i18n/translator.h"
#include "odml/mantis/common.h"
#include "odml/mantis/lib_api.h"
#include "odml/mantis/metrics.h"
#include "odml/mantis/prompt_rewriter.h"
#include "odml/mojom/big_buffer.mojom.h"
#include "odml/mojom/cros_safety.mojom.h"
#include "odml/mojom/mantis_processor.mojom.h"
#include "odml/mojom/mantis_service.mojom.h"
#include "odml/periodic_metrics.h"
#include "odml/utils/performance_timer.h"

namespace mantis {

namespace {
using mojom::InitializeResult;
using mojom::MantisError;
using mojom::MantisResult;
using mojom::SafetyClassifierVerdict;
using ::on_device_model::LanguageDetector;

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
        {cros_safety::mojom::SafetyClassifierVerdict::kNoInternetConnection,
         SafetyClassifierVerdict::kNoInternetConnection},
    });

constexpr auto kMapImageTypeToRuleset =
    base::MakeFixedFlatMap<ImageType, cros_safety::mojom::SafetyRuleset>({
        {ImageType::kInputImage,
         cros_safety::mojom::SafetyRuleset::kMantisInputImage},
        {ImageType::kOutputImage,
         cros_safety::mojom::SafetyRuleset::kMantisOutputImage},
        {ImageType::kGeneratedRegion,
         cros_safety::mojom::SafetyRuleset::kMantisGeneratedRegion},
        {ImageType::kGeneratedRegionOutpaintng,
         cros_safety::mojom::SafetyRuleset::kMantisGeneratedRegionOutpainting},
    });

// Selects the prompt's origin language from all possibilities. The result
// can be used to translate the prompt from that language to English. Returns
// `std::nullopt` if no translation is needed (e.g. it's English or language is
// undetected)
std::optional<std::string> SelectLanguage(
    const std::optional<std::vector<LanguageDetector::TextLanguage>>&
        possible_languages) {
  if (!possible_languages.has_value()) {
    return std::nullopt;
  }

  const size_t kTopLanguageToCheck =
      std::min(possible_languages->size(), static_cast<size_t>(3));
  for (int i = 0; i < kTopLanguageToCheck; ++i) {
    const LanguageDetector::TextLanguage language = (*possible_languages)[i];
    if (language.locale == kEnglishLocale) {
      return std::nullopt;
    }
  }

  for (int i = 0; i < kTopLanguageToCheck; ++i) {
    const LanguageDetector::TextLanguage language = (*possible_languages)[i];
    if (IsLanguageSupported(language.locale)) {
      return language.locale;
    }
  }

  return std::nullopt;
}

}  // namespace

MantisProcessor::MantisProcessor(
    raw_ref<MetricsLibraryInterface> metrics_lib,
    raw_ref<odml::PeriodicMetrics> periodic_metrics,
    scoped_refptr<base::SequencedTaskRunner> mantis_api_runner,
    MantisComponent component,
    const MantisAPI* api,
    mojo::PendingReceiver<mojom::MantisProcessor> receiver,
    raw_ref<cros_safety::SafetyServiceManager> safety_service_manager,
    raw_ref<on_device_model::LanguageDetector> language_detector,
    raw_ref<i18n::Translator> translator,
    base::OnceCallback<void()> on_disconnected,
    base::OnceCallback<void(InitializeResult)> callback)
    : metrics_lib_(metrics_lib),
      periodic_metrics_(periodic_metrics),
      mantis_api_runner_(std::move(mantis_api_runner)),
      component_(component),
      api_(api),
      safety_service_manager_(safety_service_manager),
      language_detector_(language_detector),
      translator_(translator),
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
  mantis_api_runner_->PostTask(
      FROM_HERE, base::BindOnce(
                     [](const MantisAPI* api, MantisComponent component) {
                       // Safe to perform even after `this` is destroyed.
                       api->DestroyMantisComponent(component);
                     },
                     api_, component_));
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
      .operation_type = OperationType::kInpainting,
      .callback = std::move(callback),
      .process_func = base::BindOnce(
          [](const MantisAPI* api, MantisComponent component,
             MantisProcess* process) -> ProcessFuncResult {
            InpaintingResult lib_result =
                api->Inpainting(component.processor, process->image,
                                process->mask, process->seed);
            if (lib_result.status != MantisStatus::kOk) {
              return ProcessFuncResult{
                  .error = kMapStatusToError.at(lib_result.status),
              };
            }

            return ProcessFuncResult{
                .image = lib_result.image,
                .generated_region = lib_result.generated_region,
            };
          },
          api_, component_),
      .time_metric = TimeMetric::kInpaintingLatency,
      .generated_image_type_metric = ImageGenerationType::kInpainting,
      .timer = odml::PerformanceTimer::Create(),
  }));
}

void MantisProcessor::Outpainting(const std::vector<uint8_t>& image,
                                  const std::vector<uint8_t>& mask,
                                  uint32_t seed,
                                  OutpaintingCallback callback) {
  ProcessImage(std::make_unique<MantisProcess>(MantisProcess{
      .image = image,
      .mask = mask,
      .seed = seed,
      .prompt = "",
      .operation_type = OperationType::kOutpainting,
      .callback = std::move(callback),
      .process_func = base::BindOnce(
          [](const MantisAPI* api, MantisComponent component,
             MantisProcess* process) -> ProcessFuncResult {
            OutpaintingResult lib_result =
                api->Outpainting(component.processor, process->image,
                                 process->mask, process->seed);
            if (lib_result.status != MantisStatus::kOk) {
              return ProcessFuncResult{
                  .error = kMapStatusToError.at(lib_result.status),
              };
            }
            return ProcessFuncResult{
                .image = lib_result.image,
                .generated_region = lib_result.generated_region,
            };
          },
          api_, component_),
      .time_metric = TimeMetric::kOutpaintingLatency,
      .generated_image_type_metric = ImageGenerationType::KOutpainting,
      .timer = odml::PerformanceTimer::Create(),
  }));
}

void MantisProcessor::OnLanguageDetectionResult(
    std::unique_ptr<MantisProcess> process,
    std::optional<std::vector<LanguageDetector::TextLanguage>> results) {
  std::optional<std::string> language = SelectLanguage(results);
  if (!language.has_value()) {
    // Use the original prompt.
    ProcessImage(std::move(process));
    return;
  }
  LOG(INFO) << "Prompt is in language " << *language;
  translator_->Translate(
      i18n::LangPair{.source = *language, .target = kEnglishLocale},
      *process->prompt,
      base::BindOnce(&MantisProcessor::OnTranslateResult,
                     weak_ptr_factory_.GetWeakPtr(), std::move(process)));
}

void MantisProcessor::OnTranslateResult(std::unique_ptr<MantisProcess> process,
                                        std::optional<std::string> result) {
  if (result.has_value()) {
    process->prompt = *result;
  }
  ProcessImage(std::move(process));
}

void MantisProcessor::GenerativeFill(const std::vector<uint8_t>& image,
                                     const std::vector<uint8_t>& mask,
                                     uint32_t seed,
                                     const std::string& prompt,
                                     GenerativeFillCallback callback) {
  auto process = std::make_unique<MantisProcess>(MantisProcess{
      .image = image,
      .mask = mask,
      .seed = seed,
      .prompt = prompt,
      .operation_type = OperationType::kGenfill,
      .callback = std::move(callback),
      .process_func = base::BindOnce(
          [](const MantisAPI* api, MantisComponent component,
             MantisProcess* process) -> ProcessFuncResult {
            GenerativeFillResult lib_result = api->GenerativeFill(
                component.processor, process->image, process->mask,
                process->seed, *process->prompt);
            if (lib_result.status != MantisStatus::kOk) {
              return ProcessFuncResult{
                  .error = kMapStatusToError.at(lib_result.status),
              };
            }

            return ProcessFuncResult{
                .image = lib_result.image,
                .generated_region = lib_result.generated_region,
            };
          },
          api_, component_),
      .time_metric = TimeMetric::kGenerativeFillLatency,
      .generated_image_type_metric = ImageGenerationType::kGenerativeFill,
      .timer = odml::PerformanceTimer::Create(),
  });
  if (process->prompt->empty()) {
    // No need to go through detection-translation flow.
    return ProcessImage(std::move(process));
  }
  language_detector_->Classify(
      prompt,
      base::BindOnce(&MantisProcessor::OnLanguageDetectionResult,
                     weak_ptr_factory_.GetWeakPtr(), std::move(process)));
}

void MantisProcessor::Segmentation(const std::vector<uint8_t>& image,
                                   const std::vector<uint8_t>& prior,
                                   SegmentationCallback callback) {
  if (!component_.segmenter) {
    std::move(callback).Run(
        MantisResult::NewError(MantisError::kMissingSegmenter));
    return;
  }

  mantis_api_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](const MantisAPI* api, MantisComponent component,
             const std::vector<uint8_t>& image,
             const std::vector<uint8_t>& prior) {
            return api->Segmentation(component.segmenter, image, prior);
          },
          api_, component_, image, prior),
      base::BindOnce(&MantisProcessor::OnSegmentationDone,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback),
                     odml::PerformanceTimer::Create()));
}

void MantisProcessor::OnSegmentationDone(SegmentationCallback callback,
                                         odml::PerformanceTimer::Ptr timer,
                                         const SegmentationResult& lib_result) {
  if (lib_result.status != MantisStatus::kOk) {
    std::move(callback).Run(
        MantisResult::NewError(kMapStatusToError.at(lib_result.status)));
    return;
  }
  SendTimeMetric(*metrics_lib_, TimeMetric::kSegmentationLatency, *timer);

  std::move(callback).Run(MantisResult::NewResultImage(lib_result.image));
}

void MantisProcessor::ProcessImage(std::unique_ptr<MantisProcess> process) {
  if (!component_.processor) {
    LOG(ERROR) << "Processor is missing";
    std::move(process->callback)
        .Run(MantisResult::NewError(MantisError::kProcessorNotInitialized));
    return;
  }

  if (process->operation_type == OperationType::kGenfill &&
      process->prompt.has_value() && !process->prompt->empty()) {
    // Rewrite prompt regardless if the prompt comes from translation or not.
    process->prompt = RewritePromptForGenerativeFill(*process->prompt);
    // If the prompt becomes empty, do Inpainting. Note that we will not reach
    // this point if the original prompt is empty, so it's expected in such case
    // to do Generative Fill. See b/406208444#comment2 for details.
    if (process->prompt->empty()) {
      return Inpainting(process->image, process->mask, process->seed,
                        std::move(process->callback));
    }
  }

  mantis_api_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(std::move(process->process_func), process.get()),
      base::BindOnce(&MantisProcessor::OnProcessDone,
                     weak_ptr_factory_.GetWeakPtr(), std::move(process)));
}

void MantisProcessor::OnProcessDone(std::unique_ptr<MantisProcess> process,
                                    const ProcessFuncResult& lib_result) {
  // Record the usage after process.
  periodic_metrics_->UpdateAndRecordMetricsNow();

  if (lib_result.error.has_value()) {
    std::move(process->callback)
        .Run(MantisResult::NewError(lib_result.error.value()));
    return;
  }
  SendTimeMetric(*metrics_lib_, process->time_metric, *process->timer);
  SendImageGenerationTypeMetric(*metrics_lib_,
                                process->generated_image_type_metric);
  std::string prompt =
      process->prompt.has_value() ? process->prompt.value() : "";
  process->image_result = lib_result.image;
  process->generated_region = lib_result.generated_region;
  const auto generated_region_image_type =
      process->operation_type == OperationType::kOutpainting
          ? ImageType::kGeneratedRegionOutpaintng
          : ImageType::kGeneratedRegion;

  const auto barrier_callback = base::BarrierCallback<SafetyClassifierVerdict>(
      2, base::BindOnce(&MantisProcessor::OnClassifyImageOutputDone,
                        weak_ptr_factory_.GetWeakPtr(), std::move(process)));

  ClassifyImageSafetyInternal(lib_result.image, prompt, ImageType::kOutputImage,
                              barrier_callback);
  ClassifyImageSafetyInternal(lib_result.generated_region, /*text=*/"",
                              generated_region_image_type, barrier_callback);
}

void MantisProcessor::ClassifyImageSafety(
    const std::vector<uint8_t>& image, ClassifyImageSafetyCallback callback) {
  ClassifyImageSafetyInternal(image, "", ImageType::kInputImage,
                              std::move(callback));
}

// Verifies that the input image complies with Google's T&S policy. The text
// input is optional and is typically used when the input image is AI-generated
// based on a specific prompt.
void MantisProcessor::ClassifyImageSafetyInternal(
    const std::vector<uint8_t>& image,
    const std::string& text,
    ImageType image_type,
    base::OnceCallback<void(SafetyClassifierVerdict)> callback) {
  auto ruleset = cros_safety::mojom::SafetyRuleset::kMantis;
  if (kMapImageTypeToRuleset.contains(image_type)) {
    ruleset = kMapImageTypeToRuleset.at(image_type);
  }
  safety_service_manager_->ClassifyImageSafety(
      ruleset, text, mojo_base::mojom::BigBuffer::NewBytes(image),
      base::BindOnce(
          [](raw_ref<MetricsLibraryInterface> metrics_lib,
             odml::PerformanceTimer::Ptr timer,
             ClassifyImageSafetyCallback callback,
             cros_safety::mojom::SafetyClassifierVerdict result) {
            // Send metric even if fail, since we need to measure the network
            // latency.
            SendTimeMetric(*metrics_lib,
                           TimeMetric::kClassifyImageSafetyLatency, *timer);
            if (kMapSafetyResult.contains(result)) {
              std::move(callback).Run(kMapSafetyResult.at(result));
            } else {
              std::move(callback).Run(SafetyClassifierVerdict::kFail);
            }
          },
          metrics_lib_, odml::PerformanceTimer::Create(), std::move(callback)));
}

void MantisProcessor::OnClassifyImageOutputDone(
    std::unique_ptr<MantisProcess> process,
    std::vector<SafetyClassifierVerdict> results) {
  for (auto& result : results) {
    if (result == SafetyClassifierVerdict::kFailedText) {
      std::move(process->callback)
          .Run(MantisResult::NewError(MantisError::kPromptSafetyError));
      return;
    }
    if (result == SafetyClassifierVerdict::kFailedImage ||
        result == SafetyClassifierVerdict::kFail) {
      std::move(process->callback)
          .Run(MantisResult::NewError(MantisError::kOutputSafetyError));
      return;
    }
  }

  std::move(process->callback)
      .Run(MantisResult::NewResultImage(process->image_result));
}

}  // namespace mantis
