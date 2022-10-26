// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>

#include "base/bind.h"
#include "cryptohome/auth_blocks/fp_service.h"
#include "cryptohome/error/cryptohome_error.h"
#include "cryptohome/error/location_utils.h"
#include "cryptohome/error/locations.h"
#include "cryptohome/fingerprint_manager.h"
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>

namespace cryptohome {

using cryptohome::error::CryptohomeError;
using cryptohome::error::ErrorAction;
using cryptohome::error::ErrorActionSet;
using hwsec_foundation::status::MakeStatus;
using hwsec_foundation::status::OkStatus;

void FingerprintAuthBlockService::CheckSessionStartResult(
    base::OnceCallback<void(CryptohomeStatus)> on_done, bool success) {
  if (!success) {
    CryptohomeStatus cryptohome_status = MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocFpServiceStartSessionFailure),
        ErrorActionSet({ErrorAction::kRetry}),
        user_data_auth::CryptohomeErrorCode::
            CRYPTOHOME_ERROR_FINGERPRINT_ERROR_INTERNAL);
    std::move(on_done).Run(std::move(cryptohome_status));
    return;
  }

  FingerprintManager* fp_manager = fp_manager_getter_.Run();
  if (!fp_manager) {
    CryptohomeStatus status = MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocFpServiceCheckSessionStartCouldNotGetFpManager),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CryptohomeErrorCode::
            CRYPTOHOME_ERROR_FINGERPRINT_ERROR_INTERNAL);
    std::move(on_done).Run(std::move(status));
    return;
  }
  fp_manager->SetSignalCallback(base::BindRepeating(
      &FingerprintAuthBlockService::Capture, base::Unretained(this)));
  std::move(on_done).Run(OkStatus<CryptohomeError>());
}

FingerprintAuthBlockService::FingerprintAuthBlockService(
    base::RepeatingCallback<FingerprintManager*()> fp_manager_getter,
    base::RepeatingCallback<void(user_data_auth::FingerprintScanResult)>
        signal_sender)
    : fp_manager_getter_(fp_manager_getter), signal_sender_(signal_sender) {}

std::unique_ptr<FingerprintAuthBlockService>
FingerprintAuthBlockService::MakeNullService() {
  // Construct an instance of the service with a getter callbacks that always
  // return null and a signal sender that does nothing.
  return std::make_unique<FingerprintAuthBlockService>(
      base::BindRepeating([]() -> FingerprintManager* { return nullptr; }),
      base::BindRepeating([](user_data_auth::FingerprintScanResult) {}));
}

void FingerprintAuthBlockService::Verify(
    base::OnceCallback<void(CryptohomeStatus)> on_done) {
  CryptohomeStatus cryptohome_status;
  FingerprintManager* fp_manager = fp_manager_getter_.Run();
  if (!fp_manager) {
    cryptohome_status = MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocFpServiceVerifyCouldNotGetFpManager),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CryptohomeErrorCode::
            CRYPTOHOME_ERROR_FINGERPRINT_ERROR_INTERNAL);
    std::move(on_done).Run(std::move(cryptohome_status));
    return;
  }

  // |user_| is not set, the service is not set up properly and
  // the verification should fail.
  if (user_.empty()) {
    cryptohome_status = MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocFpServiceCheckResultNoAuthSession),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CryptohomeErrorCode::
            CRYPTOHOME_ERROR_FINGERPRINT_ERROR_INTERNAL);
    std::move(on_done).Run(std::move(cryptohome_status));
    return;
  }

  // Use the latest scan result to decide the response status.
  switch (scan_result_) {
    case FingerprintScanStatus::SUCCESS:
      cryptohome_status = OkStatus<CryptohomeError>();
      break;
    case FingerprintScanStatus::FAILED_RETRY_ALLOWED:
      cryptohome_status = MakeStatus<CryptohomeError>(
          CRYPTOHOME_ERR_LOC(kLocFpServiceCheckResultFailedYesRetry),
          ErrorActionSet({ErrorAction::kRetry}),
          user_data_auth::CryptohomeErrorCode::
              CRYPTOHOME_ERROR_FINGERPRINT_RETRY_REQUIRED);
      break;
    case FingerprintScanStatus::FAILED_RETRY_NOT_ALLOWED:
      cryptohome_status = MakeStatus<CryptohomeError>(
          CRYPTOHOME_ERR_LOC(kLocFpServiceCheckResultFailedNoRetry),
          ErrorActionSet({ErrorAction::kAuth}),
          user_data_auth::CryptohomeErrorCode::
              CRYPTOHOME_ERROR_FINGERPRINT_DENIED);
  }
  std::move(on_done).Run(std::move(cryptohome_status));
}

void FingerprintAuthBlockService::Start(
    std::string obfuscated_username,
    base::OnceCallback<void(CryptohomeStatus)> on_done) {
  FingerprintManager* fp_manager = fp_manager_getter_.Run();
  if (!fp_manager) {
    CryptohomeStatus status = MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocFpServiceStartScanCouldNotGetFpManager),
        ErrorActionSet({ErrorAction::kRetry}),
        user_data_auth::CryptohomeErrorCode::
            CRYPTOHOME_ERROR_ATTESTATION_NOT_READY);
    std::move(on_done).Run(std::move(status));
    return;
  }

  if (!user_.empty()) {
    CryptohomeStatus status = MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocFpServiceStartConcurrentSession),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CryptohomeErrorCode::
            CRYPTOHOME_ERROR_FINGERPRINT_DENIED);
    std::move(on_done).Run(std::move(status));
    return;
  }
  user_ = obfuscated_username;

  // Set up a callback with the manager to check the start session result.
  fp_manager->StartAuthSessionAsyncForUser(
      user_,
      base::BindOnce(&FingerprintAuthBlockService::CheckSessionStartResult,
                     base::Unretained(this), std::move(on_done)));
}

void FingerprintAuthBlockService::Terminate() {
  user_.clear();
  scan_result_ = FingerprintScanStatus::FAILED_RETRY_NOT_ALLOWED;
  EndAuthSession();
}

void FingerprintAuthBlockService::Capture(FingerprintScanStatus status) {
  // If the session has been terminated, the registered user is cleared.
  // In this case, no-op when the callback is triggered.
  if (user_.empty()) {
    return;
  }
  scan_result_ = status;
  user_data_auth::FingerprintScanResult outgoing_signal;
  switch (status) {
    case FingerprintScanStatus::SUCCESS:
      outgoing_signal = user_data_auth::FINGERPRINT_SCAN_RESULT_SUCCESS;
      break;
    case FingerprintScanStatus::FAILED_RETRY_ALLOWED:
      outgoing_signal = user_data_auth::FINGERPRINT_SCAN_RESULT_RETRY;
      break;
    case FingerprintScanStatus::FAILED_RETRY_NOT_ALLOWED:
      outgoing_signal = user_data_auth::FINGERPRINT_SCAN_RESULT_LOCKOUT;
  }
  if (signal_sender_) {
    signal_sender_.Run(outgoing_signal);
  }
}

void FingerprintAuthBlockService::EndAuthSession() {
  FingerprintManager* fp_manager = fp_manager_getter_.Run();
  if (fp_manager) {
    fp_manager->EndAuthSession();
  }
}

FingerprintVerifier::FingerprintVerifier(FingerprintAuthBlockService* service)
    : AsyncCredentialVerifier(AuthFactorType::kLegacyFingerprint, "", {}),
      service_(service) {}

void FingerprintVerifier::VerifyAsync(const AuthInput& unused,
                                      StatusCallback callback) const {
  return service_->Verify(std::move(callback));
}

}  // namespace cryptohome
