// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_blocks/pin_weaver_auth_block.h"

#include <libhwsec-foundation/crypto/scrypt.h>
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/tpm.h"
#include "cryptohome/vault_keyset.h"

#include <map>
#include <string>
#include <utility>
#include <variant>

#include <base/check.h>
#include <base/check_op.h>
#include <base/logging.h>
#include <brillo/secure_blob.h>
#include <libhwsec-foundation/crypto/aes.h>
#include <libhwsec-foundation/crypto/hmac.h>
#include <libhwsec-foundation/crypto/secure_blob_util.h>
#include <libhwsec-foundation/crypto/sha.h>

#include "cryptohome/auth_blocks/auth_block_state.h"
#include "cryptohome/vault_keyset.pb.h"

using ::hwsec_foundation::CreateSecureRandomBlob;
using ::hwsec_foundation::DeriveSecretsScrypt;
using ::hwsec_foundation::HmacSha256;
using ::hwsec_foundation::kAesBlockSize;
using ::hwsec_foundation::kDefaultAesKeySize;
using ::hwsec_foundation::Sha256;

namespace cryptohome {

namespace {

constexpr int kDefaultSecretSize = 32;

CryptoError ConvertLeError(int le_error) {
  switch (le_error) {
    case LE_CRED_ERROR_INVALID_LE_SECRET:
      return CryptoError::CE_LE_INVALID_SECRET;
    case LE_CRED_ERROR_TOO_MANY_ATTEMPTS:
      return CryptoError::CE_TPM_DEFEND_LOCK;
    case LE_CRED_ERROR_INVALID_LABEL:
      return CryptoError::CE_OTHER_CRYPTO;
    case LE_CRED_ERROR_HASH_TREE:
      // TODO(b/195473713): This should be CE_OTHER_FATAL, but return
      // CE_OTHER_CRYPTO here to prevent unintended user homedir removal.
      return CryptoError::CE_OTHER_CRYPTO;
    case LE_CRED_ERROR_PCR_NOT_MATCH:
      // We might want to return an error here that will make the device
      // reboot.
      LOG(ERROR) << "PCR in unexpected state.";
      return CryptoError::CE_LE_INVALID_SECRET;
    default:
      return CryptoError::CE_OTHER_CRYPTO;
  }
}

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

bool GetValidPCRValues(const std::string& obfuscated_username,
                       ValidPcrCriteria* valid_pcr_criteria) {
  brillo::Blob default_pcr_str(std::begin(kDefaultPcrValue),
                               std::end(kDefaultPcrValue));

  // The digest used for validation of PCR values by pinweaver is sha256 of
  // the current PCR value of index 4.
  // Step 1 - calculate expected values of PCR4 initially
  // (kDefaultPcrValue = 0) and after user logins
  // (sha256(initial_value | user_specific_digest)).
  // Step 2 - calculate digest of those values, to support multi-PCR case,
  // where all those expected values for all PCRs are sha256'ed togetheri
  brillo::Blob default_digest = Sha256(default_pcr_str);

  // The second valid digest is the one obtained from the future value of
  // PCR4, after it's extended by |obfuscated_username|. Compute the value of
  // PCR4 after it will be extended first, which is
  // sha256(default_value + sha256(extend_text)).
  brillo::Blob obfuscated_username_digest = Sha256(
      brillo::Blob(obfuscated_username.begin(), obfuscated_username.end()));
  brillo::Blob combined_pcr_and_username(default_pcr_str);
  combined_pcr_and_username.insert(
      combined_pcr_and_username.end(),
      std::make_move_iterator(obfuscated_username_digest.begin()),
      std::make_move_iterator(obfuscated_username_digest.end()));

  brillo::Blob extended_arc_pcr_value = Sha256(combined_pcr_and_username);

  // The second valid digest used by pinweaver for validation will be
  // sha256 of the extended value of pcr4.
  brillo::Blob extended_digest = Sha256(extended_arc_pcr_value);

  ValidPcrValue default_pcr_value;
  memset(default_pcr_value.bitmask, 0, 2);
  default_pcr_value.bitmask[kTpmSingleUserPCR / 8] = 1u
                                                     << (kTpmSingleUserPCR % 8);
  default_pcr_value.digest =
      std::string(default_digest.begin(), default_digest.end());
  valid_pcr_criteria->push_back(default_pcr_value);

  ValidPcrValue extended_pcr_value;
  memset(extended_pcr_value.bitmask, 0, 2);
  extended_pcr_value.bitmask[kTpmSingleUserPCR / 8] =
      1u << (kTpmSingleUserPCR % 8);
  extended_pcr_value.digest =
      std::string(extended_digest.begin(), extended_digest.end());
  valid_pcr_criteria->push_back(extended_pcr_value);

  return true;
}

// String used as vector in HMAC operation to derive vkk_seed from High Entropy
// secret.
const char kHESecretHmacData[] = "vkk_seed";

// A default delay schedule to be used for LE Credentials.
// The format for a delay schedule entry is as follows:
//
// (number_of_incorrect_attempts, delay before_next_attempt)
//
// The default schedule is for the first 5 incorrect attempts to have no delay,
// and no further attempts allowed.
const struct {
  uint32_t attempts;
  uint32_t delay;
} kDefaultDelaySchedule[] = {
    {5, UINT32_MAX},
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

CryptoError PinWeaverAuthBlock::Create(const AuthInput& auth_input,
                                       AuthBlockState* auth_block_state,
                                       KeyBlobs* key_blobs) {
  DCHECK(key_blobs);

  brillo::SecureBlob salt =
      CreateSecureRandomBlob(CRYPTOHOME_DEFAULT_KEY_SALT_SIZE);

  // TODO(kerrnel): This may not be needed, but is currently retained to
  // maintain the original logic.
  if (!cryptohome_key_loader_->HasCryptohomeKey())
    cryptohome_key_loader_->Init();

  brillo::SecureBlob le_secret(kDefaultSecretSize);
  brillo::SecureBlob kdf_skey(kDefaultSecretSize);
  if (!DeriveSecretsScrypt(auth_input.user_input.value(), salt,
                           {&le_secret, &kdf_skey})) {
    return CryptoError::CE_OTHER_CRYPTO;
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

  brillo::SecureBlob reset_secret = auth_input.reset_secret.value();
  ValidPcrCriteria valid_pcr_criteria;
  if (!GetValidPCRValues(auth_input.obfuscated_username.value(),
                         &valid_pcr_criteria)) {
    return CryptoError::CE_OTHER_CRYPTO;
  }

  uint64_t label;
  int ret =
      le_manager_->InsertCredential(le_secret, he_secret, reset_secret,
                                    delay_sched, valid_pcr_criteria, &label);
  if (ret != LE_CRED_SUCCESS) {
    LogLERetCode(ret);
    return ConvertLeError(ret);
  }

  PinWeaverAuthBlockState pin_auth_state;
  pin_auth_state.le_label = label;
  pin_auth_state.salt = std::move(salt);
  *auth_block_state = AuthBlockState{.state = std::move(pin_auth_state)};
  return CryptoError::CE_NONE;
}

CryptoError PinWeaverAuthBlock::Derive(const AuthInput& auth_input,
                                       const AuthBlockState& state,
                                       KeyBlobs* key_blobs) {
  const PinWeaverAuthBlockState* auth_state;
  if (!(auth_state = std::get_if<PinWeaverAuthBlockState>(&state.state))) {
    LOG(ERROR) << "Invalid AuthBlockState";
    return CryptoError::CE_OTHER_CRYPTO;
  }

  brillo::SecureBlob le_secret(kDefaultAesKeySize);
  brillo::SecureBlob kdf_skey(kDefaultAesKeySize);
  if (!auth_state->le_label.has_value()) {
    LOG(ERROR) << "Invalid PinWeaverAuthBlockState: missing le_label";
    return CryptoError::CE_OTHER_CRYPTO;
  }
  if (!auth_state->salt.has_value()) {
    LOG(ERROR) << "Invalid PinWeaverAuthBlockState: missing salt";
    return CryptoError::CE_OTHER_CRYPTO;
  }
  brillo::SecureBlob salt = auth_state->salt.value();
  if (!DeriveSecretsScrypt(auth_input.user_input.value(), salt,
                           {&le_secret, &kdf_skey})) {
    return CryptoError::CE_OTHER_FATAL;
  }

  key_blobs->reset_secret = brillo::SecureBlob();
  // Note: Yes it is odd to pass the IV from the auth state into the key blobs
  // without performing any operation on the data. However, the fact that the
  // IVs are pre-generated in the VaultKeyset for PinWeaver credentials is an
  // implementation detail. The AuthBlocks are designed to hide those
  // implementation details, so this goes here.
  key_blobs->chaps_iv = auth_state->chaps_iv.value();
  key_blobs->vkk_iv = auth_state->fek_iv.value();

  // Try to obtain the High Entropy Secret from the LECredentialManager.
  brillo::SecureBlob he_secret;
  int ret = le_manager_->CheckCredential(auth_state->le_label.value(),
                                         le_secret, &he_secret,
                                         &key_blobs->reset_secret.value());

  if (ret != LE_CRED_SUCCESS) {
    return ConvertLeError(ret);
  }

  brillo::SecureBlob vkk_seed =
      HmacSha256(he_secret, brillo::BlobFromString(kHESecretHmacData));
  key_blobs->vkk_key = HmacSha256(kdf_skey, vkk_seed);

  return CryptoError::CE_NONE;
}

}  // namespace cryptohome
