// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_ON_DEVICE_MODEL_ML_ON_DEVICE_MODEL_EXECUTOR_H_
#define ODML_ON_DEVICE_MODEL_ML_ON_DEVICE_MODEL_EXECUTOR_H_

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/memory_mapped_file.h>
#include <base/functional/callback.h>
#include <base/memory/raw_ref.h>
#include <base/memory/scoped_refptr.h>
#include <base/native_library.h>
#include <base/threading/sequence_bound.h>
#include <base/types/expected.h>
#include <base/types/pass_key.h>
#include <metrics/metrics_library.h>
#include <mojo/public/cpp/bindings/remote.h>

#include "odml/mojom/on_device_model.mojom.h"
#include "odml/mojom/on_device_model_service.mojom.h"
#include "odml/on_device_model/ml/chrome_ml.h"
#include "odml/on_device_model/ml/session_accessor.h"
#include "odml/on_device_model/ml/ts_model.h"

namespace ml {

class ContextHolder;
class Responder;

class SessionImpl final {
 public:
  SessionImpl(raw_ref<MetricsLibraryInterface> metrics,
              const ChromeML& chrome_ml,
              ChromeMLModel model,
              SessionAccessor::Ptr session,
              SessionAccessor::Ptr empty_session,
              uint32_t max_tokens,
              std::optional<uint32_t> adaptation_id);
  ~SessionImpl();

  SessionImpl(const SessionImpl&) = delete;
  SessionImpl& operator=(const SessionImpl&) = delete;

  void AddContext(
      on_device_model::mojom::InputOptionsPtr input,
      mojo::PendingRemote<on_device_model::mojom::ContextClient> client,
      base::OnceClosure on_complete);
  void Execute(
      on_device_model::mojom::InputOptionsPtr input,
      mojo::PendingRemote<on_device_model::mojom::StreamingResponder> response,
      base::OnceClosure on_complete);
  void SizeInTokens(const std::string& text,
                    base::OnceCallback<void(uint32_t)> callback);
  void Score(const std::string& text, base::OnceCallback<void(float)> callback);
  std::unique_ptr<SessionImpl> Clone();

 private:
  void RemoveContext(ContextHolder* context);

  const raw_ref<MetricsLibraryInterface> metrics_;
  const raw_ref<const ChromeML> chrome_ml_;
  ChromeMLModel model_;
  SessionAccessor::Ptr session_;
  SessionAccessor::Ptr empty_session_;
  const uint32_t max_tokens_;
  std::unique_ptr<Responder> responder_;
  std::set<std::unique_ptr<ContextHolder>> context_holders_;
  std::optional<uint32_t> adaptation_id_;
};

// Uses the ChromeML API to create a model based on the params passed to
// |Create()|. This is the main interface for interacting with the model.
class OnDeviceModelExecutor final {
 public:
  OnDeviceModelExecutor(raw_ref<MetricsLibraryInterface> metrics,
                        base::PassKey<OnDeviceModelExecutor>,
                        const ChromeML& chrome_ml);
  ~OnDeviceModelExecutor();

  static base::expected<std::unique_ptr<OnDeviceModelExecutor>,
                        on_device_model::mojom::LoadModelResult>
  CreateWithResult(raw_ref<MetricsLibraryInterface> metrics,
                   const ChromeML& chrome_ml,
                   on_device_model::mojom::LoadModelParamsPtr params,
                   base::OnceClosure on_complete);

  // on_device_model::OnDeviceModel:
  std::unique_ptr<SessionImpl> CreateSession(
      std::optional<uint32_t> adaptation_id);
  void ClassifyTextSafety(
      const std::string& text,
      on_device_model::mojom::OnDeviceModel::ClassifyTextSafetyCallback
          callback);
  void DetectLanguage(
      const std::string& text,
      on_device_model::mojom::OnDeviceModel::DetectLanguageCallback callback);
  base::expected<uint32_t, on_device_model::mojom::LoadModelResult>
  LoadAdaptation(on_device_model::mojom::LoadAdaptationParamsPtr params,
                 base::OnceClosure on_complete);

 private:
  on_device_model::mojom::LoadModelResult Init(
      on_device_model::mojom::LoadModelParamsPtr params,
      base::OnceClosure on_complete);

  static void Schedule(uintptr_t context, std::function<void()>* fn);

  const raw_ref<MetricsLibraryInterface> metrics_;
  const raw_ref<const ChromeML> chrome_ml_;
  base::SequenceBound<std::unique_ptr<TsModel>> ts_model_;

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
