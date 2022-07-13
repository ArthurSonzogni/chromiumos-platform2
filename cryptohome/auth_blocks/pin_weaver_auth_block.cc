// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_blocks/pin_weaver_auth_block.h"

#include <libhwsec-foundation/crypto/scrypt.h>
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/tpm.h"
#include "cryptohome/vault_keyset.h"

#include <limits>
#include <map>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <base/check.h>
#include <base/check_op.h>
#include <base/logging.h>
#include <brillo/secure_blob.h>
#include <libhwsec-foundation/crypto/aes.h>
#include <libhwsec-foundation/crypto/hmac.h>
#include <libhwsec-foundation/crypto/secure_blob_util.h>
#include <libhwsec-foundation/crypto/sha.h>

#include "cryptohome/error/location_utils.h"
#include "cryptohome/flatbuffer_schemas/auth_block_state.h"
#include "cryptohome/vault_keyset.pb.h"

using ::cryptohome::error::CryptohomeCryptoError;
using ::cryptohome::error::ErrorAction;
using ::cryptohome::error::ErrorActionSet;
using ::hwsec_foundation::CreateSecureRandomBlob;
using ::hwsec_foundation::DeriveSecretsScrypt;
using ::hwsec_foundation::HmacSha256;
using ::hwsec_foundation::kAesBlockSize;
using ::hwsec_foundation::kDefaultAesKeySize;
using ::hwsec_foundation::Sha256;
using ::hwsec_foundation::status::MakeStatus;
using ::hwsec_foundation::status::OkStatus;
using ::hwsec_foundation::status::StatusChain;

namespace cryptohome {

namespace {

constexpr int kDefaultSecretSize = 32;

void LogLERetCode(int le_error) {
  switch (le_error) {
    case LE_CRED_ERROR_NO_FREE_LABEL:
      LOG(ERROR) << "No free label available in hash tree.";
      break;
    case LE_CRED_ERROR_HASH_TREE:
      LOG(ERROR) << "Hash tree error.";
      break;
  }
}

// String used as vector in HMAC operation to derive vkk_seed from High Entropy
// secret.
constexpr char kHESecretHmacData[] = "vkk_seed";

// A default delay schedule to be used for LE Credentials.
// The format for a delay schedule entry is as follows:
//
// (number_of_incorrect_attempts, delay before_next_attempt)
//
// The default schedule is for the first 5 incorrect attempts to have no delay,
// and no further attempts allowed.
constexpr uint32_t kAttemptsLimit = 5;
constexpr uint32_t kInfiniteDelay = std::numeric_limits<uint32_t>::max();
constexpr struct {
  uint32_t attempts;
  uint32_t delay;
} kDefaultDelaySchedule[] = {
    {kAttemptsLimit, kInfiniteDelay},
};

}  // namespace

PinWeaverAuthBlock::PinWeaverAuthBlock(
    LECredentialManager* le_manager,
    CryptohomeKeysManager* cryptohome_keys_manager)
    : SyncAuthBlock(kLowEntropyCredential),
      le_manager_(le_manager),
      cryptohome_key_loader_(
          cryptohome_keys_manager->GetKeyLoader(CryptohomeKeyType::kRSA)) {
  CHECK_NE(le_manager, nullptr);
  CHECK_NE(cryptohome_key_loader_, nullptr);
}

CryptoStatus PinWeaverAuthBlock::Create(const AuthInput& auth_input,
                                        AuthBlockState* auth_block_state,
                                        KeyBlobs* key_blobs) {
  DCHECK(key_blobs);

  if (!auth_input.user_input.has_value()) {
    LOG(ERROR) << "Missing user_input";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocPinWeaverAuthBlockNoUserInputInCreate),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        CryptoError::CE_OTHER_CRYPTO);
  }
  if (!auth_input.obfuscated_username.has_value()) {
    LOG(ERROR) << "Missing obfuscated_username";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocPinWeaverAuthBlockNoUsernameInCreate),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        CryptoError::CE_OTHER_CRYPTO);
  }
  if (!auth_input.reset_secret.has_value() &&
      !auth_input.reset_seed.has_value()) {
    LOG(ERROR) << "Missing reset_secret or reset_seed";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(
            kLocPinWeaverAuthBlockNoResetSecretOrResetSeedInCreate),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        CryptoError::CE_OTHER_CRYPTO);
  }

  PinWeaverAuthBlockState pin_auth_state;
  brillo::SecureBlob reset_secret;
  brillo::SecureBlob salt =
      CreateSecureRandomBlob(CRYPTOHOME_DEFAULT_KEY_SALT_SIZE);
  if (auth_input.reset_secret.has_value()) {
    // This case be used for USS as we do not have the concept of reset seed and
    // salt there.
    reset_secret = auth_input.reset_secret.value();
  } else {
    // At this point we know auth_input reset_seed is set. The expectation is
    // that this branch of code would be deprecated once we move fully to USS
    // world.
    pin_auth_state.reset_salt = CreateSecureRandomBlob(kAesBlockSize);
    reset_secret = HmacSha256(pin_auth_state.reset_salt.value(),
                              auth_input.reset_seed.value());
  }

  // This may not be needed, but is retained to maintain the original logic.
  if (!cryptohome_key_loader_->HasCryptohomeKey())
    cryptohome_key_loader_->Init();

  brillo::SecureBlob le_secret(kDefaultSecretSize);
  brillo::SecureBlob kdf_skey(kDefaultSecretSize);
  if (!DeriveSecretsScrypt(auth_input.user_input.value(), salt,
                           {&le_secret, &kdf_skey})) {
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocPinWeaverAuthBlockScryptDeriveFailedInCreate),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        CryptoError::CE_OTHER_CRYPTO);
  }

  // Create a randomly generated high entropy secret, derive VKKSeed from it,
  // and use that to generate a VKK. The High Entropy secret will be stored in
  // the LECredentialManager, along with the LE secret (which is |le_secret|
  // here).
  brillo::SecureBlob he_secret = CreateSecureRandomBlob(kDefaultSecretSize);

  // Derive the VKK_seed by performing an HMAC on he_secret.
  brillo::SecureBlob vkk_seed =
      HmacSha256(he_secret, brillo::BlobFromString(kHESecretHmacData));

  // Generate and store random new IVs for file-encryption keys and
  // chaps key encryption.
  const auto fek_iv = CreateSecureRandomBlob(kAesBlockSize);
  const auto chaps_iv = CreateSecureRandomBlob(kAesBlockSize);

  brillo::SecureBlob vkk_key = HmacSha256(kdf_skey, vkk_seed);

  key_blobs->vkk_key = vkk_key;
  key_blobs->vkk_iv = fek_iv;
  key_blobs->chaps_iv = chaps_iv;

  // Once we are able to correctly set up the VaultKeyset encryption,
  // store the Low Entropy and High Entropy credential in the
  // LECredentialManager.

  // Use the default delay schedule for now.
  std::map<uint32_t, uint32_t> delay_sched;
  for (const auto& entry : kDefaultDelaySchedule) {
    delay_sched[entry.attempts] = entry.delay;
  }

  std::vector<hwsec::OperationPolicySetting> policies = {
      hwsec::OperationPolicySetting{
          .device_config_settings =
              hwsec::DeviceConfigSettings{
                  .current_user =
                      hwsec::DeviceConfigSettings::CurrentUserSetting{
                          .username = std::nullopt,
                      },
              },
      },
      hwsec::OperationPolicySetting{
          .device_config_settings =
              hwsec::DeviceConfigSettings{
                  .current_user =
                      hwsec::DeviceConfigSettings::CurrentUserSetting{
                          .username = auth_input.obfuscated_username.value(),
                      },
              },
      },
  };

  uint64_t label;
  LECredStatus ret = le_manager_->InsertCredential(
      policies, le_secret, he_secret, reset_secret, delay_sched, &label);
  if (!ret.ok()) {
    LogLERetCode(ret->local_lecred_error());
    return MakeStatus<CryptohomeCryptoError>(
               CRYPTOHOME_ERR_LOC(
                   kLocPinWeaverAuthBlockInsertCredentialFailedInCreate))
        .Wrap(std::move(ret));
  }

  pin_auth_state.le_label = label;
  pin_auth_state.salt = std::move(salt);
  *auth_block_state = AuthBlockState{.state = std::move(pin_auth_state)};
  return OkStatus<CryptohomeCryptoError>();
}

CryptoStatus PinWeaverAuthBlock::Derive(const AuthInput& auth_input,
                                        const AuthBlockState& state,
                                        KeyBlobs* key_blobs) {
  if (!auth_input.user_input.has_value()) {
    LOG(ERROR) << "Missing user_input";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocPinWeaverAuthBlockNoUserInputInDerive),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        CryptoError::CE_OTHER_CRYPTO);
  }

  const PinWeaverAuthBlockState* auth_state;
  if (!(auth_state = std::get_if<PinWeaverAuthBlockState>(&state.state))) {
    LOG(ERROR) << "Invalid AuthBlockState";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocPinWeaverAuthBlockInvalidBlockStateInDerive),
        ErrorActionSet(
            {ErrorAction::kDevCheckUnexpectedState, ErrorAction::kAuth}),
        CryptoError::CE_OTHER_CRYPTO);
  }

  brillo::SecureBlob le_secret(kDefaultAesKeySize);
  brillo::SecureBlob kdf_skey(kDefaultAesKeySize);
  if (!auth_state->le_label.has_value()) {
    LOG(ERROR) << "Invalid PinWeaverAuthBlockState: missing le_label";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocPinWeaverAuthBlockNoLabelInDerive),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState,
                        ErrorAction::kAuth, ErrorAction::kDeleteVault}),
        CryptoError::CE_OTHER_CRYPTO);
  }
  if (!auth_state->salt.has_value()) {
    LOG(ERROR) << "Invalid PinWeaverAuthBlockState: missing salt";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocPinWeaverAuthBlockNoSaltInDerive),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState,
                        ErrorAction::kAuth, ErrorAction::kDeleteVault}),
        CryptoError::CE_OTHER_CRYPTO);
  }
  brillo::SecureBlob salt = auth_state->salt.value();
  if (!DeriveSecretsScrypt(auth_input.user_input.value(), salt,
                           {&le_secret, &kdf_skey})) {
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocPinWeaverAuthBlockDeriveScryptFailedInDerive),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        CryptoError::CE_OTHER_FATAL);
  }

  key_blobs->reset_secret = brillo::SecureBlob();
  // Note: Yes it is odd to pass the IV from the auth state into the key blobs
  // without performing any operation on the data. However, the fact that the
  // IVs are pre-generated in the VaultKeyset for PinWeaver credentials is an
  // implementation detail. The AuthBlocks are designed to hide those
  // implementation details, so this goes here.
  if (auth_state->chaps_iv.has_value()) {
    key_blobs->chaps_iv = auth_state->chaps_iv.value();
  }
  if (auth_state->fek_iv.has_value()) {
    key_blobs->vkk_iv = auth_state->fek_iv.value();
  }

  // Try to obtain the High Entropy Secret from the LECredentialManager.
  brillo::SecureBlob he_secret;
  LECredStatus ret = le_manager_->CheckCredential(
      auth_state->le_label.value(), le_secret, &he_secret,
      &key_blobs->reset_secret.value());

  if (!ret.ok()) {
    // Replace the error with CE_CREDENTIAL_LOCKED if it is caused by invalid LE
    // secret and locked.
    if (ret->local_lecred_error() == LE_CRED_ERROR_INVALID_LE_SECRET &&
        IsLocked(auth_state->le_label.value())) {
      return MakeStatus<CryptohomeCryptoError>(
                 CRYPTOHOME_ERR_LOC(
                     kLocPinWeaverAuthBlockCheckCredLockedInDerive),
                 ErrorActionSet({ErrorAction::kAuth}),
                 CryptoError::CE_CREDENTIAL_LOCKED)
          .Wrap(std::move(ret));
    }

    return MakeStatus<CryptohomeCryptoError>(
               CRYPTOHOME_ERR_LOC(
                   kLocPinWeaverAuthBlockCheckCredFailedInDerive))
        .Wrap(std::move(ret));
  }

  brillo::SecureBlob vkk_seed =
      HmacSha256(he_secret, brillo::BlobFromString(kHESecretHmacData));
  key_blobs->vkk_key = HmacSha256(kdf_skey, vkk_seed);

  return OkStatus<CryptohomeCryptoError>();
}

bool PinWeaverAuthBlock::IsLocked(uint64_t label) {
  LECredStatusOr<uint32_t> delay = le_manager_->GetDelayInSeconds(label);
  if (!delay.ok()) {
    LOG(ERROR)
        << "Failed to obtain the delay in seconds in pinweaver auth block: "
        << std::move(delay).status();
    return false;
  }

  // The pin is locked forever.
  if (delay.value() == kInfiniteDelay) {
    return true;
  }

  return false;
}

}  // namespace cryptohome
