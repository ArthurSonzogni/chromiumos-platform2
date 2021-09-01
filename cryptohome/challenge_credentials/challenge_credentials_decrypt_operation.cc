// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/challenge_credentials/challenge_credentials_decrypt_operation.h"

#include <algorithm>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/check.h>
#include <base/check_op.h>
#include <base/logging.h>

#include "cryptohome/challenge_credentials/challenge_credentials_constants.h"
#include "cryptohome/credentials.h"
#include "cryptohome/signature_sealed_data.pb.h"

using brillo::Blob;
using brillo::BlobFromString;
using brillo::SecureBlob;
using hwsec::error::TPMError;
using hwsec::error::TPMErrorBase;
using hwsec::error::TPMRetryAction;
using hwsec_foundation::error::CreateError;
using hwsec_foundation::error::WrapError;

namespace cryptohome {

namespace {

std::vector<ChallengeSignatureAlgorithm> GetSealingAlgorithms(
    const ChallengePublicKeyInfo& public_key_info) {
  std::vector<ChallengeSignatureAlgorithm> sealing_algorithms;
  for (int index = 0; index < public_key_info.signature_algorithm_size();
       ++index) {
    sealing_algorithms.push_back(public_key_info.signature_algorithm(index));
  }
  return sealing_algorithms;
}

}  // namespace

ChallengeCredentialsDecryptOperation::ChallengeCredentialsDecryptOperation(
    KeyChallengeService* key_challenge_service,
    Tpm* tpm,
    const Blob& delegate_blob,
    const Blob& delegate_secret,
    const std::string& account_id,
    const KeyData& key_data,
    const KeysetSignatureChallengeInfo& keyset_challenge_info,
    CompletionCallback completion_callback)
    : ChallengeCredentialsOperation(key_challenge_service),
      tpm_(tpm),
      delegate_blob_(delegate_blob),
      delegate_secret_(delegate_secret),
      account_id_(account_id),
      key_data_(key_data),
      keyset_challenge_info_(keyset_challenge_info),
      completion_callback_(std::move(completion_callback)),
      signature_sealing_backend_(tpm_->GetSignatureSealingBackend()) {
  DCHECK_EQ(key_data.type(), KeyData::KEY_TYPE_CHALLENGE_RESPONSE);
}

ChallengeCredentialsDecryptOperation::~ChallengeCredentialsDecryptOperation() =
    default;

void ChallengeCredentialsDecryptOperation::Start() {
  DCHECK(thread_checker_.CalledOnValidThread());
  if (TPMErrorBase err = StartProcessing()) {
    Resolve(WrapError<TPMError>(std::move(err),
                                "Failed to start the decryption operation"),
            nullptr /* credentials */);
    // |this| can be already destroyed at this point.
  }
}

void ChallengeCredentialsDecryptOperation::Abort() {
  DCHECK(thread_checker_.CalledOnValidThread());
  Resolve(CreateError<TPMError>("aborted", TPMRetryAction::kNoRetry),
          nullptr /* credentials */);
  // |this| can be already destroyed at this point.
}

TPMErrorBase ChallengeCredentialsDecryptOperation::StartProcessing() {
  if (!signature_sealing_backend_) {
    return CreateError<TPMError>("Signature sealing is disabled",
                                 TPMRetryAction::kNoRetry);
  }
  if (!key_data_.challenge_response_key_size()) {
    return CreateError<TPMError>("Missing challenge-response key information",
                                 TPMRetryAction::kNoRetry);
  }
  if (key_data_.challenge_response_key_size() > 1) {
    return CreateError<TPMError>(
        "Using multiple challenge-response keys at once is unsupported",
        TPMRetryAction::kNoRetry);
  }
  public_key_info_ = key_data_.challenge_response_key(0);
  if (!public_key_info_.signature_algorithm_size()) {
    return CreateError<TPMError>(
        "The key does not support any signature algorithm",
        TPMRetryAction::kNoRetry);
  }
  if (public_key_info_.public_key_spki_der() !=
      keyset_challenge_info_.public_key_spki_der()) {
    return CreateError<TPMError>("Wrong public key", TPMRetryAction::kNoRetry);
  }
  if (TPMErrorBase err = StartProcessingSalt()) {
    return WrapError<TPMError>(std::move(err),
                               "Failed to start processing salt");
  }
  // TODO(crbug.com/842791): This is buggy: |this| may be already deleted by
  // that point, in case when the salt's challenge request failed synchronously.
  return StartProcessingSealedSecret();
}

TPMErrorBase ChallengeCredentialsDecryptOperation::StartProcessingSalt() {
  if (!keyset_challenge_info_.has_salt()) {
    return CreateError<TPMError>("Missing salt", TPMRetryAction::kNoRetry);
  }
  const Blob salt = BlobFromString(keyset_challenge_info_.salt());
  // IMPORTANT: Verify that the salt is correctly prefixed. See the comment on
  // GetChallengeCredentialsSaltConstantPrefix() for details. Note also that, as
  // an extra validation, we require the salt to contain at least one extra byte
  // after the prefix.
  const Blob& salt_constant_prefix =
      GetChallengeCredentialsSaltConstantPrefix();
  if (salt.size() <= salt_constant_prefix.size() ||
      !std::equal(salt_constant_prefix.begin(), salt_constant_prefix.end(),
                  salt.begin())) {
    return CreateError<TPMError>("Bad salt: not correctly prefixed",
                                 TPMRetryAction::kNoRetry);
  }
  if (!keyset_challenge_info_.has_salt_signature_algorithm()) {
    return CreateError<TPMError>("Missing signature algorithm for salt",
                                 TPMRetryAction::kNoRetry);
  }
  MakeKeySignatureChallenge(
      account_id_, BlobFromString(public_key_info_.public_key_spki_der()), salt,
      keyset_challenge_info_.salt_signature_algorithm(),
      base::Bind(&ChallengeCredentialsDecryptOperation::OnSaltChallengeResponse,
                 weak_ptr_factory_.GetWeakPtr()));
  return nullptr;
}

TPMErrorBase
ChallengeCredentialsDecryptOperation::StartProcessingSealedSecret() {
  if (!keyset_challenge_info_.has_sealed_secret()) {
    return CreateError<TPMError>("Missing sealed secret",
                                 TPMRetryAction::kNoRetry);
  }
  const std::vector<ChallengeSignatureAlgorithm> key_sealing_algorithms =
      GetSealingAlgorithms(public_key_info_);
  if (TPMErrorBase err = signature_sealing_backend_->CreateUnsealingSession(
          keyset_challenge_info_.sealed_secret(),
          BlobFromString(public_key_info_.public_key_spki_der()),
          key_sealing_algorithms, delegate_blob_, delegate_secret_,
          &unsealing_session_)) {
    return WrapError<TPMError>(
        std::move(err), "Failed to start unsealing session for the secret");
  }
  MakeKeySignatureChallenge(
      account_id_, BlobFromString(public_key_info_.public_key_spki_der()),
      unsealing_session_->GetChallengeValue(),
      unsealing_session_->GetChallengeAlgorithm(),
      base::Bind(
          &ChallengeCredentialsDecryptOperation::OnUnsealingChallengeResponse,
          weak_ptr_factory_.GetWeakPtr()));
  return nullptr;
}

void ChallengeCredentialsDecryptOperation::OnSaltChallengeResponse(
    std::unique_ptr<Blob> salt_signature) {
  DCHECK(thread_checker_.CalledOnValidThread());
  if (!salt_signature) {
    Resolve(CreateError<TPMError>("Salt signature challenge failed",
                                  TPMRetryAction::kNoRetry),
            nullptr /* credentials */);
    // |this| can be already destroyed at this point.
    return;
  }
  salt_signature_ = std::move(salt_signature);
  ProceedIfChallengesDone();
}

void ChallengeCredentialsDecryptOperation::OnUnsealingChallengeResponse(
    std::unique_ptr<Blob> challenge_signature) {
  DCHECK(thread_checker_.CalledOnValidThread());
  if (!challenge_signature) {
    Resolve(CreateError<TPMError>("Unsealing signature challenge failed",
                                  TPMRetryAction::kNoRetry),
            nullptr /* credentials */);
    // |this| can be already destroyed at this point.
    return;
  }
  SecureBlob unsealed_secret;
  if (TPMErrorBase err =
          unsealing_session_->Unseal(*challenge_signature, &unsealed_secret)) {
    // TODO(crbug.com/842791): Determine the retry action based on the type of
    // the error.
    Resolve(WrapError<TPMError>(std::move(err), "Failed to unseal the secret"),
            nullptr /* credentials */);
    // |this| can be already destroyed at this point.
    return;
  }
  unsealed_secret_ = std::make_unique<SecureBlob>(unsealed_secret);
  ProceedIfChallengesDone();
}

void ChallengeCredentialsDecryptOperation::ProceedIfChallengesDone() {
  if (!salt_signature_ || !unsealed_secret_)
    return;
  auto credentials = std::make_unique<Credentials>(
      account_id_, ConstructPasskey(*unsealed_secret_, *salt_signature_));
  credentials->set_key_data(key_data_);
  Resolve(nullptr, std::move(credentials));
  // |this| can be already destroyed at this point.
}

void ChallengeCredentialsDecryptOperation::Resolve(
    TPMErrorBase error, std::unique_ptr<Credentials> credentials) {
  // Invalidate weak pointers in order to cancel all jobs that are currently
  // waiting, to prevent them from running and consuming resources after our
  // abortion (in case |this| doesn't get destroyed immediately).
  //
  // Note that the already issued challenge requests don't get cancelled, so
  // their responses will be just ignored should they arrive later. The request
  // cancellation is not supported by the challenges IPC API currently, neither
  // it is supported by the API for smart card drivers in Chrome OS.
  weak_ptr_factory_.InvalidateWeakPtrs();
  Complete(&completion_callback_, std::move(error), std::move(credentials));
}

}  // namespace cryptohome
