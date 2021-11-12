// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/vault_keyset.h"

#include <memory>
#include <utility>

#include <sys/types.h>
#include <crypto/sha2.h>
#include <openssl/sha.h>

#include <absl/types/variant.h>
#include <base/check.h>
#include <base/check_op.h>
#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/notreached.h>
#include <brillo/secure_blob.h>

#include "cryptohome/auth_block_state.h"
#include "cryptohome/challenge_credential_auth_block.h"
#include "cryptohome/crypto/aes.h"
#include "cryptohome/crypto/hmac.h"
#include "cryptohome/crypto/secure_blob_util.h"
#include "cryptohome/crypto/sha.h"
#include "cryptohome/crypto_error.h"
#include "cryptohome/cryptohome_common.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/double_wrapped_compat_auth_block.h"
#include "cryptohome/key_objects.h"
#include "cryptohome/le_credential_manager.h"
#include "cryptohome/libscrypt_compat.h"
#include "cryptohome/libscrypt_compat_auth_block.h"
#include "cryptohome/pin_weaver_auth_block.h"
#include "cryptohome/platform.h"
#include "cryptohome/tpm.h"
#include "cryptohome/tpm_bound_to_pcr_auth_block.h"
#include "cryptohome/tpm_ecc_auth_block.h"
#include "cryptohome/tpm_not_bound_to_pcr_auth_block.h"
#include "cryptohome/vault_keyset.pb.h"

using base::FilePath;
using brillo::SecureBlob;

namespace {
const mode_t kVaultFilePermissions = 0600;
const char kKeyLegacyPrefix[] = "legacy-";
}  // namespace

namespace cryptohome {

namespace {
struct AuthBlockFlags {
  int32_t require_flags;
  int32_t refuse_flags;
};

constexpr AuthBlockFlags kPinWeaverFlags = {
    .require_flags = SerializedVaultKeyset::LE_CREDENTIAL,
    .refuse_flags = 0,
};

constexpr AuthBlockFlags kChallengeCredentialFlags = {
    .require_flags = SerializedVaultKeyset::SIGNATURE_CHALLENGE_PROTECTED,
    .refuse_flags = 0,
};

constexpr AuthBlockFlags kDoubleWrappedCompatFlags = {
    .require_flags = SerializedVaultKeyset::SCRYPT_WRAPPED |
                     SerializedVaultKeyset::TPM_WRAPPED,
    .refuse_flags = 0,
};

constexpr AuthBlockFlags kLibScryptCompatFlags = {
    .require_flags = SerializedVaultKeyset::SCRYPT_WRAPPED,
    .refuse_flags = SerializedVaultKeyset::TPM_WRAPPED,
};

constexpr AuthBlockFlags kTpmNotBoundToPcrFlags = {
    .require_flags = SerializedVaultKeyset::TPM_WRAPPED,
    .refuse_flags = SerializedVaultKeyset::SCRYPT_WRAPPED |
                    SerializedVaultKeyset::PCR_BOUND |
                    SerializedVaultKeyset::ECC,
};

constexpr AuthBlockFlags kTpmBoundToPcrFlags = {
    .require_flags =
        SerializedVaultKeyset::TPM_WRAPPED | SerializedVaultKeyset::PCR_BOUND,
    .refuse_flags =
        SerializedVaultKeyset::SCRYPT_WRAPPED | SerializedVaultKeyset::ECC,
};

constexpr AuthBlockFlags kTpmEccFlags = {
    .require_flags = SerializedVaultKeyset::TPM_WRAPPED |
                     SerializedVaultKeyset::PCR_BOUND |
                     SerializedVaultKeyset::ECC,
    .refuse_flags = SerializedVaultKeyset::SCRYPT_WRAPPED,
};

bool MatchFlags(AuthBlockFlags auth_block_flags, int32_t flags) {
  return (flags & auth_block_flags.require_flags) ==
             auth_block_flags.require_flags &&
         (flags & auth_block_flags.refuse_flags) == 0;
}
}  // namespace

VaultKeyset::VaultKeyset()
    : platform_(NULL),
      crypto_(NULL),
      loaded_(false),
      encrypted_(false),
      flags_(0),
      legacy_index_(-1),
      auth_locked_(false) {}

VaultKeyset::~VaultKeyset() {}

void VaultKeyset::Initialize(Platform* platform, Crypto* crypto) {
  platform_ = platform;
  crypto_ = crypto;
}

void VaultKeyset::InitializeToAdd(const VaultKeyset& vault_keyset) {
  VaultKeysetKeys vault_keyset_keys;
  // This copies the encryption keys, reset_seed and chaps key.
  vault_keyset.ToKeys(&vault_keyset_keys);
  FromKeys(vault_keyset_keys);
  // Set chaps key if it exists.
  if (!vault_keyset.GetChapsKey().empty()) {
    SetChapsKey(vault_keyset.GetChapsKey());
  }

  // Set reset_seed reset_if it exists
  if (!vault_keyset.GetResetSeed().empty()) {
    SetResetSeed(vault_keyset.GetResetSeed());
  }

  // Set reset_iv if it exists.
  if (vault_keyset.HasResetIV()) {
    SetResetIV(vault_keyset.GetResetIV());
  }

  // Set FSCrypt policy version
  if (vault_keyset.HasFSCryptPolicyVersion()) {
    SetFSCryptPolicyVersion(vault_keyset.GetFSCryptPolicyVersion());
  }
}

void VaultKeyset::FromKeys(const VaultKeysetKeys& keys) {
  fek_.resize(sizeof(keys.fek));
  memcpy(fek_.data(), keys.fek, fek_.size());
  fek_sig_.resize(sizeof(keys.fek_sig));
  memcpy(fek_sig_.data(), keys.fek_sig, fek_sig_.size());
  fek_salt_.resize(sizeof(keys.fek_salt));
  memcpy(fek_salt_.data(), keys.fek_salt, fek_salt_.size());
  fnek_.resize(sizeof(keys.fnek));
  memcpy(fnek_.data(), keys.fnek, fnek_.size());
  fnek_sig_.resize(sizeof(keys.fnek_sig));
  memcpy(fnek_sig_.data(), keys.fnek_sig, fnek_sig_.size());
  fnek_salt_.resize(sizeof(keys.fnek_salt));
  memcpy(fnek_salt_.data(), keys.fnek_salt, fnek_salt_.size());
}

bool VaultKeyset::FromKeysBlob(const SecureBlob& keys_blob) {
  if (keys_blob.size() != sizeof(VaultKeysetKeys)) {
    return false;
  }
  VaultKeysetKeys keys;
  memcpy(&keys, keys_blob.data(), sizeof(keys));

  FromKeys(keys);

  brillo::SecureClearObject(keys);
  return true;
}

bool VaultKeyset::ToKeys(VaultKeysetKeys* keys) const {
  brillo::SecureClearObject(*keys);
  if (fek_.size() != sizeof(keys->fek)) {
    return false;
  }
  memcpy(keys->fek, fek_.data(), sizeof(keys->fek));
  if (fek_sig_.size() != sizeof(keys->fek_sig)) {
    return false;
  }
  memcpy(keys->fek_sig, fek_sig_.data(), sizeof(keys->fek_sig));
  if (fek_salt_.size() != sizeof(keys->fek_salt)) {
    return false;
  }
  memcpy(keys->fek_salt, fek_salt_.data(), sizeof(keys->fek_salt));
  if (fnek_.size() != sizeof(keys->fnek)) {
    return false;
  }
  memcpy(keys->fnek, fnek_.data(), sizeof(keys->fnek));
  if (fnek_sig_.size() != sizeof(keys->fnek_sig)) {
    return false;
  }
  memcpy(keys->fnek_sig, fnek_sig_.data(), sizeof(keys->fnek_sig));
  if (fnek_salt_.size() != sizeof(keys->fnek_salt)) {
    return false;
  }
  memcpy(keys->fnek_salt, fnek_salt_.data(), sizeof(keys->fnek_salt));

  return true;
}

bool VaultKeyset::ToKeysBlob(SecureBlob* keys_blob) const {
  VaultKeysetKeys keys;
  if (!ToKeys(&keys)) {
    return false;
  }

  SecureBlob local_buffer(sizeof(keys));
  memcpy(local_buffer.data(), &keys, sizeof(keys));
  keys_blob->swap(local_buffer);
  return true;
}

void VaultKeyset::CreateRandomChapsKey() {
  chaps_key_ = CreateSecureRandomBlob(CRYPTOHOME_CHAPS_KEY_LENGTH);
}

void VaultKeyset::CreateRandomResetSeed() {
  reset_seed_ = CreateSecureRandomBlob(CRYPTOHOME_RESET_SEED_LENGTH);
}

void VaultKeyset::CreateRandom() {
  CHECK(crypto_);

  fek_ = CreateSecureRandomBlob(CRYPTOHOME_DEFAULT_KEY_SIZE);
  fek_sig_ = CreateSecureRandomBlob(CRYPTOHOME_DEFAULT_KEY_SIGNATURE_SIZE);
  fek_salt_ = CreateSecureRandomBlob(CRYPTOHOME_DEFAULT_KEY_SALT_SIZE);
  fnek_ = CreateSecureRandomBlob(CRYPTOHOME_DEFAULT_KEY_SIZE);
  fnek_sig_ = CreateSecureRandomBlob(CRYPTOHOME_DEFAULT_KEY_SIGNATURE_SIZE);
  fnek_salt_ = CreateSecureRandomBlob(CRYPTOHOME_DEFAULT_KEY_SALT_SIZE);

  CreateRandomChapsKey();
  CreateRandomResetSeed();
}

bool VaultKeyset::Load(const FilePath& filename) {
  CHECK(platform_);
  brillo::Blob contents;
  if (!platform_->ReadFile(filename, &contents))
    return false;
  ResetVaultKeyset();

  SerializedVaultKeyset serialized;
  loaded_ = serialized.ParseFromArray(contents.data(), contents.size());
  // If it was parsed from file, consider it save-able too.
  source_file_.clear();
  if (loaded_) {
    encrypted_ = true;
    source_file_ = filename;
    InitializeFromSerialized(serialized);
  }
  return loaded_;
}

bool VaultKeyset::Decrypt(const SecureBlob& key,
                          bool locked_to_single_user,
                          CryptoError* crypto_error) {
  CHECK(crypto_);

  if (crypto_error)
    *crypto_error = CryptoError::CE_NONE;

  if (!loaded_) {
    if (crypto_error)
      *crypto_error = CryptoError::CE_OTHER_FATAL;
    return false;
  }

  CryptoError local_crypto_error = CryptoError::CE_NONE;
  bool ok = DecryptVaultKeyset(key, locked_to_single_user, &local_crypto_error);
  if (!ok && local_crypto_error == CryptoError::CE_TPM_COMM_ERROR) {
    ok = DecryptVaultKeyset(key, locked_to_single_user, &local_crypto_error);
  }

  if (!ok && IsLECredential() &&
      local_crypto_error == CryptoError::CE_TPM_DEFEND_LOCK) {
    // For LE credentials, if decrypting the keyset failed due to too many
    // attempts, set auth_locked=true in the keyset. Then save it for future
    // callers who can Load it w/o Decrypt'ing to check that flag.
    auth_locked_ = true;
    if (!Save(source_file_)) {
      LOG(WARNING) << "Failed to set auth_locked in VaultKeyset on disk.";
    }
  }

  // Make sure the returned error is non-empty, because sometimes
  // Crypto::DecryptVaultKeyset() doesn't fill it despite returning false. Note
  // that the value assigned below must *not* say a fatal error, as otherwise
  // this may result in removal of the cryptohome which is undesired in this
  // case.
  if (local_crypto_error == CryptoError::CE_NONE)
    local_crypto_error = CryptoError::CE_OTHER_CRYPTO;

  if (!ok && crypto_error)
    *crypto_error = local_crypto_error;
  return ok;
}

bool VaultKeyset::DecryptVaultKeyset(const SecureBlob& vault_key,
                                     bool locked_to_single_user,
                                     CryptoError* error) {
  const SerializedVaultKeyset& serialized = ToSerialized();
  PopulateError(error, CryptoError::CE_NONE);

  AuthBlockState auth_state;
  if (!GetAuthBlockState(&auth_state)) {
    PopulateError(error, CryptoError::CE_OTHER_CRYPTO);
    return false;
  }

  // TODO(crbug.com/1216659): Move AuthBlock instantiation to AuthFactor once it
  // is ready.
  std::unique_ptr<AuthBlock> auth_block = GetAuthBlockForDerivation();
  if (!auth_block) {
    LOG(ERROR) << "Keyset wrapped with unknown method.";
    return false;
  }

  AuthInput auth_input = {vault_key, locked_to_single_user};
  KeyBlobs vkk_data;
  if (!auth_block->Derive(auth_input, auth_state, &vkk_data, error)) {
    return false;
  }

  if (flags_ & SerializedVaultKeyset::LE_CREDENTIAL) {
    // This is possible to be empty if an old version of CR50 is running.
    if (vkk_data.reset_secret.has_value() &&
        !vkk_data.reset_secret.value().empty()) {
      SetResetSecret(vkk_data.reset_secret.value());
    }
  }

  bool unwrapping_succeeded = UnwrapVaultKeyset(serialized, vkk_data, error);
  if (unwrapping_succeeded) {
    ReportWrappingKeyDerivationType(auth_block->derivation_type(),
                                    CryptohomePhase::kMounted);
  }

  return unwrapping_succeeded;
}

bool VaultKeyset::UnwrapVKKVaultKeyset(const SerializedVaultKeyset& serialized,
                                       const KeyBlobs& vkk_data,
                                       CryptoError* error) {
  const SecureBlob& vkk_key = vkk_data.vkk_key.value();
  const SecureBlob& vkk_iv = vkk_data.vkk_iv.value();
  const SecureBlob& chaps_iv = vkk_data.chaps_iv.value();
  // Decrypt the keyset protobuf.
  SecureBlob local_encrypted_keyset(serialized.wrapped_keyset().begin(),
                                    serialized.wrapped_keyset().end());
  SecureBlob plain_text;

  if (!AesDecryptDeprecated(local_encrypted_keyset, vkk_key, vkk_iv,
                            &plain_text)) {
    LOG(ERROR) << "AES decryption failed for vault keyset.";
    PopulateError(error, CryptoError::CE_OTHER_CRYPTO);
    return false;
  }

  if (!FromKeysBlob(plain_text)) {
    LOG(ERROR) << "Failed to decode the keys blob.";
    PopulateError(error, CryptoError::CE_OTHER_CRYPTO);
    return false;
  }

  // Decrypt the chaps key.
  if (serialized.has_wrapped_chaps_key()) {
    SecureBlob local_wrapped_chaps_key(serialized.wrapped_chaps_key());
    SecureBlob unwrapped_chaps_key;

    if (!AesDecryptDeprecated(local_wrapped_chaps_key, vkk_key, chaps_iv,
                              &unwrapped_chaps_key)) {
      LOG(ERROR) << "AES decryption failed for chaps key.";
      PopulateError(error, CryptoError::CE_OTHER_CRYPTO);
      return false;
    }

    SetChapsKey(unwrapped_chaps_key);
  }

  // Decrypt the reset seed.
  bool is_le_credential =
      serialized.flags() & SerializedVaultKeyset::LE_CREDENTIAL;
  if (serialized.has_wrapped_reset_seed() && !is_le_credential) {
    SecureBlob unwrapped_reset_seed;
    SecureBlob local_wrapped_reset_seed =
        SecureBlob(serialized.wrapped_reset_seed());
    SecureBlob local_reset_iv = SecureBlob(serialized.reset_iv());

    if (!AesDecryptDeprecated(local_wrapped_reset_seed, vkk_key, local_reset_iv,
                              &unwrapped_reset_seed)) {
      LOG(ERROR) << "AES decryption failed for reset seed.";
      PopulateError(error, CryptoError::CE_OTHER_CRYPTO);
      return false;
    }

    SetResetSeed(unwrapped_reset_seed);
  }

  return true;
}

bool VaultKeyset::UnwrapScryptVaultKeyset(
    const SerializedVaultKeyset& serialized,
    const KeyBlobs& vkk_data,
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
    SetChapsKey(chaps_key);
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
    SetResetSeed(reset_seed);
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
  FromKeysBlob(decrypted);
  return true;
}

bool VaultKeyset::WrapVaultKeysetWithAesDeprecated(const KeyBlobs& blobs) {
  if (blobs.vkk_key == base::nullopt || blobs.vkk_iv == base::nullopt ||
      blobs.chaps_iv == base::nullopt) {
    DLOG(FATAL) << "Fields missing from KeyBlobs.";
    return false;
  }

  SecureBlob vault_blob;
  if (!ToKeysBlob(&vault_blob)) {
    LOG(ERROR) << "Failure serializing keyset to buffer";
    return false;
  }

  SecureBlob vault_cipher_text;
  if (!AesEncryptDeprecated(vault_blob, blobs.vkk_key.value(),
                            blobs.vkk_iv.value(), &vault_cipher_text)) {
    return false;
  }
  wrapped_keyset_ = vault_cipher_text;
  le_fek_iv_ = blobs.vkk_iv;

  if (GetChapsKey().size() == CRYPTOHOME_CHAPS_KEY_LENGTH) {
    SecureBlob wrapped_chaps_key;
    if (!AesEncryptDeprecated(GetChapsKey(), blobs.vkk_key.value(),
                              blobs.chaps_iv.value(), &wrapped_chaps_key)) {
      return false;
    }
    wrapped_chaps_key_ = wrapped_chaps_key;
    le_chaps_iv_ = blobs.chaps_iv;
  }

  // If a reset seed is present, encrypt and store it, else clear the field.
  if (!IsLECredential() && GetResetSeed().size() != 0) {
    const auto reset_iv = CreateSecureRandomBlob(kAesBlockSize);
    SecureBlob wrapped_reset_seed;
    if (!AesEncryptDeprecated(GetResetSeed(), blobs.vkk_key.value(), reset_iv,
                              &wrapped_reset_seed)) {
      LOG(ERROR) << "AES encryption of Reset seed failed.";
      return false;
    }
    wrapped_reset_seed_ = wrapped_reset_seed;
    reset_iv_ = reset_iv;
  }

  return true;
}

bool VaultKeyset::WrapScryptVaultKeyset(const KeyBlobs& key_blobs) {
  if (IsLECredential()) {
    LOG(ERROR) << "Low entropy credentials cannot be scrypt-wrapped.";
    return false;
  }

  brillo::SecureBlob blob;
  if (!ToKeysBlob(&blob)) {
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
  wrapped_keyset_ = cipher_text;

  if (GetChapsKey().size() == CRYPTOHOME_CHAPS_KEY_LENGTH) {
    SecureBlob wrapped_chaps_key;
    if (!LibScryptCompat::Encrypt(key_blobs.chaps_scrypt_key->derived_key(),
                                  key_blobs.chaps_scrypt_key->ConsumeSalt(),
                                  GetChapsKey(), kDefaultScryptParams,
                                  &wrapped_chaps_key)) {
      LOG(ERROR) << "Scrypt encrypt of chaps key blob failed.";
      return false;
    }
    wrapped_chaps_key_ = wrapped_chaps_key;
  }

  // If there is a reset seed, encrypt and store it.
  if (GetResetSeed().size() != 0) {
    brillo::SecureBlob wrapped_reset_seed;
    if (!LibScryptCompat::Encrypt(
            key_blobs.scrypt_wrapped_reset_seed_key->derived_key(),
            key_blobs.scrypt_wrapped_reset_seed_key->ConsumeSalt(),
            GetResetSeed(), kDefaultScryptParams, &wrapped_reset_seed)) {
      LOG(ERROR) << "Scrypt encrypt of reset seed failed.";
      return false;
    }

    wrapped_reset_seed_ = wrapped_reset_seed;
  }

  return true;
}

bool VaultKeyset::UnwrapVaultKeyset(const SerializedVaultKeyset& serialized,
                                    const KeyBlobs& vkk_data,
                                    CryptoError* error) {
  bool has_vkk_key = vkk_data.vkk_key != base::nullopt &&
                     vkk_data.vkk_iv != base::nullopt &&
                     vkk_data.chaps_iv != base::nullopt;
  bool has_scrypt_key = vkk_data.scrypt_key != nullptr;
  bool successfully_unwrapped = false;

  if (has_vkk_key && !has_scrypt_key) {
    successfully_unwrapped = UnwrapVKKVaultKeyset(serialized, vkk_data, error);
  } else if (has_scrypt_key && !has_vkk_key) {
    successfully_unwrapped =
        UnwrapScryptVaultKeyset(serialized, vkk_data, error);
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
    if (tpm_backed && crypto_->tpm() != nullptr) {
      crypto_->tpm()->DeclareTpmFirmwareStable();
    }
  }
  return successfully_unwrapped;
}

void VaultKeyset::SetTpmNotBoundToPcrState(
    const TpmNotBoundToPcrAuthBlockState& auth_state) {
  flags_ = kTpmNotBoundToPcrFlags.require_flags;
  if (auth_state.scrypt_derived) {
    flags_ |= SerializedVaultKeyset::SCRYPT_DERIVED;
  }

  if (auth_state.tpm_key.has_value()) {
    tpm_key_ = auth_state.tpm_key.value();
  }
  if (auth_state.tpm_public_key_hash.has_value()) {
    tpm_public_key_hash_ = auth_state.tpm_public_key_hash.value();
  }
  if (auth_state.salt.has_value()) {
    auth_salt_ = auth_state.salt.value();
  }
}

void VaultKeyset::SetTpmBoundToPcrState(
    const TpmBoundToPcrAuthBlockState& auth_state) {
  flags_ = kTpmBoundToPcrFlags.require_flags;
  if (auth_state.scrypt_derived) {
    flags_ |= SerializedVaultKeyset::SCRYPT_DERIVED;
  }

  if (auth_state.tpm_key.has_value()) {
    tpm_key_ = auth_state.tpm_key.value();
  }
  if (auth_state.extended_tpm_key.has_value()) {
    extended_tpm_key_ = auth_state.extended_tpm_key.value();
  }
  if (auth_state.tpm_public_key_hash.has_value()) {
    tpm_public_key_hash_ = auth_state.tpm_public_key_hash.value();
  }
  if (auth_state.salt.has_value()) {
    auth_salt_ = auth_state.salt.value();
  }
}

void VaultKeyset::SetPinWeaverState(const PinWeaverAuthBlockState& auth_state) {
  flags_ = kPinWeaverFlags.require_flags;

  if (auth_state.le_label.has_value()) {
    le_label_ = auth_state.le_label.value();
  }
  if (auth_state.salt.has_value()) {
    auth_salt_ = auth_state.salt.value();
  }
}

void VaultKeyset::SetLibScryptCompatState(
    const LibScryptCompatAuthBlockState& auth_state) {
  flags_ = kLibScryptCompatFlags.require_flags;

  // TODO(b/198394243): We should remove this because it's not actually used.
  if (auth_state.salt.has_value()) {
    auth_salt_ = auth_state.salt.value();
  }
}

void VaultKeyset::SetChallengeCredentialState(
    const ChallengeCredentialAuthBlockState& auth_state) {
  flags_ = kChallengeCredentialFlags.require_flags;

  // TODO(b/198394243): We should remove this because it's not actually used.
  if (auth_state.scrypt_state.salt.has_value()) {
    auth_salt_ = auth_state.scrypt_state.salt.value();
  }
}

void VaultKeyset::SetTpmEccState(const TpmEccAuthBlockState& auth_state) {
  // TODO(b/204384070): Move SCRYPT_DERIVED into the require_flags after all
  // user on dev channel migrated to the new flags.
  flags_ = kTpmEccFlags.require_flags | SerializedVaultKeyset::SCRYPT_DERIVED;
  if (auth_state.sealed_hvkkm.has_value()) {
    tpm_key_ = auth_state.sealed_hvkkm.value();
  }
  if (auth_state.extended_sealed_hvkkm.has_value()) {
    extended_tpm_key_ = auth_state.extended_sealed_hvkkm.value();
  }
  if (auth_state.tpm_public_key_hash.has_value()) {
    tpm_public_key_hash_ = auth_state.tpm_public_key_hash.value();
  }
  if (auth_state.auth_value_rounds.has_value()) {
    password_rounds_ = auth_state.auth_value_rounds.value();
  }
  if (auth_state.salt.has_value()) {
    auth_salt_ = auth_state.salt.value();
  }
  if (auth_state.vkk_iv.has_value()) {
    vkk_iv_ = auth_state.vkk_iv.value();
  }
}

void VaultKeyset::SetAuthBlockState(const AuthBlockState& auth_state) {
  if (auto* state =
          absl::get_if<TpmNotBoundToPcrAuthBlockState>(&auth_state.state)) {
    SetTpmNotBoundToPcrState(*state);
  } else if (auto* state =
                 absl::get_if<TpmBoundToPcrAuthBlockState>(&auth_state.state)) {
    SetTpmBoundToPcrState(*state);
  } else if (auto* state =
                 absl::get_if<PinWeaverAuthBlockState>(&auth_state.state)) {
    SetPinWeaverState(*state);
  } else if (auto* state = absl::get_if<LibScryptCompatAuthBlockState>(
                 &auth_state.state)) {
    SetLibScryptCompatState(*state);
  } else if (auto* state = absl::get_if<ChallengeCredentialAuthBlockState>(
                 &auth_state.state)) {
    SetChallengeCredentialState(*state);
  } else if (auto* state =
                 absl::get_if<TpmEccAuthBlockState>(&auth_state.state)) {
    SetTpmEccState(*state);
  } else {
    // other states are not supported.
    NOTREACHED() << "Invalid auth block state type";
    return;
  }
}

bool VaultKeyset::GetTpmBoundToPcrState(AuthBlockState* auth_state) const {
  // The AuthBlock can function without the |tpm_public_key_hash_|, but not
  // without the |tpm_key_| or | extended_tpm_key_|.
  if (!tpm_key_.has_value() || !extended_tpm_key_.has_value()) {
    return false;
  }

  TpmBoundToPcrAuthBlockState state;
  state.scrypt_derived =
      ((flags_ & SerializedVaultKeyset::SCRYPT_DERIVED) != 0);
  state.salt = auth_salt_;
  state.tpm_key = tpm_key_.value();
  state.extended_tpm_key = extended_tpm_key_.value();
  if (tpm_public_key_hash_.has_value()) {
    state.tpm_public_key_hash = tpm_public_key_hash_.value();
  }
  auth_state->state = std::move(state);
  return true;
}

bool VaultKeyset::GetTpmNotBoundToPcrState(AuthBlockState* auth_state) const {
  // The AuthBlock can function without the |tpm_public_key_hash_|, but not
  // without the |tpm_key_|.
  if (!tpm_key_.has_value()) {
    return false;
  }

  TpmNotBoundToPcrAuthBlockState state;
  state.scrypt_derived =
      ((flags_ & SerializedVaultKeyset::SCRYPT_DERIVED) != 0);
  state.salt = auth_salt_;
  if (password_rounds_.has_value()) {
    state.password_rounds = password_rounds_.value();
  }
  state.tpm_key = tpm_key_.value();
  if (tpm_public_key_hash_.has_value()) {
    state.tpm_public_key_hash = tpm_public_key_hash_.value();
  }
  auth_state->state = std::move(state);
  return true;
}

bool VaultKeyset::GetPinWeaverState(AuthBlockState* auth_state) const {
  // If the LE Label is missing, the AuthBlock cannot function.
  if (!le_label_.has_value()) {
    return false;
  }

  PinWeaverAuthBlockState state;
  state.salt = auth_salt_;
  if (le_label_.has_value()) {
    state.le_label = le_label_.value();
  }
  if (le_chaps_iv_.has_value()) {
    state.chaps_iv = le_chaps_iv_.value();
  }
  if (le_fek_iv_.has_value()) {
    state.fek_iv = le_fek_iv_.value();
  }
  auth_state->state = std::move(state);
  return true;
}

bool VaultKeyset::GetSignatureChallengeState(AuthBlockState* auth_state) const {
  AuthBlockState scrypt_state;
  if (!GetLibScryptCompatState(&scrypt_state)) {
    return false;
  }
  const auto* libscrypt_state =
      absl::get_if<LibScryptCompatAuthBlockState>(&scrypt_state.state);

  // This should never happen.
  if (libscrypt_state == nullptr) {
    NOTREACHED() << "LibScryptCompatState should have been created";
    return false;
  }

  ChallengeCredentialAuthBlockState cc_state = {
      .scrypt_state = std::move(*libscrypt_state)};
  auth_state->state = std::move(cc_state);
  return true;
}

bool VaultKeyset::GetLibScryptCompatState(AuthBlockState* auth_state) const {
  LibScryptCompatAuthBlockState state;

  state.wrapped_keyset = wrapped_keyset_;
  if (wrapped_chaps_key_.has_value()) {
    state.wrapped_chaps_key = wrapped_chaps_key_.value();
  }
  if (wrapped_reset_seed_.has_value()) {
    state.wrapped_reset_seed = wrapped_reset_seed_.value();
  }
  auth_state->state = std::move(state);
  return true;
}

bool VaultKeyset::GetDoubleWrappedCompatState(
    AuthBlockState* auth_state) const {
  AuthBlockState scrypt_state;
  if (!GetLibScryptCompatState(&scrypt_state)) {
    return false;
  }
  const auto* scrypt_sub_state =
      absl::get_if<LibScryptCompatAuthBlockState>(&scrypt_state.state);

  // This should never happen.
  if (scrypt_sub_state == nullptr) {
    NOTREACHED() << "LibScryptCompatState should have been created";
    return false;
  }

  AuthBlockState tpm_state;
  if (!GetTpmNotBoundToPcrState(&tpm_state)) {
    return false;
  }
  const auto* tpm_sub_state =
      absl::get_if<TpmNotBoundToPcrAuthBlockState>(&tpm_state.state);

  // This should never happen but handling it on the safe side.
  if (tpm_sub_state == nullptr) {
    NOTREACHED() << "TpmNotBoundToPcrAuthBlockState should have been created";
    return false;
  }

  DoubleWrappedCompatAuthBlockState state = {
      .scrypt_state = std::move(*scrypt_sub_state),
      .tpm_state = std::move(*tpm_sub_state)};

  auth_state->state = std::move(state);
  return true;
}

bool VaultKeyset::GetTpmEccState(AuthBlockState* auth_state) const {
  // The AuthBlock can function without the |tpm_public_key_hash_|, but not
  // without the |tpm_key_| or | extended_tpm_key_|.
  if (!password_rounds_.has_value() || !tpm_key_.has_value() ||
      !extended_tpm_key_.has_value() || !vkk_iv_.has_value()) {
    return false;
  }

  TpmEccAuthBlockState state;
  state.salt = auth_salt_;
  state.sealed_hvkkm = tpm_key_.value();
  state.extended_sealed_hvkkm = extended_tpm_key_.value();
  state.auth_value_rounds = password_rounds_.value();
  state.vkk_iv = vkk_iv_.value();
  if (tpm_public_key_hash_.has_value()) {
    state.tpm_public_key_hash = tpm_public_key_hash_.value();
  }
  if (wrapped_reset_seed_.has_value()) {
    state.wrapped_reset_seed = wrapped_reset_seed_.value();
  }

  auth_state->state = std::move(state);
  return true;
}

bool VaultKeyset::GetAuthBlockState(AuthBlockState* auth_state) const {
  // First case, handle a group of users with keysets that were incorrectly
  // flagged as being both TPM and scrypt wrapped.
  if (MatchFlags(kDoubleWrappedCompatFlags, flags_)) {
    return GetDoubleWrappedCompatState(auth_state);
  } else if (MatchFlags(kTpmEccFlags, flags_)) {
    return GetTpmEccState(auth_state);
  } else if (MatchFlags(kTpmBoundToPcrFlags, flags_)) {
    return GetTpmBoundToPcrState(auth_state);
  } else if (MatchFlags(kTpmNotBoundToPcrFlags, flags_)) {
    return GetTpmNotBoundToPcrState(auth_state);
  } else if (MatchFlags(kPinWeaverFlags, flags_)) {
    return GetPinWeaverState(auth_state);
  } else if (MatchFlags(kChallengeCredentialFlags, flags_)) {
    return GetSignatureChallengeState(auth_state);
  } else if (MatchFlags(kLibScryptCompatFlags, flags_)) {
    return GetLibScryptCompatState(auth_state);
  } else {
    LOG(ERROR) << "Unknown auth block type for flags " << flags_;
    return false;
  }
}

bool VaultKeyset::Encrypt(const SecureBlob& key,
                          const std::string& obfuscated_username) {
  CHECK(crypto_);

  // This generates the reset secret for PinWeaver credentials. Doing it per
  // secret is confusing and difficult to maintain. It's necessary so that
  // different credentials can all maintain  the same reset secret (i.e. the
  // password resets the PIN), without storing said secret in the clear. In the
  // USS key hierarchy, only one reset secret will exist.
  if (IsLECredential()) {
    // For new users, a reset seed is stored in the VaultKeyset, which is
    // derived into the reset secret.
    if (reset_seed_.empty()) {
      LOG(ERROR) << "The VaultKeyset doesn't have a reset seed, so we can't"
                    " set up an LE credential.";
      return false;
    }

    reset_salt_ = CreateSecureRandomBlob(kAesBlockSize);
    reset_secret_ = HmacSha256(reset_salt_.value(), reset_seed_);

    // crbug.com/1224150: When an LE credential is resaved, that means the user
    // authenticated successfully. In this case, auth_locked policy must always
    // be set to false. Otherwise when a user enters their password, and
    // PinWeaver unlocks the LE Credential, this field will remain set to true
    // and PIN is never usable by Chrome.
    auth_locked_ = false;
  }

  AuthBlockState auth_block_state;
  encrypted_ = EncryptVaultKeyset(key, obfuscated_username, &auth_block_state);

  if (encrypted_) {
    SetAuthBlockState(auth_block_state);
  }

  return encrypted_;
}

bool VaultKeyset::EncryptVaultKeyset(const SecureBlob& vault_key,
                                     const std::string& obfuscated_username,
                                     AuthBlockState* out_state) {
  // TODO(crbug.com/1216659): Move AuthBlock instantiation to AuthFactor once it
  // is ready.
  std::unique_ptr<AuthBlock> auth_block = GetAuthBlockForCreation();
  if (!auth_block) {
    LOG(ERROR) << "Failed to retrieve auth block.";
    return false;
  }

  base::Optional<SecureBlob> reset_secret;
  if (!GetResetSecret().empty()) {
    reset_secret = GetResetSecret();
  }

  AuthInput user_input = {vault_key, /*locked_to_single_user*=*/base::nullopt,
                          obfuscated_username, reset_secret};

  KeyBlobs key_blobs;
  CryptoError error;
  auto auth_state = auth_block->Create(user_input, &key_blobs, &error);
  if (auth_state == base::nullopt) {
    LOG(ERROR) << "Failed to create the credential: " << error;
    return false;
  }
  *out_state = auth_state.value();

  bool wrapping_succeeded;
  bool is_scrypt_wrapped =
      absl::holds_alternative<LibScryptCompatAuthBlockState>(
          auth_state->state) ||
      absl::holds_alternative<ChallengeCredentialAuthBlockState>(
          auth_state->state);
  if (is_scrypt_wrapped) {
    wrapping_succeeded = WrapScryptVaultKeyset(key_blobs);
  } else {
    wrapping_succeeded = WrapVaultKeysetWithAesDeprecated(key_blobs);
  }

  // Report wrapping key type to UMA
  if (wrapping_succeeded) {
    ReportWrappingKeyDerivationType(auth_block->derivation_type(),
                                    CryptohomePhase::kCreated);
  }

  return wrapping_succeeded;
}

// TODO(crbug.com/1216659): Move AuthBlock to AuthFactor once it is ready.
std::unique_ptr<AuthBlock> VaultKeyset::GetAuthBlockForCreation() const {
  if (IsLECredential()) {
    ReportCreateAuthBlock(AuthBlockType::kPinWeaver);
    return std::make_unique<PinWeaverAuthBlock>(
        crypto_->le_manager(), crypto_->cryptohome_keys_manager());
  }

  if (IsSignatureChallengeProtected()) {
    ReportCreateAuthBlock(AuthBlockType::kChallengeCredential);
    return std::make_unique<ChallengeCredentialAuthBlock>();
  }
  bool use_tpm = crypto_->tpm() && crypto_->tpm()->IsOwned();
  bool with_user_auth = crypto_->CanUnsealWithUserAuth();
  bool has_ecc_key = crypto_->cryptohome_keys_manager() &&
                     crypto_->cryptohome_keys_manager()->HasCryptohomeKey(
                         CryptohomeKeyType::kECC);

  if (use_tpm && with_user_auth && has_ecc_key) {
    ReportCreateAuthBlock(AuthBlockType::kTpmEcc);
    return std::make_unique<TpmEccAuthBlock>(
        crypto_->tpm(), crypto_->cryptohome_keys_manager());
  }

  if (use_tpm && with_user_auth && !has_ecc_key) {
    ReportCreateAuthBlock(AuthBlockType::kTpmBoundToPcr);
    return std::make_unique<TpmBoundToPcrAuthBlock>(
        crypto_->tpm(), crypto_->cryptohome_keys_manager());
  }

  if (use_tpm && !with_user_auth) {
    ReportCreateAuthBlock(AuthBlockType::kTpmNotBoundToPcr);
    return std::make_unique<TpmNotBoundToPcrAuthBlock>(
        crypto_->tpm(), crypto_->cryptohome_keys_manager());
  }

  ReportCreateAuthBlock(AuthBlockType::kLibScryptCompat);
  return std::make_unique<LibScryptCompatAuthBlock>();
}

// TODO(crbug.com/1216659): Move AuthBlock to AuthFactor once it is ready.
std::unique_ptr<AuthBlock> VaultKeyset::GetAuthBlockForDerivation() {
  if (MatchFlags(kPinWeaverFlags, flags_)) {
    ReportDeriveAuthBlock(AuthBlockType::kPinWeaver);
    return std::make_unique<PinWeaverAuthBlock>(
        crypto_->le_manager(), crypto_->cryptohome_keys_manager());
  } else if (MatchFlags(kChallengeCredentialFlags, flags_)) {
    ReportDeriveAuthBlock(AuthBlockType::kChallengeCredential);
    return std::make_unique<ChallengeCredentialAuthBlock>();
  } else if (MatchFlags(kDoubleWrappedCompatFlags, flags_)) {
    ReportDeriveAuthBlock(AuthBlockType::kDoubleWrappedCompat);
    return std::make_unique<DoubleWrappedCompatAuthBlock>(
        crypto_->tpm(), crypto_->cryptohome_keys_manager());
  } else if (MatchFlags(kTpmEccFlags, flags_)) {
    ReportDeriveAuthBlock(AuthBlockType::kTpmEcc);
    return std::make_unique<TpmEccAuthBlock>(
        crypto_->tpm(), crypto_->cryptohome_keys_manager());
  } else if (MatchFlags(kTpmBoundToPcrFlags, flags_)) {
    ReportDeriveAuthBlock(AuthBlockType::kTpmBoundToPcr);
    return std::make_unique<TpmBoundToPcrAuthBlock>(
        crypto_->tpm(), crypto_->cryptohome_keys_manager());
  } else if (MatchFlags(kTpmNotBoundToPcrFlags, flags_)) {
    ReportDeriveAuthBlock(AuthBlockType::kTpmNotBoundToPcr);
    return std::make_unique<TpmNotBoundToPcrAuthBlock>(
        crypto_->tpm(), crypto_->cryptohome_keys_manager());
  } else if (MatchFlags(kLibScryptCompatFlags, flags_)) {
    ReportDeriveAuthBlock(AuthBlockType::kLibScryptCompat);
    return std::make_unique<LibScryptCompatAuthBlock>();
  }
  return nullptr;
}

bool VaultKeyset::Save(const FilePath& filename) {
  CHECK(platform_);
  if (!encrypted_)
    return false;
  SerializedVaultKeyset serialized = ToSerialized();

  brillo::Blob contents(serialized.ByteSizeLong());
  google::protobuf::uint8* buf =
      static_cast<google::protobuf::uint8*>(contents.data());
  serialized.SerializeWithCachedSizesToArray(buf);

  bool ok = platform_->WriteFileAtomicDurable(filename, contents,
                                              kVaultFilePermissions);
  return ok;
}

std::string VaultKeyset::GetLabel() const {
  if (key_data_.has_value() && !key_data_->label().empty()) {
    return key_data_->label();
  }
  // Fallback for legacy keys, for which the label has to be inferred from the
  // index number.
  return base::StringPrintf("%s%d", kKeyLegacyPrefix, legacy_index_);
}

bool VaultKeyset::IsLECredential() const {
  if (key_data_.has_value()) {
    return key_data_->policy().low_entropy_credential();
  }
  return false;
}

bool VaultKeyset::IsSignatureChallengeProtected() const {
  return flags_ & SerializedVaultKeyset::SIGNATURE_CHALLENGE_PROTECTED;
}

bool VaultKeyset::HasTpmPublicKeyHash() const {
  return tpm_public_key_hash_.has_value();
}

const brillo::SecureBlob& VaultKeyset::GetTpmPublicKeyHash() const {
  DCHECK(tpm_public_key_hash_.has_value());
  return tpm_public_key_hash_.value();
}

void VaultKeyset::SetTpmPublicKeyHash(const brillo::SecureBlob& hash) {
  tpm_public_key_hash_ = hash;
}

bool VaultKeyset::HasPasswordRounds() const {
  return password_rounds_.has_value();
}

int32_t VaultKeyset::GetPasswordRounds() const {
  DCHECK(password_rounds_.has_value());
  return password_rounds_.value();
}

// TODO(b/205759690, dlunev): can be removed after a stepping stone release.
bool VaultKeyset::HasLastActivityTimestamp() const {
  return last_activity_timestamp_.has_value();
}

// TODO(b/205759690, dlunev): can be removed after a stepping stone release.
int64_t VaultKeyset::GetLastActivityTimestamp() const {
  DCHECK(last_activity_timestamp_.has_value());
  return last_activity_timestamp_.value();
}

bool VaultKeyset::HasKeyData() const {
  return key_data_.has_value();
}

void VaultKeyset::SetKeyData(const KeyData& key_data) {
  key_data_ = key_data;
}

void VaultKeyset::ClearKeyData() {
  key_data_.reset();
}

const KeyData& VaultKeyset::GetKeyData() const {
  DCHECK(key_data_.has_value());
  return key_data_.value();
}

void VaultKeyset::SetResetIV(const brillo::SecureBlob& iv) {
  reset_iv_ = iv;
}

bool VaultKeyset::HasResetIV() const {
  return reset_iv_.has_value();
}

const brillo::SecureBlob& VaultKeyset::GetResetIV() const {
  DCHECK(reset_iv_.has_value());
  return reset_iv_.value();
}

void VaultKeyset::SetLowEntropyCredential(bool is_le_cred) {
  if (!key_data_.has_value()) {
    key_data_ = KeyData();
  }
  key_data_->mutable_policy()->set_low_entropy_credential(is_le_cred);
}

void VaultKeyset::SetKeyDataLabel(const std::string& key_label) {
  if (!key_data_.has_value()) {
    key_data_ = KeyData();
  }
  key_data_->set_label(key_label);
}

void VaultKeyset::SetLELabel(uint64_t label) {
  le_label_ = label;
}

bool VaultKeyset::HasLELabel() const {
  return le_label_.has_value();
}

uint64_t VaultKeyset::GetLELabel() const {
  DCHECK(le_label_.has_value());
  return le_label_.value();
}

void VaultKeyset::SetLEFekIV(const brillo::SecureBlob& iv) {
  le_fek_iv_ = iv;
}

bool VaultKeyset::HasLEFekIV() const {
  return le_fek_iv_.has_value();
}

const brillo::SecureBlob& VaultKeyset::GetLEFekIV() const {
  DCHECK(le_fek_iv_.has_value());
  return le_fek_iv_.value();
}

void VaultKeyset::SetLEChapsIV(const brillo::SecureBlob& iv) {
  le_chaps_iv_ = iv;
}

bool VaultKeyset::HasLEChapsIV() const {
  return le_chaps_iv_.has_value();
}

const brillo::SecureBlob& VaultKeyset::GetLEChapsIV() const {
  DCHECK(le_chaps_iv_.has_value());
  return le_chaps_iv_.value();
}

void VaultKeyset::SetResetSalt(const brillo::SecureBlob& reset_salt) {
  reset_salt_ = reset_salt;
}

bool VaultKeyset::HasResetSalt() const {
  return reset_salt_.has_value();
}

const brillo::SecureBlob& VaultKeyset::GetResetSalt() const {
  DCHECK(reset_salt_.has_value());
  return reset_salt_.value();
}

void VaultKeyset::SetFSCryptPolicyVersion(int32_t policy_version) {
  fscrypt_policy_version_ = policy_version;
}

bool VaultKeyset::HasFSCryptPolicyVersion() const {
  return fscrypt_policy_version_.has_value();
}

int32_t VaultKeyset::GetFSCryptPolicyVersion() const {
  DCHECK(fscrypt_policy_version_.has_value());
  return fscrypt_policy_version_.value();
}

void VaultKeyset::SetWrappedKeyset(const brillo::SecureBlob& wrapped_keyset) {
  wrapped_keyset_ = wrapped_keyset;
}

const brillo::SecureBlob& VaultKeyset::GetWrappedKeyset() const {
  return wrapped_keyset_;
}

bool VaultKeyset::HasWrappedChapsKey() const {
  return wrapped_chaps_key_.has_value();
}

void VaultKeyset::SetWrappedChapsKey(
    const brillo::SecureBlob& wrapped_chaps_key) {
  wrapped_chaps_key_ = wrapped_chaps_key;
}

const brillo::SecureBlob& VaultKeyset::GetWrappedChapsKey() const {
  DCHECK(wrapped_chaps_key_.has_value());
  return wrapped_chaps_key_.value();
}

void VaultKeyset::ClearWrappedChapsKey() {
  wrapped_chaps_key_.reset();
}

bool VaultKeyset::HasTPMKey() const {
  return tpm_key_.has_value();
}

void VaultKeyset::SetTPMKey(const brillo::SecureBlob& tpm_key) {
  tpm_key_ = tpm_key;
}

const brillo::SecureBlob& VaultKeyset::GetTPMKey() const {
  DCHECK(tpm_key_.has_value());
  return tpm_key_.value();
}

bool VaultKeyset::HasExtendedTPMKey() const {
  return extended_tpm_key_.has_value();
}

void VaultKeyset::SetExtendedTPMKey(
    const brillo::SecureBlob& extended_tpm_key) {
  extended_tpm_key_ = extended_tpm_key;
}

const brillo::SecureBlob& VaultKeyset::GetExtendedTPMKey() const {
  DCHECK(extended_tpm_key_.has_value());
  return extended_tpm_key_.value();
}

bool VaultKeyset::HasWrappedResetSeed() const {
  return wrapped_reset_seed_.has_value();
}

void VaultKeyset::SetWrappedResetSeed(
    const brillo::SecureBlob& wrapped_reset_seed) {
  wrapped_reset_seed_ = wrapped_reset_seed;
}

const brillo::SecureBlob& VaultKeyset::GetWrappedResetSeed() const {
  DCHECK(wrapped_reset_seed_.has_value());
  return wrapped_reset_seed_.value();
}

bool VaultKeyset::HasSignatureChallengeInfo() const {
  return signature_challenge_info_.has_value();
}

const SerializedVaultKeyset::SignatureChallengeInfo&
VaultKeyset::GetSignatureChallengeInfo() const {
  DCHECK(signature_challenge_info_.has_value());
  return signature_challenge_info_.value();
}

void VaultKeyset::SetSignatureChallengeInfo(
    const SerializedVaultKeyset::SignatureChallengeInfo& info) {
  signature_challenge_info_ = info;
}

void VaultKeyset::SetChapsKey(const brillo::SecureBlob& chaps_key) {
  CHECK(chaps_key.size() == CRYPTOHOME_CHAPS_KEY_LENGTH);
  chaps_key_ = chaps_key;
}

void VaultKeyset::ClearChapsKey() {
  CHECK(chaps_key_.size() == CRYPTOHOME_CHAPS_KEY_LENGTH);
  chaps_key_.clear();
  chaps_key_.resize(0);
}

void VaultKeyset::SetResetSeed(const brillo::SecureBlob& reset_seed) {
  CHECK_EQ(reset_seed.size(), CRYPTOHOME_RESET_SEED_LENGTH);
  reset_seed_ = reset_seed;
}

void VaultKeyset::SetResetSecret(const brillo::SecureBlob& reset_secret) {
  CHECK_EQ(reset_secret.size(), CRYPTOHOME_RESET_SEED_LENGTH);
  reset_secret_ = reset_secret;
}

SerializedVaultKeyset VaultKeyset::ToSerialized() const {
  SerializedVaultKeyset serialized;
  serialized.set_flags(flags_);
  serialized.set_salt(auth_salt_.data(), auth_salt_.size());
  serialized.set_wrapped_keyset(wrapped_keyset_.data(), wrapped_keyset_.size());

  if (tpm_key_.has_value()) {
    serialized.set_tpm_key(tpm_key_->data(), tpm_key_->size());
  }

  if (tpm_public_key_hash_.has_value()) {
    serialized.set_tpm_public_key_hash(tpm_public_key_hash_->data(),
                                       tpm_public_key_hash_->size());
  }

  if (password_rounds_.has_value()) {
    serialized.set_password_rounds(password_rounds_.value());
  }

  if (key_data_.has_value()) {
    *(serialized.mutable_key_data()) = key_data_.value();
  }

  serialized.mutable_key_data()->mutable_policy()->set_auth_locked(
      auth_locked_);

  if (wrapped_chaps_key_.has_value()) {
    serialized.set_wrapped_chaps_key(wrapped_chaps_key_->data(),
                                     wrapped_chaps_key_->size());
  }

  if (wrapped_reset_seed_.has_value()) {
    serialized.set_wrapped_reset_seed(wrapped_reset_seed_->data(),
                                      wrapped_reset_seed_->size());
  }

  if (reset_iv_.has_value()) {
    serialized.set_reset_iv(reset_iv_->data(), reset_iv_->size());
  }

  if (le_label_.has_value()) {
    serialized.set_le_label(le_label_.value());
  }

  if (le_fek_iv_.has_value()) {
    serialized.set_le_fek_iv(le_fek_iv_->data(), le_fek_iv_->size());
  }

  if (le_chaps_iv_.has_value()) {
    serialized.set_le_chaps_iv(le_chaps_iv_->data(), le_chaps_iv_->size());
  }

  if (reset_salt_.has_value()) {
    serialized.set_reset_salt(reset_salt_->data(), reset_salt_->size());
  }

  if (signature_challenge_info_.has_value()) {
    *(serialized.mutable_signature_challenge_info()) =
        signature_challenge_info_.value();
  }

  if (extended_tpm_key_.has_value()) {
    serialized.set_extended_tpm_key(extended_tpm_key_->data(),
                                    extended_tpm_key_->size());
  }

  if (fscrypt_policy_version_.has_value()) {
    serialized.set_fscrypt_policy_version(fscrypt_policy_version_.value());
  }

  if (vkk_iv_.has_value()) {
    serialized.set_vkk_iv(vkk_iv_->data(), vkk_iv_->size());
  }

  return serialized;
}

void VaultKeyset::ResetVaultKeyset() {
  flags_ = -1;
  auth_salt_.clear();
  legacy_index_ = -1;
  tpm_public_key_hash_.reset();
  password_rounds_.reset();
  // TODO(b/205759690, dlunev): can be removed after a stepping stone release.
  last_activity_timestamp_.reset();
  key_data_.reset();
  reset_iv_.reset();
  le_label_.reset();
  le_fek_iv_.reset();
  le_chaps_iv_.reset();
  reset_salt_.reset();
  fscrypt_policy_version_.reset();
  wrapped_keyset_.clear();
  wrapped_chaps_key_.reset();
  tpm_key_.reset();
  extended_tpm_key_.reset();
  wrapped_reset_seed_.reset();
  signature_challenge_info_.reset();
  fek_.clear();
  fek_sig_.clear();
  fek_salt_.clear();
  fnek_.clear();
  fnek_sig_.clear();
  fnek_salt_.clear();
  chaps_key_.clear();
  reset_seed_.clear();
  reset_secret_.clear();
}

void VaultKeyset::InitializeFromSerialized(
    const SerializedVaultKeyset& serialized) {
  flags_ = serialized.flags();
  auth_salt_ =
      brillo::SecureBlob(serialized.salt().begin(), serialized.salt().end());

  wrapped_keyset_ = brillo::SecureBlob(serialized.wrapped_keyset().begin(),
                                       serialized.wrapped_keyset().end());

  if (serialized.has_tpm_key()) {
    tpm_key_ = brillo::SecureBlob(serialized.tpm_key().begin(),
                                  serialized.tpm_key().end());
  }

  if (serialized.has_tpm_public_key_hash()) {
    tpm_public_key_hash_ =
        brillo::SecureBlob(serialized.tpm_public_key_hash().begin(),
                           serialized.tpm_public_key_hash().end());
  }

  if (serialized.has_password_rounds()) {
    password_rounds_ = serialized.password_rounds();
  }

  // TODO(b/205759690, dlunev): can be removed after a stepping stone release.
  if (serialized.has_last_activity_timestamp()) {
    last_activity_timestamp_ = serialized.last_activity_timestamp();
  }

  if (serialized.has_key_data()) {
    key_data_ = serialized.key_data();

    auth_locked_ = serialized.key_data().policy().auth_locked();

    // For LECredentials, set the key policy appropriately.
    // TODO(crbug.com/832398): get rid of having two ways to identify an
    // LECredential: LE_CREDENTIAL and key_data.policy.low_entropy_credential.
    if (flags_ & SerializedVaultKeyset::LE_CREDENTIAL) {
      key_data_->mutable_policy()->set_low_entropy_credential(true);
    }
  }

  if (serialized.has_wrapped_chaps_key()) {
    wrapped_chaps_key_ =
        brillo::SecureBlob(serialized.wrapped_chaps_key().begin(),
                           serialized.wrapped_chaps_key().end());
  }

  if (serialized.has_wrapped_reset_seed()) {
    wrapped_reset_seed_ =
        brillo::SecureBlob(serialized.wrapped_reset_seed().begin(),
                           serialized.wrapped_reset_seed().end());
  }

  if (serialized.has_reset_iv()) {
    reset_iv_ = brillo::SecureBlob(serialized.reset_iv().begin(),
                                   serialized.reset_iv().end());
  }

  if (serialized.has_le_label()) {
    le_label_ = serialized.le_label();
  }

  if (serialized.has_le_fek_iv()) {
    le_fek_iv_ = brillo::SecureBlob(serialized.le_fek_iv().begin(),
                                    serialized.le_fek_iv().end());
  }

  if (serialized.has_le_chaps_iv()) {
    le_chaps_iv_ = brillo::SecureBlob(serialized.le_chaps_iv().begin(),
                                      serialized.le_chaps_iv().end());
  }

  if (serialized.has_reset_salt()) {
    reset_salt_ = brillo::SecureBlob(serialized.reset_salt().begin(),
                                     serialized.reset_salt().end());
  }

  if (serialized.has_signature_challenge_info()) {
    signature_challenge_info_ = serialized.signature_challenge_info();
  }

  if (serialized.has_extended_tpm_key()) {
    extended_tpm_key_ =
        brillo::SecureBlob(serialized.extended_tpm_key().begin(),
                           serialized.extended_tpm_key().end());
  }

  if (serialized.has_fscrypt_policy_version()) {
    fscrypt_policy_version_ = serialized.fscrypt_policy_version();
  }

  if (serialized.has_vkk_iv()) {
    vkk_iv_ = brillo::SecureBlob(serialized.vkk_iv().begin(),
                                 serialized.vkk_iv().end());
  }
}

const base::FilePath& VaultKeyset::GetSourceFile() const {
  return source_file_;
}

void VaultKeyset::SetAuthLocked(bool locked) {
  auth_locked_ = locked;
}

bool VaultKeyset::GetAuthLocked() const {
  return auth_locked_;
}

void VaultKeyset::SetFlags(int32_t flags) {
  flags_ = flags;
}

int32_t VaultKeyset::GetFlags() const {
  return flags_;
}

void VaultKeyset::SetLegacyIndex(int index) {
  legacy_index_ = index;
}

const int VaultKeyset::GetLegacyIndex() const {
  return legacy_index_;
}

const brillo::SecureBlob& VaultKeyset::GetFek() const {
  return fek_;
}

const brillo::SecureBlob& VaultKeyset::GetFekSig() const {
  return fek_sig_;
}

const brillo::SecureBlob& VaultKeyset::GetFekSalt() const {
  return fek_salt_;
}

const brillo::SecureBlob& VaultKeyset::GetFnek() const {
  return fnek_;
}

const brillo::SecureBlob& VaultKeyset::GetFnekSig() const {
  return fnek_sig_;
}

const brillo::SecureBlob& VaultKeyset::GetFnekSalt() const {
  return fnek_salt_;
}

const brillo::SecureBlob& VaultKeyset::GetChapsKey() const {
  return chaps_key_;
}

const brillo::SecureBlob& VaultKeyset::GetResetSeed() const {
  return reset_seed_;
}

const brillo::SecureBlob& VaultKeyset::GetResetSecret() const {
  return reset_secret_;
}

}  // namespace cryptohome
