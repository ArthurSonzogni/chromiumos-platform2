// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/vault_keyset.h"

#include <sys/types.h>

#include <optional>
#include <utility>
#include <variant>

#include <base/check.h>
#include <base/check_op.h>
#include <base/files/file_path.h>
#include <base/logging.h>
#include <brillo/secure_blob.h>
#include <crypto/sha2.h>
#include <libhwsec-foundation/crypto/aes.h>
#include <libhwsec-foundation/crypto/hmac.h>
#include <libhwsec-foundation/crypto/libscrypt_compat.h>
#include <libhwsec-foundation/crypto/secure_blob_util.h>
#include <libhwsec-foundation/crypto/sha.h>
#include <libstorage/platform/platform.h>
#include <openssl/sha.h>

#include "cryptohome/auth_blocks/auth_block_utils.h"
#include "cryptohome/crypto_error.h"
#include "cryptohome/cryptohome_common.h"
#include "cryptohome/error/locations.h"
#include "cryptohome/flatbuffer_schemas/auth_block_state.h"
#include "cryptohome/key_objects.h"
#include "cryptohome/signature_sealing/structures_proto.h"
#include "cryptohome/storage/file_system_keyset.h"
#include "cryptohome/vault_keyset.pb.h"

namespace cryptohome {
namespace {

using ::brillo::Blob;
using ::brillo::SecureBlob;
using ::cryptohome::error::CryptohomeCryptoError;
using ::cryptohome::error::CryptohomeError;
using ::cryptohome::error::ErrorActionSet;
using ::cryptohome::error::PossibleAction;
using ::cryptohome::error::PrimaryAction;
using ::hwsec_foundation::AesDecryptDeprecated;
using ::hwsec_foundation::AesEncryptDeprecated;
using ::hwsec_foundation::CreateRandomBlob;
using ::hwsec_foundation::CreateSecureRandomBlob;
using ::hwsec_foundation::kAesBlockSize;
using ::hwsec_foundation::kDefaultScryptParams;
using ::hwsec_foundation::LibScryptCompat;
using ::hwsec_foundation::Sha1;
using ::hwsec_foundation::status::MakeStatus;
using ::hwsec_foundation::status::OkStatus;

const mode_t kVaultFilePermissions = 0600;
const char kKeyLegacyPrefix[] = "legacy-";

// Offsets for all of the fields within a key blob. The layout of fields is:
//    (FEK, signature, salt, FNEK, signature, salt)
// The fields contain no padding so each offset is just the prior field offset
// plus the length of the prior field.
constexpr size_t kKeyBlobFekOffset = 0;
constexpr size_t kKeyBlobFekSigOffset =
    kKeyBlobFekOffset + kCryptohomeDefaultKeySize;
constexpr size_t kKeyBlobFekSaltOffset =
    kKeyBlobFekSigOffset + kCryptohomeDefaultKeySignatureSize;
constexpr size_t kKeyBlobFnekOffset =
    kKeyBlobFekSaltOffset + kCryptohomeDefaultKeySaltSize;
constexpr size_t kKeyBlobFnekSigOffset =
    kKeyBlobFnekOffset + kCryptohomeDefaultKeySize;
constexpr size_t kKeyBlobFnekSaltOffset =
    kKeyBlobFnekSigOffset + kCryptohomeDefaultKeySignatureSize;
constexpr size_t kKeyBlobEndOffset =
    kKeyBlobFnekSaltOffset + kCryptohomeDefaultKeySaltSize;

}  // namespace

void VaultKeyset::Initialize(libstorage::Platform* platform, Crypto* crypto) {
  platform_ = platform;
  crypto_ = crypto;
  backup_vk_ = false;
}

void VaultKeyset::InitializeAsBackup(libstorage::Platform* platform,
                                     Crypto* crypto) {
  platform_ = platform;
  crypto_ = crypto;
  backup_vk_ = true;
}

void VaultKeyset::InitializeToAdd(const VaultKeyset& vault_keyset) {
  // This copies the encryption keys, reset_seed and chaps key.
  brillo::SecureBlob buffer;
  CHECK(vault_keyset.ToKeysBlob(&buffer));
  CHECK(FromKeysBlob(buffer));

  // Set chaps key if it exists.
  if (!vault_keyset.GetChapsKey().empty()) {
    SetChapsKey(vault_keyset.GetChapsKey());
  }

  // Set reset_seed reset_if it exists
  if (!vault_keyset.GetResetSeed().empty()) {
    SetResetSeed(vault_keyset.GetResetSeed());
  }

  // Set reset_iv if it exists.
  if (vault_keyset.reset_iv_.has_value()) {
    SetResetIV(vault_keyset.GetResetIV());
  }

  // Set FSCrypt policy version
  if (vault_keyset.fscrypt_policy_version_.has_value()) {
    SetFSCryptPolicyVersion(vault_keyset.GetFSCryptPolicyVersion());
  }

  // Mark migrated if copied from a migrated keyset.
  if (vault_keyset.IsMigrated()) {
    backup_vk_ = true;
    migrated_vk_ = true;
  }
}

bool VaultKeyset::FromKeysBlob(const SecureBlob& keys_blob) {
  if (keys_blob.size() != kKeyBlobEndOffset) {
    return false;
  }
  auto copy_from_offset = [&keys_blob](brillo::SecureBlob& dest, size_t size,
                                       size_t offset) {
    dest.resize(size);
    memcpy(dest.data(), keys_blob.data() + offset, size);
  };
  copy_from_offset(fek_, kCryptohomeDefaultKeySize, kKeyBlobFekOffset);
  copy_from_offset(fek_sig_, kCryptohomeDefaultKeySignatureSize,
                   kKeyBlobFekSigOffset);
  copy_from_offset(fek_salt_, kCryptohomeDefaultKeySaltSize,
                   kKeyBlobFekSaltOffset);
  copy_from_offset(fnek_, kCryptohomeDefaultKeySize, kKeyBlobFnekOffset);
  copy_from_offset(fnek_sig_, kCryptohomeDefaultKeySignatureSize,
                   kKeyBlobFnekSigOffset);
  copy_from_offset(fnek_salt_, kCryptohomeDefaultKeySaltSize,
                   kKeyBlobFnekSaltOffset);
  return true;
}

bool VaultKeyset::ToKeysBlob(SecureBlob* keys_blob) const {
  SecureBlob local_buffer(kKeyBlobEndOffset);
  auto copy_to_offset = [&local_buffer](const brillo::SecureBlob& src,
                                        size_t size, size_t offset) {
    if (src.size() != size) {
      return false;
    }
    memcpy(local_buffer.data() + offset, src.data(), size);
    return true;
  };
  bool success =
      copy_to_offset(fek_, kCryptohomeDefaultKeySize, kKeyBlobFekOffset) &&
      copy_to_offset(fek_sig_, kCryptohomeDefaultKeySignatureSize,
                     kKeyBlobFekSigOffset) &&
      copy_to_offset(fek_salt_, kCryptohomeDefaultKeySaltSize,
                     kKeyBlobFekSaltOffset) &&
      copy_to_offset(fnek_, kCryptohomeDefaultKeySize, kKeyBlobFnekOffset) &&
      copy_to_offset(fnek_sig_, kCryptohomeDefaultKeySignatureSize,
                     kKeyBlobFnekSigOffset) &&
      copy_to_offset(fnek_salt_, kCryptohomeDefaultKeySaltSize,
                     kKeyBlobFnekSaltOffset);
  if (!success) {
    return false;
  }
  *keys_blob = std::move(local_buffer);
  return true;
}

void VaultKeyset::CreateRandomChapsKey() {
  chaps_key_ = CreateSecureRandomBlob(kCryptohomeChapsKeyLength);
}

void VaultKeyset::CreateRandomResetSeed() {
  reset_seed_ = CreateSecureRandomBlob(kCryptohomeResetSeedLength);
}

void VaultKeyset::CreateFromFileSystemKeyset(
    const FileSystemKeyset& file_system_keyset) {
  fek_ = file_system_keyset.Key().fek;
  fek_salt_ = file_system_keyset.Key().fek_salt;
  fnek_ = file_system_keyset.Key().fnek;
  fnek_salt_ = file_system_keyset.Key().fnek_salt;
  fek_sig_ = file_system_keyset.KeyReference().fek_sig;
  fnek_sig_ = file_system_keyset.KeyReference().fnek_sig;

  chaps_key_ = file_system_keyset.chaps_key();
  CreateRandomResetSeed();
}

FileSystemKeyset VaultKeyset::ToFileSystemKeyset() const {
  return FileSystemKeyset({.fek = fek_,
                           .fnek = fnek_,
                           .fek_salt = fek_salt_,
                           .fnek_salt = fnek_salt_},
                          {.fek_sig = fek_sig_, .fnek_sig = fnek_sig_},
                          chaps_key_);
}

bool VaultKeyset::Load(const base::FilePath& filename) {
  CHECK(platform_);
  brillo::Blob contents;
  if (!platform_->ReadFile(filename, &contents)) {
    return false;
  }
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

CryptohomeStatus VaultKeyset::EncryptEx(const KeyBlobs& key_blobs,
                                        const AuthBlockState& auth_state) {
  CHECK(crypto_);

  SetAuthBlockState(auth_state);
  if (IsLECredential()) {
    if (key_blobs.reset_secret.has_value() &&
        !key_blobs.reset_secret.value().empty()) {
      SetResetSecret(key_blobs.reset_secret.value());
    } else if (reset_seed_.empty()) {
      LOG(ERROR) << "Reset secret and reset seed are missing, so we can't"
                    " set up an LE credential.";
      return MakeStatus<CryptohomeError>(
          CRYPTOHOME_ERR_LOC(kLocVaultKeysetNoResetSeedInEncryptEx),
          ErrorActionSet({PossibleAction::kDevCheckUnexpectedState,
                          PossibleAction::kDeleteVault, PossibleAction::kAuth}),
          user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED);
    }
    auth_locked_ = false;
  }

  bool is_scrypt_wrapped =
      std::holds_alternative<ScryptAuthBlockState>(auth_state.state) ||
      std::holds_alternative<ChallengeCredentialAuthBlockState>(
          auth_state.state);

  CryptohomeStatus return_status = OkStatus<CryptohomeError>();
  if (is_scrypt_wrapped) {
    CryptohomeStatus status = WrapScryptVaultKeyset(auth_state, key_blobs);
    if (!status.ok()) {
      return_status =
          MakeStatus<CryptohomeError>(
              CRYPTOHOME_ERR_LOC(kLocVaultKeysetWrapScryptFailedInEncryptEx))
              .Wrap(std::move(status));
    }
  } else {
    CryptohomeStatus status = WrapVaultKeysetWithAesDeprecated(key_blobs);
    if (!status.ok()) {
      return_status =
          MakeStatus<CryptohomeError>(
              CRYPTOHOME_ERR_LOC(kLocVaultKeysetWrapAESDFailedInEncryptEx))
              .Wrap(std::move(status));
    }
  }

  encrypted_ = return_status.ok();
  return return_status;
}

void VaultKeyset::MarkMigrated(bool migrated) {
  migrated_vk_ = migrated;
  if (migrated) {
    backup_vk_ = true;
  }
}

CryptoStatus VaultKeyset::DecryptEx(const KeyBlobs& key_blobs) {
  CHECK(crypto_);

  if (!loaded_) {
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocVaultKeysetNotLoadedInDecryptEx),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState,
                        PossibleAction::kReboot}),
        CryptoError::CE_OTHER_CRYPTO);
  }

  return DecryptVaultKeysetEx(key_blobs);
}

CryptoStatus VaultKeyset::DecryptVaultKeysetEx(const KeyBlobs& key_blobs) {
  if (flags_ & SerializedVaultKeyset::LE_CREDENTIAL) {
    // This is possible to be empty if an old version of CR50 is running.
    if (key_blobs.reset_secret.has_value() &&
        !key_blobs.reset_secret.value().empty()) {
      SetResetSecret(key_blobs.reset_secret.value());
    }
  }

  // Loaded VaultKeyset fields are in encrypted form (e.g. wrapped_reset_seed).
  // Convert them to a serialized vault keyset and then decrypt. VaultKeyset
  // object members that carry the plain secrets are set after the decryption
  // operation (e.g. reset_seed).
  const SerializedVaultKeyset& serialized = ToSerialized();
  return UnwrapVaultKeyset(serialized, key_blobs);
}

CryptoStatus VaultKeyset::UnwrapVKKVaultKeyset(
    const SerializedVaultKeyset& serialized, const KeyBlobs& vkk_data) {
  const SecureBlob& vkk_key = vkk_data.vkk_key.value();
  const Blob& vkk_iv = vkk_data.vkk_iv.value();
  const Blob& chaps_iv = vkk_data.chaps_iv.value();
  // Decrypt the keyset protobuf.
  brillo::Blob local_encrypted_keyset(serialized.wrapped_keyset().begin(),
                                      serialized.wrapped_keyset().end());
  SecureBlob plain_text;

  if (!AesDecryptDeprecated(local_encrypted_keyset, vkk_key, vkk_iv,
                            &plain_text)) {
    // Note that AesDecryptDeprecated() checks the validity of the decrypted
    // content. Also, it is possible for the input vkk_data to be garbage
    // because some AuthBlocks (such as Scrypt) doesn't check the correctness of
    // its output when given the wrong credentials. Therefore, a decryption
    // failure here is most likely an incorrect password.
    LOG(ERROR) << "AES decryption failed for vault keyset.";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocVaultKeysetKeysetDecryptFailedInUnwrapVKK),
        ErrorActionSet(PrimaryAction::kIncorrectAuth),
        CryptoError::CE_OTHER_CRYPTO);
  }

  if (!FromKeysBlob(plain_text)) {
    LOG(ERROR) << "Failed to decode the keys blob.";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocVaultKeysetKeysetParseFailedInUnwrapVKK),
        ErrorActionSet(
            {PossibleAction::kDevCheckUnexpectedState, PossibleAction::kAuth}),
        CryptoError::CE_OTHER_CRYPTO);
  }

  // Decrypt the chaps key.
  if (serialized.has_wrapped_chaps_key()) {
    Blob local_wrapped_chaps_key(serialized.wrapped_chaps_key().begin(),
                                 serialized.wrapped_chaps_key().end());
    SecureBlob unwrapped_chaps_key;

    if (!AesDecryptDeprecated(local_wrapped_chaps_key, vkk_key, chaps_iv,
                              &unwrapped_chaps_key)) {
      LOG(ERROR) << "AES decryption failed for chaps key.";
      return MakeStatus<CryptohomeCryptoError>(
          CRYPTOHOME_ERR_LOC(kLocVaultKeysetChapsDecryptFailedInUnwrapVKK),
          ErrorActionSet({PossibleAction::kDevCheckUnexpectedState,
                          PossibleAction::kAuth}),
          CryptoError::CE_OTHER_CRYPTO);
    }

    SetChapsKey(unwrapped_chaps_key);
  }

  // Decrypt the reset seed.
  bool is_le_credential =
      serialized.flags() & SerializedVaultKeyset::LE_CREDENTIAL;
  if (serialized.has_wrapped_reset_seed() && !is_le_credential) {
    SecureBlob unwrapped_reset_seed;
    Blob local_wrapped_reset_seed =
        brillo::BlobFromString(serialized.wrapped_reset_seed());
    Blob local_reset_iv = brillo::BlobFromString(serialized.reset_iv());

    if (!AesDecryptDeprecated(local_wrapped_reset_seed, vkk_key, local_reset_iv,
                              &unwrapped_reset_seed)) {
      LOG(ERROR) << "AES decryption failed for reset seed.";
      return MakeStatus<CryptohomeCryptoError>(
          CRYPTOHOME_ERR_LOC(kLocVaultKeysetResetSeedDecryptFailedInUnwrapVKK),
          ErrorActionSet({PossibleAction::kDevCheckUnexpectedState,
                          PossibleAction::kAuth}),
          CryptoError::CE_OTHER_CRYPTO);
    }

    SetResetSeed(unwrapped_reset_seed);
  }

  return OkStatus<CryptohomeCryptoError>();
}

CryptoStatus VaultKeyset::UnwrapScryptVaultKeyset(
    const SerializedVaultKeyset& serialized, const KeyBlobs& key_blobs) {
  auto blob = brillo::BlobFromString(serialized.wrapped_keyset());
  SecureBlob decrypted(blob.size());
  if (!LibScryptCompat::Decrypt(blob, key_blobs.vkk_key.value(), &decrypted)) {
    // Note that Decrypt() checks the validity of the key. Also, it is possible
    // for the input key_blobs to be garbage because some AuthBlocks (such as
    // Scrypt) doesn't check the correctness of its output when given the wrong
    // credentials. Therefore, a decryption failure here is most likely an
    // incorrect password.
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocVaultKeysetKeysetDecryptFailedInUnwrapScrypt),
        ErrorActionSet(PrimaryAction::kIncorrectAuth),
        CryptoError::CE_OTHER_CRYPTO);
  }

  if (serialized.has_wrapped_chaps_key()) {
    auto wrapped_chaps_key =
        brillo::BlobFromString(serialized.wrapped_chaps_key());
    SecureBlob chaps_key;
    chaps_key.resize(wrapped_chaps_key.size());
    if (!LibScryptCompat::Decrypt(wrapped_chaps_key,
                                  key_blobs.scrypt_chaps_key.value(),
                                  &chaps_key)) {
      return MakeStatus<CryptohomeCryptoError>(
          CRYPTOHOME_ERR_LOC(kLocVaultKeysetChapsDecryptFailedInUnwrapScrypt),
          ErrorActionSet({PossibleAction::kDevCheckUnexpectedState,
                          PossibleAction::kAuth}),
          CryptoError::CE_OTHER_CRYPTO);
    }
    SetChapsKey(chaps_key);
  }

  if (serialized.has_wrapped_reset_seed()) {
    auto wrapped_reset_seed =
        brillo::BlobFromString(serialized.wrapped_reset_seed());
    SecureBlob reset_seed;
    reset_seed.resize(wrapped_reset_seed.size());
    if (!LibScryptCompat::Decrypt(wrapped_reset_seed,
                                  key_blobs.scrypt_reset_seed_key.value(),
                                  &reset_seed)) {
      return MakeStatus<CryptohomeCryptoError>(
          CRYPTOHOME_ERR_LOC(
              kLocVaultKeysetResetSeedDecryptFailedInUnwrapScrypt),
          ErrorActionSet({PossibleAction::kDevCheckUnexpectedState,
                          PossibleAction::kAuth}),
          CryptoError::CE_OTHER_CRYPTO);
    }
    SetResetSeed(reset_seed);
  }

  // There is a SHA hash included at the end of the decrypted blob. However,
  // scrypt already appends a MAC, so if the payload is corrupted we will fail
  // on the first call to DecryptScryptBlob.
  if (decrypted.size() < SHA_DIGEST_LENGTH) {
    LOG(ERROR) << "Message length underflow: " << decrypted.size() << " bytes?";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocVaultKeysetBlobUnderflowInUnwrapScrypt),
        ErrorActionSet(
            {PossibleAction::kDevCheckUnexpectedState, PossibleAction::kAuth}),
        CryptoError::CE_OTHER_CRYPTO);
  }
  decrypted.resize(decrypted.size() - SHA_DIGEST_LENGTH);
  if (!FromKeysBlob(decrypted)) {
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocVaultKeysetKeysetParseFailedInUnwrapScrypt),
        ErrorActionSet(
            {PossibleAction::kDevCheckUnexpectedState, PossibleAction::kAuth}),
        CryptoError::CE_OTHER_CRYPTO);
  }
  return OkStatus<CryptohomeCryptoError>();
}

CryptohomeStatus VaultKeyset::WrapVaultKeysetWithAesDeprecated(
    const KeyBlobs& blobs) {
  if (blobs.vkk_key == std::nullopt || blobs.vkk_iv == std::nullopt ||
      blobs.chaps_iv == std::nullopt) {
    LOG(ERROR) << "Fields missing from KeyBlobs.";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocVaultKeysetMissingFieldInWrapAESD),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }

  SecureBlob vault_blob;
  if (!ToKeysBlob(&vault_blob)) {
    LOG(ERROR) << "Failure serializing keyset to buffer";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocVaultKeysetSerializationFailedInWrapAESD),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }

  if (!AesEncryptDeprecated(
          vault_blob, blobs.vkk_key.value(),
          Blob(blobs.vkk_iv.value().begin(), blobs.vkk_iv.value().end()),
          &wrapped_keyset_)) {
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocVaultKeysetEncryptFailedInWrapAESD),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }
  le_fek_iv_ = blobs.vkk_iv.value();

  if (GetChapsKey().size() == kCryptohomeChapsKeyLength) {
    Blob wrapped_chaps_key;
    if (!AesEncryptDeprecated(
            GetChapsKey(), blobs.vkk_key.value(),
            Blob(blobs.chaps_iv.value().begin(), blobs.chaps_iv.value().end()),
            &wrapped_chaps_key)) {
      return MakeStatus<CryptohomeError>(
          CRYPTOHOME_ERR_LOC(kLocVaultKeysetEncryptChapsFailedInWrapAESD),
          ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
          user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
    }
    wrapped_chaps_key_ = wrapped_chaps_key;
    le_chaps_iv_ = blobs.chaps_iv.value();
  }

  // If a reset seed is present, encrypt and store it, else clear the field.
  if (!IsLECredential() && GetResetSeed().size() != 0) {
    const auto reset_iv = CreateRandomBlob(kAesBlockSize);
    Blob wrapped_reset_seed;
    if (!AesEncryptDeprecated(GetResetSeed(), blobs.vkk_key.value(), reset_iv,
                              &wrapped_reset_seed)) {
      LOG(ERROR) << "AES encryption of Reset seed failed.";
      return MakeStatus<CryptohomeError>(
          CRYPTOHOME_ERR_LOC(kLocVaultKeysetEncryptResetSeedInWrapAESD),
          ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
          user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
    }
    wrapped_reset_seed_ = wrapped_reset_seed;
    reset_iv_ = reset_iv;
  }

  return OkStatus<CryptohomeError>();
}

CryptohomeStatus VaultKeyset::WrapScryptVaultKeyset(
    const AuthBlockState& auth_block_state, const KeyBlobs& key_blobs) {
  if (IsLECredential()) {
    LOG(ERROR) << "Low entropy credentials cannot be scrypt-wrapped.";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocVaultKeysetLENotSupportedInWrapScrypt),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED);
  }

  brillo::SecureBlob blob;
  if (!ToKeysBlob(&blob)) {
    LOG(ERROR) << "Failure serializing keyset to buffer";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocVaultKeysetSerializeFailedInWrapScrypt),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED);
  }

  // Append the SHA1 hash of the keyset blob. This is done solely for
  // backwards-compatibility purposes, since scrypt already creates a
  // MAC for the encrypted blob. It is ignored in DecryptScrypt since
  // it is redundant.
  brillo::SecureBlob hash = Sha1(blob);
  brillo::SecureBlob local_blob = SecureBlob::Combine(blob, hash);
  auto* state = std::get_if<ScryptAuthBlockState>(&auth_block_state.state);
  auto* cc_state =
      std::get_if<ChallengeCredentialAuthBlockState>(&auth_block_state.state);
  // Fetch ScryptAuthBlockState from inside ChallengeCredentialAuthBlockState
  // since the |auth_block_state| ScryptAuthBlockState is empty. Either one of
  // Scrypt or ChallengeCredential states is populated per encryption with
  // Scrypt.
  if (state == nullptr) {
    state = &(cc_state->scrypt_state);
  }

  if (state == nullptr) {
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocVaultKeysetAuthBlockStateFailedInWrapScrypt),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED);
  }

  if (!LibScryptCompat::Encrypt(key_blobs.vkk_key.value(), state->salt.value(),
                                local_blob, kDefaultScryptParams,
                                &wrapped_keyset_)) {
    LOG(ERROR) << "Scrypt encrypt of keyset blob failed.";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocVaultKeysetEncryptKeysetFailedInWrapScrypt),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED);
  }

  if (GetChapsKey().size() == kCryptohomeChapsKeyLength) {
    brillo::Blob wrapped_chaps_key;
    if (!LibScryptCompat::Encrypt(key_blobs.scrypt_chaps_key.value(),
                                  state->chaps_salt.value(), GetChapsKey(),
                                  kDefaultScryptParams, &wrapped_chaps_key)) {
      LOG(ERROR) << "Scrypt encrypt of chaps key blob failed.";
      return MakeStatus<CryptohomeError>(
          CRYPTOHOME_ERR_LOC(kLocVaultKeysetEncryptChapsFailedInWrapScrypt),
          ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
          user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED);
    }
    wrapped_chaps_key_ = wrapped_chaps_key;
  }

  // If there is a reset seed, encrypt and store it.
  if (GetResetSeed().size() != 0) {
    brillo::Blob wrapped_reset_seed;
    if (!LibScryptCompat::Encrypt(key_blobs.scrypt_reset_seed_key.value(),
                                  state->reset_seed_salt.value(),
                                  GetResetSeed(), kDefaultScryptParams,
                                  &wrapped_reset_seed)) {
      LOG(ERROR) << "Scrypt encrypt of reset seed failed.";
      return MakeStatus<CryptohomeError>(
          CRYPTOHOME_ERR_LOC(kLocVaultKeysetEncryptResetSeedFailedInWrapScrypt),
          ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
          user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED);
    }

    wrapped_reset_seed_ = wrapped_reset_seed;
  }

  return OkStatus<CryptohomeError>();
}

CryptoStatus VaultKeyset::UnwrapVaultKeyset(
    const SerializedVaultKeyset& serialized, const KeyBlobs& vkk_data) {
  bool has_vkk_key = vkk_data.vkk_key != std::nullopt &&
                     vkk_data.vkk_iv != std::nullopt &&
                     vkk_data.chaps_iv != std::nullopt;
  bool has_scrypt_key =
      vkk_data.vkk_key.has_value() && vkk_data.scrypt_chaps_key.has_value();

  CryptoStatus return_status = OkStatus<CryptohomeCryptoError>();
  if (has_vkk_key && !has_scrypt_key) {
    CryptoStatus status = UnwrapVKKVaultKeyset(serialized, vkk_data);
    if (!status.ok()) {
      return_status =
          MakeStatus<CryptohomeCryptoError>(
              CRYPTOHOME_ERR_LOC(kLocVaultKeysetUnwrapVKKFailedInUnwrapVK))
              .Wrap(std::move(status));
    }
  } else if (has_scrypt_key && !has_vkk_key) {
    CryptoStatus status = UnwrapScryptVaultKeyset(serialized, vkk_data);
    if (!status.ok()) {
      return_status =
          MakeStatus<CryptohomeCryptoError>(
              CRYPTOHOME_ERR_LOC(kLocVaultKeysetUnwrapScryptFailedInUnwrapVK))
              .Wrap(std::move(status));
    }
  } else {
    DLOG(FATAL) << "An invalid key combination exists";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocVaultKeysetInvalidCombinationInUnwrapVK),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState,
                        PossibleAction::kDeleteVault, PossibleAction::kAuth}),
        CryptoError::CE_OTHER_CRYPTO);
  }

  if (return_status.ok()) {
    // By this point we know that the TPM is successfully owned, everything
    // is initialized, and we were able to successfully decrypt a
    // TPM-wrapped keyset. So, for TPMs with updateable firmware, we assume
    // that it is stable (and the TPM can invalidate the old version).
    const bool tpm_backed =
        (serialized.flags() & SerializedVaultKeyset::TPM_WRAPPED) ||
        (serialized.flags() & SerializedVaultKeyset::LE_CREDENTIAL);
    if (tpm_backed) {
      if (hwsec::Status status =
              crypto_->GetHwsec()->DeclareTpmFirmwareStable();
          !status.ok()) {
        LOG(WARNING) << "Failed to declare TPM firmware stable: "
                     << std::move(status);
      }
    }
  }
  return return_status;
}

void VaultKeyset::SetTpmNotBoundToPcrState(
    const TpmNotBoundToPcrAuthBlockState& auth_state) {
  flags_ = kTpmNotBoundToPcrFlags.require_flags;
  if (auth_state.scrypt_derived.value_or(false)) {
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
  if (auth_state.scrypt_derived.value_or(false)) {
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
  if (auth_state.reset_salt.has_value()) {
    reset_salt_ = auth_state.reset_salt.value();
  }
}

void VaultKeyset::SetScryptState(const ScryptAuthBlockState& auth_state) {
  flags_ = kScryptFlags.require_flags;

  if (auth_state.salt.has_value()) {
    auth_salt_ = auth_state.salt.value();
  }
}

void VaultKeyset::SetChallengeCredentialState(
    const ChallengeCredentialAuthBlockState& auth_state) {
  flags_ = kChallengeCredentialFlags.require_flags;

  if (auth_state.scrypt_state.salt.has_value()) {
    auth_salt_ = auth_state.scrypt_state.salt.value();
  }

  if (auth_state.keyset_challenge_info.has_value()) {
    signature_challenge_info_ =
        proto::ToProto(auth_state.keyset_challenge_info.value());
  }
}

void VaultKeyset::SetTpmEccState(const TpmEccAuthBlockState& auth_state) {
  flags_ = kTpmEccFlags.require_flags;
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
          std::get_if<TpmNotBoundToPcrAuthBlockState>(&auth_state.state)) {
    SetTpmNotBoundToPcrState(*state);
  } else if (auto* state =
                 std::get_if<TpmBoundToPcrAuthBlockState>(&auth_state.state)) {
    SetTpmBoundToPcrState(*state);
  } else if (auto* state =
                 std::get_if<PinWeaverAuthBlockState>(&auth_state.state)) {
    SetPinWeaverState(*state);
  } else if (auto* state =
                 std::get_if<ScryptAuthBlockState>(&auth_state.state)) {
    SetScryptState(*state);
  } else if (auto* state = std::get_if<ChallengeCredentialAuthBlockState>(
                 &auth_state.state)) {
    SetChallengeCredentialState(*state);
  } else if (auto* state =
                 std::get_if<TpmEccAuthBlockState>(&auth_state.state)) {
    SetTpmEccState(*state);
  } else {
    // other states are not supported.
    LOG(ERROR) << "Invalid auth block state type";
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
  if (!GetScryptState(&scrypt_state)) {
    return false;
  }
  const auto* libscrypt_state =
      std::get_if<ScryptAuthBlockState>(&scrypt_state.state);

  // This should never happen.
  if (libscrypt_state == nullptr) {
    LOG(ERROR) << "ScryptAuthBlockState should have been created";
    return false;
  }

  ChallengeCredentialAuthBlockState cc_state = {
      .scrypt_state = std::move(*libscrypt_state),
  };
  if (signature_challenge_info_.has_value()) {
    cc_state.keyset_challenge_info =
        proto::FromProto(signature_challenge_info_.value());
  }
  auth_state->state = std::move(cc_state);
  return true;
}

bool VaultKeyset::GetScryptState(AuthBlockState* auth_state) const {
  ScryptAuthBlockState state;

  hwsec_foundation::ScryptParameters params;
  brillo::Blob salt;
  if (!LibScryptCompat::ParseHeader(wrapped_keyset_, &params, &salt)) {
    LOG(ERROR) << "Failed to parse scrypt header for wrapped_keyset_.";
    return false;
  }
  state.salt = std::move(salt);

  brillo::Blob chaps_salt;
  if (HasWrappedChapsKey()) {
    if (!LibScryptCompat::ParseHeader(GetWrappedChapsKey(), &params,
                                      &chaps_salt)) {
      LOG(ERROR) << "Failed to parse scrypt header for wrapped_chaps_keyset_.";
      return false;
    }
    state.chaps_salt = std::move(chaps_salt);
  }

  brillo::Blob reset_seed_salt;
  if (HasWrappedResetSeed()) {
    if (!LibScryptCompat::ParseHeader(GetWrappedResetSeed(), &params,
                                      &reset_seed_salt)) {
      LOG(ERROR) << "Failed to parse scrypt header for wrapped_reset_seed_.";
      return false;
    }
    state.reset_seed_salt = std::move(reset_seed_salt);
  }

  state.work_factor = params.n_factor;
  state.block_size = params.r_factor;
  state.parallel_factor = params.p_factor;
  auth_state->state = std::move(state);
  return true;
}

bool VaultKeyset::GetDoubleWrappedCompatState(
    AuthBlockState* auth_state) const {
  AuthBlockState scrypt_state;
  if (!GetScryptState(&scrypt_state)) {
    return false;
  }
  const auto* scrypt_sub_state =
      std::get_if<ScryptAuthBlockState>(&scrypt_state.state);

  // This should never happen.
  if (scrypt_sub_state == nullptr) {
    LOG(ERROR) << "ScryptAuthBlockState should have been created";
    return false;
  }

  AuthBlockState tpm_state;
  if (!GetTpmNotBoundToPcrState(&tpm_state)) {
    return false;
  }
  const auto* tpm_sub_state =
      std::get_if<TpmNotBoundToPcrAuthBlockState>(&tpm_state.state);

  // This should never happen but handling it on the safe side.
  if (tpm_sub_state == nullptr) {
    LOG(ERROR) << "TpmNotBoundToPcrAuthBlockState should have been created";
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

bool VaultKeyset::Save(const base::FilePath& filename) {
  CHECK(platform_);
  if (!encrypted_) {
    return false;
  }
  SerializedVaultKeyset serialized = ToSerialized();

  brillo::Blob contents(serialized.ByteSizeLong());
  google::protobuf::uint8* buf =
      static_cast<google::protobuf::uint8*>(contents.data());
  serialized.SerializeWithCachedSizesToArray(buf);

  bool ok = platform_->WriteFileAtomicDurable(filename, contents,
                                              kVaultFilePermissions);
  if (ok) {
    source_file_ = filename;
  }

  return ok;
}

std::string VaultKeyset::GetLabel() const {
  if (key_data_.has_value() && !key_data_->label().empty()) {
    return key_data_->label();
  }
  // Fallback for legacy keys, for which the label has to be inferred from the
  // index number.
  return base::StringPrintf("%s%d", kKeyLegacyPrefix, index_);
}

bool VaultKeyset::IsLECredential() const {
  if (key_data_.has_value()) {
    return key_data_->policy().low_entropy_credential();
  }
  return false;
}

bool VaultKeyset::HasTpmPublicKeyHash() const {
  return tpm_public_key_hash_.has_value();
}

const brillo::Blob& VaultKeyset::GetTpmPublicKeyHash() const {
  CHECK(tpm_public_key_hash_.has_value());
  return tpm_public_key_hash_.value();
}

void VaultKeyset::SetTpmPublicKeyHash(const brillo::Blob& hash) {
  tpm_public_key_hash_ = hash;
}

bool VaultKeyset::HasPasswordRounds() const {
  return password_rounds_.has_value();
}

int32_t VaultKeyset::GetPasswordRounds() const {
  CHECK(password_rounds_.has_value());
  return password_rounds_.value();
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
  CHECK(key_data_.has_value());
  return key_data_.value();
}

KeyData VaultKeyset::GetKeyDataOrDefault() const {
  if (HasKeyData()) {
    return GetKeyData();
  }

  // The VK created before M91 may contain empty key data.
  // We should use default value for that case. Note that we don't populate any
  // fields, like |type| or |label|, because we can't determine the type
  // reliably and the "legacy-N" label has never been stored in the key data
  // explicitly.
  return KeyData();
}

bool VaultKeyset::HasVkkIv() const {
  return vkk_iv_.has_value();
}

const brillo::Blob& VaultKeyset::GetVkkIv() const {
  CHECK(HasVkkIv());
  return vkk_iv_.value();
}

void VaultKeyset::SetResetIV(const brillo::Blob& iv) {
  reset_iv_ = iv;
}

const brillo::Blob& VaultKeyset::GetResetIV() const {
  CHECK(reset_iv_.has_value());
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
  CHECK(le_label_.has_value());
  return le_label_.value();
}

void VaultKeyset::SetResetSalt(const brillo::Blob& reset_salt) {
  reset_salt_ = reset_salt;
}

bool VaultKeyset::HasResetSalt() const {
  return reset_salt_.has_value();
}

const brillo::Blob& VaultKeyset::GetResetSalt() const {
  CHECK(reset_salt_.has_value());
  return reset_salt_.value();
}

void VaultKeyset::SetFSCryptPolicyVersion(int32_t policy_version) {
  fscrypt_policy_version_ = policy_version;
}

int32_t VaultKeyset::GetFSCryptPolicyVersion() const {
  CHECK(fscrypt_policy_version_.has_value());
  return fscrypt_policy_version_.value();
}

void VaultKeyset::SetWrappedKeyset(const brillo::Blob& wrapped_keyset) {
  wrapped_keyset_ = wrapped_keyset;
}

const brillo::Blob& VaultKeyset::GetWrappedKeyset() const {
  return wrapped_keyset_;
}

bool VaultKeyset::HasWrappedChapsKey() const {
  return wrapped_chaps_key_.has_value();
}

void VaultKeyset::SetWrappedChapsKey(const brillo::Blob& wrapped_chaps_key) {
  wrapped_chaps_key_ = wrapped_chaps_key;
}

const brillo::Blob& VaultKeyset::GetWrappedChapsKey() const {
  CHECK(wrapped_chaps_key_.has_value());
  return wrapped_chaps_key_.value();
}

void VaultKeyset::ClearWrappedChapsKey() {
  wrapped_chaps_key_.reset();
}

bool VaultKeyset::HasTPMKey() const {
  return tpm_key_.has_value();
}

void VaultKeyset::SetTPMKey(const brillo::Blob& tpm_key) {
  tpm_key_ = tpm_key;
}

const brillo::Blob& VaultKeyset::GetTPMKey() const {
  CHECK(tpm_key_.has_value());
  return tpm_key_.value();
}

bool VaultKeyset::HasExtendedTPMKey() const {
  return extended_tpm_key_.has_value();
}

void VaultKeyset::SetExtendedTPMKey(const brillo::Blob& extended_tpm_key) {
  extended_tpm_key_ = extended_tpm_key;
}

const brillo::Blob& VaultKeyset::GetExtendedTPMKey() const {
  CHECK(extended_tpm_key_.has_value());
  return extended_tpm_key_.value();
}

bool VaultKeyset::HasWrappedResetSeed() const {
  return wrapped_reset_seed_.has_value();
}

void VaultKeyset::SetWrappedResetSeed(const brillo::Blob& wrapped_reset_seed) {
  wrapped_reset_seed_ = wrapped_reset_seed;
}

const brillo::Blob& VaultKeyset::GetWrappedResetSeed() const {
  CHECK(wrapped_reset_seed_.has_value());
  return wrapped_reset_seed_.value();
}

bool VaultKeyset::HasSignatureChallengeInfo() const {
  return signature_challenge_info_.has_value();
}

const SerializedVaultKeyset::SignatureChallengeInfo&
VaultKeyset::GetSignatureChallengeInfo() const {
  CHECK(signature_challenge_info_.has_value());
  return signature_challenge_info_.value();
}

void VaultKeyset::SetSignatureChallengeInfo(
    const SerializedVaultKeyset::SignatureChallengeInfo& info) {
  signature_challenge_info_ = info;
}

void VaultKeyset::SetChapsKey(const brillo::SecureBlob& chaps_key) {
  CHECK(chaps_key.size() == kCryptohomeChapsKeyLength);
  chaps_key_ = chaps_key;
}

void VaultKeyset::SetResetSeed(const brillo::SecureBlob& reset_seed) {
  CHECK_EQ(reset_seed.size(), kCryptohomeResetSeedLength);
  reset_seed_ = reset_seed;
}

void VaultKeyset::SetResetSecret(const brillo::SecureBlob& reset_secret) {
  CHECK_EQ(reset_secret.size(), kCryptohomeResetSeedLength);
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

  serialized.set_backup_vk(backup_vk_);

  serialized.set_migrated_vk(migrated_vk_);

  return serialized;
}

void VaultKeyset::ResetVaultKeyset() {
  flags_ = -1;
  backup_vk_ = false;
  migrated_vk_ = false;
  auth_salt_.clear();
  index_ = -1;
  tpm_public_key_hash_.reset();
  password_rounds_.reset();
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
  auth_salt_ = brillo::BlobFromString(serialized.salt());

  wrapped_keyset_ = brillo::BlobFromString(serialized.wrapped_keyset());

  if (serialized.has_tpm_key()) {
    tpm_key_ = brillo::BlobFromString(serialized.tpm_key());
  }

  if (serialized.has_tpm_public_key_hash()) {
    tpm_public_key_hash_ =
        brillo::BlobFromString(serialized.tpm_public_key_hash());
  }

  if (serialized.has_password_rounds()) {
    password_rounds_ = serialized.password_rounds();
  }

  if (serialized.has_key_data()) {
    key_data_ = serialized.key_data();

    auth_locked_ = serialized.key_data().policy().auth_locked();

    // For LECredentials, set the key policy appropriately.
    if (flags_ & SerializedVaultKeyset::LE_CREDENTIAL) {
      key_data_->mutable_policy()->set_low_entropy_credential(true);
    }
  }

  if (serialized.has_wrapped_chaps_key()) {
    wrapped_chaps_key_ = brillo::BlobFromString(serialized.wrapped_chaps_key());
  }

  if (serialized.has_wrapped_reset_seed()) {
    wrapped_reset_seed_ =
        brillo::BlobFromString(serialized.wrapped_reset_seed());
  }

  if (serialized.has_reset_iv()) {
    reset_iv_ = brillo::BlobFromString(serialized.reset_iv());
  }

  if (serialized.has_le_label()) {
    le_label_ = serialized.le_label();
  }

  if (serialized.has_le_fek_iv()) {
    le_fek_iv_ = brillo::BlobFromString(serialized.le_fek_iv());
  }

  if (serialized.has_le_chaps_iv()) {
    le_chaps_iv_ = brillo::BlobFromString(serialized.le_chaps_iv());
  }

  if (serialized.has_reset_salt()) {
    reset_salt_ = brillo::BlobFromString(serialized.reset_salt());
  }

  if (serialized.has_signature_challenge_info()) {
    signature_challenge_info_ = serialized.signature_challenge_info();
  }

  if (serialized.has_extended_tpm_key()) {
    extended_tpm_key_ = brillo::BlobFromString(serialized.extended_tpm_key());
  }

  if (serialized.has_fscrypt_policy_version()) {
    fscrypt_policy_version_ = serialized.fscrypt_policy_version();
  }

  if (serialized.has_vkk_iv()) {
    vkk_iv_ = brillo::BlobFromString(serialized.vkk_iv());
  }

  backup_vk_ = serialized.backup_vk();
  migrated_vk_ = serialized.migrated_vk();
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

void VaultKeyset::SetIndex(int index) {
  index_ = index;
}

const int VaultKeyset::GetIndex() const {
  return index_;
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
