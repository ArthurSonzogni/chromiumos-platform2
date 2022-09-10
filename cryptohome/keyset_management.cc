// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/keyset_management.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/callback.h>
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
#include "cryptohome/error/location_utils.h"
#include "cryptohome/filesystem_layout.h"
#include "cryptohome/flatbuffer_schemas/auth_block_state.h"
#include "cryptohome/key_objects.h"
#include "cryptohome/platform.h"
#include "cryptohome/storage/homedirs.h"
#include "cryptohome/timestamp.pb.h"
#include "cryptohome/vault_keyset.h"
#include "cryptohome/vault_keyset_factory.h"

using base::FilePath;
using cryptohome::error::CryptohomeError;
using cryptohome::error::CryptohomeMountError;
using cryptohome::error::ErrorAction;
using cryptohome::error::ErrorActionSet;
using hwsec_foundation::status::MakeStatus;
using hwsec_foundation::status::OkStatus;
using hwsec_foundation::status::StatusChain;

namespace cryptohome {

namespace {

// Prefix for the smartphone (easyunlock, smartunlock) VaultKeyset label.
constexpr char kEasyUnlockLabelPrefix[] = "easy-unlock-";

// Wraps VaultKeyset::DecryptEx to bind to DecryptVkCallback without object
// reference.
CryptoStatus DecryptExWrapper(const KeyBlobs& key_blobs, VaultKeyset* vk) {
  return vk->DecryptEx(key_blobs);
}

// Wraps VaultKeyset::Decrypt to bind to DecryptVkCallback without object
// reference.
CryptoStatus DecryptWrapper(const brillo::SecureBlob& key,
                            bool locked_to_single_user,
                            VaultKeyset* vk) {
  return vk->Decrypt(key, locked_to_single_user);
}

// Wraps VaultKeyset::ExncryptEx to bind to EncryptVkCallback without object
// reference.
CryptohomeStatus EncryptExWrapper(const KeyBlobs& key_blobs,
                                  std::unique_ptr<AuthBlockState> auth_state,
                                  VaultKeyset* vk) {
  return vk->EncryptEx(key_blobs, *auth_state);
}

// Wraps VaultKeyset::Exncryptto bind to EncryptVkCallback without object
// reference.
CryptohomeStatus EncryptWrapper(const brillo::SecureBlob& key,
                                const std::string& obfuscated_username,
                                VaultKeyset* vk) {
  return vk->Encrypt(key, obfuscated_username);
}

}  // namespace

KeysetManagement::KeysetManagement(
    Platform* platform,
    Crypto* crypto,
    std::unique_ptr<VaultKeysetFactory> vault_keyset_factory)
    : platform_(platform),
      crypto_(crypto),
      vault_keyset_factory_(std::move(vault_keyset_factory)) {}

bool KeysetManagement::AreCredentialsValid(const Credentials& creds) {
  MountStatusOr<std::unique_ptr<VaultKeyset>> vk = GetValidKeyset(creds);
  return vk.ok();
}

MountStatusOr<std::unique_ptr<VaultKeyset>>
KeysetManagement::GetValidKeysetWithKeyBlobs(
    const std::string& obfuscated_username,
    KeyBlobs key_blobs,
    const std::optional<std::string>& label) {
  return GetValidKeysetImpl(
      obfuscated_username, label,
      base::BindRepeating(&DecryptExWrapper,
                          base::Passed(std::move(key_blobs))));
}

MountStatusOr<std::unique_ptr<VaultKeyset>> KeysetManagement::GetValidKeyset(
    const Credentials& credentials) {
  std::string obfuscated_username = credentials.GetObfuscatedUsername();
  bool locked_to_single_user =
      platform_->FileExists(base::FilePath(kLockedToSingleUserFile));

  return GetValidKeysetImpl(
      obfuscated_username, credentials.key_data().label(),
      base::BindRepeating(&DecryptWrapper, credentials.passkey(),
                          locked_to_single_user));
}

MountStatusOr<std::unique_ptr<VaultKeyset>>
KeysetManagement::GetValidKeysetImpl(const std::string& obfuscated,
                                     const std::optional<std::string>& label,
                                     DecryptVkCallback decrypt_vk_callback) {
  std::vector<int> key_indices;
  if (!GetVaultKeysets(obfuscated, &key_indices)) {
    LOG(WARNING) << "No valid keysets on disk for " << obfuscated;
    return MakeStatus<CryptohomeMountError>(
        CRYPTOHOME_ERR_LOC(
            kLocKeysetManagementGetKeysetsFailedInGetValidKeyset),
        ErrorActionSet({ErrorAction::kReboot, ErrorAction::kDeleteVault}),
        MOUNT_ERROR_VAULT_UNRECOVERABLE);
  }

  bool any_keyset_exists = false;
  CryptoStatus last_crypto_error;
  for (int index : key_indices) {
    std::unique_ptr<VaultKeyset> vk = LoadVaultKeysetForUser(obfuscated, index);
    if (!vk) {
      continue;
    }
    any_keyset_exists = true;
    // Skip decrypt attempts if the label doesn't match.
    // Treat an empty creds label as a wildcard.
    if (label.has_value() && !label.value().empty() &&
        label != vk->GetLabel()) {
      continue;
    }
    // Skip LE Credentials if not explicitly identified by a label, since we
    // don't want unnecessary wrong attempts.
    if ((!label.has_value() || label.value().empty()) &&
        (vk->GetFlags() & SerializedVaultKeyset::LE_CREDENTIAL)) {
      continue;
    }

    last_crypto_error = decrypt_vk_callback.Run(vk.get());
    if (last_crypto_error.ok()) {
      return vk;
    }
  }

  if (!any_keyset_exists) {
    LOG(ERROR) << "No parsable keysets found for " << obfuscated;
    return MakeStatus<CryptohomeMountError>(
        CRYPTOHOME_ERR_LOC(kLocKeysetManagementNoKeysetsInGetValidKeyset),
        ErrorActionSet({ErrorAction::kReboot, ErrorAction::kDeleteVault}),
        MOUNT_ERROR_VAULT_UNRECOVERABLE);
  } else if (last_crypto_error.ok()) {
    // If we're searching by label, don't let a no-key-found become
    // MOUNT_ERROR_FATAL.  In the past, no parseable key was a fatal
    // error.  Just treat it like an invalid key.  This allows for
    // multiple per-label requests then a wildcard, worst case, before
    // the Cryptohome is removed.
    if (label.has_value() && !label.value().empty()) {
      LOG(ERROR) << "Failed to find the specified keyset for " << obfuscated;
      return MakeStatus<CryptohomeMountError>(
          CRYPTOHOME_ERR_LOC(
              kLocKeysetManagementKeysetNotDecryptedInGetValidKeyset),
          ErrorActionSet({ErrorAction::kAuth, ErrorAction::kReboot,
                          ErrorAction::kDeleteVault}),
          MOUNT_ERROR_KEY_FAILURE);

    } else {
      LOG(ERROR) << "Failed to find any suitable keyset for " << obfuscated;
      return MakeStatus<CryptohomeMountError>(
          CRYPTOHOME_ERR_LOC(
              kLocKeysetManagementNoKeysetsDecryptedInGetValidKeyset),
          ErrorActionSet({ErrorAction::kReboot, ErrorAction::kDeleteVault}),
          MOUNT_ERROR_FATAL);
    }
  } else {
    LOG(ERROR) << "Failed to decrypt any keysets for " << obfuscated << ": "
               << last_crypto_error;
    return MakeStatus<CryptohomeMountError>(
               CRYPTOHOME_ERR_LOC(
                   kLocKeysetManagementDecryptFailedInGetValidKeyset))
        .Wrap(std::move(last_crypto_error));
  }
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
    if (key_label_data->find(vk->GetLabel()) != key_label_data->end()) {
      // This is a confirmation check, we do not expect to hit this.
      LOG(INFO) << "Found a duplicate label, skipping it: " << vk->GetLabel();
      continue;
    }

    key_label_data->insert({vk->GetLabel(), vk->GetKeyDataOrDefault()});
  }

  return (key_label_data->size() > 0);
}

bool KeysetManagement::GetVaultKeysetLabels(
    const std::string& obfuscated_username,
    bool include_le_labels,
    std::vector<std::string>* labels) const {
  CHECK(labels);
  base::FilePath user_dir = UserPath(obfuscated_username);

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

    if (!include_le_labels &&
        (vk->GetFlags() & SerializedVaultKeyset::LE_CREDENTIAL)) {
      continue;
    }

    labels->push_back(vk->GetLabel());
  }

  return (labels->size() > 0);
}

CryptohomeStatusOr<std::unique_ptr<VaultKeyset>>
KeysetManagement::AddInitialKeysetWithKeyBlobs(
    const std::string& obfuscated_username,
    const KeyData& key_data,
    const std::optional<SerializedVaultKeyset_SignatureChallengeInfo>&
        challenge_credentials_keyset_info,
    const FileSystemKeyset& file_system_keyset,
    KeyBlobs key_blobs,
    std::unique_ptr<AuthBlockState> auth_state) {
  return AddInitialKeysetImpl(
      obfuscated_username, key_data, challenge_credentials_keyset_info,
      file_system_keyset,
      base::BindOnce(&EncryptExWrapper, std::move(key_blobs),
                     std::move(auth_state)));
}

CryptohomeStatusOr<std::unique_ptr<VaultKeyset>>
KeysetManagement::AddInitialKeyset(const Credentials& credentials,
                                   const FileSystemKeyset& file_system_keyset) {
  std::string obfuscated_username = credentials.GetObfuscatedUsername();
  std::optional<SerializedVaultKeyset_SignatureChallengeInfo>
      challenge_credentials_keyset_info;
  if (credentials.key_data().type() == KeyData::KEY_TYPE_CHALLENGE_RESPONSE) {
    challenge_credentials_keyset_info =
        credentials.challenge_credentials_keyset_info();
  }
  return AddInitialKeysetImpl(
      obfuscated_username, credentials.key_data(),
      challenge_credentials_keyset_info, file_system_keyset,
      base::BindOnce(&EncryptWrapper, credentials.passkey(),
                     obfuscated_username));
}

CryptohomeStatusOr<std::unique_ptr<VaultKeyset>>
KeysetManagement::AddInitialKeysetImpl(
    const std::string& obfuscated_username,
    const KeyData& key_data,
    const std::optional<SerializedVaultKeyset_SignatureChallengeInfo>&
        challenge_credentials_keyset_info,
    const FileSystemKeyset& file_system_keyset,
    EncryptVkCallback encrypt_vk_callback) {
  std::unique_ptr<VaultKeyset> vk(
      vault_keyset_factory_->New(platform_, crypto_));
  vk->Initialize(platform_, crypto_);
  vk->SetLegacyIndex(kInitialKeysetIndex);
  vk->SetKeyData(key_data);
  vk->CreateFromFileSystemKeyset(file_system_keyset);

  if (key_data.type() == KeyData::KEY_TYPE_CHALLENGE_RESPONSE) {
    vk->SetFlags(vk->GetFlags() |
                 SerializedVaultKeyset::SIGNATURE_CHALLENGE_PROTECTED);
    if (challenge_credentials_keyset_info.has_value()) {
      vk->SetSignatureChallengeInfo(challenge_credentials_keyset_info.value());
    }
  }

  CryptohomeStatus callback_result =
      std::move(encrypt_vk_callback).Run(vk.get());
  if (!callback_result.ok()) {
    return MakeStatus<CryptohomeError>(
               CRYPTOHOME_ERR_LOC(
                   kLocKeysetManagementEncryptFailedInAddInitial))
        .Wrap(std::move(callback_result).status());
  }

  if (!vk->Save(VaultKeysetPath(obfuscated_username, kInitialKeysetIndex))) {
    LOG(ERROR) << "Failed to encrypt and write keyset for the new user.";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocKeysetManagementSaveFailedInAddInitial),
        ErrorActionSet(
            {ErrorAction::kDevCheckUnexpectedState, ErrorAction::kReboot}),
        user_data_auth::CryptohomeErrorCode::
            CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }
  return vk;
}

bool KeysetManagement::ShouldReSaveKeyset(VaultKeyset* vault_keyset) const {
  // Ensure the cryptohome keys are initialized to handle the case where a user
  // logged in while cryptohome was taking TPM ownership.  In that case, their
  // vault keyset may be scrypt-wrapped. If the call to
  // is_cryptohome_key_loaded() below succeed, allowing re-wrapping (migration)
  // using the TPM.
  CryptohomeKeysManager* keys_manager = crypto_->cryptohome_keys_manager();
  if (keys_manager && !keys_manager->HasAnyCryptohomeKey()) {
    keys_manager->Init();
  }

  if (!vault_keyset->HasWrappedChapsKey()) {
    vault_keyset->CreateRandomChapsKey();
    LOG(INFO) << "Migrating keyset " << vault_keyset->GetLegacyIndex()
              << " as Cryptohome has taken TPM ownership";
    return true;
  }

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
  if (is_le_credential) {
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

CryptohomeStatus KeysetManagement::ReSaveKeyset(
    const Credentials& credentials, VaultKeyset* vault_keyset) const {
  std::string obfuscated_username = credentials.GetObfuscatedUsername();

  return ReSaveKeysetImpl(*vault_keyset,
                          base::BindOnce(&EncryptWrapper, credentials.passkey(),
                                         obfuscated_username));
}

CryptohomeStatus KeysetManagement::ReSaveKeysetWithKeyBlobs(
    VaultKeyset& vault_keyset,
    KeyBlobs key_blobs,
    std::unique_ptr<AuthBlockState> auth_state) const {
  return ReSaveKeysetImpl(
      vault_keyset, base::BindOnce(&EncryptExWrapper, std::move(key_blobs),
                                   std::move(auth_state)));
}

CryptohomeStatus KeysetManagement::ReSaveKeysetImpl(
    VaultKeyset& vault_keyset, EncryptVkCallback encrypt_vk_callback) const {
  // Save the initial keyset so we can roll-back any changes if we
  // failed to re-save.
  VaultKeyset old_keyset = vault_keyset;

  // We get the LE Label from keyset before we resave it. Once the keyset
  // is re-saved, a new label is generated making the old label obsolete.
  // It would be safe to delete that from PinWeaver tree after resave.
  std::optional<uint64_t> old_le_label;
  if (vault_keyset.HasLELabel()) {
    old_le_label = vault_keyset.GetLELabel();
  }

  CryptohomeStatus callback_result =
      std::move(encrypt_vk_callback).Run(&vault_keyset);
  if (!callback_result.ok()) {
    return MakeStatus<CryptohomeError>(
               CRYPTOHOME_ERR_LOC(
                   kLocKeysetManagementEncryptFailedInReSaveKeyset))
        .Wrap(std::move(callback_result).status());
  }
  if (!vault_keyset.Save(vault_keyset.GetSourceFile())) {
    LOG(ERROR) << "Failed to encrypt and write the vault_keyset.";
    vault_keyset = old_keyset;
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocKeysetManagementSaveFailedInReSaveKeyset),
        ErrorActionSet(
            {ErrorAction::kDevCheckUnexpectedState, ErrorAction::kReboot}),
        user_data_auth::CryptohomeErrorCode::
            CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }

  if ((vault_keyset.GetFlags() & SerializedVaultKeyset::LE_CREDENTIAL) != 0 &&
      old_le_label.has_value()) {
    CHECK_NE(old_le_label.value(), vault_keyset.GetLELabel());
    if (!crypto_->RemoveLECredential(old_le_label.value())) {
      // This is non-fatal error.
      LOG(ERROR) << "Failed to remove label = " << old_le_label.value();
    }
  }

  return OkStatus<CryptohomeError>();
}

CryptohomeStatus KeysetManagement::ReSaveKeysetIfNeeded(
    const Credentials& credentials, VaultKeyset* vault_keyset) const {
  if (ShouldReSaveKeyset(vault_keyset)) {
    return ReSaveKeyset(credentials, vault_keyset);
  }
  return OkStatus<CryptohomeError>();
}

CryptohomeErrorCode KeysetManagement::AddKeyset(
    const Credentials& new_credentials,
    const VaultKeyset& vault_keyset,
    bool clobber) {
  std::string obfuscated_username = new_credentials.GetObfuscatedUsername();
  return AddKeysetImpl(
      obfuscated_username, new_credentials.key_data(), vault_keyset,
      base::BindOnce(&EncryptWrapper, new_credentials.passkey(),
                     obfuscated_username),
      clobber);
}

CryptohomeErrorCode KeysetManagement::AddKeysetWithKeyBlobs(
    const std::string& obfuscated_username_new,
    const KeyData& key_data_new,
    const VaultKeyset& vault_keyset_old,
    KeyBlobs key_blobs_new,
    std::unique_ptr<AuthBlockState> auth_state_new,
    bool clobber) {
  return AddKeysetImpl(
      obfuscated_username_new, key_data_new, vault_keyset_old,
      base::BindOnce(&EncryptExWrapper, std::move(key_blobs_new),
                     std::move(auth_state_new)),
      clobber);
}

CryptohomeErrorCode KeysetManagement::AddKeysetImpl(
    const std::string& obfuscated_username_new,
    const KeyData& key_data_new,
    const VaultKeyset& vault_keyset_old,
    EncryptVkCallback encrypt_vk_callback,
    bool clobber) {
  // Before persisting, check if there is an existing labeled credential.
  std::unique_ptr<VaultKeyset> match =
      GetVaultKeyset(obfuscated_username_new, key_data_new.label());
  base::FilePath vk_path;
  if (match.get()) {
    LOG(INFO) << "Label already exists.";
    if (!clobber) {
      return CRYPTOHOME_ERROR_KEY_LABEL_EXISTS;
    }
    vk_path = match->GetSourceFile();
  }

  // If we need to create a new file, walk the namespace looking for the first
  // free spot. Note, nothing is stopping simultaneous access to these files
  // or enforcing mandatory locking.
  if (vk_path.empty()) {
    bool file_found = false;
    for (int new_index = 0; new_index < kKeyFileMax; ++new_index) {
      vk_path = VaultKeysetPath(obfuscated_username_new, new_index);
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
  }

  std::unique_ptr<VaultKeyset> keyset_to_add(
      vault_keyset_factory_->New(platform_, crypto_));
  keyset_to_add->InitializeToAdd(vault_keyset_old);
  keyset_to_add->SetKeyData(key_data_new);

  // Repersist the VK with the new creds.
  CryptohomeStatus status =
      std::move(encrypt_vk_callback).Run(keyset_to_add.get());
  if (!status.ok()) {
    LOG(WARNING) << "Failed to encrypt the new keyset";
    // If we're clobbering don't delete on error.
    if (!clobber || !match.get()) {
      platform_->DeleteFile(vk_path);
    }
    return CRYPTOHOME_ERROR_BACKING_STORE_FAILURE;
  }

  CryptohomeErrorCode ret_code = CRYPTOHOME_ERROR_NOT_SET;
  if (!keyset_to_add->Save(vk_path)) {
    LOG(WARNING) << "Failed to write the new keyset";
    ret_code = CRYPTOHOME_ERROR_BACKING_STORE_FAILURE;
    // If we're clobbering don't delete on error.
    if (!clobber || !match.get()) {
      platform_->DeleteFile(vk_path);
    }
  }
  return ret_code;
}

CryptohomeErrorCode KeysetManagement::UpdateKeyset(
    const Credentials& new_credentials, const VaultKeyset& vault_keyset) {
  std::string obfuscated_username = new_credentials.GetObfuscatedUsername();

  // Check if there is an existing labeled keyset.
  std::unique_ptr<VaultKeyset> match =
      GetVaultKeyset(obfuscated_username, new_credentials.key_data().label());
  if (!match.get()) {
    LOG(ERROR) << "Label does not exist.";
    return CRYPTOHOME_ERROR_AUTHORIZATION_KEY_NOT_FOUND;
  }

  // We set clobber to be true as we are sure that there is an existing keyset.
  return AddKeyset(new_credentials, vault_keyset,
                   true /* we are updating existing keyset */);
}

CryptohomeErrorCode KeysetManagement::UpdateKeysetWithKeyBlobs(
    const std::string& obfuscated_username_new,
    const KeyData& key_data_new,
    const VaultKeyset& vault_keyset,
    KeyBlobs key_blobs,
    std::unique_ptr<AuthBlockState> auth_state) {
  // Check if there is an existing labeled keyset.
  std::unique_ptr<VaultKeyset> match =
      GetVaultKeyset(obfuscated_username_new, key_data_new.label());
  if (!match.get()) {
    LOG(ERROR) << "Label does not exist.";
    return CRYPTOHOME_ERROR_AUTHORIZATION_KEY_NOT_FOUND;
  }

  // We set clobber to be true as we are sure that there is an existing keyset.
  return AddKeysetWithKeyBlobs(
      obfuscated_username_new, key_data_new, vault_keyset, std::move(key_blobs),
      std::move(auth_state), true /* we are updating existing keyset */);
}

CryptohomeErrorCode KeysetManagement::AddWrappedResetSeedIfMissing(
    VaultKeyset* vault_keyset, const Credentials& credentials) {
  if (!AddResetSeedIfMissing(*vault_keyset)) {
    return CRYPTOHOME_ERROR_NOT_SET;
  }

  if (!vault_keyset
           ->Encrypt(credentials.passkey(), credentials.GetObfuscatedUsername())
           .ok() ||
      !vault_keyset->Save(vault_keyset->GetSourceFile())) {
    LOG(WARNING) << "Failed to re-encrypt the old keyset";
    return CRYPTOHOME_ERROR_BACKING_STORE_FAILURE;
  }

  return CRYPTOHOME_ERROR_NOT_SET;
}

CryptohomeStatus KeysetManagement::RemoveKeyset(const Credentials& credentials,
                                                const KeyData& key_data) {
  // This error condition should be caught by the caller.
  if (key_data.label().empty()) {
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocKeysetManagementNoLabelInRemoveKeyset),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_KEY_NOT_FOUND);
  }

  const std::string obfuscated = credentials.GetObfuscatedUsername();

  std::unique_ptr<VaultKeyset> remove_vk =
      GetVaultKeyset(obfuscated, key_data.label());
  if (!remove_vk.get()) {
    LOG(WARNING) << "RemoveKeyset: key to remove not found";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocKeysetManagementVKNotFoundInRemoveKeyset),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_KEY_NOT_FOUND);
  }

  MountStatusOr<std::unique_ptr<VaultKeyset>> vk_status =
      GetValidKeyset(credentials);
  if (!vk_status.ok()) {
    // Differentiate between failure and non-existent.
    if (!credentials.key_data().label().empty()) {
      std::unique_ptr<VaultKeyset> vk =
          GetVaultKeyset(obfuscated, credentials.key_data().label());
      if (!vk.get()) {
        LOG(WARNING) << "RemoveKeyset: key not found";
        return MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocKeysetManagementKeyNotFoundInRemoveKeyset),
            ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
            user_data_auth::CryptohomeErrorCode::
                CRYPTOHOME_ERROR_AUTHORIZATION_KEY_NOT_FOUND);
      }
    }
    LOG(WARNING) << "RemoveKeyset: invalid authentication provided";
    return MakeStatus<CryptohomeError>(
               CRYPTOHOME_ERR_LOC(kLocKeysetManagementBadAuthInRemoveKeyset),
               ErrorActionSet({ErrorAction::kIncorrectAuth}),
               user_data_auth::CryptohomeErrorCode::
                   CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED)
        .Wrap(std::move(vk_status).status());
  }

  CryptohomeStatus status =
      ForceRemoveKeyset(obfuscated, remove_vk->GetLegacyIndex());
  if (!status.ok()) {
    LOG(ERROR) << "RemoveKeyset: failed to remove keyset file";
    return MakeStatus<CryptohomeError>(
               CRYPTOHOME_ERR_LOC(
                   kLocKeysetManagementRemoveFailedInRemoveKeyset),
               ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
               user_data_auth::CryptohomeErrorCode::
                   CRYPTOHOME_ERROR_BACKING_STORE_FAILURE)
        .Wrap(std::move(status));
  }
  return OkStatus<CryptohomeError>();
}

CryptohomeStatus KeysetManagement::ForceRemoveKeyset(
    const std::string& obfuscated, int index) {
  // Note, external callers should check credentials.
  if (index < 0 || index >= kKeyFileMax) {
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocKeysetManagementInvalidIndexInRemoveKeyset),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        user_data_auth::CryptohomeErrorCode::
            CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }

  std::unique_ptr<VaultKeyset> vk = LoadVaultKeysetForUser(obfuscated, index);
  if (!vk) {
    LOG(WARNING) << "ForceRemoveKeyset: keyset " << index << " for "
                 << obfuscated << " does not exist";
    // Since it doesn't exist, then we're done.
    return OkStatus<CryptohomeError>();
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
  if (platform_->DeleteFileSecurely(path)) {
    return OkStatus<CryptohomeError>();
  }

  // TODO(wad) Add file zeroing here or centralize with other code.
  bool success = platform_->DeleteFile(path);
  if (success) {
    return OkStatus<CryptohomeError>();
  }

  return MakeStatus<CryptohomeError>(
      CRYPTOHOME_ERR_LOC(kLocKeysetManagementDeleteFailedInRemoveKeyset),
      ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
      user_data_auth::CryptohomeErrorCode::
          CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
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
  std::string obfuscated_username = newcreds.GetObfuscatedUsername();
  // Overwrite the existing keyset.
  base::FilePath vk_path = old_vk.GetSourceFile();

  std::unique_ptr<VaultKeyset> migrated_vk(
      vault_keyset_factory_->New(platform_, crypto_));
  migrated_vk->InitializeToAdd(old_vk);
  if (old_vk.HasKeyData()) {
    migrated_vk->SetKeyData(old_vk.GetKeyData());
  }

  if (!migrated_vk->Encrypt(newcreds.passkey(), obfuscated_username).ok() ||
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
    CryptohomeStatus status =
        ForceRemoveKeyset(obfuscated_username, index);  // Failure is ok.
  }

  return true;
}

void KeysetManagement::ResetLECredentials(const Credentials& creds,
                                          const std::string& obfuscated) {
  std::vector<int> key_indices;
  if (!GetVaultKeysets(obfuscated, &key_indices)) {
    LOG(WARNING) << "No valid keysets on disk for " << obfuscated;
    return;
  }

  // Make sure the credential can actually be used for sign-in.
  // It is also the easiest way to get a valid keyset.
  MountStatusOr<std::unique_ptr<VaultKeyset>> vk_status = GetValidKeyset(creds);
  if (!vk_status.ok()) {
    LOG(WARNING) << "The provided credentials are incorrect or invalid"
                    " for LE credential reset, reset skipped.";
    return;
  }

  return ResetLECredentialsInternal(*vk_status.value(), obfuscated,
                                    key_indices);
}

void KeysetManagement::ResetLECredentialsWithValidatedVK(
    const VaultKeyset& validated_vk, const std::string& obfuscated) {
  std::vector<int> key_indices;
  if (!GetVaultKeysets(obfuscated, &key_indices)) {
    LOG(WARNING) << "No valid keysets on disk for " << obfuscated;
    return;
  }

  return ResetLECredentialsInternal(validated_vk, obfuscated, key_indices);
}

void KeysetManagement::ResetLECredentialsInternal(
    const VaultKeyset& validated_vk,
    const std::string& obfuscated,
    const std::vector<int>& key_indices) {
  for (int index : key_indices) {
    std::unique_ptr<VaultKeyset> vk_reset =
        LoadVaultKeysetForUser(obfuscated, index);
    if (!vk_reset || !vk_reset->IsLECredential() ||  // Skip non-LE Credentials.
        crypto_->GetWrongAuthAttempts(vk_reset->GetLELabel()) == 0) {
      continue;
    }

    CryptoError err;
    if (!crypto_->ResetLECredential(*vk_reset, validated_vk, &err)) {
      LOG(WARNING) << "Failed to reset an LE credential: " << err;
      continue;
    }

    vk_reset->SetAuthLocked(false);
    if (!vk_reset->Save(vk_reset->GetSourceFile())) {
      LOG(WARNING) << "Failed to clear auth_locked in VaultKeyset on disk.";
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
  if (!GetPublicMountSalt(platform_, &public_mount_salt)) {
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

void KeysetManagement::RecordAllVaultKeysetMetrics(
    const std::string& obfuscated) const {
  VaultKeysetMetrics keyset_metrics;
  std::vector<int> key_indices;
  GetVaultKeysets(obfuscated, &key_indices);
  for (int index : key_indices) {
    std::unique_ptr<VaultKeyset> vk = LoadVaultKeysetForUser(obfuscated, index);
    if (!vk) {
      continue;
    } else {
      RecordVaultKeysetMetrics(*vk.get(), keyset_metrics);
    }
  }
  ReportVaultKeysetMetrics(keyset_metrics);
}

void KeysetManagement::RecordVaultKeysetMetrics(
    const VaultKeyset& vk, VaultKeysetMetrics& keyset_metrics) const {
  if (!vk.HasKeyData()) {
    // Some legacy keysets were created without any key_data at all.
    keyset_metrics.missing_key_data_count++;
  } else if (vk.GetKeyData().label().empty()) {
    // Note that we access the label via |GetKeyData()| instead of |GetLabel()|,
    // because we want to report the number of keysets without an explicitly
    // assigned label here, meanwhile |GetLabel()| would backfill an empty label
    // with a "legacy-N" value.
    if (vk.IsLECredential()) {
      keyset_metrics.empty_label_le_cred_count++;
    } else {
      keyset_metrics.empty_label_count++;
    }
  } else if (vk.IsLECredential()) {
    // VaultKeyset is PIN based, label is non-empty.
    keyset_metrics.le_cred_count++;
  } else if (!vk.GetKeyData().has_type()) {
    // Check the case of a missing type separately, since otherwise the key
    // would be misclassified below, based on |type()|s default return value
    // |KEY_TYPE_PASSWORD|.
    keyset_metrics.untyped_count++;
    // TODO(b/204482221): Remove this log after collecting stats.
    LOG(INFO) << "Untyped vault keyset " << vk.GetLabel() << ".";
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
        // TODO(b/204482221): Remove this log after collecting stats.
        LOG(WARNING) << "Unexpected type " << vk.GetKeyData().type()
                     << " in vault keyset " << vk.GetLabel() << ".";
        keyset_metrics.unclassified_count++;
        break;
    }
  }
}

// TODO(b/205759690, dlunev): can be removed after a stepping stone release.
void KeysetManagement::CleanupPerIndexTimestampFiles(
    const std::string& obfuscated) {
  for (int i = 0; i < kKeyFileMax; ++i) {
    std::ignore = platform_->DeleteFileDurable(
        UserActivityPerIndexTimestampPath(obfuscated, i));
  }
}

bool KeysetManagement::AddResetSeedIfMissing(VaultKeyset& vault_keyset) {
  bool has_reset_seed = vault_keyset.HasWrappedResetSeed();

  if (has_reset_seed) {
    // No need to update the vault keyset.
    return false;
  }

  // PIN VK shouldn't have any reset seed other than when it is first created.
  // That initial reset seed is used to derive the reset secret and isn't saved.
  // Don't add any other reset seed otherwise it may result in fake reset
  // secrets.
  if (vault_keyset.IsLECredential()) {
    return false;
  }

  // Smartphones are not used for resetting a PIN counter, thus shouldn't have a
  // reset seed.
  std::string label = vault_keyset.GetLabel();
  if (label.rfind(kEasyUnlockLabelPrefix, 0) == 0) {
    return false;
  }

  ReportUsageOfLegacyCodePath(
      LegacyCodePathLocation::kGenerateResetSeedDuringAddKey, has_reset_seed);

  LOG(INFO) << "Keyset lacks reset_seed; generating one.";
  vault_keyset.CreateRandomResetSeed();

  return true;
}

CryptohomeErrorCode KeysetManagement::SaveKeysetWithKeyBlobs(
    VaultKeyset& vault_keyset,
    const KeyBlobs& key_blobs,
    const AuthBlockState& auth_state) {
  if (!vault_keyset.EncryptEx(key_blobs, auth_state).ok() ||
      !vault_keyset.Save(vault_keyset.GetSourceFile())) {
    LOG(WARNING) << "Failed to encrypt the keyset";
    return CRYPTOHOME_ERROR_BACKING_STORE_FAILURE;
  }

  return CRYPTOHOME_ERROR_NOT_SET;
}

}  // namespace cryptohome
