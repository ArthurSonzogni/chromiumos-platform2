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
#include <libhwsec/status.h>

#include "cryptohome/challenge_credentials/challenge_credentials_constants.h"
#include "cryptohome/error/location_utils.h"
#include "cryptohome/error/locations.h"
#include "cryptohome/signature_sealing/structures.h"

using brillo::Blob;
using brillo::SecureBlob;
using cryptohome::error::CryptohomeTPMError;
using cryptohome::error::ErrorAction;
using cryptohome::error::ErrorActionSet;
using cryptohome::error::NoErrorAction;
using hwsec::TPMError;
using hwsec::TPMErrorBase;
using hwsec::TPMRetryAction;
using hwsec_foundation::status::MakeStatus;
using hwsec_foundation::status::OkStatus;
using hwsec_foundation::status::StatusChain;

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
  StatusChain<CryptohomeTPMError> status = StartProcessing();
  if (!status.ok()) {
    Resolve(MakeStatus<CryptohomeTPMError>(
                CRYPTOHOME_ERR_LOC(kLocChalCredDecryptCantStartProcessing),
                NoErrorAction())
                .Wrap(std::move(status)),
            nullptr /* passkey */);
    // |this| can be already destroyed at this point.
  }
}

void ChallengeCredentialsDecryptOperation::Abort() {
  DCHECK(thread_checker_.CalledOnValidThread());
  Resolve(MakeStatus<CryptohomeTPMError>(
              CRYPTOHOME_ERR_LOC(kLocChalCredDecryptOperationAborted),
              NoErrorAction(), TPMRetryAction::kNoRetry),
          nullptr /* passkey */);
  // |this| can be already destroyed at this point.
}

StatusChain<CryptohomeTPMError>
ChallengeCredentialsDecryptOperation::StartProcessing() {
  if (!signature_sealing_backend_) {
    return MakeStatus<CryptohomeTPMError>(
        CRYPTOHOME_ERR_LOC(kLocChalCredDecryptNoSignatureSealingBackend),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        TPMRetryAction::kNoRetry);
  }
  if (!public_key_info_.signature_algorithm.size()) {
    return MakeStatus<CryptohomeTPMError>(
        CRYPTOHOME_ERR_LOC(kLocChalCredDecryptNoPubKeySigSize),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        TPMRetryAction::kNoRetry);
  }

  if (public_key_info_.public_key_spki_der !=
      keyset_challenge_info_.public_key_spki_der) {
    return MakeStatus<CryptohomeTPMError>(
        CRYPTOHOME_ERR_LOC(kLocChalCredDecryptSPKIPubKeyMismatch),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        TPMRetryAction::kNoRetry);
  }

  StatusChain<CryptohomeTPMError> status = StartProcessingSalt();
  if (!status.ok()) {
    return MakeStatus<CryptohomeTPMError>(
               CRYPTOHOME_ERR_LOC(kLocChalCredDecryptSaltProcessingFailed),
               NoErrorAction())
        .Wrap(std::move(status));
  }
  // TODO(crbug.com/842791): This is buggy: |this| may be already deleted by
  // that point, in case when the salt's challenge request failed synchronously.
  return StartProcessingSealedSecret();
}

StatusChain<CryptohomeTPMError>
ChallengeCredentialsDecryptOperation::StartProcessingSalt() {
  if (keyset_challenge_info_.salt.empty()) {
    return MakeStatus<CryptohomeTPMError>(
        CRYPTOHOME_ERR_LOC(kLocChalCredDecryptNoSalt),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        TPMRetryAction::kNoRetry);
  }
  if (public_key_info_.public_key_spki_der.empty()) {
    return MakeStatus<CryptohomeTPMError>(
        CRYPTOHOME_ERR_LOC(
            kLocChalCredDecryptNoSPKIPubKeyDERWhileProcessingSalt),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        TPMRetryAction::kNoRetry);
  }
  if (!keyset_challenge_info_.salt_signature_algorithm.has_value()) {
    return MakeStatus<CryptohomeTPMError>(
        CRYPTOHOME_ERR_LOC(kLocChalCredDecryptNoSaltSigAlgoWhileProcessingSalt),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
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
    return MakeStatus<CryptohomeTPMError>(
        CRYPTOHOME_ERR_LOC(kLocChalCredDecryptSaltPrefixIncorrect),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        TPMRetryAction::kNoRetry);
  }
  MakeKeySignatureChallenge(
      account_id_, public_key_info_.public_key_spki_der, salt,
      keyset_challenge_info_.salt_signature_algorithm.value(),
      base::BindOnce(
          &ChallengeCredentialsDecryptOperation::OnSaltChallengeResponse,
          weak_ptr_factory_.GetWeakPtr()));
  return OkStatus<CryptohomeTPMError>();
}

StatusChain<CryptohomeTPMError>
ChallengeCredentialsDecryptOperation::StartProcessingSealedSecret() {
  if (public_key_info_.public_key_spki_der.empty()) {
    return MakeStatus<CryptohomeTPMError>(
        CRYPTOHOME_ERR_LOC(
            kLocChalCredDecryptNoSPKIPubKeyDERWhileProcessingSecret),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
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

  auto status = MakeStatus<CryptohomeTPMError>(
      signature_sealing_backend_->CreateUnsealingSession(
          keyset_challenge_info_.sealed_secret,
          public_key_info_.public_key_spki_der, key_sealing_algorithms, pcr_set,
          delegate_blob_, delegate_secret_, locked_to_single_user_,
          &unsealing_session_));
  if (!status.ok()) {
    return MakeStatus<CryptohomeTPMError>(
               CRYPTOHOME_ERR_LOC(
                   kLocChalCredDecryptCreateUnsealingSessionFailed),
               NoErrorAction())
        .Wrap(std::move(status));
  }
  MakeKeySignatureChallenge(
      account_id_, public_key_info_.public_key_spki_der,
      unsealing_session_->GetChallengeValue(),
      unsealing_session_->GetChallengeAlgorithm(),
      base::BindOnce(
          &ChallengeCredentialsDecryptOperation::OnUnsealingChallengeResponse,
          weak_ptr_factory_.GetWeakPtr()));
  return OkStatus<CryptohomeTPMError>();
}

void ChallengeCredentialsDecryptOperation::OnSaltChallengeResponse(
    std::unique_ptr<Blob> salt_signature) {
  DCHECK(thread_checker_.CalledOnValidThread());
  if (!salt_signature) {
    Resolve(MakeStatus<CryptohomeTPMError>(
                CRYPTOHOME_ERR_LOC(kLocChalCredDecryptSaltResponseNoSignature),
                NoErrorAction(), TPMRetryAction::kNoRetry),
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
    Resolve(
        MakeStatus<CryptohomeTPMError>(
            CRYPTOHOME_ERR_LOC(kLocChalCredDecryptUnsealingResponseNoSignature),
            ErrorActionSet({ErrorAction::kAuth}), TPMRetryAction::kNoRetry),
        nullptr /* passkey */);
    // |this| can be already destroyed at this point.
    return;
  }
  SecureBlob unsealed_secret;
  auto status = MakeStatus<CryptohomeTPMError>(
      unsealing_session_->Unseal(*challenge_signature, &unsealed_secret));
  if (!status.ok()) {
    // TODO(crbug.com/842791): Determine the retry action based on the type of
    // the error.
    Resolve(MakeStatus<CryptohomeTPMError>(
                CRYPTOHOME_ERR_LOC(kLocChalCredDecryptUnsealFailed),
                NoErrorAction())
                .Wrap(std::move(status)),
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
  Resolve(OkStatus<CryptohomeTPMError>(), std::move(passkey));
  // |this| can be already destroyed at this point.
}

void ChallengeCredentialsDecryptOperation::Resolve(
    StatusChain<CryptohomeTPMError> error,
    std::unique_ptr<brillo::SecureBlob> passkey) {
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
