// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/tpm_auth_block_utils.h"

#include <string>

#include <base/logging.h>
#include <brillo/secure_blob.h>

#include "cryptohome/crypto_error.h"
#include "cryptohome/cryptohome_key_loader.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/tpm.h"
#include "cryptohome/vault_keyset.pb.h"

namespace cryptohome {

TpmAuthBlockUtils::TpmAuthBlockUtils(Tpm* tpm,
                                     CryptohomeKeyLoader* cryptohome_key_loader)
    : tpm_(tpm), cryptohome_key_loader_(cryptohome_key_loader) {}

// static
CryptoError TpmAuthBlockUtils::TpmErrorToCrypto(
    Tpm::TpmRetryAction retry_action) {
  switch (retry_action) {
    case Tpm::kTpmRetryFatal:
      // All errors mapped here will cause re-creating the cryptohome if
      // they occur when decrypting the keyset.
      return CryptoError::CE_TPM_FATAL;
    case Tpm::kTpmRetryCommFailure:
    case Tpm::kTpmRetryInvalidHandle:
    case Tpm::kTpmRetryLoadFail:
    case Tpm::kTpmRetryLater:
      return CryptoError::CE_TPM_COMM_ERROR;
    case Tpm::kTpmRetryDefendLock:
      return CryptoError::CE_TPM_DEFEND_LOCK;
    case Tpm::kTpmRetryReboot:
      return CryptoError::CE_TPM_REBOOT;
    default:
      // TODO(chromium:709646): kTpmRetryFailNoRetry maps here now. Find
      // a better corresponding CryptoError.
      return CryptoError::CE_NONE;
  }
}

CryptoError TpmAuthBlockUtils::TPMErrorToCrypto(
    const hwsec::error::TPMErrorBase& err) {
  hwsec::error::TPMRetryAction action = err->ToTPMRetryAction();
  switch (action) {
    case hwsec::error::TPMRetryAction::kCommunication:
    case hwsec::error::TPMRetryAction::kLater:
      return CryptoError::CE_TPM_COMM_ERROR;
    case hwsec::error::TPMRetryAction::kDefend:
      return CryptoError::CE_TPM_DEFEND_LOCK;
    case hwsec::error::TPMRetryAction::kReboot:
      return CryptoError::CE_TPM_REBOOT;
    default:
      // TODO(chromium:709646): kNoRetry maps here now. Find
      // a better corresponding CryptoError.
      return CryptoError::CE_TPM_CRYPTO;
  }
}

// static
bool TpmAuthBlockUtils::TpmErrorIsRetriable(Tpm::TpmRetryAction retry_action) {
  return retry_action == Tpm::kTpmRetryLoadFail ||
         retry_action == Tpm::kTpmRetryInvalidHandle ||
         retry_action == Tpm::kTpmRetryCommFailure;
}

bool TpmAuthBlockUtils::TPMErrorIsRetriable(
    const hwsec::error::TPMErrorBase& err) {
  hwsec::error::TPMRetryAction action = err->ToTPMRetryAction();
  return action == hwsec::error::TPMRetryAction::kLater ||
         action == hwsec::error::TPMRetryAction::kCommunication;
}

bool TpmAuthBlockUtils::IsTPMPubkeyHash(const brillo::SecureBlob& hash,
                                        CryptoError* error) const {
  brillo::SecureBlob pub_key_hash;
  Tpm::TpmRetryAction retry_action = tpm_->GetPublicKeyHash(
      cryptohome_key_loader_->GetCryptohomeKey(), &pub_key_hash);
  if (retry_action == Tpm::kTpmRetryLoadFail ||
      retry_action == Tpm::kTpmRetryInvalidHandle) {
    if (!cryptohome_key_loader_->ReloadCryptohomeKey()) {
      LOG(ERROR) << "Unable to reload key.";
      retry_action = Tpm::kTpmRetryFailNoRetry;
    } else {
      retry_action = tpm_->GetPublicKeyHash(
          cryptohome_key_loader_->GetCryptohomeKey(), &pub_key_hash);
    }
  }
  if (retry_action != Tpm::kTpmRetryNone) {
    LOG(ERROR) << "Unable to get the cryptohome public key from the TPM.";
    ReportCryptohomeError(kCannotReadTpmPublicKey);
    PopulateError(error, TpmErrorToCrypto(retry_action));
    return false;
  }
  if ((hash.size() != pub_key_hash.size()) ||
      (brillo::SecureMemcmp(hash.data(), pub_key_hash.data(),
                            pub_key_hash.size()))) {
    PopulateError(error, CryptoError::CE_TPM_FATAL);
    return false;
  }
  return true;
}

bool TpmAuthBlockUtils::CheckTPMReadiness(
    bool has_tpm_key,
    bool has_tpm_public_key_hash,
    const brillo::SecureBlob& tpm_public_key_hash,
    CryptoError* error) {
  if (!has_tpm_key) {
    LOG(ERROR) << "Decrypting with TPM, but no TPM key present.";
    ReportCryptohomeError(kDecryptAttemptButTpmKeyMissing);
    PopulateError(error, CryptoError::CE_TPM_FATAL);
    return false;
  }

  // If the TPM is enabled but not owned, and the keyset is TPM wrapped, then
  // it means the TPM has been cleared since the last login, and is not
  // re-owned.  In this case, the SRK is cleared and we cannot recover the
  // keyset.
  if (tpm_->IsEnabled() && !tpm_->IsOwned()) {
    LOG(ERROR) << "Fatal error--the TPM is enabled but not owned, and this "
               << "keyset was wrapped by the TPM.  It is impossible to "
               << "recover this keyset.";
    ReportCryptohomeError(kDecryptAttemptButTpmNotOwned);
    PopulateError(error, CryptoError::CE_TPM_FATAL);
    return false;
  }

  if (!cryptohome_key_loader_->HasCryptohomeKey()) {
    cryptohome_key_loader_->Init();
  }

  if (!cryptohome_key_loader_->HasCryptohomeKey()) {
    LOG(ERROR) << "Vault keyset is wrapped by the TPM, but the TPM is "
               << "unavailable.";
    ReportCryptohomeError(kDecryptAttemptButTpmNotAvailable);
    PopulateError(error, CryptoError::CE_TPM_COMM_ERROR);
    return false;
  }

  // This is a validity check that the keys still match.
  if (has_tpm_public_key_hash) {
    if (!IsTPMPubkeyHash(tpm_public_key_hash, error)) {
      LOG(ERROR) << "TPM public key hash mismatch.";
      ReportCryptohomeError(kDecryptAttemptButTpmKeyMismatch);
      return false;
    }
  }

  return true;
}

}  // namespace cryptohome
