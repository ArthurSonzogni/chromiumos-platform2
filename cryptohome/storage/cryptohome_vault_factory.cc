// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/storage/cryptohome_vault_factory.h"

#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <absl/container/flat_hash_map.h>
#include <base/files/file_path.h>
#include <libstorage/platform/platform.h>
#include <libstorage/storage_container/filesystem_key.h>
#include <libstorage/storage_container/storage_container.h>
#include <libstorage/storage_container/storage_container_factory.h>

#include "cryptohome/filesystem_layout.h"
#include "cryptohome/storage/cryptohome_vault.h"
#include "cryptohome/storage/mount_constants.h"

namespace {
// Size of logical volumes to use for the dm-crypt cryptohomes.
constexpr uint64_t kLogicalVolumeSizePercent = 90;
constexpr uint32_t kArcContainerIVOffset = 2823358739;

// By default, each ext4 filesystem takes up ~2% of the entire filesystem space
// for storing filesystem metadata including inode tables. Tune the number of
// inodes such that the overall metadata cost is <1 % of the filesystem size.
// For larger storage devices, we increase the inode count up to an upper limit
// of 2^20 inodes.
uint64_t CalculateInodeCount(int64_t filesystem_size) {
  constexpr uint64_t kGigabytes = 1024 * 1024 * 1024;
  constexpr uint64_t kBaseInodeCount = 256 * 1024;

  if (filesystem_size <= 16 * kGigabytes) {
    return kBaseInodeCount;
  }
  if (filesystem_size <= 32 * kGigabytes) {
    return 2 * kBaseInodeCount;
  }

  return 4 * kBaseInodeCount;
}

// Get IV offsets for containers.
uint32_t GetContainerIVOffset(const std::string& container_name) {
  // For each container, generate a random 32-bit value to use as the IV offset
  // so that dmcrypt containers (for or compatibility with eMMC Inline
  // Encryption spec, that allows only 32-bit IVs).
  if (container_name == "arcvm") {
    // Make sure that the IVs don't wrap around with 32-bit devices with 128GB
    // storage.
    static_assert(kArcContainerIVOffset < std::numeric_limits<uint32_t>::max() -
                                              128UL * 1024 * 1024 * 2);
    return kArcContainerIVOffset;
  }

  return 0;
}

}  // namespace

namespace cryptohome {

CryptohomeVaultFactory::CryptohomeVaultFactory(
    libstorage::Platform* platform,
    std::unique_ptr<libstorage::StorageContainerFactory>
        storage_container_factory)
    : platform_(platform),
      storage_container_factory_(std::move(storage_container_factory)) {}

std::unique_ptr<libstorage::StorageContainer>
CryptohomeVaultFactory::GenerateStorageContainer(
    libstorage::StorageContainerType type,
    const ObfuscatedUsername& obfuscated_username,
    const libstorage::FileSystemKeyReference& key_reference,
    const std::string& container_identifier,
    const DmOptions& dm_options) {
  libstorage::StorageContainerConfig config;
  base::FilePath stateful_device;
  uint64_t stateful_size;

  switch (type) {
    case libstorage::StorageContainerType::kEcryptfs:
      config.backing_dir = GetEcryptfsUserVaultPath(obfuscated_username);
      break;
    case libstorage::StorageContainerType::kFscrypt:
      config.backing_dir = GetUserMountDirectory(obfuscated_username);
      break;
    case libstorage::StorageContainerType::kDmcrypt: {
      if (!vg_ || !vg_->IsValid() || !thinpool_ || !thinpool_->IsValid()) {
        return nullptr;
      }

      // Calculate size for dm-crypt partition.
      stateful_device = platform_->GetStatefulDevice();
      if (stateful_device.empty()) {
        PLOG(ERROR) << "Can't get stateful device";
        return nullptr;
      }

      if (!platform_->GetBlkSize(stateful_device, &stateful_size)) {
        PLOG(ERROR) << "Can't get size of stateful device";
        return nullptr;
      }

      LOG_IF(INFO, dm_options.keylocker_enabled)
          << "Using Keylocker for encryption";

      config.dmsetup_config = {
          .backing_device_config =
              {.type =
                   libstorage::BackingDeviceType::kLogicalVolumeBackingDevice,
               .name = LogicalVolumePrefix(obfuscated_username) +
                       container_identifier,
               .size = static_cast<int64_t>(
                   (stateful_size * kLogicalVolumeSizePercent) /
                   (100 * 1024 * 1024)),
               .logical_volume = {.vg = vg_, .thinpool = thinpool_}},
          .dmsetup_device_name =
              DmcryptVolumePrefix(obfuscated_username) + container_identifier,
          .dmsetup_cipher = dm_options.keylocker_enabled
                                ? "capi:xts-aes-aeskl-plain64"
                                : "aes-xts-plain64"};

      if (!dm_options.is_raw_device) {
        // Configure an ext4 filesystem device:
        config.filesystem_config = {
            // TODO(sarthakkukreti): Add more dynamic checks for filesystem
            // features once dm-crypt cryptohomes are stable.
            .mkfs_opts = {"-O", "^huge_file,^flex_bg,", "-N",
                          base::StringPrintf(
                              "%" PRIu64, CalculateInodeCount(stateful_size)),
                          "-E", "discard"},
            .tune2fs_opts = {"-O", "verity,quota,project", "-Q",
                             "usrquota,grpquota,prjquota"},
            .backend_type = type,
            .recovery = dm_options.is_cache_device
                            ? libstorage::RecoveryType::kPurge
                            : libstorage::RecoveryType::kDoNothing,
            .metrics_prefix = "Platform.FileSystem.UserData"};
        type = libstorage::StorageContainerType::kExt4;
      }
      break;
    }
    case libstorage::StorageContainerType::kEphemeral:
      // Configure an ext4 filesystem that will use a ramdisk device:
      config.filesystem_config = {
          // features once dm-crypt cryptohomes are stable.
          .mkfs_opts =
              {// Always use 'default' configuration.
               "-T", "default",
               // reserved-blocks-percentage = 0%
               "-m", "0",
               // ^huge_file: Do not allow files larger than 2TB.
               // ^flex_bg: Do not allow per-block group metadata to be placed
               // anywhere.
               // ^has_journal: Do not create journal.
               "-O", "^huge_file,^flex_bg,^has_journal",
               // Attempt to discard blocks at mkfs time.
               // Assume that the storage device is already zeroed out.
               "-E", "discard,assume_storage_prezeroed=1"},
          .backend_type = type,
          // No need to specify recovery, the device is purged at destruction.
      };
      type = libstorage::StorageContainerType::kExt4;
      config.unencrypted_config = {
          .backing_device_config = {
              .type = libstorage::BackingDeviceType::kRamdiskDevice,
              .ramdisk = {.backing_file_path =
                              base::FilePath(kEphemeralCryptohomeDir)
                                  .Append(kSparseFileDir)
                                  .Append(*obfuscated_username)}}};
      break;
    case libstorage::StorageContainerType::kExt4:
      // Not given directly, passed with the raw block device.
    case libstorage::StorageContainerType::kUnencrypted:
      // cryptohome does not use plain unencrypted device.
    case libstorage::StorageContainerType::kDmDefaultKey:
      // cryptohome does not use dm-default-key.:
    case libstorage::StorageContainerType::kEcryptfsToFscrypt:
    case libstorage::StorageContainerType::kEcryptfsToDmcrypt:
    case libstorage::StorageContainerType::kFscryptToDmcrypt:
      // The migrating type is handled by the higher level abstraction.
      // FALLTHROUGH
    case libstorage::StorageContainerType::kUnknown:
      LOG(ERROR) << "Incorrect container type: " << static_cast<int>(type);
      return nullptr;
  }

  return storage_container_factory_->Generate(config, type, key_reference);
}

std::unique_ptr<CryptohomeVault> CryptohomeVaultFactory::Generate(
    const ObfuscatedUsername& obfuscated_username,
    const libstorage::FileSystemKeyReference& key_reference,
    libstorage::StorageContainerType vault_type,
    bool keylocker_enabled) {
  libstorage::StorageContainerType container_type =
      libstorage::StorageContainerType::kUnknown;
  libstorage::StorageContainerType migrating_container_type =
      libstorage::StorageContainerType::kUnknown;

  if (vault_type == libstorage::StorageContainerType::kEcryptfsToFscrypt) {
    container_type = libstorage::StorageContainerType::kEcryptfs;
    migrating_container_type = libstorage::StorageContainerType::kFscrypt;
  } else if (vault_type ==
             libstorage::StorageContainerType::kEcryptfsToDmcrypt) {
    container_type = libstorage::StorageContainerType::kEcryptfs;
    migrating_container_type = libstorage::StorageContainerType::kDmcrypt;
  } else if (vault_type ==
             libstorage::StorageContainerType::kFscryptToDmcrypt) {
    container_type = libstorage::StorageContainerType::kFscrypt;
    migrating_container_type = libstorage::StorageContainerType::kDmcrypt;
  } else {
    container_type = vault_type;
  }

  // Generate containers for the vault.
  DmOptions vault_dm_options = {
      .keylocker_enabled = keylocker_enabled,
  };
  DmOptions cache_dm_options = {
      .keylocker_enabled = keylocker_enabled,
      .is_cache_device = true,
  };
  DmOptions app_dm_options = {
      .keylocker_enabled = keylocker_enabled,
      .is_raw_device = true,
  };

  std::unique_ptr<libstorage::StorageContainer> container =
      GenerateStorageContainer(container_type, obfuscated_username,
                               key_reference, kDmcryptDataContainerSuffix,
                               vault_dm_options);
  if (!container) {
    LOG(ERROR) << "Could not create vault container";
    return nullptr;
  }

  std::unique_ptr<libstorage::StorageContainer> migrating_container;
  if (migrating_container_type != libstorage::StorageContainerType::kUnknown) {
    migrating_container = GenerateStorageContainer(
        migrating_container_type, obfuscated_username, key_reference,
        kDmcryptDataContainerSuffix, vault_dm_options);
    if (!migrating_container) {
      LOG(ERROR) << "Could not create vault container for migration";
      return nullptr;
    }
  }

  std::unique_ptr<libstorage::StorageContainer> cache_container;
  absl::flat_hash_map<std::string,
                      std::unique_ptr<libstorage::StorageContainer>>
      application_containers;
  if (container_type == libstorage::StorageContainerType::kDmcrypt ||
      migrating_container_type == libstorage::StorageContainerType::kDmcrypt) {
    cache_container = GenerateStorageContainer(
        libstorage::StorageContainerType::kDmcrypt, obfuscated_username,
        key_reference, kDmcryptCacheContainerSuffix, cache_dm_options);
    if (!cache_container) {
      LOG(ERROR) << "Could not create vault container for cache";
      return nullptr;
    }
    if (enable_application_containers_) {
      for (const auto& app : std::vector<std::string>{"arcvm"}) {
        app_dm_options.iv_offset = GetContainerIVOffset(app);
        std::unique_ptr<libstorage::StorageContainer> tmp_container =
            GenerateStorageContainer(libstorage::StorageContainerType::kDmcrypt,
                                     obfuscated_username, key_reference, app,
                                     app_dm_options);
        if (!tmp_container) {
          LOG(ERROR) << "Could not create vault container for app: " << app;
          return nullptr;
        }
        application_containers[app] = std::move(tmp_container);
      }
    }
  }
  return std::make_unique<CryptohomeVault>(
      obfuscated_username, std::move(container), std::move(migrating_container),
      std::move(cache_container), std::move(application_containers), platform_);
}

void CryptohomeVaultFactory::CacheLogicalVolumeObjects(
    std::optional<brillo::VolumeGroup> vg,
    std::optional<brillo::Thinpool> thinpool) {
  if (!vg || !thinpool) {
    LOG(WARNING) << "Attempting to cache invalid logical volume objects.";
    return;
  }

  vg_ = std::make_shared<brillo::VolumeGroup>(*vg);
  thinpool_ = std::make_shared<brillo::Thinpool>(*thinpool);
}

bool CryptohomeVaultFactory::ContainerExists(const std::string& container) {
  brillo::LogicalVolumeManager* lvm = platform_->GetLogicalVolumeManager();

  if (!vg_ || !vg_->IsValid()) {
    return false;
  }

  return lvm->GetLogicalVolume(*vg_.get(), container) != std::nullopt;
}

}  // namespace cryptohome
