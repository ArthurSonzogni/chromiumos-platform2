// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Contains the implementation of class Crypto

#include "cryptohome/crypto.h"

#include <sys/types.h>

#include <limits>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>
#include <openssl/sha.h>
#include <unistd.h>

#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <brillo/secure_blob.h>
extern "C" {
#include <scrypt/crypto_scrypt.h>
#include <scrypt/scryptenc.h>
}

#include "cryptohome/cryptohome_common.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/cryptolib.h"
#include "cryptohome/platform.h"
#include "cryptohome/tpm_init.h"
#include "cryptohome/username_passkey.h"

#include "attestation.pb.h"  // NOLINT(build/include)

// Included last because they both have conflicting defines :(
extern "C" {
#include <ecryptfs.h>
#include <keyutils.h>
}

using base::FilePath;
using brillo::SecureBlob;

namespace cryptohome {

// An upper bound on the amount of memory that we allow Scrypt to use when
// performing key strengthening (32MB).  A large size is okay since we only use
// Scrypt during the login process, before the user is logged in.  This memory
// is managed (and freed) by the scrypt library.
const unsigned int kScryptMaxMem = 32 * 1024 * 1024;

// An upper bound on the amount of time we allow Scrypt to use when performing
// key strenthening (1/3s) for encryption.
const double kScryptMaxEncryptTime = 0.333;

// An upper bound on the amount of time we allow Scrypt to use when performing
// key strenthening (100s) for decryption.  This number can be high because in
// practice, it doesn't mean much.  It simply needs to be large enough that we
// can guarantee that the key derived during encryption can always be derived at
// decryption time, so the typical time is usually close to 1/3s.  However,
// because sometimes other processes may interfere, we need it to be large
// enough to allow the same calculation to be made amidst other heavy use.
const double kScryptMaxDecryptTime = 100.0;

// Scrypt creates a header in the cipher text that we need to account for in
// buffer sizing.
const unsigned int kScryptHeaderLength = 128;

// The number of hash rounds we originally used when converting a password to a
// key.  This is used when converting older cryptohome vault keysets.
const unsigned int kDefaultLegacyPasswordRounds = 1;

// AES key size in bytes (256-bit).  This key size is used for all key creation,
// though we currently only use 128 bits for the eCryptfs File Encryption Key
// (FEK).  Larger than 128-bit has too great of a CPU overhead on unaccelerated
// architectures.
const unsigned int kDefaultAesKeySize = 32;

// Maximum size of the salt file.
const int64_t Crypto::kSaltMax = (1 << 20);  // 1 MB

// File permissions of salt file (modulo umask).
const mode_t kSaltFilePermissions = 0644;

Crypto::Crypto(Platform* platform)
    : use_tpm_(false),
      tpm_(NULL),
      platform_(platform),
      tpm_init_(NULL),
      scrypt_max_encrypt_time_(kScryptMaxEncryptTime) {
}

Crypto::~Crypto() {
}

bool Crypto::Init(TpmInit* tpm_init) {
  if (use_tpm_) {
    CHECK(tpm_init) << "Crypto wanted to use TPM but was not provided a TPM";
    if (tpm_ == NULL) {
      tpm_ = tpm_init->get_tpm();
    }
    tpm_init_ = tpm_init;
    tpm_init_->SetupTpm(true);
  }
  return true;
}

Crypto::CryptoError Crypto::EnsureTpm(bool reload_key) const {
  Crypto::CryptoError result = Crypto::CE_NONE;
  if (tpm_ && tpm_init_) {
    if (reload_key || !tpm_init_->HasCryptohomeKey()) {
      tpm_init_->SetupTpm(true);
    }
  }
  return result;
}

bool Crypto::PasskeyToTokenAuthData(const brillo::Blob& passkey,
                                    const FilePath& salt_file,
                                    SecureBlob* auth_data) const {
  // Use the scrypt algorithm to derive auth data from the passkey.
  const size_t kAuthDataSizeBytes = 32;
  // The following scrypt parameters are based on Colin Percival's interactive
  // login example in http://www.tarsnap.com/scrypt/scrypt-slides.pdf and
  // adjusted for ~100ms performance on slower models. The performance is
  // dependant on CPU and memory performance.
  const uint64_t kScryptParameterN = (1 << 11);
  const uint32_t kScryptParameterR = 8;
  const uint32_t kScryptParameterP = 1;
  const unsigned int kSaltLength = 32;
  SecureBlob salt;
  if (!GetOrCreateSalt(salt_file, kSaltLength, false, &salt)) {
    LOG(ERROR) << "Failed to get authorization data salt.";
    return false;
  }
  SecureBlob local_auth_data;
  local_auth_data.resize(kAuthDataSizeBytes);
  if (0 != crypto_scrypt(passkey.data(),
                         passkey.size(),
                         salt.data(),
                         salt.size(),
                         kScryptParameterN,
                         kScryptParameterR,
                         kScryptParameterP,
                         local_auth_data.data(),
                         kAuthDataSizeBytes)) {
    LOG(ERROR) << "Scrypt key derivation failed.";
    return false;
  }
  auth_data->swap(local_auth_data);
  return true;
}

bool Crypto::GetOrCreateSalt(const FilePath& path,
                             size_t length,
                             bool force,
                             SecureBlob* salt) const {
  int64_t file_len = 0;
  if (platform_->FileExists(path)) {
    if (!platform_->GetFileSize(path, &file_len)) {
      LOG(ERROR) << "Can't get file len for " << path.value();
      return false;
    }
  }
  SecureBlob local_salt;
  if (force || file_len == 0 || file_len > kSaltMax) {
    LOG(ERROR) << "Creating new salt at " << path.value()
               << " (" << force << ", " << file_len << ")";
    // If this salt doesn't exist, automatically create it
    local_salt.resize(length);
    CryptoLib::GetSecureRandom(local_salt.data(), local_salt.size());
    if (!platform_->WriteFileAtomicDurable(path, local_salt,
                                           kSaltFilePermissions)) {
      LOG(ERROR) << "Could not write user salt";
      return false;
    }
  } else {
    local_salt.resize(file_len);
    if (!platform_->ReadFile(path, &local_salt)) {
      LOG(ERROR) << "Could not read salt file of length " << file_len;
      return false;
    }
  }
  salt->swap(local_salt);
  return true;
}

Crypto::CryptoError Crypto::TpmErrorToCrypto(
    Tpm::TpmRetryAction retry_action) const {
  switch (retry_action) {
    case Tpm::kTpmRetryFatal:
      return Crypto::CE_TPM_FATAL;
    case Tpm::kTpmRetryCommFailure:
    case Tpm::kTpmRetryInvalidHandle:
    case Tpm::kTpmRetryLoadFail:
      return Crypto::CE_TPM_COMM_ERROR;
    case Tpm::kTpmRetryDefendLock:
      return Crypto::CE_TPM_DEFEND_LOCK;
    case Tpm::kTpmRetryReboot:
      return Crypto::CE_TPM_REBOOT;
    default:
      return Crypto::CE_NONE;
  }
}

void Crypto::PasswordToPasskey(const char* password,
                               const brillo::Blob& salt,
                               SecureBlob* passkey) {
  CHECK(password);

  std::string ascii_salt = CryptoLib::BlobToHex(salt);
  // Convert a raw password to a password hash
  SHA256_CTX sha_context;
  SecureBlob md_value(SHA256_DIGEST_LENGTH);

  SHA256_Init(&sha_context);
  SHA256_Update(&sha_context, ascii_salt.data(), ascii_salt.length());
  SHA256_Update(&sha_context, password, strlen(password));
  SHA256_Final(md_value.data(), &sha_context);

  md_value.resize(SHA256_DIGEST_LENGTH / 2);
  SecureBlob local_passkey(SHA256_DIGEST_LENGTH);
  CryptoLib::BlobToHexToBuffer(md_value,
                               local_passkey.data(),
                               local_passkey.size());
  passkey->swap(local_passkey);
}

bool Crypto::IsTPMPubkeyHash(const std::string& hash,
                             CryptoError* error) const {
  SecureBlob pub_key_hash;
  Tpm::TpmRetryAction retry_action = tpm_->GetPublicKeyHash(
      tpm_init_->GetCryptohomeKey(),
      &pub_key_hash);
  if (retry_action == Tpm::kTpmRetryLoadFail ||
      retry_action == Tpm::kTpmRetryInvalidHandle) {
    if (!tpm_init_->ReloadCryptohomeKey()) {
      LOG(ERROR) << "Unable to reload key.";
      retry_action = Tpm::kTpmRetryFailNoRetry;
    } else {
      retry_action = tpm_->GetPublicKeyHash(tpm_init_->GetCryptohomeKey(),
                                            &pub_key_hash);
    }
  }
  if (retry_action != Tpm::kTpmRetryNone) {
    LOG(ERROR) << "Unable to get the cryptohome public key from the TPM.";
    ReportCryptohomeError(kCannotReadTpmPublicKey);
    if (error) {
      *error = TpmErrorToCrypto(retry_action);
    }
    return false;
  }
  if ((hash.size() != pub_key_hash.size()) ||
      (brillo::SecureMemcmp(hash.data(),
                              pub_key_hash.data(),
                              pub_key_hash.size()))) {
    if (error)
      *error = CE_TPM_FATAL;
    return false;
  }
  return true;
}

bool Crypto::DecryptTPM(const SerializedVaultKeyset& serialized,
                        const SecureBlob& vault_key,
                        CryptoError* error,
                        VaultKeyset* keyset) const {
  CHECK(tpm_);
  CHECK(serialized.flags() & SerializedVaultKeyset::TPM_WRAPPED);
  SecureBlob local_encrypted_keyset(serialized.wrapped_keyset().length());
  serialized.wrapped_keyset().copy(
      local_encrypted_keyset.char_data(),
      serialized.wrapped_keyset().length(), 0);
  SecureBlob salt(serialized.salt().length());
  serialized.salt().copy(salt.char_data(), serialized.salt().length(), 0);
  SecureBlob local_vault_key(vault_key.begin(), vault_key.end());
  bool chaps_key_present = serialized.has_wrapped_chaps_key();
  SecureBlob local_wrapped_chaps_key;
  if (chaps_key_present) {
    local_wrapped_chaps_key = SecureBlob(serialized.wrapped_chaps_key());
  }
  if (!serialized.has_tpm_key()) {
    LOG(ERROR) << "Decrypting with TPM, but no tpm key present";
    ReportCryptohomeError(kDecryptAttemptButTpmKeyMissing);
    if (error)
      *error = CE_TPM_FATAL;
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
    if (error)
      *error = CE_TPM_FATAL;
    return false;
  }

  Crypto::CryptoError local_error = EnsureTpm(false);
  if (!is_cryptohome_key_loaded()) {
    LOG(ERROR) << "Vault keyset is wrapped by the TPM, but the TPM is "
               << "unavailable";
    ReportCryptohomeError(kDecryptAttemptButTpmNotAvailable);
    if (error)
      *error = local_error;
    return false;
  }

  unsigned int rounds;
  if (serialized.has_password_rounds()) {
    rounds = serialized.password_rounds();
  } else {
    rounds = kDefaultLegacyPasswordRounds;
  }

  if (serialized.has_tpm_public_key_hash()) {
    if (!IsTPMPubkeyHash(serialized.tpm_public_key_hash(), error)) {
      LOG(ERROR) << "TPM public key hash mismatch.";
      ReportCryptohomeError(kDecryptAttemptButTpmKeyMismatch);
      return false;
    }
  }

  SecureBlob tpm_key(serialized.tpm_key().length());
  serialized.tpm_key().copy(tpm_key.char_data(),
                            serialized.tpm_key().length(), 0);
  SecureBlob key;
  CryptoLib::PasskeyToAesKey(vault_key, salt, rounds, &key, NULL);
  Tpm::TpmRetryAction retry_action = tpm_->DecryptBlob(
      tpm_init_->GetCryptohomeKey(),
      tpm_key,
      key,
      &local_vault_key);
  if (retry_action == Tpm::kTpmRetryLoadFail ||
      retry_action == Tpm::kTpmRetryInvalidHandle ||
      retry_action == Tpm::kTpmRetryCommFailure) {
    if (!tpm_init_->ReloadCryptohomeKey()) {
      LOG(ERROR) << "Unable to reload Cryptohome key.";
      retry_action = Tpm::kTpmRetryFailNoRetry;
    } else {
      retry_action = tpm_->DecryptBlob(tpm_init_->GetCryptohomeKey(),
                                       tpm_key,
                                       key,
                                       &local_vault_key);
    }
  }
  if (retry_action != Tpm::kTpmRetryNone) {
    LOG(ERROR) << "The TPM failed to unwrap the intermediate key with the "
               << "supplied credentials";
    ReportCryptohomeError(kDecryptAttemptWithTpmKeyFailed);
    if (error) {
      *error = TpmErrorToCrypto(retry_action);
    }
    return false;
  }

  SecureBlob aes_key;
  SecureBlob iv;
  if (!CryptoLib::PasskeyToAesKey(local_vault_key,
                                  salt,
                                  rounds,
                                  &aes_key,
                                  &iv)) {
    LOG(ERROR) << "Failure converting passkey to key";
    if (error)
      *error = CE_OTHER_FATAL;
    return false;
  }
  SecureBlob plain_text;
  if (!CryptoLib::AesDecrypt(local_encrypted_keyset, aes_key, iv,
                             &plain_text)) {
    LOG(ERROR) << "AES decryption failed for vault keyset.";
    if (error)
      *error = CE_OTHER_CRYPTO;
    return false;
  }
  SecureBlob unwrapped_chaps_key;
  if (chaps_key_present && !CryptoLib::AesDecrypt(local_wrapped_chaps_key,
                                                  aes_key,
                                                  iv,
                                                  &unwrapped_chaps_key)) {
    LOG(ERROR) << "AES decryption failed for chaps key.";
    if (error)
      *error = CE_OTHER_CRYPTO;
    return false;
  }

  // Use the same key to unwrap the wrapped authorization data.
  if (serialized.key_data().authorization_data_size() > 0) {
    KeyData* key_data = keyset->mutable_serialized()->mutable_key_data();
    for (int auth_data_i = 0;
         auth_data_i < key_data->authorization_data_size();
         ++auth_data_i) {
      KeyAuthorizationData* auth_data =
          key_data->mutable_authorization_data(auth_data_i);
      for (int secret_i = 0; secret_i < auth_data->secrets_size(); ++secret_i) {
        KeyAuthorizationSecret* secret = auth_data->mutable_secrets(secret_i);
        if (!secret->wrapped() || !secret->has_symmetric_key())
          continue;
        SecureBlob encrypted_auth_key(secret->symmetric_key());
        SecureBlob clear_key;
        // Is it reasonable to use this key here as well?
        if (!CryptoLib::AesDecrypt(encrypted_auth_key, aes_key, iv,
                                   &clear_key)) {
          LOG(ERROR) << "Failed to unwrap a symmetric authorization key:"
                     << " (" << auth_data_i << "," << secret_i << ")";
          // This does not force a failure to use the keyset.
          continue;
        }
        secret->set_symmetric_key(clear_key.to_string());
        secret->set_wrapped(false);
      }
    }
  }

  keyset->FromKeysBlob(plain_text);
  if (chaps_key_present) {
    keyset->set_chaps_key(unwrapped_chaps_key);
  }
  if (!serialized.has_tpm_public_key_hash() && error) {
    *error = CE_NO_PUBLIC_KEY_HASH;
  }

  // By this point we know that the TPM is successfully owned, everything
  // is initialized, and we were able to successfully decrypted a
  // tpm-wrapped keyset. So, for TPMs with updateable firmware, we assume
  // that it is stable (and the TPM can invalidate the old version).
  tpm_->DeclareTpmFirmwareStable();

  return true;
}

bool Crypto::DecryptScrypt(const SerializedVaultKeyset& serialized,
                           const SecureBlob& key,
                           CryptoError* error,
                           VaultKeyset* keyset) const {
  SecureBlob blob = SecureBlob(serialized.wrapped_keyset());
  int scrypt_rc;
  size_t out_len = 0;
  SecureBlob decrypted(blob.size());
  // Perform a Scrypt operation on wrapped vault keyset.
  if ((scrypt_rc = scryptdec_buf(blob.data(),
                                 blob.size(),
                                 decrypted.data(),
                                 &out_len,
                                 key.data(),
                                 key.size(),
                                 kScryptMaxMem,
                                 100.0,
                                 kScryptMaxDecryptTime))) {
    LOG(ERROR) << "Vault Keyset Scrypt decryption returned error code: "
               << scrypt_rc;
    if (error)
      *error = CE_SCRYPT_CRYPTO;
    return false;
  }
  // Check if the plaintext is the right length.
  if (blob.size() < kScryptHeaderLength ||
      out_len != (blob.size() - kScryptHeaderLength)) {
    LOG(ERROR) << "Scrypt decryption output was the wrong length.";
    if (error) {
      *error = CE_SCRYPT_CRYPTO;
    }
    return false;
  }
  decrypted.resize(out_len);
  out_len = 0;
  SecureBlob chaps_key;
  SecureBlob wrapped_chaps_key;
  bool chaps_key_present = serialized.has_wrapped_chaps_key();
  if (chaps_key_present) {
    wrapped_chaps_key = SecureBlob(serialized.wrapped_chaps_key());
    chaps_key.resize(wrapped_chaps_key.size());
  }
  // Perform a Scrypt operation on wrapped chaps key.
  if (chaps_key_present) {
    scrypt_rc = scryptdec_buf(wrapped_chaps_key.data(),
                              wrapped_chaps_key.size(),
                              chaps_key.data(),
                              &out_len,
                              key.data(),
                              key.size(),
                              kScryptMaxMem,
                              100.0,
                              kScryptMaxDecryptTime);
    if (scrypt_rc) {
      LOG(ERROR) << "Chaps keyset Scrypt decryption returned error code: "
                 << scrypt_rc;
      if (error)
        *error = CE_SCRYPT_CRYPTO;
      return false;
    }
    // Check if the plaintext is the right length.
    if ((wrapped_chaps_key.size() < kScryptHeaderLength) ||
        (out_len != (wrapped_chaps_key.size() - kScryptHeaderLength))) {
      LOG(ERROR) << "Scrypt decryption output was the wrong length";
      if (error) {
        *error = CE_SCRYPT_CRYPTO;
      }
      return false;
    }
    chaps_key.resize(out_len);
    keyset->set_chaps_key(chaps_key);
  }

  // Perform sanity check to ensure vault keyset is not tampered with.
  SecureBlob included_hash(SHA_DIGEST_LENGTH);
  if (decrypted.size() < SHA_DIGEST_LENGTH) {
    LOG(ERROR) << "Message length underflow: " << decrypted.size() << " bytes?";
    return false;
  }
  memcpy(included_hash.data(), &decrypted[decrypted.size() - SHA_DIGEST_LENGTH],
         SHA_DIGEST_LENGTH);
  decrypted.resize(decrypted.size() - SHA_DIGEST_LENGTH);
  brillo::Blob hash = CryptoLib::Sha1(decrypted);
  if (brillo::SecureMemcmp(hash.data(), included_hash.data(), hash.size())) {
    LOG(ERROR) << "Scrypt hash verification failed";
    if (error) {
      *error = CE_SCRYPT_CRYPTO;
    }
    return false;
  }
  keyset->FromKeysBlob(decrypted);
  return true;
}

bool Crypto::DecryptVaultKeyset(const SerializedVaultKeyset& serialized,
                                const SecureBlob& vault_key,
                                unsigned int* crypt_flags, CryptoError* error,
                                VaultKeyset* vault_keyset) const {
  if (crypt_flags)
    *crypt_flags = serialized.flags();
  if (error)
    *error = CE_NONE;
  // Check if the vault keyset was Scrypt-wrapped
  // (start with Scrypt to avoid reaching to TPM if both flags are set)
  unsigned int flags = serialized.flags();
  if (flags & SerializedVaultKeyset::SCRYPT_WRAPPED) {
    bool should_try_tpm = false;
    if (flags & SerializedVaultKeyset::TPM_WRAPPED) {
      LOG(ERROR) << "Keyset wrapped with both TPM and Scrypt?";
      ReportCryptohomeError(cryptohome::kBothTpmAndScryptWrappedKeyset);
      // Fallback for the bug when both flags were set: try both methods
      should_try_tpm = true;
    }
    if (DecryptScrypt(serialized, vault_key, error, vault_keyset))
      return true;
    if (!should_try_tpm)
      return false;
  }
  if (flags & SerializedVaultKeyset::TPM_WRAPPED) {
    return DecryptTPM(serialized, vault_key, error, vault_keyset);
  } else {
    LOG(ERROR) << "Keyset wrapped with neither TPM nor Scrypt?";
    return false;
  }
}

bool Crypto::EncryptTPM(const VaultKeyset& vault_keyset,
                        const SecureBlob& key,
                        const SecureBlob& salt,
                        SerializedVaultKeyset* serialized) const {
  if (!use_tpm_)
    return false;
  EnsureTpm(false);
  if (!is_cryptohome_key_loaded())
    return false;
  SecureBlob local_blob(kDefaultAesKeySize);
  CryptoLib::GetSecureRandom(local_blob.data(), local_blob.size());
  SecureBlob tpm_key;
  SecureBlob derived_key;
  unsigned int rounds = kDefaultPasswordRounds;
  CryptoLib::PasskeyToAesKey(key, salt, rounds, &derived_key, NULL);
  // Encrypt the VKK using the TPM and the user's passkey.  The output is an
  // encrypted blob in tpm_key, which is stored in the serialized vault
  // keyset.
  if (tpm_->EncryptBlob(tpm_init_->GetCryptohomeKey(),
                        local_blob,
                        derived_key,
                        &tpm_key) != Tpm::kTpmRetryNone) {
    LOG(ERROR) << "Failed to wrap vkk with creds.";
    return false;
  }

  SecureBlob aes_key;
  SecureBlob iv;
  if (!CryptoLib::PasskeyToAesKey(local_blob, salt, rounds, &aes_key, &iv)) {
    LOG(ERROR) << "Failure converting passkey to key";
    return false;
  }

  SecureBlob blob;
  if (!vault_keyset.ToKeysBlob(&blob)) {
    LOG(ERROR) << "Failure serializing keyset to buffer";
    return false;
  }
  SecureBlob chaps_key = vault_keyset.chaps_key();
  SecureBlob cipher_text;
  SecureBlob wrapped_chaps_key;
  if (!CryptoLib::AesEncrypt(blob, aes_key, iv, &cipher_text) ||
      !CryptoLib::AesEncrypt(chaps_key, aes_key, iv, &wrapped_chaps_key)) {
    LOG(ERROR) << "AES encryption failed.";
    return false;
  }

  // Allow this to fail.  It is not absolutely necessary; it allows us to
  // detect a TPM clear.  If this fails due to a transient issue, then on next
  // successful login, the vault keyset will be re-saved anyway.
  SecureBlob pub_key_hash;
  if (tpm_->GetPublicKeyHash(tpm_init_->GetCryptohomeKey(),
                             &pub_key_hash) == Tpm::kTpmRetryNone)
    serialized->set_tpm_public_key_hash(pub_key_hash.data(),
                                        pub_key_hash.size());

  unsigned int flags = serialized->flags();
  serialized->set_password_rounds(rounds);
  serialized->set_flags((flags & ~SerializedVaultKeyset::SCRYPT_WRAPPED)
                        | SerializedVaultKeyset::TPM_WRAPPED);
  serialized->set_tpm_key(tpm_key.data(), tpm_key.size());
  serialized->set_wrapped_keyset(cipher_text.data(), cipher_text.size());
  if (vault_keyset.chaps_key().size() == CRYPTOHOME_CHAPS_KEY_LENGTH) {
    serialized->set_wrapped_chaps_key(wrapped_chaps_key.data(),
                                      wrapped_chaps_key.size());
  } else {
    serialized->clear_wrapped_chaps_key();
  }

  // Handle AuthorizationData secrets if provided.
  if (serialized->key_data().authorization_data_size() > 0) {
    KeyData* key_data = serialized->mutable_key_data();
    for (int auth_data_i = 0;
         auth_data_i < key_data->authorization_data_size();
         ++auth_data_i) {
      KeyAuthorizationData* auth_data =
          key_data->mutable_authorization_data(auth_data_i);
      for (int secret_i = 0; secret_i < auth_data->secrets_size(); ++secret_i) {
        KeyAuthorizationSecret* secret = auth_data->mutable_secrets(secret_i);
        // Secrets that are externally provided should not be wrapped when
        // this is called.  However, calling Encrypt() again should be
        // idempotent.  External callers should be filtered at the API layer.
        if (secret->wrapped() || !secret->has_symmetric_key())
          continue;
        SecureBlob clear_auth_key(secret->symmetric_key());
        SecureBlob encrypted_auth_key;

        if (!CryptoLib::AesEncrypt(clear_auth_key, aes_key, iv,
                                   &encrypted_auth_key)) {
          LOG(ERROR) << "Failed to wrap a symmetric authorization key:"
                     << " (" << auth_data_i << "," << secret_i << ")";
          // This forces a failure.
          return false;
        }
        secret->set_symmetric_key(encrypted_auth_key.to_string());
        secret->set_wrapped(true);
      }
    }
  }

  return true;
}

bool Crypto::EncryptScrypt(const VaultKeyset& vault_keyset,
                           const SecureBlob& key,
                           SerializedVaultKeyset* serialized) const {
  SecureBlob blob;
  if (!vault_keyset.ToKeysBlob(&blob)) {
    LOG(ERROR) << "Failure serializing keyset to buffer";
    return false;
  }
  // Append the SHA1 hash of the keyset blob
  SecureBlob hash = CryptoLib::Sha1(blob);
  SecureBlob local_blob = SecureBlob::Combine(blob, hash);
  SecureBlob cipher_text(local_blob.size() + kScryptHeaderLength);

  int scrypt_rc;
  if ((scrypt_rc = scryptenc_buf(local_blob.data(),
                                 local_blob.size(),
                                 cipher_text.data(),
                                 key.data(),
                                 key.size(),
                                 kScryptMaxMem,
                                 100.0,
                                 scrypt_max_encrypt_time_))) {
    LOG(ERROR) << "Vault Keyset Scrypt encryption returned error code: "
               << scrypt_rc;
    return false;
  }

  SecureBlob chaps_key = vault_keyset.chaps_key();
  SecureBlob wrapped_chaps_key;
  wrapped_chaps_key.resize(chaps_key.size() + kScryptHeaderLength);
  scrypt_rc = scryptenc_buf(chaps_key.data(),
                            chaps_key.size(),
                            wrapped_chaps_key.data(),
                            key.data(),
                            key.size(),
                            kScryptMaxMem,
                            100.0,
                            scrypt_max_encrypt_time_);
  if (scrypt_rc) {
    LOG(ERROR) << "Chaps Key Scrypt encryption returned error code: "
               << scrypt_rc;
    return false;
  }
  unsigned int flags = serialized->flags();
  serialized->set_flags((flags & ~SerializedVaultKeyset::TPM_WRAPPED)
                        | SerializedVaultKeyset::SCRYPT_WRAPPED);
  serialized->set_wrapped_keyset(cipher_text.data(), cipher_text.size());
  if (vault_keyset.chaps_key().size() == CRYPTOHOME_CHAPS_KEY_LENGTH) {
    serialized->set_wrapped_chaps_key(wrapped_chaps_key.data(),
                                      wrapped_chaps_key.size());
  } else {
    serialized->clear_wrapped_chaps_key();
  }

  return true;
}

bool Crypto::EncryptVaultKeyset(const VaultKeyset& vault_keyset,
                                const SecureBlob& vault_key,
                                const SecureBlob& vault_key_salt,
                                SerializedVaultKeyset* serialized) const {
  if (!EncryptTPM(vault_keyset, vault_key, vault_key_salt, serialized)) {
    if (use_tpm_ && tpm_ && tpm_->IsOwned()) {
      ReportCryptohomeError(kEncryptWithTpmFailed);
    }
    if (!EncryptScrypt(vault_keyset, vault_key, serialized)) {
      return false;
    }
  }

  serialized->set_salt(vault_key_salt.data(),
                       vault_key_salt.size());
  return true;
}

bool Crypto::EncryptWithTpm(const SecureBlob& data,
                            std::string* encrypted_data) const {
  SecureBlob aes_key;
  SecureBlob sealed_key;
  if (!CreateSealedKey(&aes_key, &sealed_key))
    return false;
  return EncryptData(data, aes_key, sealed_key, encrypted_data);
}

bool Crypto::DecryptWithTpm(const std::string& encrypted_data,
                            SecureBlob* data) const {
  SecureBlob aes_key;
  SecureBlob sealed_key;
  if (!UnsealKey(encrypted_data, &aes_key, &sealed_key)) {
    return false;
  }
  return DecryptData(encrypted_data, aes_key, data);
}

bool Crypto::CreateSealedKey(SecureBlob* aes_key,
                             SecureBlob* sealed_key) const {
  if (!use_tpm_)
    return false;
  if (!tpm_->GetRandomData(kDefaultAesKeySize, aes_key)) {
    LOG(ERROR) << "GetRandomData failed.";
    return false;
  }
  if (!tpm_->SealToPCR0(*aes_key, sealed_key)) {
    LOG(ERROR) << "Failed to seal cipher key.";
    return false;
  }
  return true;
}

bool Crypto::EncryptData(const SecureBlob& data,
                         const SecureBlob& aes_key,
                         const SecureBlob& sealed_key,
                         std::string* encrypted_data) const {
  if (!use_tpm_)
    return false;
  SecureBlob iv;
  if (!tpm_->GetRandomData(kAesBlockSize, &iv)) {
    LOG(ERROR) << "GetRandomData failed.";
    return false;
  }
  SecureBlob encrypted_data_blob;
  if (!CryptoLib::AesEncryptSpecifyBlockMode(data, 0, data.size(), aes_key, iv,
                                             CryptoLib::kPaddingStandard,
                                             CryptoLib::kCbc,
                                             &encrypted_data_blob)) {
    LOG(ERROR) << "Failed to encrypt serial data.";
    return false;
  }
  EncryptedData encrypted_pb;
  encrypted_pb.set_wrapped_key(sealed_key.data(), sealed_key.size());
  encrypted_pb.set_iv(iv.data(), iv.size());
  encrypted_pb.set_encrypted_data(encrypted_data_blob.data(),
                                  encrypted_data_blob.size());
  encrypted_pb.set_mac(CryptoLib::ComputeEncryptedDataHMAC(encrypted_pb,
                                                           aes_key));
  if (!encrypted_pb.SerializeToString(encrypted_data)) {
    LOG(ERROR) << "Could not serialize data to string.";
    return false;
  }
  return true;
}

bool Crypto::UnsealKey(const std::string& encrypted_data,
                       SecureBlob* aes_key,
                       SecureBlob* sealed_key) const {
  if (!use_tpm_)
    return false;
  EncryptedData encrypted_pb;
  if (!encrypted_pb.ParseFromString(encrypted_data)) {
    LOG(ERROR) << "Could not decrypt data as it was not an EncryptedData "
               << "protobuf";
    return false;
  }
  SecureBlob tmp(encrypted_pb.wrapped_key().begin(),
                 encrypted_pb.wrapped_key().end());
  sealed_key->swap(tmp);
  if (!tpm_->Unseal(*sealed_key, aes_key)) {
    LOG(ERROR) << "Cannot unseal aes key.";
    return false;
  }
  return true;
}

bool Crypto::DecryptData(const std::string& encrypted_data,
                         const brillo::SecureBlob& aes_key,
                         brillo::SecureBlob* data) const {
  EncryptedData encrypted_pb;
  if (!encrypted_pb.ParseFromString(encrypted_data)) {
    LOG(ERROR) << "Could not decrypt data as it was not an EncryptedData "
               << "protobuf";
    return false;
  }
  std::string mac = CryptoLib::ComputeEncryptedDataHMAC(encrypted_pb, aes_key);
  if (mac.length() != encrypted_pb.mac().length()) {
    LOG(ERROR) << "Corrupted data in encrypted pb.";
    return false;
  }
  if (0 != brillo::SecureMemcmp(mac.data(), encrypted_pb.mac().data(),
                                  mac.length())) {
    LOG(ERROR) << "Corrupted data in encrypted pb.";
    return false;
  }
  SecureBlob iv(encrypted_pb.iv().begin(), encrypted_pb.iv().end());
  SecureBlob encrypted_data_blob(encrypted_pb.encrypted_data().begin(),
                                 encrypted_pb.encrypted_data().end());
  if (!CryptoLib::AesDecryptSpecifyBlockMode(encrypted_data_blob,
                                             0,
                                             encrypted_data_blob.size(),
                                             aes_key,
                                             iv,
                                             CryptoLib::kPaddingStandard,
                                             CryptoLib::kCbc,
                                             data)) {
    LOG(ERROR) << "Failed to decrypt encrypted data.";
    return false;
  }
  return true;
}

bool Crypto::is_cryptohome_key_loaded() const {
  if (tpm_ == NULL || tpm_init_ == NULL) {
    return false;
  }
  return tpm_init_->HasCryptohomeKey();
}
}  // namespace cryptohome
