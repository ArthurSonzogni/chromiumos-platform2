// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBSTORAGE_STORAGE_CONTAINER_FAKE_STORAGE_CONTAINER_FACTORY_H_
#define LIBSTORAGE_STORAGE_CONTAINER_FAKE_STORAGE_CONTAINER_FACTORY_H_

#include "libstorage/storage_container/storage_container_factory.h"

#include <memory>
#include <utility>

#include <base/files/file_path.h>
#include <brillo/blkdev_utils/device_mapper_fake.h>
#include <libstorage/platform/keyring/keyring.h>
#include <libstorage/platform/platform.h>

#include "libstorage/storage_container/backing_device.h"
#include "libstorage/storage_container/dmcrypt_container.h"
#include "libstorage/storage_container/ecryptfs_container.h"
#include "libstorage/storage_container/ext4_container.h"
#include "libstorage/storage_container/fake_backing_device.h"
#include "libstorage/storage_container/fscrypt_container.h"
#include "libstorage/storage_container/storage_container.h"

namespace libstorage {

// Fake for generating fake encrypted containers.
class FakeStorageContainerFactory : public StorageContainerFactory {
 public:
  explicit FakeStorageContainerFactory(Platform* platform,
                                       std::unique_ptr<Keyring> keyring)
      : StorageContainerFactory(platform, /* metrics */ nullptr),
        platform_(platform),
        keyring_(std::move(keyring)),
        backing_device_factory_(platform) {}

  ~FakeStorageContainerFactory() = default;

  std::unique_ptr<StorageContainer> Generate(
      const StorageContainerConfig& config,
      const StorageContainerType type,
      const FileSystemKeyReference& key_reference) override {
    return Generate(config, type, key_reference, /*create=*/false);
  }

  std::unique_ptr<StorageContainer> Generate(
      const StorageContainerConfig& config,
      const StorageContainerType type,
      const FileSystemKeyReference& key_reference,
      bool create) {
    switch (type) {
      case StorageContainerType::kFscrypt:
        return std::make_unique<FscryptContainer>(
            config.backing_dir, key_reference,
            /*allow_v2=*/true, platform_, keyring_.get());
      case StorageContainerType::kEcryptfs:
        return std::make_unique<EcryptfsContainer>(
            config.backing_dir, key_reference, platform_, keyring_.get());
      case StorageContainerType::kDmcrypt: {
        std::unique_ptr<BackingDevice> backing_device =
            backing_device_factory_.Generate(
                config.dmcrypt_config.backing_device_config);
        if (create)
          backing_device->Create();
        return std::make_unique<DmcryptContainer>(
            config.dmcrypt_config, std::move(backing_device), key_reference,
            platform_, keyring_.get(),
            std::make_unique<brillo::DeviceMapper>(
                base::BindRepeating(&brillo::fake::CreateDevmapperTask)));
      }
      case StorageContainerType::kExt4: {
        auto backing_device = Generate(
            config, config.filesystem_config.backend_type, key_reference);
        if (!backing_device)
          return nullptr;
        return std::make_unique<Ext4Container>(
            config.filesystem_config, std::move(backing_device), platform_,
            /* metrics */ nullptr);
      }
      default:
        return nullptr;
    }
  }

 private:
  Platform* platform_;
  std::unique_ptr<Keyring> keyring_;
  FakeBackingDeviceFactory backing_device_factory_;
};

}  // namespace libstorage

#endif  // LIBSTORAGE_STORAGE_CONTAINER_FAKE_STORAGE_CONTAINER_FACTORY_H_
