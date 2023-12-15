// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Contains the implementation of class Crypto

#include "cryptohome/crypto.h"

#include <sys/types.h>
#include <unistd.h>

#include <cstdint>
#include <limits>
#include <map>
#include <utility>

#include <base/check.h>
#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <brillo/secure_blob.h>
#include <crypto/sha2.h>
#include <libhwsec/frontend/pinweaver_manager/frontend.h>
#include <libhwsec/status.h>
#include <libhwsec-foundation/crypto/aes.h>
#include <libhwsec-foundation/crypto/hmac.h>
#include <libhwsec-foundation/crypto/libscrypt_compat.h>
#include <libhwsec-foundation/crypto/scrypt.h>
#include <libhwsec-foundation/crypto/secure_blob_util.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>
#include <openssl/sha.h>

#include "cryptohome/cryptohome_common.h"
#include "cryptohome/cryptohome_keys_manager.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/filesystem_layout.h"
#include "cryptohome/key_objects.h"
#include "cryptohome/vault_keyset.h"

using base::FilePath;
using brillo::SecureBlob;
using ResetType = hwsec::PinWeaverManagerFrontend::ResetType;
using hwsec::TPMErrorBase;
using hwsec_foundation::HmacSha256;
using hwsec_foundation::SecureBlobToHex;
using hwsec_foundation::SecureBlobToHexToBuffer;

namespace cryptohome {

Crypto::Crypto(const hwsec::CryptohomeFrontend* hwsec,
               const hwsec::PinWeaverManagerFrontend* hwsec_pw_manager,
               CryptohomeKeysManager* cryptohome_keys_manager,
               const hwsec::RecoveryCryptoFrontend* recovery_hwsec)
    : hwsec_(hwsec),
      hwsec_pw_manager_(hwsec_pw_manager),
      cryptohome_keys_manager_(cryptohome_keys_manager),
      recovery_hwsec_(recovery_hwsec) {
  CHECK(hwsec);
  CHECK(hwsec_pw_manager);
  CHECK(cryptohome_keys_manager);
  // recovery_hwsec_ may be nullptr.
}

Crypto::~Crypto() {}

void Crypto::Init() {
  cryptohome_keys_manager_->Init();

  // Calling Initialize() to ensure that PinWeaverManager is initialized
  // (specifically the memory-mapped pinweaver leaf cache file). This is not
  // necessary as we always ensure state is ready before running any PW
  // operation functions, but it may result in better boot performance.
  if (hwsec::Status status = hwsec_pw_manager_->Initialize(); !status.ok()) {
    LOG(ERROR) << "PinWeaver Manager is not in a good state: "
               << status.status();
    // We don't report the error to the caller: this failure shouldn't abort
    // the daemon initialization.
  }
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

bool Crypto::ResetLeCredential(const uint64_t le_label,
                               const SecureBlob& reset_secret,
                               bool strong_reset,
                               CryptoError& out_error) const {
  // Bail immediately if we don't have a valid LECredentialManager.
  hwsec::StatusOr<bool> is_enabled = hwsec_pw_manager_->IsEnabled();
  if (!is_enabled.ok() || !is_enabled.value()) {
    LOG(ERROR) << "Attempting to Reset LECredential on a platform that doesn't "
                  "support LECredential";
    PopulateError(&out_error, CryptoError::CE_LE_NOT_SUPPORTED);
    return false;
  }

  auto&& reset_type =
      static_cast<hwsec::PinWeaverManagerFrontend::ResetType>(strong_reset);
  hwsec::Status ret =
      hwsec_pw_manager_->ResetCredential(le_label, reset_secret, reset_type);
  if (!ret.ok()) {
    PopulateError(&out_error,
                  ret->ToTPMRetryAction() == hwsec::TPMRetryAction::kUserAuth
                      ? CryptoError::CE_LE_INVALID_SECRET
                      : CryptoError::CE_OTHER_FATAL);
    return false;
  }
  return true;
}

int Crypto::GetWrongAuthAttempts(uint64_t le_label) const {
  hwsec::StatusOr<bool> is_enabled = hwsec_pw_manager_->IsEnabled();
  CHECK(is_enabled.ok() && is_enabled.value())
      << "pinweaver_manager isn't enabled when calling GetWrongAuthAttempts()";
  hwsec::StatusOr<uint32_t> result =
      hwsec_pw_manager_->GetWrongAuthAttempts(le_label);
  return result.value_or(-1);
}

bool Crypto::RemoveLECredential(uint64_t label) const {
  // Bail immediately if we don't have a valid LECredentialManager.
  hwsec::StatusOr<bool> is_enabled = hwsec_pw_manager_->IsEnabled();
  if (!is_enabled.ok() || !is_enabled.value()) {
    LOG(ERROR) << "Attempting to Reset LECredential on a platform that doesn't "
                  "support LECredential";
    return false;
  }

  return hwsec_pw_manager_->RemoveCredential(label).ok();
}

bool Crypto::is_cryptohome_key_loaded() const {
  return cryptohome_keys_manager_->HasAnyCryptohomeKey();
}

bool Crypto::CanUnsealWithUserAuth() const {
  hwsec::StatusOr<bool> is_ready = hwsec_->IsSealingSupported();
  if (!is_ready.ok()) {
    LOG(ERROR) << "Failed to get da mitigation status: " << is_ready.status();
    return false;
  }

  return is_ready.value();
}

}  // namespace cryptohome
