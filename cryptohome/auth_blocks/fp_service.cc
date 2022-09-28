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

namespace cryptohome {

using cryptohome::error::CryptohomeError;
using cryptohome::error::ErrorAction;
using cryptohome::error::ErrorActionSet;
using hwsec_foundation::status::MakeStatus;
using hwsec_foundation::status::OkStatus;

void FingerprintAuthBlockService::CheckFingerprintResult(
    base::OnceCallback<void(CryptohomeStatus)> on_done,
    FingerprintScanStatus status) {
  scan_result_ = status;

  // Update the attempt counter, which will be checked when initiating a
  // fingerprint scan the next time.
  switch (status) {
    case FingerprintScanStatus::SUCCESS:
      break;
    case FingerprintScanStatus::FAILED_RETRY_ALLOWED:
      if (attempts_left_ > 0) {
        attempts_left_--;
      }
      if (attempts_left_ <= 0) {
        scan_result_ = FingerprintScanStatus::FAILED_RETRY_NOT_ALLOWED;
      }
      break;
    case FingerprintScanStatus::FAILED_RETRY_NOT_ALLOWED:
      attempts_left_ = 0;
  }

  EndAuthSession();

  // Return success status regardless the scan result, it simply means a
  // fingerprint scan has completed.
  std::move(on_done).Run(OkStatus<CryptohomeError>());
}

void FingerprintAuthBlockService::CheckSessionStartResult(
    base::OnceCallback<void(CryptohomeStatus)> on_done, bool success) {
  if (!success) {
    CryptohomeStatus cryptohome_status = MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocFpServiceStartSessionFailure),
        ErrorActionSet({ErrorAction::kRetry}),
        user_data_auth::CryptohomeErrorCode::
            CRYPTOHOME_ERROR_FINGERPRINT_ERROR_INTERNAL);
    std::move(on_done).Run(std::move(cryptohome_status));
  } else {
    Capture(std::move(on_done));
  }
}

FingerprintAuthBlockService::FingerprintAuthBlockService(
    base::RepeatingCallback<FingerprintManager*()> fp_manager_getter)
    : fp_manager_getter_(fp_manager_getter) {}

std::unique_ptr<FingerprintAuthBlockService>
FingerprintAuthBlockService::MakeNullService() {
  // Construct an instance of the service with a getter callbacks that always
  // return null.
  return std::make_unique<FingerprintAuthBlockService>(
      base::BindRepeating([]() -> FingerprintManager* { return nullptr; }));
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

void FingerprintAuthBlockService::Scan(
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

  // empty user_ means Start was not called.
  if (user_.empty()) {
    CryptohomeStatus status = MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocFpServiceStartScanNoStart),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CryptohomeErrorCode::
            CRYPTOHOME_ERROR_FINGERPRINT_DENIED);
    std::move(on_done).Run(std::move(status));
    return;
  }

  if (attempts_left_ <= 0) {
    CryptohomeStatus status = MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocFpServiceStartScanLockedOut),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CryptohomeErrorCode::
            CRYPTOHOME_ERROR_FINGERPRINT_DENIED);
    std::move(on_done).Run(std::move(status));
    return;
  }

  // Set up a callback with the manager to check the start session result.
  fp_manager->StartAuthSessionAsyncForUser(
      user_,
      base::BindOnce(&FingerprintAuthBlockService::CheckSessionStartResult,
                     base::Unretained(this), std::move(on_done)));
}

CryptohomeStatus FingerprintAuthBlockService::Start(
    std::string obfuscated_username) {
  if (!user_.empty() && user_ != obfuscated_username) {
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocFpServiceStartConcurrentSession),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CryptohomeErrorCode::
            CRYPTOHOME_ERROR_FINGERPRINT_DENIED);
  }

  if (user_.empty()) {
    user_ = obfuscated_username;
    attempts_left_ = kMaxFingerprintRetries;
  }

  return OkStatus<CryptohomeError>();
}

void FingerprintAuthBlockService::Terminate() {
  user_.clear();
  EndAuthSession();
}

void FingerprintAuthBlockService::Capture(
    base::OnceCallback<void(CryptohomeStatus)> on_done) {
  FingerprintManager* fp_manager = fp_manager_getter_.Run();
  if (!fp_manager) {
    CryptohomeStatus status = MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocFpServiceScanCouldNotGetFpManager),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CryptohomeErrorCode::
            CRYPTOHOME_ERROR_FINGERPRINT_ERROR_INTERNAL);
    std::move(on_done).Run(std::move(status));
    return;
  }

  fp_manager->SetAuthScanDoneCallback(
      base::BindOnce(&FingerprintAuthBlockService::CheckFingerprintResult,
                     base::Unretained(this), std::move(on_done)));
}

void FingerprintAuthBlockService::EndAuthSession() {
  FingerprintManager* fp_manager = fp_manager_getter_.Run();
  if (fp_manager) {
    fp_manager->EndAuthSession();
  }
}

}  // namespace cryptohome
