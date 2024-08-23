// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_ON_DEVICE_MODEL_ML_TS_MODEL_H_
#define ODML_ON_DEVICE_MODEL_ML_TS_MODEL_H_

#include <memory>
#include <string>

#include <base/files/memory_mapped_file.h>
#include <base/memory/raw_ref.h>
#include <base/threading/sequence_bound.h>

#include "odml/mojom/on_device_model.mojom.h"
#include "odml/mojom/on_device_model_service.mojom.h"
#include "odml/on_device_model/ml/chrome_ml.h"
#include "odml/on_device_model/ml/chrome_ml_api.h"
#include "odml/on_device_model/ml/stub_language_detector.h"

namespace ml {

class TsModel final {
 public:
  ~TsModel();

  static base::SequenceBound<std::unique_ptr<TsModel>> Create(
      const ChromeML& chrome_ml,
      on_device_model::mojom::ModelAssetsPtr ts_assets,
      base::File language_detection_file);

  on_device_model::mojom::SafetyInfoPtr ClassifyTextSafety(
      const std::string& text);
  on_device_model::mojom::LanguageDetectionResultPtr DetectLanguage(
      std::string_view text);

 private:
  explicit TsModel(
      const ChromeML& chrome_ml,
      std::unique_ptr<translate::LanguageDetectionModel> language_detector);
  void InitTextSafetyModel();

  const raw_ref<const ChromeML> chrome_ml_;
  ChromeMLTSModel model_ = 0;
  std::unique_ptr<translate::LanguageDetectionModel> language_detector_;
  base::MemoryMappedFile data_;
  base::MemoryMappedFile sp_model_;
};

}  // namespace ml

#endif  // ODML_ON_DEVICE_MODEL_ML_TS_MODEL_H_
