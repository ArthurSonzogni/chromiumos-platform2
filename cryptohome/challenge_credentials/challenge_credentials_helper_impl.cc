// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/challenge_credentials/challenge_credentials_helper_impl.h"

#include <utility>

#include <base/bind.h>
#include <base/check.h>
#include <base/check_op.h>
#include <base/logging.h>
#include <libhwsec/status.h>

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

}  // namespace

ChallengeCredentialsHelperImpl::ChallengeCredentialsHelperImpl(Tpm* tpm)
    : tpm_(tpm) {
  DCHECK(tpm_);
}

ChallengeCredentialsHelperImpl::~ChallengeCredentialsHelperImpl() {
  DCHECK(thread_checker_.CalledOnValidThread());
}

void ChallengeCredentialsHelperImpl::GenerateNew(
    const std::string& account_id,
    const structure::ChallengePublicKeyInfo& public_key_info,
    const std::string& obfuscated_username,
    std::unique_ptr<KeyChallengeService> key_challenge_service,
    GenerateNewCallback callback) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(!callback.is_null());
  CancelRunningOperation();
  key_challenge_service_ = std::move(key_challenge_service);
  operation_ = std::make_unique<ChallengeCredentialsGenerateNewOperation>(
      key_challenge_service_.get(), tpm_, account_id, public_key_info,
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
  CancelRunningOperation();
  key_challenge_service_ = std::move(key_challenge_service);
  operation_ = std::make_unique<ChallengeCredentialsVerifyKeyOperation>(
      key_challenge_service_.get(), tpm_, account_id, public_key_info,
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
      key_challenge_service_.get(), tpm_, account_id, public_key_info,
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
