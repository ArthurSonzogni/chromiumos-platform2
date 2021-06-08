// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/vault_keyset.h"

#include <sys/types.h>

#include <base/check.h>
#include <base/check_op.h>
#include <base/files/file_path.h>
#include <base/logging.h>
#include <brillo/secure_blob.h>

#include "cryptohome/auth_block_state.pb.h"
#include "cryptohome/crypto/hmac.h"
#include "cryptohome/crypto/secure_blob_util.h"
#include "cryptohome/crypto_error.h"
#include "cryptohome/key_objects.h"
#include "cryptohome/platform.h"

using base::FilePath;
using brillo::SecureBlob;

namespace {
const mode_t kVaultFilePermissions = 0600;
const char kKeyLegacyPrefix[] = "legacy-";
}

namespace cryptohome {

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

    FilePath timestamp_path = filename.AddExtension("timestamp");
    brillo::Blob tcontents;
    // If we fail to read the ts file, just use whatever is stored in the
    // serialized field.
    if (platform_->ReadFile(timestamp_path, &tcontents)) {
      cryptohome::Timestamp timestamp;
      if (timestamp.ParseFromArray(tcontents.data(), tcontents.size())) {
        last_activity_timestamp_ = timestamp.timestamp();
      } else {
        LOG(WARNING) << "Failure to parse timestamp file: " << timestamp_path;
      }
    } else {
      LOG(WARNING) << "Failure to read timestamp file: " << timestamp_path;
    }
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
  bool ok = crypto_->DecryptVaultKeyset(this, key, locked_to_single_user,
                                        nullptr, &local_crypto_error);
  if (!ok && local_crypto_error == CryptoError::CE_TPM_COMM_ERROR) {
    ok = crypto_->DecryptVaultKeyset(this, key, locked_to_single_user, nullptr,
                                     &local_crypto_error);
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

void VaultKeyset::SetTpmNotBoundToPcrState(
    const AuthBlockState::TpmNotBoundToPcrAuthBlockState& auth_state) {
  flags_ = SerializedVaultKeyset::TPM_WRAPPED;
  if (auth_state.has_scrypt_derived() && auth_state.scrypt_derived()) {
    flags_ |= SerializedVaultKeyset::SCRYPT_DERIVED;
  }
  if (auth_state.has_tpm_key()) {
    tpm_key_ = brillo::SecureBlob(auth_state.tpm_key().begin(),
                                  auth_state.tpm_key().end());
  }
  if (auth_state.has_tpm_public_key_hash()) {
    tpm_public_key_hash_ =
        brillo::SecureBlob(auth_state.tpm_public_key_hash().begin(),
                           auth_state.tpm_public_key_hash().end());
  }
}

void VaultKeyset::SetTpmBoundToPcrState(
    const AuthBlockState::TpmBoundToPcrAuthBlockState& auth_state) {
  flags_ =
      SerializedVaultKeyset::TPM_WRAPPED | SerializedVaultKeyset::PCR_BOUND;
  if (auth_state.has_scrypt_derived() && auth_state.scrypt_derived()) {
    flags_ |= SerializedVaultKeyset::SCRYPT_DERIVED;
  }
  if (auth_state.has_tpm_key()) {
    tpm_key_ = brillo::SecureBlob(auth_state.tpm_key().begin(),
                                  auth_state.tpm_key().end());
  }
  if (auth_state.has_extended_tpm_key()) {
    extended_tpm_key_ =
        brillo::SecureBlob(auth_state.extended_tpm_key().begin(),
                           auth_state.extended_tpm_key().end());
  }
  if (auth_state.has_tpm_public_key_hash()) {
    tpm_public_key_hash_ =
        brillo::SecureBlob(auth_state.tpm_public_key_hash().begin(),
                           auth_state.tpm_public_key_hash().end());
  }
}

void VaultKeyset::SetPinWeaverState(
    const AuthBlockState::PinWeaverAuthBlockState& auth_state) {
  flags_ = SerializedVaultKeyset::LE_CREDENTIAL;
  if (auth_state.has_le_label()) {
    le_label_ = auth_state.le_label();
  }
}

void VaultKeyset::SetLibScryptCompatState(
    const AuthBlockState::LibScryptCompatAuthBlockState& auth_state) {
  flags_ = SerializedVaultKeyset::SCRYPT_WRAPPED;
}

void VaultKeyset::SetChallengeCredentialState(
    const AuthBlockState::ChallengeCredentialAuthBlockState& auth_state) {
  flags_ = SerializedVaultKeyset::SCRYPT_WRAPPED |
           SerializedVaultKeyset::SIGNATURE_CHALLENGE_PROTECTED;
}

void VaultKeyset::SetAuthBlockState(const AuthBlockState& auth_state) {
  switch (auth_state.auth_block_state_case()) {
    case AuthBlockState::kTpmNotBoundToPcrState:
      SetTpmNotBoundToPcrState(auth_state.tpm_not_bound_to_pcr_state());
      return;
    case AuthBlockState::kTpmBoundToPcrState:
      SetTpmBoundToPcrState(auth_state.tpm_bound_to_pcr_state());
      return;
    case AuthBlockState::kPinWeaverState:
      SetPinWeaverState(auth_state.pin_weaver_state());
      return;
    case AuthBlockState::kLibscryptCompatState:
      SetLibScryptCompatState(auth_state.libscrypt_compat_state());
      return;
    case AuthBlockState::kChallengeCredentialState:
      SetChallengeCredentialState(auth_state.challenge_credential_state());
      return;
    default:
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

  AuthBlockState::TpmBoundToPcrAuthBlockState* state =
      auth_state->mutable_tpm_bound_to_pcr_state();
  state->set_scrypt_derived((flags_ & SerializedVaultKeyset::SCRYPT_DERIVED) !=
                            0);
  state->set_salt(salt_.data(), salt_.size());
  state->set_tpm_key(tpm_key_->data(), tpm_key_->size());
  state->set_extended_tpm_key(extended_tpm_key_->data(),
                              extended_tpm_key_->size());
  if (tpm_public_key_hash_.has_value()) {
    state->set_tpm_public_key_hash(tpm_public_key_hash_->data(),
                                   tpm_public_key_hash_->size());
  }
  if (wrapped_reset_seed_.has_value()) {
    state->set_wrapped_reset_seed(wrapped_reset_seed_->data(),
                                  wrapped_reset_seed_->size());
  }
  return true;
}

bool VaultKeyset::GetTpmNotBoundToPcrState(AuthBlockState* auth_state) const {
  // The AuthBlock can function without the |tpm_public_key_hash_|, but not
  // without the |tpm_key_|.
  if (!tpm_key_.has_value()) {
    return false;
  }

  AuthBlockState::TpmNotBoundToPcrAuthBlockState* state =
      auth_state->mutable_tpm_not_bound_to_pcr_state();
  state->set_scrypt_derived((flags_ & SerializedVaultKeyset::SCRYPT_DERIVED) !=
                            0);
  state->set_salt(salt_.data(), salt_.size());
  if (password_rounds_.has_value()) {
    state->set_password_rounds(password_rounds_.value());
  }
  state->set_tpm_key(tpm_key_->data(), tpm_key_->size());
  if (tpm_public_key_hash_.has_value()) {
    state->set_tpm_public_key_hash(tpm_public_key_hash_->data(),
                                   tpm_public_key_hash_->size());
  }
  if (wrapped_reset_seed_.has_value()) {
    state->set_wrapped_reset_seed(wrapped_reset_seed_->data(),
                                  wrapped_reset_seed_->size());
  }
  return true;
}

bool VaultKeyset::GetPinWeaverState(AuthBlockState* auth_state) const {
  // If the LE Label is missing, the AuthBlock cannot function.
  if (!le_label_.has_value()) {
    return false;
  }

  AuthBlockState::PinWeaverAuthBlockState* state =
      auth_state->mutable_pin_weaver_state();
  state->set_salt(salt_.data(), salt_.size());
  if (le_label_.has_value()) {
    state->set_le_label(le_label_.value());
  }
  if (le_chaps_iv_.has_value()) {
    state->set_chaps_iv(le_chaps_iv_->data(), le_chaps_iv_->size());
  }
  if (le_fek_iv_.has_value()) {
    state->set_fek_iv(le_fek_iv_->data(), le_fek_iv_->size());
  }
  return true;
}

bool VaultKeyset::GetSignatureChallengeState(AuthBlockState* auth_state) const {
  AuthBlockState scrypt_state;
  if (!GetLibScryptCompatState(&scrypt_state)) {
    return false;
  }

  *(auth_state->mutable_challenge_credential_state()->mutable_scrypt_state()) =
      scrypt_state.libscrypt_compat_state();
  return true;
}

bool VaultKeyset::GetLibScryptCompatState(AuthBlockState* auth_state) const {
  AuthBlockState::LibScryptCompatAuthBlockState* state =
      auth_state->mutable_libscrypt_compat_state();

  state->set_wrapped_keyset(wrapped_keyset_.data(), wrapped_keyset_.size());
  if (wrapped_chaps_key_.has_value()) {
    state->set_wrapped_chaps_key(wrapped_chaps_key_->data(),
                                 wrapped_chaps_key_->size());
  }
  if (wrapped_reset_seed_.has_value()) {
    state->set_wrapped_reset_seed(wrapped_reset_seed_->data(),
                                  wrapped_reset_seed_->size());
  }
  return true;
}

bool VaultKeyset::GetDoubleWrappedCompatState(
    AuthBlockState* auth_state) const {
  AuthBlockState::DoubleWrappedCompatAuthBlockState* state =
      auth_state->mutable_double_wrapped_compat_state();
  AuthBlockState scrypt_state;
  if (!GetLibScryptCompatState(&scrypt_state)) {
    return false;
  }
  *(state->mutable_scrypt_state()) = scrypt_state.libscrypt_compat_state();

  AuthBlockState tpm_state;
  if (!GetTpmNotBoundToPcrState(&tpm_state)) {
    return false;
  }
  *(state->mutable_tpm_state()) = tpm_state.tpm_not_bound_to_pcr_state();

  return true;
}

bool VaultKeyset::GetAuthBlockState(AuthBlockState* auth_state) const {
  // First case, handle a group of users with keysets that were incorrectly
  // flagged as being both TPM and scrypt wrapped.
  if ((flags_ & SerializedVaultKeyset::SCRYPT_WRAPPED) &&
      (flags_ & SerializedVaultKeyset::TPM_WRAPPED)) {
    return GetDoubleWrappedCompatState(auth_state);
  } else if (flags_ & SerializedVaultKeyset::TPM_WRAPPED &&
             flags_ & SerializedVaultKeyset::PCR_BOUND) {
    return GetTpmBoundToPcrState(auth_state);
  } else if (flags_ & SerializedVaultKeyset::TPM_WRAPPED) {
    return GetTpmNotBoundToPcrState(auth_state);
  } else if (flags_ & SerializedVaultKeyset::LE_CREDENTIAL) {
    return GetPinWeaverState(auth_state);
  } else if (flags_ & SerializedVaultKeyset::SIGNATURE_CHALLENGE_PROTECTED) {
    return GetSignatureChallengeState(auth_state);
  } else if (flags_ & SerializedVaultKeyset::SCRYPT_WRAPPED) {
    return GetLibScryptCompatState(auth_state);
  } else {
    LOG(ERROR) << "Unknown auth block type for flags " << flags_;
    return false;
  }
}

void VaultKeyset::SetWrappedKeyMaterial(
    const WrappedKeyMaterial& key_material) {
  if (IsLECredential() && key_material.vkk_iv.has_value()) {
    le_fek_iv_ = key_material.vkk_iv;
  }
  if (key_material.wrapped_keyset.has_value()) {
    wrapped_keyset_ = key_material.wrapped_keyset.value();
  }
  if (IsLECredential() && key_material.chaps_iv.has_value()) {
    le_chaps_iv_ = key_material.chaps_iv;
  }
  if (key_material.wrapped_chaps_key.has_value()) {
    wrapped_chaps_key_ = key_material.wrapped_chaps_key;
  }
  if (key_material.reset_iv.has_value()) {
    reset_iv_ = key_material.reset_iv;
  }
  if (key_material.wrapped_reset_seed.has_value()) {
    wrapped_reset_seed_ = key_material.wrapped_reset_seed;
  }
}

bool VaultKeyset::Encrypt(const SecureBlob& key,
                          const std::string& obfuscated_username) {
  CHECK(crypto_);
  salt_ = CreateSecureRandomBlob(CRYPTOHOME_DEFAULT_KEY_SALT_SIZE);

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
  }

  AuthBlockState auth_block_state;
  WrappedKeyMaterial wrapped;
  encrypted_ = crypto_->EncryptVaultKeyset(
      *this, key, salt_, obfuscated_username, &auth_block_state, &wrapped);

  if (encrypted_) {
    SetAuthBlockState(auth_block_state);
    SetWrappedKeyMaterial(wrapped);
  }

  return encrypted_;
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

int VaultKeyset::GetFscryptPolicyVersion() {
  return fscrypt_policy_version_.value_or(-1);
}

void VaultKeyset::SetFscryptPolicyVersion(int policy_version) {
  fscrypt_policy_version_ = policy_version;
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

bool VaultKeyset::HasLastActivityTimestamp() const {
  return last_activity_timestamp_.has_value();
}

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
  serialized.set_salt(salt_.data(), salt_.size());
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

  if (last_activity_timestamp_.has_value()) {
    serialized.set_last_activity_timestamp(last_activity_timestamp_.value());
  }

  if (key_data_.has_value()) {
    *(serialized.mutable_key_data()) = key_data_.value();
  }

  if (auth_locked_) {
    serialized.mutable_key_data()->mutable_policy()->set_auth_locked(
        auth_locked_);
  }

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

  return serialized;
}

void VaultKeyset::ResetVaultKeyset() {
  flags_ = -1;
  salt_.clear();
  legacy_index_ = -1;
  tpm_public_key_hash_.reset();
  password_rounds_.reset();
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
  salt_ =
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

const brillo::SecureBlob& VaultKeyset::GetSalt() const {
  return salt_;
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
