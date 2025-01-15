// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_I18N_ML_SERVICE_LANGUAGE_DETECTOR_H_
#define ODML_I18N_ML_SERVICE_LANGUAGE_DETECTOR_H_

#include <optional>
#include <string>
#include <vector>

#include <base/functional/callback.h>
#include <base/memory/weak_ptr.h>
#include <mojo/public/cpp/bindings/receiver.h>
#include <mojo/public/cpp/bindings/remote.h>

#include "ml/mojom/machine_learning_service.mojom.h"
#include "ml/mojom/text_classifier.mojom.h"
#include "odml/i18n/language_detector.h"

namespace on_device_model {

class MlServiceLanguageDetector : public LanguageDetector {
 public:
  MlServiceLanguageDetector() = default;
  MlServiceLanguageDetector(const MlServiceLanguageDetector&) = delete;
  MlServiceLanguageDetector& operator=(const MlServiceLanguageDetector&) =
      delete;

  // Initialize the language detector with MachineLearningService. IsAvailable
  // will always be false before this is run, but it's not guaranteed that
  // IsAvailable will return true after calling Initialize as well.
  void Initialize(
      chromeos::machine_learning::mojom::MachineLearningService& ml_service);

  // LanguageDetector overrides.
  bool IsAvailable() override;
  void Classify(
      const std::string& text,
      base::OnceCallback<void(std::optional<std::vector<TextLanguage>>)>
          callback) override;

 private:
  void OnLoadTextClassifierResult(
      chromeos::machine_learning::mojom::LoadModelResult result);
  void OnDisconnected();

  void OnFindLanguagesResult(
      base::OnceCallback<void(std::optional<std::vector<TextLanguage>>)>
          callback,
      std::vector<chromeos::machine_learning::mojom::TextLanguagePtr>
          languages);

  bool is_available_ = false;
  mojo::Remote<chromeos::machine_learning::mojom::TextClassifier>
      text_classifier_;

  base::WeakPtrFactory<MlServiceLanguageDetector> weak_ptr_factory_{this};
};

}  // namespace on_device_model

#endif  // ODML_I18N_ML_SERVICE_LANGUAGE_DETECTOR_H_
