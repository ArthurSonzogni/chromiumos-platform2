// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/i18n/ml_service_language_detector.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <base/functional/callback.h>
#include <mojo/public/cpp/bindings/callback_helpers.h>
#include <mojo/public/cpp/bindings/receiver.h>
#include <mojo/public/cpp/bindings/remote.h>

#include "ml/mojom/machine_learning_service.mojom.h"
#include "ml/mojom/text_classifier.mojom.h"

namespace on_device_model {

bool MlServiceLanguageDetector::IsAvailable() {
  return is_available_ && text_classifier_.is_bound() &&
         text_classifier_.is_connected();
}

void MlServiceLanguageDetector::Initialize(
    chromeos::machine_learning::mojom::MachineLearningService& ml_service) {
  is_available_ = false;
  text_classifier_.reset();
  ml_service.LoadTextClassifier(
      text_classifier_.BindNewPipeAndPassReceiver(),
      base::BindOnce(&MlServiceLanguageDetector::OnLoadTextClassifierResult,
                     weak_ptr_factory_.GetWeakPtr()));
  text_classifier_.set_disconnect_handler(
      base::BindOnce(&MlServiceLanguageDetector::OnDisconnected,
                     weak_ptr_factory_.GetWeakPtr()));
}

void MlServiceLanguageDetector::Classify(
    const std::string& text,
    base::OnceCallback<void(std::optional<std::vector<TextLanguage>>)>
        callback) {
  if (!IsAvailable()) {
    std::move(callback).Run(std::nullopt);
    return;
  }
  // The callback could still be dropped during the mojo call. In that case,
  // trigger it with nullopt.
  text_classifier_->FindLanguages(
      text, base::BindOnce(&MlServiceLanguageDetector::OnFindLanguagesResult,
                           weak_ptr_factory_.GetWeakPtr(),
                           mojo::WrapCallbackWithDefaultInvokeIfNotRun(
                               std::move(callback), std::nullopt)));
}

void MlServiceLanguageDetector::OnLoadTextClassifierResult(
    chromeos::machine_learning::mojom::LoadModelResult result) {
  if (result != chromeos::machine_learning::mojom::LoadModelResult::OK) {
    LOG(ERROR) << "Load TextClassifier failed with error code: "
               << static_cast<int>(result);
    is_available_ = false;
    text_classifier_.reset();
    return;
  }
  is_available_ = true;
}

void MlServiceLanguageDetector::OnDisconnected() {
  is_available_ = false;
  text_classifier_.reset();
}

void MlServiceLanguageDetector::OnFindLanguagesResult(
    base::OnceCallback<void(std::optional<std::vector<TextLanguage>>)> callback,
    std::vector<chromeos::machine_learning::mojom::TextLanguagePtr> languages) {
  std::vector<TextLanguage> ret;
  for (const chromeos::machine_learning::mojom::TextLanguagePtr& language :
       languages) {
    ret.push_back(TextLanguage{
        .locale = language->locale,
        .confidence = language->confidence,
    });
  }
  std::move(callback).Run(std::move(ret));
}

}  // namespace on_device_model
