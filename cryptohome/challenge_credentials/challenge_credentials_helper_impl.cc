// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/challenge_credentials/challenge_credentials_helper_impl.h"

#include <optional>
#include <utility>

#include <base/bind.h>
#include <base/check.h>
#include <base/check_op.h>
#include <base/logging.h>
#include <base/strings/string_util.h>
#include <base/system/sys_info.h>
#include <libhwsec/status.h>
#include <libhwsec/frontend/cryptohome/frontend.h>

#include "cryptohome/challenge_credentials/challenge_credentials_decrypt_operation.h"
#include "cryptohome/challenge_credentials/challenge_credentials_generate_new_operation.h"
#include "cryptohome/challenge_credentials/challenge_credentials_operation.h"
#include "cryptohome/challenge_credentials/challenge_credentials_verify_key_operation.h"
#include "cryptohome/error/location_utils.h"
#include "cryptohome/key_challenge_service.h"

using brillo::Blob;
using cryptohome::error::CryptohomeTPMError;
using cryptohome::error::ErrorAction;
using cryptohome::error::ErrorActionSet;
using hwsec::TPMError;
using hwsec::TPMErrorBase;
using hwsec::TPMRetryAction;
using hwsec_foundation::error::CreateError;
using hwsec_foundation::error::WrapError;
using hwsec_foundation::status::MakeStatus;
using hwsec_foundation::status::OkStatus;
using hwsec_foundation::status::StatusChain;

namespace cryptohome {

namespace {

bool IsOperationFailureTransient(
    const StatusChain<CryptohomeTPMError>& status) {
  TPMRetryAction action = status->ToTPMRetryAction();
  return action == TPMRetryAction::kCommunication ||
         action == TPMRetryAction::kLater;
}

// Returns whether the Chrome OS image is a test one.
bool IsOsTestImage() {
  std::string chromeos_release_track;
  if (!base::SysInfo::GetLsbReleaseValue("CHROMEOS_RELEASE_TRACK",
                                         &chromeos_release_track)) {
    // Fall back to the safer assumption that we're not in a test image.
    return false;
  }
  return base::StartsWith(chromeos_release_track, "test",
                          base::CompareCase::SENSITIVE);
}

}  // namespace

ChallengeCredentialsHelperImpl::ChallengeCredentialsHelperImpl(
    hwsec::CryptohomeFrontend* hwsec)
    : roca_vulnerable_(std::nullopt), hwsec_(hwsec) {
  DCHECK(hwsec_);
}

ChallengeCredentialsHelperImpl::~ChallengeCredentialsHelperImpl() {
  DCHECK(thread_checker_.CalledOnValidThread());
}

TPMStatus ChallengeCredentialsHelperImpl::CheckSrkRocaStatus() {
  // Prepare the TPMStatus for vulnerable case because it will be used in
  // multiple places.
  auto vulnerable_status = MakeStatus<CryptohomeTPMError>(
      CRYPTOHOME_ERR_LOC(kLocChalCredHelperROCAVulnerableInCheckSrkRocaStatus),
      ErrorActionSet({ErrorAction::kTpmUpdateRequired}),
      TPMRetryAction::kNoRetry,
      user_data_auth::CRYPTOHOME_ERROR_TPM_UPDATE_REQUIRED);

  // Have we checked before?
  if (roca_vulnerable_.has_value()) {
    if (roca_vulnerable_.value()) {
      return vulnerable_status;
    }
    return OkStatus<CryptohomeTPMError>();
  }

  // Fail if the security chip is known to be vulnerable and we're not in a test
  // image.
  hwsec::StatusOr<bool> is_srk_roca_vulnerable = hwsec_->IsSrkRocaVulnerable();
  if (!is_srk_roca_vulnerable.ok()) {
    LOG(ERROR) << "Failed to get the hwsec SRK ROCA vulnerable status: "
               << is_srk_roca_vulnerable.status();
    return MakeStatus<CryptohomeTPMError>(
        CRYPTOHOME_ERR_LOC(
            kLocChalCredHelperCantQueryROCAVulnInCheckSrkRocaStatus),
        ErrorActionSet({ErrorAction::kReboot}), TPMRetryAction::kReboot,
        user_data_auth::CRYPTOHOME_ERROR_MOUNT_FATAL);
  }

  if (is_srk_roca_vulnerable.value()) {
    if (!IsOsTestImage()) {
      LOG(ERROR)
          << "Cannot do challenge-response mount: HWSec is ROCA vulnerable";
      roca_vulnerable_ = true;
      return vulnerable_status;
    }
    LOG(WARNING) << "HWSec is ROCA vulnerable; ignoring this for "
                    "challenge-response mount due to running in test image";
  }

  roca_vulnerable_ = false;
  return OkStatus<CryptohomeTPMError>();
}

void ChallengeCredentialsHelperImpl::GenerateNew(
    const std::string& account_id,
    const structure::ChallengePublicKeyInfo& public_key_info,
    const std::string& obfuscated_username,
    std::unique_ptr<KeyChallengeService> key_challenge_service,
    GenerateNewCallback callback) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(!callback.is_null());

  // Check SRK ROCA status.
  TPMStatus status = CheckSrkRocaStatus();
  if (!status.ok()) {
    // We can forward the TPMStatus directly without wrapping because the
    // callback will usually Wrap() the resulting error status anyway.
    std::move(callback).Run(std::move(status));
    return;
  }

  CancelRunningOperation();
  key_challenge_service_ = std::move(key_challenge_service);
  operation_ = std::make_unique<ChallengeCredentialsGenerateNewOperation>(
      key_challenge_service_.get(), hwsec_, account_id, public_key_info,
      obfuscated_username,
      base::BindOnce(&ChallengeCredentialsHelperImpl::OnGenerateNewCompleted,
                     base::Unretained(this), std::move(callback)));
  operation_->Start();
}

void ChallengeCredentialsHelperImpl::Decrypt(
    const std::string& account_id,
    const structure::ChallengePublicKeyInfo& public_key_info,
    const structure::SignatureChallengeInfo& keyset_challenge_info,
    std::unique_ptr<KeyChallengeService> key_challenge_service,
    DecryptCallback callback) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(!callback.is_null());

  // Check SRK ROCA status.
  TPMStatus status = CheckSrkRocaStatus();
  if (!status.ok()) {
    // We can forward the TPMStatus directly without wrapping because the
    // callback will usually Wrap() the resulting error status anyway.
    std::move(callback).Run(std::move(status));
    return;
  }

  CancelRunningOperation();
  key_challenge_service_ = std::move(key_challenge_service);
  StartDecryptOperation(account_id, public_key_info, keyset_challenge_info,
                        1 /* attempt_number */, std::move(callback));
}

void ChallengeCredentialsHelperImpl::VerifyKey(
    const std::string& account_id,
    const structure::ChallengePublicKeyInfo& public_key_info,
    std::unique_ptr<KeyChallengeService> key_challenge_service,
    VerifyKeyCallback callback) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(!callback.is_null());

  // Check SRK ROCA status.
  TPMStatus status = CheckSrkRocaStatus();
  if (!status.ok()) {
    // We can forward the TPMStatus directly without wrapping because the
    // callback will usually Wrap() the resulting error status anyway.
    std::move(callback).Run(std::move(status));
    return;
  }

  CancelRunningOperation();
  key_challenge_service_ = std::move(key_challenge_service);
  operation_ = std::make_unique<ChallengeCredentialsVerifyKeyOperation>(
      key_challenge_service_.get(), hwsec_, account_id, public_key_info,
      base::BindOnce(&ChallengeCredentialsHelperImpl::OnVerifyKeyCompleted,
                     base::Unretained(this), std::move(callback)));
  operation_->Start();
}

void ChallengeCredentialsHelperImpl::StartDecryptOperation(
    const std::string& account_id,
    const structure::ChallengePublicKeyInfo& public_key_info,
    const structure::SignatureChallengeInfo& keyset_challenge_info,
    int attempt_number,
    DecryptCallback callback) {
  DCHECK(!operation_);
  operation_ = std::make_unique<ChallengeCredentialsDecryptOperation>(
      key_challenge_service_.get(), hwsec_, account_id, public_key_info,
      keyset_challenge_info,
      base::BindOnce(&ChallengeCredentialsHelperImpl::OnDecryptCompleted,
                     base::Unretained(this), account_id, public_key_info,
                     keyset_challenge_info, attempt_number,
                     std::move(callback)));
  operation_->Start();
}

void ChallengeCredentialsHelperImpl::CancelRunningOperation() {
  // Destroy the previous Operation before instantiating a new one, to keep the
  // resource usage constrained (for example, there must be only one instance of
  // hwsec::CryptohomeFrontend::ChallengeWithSignatureAndCurrentUser at a time).
  if (operation_) {
    DLOG(INFO) << "Cancelling an old challenge-response credentials operation";
    // Note: kReboot is specified here instead of kRetry because kRetry could
    // trigger upper layer to retry immediately, causing failures again.
    operation_->Abort(MakeStatus<CryptohomeTPMError>(
        CRYPTOHOME_ERR_LOC(kLocChalCredHelperConcurrencyNotAllowed),
        ErrorActionSet({ErrorAction::kReboot}), TPMRetryAction::kReboot));
    operation_.reset();
    // It's illegal for the consumer code to request a new operation in
    // immediate response to completion of a previous one.
    DCHECK(!operation_);
  }
}

void ChallengeCredentialsHelperImpl::OnGenerateNewCompleted(
    GenerateNewCallback original_callback,
    TPMStatusOr<ChallengeCredentialsHelper::GenerateNewOrDecryptResult>
        result) {
  DCHECK(thread_checker_.CalledOnValidThread());
  CancelRunningOperation();
  std::move(original_callback).Run(std::move(result));
}

void ChallengeCredentialsHelperImpl::OnDecryptCompleted(
    const std::string& account_id,
    const structure::ChallengePublicKeyInfo& public_key_info,
    const structure::SignatureChallengeInfo& keyset_challenge_info,
    int attempt_number,
    DecryptCallback original_callback,
    TPMStatusOr<ChallengeCredentialsHelper::GenerateNewOrDecryptResult>
        result) {
  DCHECK(thread_checker_.CalledOnValidThread());
  CancelRunningOperation();
  if (!result.ok() && IsOperationFailureTransient(result.status()) &&
      attempt_number < kRetryAttemptCount) {
    LOG(WARNING) << "Retrying the decryption operation after transient error: "
                 << result.status();
    StartDecryptOperation(account_id, public_key_info, keyset_challenge_info,
                          attempt_number + 1, std::move(original_callback));
  } else {
    if (!result.ok()) {
      LOG(ERROR) << "Decryption completed with error: " << result.status();
    }
    std::move(original_callback).Run(std::move(result));
  }
}

void ChallengeCredentialsHelperImpl::OnVerifyKeyCompleted(
    VerifyKeyCallback original_callback, TPMStatus verify_status) {
  DCHECK(thread_checker_.CalledOnValidThread());
  CancelRunningOperation();
  std::move(original_callback).Run(std::move(verify_status));
}

}  // namespace cryptohome
