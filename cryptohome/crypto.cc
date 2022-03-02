// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Contains the implementation of class Crypto

#include "cryptohome/crypto.h"

#include <sys/types.h>
#include <unistd.h>

#include <limits>
#include <map>
#include <utility>

#include <base/check.h>
#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <brillo/secure_blob.h>
#include <crypto/sha2.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>
#include <openssl/sha.h>

#include "cryptohome/attestation.pb.h"
#include "cryptohome/crypto/aes.h"
#include "cryptohome/crypto/hmac.h"
#include "cryptohome/crypto/scrypt.h"
#include "cryptohome/crypto/secure_blob_util.h"
#include "cryptohome/cryptohome_common.h"
#include "cryptohome/cryptohome_keys_manager.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/filesystem_layout.h"
#include "cryptohome/key_objects.h"
#include "cryptohome/le_credential_manager_impl.h"
#include "cryptohome/libscrypt_compat.h"
#include "cryptohome/platform.h"
#include "cryptohome/vault_keyset.h"

using base::FilePath;
using brillo::SecureBlob;
using hwsec::StatusChain;
using hwsec::TPMErrorBase;

namespace cryptohome {

namespace {

// Location where we store the Low Entropy (LE) credential manager related
// state.
const char kSignInHashTreeDir[] = "/home/.shadow/low_entropy_creds";

}  // namespace

Crypto::Crypto(Platform* platform)
    : tpm_(NULL),
      platform_(platform),
      cryptohome_keys_manager_(NULL),
      disable_logging_for_tests_(false) {}

Crypto::~Crypto() {}

bool Crypto::Init(Tpm* tpm, CryptohomeKeysManager* cryptohome_keys_manager) {
  CHECK(cryptohome_keys_manager)
      << "Crypto wanted to use CryptohomeKeysManager but was not provided";
  CHECK(tpm) << "Crypto wanted to use Tpm but was not provided";
  if (tpm_ == NULL) {
    tpm_ = tpm;
  }
  cryptohome_keys_manager_ = cryptohome_keys_manager;
  cryptohome_keys_manager_->Init();
  if (tpm_->GetLECredentialBackend() &&
      tpm_->GetLECredentialBackend()->IsSupported()) {
    le_manager_ = std::make_unique<LECredentialManagerImpl>(
        tpm_->GetLECredentialBackend(), base::FilePath(kSignInHashTreeDir));
  }
  return true;
}

CryptoError Crypto::EnsureTpm(bool reload_key) const {
  CryptoError result = CryptoError::CE_NONE;
  if (tpm_ && cryptohome_keys_manager_) {
    if (reload_key || !cryptohome_keys_manager_->HasAnyCryptohomeKey()) {
      cryptohome_keys_manager_->Init();
    }
  }
  return result;
}

void Crypto::PasswordToPasskey(const char* password,
                               const brillo::SecureBlob& salt,
                               SecureBlob* passkey) {
  CHECK(password);

  std::string ascii_salt = SecureBlobToHex(salt);
  // Convert a raw password to a password hash
  SHA256_CTX sha_context;
  SecureBlob md_value(SHA256_DIGEST_LENGTH);

  SHA256_Init(&sha_context);
  SHA256_Update(&sha_context, ascii_salt.data(), ascii_salt.length());
  SHA256_Update(&sha_context, password, strlen(password));
  SHA256_Final(md_value.data(), &sha_context);

  md_value.resize(SHA256_DIGEST_LENGTH / 2);
  SecureBlob local_passkey(SHA256_DIGEST_LENGTH);
  SecureBlobToHexToBuffer(md_value, local_passkey.data(), local_passkey.size());
  passkey->swap(local_passkey);
}

bool Crypto::NeedsPcrBinding(const uint64_t& label) const {
  DCHECK(le_manager_)
      << "le_manage_ doesn't exist when calling NeedsPcrBinding()";
  return le_manager_->NeedsPcrBinding(label);
}

bool Crypto::ResetLECredential(const VaultKeyset& vk_reset,
                               const VaultKeyset& vk,
                               CryptoError* error) const {
  if (!tpm_) {
    return false;
  }

  // Bail immediately if we don't have a valid LECredentialManager.
  if (!le_manager_) {
    LOG(ERROR) << "Attempting to Reset LECredential on a platform that doesn't "
                  "support LECredential";
    PopulateError(error, CryptoError::CE_LE_NOT_SUPPORTED);
    return false;
  }

  if (!vk_reset.IsLECredential()) {
    LOG(ERROR) << "vk_reset is not an LE credential.";
    PopulateError(error, CryptoError::CE_LE_FLAGS_AND_POLICY_MISMATCH);
    return false;
  }

  SecureBlob local_reset_seed(vk.GetResetSeed().begin(),
                              vk.GetResetSeed().end());
  SecureBlob reset_salt(vk_reset.GetResetSalt().begin(),
                        vk_reset.GetResetSalt().end());
  if (local_reset_seed.empty() || reset_salt.empty()) {
    LOG(ERROR) << "Reset seed/salt is empty, can't reset LE credential.";
    PopulateError(error, CryptoError::CE_OTHER_FATAL);
    return false;
  }

  SecureBlob reset_secret = HmacSha256(reset_salt, local_reset_seed);
  return ResetLeCredentialEx(vk_reset.GetLELabel(), reset_secret, *error);
}

bool Crypto::ResetLeCredentialEx(const uint64_t le_label,
                                 const SecureBlob& reset_secret,
                                 CryptoError& out_error) const {
  int ret = le_manager_->ResetCredential(le_label, reset_secret);
  if (ret != LE_CRED_SUCCESS) {
    PopulateError(&out_error, ret == LE_CRED_ERROR_INVALID_RESET_SECRET
                                  ? CryptoError::CE_LE_INVALID_SECRET
                                  : CryptoError::CE_OTHER_FATAL);
    return false;
  }
  return true;
}

int Crypto::GetWrongAuthAttempts(uint64_t le_label) const {
  DCHECK(le_manager_)
      << "le_manage_ doesn't exist when calling GetWrongAuthAttempts()";
  return le_manager_->GetWrongAuthAttempts(le_label);
}

bool Crypto::RemoveLECredential(uint64_t label) const {
  if (!tpm_) {
    LOG(WARNING) << "No TPM instance for RemoveLECredential.";
    return false;
  }

  // Bail immediately if we don't have a valid LECredentialManager.
  if (!le_manager_) {
    LOG(ERROR) << "No LECredentialManager instance for RemoveLECredential.";
    return false;
  }

  return le_manager_->RemoveCredential(label) == LE_CRED_SUCCESS;
}

bool Crypto::is_cryptohome_key_loaded() const {
  if (tpm_ == NULL || cryptohome_keys_manager_ == NULL) {
    return false;
  }
  return cryptohome_keys_manager_->HasAnyCryptohomeKey();
}

bool Crypto::CanUnsealWithUserAuth() const {
  if (!tpm_)
    return false;
  if (tpm_->GetVersion() != Tpm::TPM_1_2)
    return true;
  if (!tpm_->DelegateCanResetDACounter())
    return false;

  bool is_pcr_bound;
  if (StatusChain<TPMErrorBase> err =
          tpm_->IsDelegateBoundToPcr(&is_pcr_bound)) {
    LOG(ERROR) << "Failed to check the status of delegate bound to pcr: "
               << err;
  } else {
    if (!is_pcr_bound) {
      return true;
    }
  }

#if USE_DOUBLE_EXTEND_PCR_ISSUE
  return false;
#else
  return true;
#endif
}

}  // namespace cryptohome
