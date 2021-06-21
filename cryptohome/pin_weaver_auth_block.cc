// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/pin_weaver_auth_block.h"

#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/cryptolib.h"
#include "cryptohome/tpm.h"
#include "cryptohome/vault_keyset.h"

#include <map>
#include <string>

#include <base/check.h>
#include <base/check_op.h>
#include <base/optional.h>
#include <brillo/secure_blob.h>

#include "cryptohome/crypto/aes.h"
#include "cryptohome/crypto/hmac.h"
#include "cryptohome/crypto/secure_blob_util.h"
#include "cryptohome/crypto/sha.h"
#include "cryptohome/vault_keyset.pb.h"

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
      return CryptoError::CE_OTHER_FATAL;
    case LE_CRED_ERROR_HASH_TREE:
      return CryptoError::CE_OTHER_FATAL;
    case LE_CRED_ERROR_PCR_NOT_MATCH:
      // We might want to return an error here that will make the device
      // reboot.
      LOG(ERROR) << "PCR in unexpected state.";
      return CryptoError::CE_LE_INVALID_SECRET;
    default:
      return CryptoError::CE_OTHER_FATAL;
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
  default_pcr_value.bitmask[kTpmSingleUserPCR / 8] = 1u << kTpmSingleUserPCR;
  default_pcr_value.digest =
      std::string(default_digest.begin(), default_digest.end());
  valid_pcr_criteria->push_back(default_pcr_value);

  ValidPcrValue extended_pcr_value;
  memset(extended_pcr_value.bitmask, 0, 2);
  extended_pcr_value.bitmask[kTpmSingleUserPCR / 8] = 1u << kTpmSingleUserPCR;
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
    LECredentialManager* le_manager, CryptohomeKeyLoader* cryptohome_key_loader)
    : AuthBlock(kLowEntropyCredential),
      le_manager_(le_manager),
      cryptohome_key_loader_(cryptohome_key_loader) {
  CHECK_NE(le_manager, nullptr);
  CHECK_NE(cryptohome_key_loader, nullptr);
}

base::Optional<AuthBlockState> PinWeaverAuthBlock::Create(
    const AuthInput& auth_input, KeyBlobs* key_blobs, CryptoError* error) {
  DCHECK(key_blobs);

  // TODO(kerrnel): This may not be needed, but is currently retained to
  // maintain the original logic.
  if (!cryptohome_key_loader_->HasCryptohomeKey())
    cryptohome_key_loader_->Init();

  brillo::SecureBlob le_secret(kDefaultSecretSize);
  brillo::SecureBlob kdf_skey(kDefaultSecretSize);
  if (!CryptoLib::DeriveSecretsScrypt(auth_input.user_input.value(),
                                      auth_input.salt.value(),
                                      {&le_secret, &kdf_skey})) {
    return base::nullopt;
  }

  // Create a randomly generated high entropy secret, derive VKKSeed from it,
  // and use that to generate a VKK. The HE secret will be stored in the
  // LECredentialManager, along with the LE secret (which is |le_secret| here).
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
  // store the LE and HE credential in the LECredentialManager.

  // Use the default delay schedule for now.
  std::map<uint32_t, uint32_t> delay_sched;
  for (const auto& entry : kDefaultDelaySchedule) {
    delay_sched[entry.attempts] = entry.delay;
  }

  brillo::SecureBlob reset_secret = auth_input.reset_secret.value();
  ValidPcrCriteria valid_pcr_criteria;
  if (!GetValidPCRValues(auth_input.obfuscated_username.value(),
                         &valid_pcr_criteria)) {
    return base::nullopt;
  }

  uint64_t label;
  int ret =
      le_manager_->InsertCredential(le_secret, he_secret, reset_secret,
                                    delay_sched, valid_pcr_criteria, &label);
  if (ret != LE_CRED_SUCCESS) {
    LogLERetCode(ret);
    PopulateError(error, ConvertLeError(ret));
    return base::nullopt;
  }

  AuthBlockState auth_state;
  AuthBlockState::PinWeaverAuthBlockState* pin_auth_state =
      auth_state.mutable_pin_weaver_state();
  pin_auth_state->set_le_label(label);
  return auth_state;
}

bool PinWeaverAuthBlock::Derive(const AuthInput& auth_input,
                                const AuthBlockState& state,
                                KeyBlobs* key_blobs,
                                CryptoError* error) {
  if (!state.has_pin_weaver_state()) {
    DLOG(FATAL) << "Invalid AuthBlockState";
    return false;
  }

  const AuthBlockState::PinWeaverAuthBlockState& auth_state =
      state.pin_weaver_state();

  brillo::SecureBlob le_secret(kDefaultAesKeySize);
  brillo::SecureBlob kdf_skey(kDefaultAesKeySize);
  brillo::SecureBlob salt(auth_state.salt().begin(), auth_state.salt().end());
  if (!CryptoLib::DeriveSecretsScrypt(auth_input.user_input.value(), salt,
                                      {&le_secret, &kdf_skey})) {
    PopulateError(error, CryptoError::CE_OTHER_FATAL);
    return false;
  }

  key_blobs->reset_secret = brillo::SecureBlob();
  // Note: Yes it is odd to pass the IV from the auth state into the key blobs
  // without performing any operation on the data. However, the fact that the
  // IVs are pre-generated in the VaultKeyset for PinWeaver credentials is an
  // implementation detail. The AuthBlocks are designed to hide those
  // implementation details, so this goes here.
  key_blobs->chaps_iv = brillo::SecureBlob(auth_state.chaps_iv().begin(),
                                           auth_state.chaps_iv().end());
  key_blobs->vkk_iv = brillo::SecureBlob(auth_state.fek_iv().begin(),
                                         auth_state.fek_iv().end());

  // Try to obtain the HE Secret from the LECredentialManager.
  brillo::SecureBlob he_secret;
  int ret =
      le_manager_->CheckCredential(auth_state.le_label(), le_secret, &he_secret,
                                   &key_blobs->reset_secret.value());

  if (ret != LE_CRED_SUCCESS) {
    PopulateError(error, ConvertLeError(ret));
    return false;
  }

  brillo::SecureBlob vkk_seed =
      HmacSha256(he_secret, brillo::BlobFromString(kHESecretHmacData));
  key_blobs->vkk_key = HmacSha256(kdf_skey, vkk_seed);

  return true;
}

}  // namespace cryptohome
