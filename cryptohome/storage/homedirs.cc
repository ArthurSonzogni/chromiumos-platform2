// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/storage/homedirs.h"

#include <algorithm>
#include <memory>
#include <set>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/callback.h>
#include <base/callback_helpers.h>
#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/stringprintf.h>
#include <brillo/cryptohome.h>
// TODO(b/177929620): Cleanup once lvm utils are built unconditionally.
#if USE_LVM_STATEFUL_PARTITION
#include <brillo/blkdev_utils/lvm.h>
#endif  // USE_LVM_STATEFUL_PARTITION
#include <brillo/scoped_umask.h>
#include <brillo/secure_blob.h>
#include <chromeos/constants/cryptohome.h>

#include "cryptohome/credentials.h"
#include "cryptohome/crypto.h"
#include "cryptohome/crypto_error.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/dircrypto_util.h"
#include "cryptohome/filesystem_layout.h"
#include "cryptohome/key.pb.h"
#include "cryptohome/platform.h"
#include "cryptohome/signed_secret.pb.h"
#include "cryptohome/storage/cryptohome_vault.h"
#include "cryptohome/storage/cryptohome_vault_factory.h"
#include "cryptohome/storage/encrypted_container/encrypted_container.h"
#include "cryptohome/storage/mount_helper.h"

using base::FilePath;
using brillo::SecureBlob;
using brillo::cryptohome::home::SanitizeUserNameWithSalt;

namespace cryptohome {

const char* kEmptyOwner = "";
// Each xattr is set to Android app internal data directory, contains
// 8-byte inode number of cache subdirectory.  See
// frameworks/base/core/java/android/app/ContextImpl.java
const char kAndroidCacheInodeAttribute[] = "user.inode_cache";
const char kAndroidCodeCacheInodeAttribute[] = "user.inode_code_cache";
const char kTrackedDirectoryNameAttribute[] = "user.TrackedDirectoryName";
const char kRemovableFileAttribute[] = "user.GCacheRemovable";

HomeDirs::HomeDirs(Platform* platform,
                   const brillo::SecureBlob& system_salt,
                   std::unique_ptr<policy::PolicyProvider> policy_provider,
                   const RemoveCallback& remove_callback)
    : HomeDirs(platform,
               system_salt,
               std::move(policy_provider),
               remove_callback,
               std::make_unique<CryptohomeVaultFactory>(platform)) {}

HomeDirs::HomeDirs(Platform* platform,
                   const brillo::SecureBlob& system_salt,
                   std::unique_ptr<policy::PolicyProvider> policy_provider,
                   const RemoveCallback& remove_callback,
                   std::unique_ptr<CryptohomeVaultFactory> vault_factory)
    : platform_(platform),
      system_salt_(system_salt),
      policy_provider_(std::move(policy_provider)),
      enterprise_owned_(false),
      vault_factory_(std::move(vault_factory)),
      remove_callback_(remove_callback) {
// TODO(b/177929620): Cleanup once lvm utils are built unconditionally.
#if USE_LVM_STATEFUL_PARTITION
  lvm_ = std::make_unique<brillo::LogicalVolumeManager>();
#endif  // USE_LVM_STATEFUL_PARTITION
}

HomeDirs::~HomeDirs() {}

void HomeDirs::LoadDevicePolicy() {
  policy_provider_->Reload();
}

bool HomeDirs::AreEphemeralUsersEnabled() {
  LoadDevicePolicy();
  // If the policy cannot be loaded, default to non-ephemeral users.
  bool ephemeral_users_enabled = false;
  if (policy_provider_->device_policy_is_loaded())
    policy_provider_->GetDevicePolicy().GetEphemeralUsersEnabled(
        &ephemeral_users_enabled);
  return ephemeral_users_enabled;
}

bool HomeDirs::SetLockedToSingleUser() const {
  return platform_->TouchFileDurable(base::FilePath(kLockedToSingleUserFile));
}

bool HomeDirs::Exists(const std::string& obfuscated_username) const {
  FilePath user_dir = ShadowRoot().Append(obfuscated_username);
  return platform_->DirectoryExists(user_dir);
}

bool HomeDirs::CryptohomeExists(const std::string& obfuscated_username,
                                MountError* error) const {
  *error = MOUNT_ERROR_NONE;
  return EcryptfsCryptohomeExists(obfuscated_username) ||
         DircryptoCryptohomeExists(obfuscated_username, error) ||
         DmcryptCryptohomeExists(obfuscated_username);
}

bool HomeDirs::EcryptfsCryptohomeExists(
    const std::string& obfuscated_username) const {
  // Check for the presence of a vault directory for ecryptfs.
  return platform_->DirectoryExists(
      GetEcryptfsUserVaultPath(obfuscated_username));
}

bool HomeDirs::DircryptoCryptohomeExists(const std::string& obfuscated_username,
                                         MountError* error) const {
  // Check for the presence of an encrypted mount directory for dircrypto.
  FilePath mount_path = GetUserMountDirectory(obfuscated_username);

  if (!platform_->DirectoryExists(mount_path)) {
    return false;
  }

  switch (platform_->GetDirCryptoKeyState(mount_path)) {
    case dircrypto::KeyState::NO_KEY:
    case dircrypto::KeyState::NOT_SUPPORTED:
      return false;
    case dircrypto::KeyState::ENCRYPTED:
      return true;
    case dircrypto::KeyState::UNKNOWN:
      *error = MOUNT_ERROR_FATAL;
      PLOG(ERROR) << "Directory has inconsistent Fscrypt state:"
                  << mount_path.value();
      return false;
  }
  return false;
}

bool HomeDirs::DmcryptContainerExists(
    const std::string& obfuscated_username,
    const std::string& container_suffix) const {
// TODO(b/177929620): Cleanup once lvm utils are built unconditionally.
#if USE_LVM_STATEFUL_PARTITION
  // Check for the presence of the logical volume for the user's data container.
  std::string logical_volume_container =
      LogicalVolumePrefix(obfuscated_username).append(container_suffix);

  // Attempt to check if the stateful partition is setup with a valid physical
  // volume.
  base::FilePath physical_volume = platform_->GetStatefulDevice();
  if (physical_volume.empty())
    return false;

  auto pv = lvm_->GetPhysicalVolume(physical_volume);
  if (!pv || !pv->IsValid())
    return false;

  auto vg = lvm_->GetVolumeGroup(*pv);
  if (!vg || !vg->IsValid())
    return false;

  return lvm_->GetLogicalVolume(*vg, logical_volume_container) != base::nullopt;
#else
  return false;
#endif  // USE_LVM_STATEFUL_PARTITION
}

bool HomeDirs::DmcryptCryptohomeExists(
    const std::string& obfuscated_username) const {
  return DmcryptContainerExists(obfuscated_username,
                                kDmcryptDataContainerSuffix);
}

bool HomeDirs::DmcryptCacheContainerExists(
    const std::string& obfuscated_username) const {
  return DmcryptContainerExists(obfuscated_username,
                                kDmcryptCacheContainerSuffix);
}

void HomeDirs::RemoveNonOwnerCryptohomes() {
  // If the device is not enterprise owned it should have an owner user.
  std::string owner;
  if (!enterprise_owned_ && !GetOwner(&owner)) {
    return;
  }

  auto homedirs = GetHomeDirs();
  FilterMountedHomedirs(&homedirs);

  for (const auto& dir : homedirs) {
    if (GetOwner(&owner)) {
      if (dir.obfuscated == owner && !enterprise_owned_) {
        continue;  // Remove them all if enterprise owned.
      }
    }
    if (platform_->IsDirectoryMounted(
            brillo::cryptohome::home::GetUserPathPrefix().Append(
                dir.obfuscated)) ||
        platform_->IsDirectoryMounted(
            brillo::cryptohome::home::GetRootPathPrefix().Append(
                dir.obfuscated))) {
      continue;  // Don't use LE credentials if user cryptohome is mounted.
    } else if (!HomeDirs::Remove(dir.obfuscated)) {
      LOG(WARNING) << "Failed to remove all non-owner home directories.";
    }
  }
}

std::vector<HomeDirs::HomeDir> HomeDirs::GetHomeDirs() {
  std::vector<HomeDirs::HomeDir> ret;
  std::vector<FilePath> entries;
  if (!platform_->EnumerateDirectoryEntries(ShadowRoot(), false, &entries)) {
    return ret;
  }

  for (const auto& entry : entries) {
    HomeDirs::HomeDir dir;

    dir.obfuscated = entry.BaseName().value();

    if (!brillo::cryptohome::home::IsSanitizedUserName(dir.obfuscated))
      continue;

    if (!platform_->DirectoryExists(
            brillo::cryptohome::home::GetHashedUserPath(dir.obfuscated)))
      continue;

    ret.push_back(dir);
  }

  std::vector<FilePath> user_paths;
  std::transform(
      ret.begin(), ret.end(), std::back_inserter(user_paths),
      [](const HomeDirs::HomeDir& homedir) {
        return brillo::cryptohome::home::GetHashedUserPath(homedir.obfuscated);
      });

  auto is_mounted = platform_->AreDirectoriesMounted(user_paths);

  if (!is_mounted)
    return ret;  // assume all are unmounted

  int i = 0;
  for (const bool& m : is_mounted.value()) {
    ret[i++].is_mounted = m;
  }

  return ret;
}

void HomeDirs::FilterMountedHomedirs(std::vector<HomeDirs::HomeDir>* homedirs) {
  homedirs->erase(std::remove_if(homedirs->begin(), homedirs->end(),
                                 [](const HomeDirs::HomeDir& dir) {
                                   return dir.is_mounted;
                                 }),
                  homedirs->end());
}

bool HomeDirs::GetTrackedDirectory(const FilePath& user_dir,
                                   const FilePath& tracked_dir_name,
                                   FilePath* out) {
  FilePath vault_path = user_dir.Append(kEcryptfsVaultDir);
  if (platform_->DirectoryExists(vault_path)) {
    // On Ecryptfs, tracked directories' names are not encrypted.
    *out = user_dir.Append(kEcryptfsVaultDir).Append(tracked_dir_name);
    return true;
  }
  // This is dircrypto. Use the xattr to locate the directory.
  return GetTrackedDirectoryForDirCrypto(user_dir.Append(kMountDir),
                                         tracked_dir_name, out);
}

bool HomeDirs::GetTrackedDirectoryForDirCrypto(const FilePath& mount_dir,
                                               const FilePath& tracked_dir_name,
                                               FilePath* out) {
  FilePath current_name;
  FilePath current_path = mount_dir;

  // Iterate over name components. This way, we don't have to inspect every
  // directory under |mount_dir|.
  std::vector<std::string> name_components;
  tracked_dir_name.GetComponents(&name_components);
  for (const auto& name_component : name_components) {
    FilePath next_path;
    std::unique_ptr<FileEnumerator> enumerator(
        platform_->GetFileEnumerator(current_path, false /* recursive */,
                                     base::FileEnumerator::DIRECTORIES));
    for (FilePath dir = enumerator->Next(); !dir.empty();
         dir = enumerator->Next()) {
      if (platform_->HasExtendedFileAttribute(dir,
                                              kTrackedDirectoryNameAttribute)) {
        std::string name;
        if (!platform_->GetExtendedFileAttributeAsString(
                dir, kTrackedDirectoryNameAttribute, &name))
          return false;
        if (name == name_component) {
          // This is the directory we're looking for.
          next_path = dir;
          break;
        }
      }
    }
    if (next_path.empty()) {
      LOG(ERROR) << "Tracked dir not found " << tracked_dir_name.value();
      return false;
    }
    current_path = next_path;
  }
  *out = current_path;
  return true;
}

EncryptedContainerType HomeDirs::ChooseVaultType() {
// TODO(b/177929620): Cleanup once lvm utils are built unconditionally.
#if USE_LVM_STATEFUL_PARTITION
  // Validate stateful partition thinpool.
  base::Optional<brillo::PhysicalVolume> pv =
      lvm_->GetPhysicalVolume(platform_->GetStatefulDevice());
  if (pv && pv->IsValid()) {
    base::Optional<brillo::VolumeGroup> vg = lvm_->GetVolumeGroup(*pv);
    if (vg && vg->IsValid()) {
      base::Optional<brillo::Thinpool> thinpool =
          lvm_->GetThinpool(*vg, "thinpool");
      if (thinpool && thinpool->IsValid())
        return EncryptedContainerType::kDmcrypt;
    }
  }
#endif  // USE_LVM_STATEFUL_PARTITION

  dircrypto::KeyState state = platform_->GetDirCryptoKeyState(ShadowRoot());
  switch (state) {
    case dircrypto::KeyState::NOT_SUPPORTED:
      return EncryptedContainerType::kEcryptfs;
    case dircrypto::KeyState::NO_KEY:
      return EncryptedContainerType::kFscrypt;
    case dircrypto::KeyState::UNKNOWN:
    case dircrypto::KeyState::ENCRYPTED:
      LOG(ERROR) << "Unexpected state " << static_cast<int>(state);
      return EncryptedContainerType::kUnknown;
  }
}

std::unique_ptr<CryptohomeVault> HomeDirs::CreatePristineVault(
    const std::string& obfuscated_username,
    const FileSystemKeyReference& key_reference,
    CryptohomeVault::Options options,
    MountError* mount_error) {
  EncryptedContainerType container_type =
      options.force_type == EncryptedContainerType::kUnknown
          ? ChooseVaultType()
          : options.force_type;

  if (container_type == EncryptedContainerType::kUnknown) {
    LOG(ERROR) << "Pristine mount attempted with unknown vault type";
    *mount_error = MOUNT_ERROR_UNEXPECTED_MOUNT_TYPE;
    return nullptr;
  }

  return vault_factory_->Generate(obfuscated_username, key_reference,
                                  container_type,
                                  EncryptedContainerType::kUnknown);
}

std::unique_ptr<CryptohomeVault> HomeDirs::CreateNonMigratingVault(
    const std::string& obfuscated_username,
    const FileSystemKeyReference& key_reference,
    CryptohomeVault::Options options,
    MountError* mount_error) {
  EncryptedContainerType container_type = EncryptedContainerType::kUnknown;
  if (EcryptfsCryptohomeExists(obfuscated_username)) {
    if (DircryptoCryptohomeExists(obfuscated_username, mount_error)) {
      if (*mount_error != MOUNT_ERROR_NONE) {
        // Preserve dircrypto encryption check error
        return nullptr;
      }
      // If both types of home directory existed, it implies that the
      // migration attempt was aborted in the middle before doing clean up.
      LOG(ERROR) << "Mount failed because both eCryptfs and dircrypto home"
                 << " directories were found. Need to resume and finish"
                 << " migration first.";
      *mount_error = MOUNT_ERROR_PREVIOUS_MIGRATION_INCOMPLETE;
      return nullptr;
    }

    if (options.block_ecryptfs) {
      LOG(ERROR) << "Mount attempt with block_ecryptfs on eCryptfs.";
      *mount_error = MOUNT_ERROR_OLD_ENCRYPTION;
      return nullptr;
    }

    container_type = EncryptedContainerType::kEcryptfs;
  } else if (DircryptoCryptohomeExists(obfuscated_username, mount_error)) {
    if (*mount_error != MOUNT_ERROR_NONE) {
      // Preserve dircrypto encryption check error
      return nullptr;
    }
    container_type = EncryptedContainerType::kFscrypt;
  } else if (DmcryptCryptohomeExists(obfuscated_username)) {
    container_type = EncryptedContainerType::kDmcrypt;
  } else {
    LOG(ERROR) << "Non-migrating mount attempted with unknown vault type";
    *mount_error = MOUNT_ERROR_UNEXPECTED_MOUNT_TYPE;
    return nullptr;
  }

  return vault_factory_->Generate(obfuscated_username, key_reference,
                                  container_type,
                                  EncryptedContainerType::kUnknown);
}

std::unique_ptr<CryptohomeVault> HomeDirs::CreateMigratingVault(
    const std::string& obfuscated_username,
    const FileSystemKeyReference& key_reference,
    CryptohomeVault::Options options,
    MountError* mount_error) {
  EncryptedContainerType container_type = EncryptedContainerType::kUnknown;
  EncryptedContainerType migrating_container_type =
      EncryptedContainerType::kUnknown;
  if (EcryptfsCryptohomeExists(obfuscated_username)) {
    container_type = EncryptedContainerType::kEcryptfs;
    migrating_container_type = EncryptedContainerType::kFscrypt;
  } else {
    LOG(ERROR) << "Mount attempt with migration on non-eCryptfs mount";
    *mount_error = MOUNT_ERROR_UNEXPECTED_MOUNT_TYPE;
    return nullptr;
  }

  return vault_factory_->Generate(obfuscated_username, key_reference,
                                  container_type, migrating_container_type);
}

std::unique_ptr<CryptohomeVault> HomeDirs::GenerateCryptohomeVault(
    const std::string& obfuscated_username,
    const FileSystemKeyReference& key_reference,
    CryptohomeVault::Options options,
    bool is_pristine,
    MountError* mount_error) {
  if (is_pristine) {
    return CreatePristineVault(obfuscated_username, key_reference, options,
                               mount_error);
  }

  if (options.migrate) {
    return CreateMigratingVault(obfuscated_username, key_reference, options,
                                mount_error);
  }

  return CreateNonMigratingVault(obfuscated_username, key_reference, options,
                                 mount_error);
}

bool HomeDirs::GetPlainOwner(std::string* owner) {
  LoadDevicePolicy();
  if (!policy_provider_->device_policy_is_loaded())
    return false;
  policy_provider_->GetDevicePolicy().GetOwner(owner);
  return true;
}

bool HomeDirs::GetOwner(std::string* owner) {
  std::string plain_owner;
  if (!GetPlainOwner(&plain_owner) || plain_owner.empty())
    return false;

  *owner = SanitizeUserNameWithSalt(plain_owner, system_salt_);
  return true;
}

bool HomeDirs::IsOrWillBeOwner(const std::string& account_id) {
  std::string owner;
  GetPlainOwner(&owner);
  return !enterprise_owned_ && (owner.empty() || account_id == owner);
}

bool HomeDirs::GetSystemSalt(SecureBlob* blob) {
  *blob = system_salt_;
  return true;
}

bool HomeDirs::Create(const std::string& username) {
  brillo::ScopedUmask scoped_umask(kDefaultUmask);
  std::string obfuscated_username =
      SanitizeUserNameWithSalt(username, system_salt_);

  // Create the user's entry in the shadow root
  FilePath user_dir = ShadowRoot().Append(obfuscated_username);
  if (!platform_->CreateDirectory(user_dir)) {
    return false;
  }

  return true;
}

bool HomeDirs::Remove(const std::string& obfuscated) {
  remove_callback_.Run(obfuscated);
  FilePath user_dir = ShadowRoot().Append(obfuscated);
  FilePath user_path =
      brillo::cryptohome::home::GetUserPathPrefix().Append(obfuscated);
  FilePath root_path =
      brillo::cryptohome::home::GetRootPathPrefix().Append(obfuscated);

  bool ret = true;

  if (DmcryptCryptohomeExists(obfuscated)) {
    auto vault = vault_factory_->Generate(obfuscated, FileSystemKeyReference(),
                                          EncryptedContainerType::kDmcrypt,
                                          EncryptedContainerType::kUnknown);
    ret = vault->Purge();
  }

  return ret && platform_->DeletePathRecursively(user_dir) &&
         platform_->DeletePathRecursively(user_path) &&
         platform_->DeletePathRecursively(root_path);
}

bool HomeDirs::Rename(const std::string& account_id_from,
                      const std::string& account_id_to) {
  if (account_id_from == account_id_to) {
    return true;
  }

  const std::string obfuscated_from =
      SanitizeUserNameWithSalt(account_id_from, system_salt_);
  const std::string obfuscated_to =
      SanitizeUserNameWithSalt(account_id_to, system_salt_);

  const FilePath user_dir_from = ShadowRoot().Append(obfuscated_from);
  const FilePath user_path_from =
      brillo::cryptohome::home::GetUserPath(account_id_from);
  const FilePath root_path_from =
      brillo::cryptohome::home::GetRootPath(account_id_from);
  const FilePath new_user_path_from =
      FilePath(MountHelper::GetNewUserPath(account_id_from));

  const FilePath user_dir_to = ShadowRoot().Append(obfuscated_to);
  const FilePath user_path_to =
      brillo::cryptohome::home::GetUserPath(account_id_to);
  const FilePath root_path_to =
      brillo::cryptohome::home::GetRootPath(account_id_to);
  const FilePath new_user_path_to =
      FilePath(MountHelper::GetNewUserPath(account_id_to));

  LOG(INFO) << "HomeDirs::Rename(from='" << account_id_from << "', to='"
            << account_id_to << "'):"
            << " renaming '" << user_dir_from.value() << "' "
            << "(exists=" << platform_->DirectoryExists(user_dir_from) << ") "
            << "=> '" << user_dir_to.value() << "' "
            << "(exists=" << platform_->DirectoryExists(user_dir_to) << "); "
            << "renaming '" << user_path_from.value() << "' "
            << "(exists=" << platform_->DirectoryExists(user_path_from) << ") "
            << "=> '" << user_path_to.value() << "' "
            << "(exists=" << platform_->DirectoryExists(user_path_to) << "); "
            << "renaming '" << root_path_from.value() << "' "
            << "(exists=" << platform_->DirectoryExists(root_path_from) << ") "
            << "=> '" << root_path_to.value() << "' "
            << "(exists=" << platform_->DirectoryExists(root_path_to) << "); "
            << "renaming '" << new_user_path_from.value() << "' "
            << "(exists=" << platform_->DirectoryExists(new_user_path_from)
            << ") "
            << "=> '" << new_user_path_to.value() << "' "
            << "(exists=" << platform_->DirectoryExists(new_user_path_to)
            << ")";

  const bool already_renamed = !platform_->DirectoryExists(user_dir_from);

  if (already_renamed) {
    LOG(INFO) << "HomeDirs::Rename(from='" << account_id_from << "', to='"
              << account_id_to << "'): Consider already renamed. "
              << "('" << user_dir_from.value() << "' doesn't exist.)";
    return true;
  }

  const bool can_rename = !platform_->DirectoryExists(user_dir_to);

  if (!can_rename) {
    LOG(ERROR) << "HomeDirs::Rename(from='" << account_id_from << "', to='"
               << account_id_to << "'): Destination already exists! "
               << " '" << user_dir_from.value() << "' "
               << "(exists=" << platform_->DirectoryExists(user_dir_from)
               << ") "
               << "=> '" << user_dir_to.value() << "' "
               << "(exists=" << platform_->DirectoryExists(user_dir_to)
               << "); ";
    return false;
  }

  // |user_dir_renamed| is return value, because three other directories are
  // empty and will be created as needed.
  const bool user_dir_renamed = !platform_->DirectoryExists(user_dir_from) ||
                                platform_->Rename(user_dir_from, user_dir_to);

  if (user_dir_renamed) {
    const bool user_path_deleted =
        platform_->DeletePathRecursively(user_path_from);
    const bool root_path_deleted =
        platform_->DeletePathRecursively(root_path_from);
    const bool new_user_path_deleted =
        platform_->DeletePathRecursively(new_user_path_from);
    if (!user_path_deleted) {
      LOG(WARNING) << "HomeDirs::Rename(from='" << account_id_from << "', to='"
                   << account_id_to << "'): failed to delete user_path.";
    }
    if (!root_path_deleted) {
      LOG(WARNING) << "HomeDirs::Rename(from='" << account_id_from << "', to='"
                   << account_id_to << "'): failed to delete root_path.";
    }
    if (!new_user_path_deleted) {
      LOG(WARNING) << "HomeDirs::Rename(from='" << account_id_from << "', to='"
                   << account_id_to << "'): failed to delete new_user_path.";
    }
  } else {
    LOG(ERROR) << "HomeDirs::Rename(from='" << account_id_from << "', to='"
               << account_id_to << "'): failed to rename user_dir.";
  }

  return user_dir_renamed;
}

int64_t HomeDirs::ComputeDiskUsage(const std::string& account_id) {
  // SanitizeUserNameWithSalt below doesn't accept empty username.
  if (account_id.empty()) {
    // Empty account is always non-existent, return 0 as specified.
    return 0;
  }

  // Note that for ephemeral mounts, there could be a vault that's not
  // ephemeral, but the current mount is ephemeral. In this case,
  // ComputeDiskUsage() return the non ephemeral on disk vault's size.
  std::string obfuscated = SanitizeUserNameWithSalt(account_id, system_salt_);
  FilePath user_dir = ShadowRoot().Append(obfuscated);

  int64_t size = 0;
  if (!platform_->DirectoryExists(user_dir)) {
    // It's either ephemeral or the user doesn't exist. In either case, we check
    // /home/user/$hash.
    FilePath user_home_dir = brillo::cryptohome::home::GetUserPath(account_id);
    size = platform_->ComputeDirectoryDiskUsage(user_home_dir);
  } else {
    // Note that we'll need to handle both ecryptfs and dircrypto.
    // dircrypto:
    // /home/.shadow/$hash/mount: Always equal to the size occupied.
    // ecryptfs:
    // /home/.shadow/$hash/vault: Always equal to the size occupied.
    // /home/.shadow/$hash/mount: Equal to the size occupied only when mounted.
    // Therefore, we check to see if vault exists, if it exists, we compute
    // vault's size, otherwise, we check mount's size.
    FilePath mount_dir = user_dir.Append(kMountDir);
    FilePath vault_dir = user_dir.Append(kEcryptfsVaultDir);
    if (platform_->DirectoryExists(vault_dir)) {
      // ecryptfs
      size = platform_->ComputeDirectoryDiskUsage(vault_dir);
    } else {
      // dircrypto
      size = platform_->ComputeDirectoryDiskUsage(mount_dir);
    }
  }
  if (size > 0) {
    return size;
  }
  return 0;
}

namespace {
const char* kChapsDaemonName = "chaps";
}  // namespace

FilePath HomeDirs::GetChapsTokenDir(const std::string& user) const {
  return brillo::cryptohome::home::GetDaemonStorePath(user, kChapsDaemonName);
}

bool HomeDirs::NeedsDircryptoMigration(
    const std::string& obfuscated_username) const {
  // Bail if dircrypto is not supported.
  const dircrypto::KeyState state =
      platform_->GetDirCryptoKeyState(ShadowRoot());
  if (state == dircrypto::KeyState::UNKNOWN ||
      state == dircrypto::KeyState::NOT_SUPPORTED) {
    return false;
  }

  // Use the existence of eCryptfs vault as a single of whether the user needs
  // dircrypto migration. eCryptfs test is adapted from
  // Mount::DoesEcryptfsCryptohomeExist.
  const FilePath user_ecryptfs_vault_dir =
      ShadowRoot().Append(obfuscated_username).Append(kEcryptfsVaultDir);
  return platform_->DirectoryExists(user_ecryptfs_vault_dir);
}

int32_t HomeDirs::GetUnmountedAndroidDataCount() {
  const auto homedirs = GetHomeDirs();

  return std::count_if(
      homedirs.begin(), homedirs.end(), [&](const HomeDirs::HomeDir& dir) {
        if (dir.is_mounted)
          return false;

        if (EcryptfsCryptohomeExists(dir.obfuscated))
          return false;

        FilePath shadow_dir = ShadowRoot().Append(dir.obfuscated);
        FilePath root_home_dir;
        return GetTrackedDirectory(shadow_dir, FilePath(kRootHomeSuffix),
                                   &root_home_dir) &&
               MayContainAndroidData(root_home_dir);
      });
}

bool HomeDirs::MayContainAndroidData(
    const base::FilePath& root_home_dir) const {
  // The root home directory is considered to contain Android data if its
  // grandchild (supposedly android-data/data) is owned by android's system UID.
  std::unique_ptr<FileEnumerator> dir_enum(platform_->GetFileEnumerator(
      root_home_dir, false, base::FileEnumerator::DIRECTORIES));
  for (base::FilePath subdirectory = dir_enum->Next(); !subdirectory.empty();
       subdirectory = dir_enum->Next()) {
    if (LooksLikeAndroidData(subdirectory)) {
      return true;
    }
  }
  return false;
}

bool HomeDirs::LooksLikeAndroidData(const base::FilePath& directory) const {
  std::unique_ptr<FileEnumerator> dir_enum(platform_->GetFileEnumerator(
      directory, false, base::FileEnumerator::DIRECTORIES));

  for (base::FilePath subdirectory = dir_enum->Next(); !subdirectory.empty();
       subdirectory = dir_enum->Next()) {
    if (IsOwnedByAndroidSystem(subdirectory)) {
      return true;
    }
  }
  return false;
}

bool HomeDirs::IsOwnedByAndroidSystem(const base::FilePath& directory) const {
  uid_t uid = 0;
  gid_t gid = 0;
  if (!platform_->GetOwnership(directory, &uid, &gid, false)) {
    return false;
  }
  return uid == kAndroidSystemUid + kArcContainerShiftUid;
}

}  // namespace cryptohome
