// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include "base/bind.h"
#include "cryptohome/auth_blocks/fp_service.h"
#include "cryptohome/error/cryptohome_error.h"
#include "cryptohome/error/location_utils.h"
#include "cryptohome/error/locations.h"
#include "cryptohome/fingerprint_manager.h"

namespace cryptohome {
namespace {

using cryptohome::error::CryptohomeError;
using cryptohome::error::ErrorAction;
using cryptohome::error::ErrorActionSet;
using hwsec_foundation::status::MakeStatus;
using hwsec_foundation::status::OkStatus;

// Check the result of a given fingerprint scan status and then
// forward it to the given |on_done| callback. This function is
// designed to be used as a callback with FingerprintManager.
void CheckFingerprintResult(base::OnceCallback<void(CryptohomeStatus)> on_done,
                            FingerprintScanStatus status) {
  CryptohomeStatus cryptohome_status;
  switch (status) {
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

}  // namespace

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
  // Try to get a pointer to the fingerprint manager. If this fails then just
  // execute the on done callback immediately with a failure.
  FingerprintManager* fp_manager = fp_manager_getter_.Run();
  if (!fp_manager) {
    CryptohomeStatus status = MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocFpServiceVerifyCouldNotGetFpManager),
        ErrorActionSet({ErrorAction::kRetry}),
        user_data_auth::CryptohomeErrorCode::
            CRYPTOHOME_ERROR_ATTESTATION_NOT_READY);
    std::move(on_done).Run(std::move(status));
    return;
  }

  // Set up a callback with the manager to check the result and forward it to
  // the on_done callback.
  fp_manager->SetAuthScanDoneCallback(
      base::BindOnce(&CheckFingerprintResult, std::move(on_done)));
}

}  // namespace cryptohome
