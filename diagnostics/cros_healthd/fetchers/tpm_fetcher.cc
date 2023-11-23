// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fetchers/tpm_fetcher.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <attestation/proto_bindings/interface.pb.h>
#include <attestation-client/attestation/dbus-proxies.h>
#include <base/check.h>
#include <base/functional/callback.h>
#include <base/strings/string_number_conversions.h>
#include <base/time/time.h>
#include <brillo/errors/error.h>
#include <tpm_manager/proto_bindings/tpm_manager.pb.h>
#include <tpm_manager-client/tpm_manager/dbus-proxies.h>

#include "diagnostics/base/file_utils.h"
#include "diagnostics/cros_healthd/system/context.h"
#include "diagnostics/cros_healthd/utils/callback_barrier.h"
#include "diagnostics/cros_healthd/utils/dbus_utils.h"
#include "diagnostics/cros_healthd/utils/error_utils.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

// Tpm manager and attestation require a long timeout.
constexpr int64_t kDBusTimeoutMs = base::Minutes(2).InMilliseconds();

mojom::TpmGSCVersion GetGscVersion(
    const tpm_manager::GetVersionInfoReply& reply) {
  switch (reply.gsc_version()) {
    case tpm_manager::GSC_VERSION_NOT_GSC:
      return mojom::TpmGSCVersion::kNotGSC;
    case tpm_manager::GSC_VERSION_CR50:
      return mojom::TpmGSCVersion::kCr50;
    case tpm_manager::GSC_VERSION_TI50:
      return mojom::TpmGSCVersion::kTi50;
  }
}

class State {
 public:
  State();
  State(const State&) = delete;
  State& operator=(const State&) = delete;
  ~State() = default;

  // Handle the response of version from tpm manager.
  void HandleVersion(brillo::Error* err,
                     const tpm_manager::GetVersionInfoReply& reply);

  // Handle the response of status from tpm manager.
  void HandleStatus(brillo::Error* err,
                    const tpm_manager::GetTpmNonsensitiveStatusReply& reply);

  // Handle the response of dictionary attack from tpm manager.
  void HandleDictionaryAttack(
      brillo::Error* err,
      const tpm_manager::GetDictionaryAttackInfoReply& reply);

  // Handle the response of status from attestation.
  void HandleAttestation(brillo::Error* err,
                         const attestation::GetStatusReply& reply);

  // Handle the response of supported features from tpm manager.
  void HandleSupportedFeatures(
      brillo::Error* err, const tpm_manager::GetSupportedFeaturesReply& reply);

  // Set up the |error_|.
  void SetError(const std::string& message);

  // Send back the TpmResult via |callback|. The result is ProbeError if
  // |error_| is not null or |is_finished| is false, otherwise |info_|.
  void HandleResult(FetchTpmInfoCallback callback, bool is_finished);

 private:
  // The info to be returned.
  mojom::TpmInfoPtr info_;
  // The error to be returned.
  mojom::ProbeErrorPtr error_;
};

State::State() : info_(mojom::TpmInfo::New()) {
  ReadAndTrimString(GetRootDir().Append(kFileTpmDidVid), &info_->did_vid);
}

void State::HandleVersion(brillo::Error* err,
                          const tpm_manager::GetVersionInfoReply& reply) {
  if (err) {
    SetError("Failed to call TpmManager::GetVersionInfo(): " +
             err->GetMessage());
    return;
  }
  if (reply.status() != tpm_manager::STATUS_SUCCESS) {
    SetError("TpmManager::GetVersionInfo() returned error status: " +
             base::NumberToString(reply.status()));
    return;
  }
  auto version = mojom::TpmVersion::New();
  version->gsc_version = GetGscVersion(reply);
  version->family = reply.family();
  version->spec_level = reply.spec_level();
  version->manufacturer = reply.manufacturer();
  version->tpm_model = reply.tpm_model();
  version->firmware_version = reply.firmware_version();
  version->vendor_specific = reply.vendor_specific().empty()
                                 ? std::nullopt
                                 : std::make_optional(reply.vendor_specific());
  info_->version = std::move(version);
}

void State::HandleStatus(
    brillo::Error* err,
    const tpm_manager::GetTpmNonsensitiveStatusReply& reply) {
  if (err) {
    SetError("Failed to call TpmManager::GetTpmNonsensitiveStatus(): " +
             err->GetMessage());
    return;
  }
  if (reply.status() != tpm_manager::STATUS_SUCCESS) {
    SetError("TpmManager::GetTpmNonsensitiveStatus() returned error status: " +
             base::NumberToString(reply.status()));
    return;
  }
  auto status = mojom::TpmStatus::New();
  status->enabled = reply.is_enabled();
  status->owned = reply.is_owned();
  status->owner_password_is_present = reply.is_owner_password_present();
  info_->status = std::move(status);
}

void State::HandleDictionaryAttack(
    brillo::Error* err,
    const tpm_manager::GetDictionaryAttackInfoReply& reply) {
  if (err) {
    SetError("Failed to call TpmManager::GetDictionaryAttackInfo(): " +
             err->GetMessage());
    return;
  }
  if (reply.status() != tpm_manager::STATUS_SUCCESS) {
    SetError("TpmManager::GetDictionaryAttackInfo() returned error status: " +
             base::NumberToString(reply.status()));
    return;
  }

  auto da = mojom::TpmDictionaryAttack::New();
  da->counter = reply.dictionary_attack_counter();
  da->threshold = reply.dictionary_attack_threshold();
  da->lockout_in_effect = reply.dictionary_attack_lockout_in_effect();
  da->lockout_seconds_remaining =
      reply.dictionary_attack_lockout_seconds_remaining();
  info_->dictionary_attack = std::move(da);
}

void State::HandleAttestation(brillo::Error* err,
                              const attestation::GetStatusReply& reply) {
  if (err) {
    SetError("Failed to call Attestation::GetStatus(): " + err->GetMessage());
    return;
  }
  if (reply.status() != attestation::STATUS_SUCCESS) {
    SetError("Attestation::GetStatus() returned error status: " +
             base::NumberToString(reply.status()));
    return;
  }

  auto data = mojom::TpmAttestation::New();
  data->prepared_for_enrollment = reply.prepared_for_enrollment();
  data->enrolled = reply.enrolled();
  info_->attestation = std::move(data);
}

void State::HandleSupportedFeatures(
    brillo::Error* err, const tpm_manager::GetSupportedFeaturesReply& reply) {
  if (err) {
    SetError("Failed to call TpmManager::GetSupportedFeatures(): " +
             err->GetMessage());
    return;
  }
  if (reply.status() != tpm_manager::STATUS_SUCCESS) {
    SetError("TpmManager::GetSupportedFeatures() returned error status: " +
             base::NumberToString(reply.status()));
    return;
  }

  auto data = mojom::TpmSupportedFeatures::New();
  data->support_u2f = reply.support_u2f();
  data->support_pinweaver = reply.support_pinweaver();
  data->support_runtime_selection = reply.support_runtime_selection();
  data->is_allowed = reply.is_allowed();
  info_->supported_features = std::move(data);
}

void State::SetError(const std::string& message) {
  error_ =
      CreateAndLogProbeError(mojom::ErrorType::kServiceUnavailable, message);
}

void State::HandleResult(FetchTpmInfoCallback callback, bool is_finished) {
  if (!is_finished) {
    error_ = CreateAndLogProbeError(mojom::ErrorType::kSystemUtilityError,
                                    "Failed to finish all callbacks.");
  }
  if (!error_.is_null()) {
    std::move(callback).Run(mojom::TpmResult::NewError(std::move(error_)));
    return;
  }
  std::move(callback).Run(mojom::TpmResult::NewTpmInfo(std::move(info_)));
}

void FetchVersion(Context* context,
                  CallbackBarrier& barrier,
                  State* state_ptr) {
  tpm_manager::GetVersionInfoRequest request;
  auto [on_success, on_error] = SplitDbusCallback(barrier.Depend(
      base::BindOnce(&State::HandleVersion, base::Unretained(state_ptr))));
  context->tpm_manager_proxy()->GetVersionInfoAsync(
      request, std::move(on_success), std::move(on_error), kDBusTimeoutMs);
}

void FetchStatus(Context* context, CallbackBarrier& barrier, State* state_ptr) {
  tpm_manager::GetTpmNonsensitiveStatusRequest request;
  auto [on_success, on_error] = SplitDbusCallback(barrier.Depend(
      base::BindOnce(&State::HandleStatus, base::Unretained(state_ptr))));
  context->tpm_manager_proxy()->GetTpmNonsensitiveStatusAsync(
      request, std::move(on_success), std::move(on_error), kDBusTimeoutMs);
}

void FetchDictionaryAttack(Context* context,
                           CallbackBarrier& barrier,
                           State* state_ptr) {
  tpm_manager::GetDictionaryAttackInfoRequest request;
  auto [on_success, on_error] = SplitDbusCallback(barrier.Depend(base::BindOnce(
      &State::HandleDictionaryAttack, base::Unretained(state_ptr))));
  context->tpm_manager_proxy()->GetDictionaryAttackInfoAsync(
      request, std::move(on_success), std::move(on_error), kDBusTimeoutMs);
}

void FetchAttestation(Context* context,
                      CallbackBarrier& barrier,
                      State* state_ptr) {
  attestation::GetStatusRequest request;
  auto [on_success, on_error] = SplitDbusCallback(barrier.Depend(
      base::BindOnce(&State::HandleAttestation, base::Unretained(state_ptr))));
  context->attestation_proxy()->GetStatusAsync(
      request, std::move(on_success), std::move(on_error), kDBusTimeoutMs);
}

void FetchSupportedFeatures(Context* context,
                            CallbackBarrier& barrier,
                            State* state_ptr) {
  tpm_manager::GetSupportedFeaturesRequest request;
  auto [on_success, on_error] = SplitDbusCallback(barrier.Depend(base::BindOnce(
      &State::HandleSupportedFeatures, base::Unretained(state_ptr))));
  context->tpm_manager_proxy()->GetSupportedFeaturesAsync(
      request, std::move(on_success), std::move(on_error), kDBusTimeoutMs);
}

}  // namespace

void FetchTpmInfo(Context* context, FetchTpmInfoCallback callback) {
  auto state = std::make_unique<State>();
  State* state_ptr = state.get();
  CallbackBarrier barrier{base::BindOnce(&State::HandleResult, std::move(state),
                                         std::move(callback))};
  FetchVersion(context, barrier, state_ptr);
  FetchStatus(context, barrier, state_ptr);
  FetchDictionaryAttack(context, barrier, state_ptr);
  FetchAttestation(context, barrier, state_ptr);
  FetchSupportedFeatures(context, barrier, state_ptr);
}

}  // namespace diagnostics
