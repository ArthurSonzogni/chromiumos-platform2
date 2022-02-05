// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/keyset_management.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/timer/elapsed_timer.h>
#include <brillo/secure_blob.h>
#include <chromeos/constants/cryptohome.h>
#include <dbus/cryptohome/dbus-constants.h>

#include "cryptohome/credentials.h"
#include "cryptohome/crypto.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/filesystem_layout.h"
#include "cryptohome/platform.h"
#include "cryptohome/storage/homedirs.h"
#include "cryptohome/timestamp.pb.h"
#include "cryptohome/vault_keyset.h"
#include "cryptohome/vault_keyset_factory.h"

using base::FilePath;

namespace cryptohome {

KeysetManagement::KeysetManagement(
    Platform* platform,
    Crypto* crypto,
    const brillo::SecureBlob& system_salt,
    std::unique_ptr<VaultKeysetFactory> vault_keyset_factory)
    : platform_(platform),
      crypto_(crypto),
      system_salt_(system_salt),
      vault_keyset_factory_(std::move(vault_keyset_factory)) {}

bool KeysetManagement::AreCredentialsValid(const Credentials& creds) {
  std::unique_ptr<VaultKeyset> vk = GetValidKeyset(creds, nullptr /* error */);
  return vk.get() != nullptr;
}

std::unique_ptr<VaultKeyset> KeysetManagement::GetValidKeyset(
    const Credentials& creds, MountError* error) {
  if (error)
    *error = MOUNT_ERROR_NONE;

  std::string obfuscated = creds.GetObfuscatedUsername(system_salt_);

  std::vector<int> key_indices;
  if (!GetVaultKeysets(obfuscated, &key_indices)) {
    LOG(WARNING) << "No valid keysets on disk for " << obfuscated;
    if (error)
      *error = MOUNT_ERROR_VAULT_UNRECOVERABLE;
    return nullptr;
  }

  bool any_keyset_exists = false;
  CryptoError last_crypto_error = CryptoError::CE_NONE;
  for (int index : key_indices) {
    std::unique_ptr<VaultKeyset> vk = LoadVaultKeysetForUser(obfuscated, index);
    if (!vk) {
      continue;
    }
    any_keyset_exists = true;
    // Skip decrypt attempts if the label doesn't match.
    // Treat an empty creds label as a wildcard.
    if (!creds.key_data().label().empty() &&
        creds.key_data().label() != vk->GetLabel())
      continue;
    // Skip LE Credentials if not explicitly identified by a label, since we
    // don't want unnecessary wrong attempts.
    if (creds.key_data().label().empty() &&
        (vk->GetFlags() & SerializedVaultKeyset::LE_CREDENTIAL)) {
      continue;
    }
    bool locked_to_single_user =
        platform_->FileExists(base::FilePath(kLockedToSingleUserFile));
    if (vk->Decrypt(creds.passkey(), locked_to_single_user,
                    &last_crypto_error)) {
      return vk;
    }
  }

  MountError local_error = MOUNT_ERROR_NONE;
  if (!any_keyset_exists) {
    LOG(ERROR) << "No parsable keysets found for " << obfuscated;
    local_error = MOUNT_ERROR_VAULT_UNRECOVERABLE;
  } else if (last_crypto_error == CryptoError::CE_NONE) {
    // If we're searching by label, don't let a no-key-found become
    // MOUNT_ERROR_FATAL.  In the past, no parseable key was a fatal
    // error.  Just treat it like an invalid key.  This allows for
    // multiple per-label requests then a wildcard, worst case, before
    // the Cryptohome is removed.
    if (!creds.key_data().label().empty()) {
      LOG(ERROR) << "Failed to find the specified keyset for " << obfuscated;
      local_error = MOUNT_ERROR_KEY_FAILURE;
    } else {
      LOG(ERROR) << "Failed to find any suitable keyset for " << obfuscated;
      local_error = MOUNT_ERROR_FATAL;
    }
  } else {
    switch (last_crypto_error) {
      case CryptoError::CE_TPM_FATAL:
      case CryptoError::CE_OTHER_FATAL:
        local_error = MOUNT_ERROR_VAULT_UNRECOVERABLE;
        break;
      case CryptoError::CE_TPM_COMM_ERROR:
        local_error = MOUNT_ERROR_TPM_COMM_ERROR;
        break;
      case CryptoError::CE_TPM_DEFEND_LOCK:
        local_error = MOUNT_ERROR_TPM_DEFEND_LOCK;
        break;
      case CryptoError::CE_TPM_REBOOT:
        local_error = MOUNT_ERROR_TPM_NEEDS_REBOOT;
        break;
      default:
        local_error = MOUNT_ERROR_KEY_FAILURE;
        break;
    }
    LOG(ERROR) << "Failed to decrypt any keysets for " << obfuscated
               << ": mount error " << local_error << ", crypto error "
               << last_crypto_error;
  }
  if (error)
    *error = local_error;
  return nullptr;
}

std::unique_ptr<VaultKeyset> KeysetManagement::GetVaultKeyset(
    const std::string& obfuscated_username,
    const std::string& key_label) const {
  if (key_label.empty())
    return NULL;

  // Walk all indices to find a match.
  // We should move to label-derived suffixes to be efficient.
  std::vector<int> key_indices;
  if (!GetVaultKeysets(obfuscated_username, &key_indices)) {
    return NULL;
  }
  for (int index : key_indices) {
    std::unique_ptr<VaultKeyset> vk =
        LoadVaultKeysetForUser(obfuscated_username, index);
    if (!vk) {
      continue;
    }
    if (vk->GetLabel() == key_label) {
      return vk;
    }
  }
  return NULL;
}

// TODO(wad) Figure out how this might fit in with vault_keyset.cc
bool KeysetManagement::GetVaultKeysets(const std::string& obfuscated,
                                       std::vector<int>* keysets) const {
  CHECK(keysets);
  base::FilePath user_dir = UserPath(obfuscated);

  std::unique_ptr<FileEnumerator> file_enumerator(platform_->GetFileEnumerator(
      user_dir, false, base::FileEnumerator::FILES));
  base::FilePath next_path;
  while (!(next_path = file_enumerator->Next()).empty()) {
    base::FilePath file_name = next_path.BaseName();
    // Scan for "master." files. // nocheck
    if (file_name.RemoveFinalExtension().value() != kKeyFile) {
      continue;
    }
    std::string index_str = file_name.FinalExtension();
    int index;
    if (!base::StringToInt(&index_str[1], &index)) {
      continue;
    }
    // The test below will catch all strtol(3) error conditions.
    if (index < 0 || index >= kKeyFileMax) {
      LOG(ERROR) << "Invalid key file range: " << index;
      continue;
    }
    keysets->push_back(static_cast<int>(index));
  }

  // Ensure it is sorted numerically and not lexigraphically.
  std::sort(keysets->begin(), keysets->end());

  return keysets->size() != 0;
}

bool KeysetManagement::GetVaultKeysetLabelsAndData(
    const std::string& obfuscated_username,
    std::map<std::string, KeyData>* key_label_data) const {
  CHECK(key_label_data);
  base::FilePath user_dir = UserPath(obfuscated_username);

  // Struct to records various metrics for UMA.
  VaultKeysetMetrics keyset_metrics;

  std::unique_ptr<FileEnumerator> file_enumerator(platform_->GetFileEnumerator(
      user_dir, false /* Not recursive. */, base::FileEnumerator::FILES));
  base::FilePath next_path;
  while (!(next_path = file_enumerator->Next()).empty()) {
    base::FilePath file_name = next_path.BaseName();
    // Scan for key files.
    if (file_name.RemoveFinalExtension().value() != kKeyFile) {
      continue;
    }
    int index = 0;
    std::string index_str = file_name.FinalExtension();
    // StringToInt will only return true for a perfect conversion.
    if (!base::StringToInt(&index_str[1], &index)) {
      continue;
    }
    if (index < 0 || index >= kKeyFileMax) {
      LOG(ERROR) << "Invalid key file range: " << index;
      continue;
    }
    // Now parse the keyset to get its label and keydata or skip it. The
    // VaultKeyset will not be decrypted during this step.
    std::unique_ptr<VaultKeyset> vk =
        LoadVaultKeysetForUser(obfuscated_username, index);
    if (!vk) {
      continue;
    }
    if (!RecordVaultKeysetMetrics(*vk.get(), keyset_metrics)) {
      LOG(ERROR) << "Metrics not recorded for " << vk->GetLabel();
    }
    if (key_label_data->find(vk->GetLabel()) != key_label_data->end()) {
      // This is a confirmation check, we do not expect to hit this.
      LOG(INFO) << "Found a duplicate label, skipping it: " << vk->GetLabel();
      continue;
    }
    key_label_data->insert({vk->GetLabel(), vk->GetKeyData()});
  }

  // Report VaultKeyset metrics through UMA.
  ReportVaultKeysetMetrics(keyset_metrics);

  return (key_label_data->size() > 0);
}

bool KeysetManagement::GetVaultKeysetLabels(
    const std::string& obfuscated_username,
    std::vector<std::string>* labels) const {
  CHECK(labels);
  base::FilePath user_dir = UserPath(obfuscated_username);

  // Struct to records various metrics for UMA.
  VaultKeysetMetrics keyset_metrics;

  std::unique_ptr<FileEnumerator> file_enumerator(platform_->GetFileEnumerator(
      user_dir, false /* Not recursive. */, base::FileEnumerator::FILES));
  base::FilePath next_path;
  while (!(next_path = file_enumerator->Next()).empty()) {
    base::FilePath file_name = next_path.BaseName();
    // Scan for "master." files. // nocheck
    if (file_name.RemoveFinalExtension().value() != kKeyFile) {
      continue;
    }
    int index = 0;
    std::string index_str = file_name.FinalExtension();
    // StringToInt will only return true for a perfect conversion.
    if (!base::StringToInt(&index_str[1], &index)) {
      continue;
    }
    if (index < 0 || index >= kKeyFileMax) {
      LOG(ERROR) << "Invalid key file range: " << index;
      continue;
    }
    // Now parse the keyset to get its label or skip it.
    std::unique_ptr<VaultKeyset> vk =
        LoadVaultKeysetForUser(obfuscated_username, index);
    if (!vk) {
      continue;
    }
    if (!RecordVaultKeysetMetrics(*vk.get(), keyset_metrics)) {
      LOG(ERROR) << "Metrics not recorded for " << vk->GetLabel();
    }

    labels->push_back(vk->GetLabel());
  }

  // Report VaultKeyset metrics through UMA.
  ReportVaultKeysetMetrics(keyset_metrics);

  return (labels->size() > 0);
}

std::unique_ptr<VaultKeyset> KeysetManagement::AddInitialKeyset(
    const Credentials& credentials) {
  const brillo::SecureBlob passkey = credentials.passkey();
  std::string obfuscated_username =
      credentials.GetObfuscatedUsername(system_salt_);

  std::unique_ptr<VaultKeyset> vk(
      vault_keyset_factory_->New(platform_, crypto_));
  vk->Initialize(platform_, crypto_);
  vk->CreateRandom();
  vk->SetLegacyIndex(kInitialKeysetIndex);
  vk->SetKeyData(credentials.key_data());

  if (credentials.key_data().type() == KeyData::KEY_TYPE_CHALLENGE_RESPONSE) {
    vk->SetFlags(vk->GetFlags() |
                 SerializedVaultKeyset::SIGNATURE_CHALLENGE_PROTECTED);
    vk->SetSignatureChallengeInfo(
        credentials.challenge_credentials_keyset_info());
  }

  if (!vk->Encrypt(passkey, obfuscated_username) ||
      !vk->Save(VaultKeysetPath(obfuscated_username, kInitialKeysetIndex))) {
    LOG(ERROR) << "Failed to encrypt and write keyset for the new user.";
    return nullptr;
  }
  return vk;
}

bool KeysetManagement::ShouldReSaveKeyset(VaultKeyset* vault_keyset) const {
  // If the vault keyset's TPM state is not the same as that configured for
  // the device, re-save the keyset (this will save in the device's default
  // method).
  // In the table below: X = true, - = false, * = any value
  //
  //                 1   2   3   4   5   6   7   8   9
  // should_tpm      X   X   X   X   -   -   -   *   X
  //
  // pcr_bound       -   X   *   -   -   *   -   *   -
  //
  // tpm_wrapped     -   X   X   -   -   X   -   X   *
  //
  // scrypt_wrapped  -   -   -   X   -   -   X   X   *
  //
  // scrypt_derived  *   X   -   *   *   *   *   *   *
  //
  // migrate         Y   N   Y   Y   Y   Y   N   Y   Y
  //
  // If the vault keyset is signature-challenge protected, we should not
  // re-encrypt it at all (that is unnecessary).
  const unsigned crypt_flags = vault_keyset->GetFlags();
  bool pcr_bound = (crypt_flags & SerializedVaultKeyset::PCR_BOUND) != 0;
  bool tpm_wrapped = (crypt_flags & SerializedVaultKeyset::TPM_WRAPPED) != 0;
  bool scrypt_wrapped =
      (crypt_flags & SerializedVaultKeyset::SCRYPT_WRAPPED) != 0;
  bool scrypt_derived =
      (crypt_flags & SerializedVaultKeyset::SCRYPT_DERIVED) != 0;
  bool is_signature_challenge_protected =
      (crypt_flags & SerializedVaultKeyset::SIGNATURE_CHALLENGE_PROTECTED) != 0;
  bool should_tpm = (crypto_->is_cryptohome_key_loaded() &&
                     !is_signature_challenge_protected);
  bool can_unseal_with_user_auth = crypto_->CanUnsealWithUserAuth();
  bool has_tpm_public_key_hash = vault_keyset->HasTpmPublicKeyHash();

  if (is_signature_challenge_protected) {
    return false;
  }

  bool is_le_credential =
      (crypt_flags & SerializedVaultKeyset::LE_CREDENTIAL) != 0;
  if (is_le_credential &&
      !crypto_->NeedsPcrBinding(vault_keyset->GetLELabel())) {
    return false;
  }

  // If the keyset was TPM-wrapped, but there was no public key hash,
  // always re-save.
  if (tpm_wrapped && !has_tpm_public_key_hash) {
    LOG(INFO) << "Migrating keyset " << vault_keyset->GetLegacyIndex()
              << " as there is no public hash";
    return true;
  }

  // Check the table.
  if (tpm_wrapped && should_tpm && scrypt_derived && !scrypt_wrapped) {
    if ((pcr_bound && can_unseal_with_user_auth) ||
        (!pcr_bound && !can_unseal_with_user_auth)) {
      return false;  // 2
    }
  }
  if (scrypt_wrapped && !should_tpm && !tpm_wrapped)
    return false;  // 7

  LOG(INFO) << "Migrating keyset " << vault_keyset->GetLegacyIndex()
            << ": should_tpm=" << should_tpm
            << ", has_hash=" << has_tpm_public_key_hash
            << ", flags=" << crypt_flags << ", pcr_bound=" << pcr_bound
            << ", can_unseal_with_user_auth=" << can_unseal_with_user_auth;

  return true;
}

bool KeysetManagement::ReSaveKeyset(const Credentials& credentials,
                                    VaultKeyset* keyset) const {
  // Save the initial keyset so we can roll-back any changes if we
  // failed to re-save.
  VaultKeyset old_keyset;
  old_keyset = *keyset;

  std::string obfuscated_username =
      credentials.GetObfuscatedUsername(system_salt_);

  // We get the LE Label from keyset before we resave it. Once the keyset
  // is re-saved, a new label is generated making the old label obsolete.
  // It would be safe to delete that from PinWeaver tree after resave.
  std::optional<uint64_t> old_le_label;
  if (keyset->HasLELabel()) {
    old_le_label = keyset->GetLELabel();
  }

  if (!keyset->Encrypt(credentials.passkey(), obfuscated_username) ||
      !keyset->Save(keyset->GetSourceFile())) {
    LOG(ERROR) << "Failed to encrypt and write the keyset.";
    *keyset = old_keyset;
    return false;
  }

  if ((keyset->GetFlags() & SerializedVaultKeyset::LE_CREDENTIAL) != 0 &&
      old_le_label.has_value()) {
    CHECK_NE(old_le_label.value(), keyset->GetLELabel());
    if (!crypto_->RemoveLECredential(old_le_label.value())) {
      // This is non-fatal error.
      LOG(ERROR) << "Failed to remove label = " << old_le_label.value();
    }
  }

  return true;
}

bool KeysetManagement::ReSaveKeysetIfNeeded(const Credentials& credentials,
                                            VaultKeyset* keyset) const {
  // Calling EnsureTpm here handles the case where a user logged in while
  // cryptohome was taking TPM ownership.  In that case, their vault keyset
  // would be scrypt-wrapped and the TPM would not be connected.  If we're
  // configured to use the TPM, calling EnsureTpm will try to connect, and
  // if successful, the call to has_tpm() below will succeed, allowing
  // re-wrapping (migration) using the TPM.
  crypto_->EnsureTpm(false);

  bool force_resave = false;
  if (!keyset->HasWrappedChapsKey()) {
    keyset->CreateRandomChapsKey();
    force_resave = true;
  }

  if (force_resave || ShouldReSaveKeyset(keyset)) {
    return ReSaveKeyset(credentials, keyset);
  }

  return true;
}

CryptohomeErrorCode KeysetManagement::AddKeyset(
    const Credentials& new_credentials,
    const VaultKeyset& vault_keyset,
    bool clobber) {
  std::string obfuscated_username =
      new_credentials.GetObfuscatedUsername(system_salt_);

  // Walk the namespace looking for the first free spot.
  // Note, nothing is stopping simultaneous access to these files
  // or enforcing mandatory locking.
  base::FilePath vk_path;
  bool file_found = false;
  for (int new_index = 0; new_index < kKeyFileMax; ++new_index) {
    vk_path = VaultKeysetPath(obfuscated_username, new_index);
    // Rely on fopen()'s O_EXCL|O_CREAT behavior to fail
    // repeatedly until there is an opening.
    base::ScopedFILE vk_file(platform_->OpenFile(vk_path, "wx"));
    if (vk_file) {  // got one
      file_found = true;
      break;
    }
  }

  if (!file_found) {
    LOG(WARNING) << "Failed to find an available keyset slot";
    return CRYPTOHOME_ERROR_KEY_QUOTA_EXCEEDED;
  }

  // Before persisting, check if there is an existing labeled credential.
  std::unique_ptr<VaultKeyset> match =
      GetVaultKeyset(obfuscated_username, new_credentials.key_data().label());
  if (match.get()) {
    LOG(INFO) << "Label already exists.";
    platform_->DeleteFile(vk_path);
    if (!clobber) {
      return CRYPTOHOME_ERROR_KEY_LABEL_EXISTS;
    }
    vk_path = match->GetSourceFile();
  }

  std::unique_ptr<VaultKeyset> keyset_to_add(
      vault_keyset_factory_->New(platform_, crypto_));
  keyset_to_add->InitializeToAdd(vault_keyset);
  keyset_to_add->SetKeyData(new_credentials.key_data());

  // Repersist the VK with the new creds.
  CryptohomeErrorCode ret_code = CRYPTOHOME_ERROR_NOT_SET;
  if (!keyset_to_add->Encrypt(new_credentials.passkey(), obfuscated_username) ||
      !keyset_to_add->Save(vk_path)) {
    LOG(WARNING) << "Failed to encrypt or write the new keyset";
    ret_code = CRYPTOHOME_ERROR_BACKING_STORE_FAILURE;
    // If we're clobbering don't delete on error.
    if (!clobber || !match.get()) {
      platform_->DeleteFile(vk_path);
    }
  }
  return ret_code;
}

CryptohomeErrorCode KeysetManagement::AddWrappedResetSeedIfMissing(
    VaultKeyset* vault_keyset, const Credentials& credentials) {
  bool has_reset_seed = vault_keyset->HasWrappedResetSeed();

  ReportUsageOfLegacyCodePath(
      LegacyCodePathLocation::kGenerateResetSeedDuringAddKey, has_reset_seed);

  if (has_reset_seed) {
    // No need to update the vault keyset.
    return CRYPTOHOME_ERROR_NOT_SET;
  }

  LOG(INFO) << "Keyset lacks reset_seed; generating one.";
  vault_keyset->CreateRandomResetSeed();
  if (!vault_keyset->Encrypt(credentials.passkey(),
                             credentials.GetObfuscatedUsername(system_salt_)) ||
      !vault_keyset->Save(vault_keyset->GetSourceFile())) {
    LOG(WARNING) << "Failed to re-encrypt the old keyset";
    return CRYPTOHOME_ERROR_BACKING_STORE_FAILURE;
  }

  return CRYPTOHOME_ERROR_NOT_SET;
}

CryptohomeErrorCode KeysetManagement::RemoveKeyset(
    const Credentials& credentials, const KeyData& key_data) {
  // This error condition should be caught by the caller.
  if (key_data.label().empty())
    return CRYPTOHOME_ERROR_KEY_NOT_FOUND;

  const std::string obfuscated =
      credentials.GetObfuscatedUsername(system_salt_);

  std::unique_ptr<VaultKeyset> remove_vk =
      GetVaultKeyset(obfuscated, key_data.label());
  if (!remove_vk.get()) {
    LOG(WARNING) << "RemoveKeyset: key to remove not found";
    return CRYPTOHOME_ERROR_KEY_NOT_FOUND;
  }

  std::unique_ptr<VaultKeyset> vk =
      GetValidKeyset(credentials, nullptr /* error */);
  if (!vk) {
    // Differentiate between failure and non-existent.
    if (!credentials.key_data().label().empty()) {
      vk = GetVaultKeyset(obfuscated, credentials.key_data().label());
      if (!vk.get()) {
        LOG(WARNING) << "RemoveKeyset: key not found";
        return CRYPTOHOME_ERROR_AUTHORIZATION_KEY_NOT_FOUND;
      }
    }
    LOG(WARNING) << "RemoveKeyset: invalid authentication provided";
    return CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED;
  }

  if (!ForceRemoveKeyset(obfuscated, remove_vk->GetLegacyIndex())) {
    LOG(ERROR) << "RemoveKeyset: failed to remove keyset file";
    return CRYPTOHOME_ERROR_BACKING_STORE_FAILURE;
  }
  return CRYPTOHOME_ERROR_NOT_SET;
}

bool KeysetManagement::ForceRemoveKeyset(const std::string& obfuscated,
                                         int index) {
  // Note, external callers should check credentials.
  if (index < 0 || index >= kKeyFileMax)
    return false;

  std::unique_ptr<VaultKeyset> vk = LoadVaultKeysetForUser(obfuscated, index);
  if (!vk) {
    LOG(WARNING) << "ForceRemoveKeyset: keyset " << index << " for "
                 << obfuscated << " does not exist";
    // Since it doesn't exist, then we're done.
    return true;
  }

  // Try removing the LE credential data, if applicable. But, don't abort if we
  // fail. The leaf data will remain, but at least the SerializedVaultKeyset
  // will be deleted.
  if (vk->IsLECredential()) {
    if (!crypto_->RemoveLECredential(vk->GetLELabel())) {
      LOG(ERROR)
          << "ForceRemoveKeyset: Failed to remove LE credential metadata.";
    }
  }

  base::FilePath path = VaultKeysetPath(obfuscated, index);
  if (platform_->DeleteFileSecurely(path))
    return true;

  // TODO(wad) Add file zeroing here or centralize with other code.
  return platform_->DeleteFile(path);
}

bool KeysetManagement::MoveKeyset(const std::string& obfuscated,
                                  int src,
                                  int dst) {
  if (src < 0 || dst < 0 || src >= kKeyFileMax || dst >= kKeyFileMax)
    return false;

  base::FilePath src_path = VaultKeysetPath(obfuscated, src);
  base::FilePath dst_path = VaultKeysetPath(obfuscated, dst);
  if (!platform_->FileExists(src_path))
    return false;
  if (platform_->FileExists(dst_path))
    return false;
  // Grab the destination exclusively
  FILE* vk_file = platform_->OpenFile(dst_path, "wx");
  if (!vk_file)
    return false;
  // The creation occurred so there's no reason to keep the handle.
  platform_->CloseFile(vk_file);
  if (!platform_->Rename(src_path, dst_path))
    return false;
  return true;
}

std::unique_ptr<VaultKeyset> KeysetManagement::LoadVaultKeysetForUser(
    const std::string& obfuscated_user, int index) const {
  std::unique_ptr<VaultKeyset> keyset(
      vault_keyset_factory_->New(platform_, crypto_));
  // Load the encrypted keyset
  base::FilePath user_key_file = VaultKeysetPath(obfuscated_user, index);
  // We don't have keys yet, so just load it.
  // TODO(wad) Move to passing around keysets and not serialized versions.
  if (!keyset->Load(user_key_file)) {
    LOG(ERROR) << "Failed to load keyset file for user " << obfuscated_user;
    return nullptr;
  }
  keyset->SetLegacyIndex(index);
  return keyset;
}

bool KeysetManagement::Migrate(const VaultKeyset& old_vk,
                               const Credentials& newcreds) {
  int key_index = old_vk.GetLegacyIndex();
  if (key_index == -1) {
    LOG(ERROR) << "Attempted migration of key-less mount.";
    return false;
  }
  std::string obfuscated_username =
      newcreds.GetObfuscatedUsername(system_salt_);
  // Overwrite the existing keyset.
  base::FilePath vk_path = old_vk.GetSourceFile();

  std::unique_ptr<VaultKeyset> migrated_vk(
      vault_keyset_factory_->New(platform_, crypto_));
  migrated_vk->InitializeToAdd(old_vk);
  if (old_vk.HasKeyData()) {
    migrated_vk->SetKeyData(old_vk.GetKeyData());
  }

  if (!migrated_vk->Encrypt(newcreds.passkey(), obfuscated_username) ||
      !migrated_vk->Save(vk_path)) {
    LOG(WARNING) << "Failed to encrypt or write the new keyset to migrate.";
    return false;
  }

  // Remove all other keysets during a "migration".
  std::vector<int> key_indices;
  if (!GetVaultKeysets(obfuscated_username, &key_indices)) {
    LOG(WARNING) << "Failed to enumerate keysets after adding one. Weird.";
    // Fallthrough: The user is migrated, but something else changed keys.
  }
  for (int index : key_indices) {
    if (index == key_index)
      continue;
    LOG(INFO) << "Removing keyset " << index << " due to migration.";
    ForceRemoveKeyset(obfuscated_username, index);  // Failure is ok.
  }

  return true;
}

void KeysetManagement::ResetLECredentials(const Credentials& creds) {
  std::string obfuscated = creds.GetObfuscatedUsername(system_salt_);
  std::vector<int> key_indices;
  if (!GetVaultKeysets(obfuscated, &key_indices)) {
    LOG(WARNING) << "No valid keysets on disk for " << obfuscated;
    return;
  }

  bool credentials_checked = false;
  std::unique_ptr<VaultKeyset> vk;
  for (int index : key_indices) {
    std::unique_ptr<VaultKeyset> vk_reset =
        LoadVaultKeysetForUser(obfuscated, index);
    if (!vk_reset || !vk_reset->IsLECredential() ||  // Skip non-LE Credentials.
        crypto_->GetWrongAuthAttempts(vk_reset->GetLELabel()) == 0) {
      continue;
    }

    if (!credentials_checked) {
      // Make sure the credential can actually be used for sign-in.
      // It is also the easiest way to get a valid keyset.
      vk = GetValidKeyset(creds, nullptr /* error */);
      if (!vk) {
        LOG(WARNING) << "The provided credentials are incorrect or invalid"
                        " for LE credential reset, reset skipped.";
        return;
      }
      credentials_checked = true;
    }

    CryptoError err;
    if (!crypto_->ResetLECredential(*vk_reset, *vk, &err)) {
      LOG(WARNING) << "Failed to reset an LE credential: " << err;
    } else {
      vk_reset->SetAuthLocked(false);
      if (!vk_reset->Save(vk_reset->GetSourceFile())) {
        LOG(WARNING) << "Failed to clear auth_locked in VaultKeyset on disk.";
      }
    }
  }
}

void KeysetManagement::RemoveLECredentials(
    const std::string& obfuscated_username) {
  std::vector<int> key_indices;
  if (!GetVaultKeysets(obfuscated_username, &key_indices)) {
    LOG(WARNING) << "No valid keysets on disk for " << obfuscated_username;
    return;
  }

  for (int index : key_indices) {
    std::unique_ptr<VaultKeyset> vk_remove =
        LoadVaultKeysetForUser(obfuscated_username, index);
    if (!vk_remove ||
        !vk_remove->IsLECredential()) {  // Skip non-LE Credentials.
      continue;
    }

    uint64_t label = vk_remove->GetLELabel();
    if (!crypto_->RemoveLECredential(label)) {
      LOG(WARNING) << "Failed to remove an LE credential, label: " << label;
      continue;
    }

    // Remove the cryptohome VaultKeyset data.
    base::FilePath vk_path = VaultKeysetPath(obfuscated_username, index);
    platform_->DeleteFile(vk_path);
  }
  return;
}

bool KeysetManagement::UserExists(const std::string& obfuscated_username) {
  base::FilePath user_dir = UserPath(obfuscated_username);
  return platform_->DirectoryExists(user_dir);
}

brillo::SecureBlob KeysetManagement::GetPublicMountPassKey(
    const std::string& account_id) {
  brillo::SecureBlob public_mount_salt;
  if (!crypto_->GetPublicMountSalt(&public_mount_salt)) {
    LOG(ERROR) << "Could not get or create public salt from file";
    // Ensure that it is empty so that caller can confirm there was an error.
    public_mount_salt.clear();
    return public_mount_salt;
  }
  brillo::SecureBlob passkey;
  Crypto::PasswordToPasskey(account_id.c_str(), public_mount_salt, &passkey);
  return passkey;
}

// TODO(b/205759690, dlunev): can be removed after a stepping stone release.
base::Time KeysetManagement::GetPerIndexTimestampFileData(
    const std::string& obfuscated, int index) {
  brillo::Blob tcontents;
  if (!platform_->ReadFile(UserActivityPerIndexTimestampPath(obfuscated, index),
                           &tcontents)) {
    return base::Time();
  }

  cryptohome::Timestamp timestamp;
  if (!timestamp.ParseFromArray(tcontents.data(), tcontents.size())) {
    return base::Time();
  }

  return base::Time::FromInternalValue(timestamp.timestamp());
}

// TODO(b/205759690, dlunev): can be removed after a stepping stone release.
base::Time KeysetManagement::GetKeysetBoundTimestamp(
    const std::string& obfuscated) {
  base::Time timestamp = base::Time();

  std::vector<int> key_indices;
  GetVaultKeysets(obfuscated, &key_indices);
  for (int index : key_indices) {
    std::unique_ptr<VaultKeyset> keyset =
        LoadVaultKeysetForUser(obfuscated, index);
    if (keyset.get() && keyset->HasLastActivityTimestamp()) {
      const base::Time new_timestamp =
          base::Time::FromInternalValue(keyset->GetLastActivityTimestamp());
      if (new_timestamp > timestamp) {
        timestamp = new_timestamp;
      }
    }
    const base::Time ts_from_index_file =
        GetPerIndexTimestampFileData(obfuscated, index);
    if (ts_from_index_file > timestamp) {
      timestamp = ts_from_index_file;
    }
  }

  return timestamp;
}

bool KeysetManagement::RecordVaultKeysetMetrics(
    const VaultKeyset& vk, VaultKeysetMetrics& keyset_metrics) const {
  if (!vk.HasKeyData()) {
    LOG(ERROR) << "VaultKeyset doesn't have a valid KeyData field.";
    return false;
  }
  if (vk.GetKeyData().label().empty()) {
    // VaultKeyset label is empty.
    if (vk.IsLECredential()) {
      keyset_metrics.empty_label_le_cred_count++;
    } else {
      keyset_metrics.empty_label_count++;
    }
  } else if (vk.IsLECredential()) {
    // VaultKeyset is PIN based, label is non-empty.
    keyset_metrics.le_cred_count++;
  } else {
    switch (vk.GetKeyData().type()) {
      case KeyData::KEY_TYPE_PASSWORD:
        if (vk.GetKeyData().has_provider_data()) {
          // VaultKeyset is based on SmartUnlock/EasyUnlock.
          keyset_metrics.smart_unlock_count++;
        } else {
          // VaultKeyset is password based.
          keyset_metrics.password_count++;
        }
        break;
      case KeyData::KEY_TYPE_CHALLENGE_RESPONSE:
        // VaultKeyset is smartcard/challenge-response based.
        keyset_metrics.smartcard_count++;
        break;
      case KeyData::KEY_TYPE_FINGERPRINT:
        // VaultKeyset is fingerprint-based.
        keyset_metrics.fingerprint_count++;
        break;
      case KeyData::KEY_TYPE_KIOSK:
        // VaultKeyset is kiosk-based.
        keyset_metrics.kiosk_count++;
        break;
      default:
        keyset_metrics.unclassified_count++;
        break;
    }
  }
  return true;
}

// TODO(b/205759690, dlunev): can be removed after a stepping stone release.
void KeysetManagement::CleanupPerIndexTimestampFiles(
    const std::string& obfuscated) {
  for (int i = 0; i < kKeyFileMax; ++i) {
    ignore_result(platform_->DeleteFileDurable(
        UserActivityPerIndexTimestampPath(obfuscated, i)));
  }
}

}  // namespace cryptohome
