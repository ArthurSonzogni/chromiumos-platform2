// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/on_device_model/on_device_model_service.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

#include <base/functional/callback.h>
#include <base/memory/raw_ref.h>
#include <base/memory/weak_ptr.h>
#include <base/task/bind_post_task.h>
#include <base/timer/elapsed_timer.h>
#include <metrics/metrics_library.h>
#include <mojo/public/cpp/bindings/receiver_set.h>

#include "odml/on_device_model/features.h"
#include "odml/on_device_model/ml/on_device_model_internal.h"
#include "odml/on_device_model/platform_model_loader.h"
#include "odml/on_device_model/platform_model_loader_chromeos.h"

namespace on_device_model {
namespace {

// For medium timings up to 3 minutes (50 buckets).
void ReportHistogramMediumTimes(raw_ref<MetricsLibraryInterface> metrics,
                                std::string_view name,
                                base::TimeDelta sample) {
  metrics->SendTimeToUMA(name, sample, base::Milliseconds(1), base::Minutes(3),
                         50);
}

class ModelWrapper;

class SessionWrapper final : public mojom::Session {
 public:
  SessionWrapper(base::WeakPtr<ModelWrapper> model,
                 mojo::PendingReceiver<mojom::Session> receiver,
                 std::unique_ptr<ml::SessionImpl> session)
      : model_(model),
        receiver_(this, std::move(receiver)),
        session_(std::move(session)) {}
  ~SessionWrapper() override = default;

  SessionWrapper(const SessionWrapper&) = delete;
  SessionWrapper& operator=(const SessionWrapper&) = delete;

  void AddContext(mojom::InputOptionsPtr input,
                  mojo::PendingRemote<mojom::ContextClient> client) override;
  void Execute(
      mojom::InputOptionsPtr input,
      mojo::PendingRemote<mojom::StreamingResponder> response) override;
  void GetSizeInTokens(const std::string& text,
                       GetSizeInTokensCallback callback) override;
  void Score(const std::string& text, ScoreCallback callback) override;
  void Clone(mojo::PendingReceiver<mojom::Session> session) override;

  mojo::Receiver<mojom::Session>& receiver() { return receiver_; }

  void AddPreviousContext(mojom::InputOptionsPtr input) {
    previous_contexts_.push_back(std::move(input));
  }

 private:
  void AddContextInternal(mojom::InputOptionsPtr input,
                          mojo::PendingRemote<mojom::ContextClient> client,
                          base::OnceClosure on_complete) {
    session_->AddContext(std::move(input), std::move(client),
                         std::move(on_complete));
  }

  void ExecuteInternal(mojom::InputOptionsPtr input,
                       mojo::PendingRemote<mojom::StreamingResponder> response,
                       base::OnceClosure on_complete) {
    session_->Execute(std::move(input), std::move(response),
                      std::move(on_complete));
  }

  void GetSizeInTokensInternal(const std::string& text,
                               GetSizeInTokensCallback callback,
                               base::OnceClosure on_complete) {
    session_->SizeInTokens(text,
                           std::move(callback).Then(std::move(on_complete)));
  }

  void ScoreInternal(const std::string& text,
                     ScoreCallback callback,
                     base::OnceClosure on_complete) {
    session_->Score(text, std::move(callback).Then(std::move(on_complete)));
  }

  void CloneInternal(mojo::PendingReceiver<mojom::Session> session);

  base::WeakPtr<ModelWrapper> model_;
  mojo::Receiver<mojom::Session> receiver_;
  std::unique_ptr<ml::SessionImpl> session_;
  std::vector<mojom::InputOptionsPtr> previous_contexts_;
  base::WeakPtrFactory<SessionWrapper> weak_ptr_factory_{this};
};

class ModelWrapper final : public mojom::OnDeviceModel {
 public:
  explicit ModelWrapper(
      raw_ref<MetricsLibraryInterface> metrics,
      bool support_multiple_sessions,
      std::unique_ptr<ml::OnDeviceModelExecutor> model,
      mojo::PendingReceiver<mojom::OnDeviceModel> receiver,
      base::OnceCallback<void(base::WeakPtr<mojom::OnDeviceModel>)> on_delete)
      : metrics_(metrics),
        support_multiple_sessions_(support_multiple_sessions),
        model_(std::move(model)),
        on_delete_(std::move(on_delete)) {
    receivers_.Add(this, std::move(receiver), std::nullopt);
    receivers_.set_disconnect_handler(base::BindRepeating(
        &ModelWrapper::ModelDisconnected, weak_ptr_factory_.GetWeakPtr()));
  }
  ~ModelWrapper() override = default;

  ModelWrapper(const ModelWrapper&) = delete;
  ModelWrapper& operator=(const ModelWrapper&) = delete;

  bool support_multiple_sessions() const { return support_multiple_sessions_; }

  void AddAndRunPendingTask(
      base::OnceCallback<void(base::OnceClosure finish_callback)> task,
      base::WeakPtr<SessionWrapper> session = nullptr) {
    if (support_multiple_sessions_) {
      base::ScopedClosureRunner task_finished(base::BindOnce(
          &ModelWrapper::TaskFinished, weak_ptr_factory_.GetWeakPtr()));
      pending_tasks_.push(PendingTask{
          .session = session,
          .task = base::BindOnce(
              std::move(task), base::BindOnce([](base::ScopedClosureRunner) {},
                                              std::move(task_finished))),
      });
      RunTaskIfPossible();
      return;
    }

    std::move(task).Run(base::DoNothing());
  }

  void StartSession(mojo::PendingReceiver<mojom::Session> session) override {
    AddSession(std::move(session),
               model_->CreateSession(receivers_.current_context()), {});
  }

  void ClassifyTextSafety(const std::string& text,
                          ClassifyTextSafetyCallback callback) override {
    model_->ClassifyTextSafety(text, std::move(callback));
  }

  void DetectLanguage(const std::string& text,
                      DetectLanguageCallback callback) override {
    model_->DetectLanguage(text, std::move(callback));
  }

  void LoadAdaptation(mojom::LoadAdaptationParamsPtr params,
                      mojo::PendingReceiver<mojom::OnDeviceModel> model,
                      LoadAdaptationCallback callback) override {
    if (!support_multiple_sessions_) {
      sessions_.clear();
    }

    auto load_adaptation = base::BindOnce(
        &ModelWrapper::LoadAdaptationInternal, weak_ptr_factory_.GetWeakPtr(),
        std::move(params), std::move(model), std::move(callback));
    AddAndRunPendingTask(
        base::IgnoreArgs<base::OnceClosure>(std::move(load_adaptation)));
  }

  void AddSession(
      mojo::PendingReceiver<mojom::Session> receiver,
      std::unique_ptr<ml::SessionImpl> session,
      const std::vector<mojom::InputOptionsPtr>& previous_contexts) {
    auto current_session = std::make_unique<SessionWrapper>(
        weak_ptr_factory_.GetWeakPtr(), std::move(receiver),
        std::move(session));
    for (const auto& context : previous_contexts) {
      current_session->AddPreviousContext(context.Clone());
    }
    SessionWrapper* current_session_ptr = current_session.get();

    if (!support_multiple_sessions_) {
      sessions_.clear();
    }

    sessions_.insert(std::move(current_session));
    current_session_ptr->receiver().set_disconnect_handler(
        base::BindOnce(&ModelWrapper::SessionDisconnected,
                       base::Unretained(this), current_session_ptr));
  }

 private:
  struct PendingTask {
    base::WeakPtr<SessionWrapper> session;
    base::OnceClosure task;
  };

  void SessionDisconnected(SessionWrapper* ptr) {
    auto it = sessions_.find(ptr);
    if (it != sessions_.end()) {
      sessions_.erase(it);
    }
  }

  void ModelDisconnected() {
    if (receivers_.empty()) {
      std::move(on_delete_).Run(weak_ptr_factory_.GetWeakPtr());
    }
  }

  void LoadAdaptationInternal(mojom::LoadAdaptationParamsPtr params,
                              mojo::PendingReceiver<mojom::OnDeviceModel> model,
                              LoadAdaptationCallback callback) {
    auto start = base::TimeTicks::Now();
    auto result = model_->LoadAdaptation(
        std::move(params),
        base::BindPostTask(
            base::SequencedTaskRunner::GetCurrentDefault(),
            base::BindOnce(
                [](base::WeakPtr<ModelWrapper> self, base::TimeTicks start) {
                  if (!self) {
                    return;
                  }
                  ReportHistogramMediumTimes(
                      self->metrics_,
                      "OnDeviceModel.LoadAdaptationModelDuration",
                      base::TimeTicks::Now() - start);
                },
                weak_ptr_factory_.GetWeakPtr(), start)));
    if (!result.has_value()) {
      std::move(callback).Run(result.error());
      return;
    }
    receivers_.Add(this, std::move(model), *result);
    std::move(callback).Run(mojom::LoadModelResult::kSuccess);
  }

  void RunTaskIfPossible() {
    if (!support_multiple_sessions_) {
      return;
    }

    if (is_running_) {
      return;
    }

    if (pending_tasks_.empty()) {
      return;
    }

    PendingTask pending_task = std::move(pending_tasks_.front());
    pending_tasks_.pop();

    is_running_ = true;
    running_session_ = pending_task.session;

    std::move(pending_task.task).Run();
  }

  void TaskFinished() {
    last_session_ = running_session_;
    is_running_ = false;
    RunTaskIfPossible();
  }

  const raw_ref<MetricsLibraryInterface> metrics_;
  bool support_multiple_sessions_;
  std::set<std::unique_ptr<SessionWrapper>, base::UniquePtrComparator>
      sessions_;
  std::unique_ptr<ml::OnDeviceModelExecutor> model_;
  mojo::ReceiverSet<mojom::OnDeviceModel, std::optional<uint32_t>> receivers_;
  base::OnceCallback<void(base::WeakPtr<mojom::OnDeviceModel>)> on_delete_;
  std::queue<PendingTask> pending_tasks_;
  bool is_running_ = false;
  base::WeakPtr<SessionWrapper> running_session_;
  // Last session a task was executed in.
  base::WeakPtr<SessionWrapper> last_session_;
  base::WeakPtrFactory<ModelWrapper> weak_ptr_factory_{this};
};

void SessionWrapper::AddContext(
    mojom::InputOptionsPtr input,
    mojo::PendingRemote<mojom::ContextClient> client) {
  if (!model_) {
    return;
  }

  base::OnceClosure save_context = base::DoNothing();
  if (model_->support_multiple_sessions()) {
    save_context =
        base::BindOnce(&SessionWrapper::AddPreviousContext,
                       weak_ptr_factory_.GetWeakPtr(), input.Clone());
  }

  auto add_context_internal = base::BindOnce(
      &SessionWrapper::AddContextInternal, weak_ptr_factory_.GetWeakPtr(),
      std::move(input), std::move(client));

  auto add_context = base::BindOnce(
      [](decltype(add_context_internal) add_context_internal,
         base::OnceClosure save_context, base::OnceClosure finish_callback) {
        std::move(add_context_internal)
            .Run(std::move(save_context).Then(std::move(finish_callback)));
      },
      std::move(add_context_internal), std::move(save_context));

  model_->AddAndRunPendingTask(std::move(add_context),
                               weak_ptr_factory_.GetWeakPtr());
}

void SessionWrapper::Execute(
    mojom::InputOptionsPtr input,
    mojo::PendingRemote<mojom::StreamingResponder> response) {
  if (!model_) {
    return;
  }

  auto execute_internal = base::BindOnce(&SessionWrapper::ExecuteInternal,
                                         weak_ptr_factory_.GetWeakPtr(),
                                         std::move(input), std::move(response));

  model_->AddAndRunPendingTask(std::move(execute_internal),
                               weak_ptr_factory_.GetWeakPtr());
}

void SessionWrapper::GetSizeInTokens(const std::string& text,
                                     GetSizeInTokensCallback callback) {
  if (!model_) {
    return;
  }

  auto size_in_tokens_internal =
      base::BindOnce(&SessionWrapper::GetSizeInTokensInternal,
                     weak_ptr_factory_.GetWeakPtr(), text, std::move(callback));

  model_->AddAndRunPendingTask(std::move(size_in_tokens_internal),
                               weak_ptr_factory_.GetWeakPtr());
}

void SessionWrapper::Score(const std::string& text, ScoreCallback callback) {
  if (!model_) {
    return;
  }

  model_->AddAndRunPendingTask(
      base::BindOnce(&SessionWrapper::ScoreInternal,
                     weak_ptr_factory_.GetWeakPtr(), text, std::move(callback)),
      weak_ptr_factory_.GetWeakPtr());
}

void SessionWrapper::Clone(mojo::PendingReceiver<mojom::Session> session) {
  if (!model_) {
    return;
  }

  model_->AddAndRunPendingTask(
      base::IgnoreArgs<base::OnceClosure>(
          base::BindOnce(&SessionWrapper::CloneInternal,
                         weak_ptr_factory_.GetWeakPtr(), std::move(session))),
      weak_ptr_factory_.GetWeakPtr());
}

void SessionWrapper::CloneInternal(
    mojo::PendingReceiver<mojom::Session> session) {
  if (!model_) {
    return;
  }

  model_->AddSession(std::move(session), session_->Clone(), previous_contexts_);
}

}  // namespace

OnDeviceModelService::OnDeviceModelService(
    raw_ref<MetricsLibraryInterface> metrics,
    raw_ref<odml::OdmlShimLoader> shim_loader,
    std::unique_ptr<const ml::OnDeviceModelInternalImpl> impl)
    : metrics_(metrics),
      shim_loader_(shim_loader),
      impl_(std::move(impl)),
      platform_model_loader_(std::make_unique<ChromeosPlatformModelLoader>(
          metrics_, raw_ref(*this))) {}

OnDeviceModelService::~OnDeviceModelService() = default;

void OnDeviceModelService::LoadModel(
    mojom::LoadModelParamsPtr params,
    mojo::PendingReceiver<mojom::OnDeviceModel> model,
    LoadPlatformModelCallback callback) {
  auto start = base::TimeTicks::Now();
  bool support_multiple_sessions = params->support_multiple_sessions;
  auto model_impl = impl_->CreateModel(
      std::move(params),
      base::BindPostTask(base::SequencedTaskRunner::GetCurrentDefault(),
                         base::BindOnce(
                             [](base::WeakPtr<OnDeviceModelService> self,
                                base::TimeTicks start) {
                               if (!self) {
                                 return;
                               }
                               ReportHistogramMediumTimes(
                                   self->metrics_,
                                   "OnDeviceModel.LoadModelDuration",
                                   base::TimeTicks::Now() - start);
                             },
                             weak_ptr_factory_.GetWeakPtr(), start)));
  if (!model_impl.has_value()) {
    std::move(callback).Run(model_impl.error());
    return;
  }

  models_.insert(std::make_unique<ModelWrapper>(
      metrics_, support_multiple_sessions, std::move(model_impl.value()),
      std::move(model),
      base::BindOnce(&OnDeviceModelService::DeleteModel,
                     base::Unretained(this))));
  std::move(callback).Run(mojom::LoadModelResult::kSuccess);
}

// If the shim is not ready, this function will retry the function with the
// given arguments after the shim is ready, and the ownership of callback &
// args will be taken in this kind of case.
// Returns true if the function will be retried.
template <typename FuncType,
          typename CallbackType,
          typename FailureType,
          typename... Args>
bool OnDeviceModelService::RetryIfShimIsNotReady(FuncType func,
                                                 CallbackType& callback,
                                                 FailureType failure_result,
                                                 Args&... args) {
  if (shim_loader_->IsShimReady()) {
    return false;
  }

  auto split = base::SplitOnceCallback(std::move(callback));
  base::OnceClosure retry_cb =
      base::BindOnce(func, weak_ptr_factory_.GetWeakPtr(), std::move(args)...,
                     std::move(split.first));

  shim_loader_->EnsureShimReady(base::BindOnce(
      [](CallbackType callback, base::OnceClosure retry_cb,
         FailureType failure_result, bool result) {
        if (!result) {
          LOG(ERROR) << "Failed to ensure the shim is ready.";
          std::move(callback).Run(std::move(failure_result));
          return;
        }
        std::move(retry_cb).Run();
      },
      std::move(split.second), std::move(retry_cb), std::move(failure_result)));

  return true;
}

void OnDeviceModelService::LoadPlatformModel(
    const base::Uuid& uuid,
    mojo::PendingReceiver<mojom::OnDeviceModel> model,
    mojo::PendingRemote<mojom::PlatformModelProgressObserver> progress_observer,
    LoadPlatformModelCallback callback) {
  if (RetryIfShimIsNotReady(&OnDeviceModelService::LoadPlatformModel, callback,
                            mojom::LoadModelResult::kFailedToLoadLibrary, uuid,
                            model, progress_observer)) {
    return;
  }

  platform_model_loader_->LoadModelWithUuid(uuid, std::move(model),
                                            std::move(progress_observer),
                                            std::move(callback));
}

void OnDeviceModelService::GetPlatformModelState(
    const base::Uuid& uuid, GetPlatformModelStateCallback callback) {
  if (RetryIfShimIsNotReady(&OnDeviceModelService::GetPlatformModelState,
                            callback, mojom::PlatformModelState::kUnknownState,
                            uuid)) {
    return;
  }

  platform_model_loader_->GetModelState(uuid, std::move(callback));
}

void OnDeviceModelService::GetEstimatedPerformanceClass(
    GetEstimatedPerformanceClassCallback callback) {
  if (RetryIfShimIsNotReady(&OnDeviceModelService::GetEstimatedPerformanceClass,
                            callback,
                            mojom::PerformanceClass::kFailedToLoadLibrary)) {
    return;
  }

  std::move(callback).Run(impl_->GetEstimatedPerformanceClass());
}

void OnDeviceModelService::FormatInput(
    const base::Uuid& uuid,
    mojom::FormatFeature feature,
    const base::flat_map<std::string, std::string>& fields,
    FormatInputCallback callback) {
  if (RetryIfShimIsNotReady(&OnDeviceModelService::FormatInput, callback,
                            std::nullopt, uuid, feature, fields)) {
    return;
  }

  auto format_input = shim_loader_->Get<FormatInputSignature>(kFormatInputName);
  if (!format_input) {
    LOG(ERROR) << "Unable to resolve FormatInput() symbol.";
    std::move(callback).Run(std::nullopt);
    return;
  }

  std::unordered_map<std::string, std::string> new_fields;
  std::copy(fields.begin(), fields.end(),
            std::inserter(new_fields, new_fields.end()));

  std::optional<std::string> result = format_input(
      uuid.AsLowercaseString(), static_cast<Feature>(feature), new_fields);

  std::move(callback).Run(result);
}

void OnDeviceModelService::ValidateSafetyResult(
    mojom::SafetyFeature safety_feature,
    const std::string& text,
    mojom::SafetyInfoPtr safety_info,
    ValidateSafetyResultCallback callback) {
  if (RetryIfShimIsNotReady(&OnDeviceModelService::ValidateSafetyResult,
                            callback, false, safety_feature, text,
                            safety_info)) {
    return;
  }

  auto validate_safety_result =
      shim_loader_->Get<ValidateSafetyResultSignature>(
          kValidateSafetyResultName);
  if (!validate_safety_result) {
    LOG(ERROR) << "Unable to resolve ValidateSafetyResult() symbol.";
    std::move(callback).Run(false);
    return;
  }

  bool result =
      validate_safety_result(static_cast<SafetyFeature>(safety_feature), text,
                             safety_info->class_scores);

  std::move(callback).Run(result);
}

void OnDeviceModelService::DeleteModel(
    base::WeakPtr<mojom::OnDeviceModel> model) {
  if (!model) {
    return;
  }
  auto it = models_.find(model.get());
  DCHECK(it != models_.end());
  models_.erase(it);
}

}  // namespace on_device_model
