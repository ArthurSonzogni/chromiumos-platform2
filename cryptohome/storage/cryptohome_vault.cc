// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <tuple>
#include <utility>

#include <absl/container/flat_hash_map.h>
#include <base/location.h>
#include <base/logging.h>
#include <cryptohome/storage/cryptohome_vault.h>
#include <dbus/cryptohome/dbus-constants.h>
#include <libstorage/platform/platform.h>
#include <libstorage/storage_container/filesystem_key.h>
#include <libstorage/storage_container/storage_container.h>

#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/filesystem_layout.h"
#include "cryptohome/storage/error.h"
#include "cryptohome/storage/mount_constants.h"

namespace cryptohome {

CryptohomeVault::CryptohomeVault(
    const ObfuscatedUsername& obfuscated_username,
    std::unique_ptr<libstorage::StorageContainer> container,
    std::unique_ptr<libstorage::StorageContainer> migrating_container,
    std::unique_ptr<libstorage::StorageContainer> cache_container,
    absl::flat_hash_map<std::string,
                        std::unique_ptr<libstorage::StorageContainer>>
        application_containers,
    libstorage::Platform* platform)
    : obfuscated_username_(obfuscated_username),
      container_(std::move(container)),
      migrating_container_(std::move(migrating_container)),
      cache_container_(std::move(cache_container)),
      application_containers_(std::move(application_containers)),
      platform_(platform) {}

// Teardown the vault on object destruction.
CryptohomeVault::~CryptohomeVault() {
  std::ignore = Teardown();
}

StorageStatus CryptohomeVault::Setup(
    const libstorage::FileSystemKey& filesystem_key) {
  if (!platform_->ClearUserKeyring()) {
    LOG(ERROR) << "Failed to clear user keyring";
  }

  if (!platform_->SetupProcessKeyring()) {
    return StorageStatus::Make(FROM_HERE, "Failed to set up a process keyring.",
                               MOUNT_ERROR_SETUP_PROCESS_KEYRING_FAILED);
  }

  // Set up the data container. During migration, this container is also used
  // as the source for user data.
  if (!container_->Setup(filesystem_key)) {
    // TODO(sarthakkukreti): MOUNT_ERROR_KEYRING_FAILED should be replaced with
    // a more specific type.
    return StorageStatus::Make(FROM_HERE, "Failed to setup container.",
                               MOUNT_ERROR_KEYRING_FAILED);
  }

  // If migration is allowed, set up the migrating container, depending on
  // whether it has already been set up or not.
  if (migrating_container_ && !migrating_container_->Setup(filesystem_key)) {
    // TODO(sarthakkukreti): MOUNT_ERROR_KEYRING_FAILED should be replaced
    //  with a more specific type.
    return StorageStatus::Make(FROM_HERE,
                               "Failed to setup migrating container.",
                               MOUNT_ERROR_KEYRING_FAILED);
  }

  // If we are mounting a dm-crypt cryptohome, setup a separate cache container.
  if (cache_container_ && !cache_container_->Setup(filesystem_key)) {
    // TODO(sarthakkukreti): MOUNT_ERROR_KEYRING_FAILED should be replaced
    //  with a more specific type.
    return StorageStatus::Make(FROM_HERE, "Failed to setup cache container.",
                               MOUNT_ERROR_KEYRING_FAILED);
  }

  for (auto& [name, container] : application_containers_) {
    if (!container->Setup(filesystem_key)) {
      LOG(ERROR) << "Failed to setup an application container " << name;
      // TODO(sarthakkukreti): MOUNT_ERROR_KEYRING_FAILED should be replaced
      //  with a more specific type.
      return StorageStatus::Make(FROM_HERE,
                                 "Failed to setup an application container.",
                                 MOUNT_ERROR_KEYRING_FAILED);
    }
  }

  if (container_->GetType() == libstorage::StorageContainerType::kEphemeral) {
    // Do not create /home/.shadow/<hash>/mount for ephemeral.
    return StorageStatus::Ok();
  }

  base::FilePath mount_point = GetUserMountDirectory(obfuscated_username_);
  if (!platform_->CreateDirectory(mount_point)) {
    return StorageStatus::Make(
        FROM_HERE,
        "User mount directory creation failed for " + mount_point.value(),
        MOUNT_ERROR_DIR_CREATION_FAILED);
  }

  // During migration, the existing ecryptfs container is mounted at
  // |temporary_mount_point|.
  if (migrating_container_) {
    base::FilePath temporary_mount_point =
        GetUserTemporaryMountDirectory(obfuscated_username_);
    if (!platform_->CreateDirectory(temporary_mount_point)) {
      return StorageStatus::Make(
          FROM_HERE,
          "User temporary mount directory creation failed for " +
              temporary_mount_point.value(),
          MOUNT_ERROR_DIR_CREATION_FAILED);
    }
  }

  // For valid cache containers, create the cache mount directory.
  if (cache_container_) {
    base::FilePath cache_mount_point =
        GetDmcryptUserCacheDirectory(obfuscated_username_);
    if (!platform_->CreateDirectory(cache_mount_point)) {
      return StorageStatus::Make(FROM_HERE,
                                 "Cache mount directory creation failed for " +
                                     cache_mount_point.value(),
                                 MOUNT_ERROR_DIR_CREATION_FAILED);
    }
  }

  return StorageStatus::Ok();
}

void CryptohomeVault::ReportVaultEncryptionType() {
  libstorage::StorageContainerType type = migrating_container_
                                              ? migrating_container_->GetType()
                                              : container_->GetType();
  switch (type) {
    case libstorage::StorageContainerType::kDmcrypt:
      ReportHomedirEncryptionType(HomedirEncryptionType::kDmcrypt);
      break;
    case libstorage::StorageContainerType::kEcryptfs:
      ReportHomedirEncryptionType(HomedirEncryptionType::kEcryptfs);
      break;
    case libstorage::StorageContainerType::kFscrypt:
      ReportHomedirEncryptionType(HomedirEncryptionType::kDircrypto);
      break;
    case libstorage::StorageContainerType::kEphemeral:
      // Not an encrypted vault
      break;
    default:
      // We're only interested in encrypted home directories.
      LOG(ERROR) << "Unknown homedir encryption type: "
                 << static_cast<int>(type);
      break;
  }
}

MountType CryptohomeVault::GetMountType() {
  libstorage::StorageContainerType type = container_->GetType();
  switch (type) {
    case libstorage::StorageContainerType::kEcryptfs:
      if (migrating_container_ &&
          migrating_container_->GetType() ==
              libstorage::StorageContainerType::kFscrypt) {
        return MountType::ECRYPTFS_TO_DIR_CRYPTO;
      }
      if (migrating_container_ &&
          migrating_container_->GetType() ==
              libstorage::StorageContainerType::kDmcrypt) {
        return MountType::ECRYPTFS_TO_DMCRYPT;
      }
      return MountType::ECRYPTFS;
    case libstorage::StorageContainerType::kFscrypt:
      if (migrating_container_) {
        return MountType::DIR_CRYPTO_TO_DMCRYPT;
      }
      return MountType::DIR_CRYPTO;
    case libstorage::StorageContainerType::kDmcrypt:
      return MountType::DMCRYPT;
    case libstorage::StorageContainerType::kEphemeral:
      return MountType::EPHEMERAL;
    default:
      return MountType::NONE;
  }
}

bool CryptohomeVault::SetLazyTeardownWhenUnused() {
  bool ret = true;
  if (container_->IsLazyTeardownSupported() &&
      !container_->SetLazyTeardownWhenUnused()) {
    LOG(ERROR) << "Failed to set lazy teardown for container";
    ret = false;
  }

  if (migrating_container_ && migrating_container_->IsLazyTeardownSupported() &&
      !migrating_container_->SetLazyTeardownWhenUnused()) {
    LOG(ERROR) << "Failed to set lazy teardown for migrating container";
    ret = false;
  }

  if (cache_container_ && cache_container_->IsLazyTeardownSupported() &&
      !cache_container_->SetLazyTeardownWhenUnused()) {
    LOG(ERROR) << "Failed to set lazy teardown for cache container";
    ret = false;
  }

  // TODO(b:225769250, dlunev): figure out lazy teardown for non-mounted
  // application containers.

  return ret;
}

bool CryptohomeVault::Teardown() {
  bool ret = true;
  if (!container_->Teardown()) {
    LOG(ERROR) << "Failed to teardown container";
    ret = false;
  }

  if (migrating_container_ && !migrating_container_->Teardown()) {
    LOG(ERROR) << "Failed to teardown migrating container";
    ret = false;
  }

  if (cache_container_ && !cache_container_->Teardown()) {
    LOG(ERROR) << "Failed to teardown cache container";
    ret = false;
  }

  for (auto& [name, container] : application_containers_) {
    if (!container->Teardown()) {
      LOG(ERROR) << "Failed to teardown application container " << name;
      ret = false;
    }
  }

  return ret;
}

bool CryptohomeVault::ResetApplicationContainer(const std::string& app) {
  auto it = application_containers_.find(app);
  if (it == application_containers_.end()) {
    LOG(ERROR) << "Failed to find a valid application container for " << app;
    return false;
  }

  return it->second->Reset();
}

bool CryptohomeVault::Purge() {
  bool ret = true;
  if (container_->Exists() && !container_->Purge()) {
    LOG(ERROR) << "Failed to purge container";
    ret = false;
  }

  if (migrating_container_ && migrating_container_->Exists() &&
      !migrating_container_->Purge()) {
    LOG(ERROR) << "Failed to purge migrating container";
    ret = false;
  }

  if (cache_container_ && cache_container_->Exists() &&
      !cache_container_->Purge()) {
    LOG(ERROR) << "Failed to purge cache container";
    ret = false;
  }

  for (auto& [name, container] : application_containers_) {
    if (container->Exists() && !container->Purge()) {
      LOG(ERROR) << "Failed to purge application container " << name;
      ret = false;
    }
  }

  return ret;
}

bool CryptohomeVault::PurgeCacheContainer() {
  if (!cache_container_) {
    return false;
  }

  if (cache_container_->Exists() && !cache_container_->Purge()) {
    return false;
  }

  return true;
}

}  // namespace cryptohome
