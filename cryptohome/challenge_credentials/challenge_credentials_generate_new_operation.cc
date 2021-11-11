// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/challenge_credentials/challenge_credentials_generate_new_operation.h"

#include <utility>

#include <base/bind.h>
#include <base/check.h>
#include <base/check_op.h>
#include <base/logging.h>
#include <base/optional.h>

#include "cryptohome/challenge_credentials/challenge_credentials_constants.h"
#include "cryptohome/tpm.h"

using brillo::Blob;
using brillo::CombineBlobs;
using brillo::SecureBlob;
using hwsec::error::TPMErrorBase;

namespace cryptohome {

namespace {

// Returns the signature algorithm that should be used for signing salt from the
// set of algorithms supported by the given key. Returns nullopt when no
// suitable algorithm was found.
base::Optional<structure::ChallengeSignatureAlgorithm>
ChooseSaltSignatureAlgorithm(
    const structure::ChallengePublicKeyInfo& public_key_info) {
  DCHECK(public_key_info.signature_algorithm.size());
  base::Optional<structure::ChallengeSignatureAlgorithm>
      currently_chosen_algorithm;
  // Respect the input's algorithm prioritization, with the exception of
  // considering SHA-1 as the least preferred option.
  for (auto algo : public_key_info.signature_algorithm) {
    currently_chosen_algorithm = algo;
    if (*currently_chosen_algorithm !=
        structure::ChallengeSignatureAlgorithm::kRsassaPkcs1V15Sha1)
      break;
  }
  return currently_chosen_algorithm;
}

}  // namespace

ChallengeCredentialsGenerateNewOperation::
    ChallengeCredentialsGenerateNewOperation(
        KeyChallengeService* key_challenge_service,
        Tpm* tpm,
        const brillo::Blob& delegate_blob,
        const brillo::Blob& delegate_secret,
        const std::string& account_id,
        const structure::ChallengePublicKeyInfo& public_key_info,
        const std::vector<std::map<uint32_t, brillo::Blob>>& pcr_restrictions,
        CompletionCallback completion_callback)
    : ChallengeCredentialsOperation(key_challenge_service),
      tpm_(tpm),
      delegate_blob_(delegate_blob),
      delegate_secret_(delegate_secret),
      account_id_(account_id),
      public_key_info_(public_key_info),
      pcr_restrictions_(pcr_restrictions),
      completion_callback_(std::move(completion_callback)),
      signature_sealing_backend_(tpm_->GetSignatureSealingBackend()) {}

ChallengeCredentialsGenerateNewOperation::
    ~ChallengeCredentialsGenerateNewOperation() = default;

void ChallengeCredentialsGenerateNewOperation::Start() {
  DCHECK(thread_checker_.CalledOnValidThread());
  if (!StartProcessing()) {
    LOG(ERROR) << "Failed to start the generation operation";
    Abort();
    // |this| can be already destroyed at this point.
  }
}

void ChallengeCredentialsGenerateNewOperation::Abort() {
  DCHECK(thread_checker_.CalledOnValidThread());
  // Invalidate weak pointers in order to cancel all jobs that are currently
  // waiting, to prevent them from running and consuming resources after our
  // abortion (in case |this| doesn't get destroyed immediately).
  //
  // Note that the already issued challenge requests don't get cancelled, so
  // their responses will be just ignored should they arrive later. The request
  // cancellation is not supported by the challenges IPC API currently, neither
  // it is supported by the API for smart card drivers in Chrome OS.
  weak_ptr_factory_.InvalidateWeakPtrs();
  Complete(&completion_callback_, nullptr /* signature_challenge_info */,
           nullptr /* passkey */);
  // |this| can be already destroyed at this point.
}

bool ChallengeCredentialsGenerateNewOperation::StartProcessing() {
  if (!signature_sealing_backend_) {
    LOG(ERROR) << "Signature sealing is disabled";
    return false;
  }
  if (!public_key_info_.signature_algorithm.size()) {
    LOG(ERROR) << "The key does not support any signature algorithm";
    return false;
  }
  if (!GenerateSalt() || !StartGeneratingSaltSignature())
    return false;
  // TODO(crbug.com/842791): This is buggy: |this| may be already deleted by
  // that point, in case when the salt's challenge request failed synchronously.
  if (!CreateTpmProtectedSecret())
    return false;
  ProceedIfComputationsDone();
  return true;
}

bool ChallengeCredentialsGenerateNewOperation::GenerateSalt() {
  Blob salt_random_bytes;
  if (TPMErrorBase err = tpm_->GetRandomDataBlob(
          kChallengeCredentialsSaltRandomByteCount, &salt_random_bytes)) {
    LOG(ERROR) << "Failed to generate random bytes for the salt: " << *err;
    return false;
  }
  DCHECK_EQ(kChallengeCredentialsSaltRandomByteCount, salt_random_bytes.size());
  // IMPORTANT: Make sure the salt is prefixed with a constant. See the comment
  // on GetChallengeCredentialsSaltConstantPrefix() for details.
  salt_ = CombineBlobs(
      {GetChallengeCredentialsSaltConstantPrefix(), salt_random_bytes});
  return true;
}

bool ChallengeCredentialsGenerateNewOperation::StartGeneratingSaltSignature() {
  DCHECK(!salt_.empty());
  base::Optional<structure::ChallengeSignatureAlgorithm>
      chosen_salt_signature_algorithm =
          ChooseSaltSignatureAlgorithm(public_key_info_);
  if (!chosen_salt_signature_algorithm) {
    LOG(ERROR) << "Failed to choose salt signature algorithm";
    return false;
  }
  salt_signature_algorithm_ = *chosen_salt_signature_algorithm;
  MakeKeySignatureChallenge(
      account_id_, public_key_info_.public_key_spki_der, salt_,
      salt_signature_algorithm_,
      base::BindOnce(
          &ChallengeCredentialsGenerateNewOperation::OnSaltChallengeResponse,
          weak_ptr_factory_.GetWeakPtr()));
  return true;
}

bool ChallengeCredentialsGenerateNewOperation::CreateTpmProtectedSecret() {
  SecureBlob local_tpm_protected_secret_value;
  if (TPMErrorBase err = signature_sealing_backend_->CreateSealedSecret(
          public_key_info_.public_key_spki_der,
          public_key_info_.signature_algorithm, pcr_restrictions_,
          delegate_blob_, delegate_secret_, &local_tpm_protected_secret_value,
          &tpm_sealed_secret_data_)) {
    LOG(ERROR) << "Failed to create TPM-protected secret: " << *err;
    return false;
  }
  DCHECK(local_tpm_protected_secret_value.size());
  tpm_protected_secret_value_ =
      std::make_unique<SecureBlob>(std::move(local_tpm_protected_secret_value));
  return true;
}

void ChallengeCredentialsGenerateNewOperation::OnSaltChallengeResponse(
    std::unique_ptr<Blob> salt_signature) {
  DCHECK(thread_checker_.CalledOnValidThread());
  if (!salt_signature) {
    LOG(ERROR) << "Salt signature challenge failed";
    Abort();
    // |this| can be already destroyed at this point.
    return;
  }
  salt_signature_ = std::move(salt_signature);
  ProceedIfComputationsDone();
}

void ChallengeCredentialsGenerateNewOperation::ProceedIfComputationsDone() {
  if (!salt_signature_ || !tpm_protected_secret_value_)
    return;

  auto signature_challenge_info =
      std::make_unique<structure::SignatureChallengeInfo>(
          ConstructKeysetSignatureChallengeInfo());

  auto passkey = std::make_unique<brillo::SecureBlob>(
      ConstructPasskey(*tpm_protected_secret_value_, *salt_signature_));
  Complete(&completion_callback_, std::move(signature_challenge_info),
           std::move(passkey));
  // |this| can be already destroyed at this point.
}

structure::SignatureChallengeInfo ChallengeCredentialsGenerateNewOperation::
    ConstructKeysetSignatureChallengeInfo() const {
  structure::SignatureChallengeInfo keyset_signature_challenge_info;
  keyset_signature_challenge_info.public_key_spki_der =
      public_key_info_.public_key_spki_der;
  keyset_signature_challenge_info.sealed_secret = tpm_sealed_secret_data_;
  keyset_signature_challenge_info.salt = salt_;
  keyset_signature_challenge_info.salt_signature_algorithm =
      salt_signature_algorithm_;
  return keyset_signature_challenge_info;
}

}  // namespace cryptohome
