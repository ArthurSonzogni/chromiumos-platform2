// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_blocks/tpm_auth_block_utils.h"

#include <string>

#include <base/logging.h>
#include <brillo/secure_blob.h>

#include "cryptohome/crypto_error.h"
#include "cryptohome/cryptohome_key_loader.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/tpm.h"
#include "cryptohome/vault_keyset.pb.h"

using hwsec::StatusChain;
using hwsec::TPMErrorBase;

namespace cryptohome {

TpmAuthBlockUtils::TpmAuthBlockUtils(Tpm* tpm,
                                     CryptohomeKeyLoader* cryptohome_key_loader)
    : tpm_(tpm), cryptohome_key_loader_(cryptohome_key_loader) {}

CryptoError TpmAuthBlockUtils::TPMErrorToCrypto(
    const StatusChain<hwsec::TPMErrorBase>& err) {
  hwsec::TPMRetryAction action = err->ToTPMRetryAction();
  switch (action) {
    case hwsec::TPMRetryAction::kCommunication:
    case hwsec::TPMRetryAction::kLater:
      return CryptoError::CE_TPM_COMM_ERROR;
    case hwsec::TPMRetryAction::kDefend:
      return CryptoError::CE_TPM_DEFEND_LOCK;
    case hwsec::TPMRetryAction::kReboot:
      return CryptoError::CE_TPM_REBOOT;
    default:
      // TODO(chromium:709646): kNoRetry maps here now. Find
      // a better corresponding CryptoError.
      return CryptoError::CE_TPM_CRYPTO;
  }
}

bool TpmAuthBlockUtils::TPMErrorIsRetriable(
    const StatusChain<hwsec::TPMErrorBase>& err) {
  hwsec::TPMRetryAction action = err->ToTPMRetryAction();
  return action == hwsec::TPMRetryAction::kLater ||
         action == hwsec::TPMRetryAction::kCommunication;
}

CryptoError TpmAuthBlockUtils::IsTPMPubkeyHash(
    const brillo::SecureBlob& hash) const {
  brillo::SecureBlob pub_key_hash;
  if (StatusChain<TPMErrorBase> err = tpm_->GetPublicKeyHash(
          cryptohome_key_loader_->GetCryptohomeKey(), &pub_key_hash)) {
    if (TPMErrorIsRetriable(err)) {
      if (!cryptohome_key_loader_->ReloadCryptohomeKey()) {
        LOG(ERROR) << "Unable to reload key.";
        ReportCryptohomeError(kCannotReadTpmPublicKey);
        return CryptoError::CE_NO_PUBLIC_KEY_HASH;
      } else {
        err = tpm_->GetPublicKeyHash(cryptohome_key_loader_->GetCryptohomeKey(),
                                     &pub_key_hash);
      }
    }
    if (err) {
      LOG(ERROR) << "Unable to get the cryptohome public key from the TPM: "
                 << err;
      ReportCryptohomeError(kCannotReadTpmPublicKey);
      return TPMErrorToCrypto(err);
    }
  }

  if ((hash.size() != pub_key_hash.size()) ||
      (brillo::SecureMemcmp(hash.data(), pub_key_hash.data(),
                            pub_key_hash.size()))) {
    return CryptoError::CE_TPM_FATAL;
  }
  return CryptoError::CE_NONE;
}

CryptoError TpmAuthBlockUtils::CheckTPMReadiness(
    bool has_tpm_key,
    bool has_tpm_public_key_hash,
    const brillo::SecureBlob& tpm_public_key_hash) {
  if (!has_tpm_key) {
    LOG(ERROR) << "Decrypting with TPM, but no TPM key present.";
    ReportCryptohomeError(kDecryptAttemptButTpmKeyMissing);
    return CryptoError::CE_TPM_FATAL;
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
    return CryptoError::CE_TPM_FATAL;
  }

  if (!cryptohome_key_loader_->HasCryptohomeKey()) {
    cryptohome_key_loader_->Init();
  }

  if (!cryptohome_key_loader_->HasCryptohomeKey()) {
    LOG(ERROR) << "Vault keyset is wrapped by the TPM, but the TPM is "
               << "unavailable.";
    ReportCryptohomeError(kDecryptAttemptButTpmNotAvailable);
    return CryptoError::CE_TPM_COMM_ERROR;
  }

  // This is a validity check that the keys still match.
  if (has_tpm_public_key_hash) {
    CryptoError error = IsTPMPubkeyHash(tpm_public_key_hash);
    if (error != CryptoError::CE_NONE) {
      LOG(ERROR) << "TPM public key hash mismatch.";
      ReportCryptohomeError(kDecryptAttemptButTpmKeyMismatch);
      return error;
    }
  }

  return CryptoError::CE_NONE;
}

}  // namespace cryptohome
