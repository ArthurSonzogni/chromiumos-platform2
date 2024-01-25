// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libstorage/storage_container/storage_container_factory.h"

#include <memory>
#include <utility>

#include <base/files/file_path.h>
#include <libstorage/platform/keyring/keyring.h>
#include <libstorage/platform/keyring/real_keyring.h>
#include <libstorage/platform/platform.h>

#include "libstorage/storage_container/backing_device_factory.h"
#include "libstorage/storage_container/dmcrypt_container.h"
#include "libstorage/storage_container/ecryptfs_container.h"
#include "libstorage/storage_container/ephemeral_container.h"
#include "libstorage/storage_container/ext4_container.h"
#include "libstorage/storage_container/filesystem_key.h"
#include "libstorage/storage_container/fscrypt_container.h"
#include "libstorage/storage_container/storage_container.h"

namespace libstorage {

StorageContainerFactory::StorageContainerFactory(
    Platform* platform, MetricsLibraryInterface* metrics)
    : StorageContainerFactory(
          platform,
          metrics,
          std::make_unique<RealKeyring>(),
          std::make_unique<BackingDeviceFactory>(platform)) {}

StorageContainerFactory::StorageContainerFactory(
    Platform* platform,
    MetricsLibraryInterface* metrics,
    std::unique_ptr<Keyring> keyring,
    std::unique_ptr<BackingDeviceFactory> backing_device_factory)
    : platform_(platform),
      metrics_(metrics),
      keyring_(std::move(keyring)),
      backing_device_factory_(std::move(backing_device_factory)),
      allow_fscrypt_v2_(false) {}

std::unique_ptr<StorageContainer> StorageContainerFactory::Generate(
    const StorageContainerConfig& config,
    StorageContainerType type,
    const FileSystemKeyReference& key_reference) {
  switch (type) {
    case StorageContainerType::kFscrypt:
      return std::make_unique<FscryptContainer>(
          config.backing_dir, key_reference, allow_fscrypt_v2_, platform_,
          keyring_.get());
    case StorageContainerType::kEcryptfs:
      return std::make_unique<EcryptfsContainer>(
          config.backing_dir, key_reference, platform_, keyring_.get());
    case StorageContainerType::kExt4: {
      auto backing_device = Generate(
          config, config.filesystem_config.backend_type, key_reference);
      if (!backing_device) {
        LOG(ERROR)
            << "Could not create backing device for the filesystem container";
        return nullptr;
      }
      return std::make_unique<Ext4Container>(config.filesystem_config,
                                             std::move(backing_device),
                                             platform_, metrics_);
    }
    case StorageContainerType::kDmcrypt: {
      auto backing_device = backing_device_factory_->Generate(
          config.dmcrypt_config.backing_device_config);
      if (!backing_device) {
        LOG(ERROR) << "Could not create backing device for dmcrypt container";
        return nullptr;
      }
      return std::make_unique<DmcryptContainer>(
          config.dmcrypt_config, std::move(backing_device), key_reference,
          platform_, keyring_.get());
    }
    case StorageContainerType::kEphemeral: {
      auto backing_device =
          RamdiskDevice::Generate(config.backing_file_path, platform_);
      if (!backing_device) {
        LOG(ERROR) << "Could not create backing device for ephemeral container";
        return nullptr;
      }
      return std::make_unique<EphemeralContainer>(std::move(backing_device),
                                                  platform_);
    }
    case StorageContainerType::kEcryptfsToFscrypt:
    case StorageContainerType::kEcryptfsToDmcrypt:
    case StorageContainerType::kFscryptToDmcrypt:
      // The migrating type is handled by the higher level abstraction.
      // FALLTHROUGH
    case StorageContainerType::kUnknown:
      return nullptr;
  }
}

}  // namespace libstorage
