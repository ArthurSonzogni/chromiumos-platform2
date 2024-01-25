// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_STORAGE_CRYPTOHOME_VAULT_H_
#define CRYPTOHOME_STORAGE_CRYPTOHOME_VAULT_H_

#include <memory>
#include <string>
#include <unordered_map>

#include <dbus/cryptohome/dbus-constants.h>
#include <libstorage/platform/platform.h>
#include <libstorage/storage_container/filesystem_key.h>
#include <libstorage/storage_container/storage_container.h>

#include "cryptohome/storage/error.h"
#include "cryptohome/storage/mount_constants.h"
#include "cryptohome/username.h"

namespace cryptohome {

// A cryptohome vault represents the user's active encrypted containers that
// comprise the user's home directory and handles operations relating to setting
// up the user's home directory for mount and tearing down the encrypted
// containers after unmount.
class CryptohomeVault {
 public:
  struct Options {
    // Forces the type of new encrypted containers set up.
    libstorage::StorageContainerType force_type =
        libstorage::StorageContainerType::kUnknown;
    // Checks if migration should be allowed for the current vault. Currently,
    // this is only used for ecryptfs.
    bool migrate = false;
    // Checks if mount requests for ecryptfs mounts should be blocked without
    // migration.
    bool block_ecryptfs = false;
  };
  CryptohomeVault(
      const ObfuscatedUsername& obfuscated_username,
      std::unique_ptr<libstorage::StorageContainer> container,
      std::unique_ptr<libstorage::StorageContainer> migrating_container,
      std::unique_ptr<libstorage::StorageContainer> cache_container,
      std::unordered_map<std::string,
                         std::unique_ptr<libstorage::StorageContainer>>
          application_containers,
      libstorage::Platform* platform);
  ~CryptohomeVault();

  // Sets up the cryptohome vault for mounting.
  StorageStatus Setup(const libstorage::FileSystemKey& filesystem_key);

  // Evict the cryptohome filesystem key from memory. Currently only
  // Dmcrypt container based vault supports this operation.
  StorageStatus EvictKey();

  // Restore the in-memory cryptohome filesystem key. Currently only
  // dmcrypt container based vault supports this operation.
  StorageStatus RestoreKey(const libstorage::FileSystemKey& filesystem_key);

  // Removes the vault.
  bool Purge();

  // Tears down the vault post-unmount.
  bool Teardown();

  // Marks the underlying containers for lazy teardown once the last reference
  // to the containers has been dropped.
  bool SetLazyTeardownWhenUnused();

  // Get mount type for mount to use.
  MountType GetMountType();

  void ReportVaultEncryptionType();

  libstorage::StorageContainerType GetContainerType() {
    return container_ ? container_->GetType()
                      : libstorage::StorageContainerType::kUnknown;
  }
  base::FilePath GetContainerBackingLocation() {
    return container_ ? container_->GetBackingLocation() : base::FilePath();
  }
  libstorage::StorageContainerType GetMigratingContainerType() {
    return migrating_container_ ? migrating_container_->GetType()
                                : libstorage::StorageContainerType::kUnknown;
  }
  libstorage::StorageContainerType GetCacheContainerType() {
    return cache_container_ ? cache_container_->GetType()
                            : libstorage::StorageContainerType::kUnknown;
  }

  bool ResetApplicationContainer(const std::string& app);

  bool PurgeCacheContainer();

 private:
  friend class CryptohomeVaultTest;

  const ObfuscatedUsername obfuscated_username_;

  // Represents the active encrypted container for the vault.
  std::unique_ptr<libstorage::StorageContainer> container_;
  // During migration, we set up the target migration container as
  // |migrating_container_|.
  std::unique_ptr<libstorage::StorageContainer> migrating_container_;
  // For dm-crypt based vaults, we set up an additional cache container that
  // serves as the backing store for temporary data.
  std::unique_ptr<libstorage::StorageContainer> cache_container_;
  // Containers that store application info.
  std::unordered_map<std::string, std::unique_ptr<libstorage::StorageContainer>>
      application_containers_;

  libstorage::Platform* platform_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_STORAGE_CRYPTOHOME_VAULT_H_
