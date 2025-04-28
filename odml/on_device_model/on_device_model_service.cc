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
#include <base/notreached.h>
#include <base/task/bind_post_task.h>
#include <base/task/task_traits.h>
#include <base/task/thread_pool.h>
#include <base/timer/elapsed_timer.h>
#include <metrics/metrics_library.h>
#include <mojo/public/cpp/bindings/receiver_set.h>

#include "odml/on_device_model/features.h"
#include "odml/on_device_model/ml/on_device_model_executor.h"
#include "odml/on_device_model/ml/performance_class.h"
#include "odml/on_device_model/ml/ts_model.h"
#include "odml/on_device_model/platform_model_loader.h"
#include "odml/on_device_model/platform_model_loader_chromeos.h"
#include "odml/periodic_metrics.h"

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

  void Append(mojom::AppendOptionsPtr options,
              mojo::PendingRemote<mojom::ContextClient> client) override;
  void Generate(
      mojom::GenerateOptionsPtr options,
      mojo::PendingRemote<mojom::StreamingResponder> response) override;
  void GetSizeInTokens(mojom::InputPtr input,
                       GetSizeInTokensCallback callback) override;
  void Score(const std::string& text, ScoreCallback callback) override;
  void Clone(mojo::PendingReceiver<mojom::Session> session) override;

  mojo::Receiver<mojom::Session>& receiver() { return receiver_; }

 private:
  void AppendInternal(mojom::AppendOptionsPtr options,
                      mojo::PendingRemote<mojom::ContextClient> client,
                      base::OnceClosure on_complete) {
    session_->Append(std::move(options), std::move(client),
                     std::move(on_complete));
  }

  void GenerateInternal(mojom::GenerateOptionsPtr input,
                        mojo::PendingRemote<mojom::StreamingResponder> response,
                        base::OnceClosure on_complete) {
    session_->Generate(std::move(input), std::move(response),
                       std::move(on_complete));
  }

  void GetSizeInTokensInternal(mojom::InputPtr input,
                               GetSizeInTokensCallback callback,
                               base::OnceClosure on_complete) {
    session_->SizeInTokens(std::move(input),
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
  base::WeakPtrFactory<SessionWrapper> weak_ptr_factory_{this};
};

class ModelWrapper final : public mojom::OnDeviceModel {
 public:
  explicit ModelWrapper(
      raw_ref<MetricsLibraryInterface> metrics,
      std::unique_ptr<ml::OnDeviceModelExecutor> model,
      mojo::PendingReceiver<mojom::OnDeviceModel> receiver,
      base::OnceCallback<void(base::WeakPtr<mojom::OnDeviceModel>)> on_delete,
      mojom::TextSafetyModel* ts_model)
      : metrics_(metrics),
        model_(std::move(model)),
        on_delete_(std::move(on_delete)),
        ts_model_(ts_model) {
    if (ts_model_) {
      ts_model_->StartSession(ts_session_.BindNewPipeAndPassReceiver());
    }
    receivers_.Add(
        this, std::move(receiver),
        std::unique_ptr<ml::OnDeviceModelExecutor::ScopedAdaptation>());
    receivers_.set_disconnect_handler(base::BindRepeating(
        &ModelWrapper::ModelDisconnected, weak_ptr_factory_.GetWeakPtr()));
  }
  ~ModelWrapper() override = default;

  ModelWrapper(const ModelWrapper&) = delete;
  ModelWrapper& operator=(const ModelWrapper&) = delete;

  void AddAndRunPendingTask(
      base::OnceCallback<void(base::OnceClosure finish_callback)> task,
      base::WeakPtr<SessionWrapper> session = nullptr) {
    base::ScopedClosureRunner task_finished(
        base::BindPostTaskToCurrentDefault(base::BindOnce(
            &ModelWrapper::TaskFinished, weak_ptr_factory_.GetWeakPtr())));
    pending_tasks_.push(PendingTask{
        .session = session,
        .task = base::BindOnce(std::move(task),
                               base::BindOnce([](base::ScopedClosureRunner) {},
                                              std::move(task_finished))),
    });
    RunTaskIfPossible();
  }

  void StartSession(mojo::PendingReceiver<mojom::Session> session,
                    mojom::SessionParamsPtr params) override {
    AddSession(std::move(session),
               model_->CreateSession(receivers_.current_context().get(),
                                     std::move(params)));
  }

  void ClassifyTextSafety(const std::string& text,
                          ClassifyTextSafetyCallback callback) override {
    if (!ts_model_ || !ts_session_.is_bound()) {
      std::move(callback).Run(nullptr);
      return;
    }
    ts_session_->ClassifyTextSafety(text, std::move(callback));
  }

  void DetectLanguage(const std::string& text,
                      DetectLanguageCallback callback) override {
    if (!ts_model_ || !ts_session_.is_bound()) {
      std::move(callback).Run(nullptr);
      return;
    }
    ts_session_->DetectLanguage(text, std::move(callback));
  }

  void LoadAdaptation(mojom::LoadAdaptationParamsPtr params,
                      mojo::PendingReceiver<mojom::OnDeviceModel> model,
                      LoadAdaptationCallback callback) override {
    auto load_adaptation = base::BindOnce(
        &ModelWrapper::LoadAdaptationInternal, weak_ptr_factory_.GetWeakPtr(),
        std::move(params), std::move(model), std::move(callback));
    AddAndRunPendingTask(
        base::IgnoreArgs<base::OnceClosure>(std::move(load_adaptation)));
  }

  void AddSession(mojo::PendingReceiver<mojom::Session> receiver,
                  std::unique_ptr<ml::SessionImpl> session) {
    auto current_session = std::make_unique<SessionWrapper>(
        weak_ptr_factory_.GetWeakPtr(), std::move(receiver),
        std::move(session));
    SessionWrapper* current_session_ptr = current_session.get();
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
    auto result = model_->LoadAdaptation(std::move(params));
    CHECK(result);
    ReportHistogramMediumTimes(metrics_,
                               "OnDeviceModel.LoadAdaptationModelDuration",
                               base::TimeTicks::Now() - start);
    receivers_.Add(this, std::move(model), std::move(result));
    std::move(callback).Run(mojom::LoadModelResult::kSuccess);
  }

  void RunTaskIfPossible() {
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
  std::set<std::unique_ptr<SessionWrapper>, base::UniquePtrComparator>
      sessions_;
  std::unique_ptr<ml::OnDeviceModelExecutor> model_;
  mojo::ReceiverSet<
      mojom::OnDeviceModel,
      std::unique_ptr<ml::OnDeviceModelExecutor::ScopedAdaptation>>
      receivers_;
  base::OnceCallback<void(base::WeakPtr<mojom::OnDeviceModel>)> on_delete_;
  std::queue<PendingTask> pending_tasks_;
  bool is_running_ = false;
  base::WeakPtr<SessionWrapper> running_session_;
  // Last session a task was executed in.
  base::WeakPtr<SessionWrapper> last_session_;
  mojom::TextSafetyModel* ts_model_;
  mojo::Remote<mojom::TextSafetySession> ts_session_;
  base::WeakPtrFactory<ModelWrapper> weak_ptr_factory_{this};
};

void SessionWrapper::Append(mojom::AppendOptionsPtr options,
                            mojo::PendingRemote<mojom::ContextClient> client) {
  if (!model_) {
    return;
  }

  auto append_internal = base::BindOnce(&SessionWrapper::AppendInternal,
                                        weak_ptr_factory_.GetWeakPtr(),
                                        std::move(options), std::move(client));

  model_->AddAndRunPendingTask(std::move(append_internal),
                               weak_ptr_factory_.GetWeakPtr());
}

void SessionWrapper::Generate(
    mojom::GenerateOptionsPtr options,
    mojo::PendingRemote<mojom::StreamingResponder> response) {
  if (!model_) {
    return;
  }

  auto generate_internal = base::BindOnce(
      &SessionWrapper::GenerateInternal, weak_ptr_factory_.GetWeakPtr(),
      std::move(options), std::move(response));

  model_->AddAndRunPendingTask(std::move(generate_internal),
                               weak_ptr_factory_.GetWeakPtr());
}

void SessionWrapper::GetSizeInTokens(mojom::InputPtr input,
                                     GetSizeInTokensCallback callback) {
  if (!model_) {
    return;
  }

  auto size_in_tokens_internal = base::BindOnce(
      &SessionWrapper::GetSizeInTokensInternal, weak_ptr_factory_.GetWeakPtr(),
      std::move(input), std::move(callback));

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

  model_->AddSession(std::move(session), session_->Clone());
}

}  // namespace

OnDeviceModelService::OnDeviceModelService(
    raw_ref<MetricsLibraryInterface> metrics,
    raw_ref<odml::PeriodicMetrics> periodic_metrics,
    raw_ref<odml::OdmlShimLoader> shim_loader)
    : metrics_(metrics),
      periodic_metrics_(periodic_metrics),
      shim_loader_(shim_loader),
      platform_model_loader_(std::make_unique<ChromeosPlatformModelLoader>(
          metrics_, periodic_metrics_, raw_ref(*this))) {}

OnDeviceModelService::~OnDeviceModelService() = default;

void OnDeviceModelService::LoadModel(
    mojom::LoadModelParamsPtr params,
    mojo::PendingReceiver<mojom::OnDeviceModel> model,
    LoadPlatformModelCallback callback) {
  LoadModel(std::move(params), std::move(model), std::move(callback), nullptr);
}

void OnDeviceModelService::LoadModel(
    mojom::LoadModelParamsPtr params,
    mojo::PendingReceiver<mojom::OnDeviceModel> model,
    LoadPlatformModelCallback callback,
    mojom::TextSafetyModel* ts_model) {
  auto start = base::TimeTicks::Now();
  const ml::ChromeML* chrome_ml = ml::ChromeML::Get(metrics_, shim_loader_);
  if (!chrome_ml) {
    std::move(callback).Run(mojom::LoadModelResult::kFailedToLoadLibrary);
    return;
  }

  auto model_impl = ml::OnDeviceModelExecutor::CreateWithResult(
      metrics_, *chrome_ml, std::move(params),
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
      metrics_, std::move(model_impl.value()), std::move(model),
      base::BindOnce(&OnDeviceModelService::DeleteModel,
                     base::Unretained(this)),
      ts_model));
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

  auto is_apu_available = shim_loader_->Get<bool (*)()>("IsApuAvailable");
  if (is_apu_available && is_apu_available()) {
    std::move(callback).Run(on_device_model::mojom::PerformanceClass::kHigh);
    return;
  }

  const ml::ChromeML* chrome_ml = ml::ChromeML::Get(metrics_, shim_loader_);
  if (!chrome_ml) {
    std::move(callback).Run(mojom::PerformanceClass::kFailedToLoadLibrary);
    return;
  }

  std::move(callback).Run(
      ml::GetEstimatedPerformanceClass(metrics_, *chrome_ml));
}

void OnDeviceModelService::LoadTextSafetyModel(
    on_device_model::mojom::TextSafetyModelParamsPtr params,
    mojo::PendingReceiver<mojom::TextSafetyModel> model) {
  if (ts_holder_.is_null()) {
    const ml::ChromeML* chrome_ml = ml::ChromeML::Get(metrics_, shim_loader_);
    if (!chrome_ml) {
      return;
    }

    ts_holder_ = ml::TsHolder::Create(raw_ref(*chrome_ml));
  }

  ts_holder_.AsyncCall(&ml::TsHolder::Reset)
      .WithArgs(std::move(params), std::move(model));
}

void OnDeviceModelService::LoadPlatformTextSafetyModel(
    const base::Uuid& uuid,
    mojo::PendingReceiver<mojom::TextSafetyModel> model,
    mojo::PendingRemote<mojom::PlatformModelProgressObserver> progress_observer,
    LoadPlatformModelCallback callback) {
  if (RetryIfShimIsNotReady(&OnDeviceModelService::LoadPlatformTextSafetyModel,
                            callback,
                            mojom::LoadModelResult::kFailedToLoadLibrary, uuid,
                            model, progress_observer)) {
    return;
  }

  platform_model_loader_->LoadTextSafetyModelWithUuid(
      uuid, std::move(model), std::move(progress_observer),
      std::move(callback));
  return;
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
