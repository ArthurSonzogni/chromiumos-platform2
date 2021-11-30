// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/challenge_credentials/challenge_credentials_helper_impl.h"

#include <utility>

#include <base/bind.h>
#include <base/check.h>
#include <base/check_op.h>
#include <base/logging.h>

#include "cryptohome/challenge_credentials/challenge_credentials_decrypt_operation.h"
#include "cryptohome/challenge_credentials/challenge_credentials_generate_new_operation.h"
#include "cryptohome/challenge_credentials/challenge_credentials_operation.h"
#include "cryptohome/challenge_credentials/challenge_credentials_verify_key_operation.h"
#include "cryptohome/key_challenge_service.h"
#include "cryptohome/signature_sealing_backend.h"

using brillo::Blob;
using hwsec::StatusChain;
using hwsec::TPMError;
using hwsec::TPMErrorBase;
using hwsec::TPMRetryAction;
using hwsec_foundation::error::CreateError;
using hwsec_foundation::error::WrapError;

namespace cryptohome {

namespace {

bool IsOperationFailureTransient(const StatusChain<TPMErrorBase>& error) {
  TPMRetryAction action = error->ToTPMRetryAction();
  return action == TPMRetryAction::kCommunication ||
         action == TPMRetryAction::kLater;
}

}  // namespace

ChallengeCredentialsHelperImpl::ChallengeCredentialsHelperImpl(
    Tpm* tpm, const Blob& delegate_blob, const Blob& delegate_secret)
    : tpm_(tpm),
      delegate_blob_(delegate_blob),
      delegate_secret_(delegate_secret) {
  DCHECK(tpm_);
}

ChallengeCredentialsHelperImpl::~ChallengeCredentialsHelperImpl() {
  DCHECK(thread_checker_.CalledOnValidThread());
}

void ChallengeCredentialsHelperImpl::GenerateNew(
    const std::string& account_id,
    const structure::ChallengePublicKeyInfo& public_key_info,
    const std::map<uint32_t, brillo::Blob>& default_pcr_map,
    const std::map<uint32_t, brillo::Blob>& extended_pcr_map,
    std::unique_ptr<KeyChallengeService> key_challenge_service,
    GenerateNewCallback callback) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(!callback.is_null());
  CancelRunningOperation();
  key_challenge_service_ = std::move(key_challenge_service);
  operation_ = std::make_unique<ChallengeCredentialsGenerateNewOperation>(
      key_challenge_service_.get(), tpm_, delegate_blob_, delegate_secret_,
      account_id, public_key_info, default_pcr_map, extended_pcr_map,
      base::BindOnce(&ChallengeCredentialsHelperImpl::OnGenerateNewCompleted,
                     base::Unretained(this), std::move(callback)));
  operation_->Start();
}

void ChallengeCredentialsHelperImpl::Decrypt(
    const std::string& account_id,
    const structure::ChallengePublicKeyInfo& public_key_info,
    const structure::SignatureChallengeInfo& keyset_challenge_info,
    bool locked_to_single_user,
    std::unique_ptr<KeyChallengeService> key_challenge_service,
    DecryptCallback callback) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(!callback.is_null());
  CancelRunningOperation();
  key_challenge_service_ = std::move(key_challenge_service);
  StartDecryptOperation(account_id, public_key_info, keyset_challenge_info,
                        locked_to_single_user, 1 /* attempt_number */,
                        std::move(callback));
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
    bool locked_to_single_user,
    int attempt_number,
    DecryptCallback callback) {
  DCHECK(!operation_);
  operation_ = std::make_unique<ChallengeCredentialsDecryptOperation>(
      key_challenge_service_.get(), tpm_, delegate_blob_, delegate_secret_,
      account_id, public_key_info, keyset_challenge_info, locked_to_single_user,
      base::BindOnce(&ChallengeCredentialsHelperImpl::OnDecryptCompleted,
                     base::Unretained(this), account_id, public_key_info,
                     keyset_challenge_info, locked_to_single_user,
                     attempt_number, std::move(callback)));
  operation_->Start();
}

void ChallengeCredentialsHelperImpl::CancelRunningOperation() {
  // Destroy the previous Operation before instantiating a new one, to keep the
  // resource usage constrained (for example, there must be only one instance of
  // SignatureSealingBackend::UnsealingSession at a time).
  if (operation_) {
    DLOG(INFO) << "Cancelling an old challenge-response credentials operation";
    operation_->Abort();
    operation_.reset();
    // It's illegal for the consumer code to request a new operation in
    // immediate response to completion of a previous one.
    DCHECK(!operation_);
  }
}

void ChallengeCredentialsHelperImpl::OnGenerateNewCompleted(
    GenerateNewCallback original_callback,
    std::unique_ptr<structure::SignatureChallengeInfo> signature_challenge_info,
    std::unique_ptr<brillo::SecureBlob> passkey) {
  DCHECK(thread_checker_.CalledOnValidThread());
  CancelRunningOperation();
  std::move(original_callback)
      .Run(std::move(signature_challenge_info), std::move(passkey));
}

void ChallengeCredentialsHelperImpl::OnDecryptCompleted(
    const std::string& account_id,
    const structure::ChallengePublicKeyInfo& public_key_info,
    const structure::SignatureChallengeInfo& keyset_challenge_info,
    bool locked_to_single_user,
    int attempt_number,
    DecryptCallback original_callback,
    StatusChain<TPMErrorBase> error,
    std::unique_ptr<brillo::SecureBlob> passkey) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK_EQ(passkey == nullptr, error != nullptr);
  CancelRunningOperation();
  if (error && IsOperationFailureTransient(error) &&
      attempt_number < kRetryAttemptCount) {
    LOG(WARNING) << "Retrying the decryption operation after transient error: "
                 << error;
    StartDecryptOperation(account_id, public_key_info, keyset_challenge_info,
                          locked_to_single_user, attempt_number + 1,
                          std::move(original_callback));
  } else {
    if (error) {
      LOG(ERROR) << "Decryption completed with error: " << error;
    }
    std::move(original_callback).Run(std::move(passkey));
  }
}

void ChallengeCredentialsHelperImpl::OnVerifyKeyCompleted(
    VerifyKeyCallback original_callback, bool is_key_valid) {
  DCHECK(thread_checker_.CalledOnValidThread());
  CancelRunningOperation();
  std::move(original_callback).Run(is_key_valid);
}

}  // namespace cryptohome
