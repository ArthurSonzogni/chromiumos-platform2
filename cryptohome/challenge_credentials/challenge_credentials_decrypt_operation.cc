// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/challenge_credentials/challenge_credentials_decrypt_operation.h"

#include <algorithm>
#include <set>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/check.h>
#include <base/check_op.h>
#include <base/logging.h>

#include "cryptohome/challenge_credentials/challenge_credentials_constants.h"
#include "cryptohome/signature_sealing/structures.h"

using brillo::Blob;
using brillo::SecureBlob;
using hwsec::error::TPMError;
using hwsec::error::TPMErrorBase;
using hwsec::error::TPMRetryAction;
using hwsec_foundation::error::CreateError;
using hwsec_foundation::error::WrapError;

namespace cryptohome {

ChallengeCredentialsDecryptOperation::ChallengeCredentialsDecryptOperation(
    KeyChallengeService* key_challenge_service,
    Tpm* tpm,
    const Blob& delegate_blob,
    const Blob& delegate_secret,
    const std::string& account_id,
    const structure::ChallengePublicKeyInfo& public_key_info,
    const structure::SignatureChallengeInfo& keyset_challenge_info,
    bool locked_to_single_user,
    CompletionCallback completion_callback)
    : ChallengeCredentialsOperation(key_challenge_service),
      tpm_(tpm),
      delegate_blob_(delegate_blob),
      delegate_secret_(delegate_secret),
      account_id_(account_id),
      public_key_info_(public_key_info),
      keyset_challenge_info_(keyset_challenge_info),
      locked_to_single_user_(locked_to_single_user),
      completion_callback_(std::move(completion_callback)),
      signature_sealing_backend_(tpm_->GetSignatureSealingBackend()) {}

ChallengeCredentialsDecryptOperation::~ChallengeCredentialsDecryptOperation() =
    default;

void ChallengeCredentialsDecryptOperation::Start() {
  DCHECK(thread_checker_.CalledOnValidThread());
  if (TPMErrorBase err = StartProcessing()) {
    Resolve(WrapError<TPMError>(std::move(err),
                                "Failed to start the decryption operation"),
            nullptr /* passkey */);
    // |this| can be already destroyed at this point.
  }
}

void ChallengeCredentialsDecryptOperation::Abort() {
  DCHECK(thread_checker_.CalledOnValidThread());
  Resolve(CreateError<TPMError>("aborted", TPMRetryAction::kNoRetry),
          nullptr /* passkey */);
  // |this| can be already destroyed at this point.
}

TPMErrorBase ChallengeCredentialsDecryptOperation::StartProcessing() {
  if (!signature_sealing_backend_) {
    return CreateError<TPMError>("Signature sealing is disabled",
                                 TPMRetryAction::kNoRetry);
  }
  if (!public_key_info_.signature_algorithm.size()) {
    return CreateError<TPMError>(
        "The key does not support any signature algorithm",
        TPMRetryAction::kNoRetry);
  }

  if (public_key_info_.public_key_spki_der !=
      keyset_challenge_info_.public_key_spki_der) {
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
  if (keyset_challenge_info_.salt.empty()) {
    return CreateError<TPMError>("Missing salt", TPMRetryAction::kNoRetry);
  }
  if (public_key_info_.public_key_spki_der.empty()) {
    return CreateError<TPMError>("Missing public key",
                                 TPMRetryAction::kNoRetry);
  }
  const Blob& salt = keyset_challenge_info_.salt;
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
  MakeKeySignatureChallenge(
      account_id_, public_key_info_.public_key_spki_der, salt,
      keyset_challenge_info_.salt_signature_algorithm,
      base::BindOnce(
          &ChallengeCredentialsDecryptOperation::OnSaltChallengeResponse,
          weak_ptr_factory_.GetWeakPtr()));
  return nullptr;
}

TPMErrorBase
ChallengeCredentialsDecryptOperation::StartProcessingSealedSecret() {
  if (public_key_info_.public_key_spki_der.empty()) {
    return CreateError<TPMError>("Missing public key",
                                 TPMRetryAction::kNoRetry);
  }
  const std::vector<structure::ChallengeSignatureAlgorithm>
      key_sealing_algorithms = public_key_info_.signature_algorithm;

  // Get the PCR set from the empty user PCR map.
  std::map<uint32_t, brillo::Blob> pcr_map =
      tpm_->GetPcrMap("", locked_to_single_user_);
  std::set<uint32_t> pcr_set;
  for (const auto& [pcr_index, pcr_value] : pcr_map) {
    pcr_set.insert(pcr_index);
  }

  if (TPMErrorBase err = signature_sealing_backend_->CreateUnsealingSession(
          keyset_challenge_info_.sealed_secret,
          public_key_info_.public_key_spki_der, key_sealing_algorithms, pcr_set,
          delegate_blob_, delegate_secret_, locked_to_single_user_,
          &unsealing_session_)) {
    return WrapError<TPMError>(
        std::move(err), "Failed to start unsealing session for the secret");
  }
  MakeKeySignatureChallenge(
      account_id_, public_key_info_.public_key_spki_der,
      unsealing_session_->GetChallengeValue(),
      unsealing_session_->GetChallengeAlgorithm(),
      base::BindOnce(
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
            nullptr /* passkey */);
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
            nullptr /* passkey */);
    // |this| can be already destroyed at this point.
    return;
  }
  SecureBlob unsealed_secret;
  if (TPMErrorBase err =
          unsealing_session_->Unseal(*challenge_signature, &unsealed_secret)) {
    // TODO(crbug.com/842791): Determine the retry action based on the type of
    // the error.
    Resolve(WrapError<TPMError>(std::move(err), "Failed to unseal the secret"),
            nullptr /* passkey */);
    // |this| can be already destroyed at this point.
    return;
  }
  unsealed_secret_ = std::make_unique<SecureBlob>(unsealed_secret);
  ProceedIfChallengesDone();
}

void ChallengeCredentialsDecryptOperation::ProceedIfChallengesDone() {
  if (!salt_signature_ || !unsealed_secret_)
    return;
  auto passkey = std::make_unique<brillo::SecureBlob>(
      ConstructPasskey(*unsealed_secret_, *salt_signature_));
  Resolve(nullptr, std::move(passkey));
  // |this| can be already destroyed at this point.
}

void ChallengeCredentialsDecryptOperation::Resolve(
    TPMErrorBase error, std::unique_ptr<brillo::SecureBlob> passkey) {
  // Invalidate weak pointers in order to cancel all jobs that are currently
  // waiting, to prevent them from running and consuming resources after our
  // abortion (in case |this| doesn't get destroyed immediately).
  //
  // Note that the already issued challenge requests don't get cancelled, so
  // their responses will be just ignored should they arrive later. The request
  // cancellation is not supported by the challenges IPC API currently, neither
  // it is supported by the API for smart card drivers in Chrome OS.
  weak_ptr_factory_.InvalidateWeakPtrs();
  Complete(&completion_callback_, std::move(error), std::move(passkey));
}

}  // namespace cryptohome
