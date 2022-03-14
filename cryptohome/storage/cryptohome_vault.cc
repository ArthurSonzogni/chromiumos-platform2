// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cryptohome/storage/cryptohome_vault.h>

#include <memory>
#include <string>
#include <utility>

#include <base/logging.h>
#include <base/notreached.h>
#include <dbus/cryptohome/dbus-constants.h>

#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/filesystem_layout.h"
#include "cryptohome/platform.h"
#include "cryptohome/storage/encrypted_container/encrypted_container.h"
#include "cryptohome/storage/encrypted_container/filesystem_key.h"
#include "cryptohome/storage/mount_constants.h"

namespace cryptohome {

CryptohomeVault::CryptohomeVault(
    const std::string& obfuscated_username,
    std::unique_ptr<EncryptedContainer> container,
    std::unique_ptr<EncryptedContainer> migrating_container,
    std::unique_ptr<EncryptedContainer> cache_container,
    Platform* platform)
    : obfuscated_username_(obfuscated_username),
      container_(std::move(container)),
      migrating_container_(std::move(migrating_container)),
      cache_container_(std::move(cache_container)),
      platform_(platform) {}

// Teardown the vault on object destruction.
CryptohomeVault::~CryptohomeVault() {
  ignore_result(Teardown());
}

MountError CryptohomeVault::Setup(const FileSystemKey& filesystem_key) {
  if (!platform_->ClearUserKeyring()) {
    LOG(ERROR) << "Failed to clear user keyring";
  }

  if (!platform_->SetupProcessKeyring()) {
    LOG(ERROR) << "Failed to set up a process keyring.";
    return MOUNT_ERROR_SETUP_PROCESS_KEYRING_FAILED;
  }

  // If there is a migrating data container, we need to set up the existing
  // data container.
  if (!container_->Setup(filesystem_key)) {
    LOG(ERROR) << "Failed to setup container.";
    // TODO(sarthakkukreti): MOUNT_ERROR_KEYRING_FAILED should be replaced with
    // a more specific type.
    return MOUNT_ERROR_KEYRING_FAILED;
  }

  // If migration is allowed, set up the migrating container, depending on
  // whether it has already been set up or not.
  if (migrating_container_ && !migrating_container_->Setup(filesystem_key)) {
    LOG(ERROR) << "Failed to setup migrating container.";
    // TODO(sarthakkukreti): MOUNT_ERROR_KEYRING_FAILED should be replaced
    //  with a more specific type.
    return MOUNT_ERROR_KEYRING_FAILED;
  }

  // If we are mounting a dm-crypt cryptohome, setup a separate cache container.
  if (cache_container_ && !cache_container_->Setup(filesystem_key)) {
    LOG(ERROR) << "Failed to setup cache container.";
    // TODO(sarthakkukreti): MOUNT_ERROR_KEYRING_FAILED should be replaced
    //  with a more specific type.
    return MOUNT_ERROR_KEYRING_FAILED;
  }

  if (container_->GetType() == EncryptedContainerType::kEphemeral) {
    // Do not create /home/.shadow/<hash>/mount for ephemeral.
    return MOUNT_ERROR_NONE;
  }

  base::FilePath mount_point = GetUserMountDirectory(obfuscated_username_);
  if (!platform_->CreateDirectory(mount_point)) {
    PLOG(ERROR) << "User mount directory creation failed for "
                << mount_point.value();
    return MOUNT_ERROR_DIR_CREATION_FAILED;
  }

  // During migration, the existing ecryptfs container is mounted at
  // |temporary_mount_point|.
  if (migrating_container_) {
    base::FilePath temporary_mount_point =
        GetUserTemporaryMountDirectory(obfuscated_username_);
    if (!platform_->CreateDirectory(temporary_mount_point)) {
      PLOG(ERROR) << "User temporary mount directory creation failed for "
                  << temporary_mount_point.value();
      return MOUNT_ERROR_DIR_CREATION_FAILED;
    }
  }

  // For valid cache containers, create the cache mount directory.
  if (cache_container_) {
    base::FilePath cache_mount_point =
        GetDmcryptUserCacheDirectory(obfuscated_username_);
    if (!platform_->CreateDirectory(cache_mount_point)) {
      PLOG(ERROR) << "Cache mount directory creation failed for "
                  << cache_mount_point.value();
      return MOUNT_ERROR_DIR_CREATION_FAILED;
    }
  }

  return MOUNT_ERROR_NONE;
}

void CryptohomeVault::ReportVaultEncryptionType() {
  EncryptedContainerType type = migrating_container_
                                    ? migrating_container_->GetType()
                                    : container_->GetType();
  switch (type) {
    case EncryptedContainerType::kDmcrypt:
      ReportHomedirEncryptionType(HomedirEncryptionType::kDmcrypt);
      break;
    case EncryptedContainerType::kEcryptfs:
      ReportHomedirEncryptionType(HomedirEncryptionType::kEcryptfs);
      break;
    case EncryptedContainerType::kFscrypt:
      ReportHomedirEncryptionType(HomedirEncryptionType::kDircrypto);
      break;
    case EncryptedContainerType::kEphemeral:
      // Not an encrypted vault
      break;
    default:
      // We're only interested in encrypted home directories.
      NOTREACHED() << "Unknown homedir encryption type: "
                   << static_cast<int>(type);
      break;
  }
}

MountType CryptohomeVault::GetMountType() {
  EncryptedContainerType type = container_->GetType();
  switch (type) {
    case EncryptedContainerType::kEcryptfs:
      if (migrating_container_ &&
          migrating_container_->GetType() == EncryptedContainerType::kFscrypt) {
        return MountType::ECRYPTFS_TO_DIR_CRYPTO;
      }
      if (migrating_container_ &&
          migrating_container_->GetType() == EncryptedContainerType::kDmcrypt) {
        return MountType::ECRYPTFS_TO_DMCRYPT;
      }
      return MountType::ECRYPTFS;
    case EncryptedContainerType::kFscrypt:
      if (migrating_container_) {
        return MountType::DIR_CRYPTO_TO_DMCRYPT;
      }
      return MountType::DIR_CRYPTO;
    case EncryptedContainerType::kDmcrypt:
      return MountType::DMCRYPT;
    case EncryptedContainerType::kEphemeral:
      return MountType::EPHEMERAL;
    default:
      return MountType::NONE;
  }
}

bool CryptohomeVault::SetLazyTeardownWhenUnused() {
  bool ret = true;
  if (!container_->SetLazyTeardownWhenUnused()) {
    LOG(ERROR) << "Failed to set lazy teardown for container";
    ret = false;
  }

  if (migrating_container_ &&
      !migrating_container_->SetLazyTeardownWhenUnused()) {
    LOG(ERROR) << "Failed to set lazy teardown for migrating container";
    ret = false;
  }

  if (cache_container_ && !cache_container_->SetLazyTeardownWhenUnused()) {
    LOG(ERROR) << "Failed to set lazy teardown for cache container";
    ret = false;
  }

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

  return ret;
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

  return ret;
}

bool CryptohomeVault::PurgeCacheContainer() {
  if (!cache_container_)
    return false;

  if (cache_container_->Exists() && !cache_container_->Purge())
    return false;

  return true;
}

}  // namespace cryptohome
