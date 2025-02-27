// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/cros_safety/safety_service_manager_impl.h"

#include <utility>

#include <base/check.h>
#include <base/functional/bind.h>
#include <base/functional/callback_forward.h>
#include <metrics/metrics_library.h>
#include <mojo/public/cpp/bindings/callback_helpers.h>
#include <mojo/public/cpp/bindings/remote.h>
#include <mojo/service_constants.h>
#include <mojo_service_manager/lib/connect.h>
#include <mojo_service_manager/lib/mojom/service_manager.mojom.h>

#include "base/containers/fixed_flat_set.h"
#include "odml/mojom/cros_safety.mojom-shared.h"
#include "odml/utils/performance_timer.h"
namespace cros_safety {

namespace {

// These are the verdicts that actually returned by the the safety model.
constexpr auto kTrueSafetyVerdicts =
    base::MakeFixedFlatSet<mojom::SafetyClassifierVerdict>(
        {mojom::SafetyClassifierVerdict::kPass,
         mojom::SafetyClassifierVerdict::kFailedText,
         mojom::SafetyClassifierVerdict::kFailedImage});

}  // namespace

SafetyServiceManagerImpl::SafetyServiceManagerImpl(
    mojo::Remote<chromeos::mojo_service_manager::mojom::ServiceManager>&
        service_manager,
    raw_ref<MetricsLibraryInterface> metrics)
    : service_manager_(service_manager), metrics_(metrics) {
  CHECK(service_manager_);
  service_manager_->Request(
      /*service_name=*/chromeos::mojo_services::kCrosSafetyService,
      /*timeout=*/std::nullopt,
      safety_service_.BindNewPipeAndPassReceiver().PassPipe());

  safety_service_.set_disconnect_with_reason_handler(
      base::BindOnce(&SafetyServiceManagerImpl::OnSafetyServiceDisconnected,
                     weak_ptr_factory_.GetWeakPtr()));
}

void SafetyServiceManagerImpl::PrepareImageSafetyClassifier(
    base::OnceCallback<void(bool)> callback) {
  if (cloud_safety_session_.is_bound()) {
    return std::move(callback).Run(true);
  }

  safety_service_->CreateCloudSafetySession(
      cloud_safety_session_.BindNewPipeAndPassReceiver(),
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(
          base::BindOnce(
              [](base::OnceCallback<void(bool)> callback,
                 mojom::GetCloudSafetySessionResult result) {
                std::move(callback).Run(
                    result == mojom::GetCloudSafetySessionResult::kOk);
              },
              std::move(callback)),
          mojom::GetCloudSafetySessionResult::kGenericError));
}

void SafetyServiceManagerImpl::ClassifyImageSafety(
    mojom::SafetyRuleset ruleset,
    const std::optional<std::string>& text,
    mojo_base::mojom::BigBufferPtr image,
    ClassifySafetyCallback callback) {
  EnsureCloudSafetySessionCreated(
      base::BindOnce(&SafetyServiceManagerImpl::ClassifyImageSafetyInternal,
                     weak_ptr_factory_.GetWeakPtr(), ruleset, text,
                     std::move(image), std::move(callback)));
}

void SafetyServiceManagerImpl::OnSafetyServiceDisconnected(
    uint32_t error, const std::string& message) {
  LOG(INFO) << "Disconnected from chrome safety service manager. Error: "
            << error << ", message: " << message
            << ". Reset remote and request again from mojo service manager.";
  safety_service_.reset();
  service_manager_->Request(
      /*service_name=*/chromeos::mojo_services::kCrosSafetyService,
      /*timeout=*/std::nullopt,
      safety_service_.BindNewPipeAndPassReceiver().PassPipe());
  safety_service_.set_disconnect_with_reason_handler(
      base::BindOnce(&SafetyServiceManagerImpl::OnSafetyServiceDisconnected,
                     weak_ptr_factory_.GetWeakPtr()));

  // The safety sessions must be disconnected as well when safety service is
  // dead, so reset them anyway.
  cloud_safety_session_.reset();
  on_device_safety_session_.reset();
}

void SafetyServiceManagerImpl::OnCloudSafetySessionDisconnected(
    uint32_t error, const std::string& message) {
  LOG(INFO) << "Disconnected from cloud safety session. Error: " << error
            << ", message: " << message << ". Reset remote.";
  cloud_safety_session_.reset();
}

void SafetyServiceManagerImpl::OnOnDeviceSafetySessionDisconnected(
    uint32_t error, const std::string& message) {
  LOG(INFO) << "Disconnected from on-device safety session. Error: " << error
            << ", message: " << message << ". Reset remote.";
  on_device_safety_session_.reset();
}

void SafetyServiceManagerImpl::OnClassifySafetyDone(
    ClassifySafetyCallback callback,
    mojom::SafetyRuleset ruleset,
    odml::PerformanceTimer::Ptr timer,
    mojom::SafetyClassifierVerdict verdict) {
  metrics_.SendClassifySafetyResult(ruleset, verdict);
  if (!kTrueSafetyVerdicts.contains(verdict)) {
    LOG(WARNING) << "Internal error(" << verdict
                 << ") encountered while classifying safety with ruleset '"
                 << ruleset << ".";
  } else {
    metrics_.SendClassifySafetyLatency(ruleset, timer->GetDuration());
  }
  std::move(callback).Run(verdict);
}

void SafetyServiceManagerImpl::EnsureCloudSafetySessionCreated(
    base::OnceClosure callback) {
  if (cloud_safety_session_.is_bound()) {
    std::move(callback).Run();
    return;
  }

  safety_service_->CreateCloudSafetySession(
      cloud_safety_session_.BindNewPipeAndPassReceiver(),
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(
          base::BindOnce(&SafetyServiceManagerImpl::GetCloudSafetySessionDone,
                         weak_ptr_factory_.GetWeakPtr(), std::move(callback)),
          mojom::GetCloudSafetySessionResult::kGenericError));
}

void SafetyServiceManagerImpl::GetCloudSafetySessionDone(
    base::OnceClosure callback, mojom::GetCloudSafetySessionResult result) {
  metrics_.SendGetCloudSafetySessionResult(result);
  if (result != mojom::GetCloudSafetySessionResult::kOk) {
    cloud_safety_session_.reset();
    LOG(ERROR) << "GetCloudSafetySession failed with result: " << result;
  } else {
    cloud_safety_session_.set_disconnect_with_reason_handler(base::BindOnce(
        &SafetyServiceManagerImpl::OnCloudSafetySessionDisconnected,
        weak_ptr_factory_.GetWeakPtr()));
  }
  std::move(callback).Run();
}

void SafetyServiceManagerImpl::ClassifyImageSafetyInternal(
    mojom::SafetyRuleset ruleset,
    const std::optional<std::string>& text,
    mojo_base::mojom::BigBufferPtr image,
    ClassifySafetyCallback callback) {
  if (!cloud_safety_session_.is_bound()) {
    std::move(callback).Run(mojom::SafetyClassifierVerdict::kGenericError);
    return;
  }

  cloud_safety_session_->ClassifyImageSafety(
      ruleset, text, std::move(image),
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(
          base::BindOnce(&SafetyServiceManagerImpl::OnClassifySafetyDone,
                         weak_ptr_factory_.GetWeakPtr(), std::move(callback),
                         ruleset, odml::PerformanceTimer::Create()),
          mojom::SafetyClassifierVerdict::kServiceNotAvailable));
}

void SafetyServiceManagerImpl::ClassifyTextSafety(
    mojom::SafetyRuleset ruleset,
    const std::string& text,
    ClassifySafetyCallback callback) {
  EnsureOnDeviceSafetySessionCreated(base::BindOnce(
      &SafetyServiceManagerImpl::ClassifyTextSafetyInternal,
      weak_ptr_factory_.GetWeakPtr(), ruleset, text, std::move(callback)));
}

void SafetyServiceManagerImpl::EnsureOnDeviceSafetySessionCreated(
    base::OnceClosure callback) {
  if (on_device_safety_session_.is_bound()) {
    std::move(callback).Run();
    return;
  }

  safety_service_->CreateOnDeviceSafetySession(
      on_device_safety_session_.BindNewPipeAndPassReceiver(),
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(
          base::BindOnce(
              &SafetyServiceManagerImpl::GetOnDeviceSafetySessionDone,
              weak_ptr_factory_.GetWeakPtr(), std::move(callback)),
          mojom::GetOnDeviceSafetySessionResult::
              kCrosSafetyServiceNotAvailable));
}

void SafetyServiceManagerImpl::GetOnDeviceSafetySessionDone(
    base::OnceClosure callback, mojom::GetOnDeviceSafetySessionResult result) {
  metrics_.SendGetOnDeviceSafetySessionResult(result);
  if (result != mojom::GetOnDeviceSafetySessionResult::kOk) {
    on_device_safety_session_.reset();
    LOG(ERROR) << "GetOnDeviceSafetySession failed with result: " << result;
  } else {
    on_device_safety_session_.set_disconnect_with_reason_handler(base::BindOnce(
        &SafetyServiceManagerImpl::OnOnDeviceSafetySessionDisconnected,
        weak_ptr_factory_.GetWeakPtr()));
  }
  std::move(callback).Run();
}

void SafetyServiceManagerImpl::ClassifyTextSafetyInternal(
    mojom::SafetyRuleset ruleset,
    const std::string& text,
    ClassifySafetyCallback callback) {
  if (!on_device_safety_session_.is_bound()) {
    std::move(callback).Run(mojom::SafetyClassifierVerdict::kGenericError);
    return;
  }

  on_device_safety_session_->ClassifyTextSafety(
      ruleset, text,
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(
          base::BindOnce(&SafetyServiceManagerImpl::OnClassifySafetyDone,
                         weak_ptr_factory_.GetWeakPtr(), std::move(callback),
                         ruleset, odml::PerformanceTimer::Create()),
          mojom::SafetyClassifierVerdict::kServiceNotAvailable));
}

}  // namespace cros_safety
