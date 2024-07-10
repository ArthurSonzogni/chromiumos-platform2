// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_ON_DEVICE_MODEL_ML_ON_DEVICE_MODEL_EXECUTOR_H_
#define ODML_ON_DEVICE_MODEL_ML_ON_DEVICE_MODEL_EXECUTOR_H_

#include <base/files/file_path.h>
#include <base/files/memory_mapped_file.h>
#include <base/functional/callback.h>
#include <base/memory/raw_ref.h>
#include <base/memory/scoped_refptr.h>
#include <base/native_library.h>
#include <base/types/expected.h>
#include <base/types/pass_key.h>
#include <metrics/metrics_library.h>
#include <mojo/public/cpp/bindings/remote.h>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "odml/mojom/on_device_model.mojom.h"
#include "odml/mojom/on_device_model_service.mojom.h"
#include "odml/on_device_model/ml/chrome_ml.h"
#include "odml/on_device_model/ml/session_accessor.h"
#include "odml/on_device_model/public/cpp/on_device_model.h"

namespace ml {

class LanguageDetector;

// Uses the ChromeML API to create a model based on the params passed to
// |Create()|. This is the main interface for interacting with the model.
class OnDeviceModelExecutor : public on_device_model::OnDeviceModel {
 public:
  OnDeviceModelExecutor(raw_ref<MetricsLibraryInterface> metrics,
                        base::PassKey<OnDeviceModelExecutor>,
                        const ChromeML& chrome_ml);
  ~OnDeviceModelExecutor() override;

  static base::expected<std::unique_ptr<OnDeviceModelExecutor>,
                        on_device_model::mojom::LoadModelResult>
  CreateWithResult(raw_ref<MetricsLibraryInterface> metrics,
                   const ChromeML& chrome_ml,
                   on_device_model::mojom::LoadModelParamsPtr params,
                   base::OnceClosure on_complete);

  // on_device_model::OnDeviceModel:
  std::unique_ptr<Session> CreateSession(
      std::optional<uint32_t> adaptation_id) override;
  on_device_model::mojom::SafetyInfoPtr ClassifyTextSafety(
      const std::string& text) override;
  on_device_model::mojom::LanguageDetectionResultPtr DetectLanguage(
      const std::string& text) override;
  base::expected<uint32_t, on_device_model::mojom::LoadModelResult>
  LoadAdaptation(on_device_model::mojom::LoadAdaptationParamsPtr params,
                 base::OnceClosure on_complete) override;

 private:
  on_device_model::mojom::LoadModelResult Init(
      on_device_model::mojom::LoadModelParamsPtr params,
      base::OnceClosure on_complete);

  static void Schedule(uintptr_t context, std::function<void()>* fn);

  const raw_ref<MetricsLibraryInterface> metrics_;
  const raw_ref<const ChromeML> chrome_ml_;
  base::MemoryMappedFile ts_data_;
  base::MemoryMappedFile ts_sp_model_;
  scoped_refptr<LanguageDetector> language_detector_;

  // TODO(b/323572952): Allow disposing of adaptation weights.
  std::vector<std::unique_ptr<base::MemoryMappedFile>> adaptation_data_;

  // Empty sessions keyed by the adaptation ID that can be cloned from.
  std::map<std::optional<uint32_t>, SessionAccessor::Ptr> base_sessions_;

  ChromeMLModel model_ = 0;
  scoped_refptr<base::SequencedTaskRunner> task_runner_;
  uint32_t max_tokens_ = 0;
  scoped_refptr<base::SequencedTaskRunner> model_task_runner_;
};

}  // namespace ml

#endif  // ODML_ON_DEVICE_MODEL_ML_ON_DEVICE_MODEL_EXECUTOR_H_
