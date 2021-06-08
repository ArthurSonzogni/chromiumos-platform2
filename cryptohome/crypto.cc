// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Contains the implementation of class Crypto

#include "cryptohome/crypto.h"

#include <sys/types.h>

#include <crypto/sha2.h>
#include <limits>
#include <map>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>
#include <openssl/sha.h>
#include <unistd.h>
#include <utility>

#include <base/check.h>
#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <brillo/secure_blob.h>

#include "cryptohome/attestation.pb.h"
#include "cryptohome/auth_block_state.pb.h"
#include "cryptohome/challenge_credential_auth_block.h"
#include "cryptohome/crypto/hmac.h"
#include "cryptohome/crypto/sha.h"
#include "cryptohome/cryptohome_common.h"
#include "cryptohome/cryptohome_key_loader.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/cryptolib.h"
#include "cryptohome/double_wrapped_compat_auth_block.h"
#include "cryptohome/key_objects.h"
#include "cryptohome/le_credential_manager_impl.h"
#include "cryptohome/libscrypt_compat.h"
#include "cryptohome/libscrypt_compat_auth_block.h"
#include "cryptohome/pin_weaver_auth_block.h"
#include "cryptohome/platform.h"
#include "cryptohome/tpm_bound_to_pcr_auth_block.h"
#include "cryptohome/tpm_not_bound_to_pcr_auth_block.h"
#include "cryptohome/vault_keyset.h"

using base::FilePath;
using brillo::SecureBlob;

namespace cryptohome {

namespace {

// Location where we store the Low Entropy (LE) credential manager related
// state.
const char kSignInHashTreeDir[] = "/home/.shadow/low_entropy_creds";

// Maximum size of the salt file.
const int64_t kSystemSaltMaxSize = (1 << 20);  // 1 MB

// File permissions of salt file (modulo umask).
const mode_t kSaltFilePermissions = 0644;

bool UnwrapVKKVaultKeyset(const SerializedVaultKeyset& serialized,
                          const KeyBlobs& vkk_data,
                          VaultKeyset* keyset,
                          CryptoError* error) {
  const SecureBlob& vkk_key = vkk_data.vkk_key.value();
  const SecureBlob& vkk_iv = vkk_data.vkk_iv.value();
  const SecureBlob& chaps_iv = vkk_data.chaps_iv.value();

  // Decrypt the keyset protobuf.
  SecureBlob local_encrypted_keyset(serialized.wrapped_keyset().begin(),
                                    serialized.wrapped_keyset().end());
  SecureBlob plain_text;

  if (!CryptoLib::AesDecryptDeprecated(local_encrypted_keyset, vkk_key, vkk_iv,
                                       &plain_text)) {
    LOG(ERROR) << "AES decryption failed for vault keyset.";
    PopulateError(error, CryptoError::CE_OTHER_CRYPTO);
    return false;
  }
  if (!keyset->FromKeysBlob(plain_text)) {
    LOG(ERROR) << "Failed to decode the keys blob.";
    PopulateError(error, CryptoError::CE_OTHER_CRYPTO);
    return false;
  }

  // Decrypt the chaps key.
  if (serialized.has_wrapped_chaps_key()) {
    SecureBlob local_wrapped_chaps_key(serialized.wrapped_chaps_key());
    SecureBlob unwrapped_chaps_key;

    if (!CryptoLib::AesDecryptDeprecated(local_wrapped_chaps_key, vkk_key,
                                         chaps_iv, &unwrapped_chaps_key)) {
      LOG(ERROR) << "AES decryption failed for chaps key.";
      PopulateError(error, CryptoError::CE_OTHER_CRYPTO);
      return false;
    }

    keyset->SetChapsKey(unwrapped_chaps_key);
  }

  // Decrypt the reset seed.
  if (vkk_data.wrapped_reset_seed != base::nullopt &&
      !vkk_data.wrapped_reset_seed.value().empty()) {
    SecureBlob unwrapped_reset_seed;
    SecureBlob local_wrapped_reset_seed =
        SecureBlob(serialized.wrapped_reset_seed());
    SecureBlob local_reset_iv = SecureBlob(serialized.reset_iv());

    if (!CryptoLib::AesDecryptDeprecated(local_wrapped_reset_seed, vkk_key,
                                         local_reset_iv,
                                         &unwrapped_reset_seed)) {
      LOG(ERROR) << "AES decryption failed for reset seed.";
      PopulateError(error, CryptoError::CE_OTHER_CRYPTO);
      return false;
    }

    keyset->SetResetSeed(unwrapped_reset_seed);
  }

  return true;
}

bool UnwrapScryptVaultKeyset(const SerializedVaultKeyset& serialized,
                             const KeyBlobs& vkk_data,
                             VaultKeyset* keyset,
                             CryptoError* error) {
  SecureBlob blob = SecureBlob(serialized.wrapped_keyset());
  SecureBlob decrypted(blob.size());
  if (!LibScryptCompat::Decrypt(blob, vkk_data.scrypt_key->derived_key(),
                                &decrypted)) {
    return false;
  }

  if (serialized.has_wrapped_chaps_key()) {
    SecureBlob chaps_key;
    SecureBlob wrapped_chaps_key = SecureBlob(serialized.wrapped_chaps_key());
    chaps_key.resize(wrapped_chaps_key.size());
    if (!LibScryptCompat::Decrypt(wrapped_chaps_key,
                                  vkk_data.chaps_scrypt_key->derived_key(),
                                  &chaps_key)) {
      return false;
    }
    keyset->SetChapsKey(chaps_key);
  }

  if (serialized.has_wrapped_reset_seed()) {
    SecureBlob reset_seed;
    SecureBlob wrapped_reset_seed = SecureBlob(serialized.wrapped_reset_seed());
    reset_seed.resize(wrapped_reset_seed.size());
    if (!LibScryptCompat::Decrypt(
            wrapped_reset_seed,
            vkk_data.scrypt_wrapped_reset_seed_key->derived_key(),
            &reset_seed)) {
      return false;
    }
    keyset->SetResetSeed(reset_seed);
  }

  // There is a SHA hash included at the end of the decrypted blob. However,
  // scrypt already appends a MAC, so if the payload is corrupted we will fail
  // on the first call to DecryptScryptBlob.
  // TODO(crbug.com/984782): get rid of this entirely.
  if (decrypted.size() < SHA_DIGEST_LENGTH) {
    LOG(ERROR) << "Message length underflow: " << decrypted.size() << " bytes?";
    return false;
  }
  decrypted.resize(decrypted.size() - SHA_DIGEST_LENGTH);
  keyset->FromKeysBlob(decrypted);
  return true;
}

bool WrapVaultKeysetWithAesDeprecated(const VaultKeyset& vault_keyset,
                                      const KeyBlobs& blobs,
                                      bool store_reset_seed,
                                      WrappedKeyMaterial* wrapped) {
  if (blobs.vkk_key == base::nullopt || blobs.vkk_iv == base::nullopt ||
      blobs.chaps_iv == base::nullopt) {
    DLOG(FATAL) << "Fields missing from KeyBlobs.";
    return false;
  }

  SecureBlob vault_blob;
  if (!vault_keyset.ToKeysBlob(&vault_blob)) {
    LOG(ERROR) << "Failure serializing keyset to buffer";
    return false;
  }

  SecureBlob vault_cipher_text;
  if (!CryptoLib::AesEncryptDeprecated(vault_blob, blobs.vkk_key.value(),
                                       blobs.vkk_iv.value(),
                                       &vault_cipher_text)) {
    return false;
  }
  wrapped->wrapped_keyset = vault_cipher_text;
  wrapped->vkk_iv = blobs.vkk_iv;

  if (vault_keyset.GetChapsKey().size() == CRYPTOHOME_CHAPS_KEY_LENGTH) {
    SecureBlob wrapped_chaps_key;
    if (!CryptoLib::AesEncryptDeprecated(
            vault_keyset.GetChapsKey(), blobs.vkk_key.value(),
            blobs.chaps_iv.value(), &wrapped_chaps_key)) {
      return false;
    }
    wrapped->wrapped_chaps_key = wrapped_chaps_key;
    wrapped->chaps_iv = blobs.chaps_iv;
  }

  // If a reset seed is present, encrypt and store it, else clear the field.
  if (store_reset_seed && vault_keyset.GetResetSeed().size() != 0) {
    const auto reset_iv = CryptoLib::CreateSecureRandomBlob(kAesBlockSize);
    SecureBlob wrapped_reset_seed;
    if (!CryptoLib::AesEncryptDeprecated(vault_keyset.GetResetSeed(),
                                         blobs.vkk_key.value(), reset_iv,
                                         &wrapped_reset_seed)) {
      LOG(ERROR) << "AES encryption of Reset seed failed.";
      return false;
    }
    wrapped->wrapped_reset_seed = wrapped_reset_seed;
    wrapped->reset_iv = reset_iv;
  }

  return true;
}

bool WrapScryptVaultKeyset(const VaultKeyset& vault_keyset,
                           const KeyBlobs& key_blobs,
                           WrappedKeyMaterial* wrapped) {
  if (vault_keyset.IsLECredential()) {
    LOG(ERROR) << "Low entropy credentials cannot be scrypt-wrapped.";
    return false;
  }

  brillo::SecureBlob blob;
  if (!vault_keyset.ToKeysBlob(&blob)) {
    LOG(ERROR) << "Failure serializing keyset to buffer";
    return false;
  }

  // Append the SHA1 hash of the keyset blob. This is done solely for
  // backwards-compatibility purposes, since scrypt already creates a
  // MAC for the encrypted blob. It is ignored in DecryptScrypt since
  // it is redundant.
  brillo::SecureBlob hash = Sha1(blob);
  brillo::SecureBlob local_blob = SecureBlob::Combine(blob, hash);
  brillo::SecureBlob cipher_text;
  if (!LibScryptCompat::Encrypt(key_blobs.scrypt_key->derived_key(),
                                key_blobs.scrypt_key->ConsumeSalt(), local_blob,
                                kDefaultScryptParams, &cipher_text)) {
    LOG(ERROR) << "Scrypt encrypt of keyset blob failed.";
    return false;
  }
  wrapped->wrapped_keyset = cipher_text;

  if (vault_keyset.GetChapsKey().size() == CRYPTOHOME_CHAPS_KEY_LENGTH) {
    SecureBlob wrapped_chaps_key;
    if (!LibScryptCompat::Encrypt(key_blobs.chaps_scrypt_key->derived_key(),
                                  key_blobs.chaps_scrypt_key->ConsumeSalt(),
                                  vault_keyset.GetChapsKey(),
                                  kDefaultScryptParams, &wrapped_chaps_key)) {
      LOG(ERROR) << "Scrypt encrypt of chaps key blob failed.";
      return false;
    }
    wrapped->wrapped_chaps_key = wrapped_chaps_key;
  }

  // If there is a reset seed, encrypt and store it.
  if (vault_keyset.GetResetSeed().size() != 0) {
    brillo::SecureBlob wrapped_reset_seed;
    if (!LibScryptCompat::Encrypt(
            key_blobs.scrypt_wrapped_reset_seed_key->derived_key(),
            key_blobs.scrypt_wrapped_reset_seed_key->ConsumeSalt(),
            vault_keyset.GetResetSeed(), kDefaultScryptParams,
            &wrapped_reset_seed)) {
      LOG(ERROR) << "Scrypt encrypt of reset seed failed.";
      return false;
    }

    wrapped->wrapped_reset_seed = wrapped_reset_seed;
  }

  return true;
}

}  // namespace

Crypto::Crypto(Platform* platform)
    : tpm_(NULL),
      platform_(platform),
      cryptohome_key_loader_(NULL),
      disable_logging_for_tests_(false) {}

Crypto::~Crypto() {}

bool Crypto::Init(Tpm* tpm, CryptohomeKeyLoader* cryptohome_key_loader) {
  CHECK(cryptohome_key_loader)
      << "Crypto wanted to use CryptohomeKeyLoader but was not provided";
  CHECK(tpm) << "Crypto wanted to use Tpm but was not provided";
  if (tpm_ == NULL) {
    tpm_ = tpm;
  }
  cryptohome_key_loader_ = cryptohome_key_loader;
  cryptohome_key_loader_->Init();
  if (tpm_->GetLECredentialBackend() &&
      tpm_->GetLECredentialBackend()->IsSupported()) {
    le_manager_ = std::make_unique<LECredentialManagerImpl>(
        tpm_->GetLECredentialBackend(), base::FilePath(kSignInHashTreeDir));
  }
  return true;
}

CryptoError Crypto::EnsureTpm(bool reload_key) const {
  CryptoError result = CryptoError::CE_NONE;
  if (tpm_ && cryptohome_key_loader_) {
    if (reload_key || !cryptohome_key_loader_->HasCryptohomeKey()) {
      cryptohome_key_loader_->Init();
    }
  }
  return result;
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
  if (force || file_len == 0 || file_len > kSystemSaltMaxSize) {
    LOG(ERROR) << "Creating new salt at " << path.value() << " (" << force
               << ", " << file_len << ")";
    // If this salt doesn't exist, automatically create it.
    local_salt = CryptoLib::CreateSecureRandomBlob(length);
    if (!platform_->WriteSecureBlobToFileAtomicDurable(path, local_salt,
                                                       kSaltFilePermissions)) {
      LOG(ERROR) << "Could not write user salt";
      return false;
    }
  } else {
    local_salt.resize(file_len);
    if (!platform_->ReadFileToSecureBlob(path, &local_salt)) {
      LOG(ERROR) << "Could not read salt file of length " << file_len;
      return false;
    }
  }
  if (salt) {
    salt->swap(local_salt);
  }
  return true;
}

void Crypto::PasswordToPasskey(const char* password,
                               const brillo::SecureBlob& salt,
                               SecureBlob* passkey) {
  CHECK(password);

  std::string ascii_salt = CryptoLib::SecureBlobToHex(salt);
  // Convert a raw password to a password hash
  SHA256_CTX sha_context;
  SecureBlob md_value(SHA256_DIGEST_LENGTH);

  SHA256_Init(&sha_context);
  SHA256_Update(&sha_context, ascii_salt.data(), ascii_salt.length());
  SHA256_Update(&sha_context, password, strlen(password));
  SHA256_Final(md_value.data(), &sha_context);

  md_value.resize(SHA256_DIGEST_LENGTH / 2);
  SecureBlob local_passkey(SHA256_DIGEST_LENGTH);
  CryptoLib::SecureBlobToHexToBuffer(md_value, local_passkey.data(),
                                     local_passkey.size());
  passkey->swap(local_passkey);
}

bool Crypto::UnwrapVaultKeyset(const SerializedVaultKeyset& serialized,
                               const KeyBlobs& vkk_data,
                               VaultKeyset* keyset,
                               CryptoError* error) {
  bool has_vkk_key = vkk_data.vkk_key != base::nullopt &&
                     vkk_data.vkk_iv != base::nullopt &&
                     vkk_data.chaps_iv != base::nullopt;
  bool has_scrypt_key = vkk_data.scrypt_key != nullptr;
  bool successfully_unwrapped = false;

  if (has_vkk_key && !has_scrypt_key) {
    successfully_unwrapped =
        UnwrapVKKVaultKeyset(serialized, vkk_data, keyset, error);
  } else if (has_scrypt_key && !has_vkk_key) {
    successfully_unwrapped =
        UnwrapScryptVaultKeyset(serialized, vkk_data, keyset, error);
  } else {
    DLOG(FATAL) << "An invalid key combination exists";
    return false;
  }

  if (successfully_unwrapped) {
    // By this point we know that the TPM is successfully owned, everything
    // is initialized, and we were able to successfully decrypt a
    // TPM-wrapped keyset. So, for TPMs with updateable firmware, we assume
    // that it is stable (and the TPM can invalidate the old version).
    // TODO(dlunev): We shall try to get this out of cryptohome eventually.
    const bool tpm_backed =
        (serialized.flags() & SerializedVaultKeyset::TPM_WRAPPED) ||
        (serialized.flags() & SerializedVaultKeyset::LE_CREDENTIAL);
    if (tpm_backed && tpm_ != nullptr) {
      tpm_->DeclareTpmFirmwareStable();
    }
  }
  return successfully_unwrapped;
}

bool Crypto::DecryptScrypt(const SerializedVaultKeyset& serialized,
                           const SecureBlob& key,
                           CryptoError* error,
                           VaultKeyset* keyset) const {
  SecureBlob blob = SecureBlob(serialized.wrapped_keyset());
  SecureBlob decrypted(blob.size());
  if (!CryptoLib::DeprecatedDecryptScryptBlob(blob, key, &decrypted, error)) {
    LOG(ERROR) << "Wrapped keyset Scrypt decrypt failed.";
    return false;
  }

  bool chaps_key_present = serialized.has_wrapped_chaps_key();
  if (chaps_key_present) {
    SecureBlob chaps_key;
    SecureBlob wrapped_chaps_key = SecureBlob(serialized.wrapped_chaps_key());
    chaps_key.resize(wrapped_chaps_key.size());
    // Perform a Scrypt operation on wrapped chaps key.
    if (!CryptoLib::DeprecatedDecryptScryptBlob(wrapped_chaps_key, key,
                                                &chaps_key, error)) {
      LOG(ERROR) << "Chaps key scrypt decrypt failed.";
      return false;
    }
    keyset->SetChapsKey(chaps_key);
  }

  if (serialized.has_wrapped_reset_seed()) {
    SecureBlob reset_seed;
    SecureBlob wrapped_reset_seed = SecureBlob(serialized.wrapped_reset_seed());
    reset_seed.resize(wrapped_reset_seed.size());
    // Perform a Scrypt operation on wrapped reset seed.
    if (!CryptoLib::DeprecatedDecryptScryptBlob(wrapped_reset_seed, key,
                                                &reset_seed, error)) {
      LOG(ERROR) << "Reset seed scrypt decrypt failed.";
      return false;
    }
    keyset->SetResetSeed(reset_seed);
  }

  // There is a SHA hash included at the end of the decrypted blob. However,
  // scrypt already appends a MAC, so if the payload is corrupted we will fail
  // on the first call to DecryptScryptBlob.
  // TODO(crbug.com/984782): get rid of this entirely.
  if (decrypted.size() < SHA_DIGEST_LENGTH) {
    LOG(ERROR) << "Message length underflow: " << decrypted.size() << " bytes?";
    return false;
  }
  decrypted.resize(decrypted.size() - SHA_DIGEST_LENGTH);
  keyset->FromKeysBlob(decrypted);
  return true;
}

bool Crypto::NeedsPcrBinding(const uint64_t& label) const {
  DCHECK(le_manager_)
      << "le_manage_ doesn't exist when calling NeedsPcrBinding()";
  return le_manager_->NeedsPcrBinding(label);
}

bool Crypto::DecryptVaultKeyset(VaultKeyset* vault_keyset,
                                const SecureBlob& vault_key,
                                bool locked_to_single_user,
                                unsigned int* crypt_flags,
                                CryptoError* error) {
  const SerializedVaultKeyset& serialized = vault_keyset->ToSerialized();
  if (crypt_flags)
    *crypt_flags = serialized.flags();
  PopulateError(error, CryptoError::CE_NONE);

  AuthBlockState auth_state;
  if (!vault_keyset->GetAuthBlockState(&auth_state)) {
    PopulateError(error, CryptoError::CE_OTHER_CRYPTO);
    return false;
  }

  unsigned int flags = serialized.flags();
  std::unique_ptr<AuthBlock> auth_block = DeriveAuthBlock(flags);
  if (!auth_block) {
    LOG(ERROR) << "Keyset wrapped with unknown method.";
    return false;
  }

  AuthInput auth_input = {vault_key, locked_to_single_user};
  KeyBlobs vkk_data;

  if (!auth_block->Derive(auth_input, auth_state, &vkk_data, error)) {
    return false;
  }

  if (flags & SerializedVaultKeyset::LE_CREDENTIAL) {
    // This is possible to be empty if an old version of CR50 is running.
    if (vkk_data.reset_secret.has_value() &&
        !vkk_data.reset_secret.value().empty()) {
      vault_keyset->SetResetSecret(vkk_data.reset_secret.value());
    }
  }

  bool unwrapping_succeeded =
      UnwrapVaultKeyset(serialized, vkk_data, vault_keyset, error);
  if (unwrapping_succeeded) {
    ReportWrappingKeyDerivationType(auth_block->derivation_type());
  }

  return unwrapping_succeeded;
}

bool Crypto::EncryptVaultKeyset(const VaultKeyset& vault_keyset,
                                const SecureBlob& vault_key,
                                const SecureBlob& vault_key_salt,
                                const std::string& obfuscated_username,
                                AuthBlockState* out_state,
                                WrappedKeyMaterial* wrapped) const {
  std::unique_ptr<AuthBlock> auth_block = CreateAuthBlock(vault_keyset);
  if (!auth_block) {
    LOG(ERROR) << "Failed to retrieve auth block.";
    return false;
  }

  bool store_reset_seed = !vault_keyset.IsLECredential();
  base::Optional<SecureBlob> reset_secret;
  if (!vault_keyset.GetResetSecret().empty()) {
    reset_secret = vault_keyset.GetResetSecret();
  }

  AuthInput user_input = {vault_key, /*locked_to_single_user*=*/base::nullopt,
                          vault_key_salt, obfuscated_username, reset_secret};

  KeyBlobs key_blobs;
  CryptoError error;
  auto auth_state = auth_block->Create(user_input, &key_blobs, &error);
  if (auth_state == base::nullopt) {
    LOG_IF(ERROR, !disable_logging_for_tests_)
        << "Failed to create the credential: " << error;
    return false;
  }
  *out_state = auth_state.value();

  bool is_scrypt_wrapped = auth_state->has_libscrypt_compat_state() ||
                           auth_state->has_challenge_credential_state();
  if (is_scrypt_wrapped) {
    return WrapScryptVaultKeyset(vault_keyset, key_blobs, wrapped);
  }

  return WrapVaultKeysetWithAesDeprecated(vault_keyset, key_blobs,
                                          store_reset_seed, wrapped);
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
  if (!tpm_->GetRandomDataSecureBlob(kDefaultAesKeySize, aes_key)) {
    LOG(ERROR) << "GetRandomDataSecureBlob failed.";
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
  SecureBlob iv;
  if (!tpm_->GetRandomDataSecureBlob(kAesBlockSize, &iv)) {
    LOG(ERROR) << "GetRandomDataSecureBlob failed.";
    return false;
  }
  SecureBlob encrypted_data_blob;
  if (!CryptoLib::AesEncryptSpecifyBlockMode(
          data, 0, data.size(), aes_key, iv, CryptoLib::kPaddingStandard,
          CryptoLib::kCbc, &encrypted_data_blob)) {
    LOG(ERROR) << "Failed to encrypt serial data.";
    return false;
  }
  EncryptedData encrypted_pb;
  encrypted_pb.set_wrapped_key(sealed_key.data(), sealed_key.size());
  encrypted_pb.set_iv(iv.data(), iv.size());
  encrypted_pb.set_encrypted_data(encrypted_data_blob.data(),
                                  encrypted_data_blob.size());
  encrypted_pb.set_mac(ComputeEncryptedDataHmac(encrypted_pb, aes_key));
  if (!encrypted_pb.SerializeToString(encrypted_data)) {
    LOG(ERROR) << "Could not serialize data to string.";
    return false;
  }
  return true;
}

bool Crypto::UnsealKey(const std::string& encrypted_data,
                       SecureBlob* aes_key,
                       SecureBlob* sealed_key) const {
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
  std::string mac = ComputeEncryptedDataHmac(encrypted_pb, aes_key);
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
  if (!CryptoLib::AesDecryptSpecifyBlockMode(
          encrypted_data_blob, 0, encrypted_data_blob.size(), aes_key, iv,
          CryptoLib::kPaddingStandard, CryptoLib::kCbc, data)) {
    LOG(ERROR) << "Failed to decrypt encrypted data.";
    return false;
  }
  return true;
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
  int ret = le_manager_->ResetCredential(vk_reset.GetLELabel(), reset_secret);
  if (ret != LE_CRED_SUCCESS) {
    PopulateError(error, ret == LE_CRED_ERROR_INVALID_RESET_SECRET
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
  if (tpm_ == NULL || cryptohome_key_loader_ == NULL) {
    return false;
  }
  return cryptohome_key_loader_->HasCryptohomeKey();
}

bool Crypto::CanUnsealWithUserAuth() const {
  if (!tpm_)
    return false;
  if (tpm_->GetVersion() != Tpm::TPM_1_2)
    return true;
  if (!tpm_->DelegateCanResetDACounter())
    return false;
  base::Optional<bool> is_pcr_bound = tpm_->IsDelegateBoundToPcr();
  if (is_pcr_bound.has_value() && !is_pcr_bound.value())
    return true;

#if USE_DOUBLE_EXTEND_PCR_ISSUE
  return false;
#else
  return true;
#endif
}

std::unique_ptr<AuthBlock> Crypto::CreateAuthBlock(
    const VaultKeyset& vk) const {
  if (vk.IsLECredential()) {
    ReportCreateAuthBlock(AuthBlockType::kPinWeaver);
    return std::make_unique<PinWeaverAuthBlock>(le_manager_.get(),
                                                cryptohome_key_loader_);
  }

  if (vk.IsSignatureChallengeProtected()) {
    ReportCreateAuthBlock(AuthBlockType::kChallengeCredential);
    return std::make_unique<ChallengeCredentialAuthBlock>();
  }

  bool use_tpm = tpm_ && tpm_->IsOwned();
  bool with_user_auth = CanUnsealWithUserAuth();
  if (use_tpm && with_user_auth) {
    ReportCreateAuthBlock(AuthBlockType::kTpmBoundToPcr);
    return std::make_unique<TpmBoundToPcrAuthBlock>(tpm_,
                                                    cryptohome_key_loader_);
  }

  if (use_tpm && !with_user_auth) {
    ReportCreateAuthBlock(AuthBlockType::kTpmNotBoundToPcr);
    return std::make_unique<TpmNotBoundToPcrAuthBlock>(tpm_,
                                                       cryptohome_key_loader_);
  }

  ReportCreateAuthBlock(AuthBlockType::kLibScryptCompat);
  return std::make_unique<LibScryptCompatAuthBlock>();
}

std::unique_ptr<AuthBlock> Crypto::DeriveAuthBlock(int serialized_key_flags) {
  if (serialized_key_flags & SerializedVaultKeyset::LE_CREDENTIAL) {
    ReportDeriveAuthBlock(AuthBlockType::kPinWeaver);
    return std::make_unique<PinWeaverAuthBlock>(le_manager_.get(),
                                                cryptohome_key_loader_);
  } else if (serialized_key_flags &
             SerializedVaultKeyset::SIGNATURE_CHALLENGE_PROTECTED) {
    ReportDeriveAuthBlock(AuthBlockType::kChallengeCredential);
    return std::make_unique<ChallengeCredentialAuthBlock>();
  } else if (serialized_key_flags & SerializedVaultKeyset::SCRYPT_WRAPPED &&
             serialized_key_flags & SerializedVaultKeyset::TPM_WRAPPED) {
    ReportDeriveAuthBlock(AuthBlockType::kDoubleWrappedCompat);
    return std::make_unique<DoubleWrappedCompatAuthBlock>(
        tpm_, cryptohome_key_loader_);
  } else if (serialized_key_flags & SerializedVaultKeyset::TPM_WRAPPED) {
    if (serialized_key_flags & SerializedVaultKeyset::PCR_BOUND) {
      ReportDeriveAuthBlock(AuthBlockType::kTpmBoundToPcr);
      return std::make_unique<TpmBoundToPcrAuthBlock>(tpm_,
                                                      cryptohome_key_loader_);
    } else {
      ReportDeriveAuthBlock(AuthBlockType::kTpmNotBoundToPcr);
      return std::make_unique<TpmNotBoundToPcrAuthBlock>(
          tpm_, cryptohome_key_loader_);
    }
  } else if (serialized_key_flags & SerializedVaultKeyset::SCRYPT_WRAPPED) {
    ReportDeriveAuthBlock(AuthBlockType::kLibScryptCompat);
    return std::make_unique<LibScryptCompatAuthBlock>();
  }
  return nullptr;
}

}  // namespace cryptohome
