// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/storage/cryptohome_vault_factory.h"

#include <memory>
#include <string>
#include <utility>

#include <base/files/file_path.h>

#include "cryptohome/filesystem_layout.h"
#include "cryptohome/platform.h"
#include "cryptohome/storage/cryptohome_vault.h"
#include "cryptohome/storage/encrypted_container/encrypted_container.h"
#include "cryptohome/storage/encrypted_container/encrypted_container_factory.h"
#include "cryptohome/storage/encrypted_container/filesystem_key.h"

namespace {
// Size of logical volumes to use for the dm-crypt cryptohomes.
constexpr uint64_t kLogicalVolumeSizePercent = 90;

// By default, each ext4 filesystem takes up ~2% of the entire filesystem space
// for storing filesystem metadata including inode tables. Tune the number of
// inodes such that the overall metadata cost is <1 % of the filesystem size.
// For larger storage devices, we increase the inode count up to an upper limit
// of 2^20 inodes.
uint64_t CalculateInodeCount(int64_t filesystem_size) {
  constexpr uint64_t kGigabytes = 1024 * 1024 * 1024;
  constexpr uint64_t kBaseInodeCount = 256 * 1024;

  if (filesystem_size <= 16 * kGigabytes)
    return kBaseInodeCount;
  if (filesystem_size <= 32 * kGigabytes)
    return 2 * kBaseInodeCount;

  return 4 * kBaseInodeCount;
}

}  // namespace

namespace cryptohome {

CryptohomeVaultFactory::CryptohomeVaultFactory(
    Platform* platform,
    std::unique_ptr<EncryptedContainerFactory> encrypted_container_factory)
    : platform_(platform),
      encrypted_container_factory_(std::move(encrypted_container_factory)) {}

CryptohomeVaultFactory::CryptohomeVaultFactory(Platform* platform)
    : CryptohomeVaultFactory(
          platform, std::make_unique<EncryptedContainerFactory>(platform)) {}

CryptohomeVaultFactory::~CryptohomeVaultFactory() {}

std::unique_ptr<EncryptedContainer>
CryptohomeVaultFactory::GenerateEncryptedContainer(
    EncryptedContainerType type,
    const std::string& obfuscated_username,
    const FileSystemKeyReference& key_reference,
    const std::string& container_identifier,
    bool keylocker_enabled) {
  EncryptedContainerConfig config;
  base::FilePath stateful_device;
  uint64_t stateful_size;

  switch (type) {
    case EncryptedContainerType::kEcryptfs:
      config.backing_dir = GetEcryptfsUserVaultPath(obfuscated_username);
      config.type = EncryptedContainerType::kEcryptfs;
      break;
    case EncryptedContainerType::kFscrypt:
      config.backing_dir = GetUserMountDirectory(obfuscated_username);
      config.type = EncryptedContainerType::kFscrypt;
      break;
    case EncryptedContainerType::kDmcrypt:
      // Calculate size for dm-crypt partition.
      stateful_device = platform_->GetStatefulDevice();
      if (stateful_device.empty())
        return nullptr;

      if (!platform_->GetBlkSize(stateful_device, &stateful_size))
        return nullptr;

      LOG_IF(INFO, keylocker_enabled) << "Using Keylocker for encryption";

      config.type = EncryptedContainerType::kDmcrypt;
      config.dmcrypt_config = {
          .backing_device_config =
              {.type = BackingDeviceType::kLogicalVolumeBackingDevice,
               .name = LogicalVolumePrefix(obfuscated_username) +
                       container_identifier,
               .size = static_cast<int64_t>(
                   (stateful_size * kLogicalVolumeSizePercent) /
                   (100 * 1024 * 1024)),
               .logical_volume = {.thinpool_name = "thinpool",
                                  .physical_volume = stateful_device}},
          .dmcrypt_device_name =
              DmcryptVolumePrefix(obfuscated_username) + container_identifier,
          .dmcrypt_cipher = keylocker_enabled ? "capi:xts-aes-aeskl-plain64"
                                              : "aes-xts-plain64",
          // TODO(sarthakkukreti): Add more dynamic checks for filesystem
          // features once dm-crypt cryptohomes are stable.
          .mkfs_opts = {"-O", "^huge_file,^flex_bg,", "-N",
                        base::StringPrintf("%" PRIu64,
                                           CalculateInodeCount(stateful_size)),
                        "-E", "discard"},
          .tune2fs_opts = {"-O", "verity,quota,project", "-Q",
                           "usrquota,grpquota,prjquota"}};
      break;
    case EncryptedContainerType::kEphemeral:
      config.type = EncryptedContainerType::kEphemeral;
      config.backing_file_name = obfuscated_username;
      break;
    case EncryptedContainerType::kEcryptfsToFscrypt:
      // The migrating type is handled by the higher level abstraction.
      // FALLTHROUGH
    case EncryptedContainerType::kUnknown:
      return nullptr;
  }

  return encrypted_container_factory_->Generate(config, key_reference);
}

std::unique_ptr<CryptohomeVault> CryptohomeVaultFactory::Generate(
    const std::string& obfuscated_username,
    const FileSystemKeyReference& key_reference,
    EncryptedContainerType vault_type,
    bool keylocker_enabled) {
  EncryptedContainerType container_type = EncryptedContainerType::kUnknown;
  EncryptedContainerType migrating_container_type =
      EncryptedContainerType::kUnknown;

  if (vault_type != EncryptedContainerType::kEcryptfsToFscrypt) {
    container_type = vault_type;
  } else {
    container_type = EncryptedContainerType::kEcryptfs;
    migrating_container_type = EncryptedContainerType::kFscrypt;
  }

  // Generate containers for the vault.
  std::unique_ptr<EncryptedContainer> container = GenerateEncryptedContainer(
      container_type, obfuscated_username, key_reference,
      kDmcryptDataContainerSuffix, keylocker_enabled);
  if (!container) {
    LOG(ERROR) << "Could not create vault container";
    return nullptr;
  }

  std::unique_ptr<EncryptedContainer> migrating_container;
  if (migrating_container_type != EncryptedContainerType::kUnknown) {
    migrating_container = GenerateEncryptedContainer(
        migrating_container_type, obfuscated_username, key_reference,
        kDmcryptDataContainerSuffix, keylocker_enabled);
    if (!migrating_container) {
      LOG(ERROR) << "Could not create vault container for migration";
      return nullptr;
    }
  }

  std::unique_ptr<EncryptedContainer> cache_container;
  if (container_type == EncryptedContainerType::kDmcrypt) {
    cache_container =
        GenerateEncryptedContainer(container_type, obfuscated_username,
                                   key_reference, kDmcryptCacheContainerSuffix);
    if (!cache_container) {
      LOG(ERROR) << "Could not create vault container for cache";
      return nullptr;
    }
  }
  return std::make_unique<CryptohomeVault>(
      obfuscated_username, std::move(container), std::move(migrating_container),
      std::move(cache_container), platform_);
}

}  // namespace cryptohome
