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

#include <absl/container/flat_hash_map.h>
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

namespace ml {

class ContextHolder;
class Responder;

class SessionImpl final {
 public:
  SessionImpl(raw_ref<MetricsLibraryInterface> metrics,
              const ChromeML& chrome_ml,
              ChromeMLModel model,
              SessionAccessor::Ptr session,
              uint32_t max_tokens,
              std::optional<uint32_t> adaptation_id);
  ~SessionImpl();

  SessionImpl(const SessionImpl&) = delete;
  SessionImpl& operator=(const SessionImpl&) = delete;

  void Append(on_device_model::mojom::AppendOptionsPtr options,
              mojo::PendingRemote<on_device_model::mojom::ContextClient> client,
              base::OnceClosure on_complete);
  void Generate(
      on_device_model::mojom::GenerateOptionsPtr input,
      mojo::PendingRemote<on_device_model::mojom::StreamingResponder> response,
      base::OnceClosure on_complete);
  void SizeInTokens(on_device_model::mojom::InputPtr input,
                    base::OnceCallback<void(uint32_t)> callback);
  void Score(const std::string& text, base::OnceCallback<void(float)> callback);
  std::unique_ptr<SessionImpl> Clone();

 private:
  void RemoveContext(ContextHolder* context);

  const raw_ref<MetricsLibraryInterface> metrics_;
  const raw_ref<const ChromeML> chrome_ml_;
  ChromeMLModel model_;
  SessionAccessor::Ptr session_;
  const uint32_t max_tokens_;
  std::unique_ptr<Responder> responder_;
  std::set<std::unique_ptr<ContextHolder>> context_holders_;
  std::optional<uint32_t> adaptation_id_;
};

// Uses the ChromeML API to create a model based on the params passed to
// |Create()|. This is the main interface for interacting with the model.
class OnDeviceModelExecutor final {
 public:
  // A handle for an adaptation ID that takes care of erasing the session when
  // it is destroyed.
  class COMPONENT_EXPORT(ON_DEVICE_MODEL_ML) ScopedAdaptation {
   public:
    ScopedAdaptation(base::WeakPtr<OnDeviceModelExecutor> executor,
                     uint32_t adaptation_id);
    ~ScopedAdaptation();

    uint32_t adaptation_id() const { return adaptation_id_; }

   private:
    base::WeakPtr<OnDeviceModelExecutor> executor_;
    uint32_t adaptation_id_;
  };

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

  static on_device_model::Capabilities GetCapabilities(
      const ChromeML& chrome_ml, on_device_model::ModelAssets assets);

  std::unique_ptr<SessionImpl> CreateSession(
      const ScopedAdaptation* adaptation,
      on_device_model::mojom::SessionParamsPtr params);
  std::unique_ptr<ScopedAdaptation> LoadAdaptation(
      on_device_model::mojom::LoadAdaptationParamsPtr params);

 private:
  on_device_model::mojom::LoadModelResult Init(
      on_device_model::mojom::LoadModelParamsPtr params,
      base::OnceClosure on_complete);

  static void Schedule(uintptr_t context, std::function<void()>* fn);

  const raw_ref<MetricsLibraryInterface> metrics_;
  const raw_ref<const ChromeML> chrome_ml_;

  // Params for adaptations that have been loaded.
  absl::flat_hash_map<uint32_t, on_device_model::mojom::LoadAdaptationParamsPtr>
      adaptation_params_;

  ChromeMLModel model_ = 0;
  scoped_refptr<base::SequencedTaskRunner> model_task_runner_;
  uint32_t max_tokens_ = 0;
  uint32_t next_adaptation_id_ = 0;
  base::WeakPtrFactory<OnDeviceModelExecutor> weak_ptr_factory_{this};
};

}  // namespace ml

#endif  // ODML_ON_DEVICE_MODEL_ML_ON_DEVICE_MODEL_EXECUTOR_H_
