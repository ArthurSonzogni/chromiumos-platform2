// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBSTORAGE_STORAGE_CONTAINER_DMCRYPT_CONTAINER_H_
#define LIBSTORAGE_STORAGE_CONTAINER_DMCRYPT_CONTAINER_H_

#include "libstorage/storage_container/storage_container.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <brillo/blkdev_utils/device_mapper.h>
#include <brillo/blkdev_utils/loop_device.h>
#include <brillo/brillo_export.h>
#include <libstorage/platform/keyring/keyring.h>
#include <libstorage/platform/platform.h>

#include "libstorage/storage_container/backing_device.h"
#include "libstorage/storage_container/filesystem_key.h"

namespace libstorage {

// `DmcryptContainer` is a block-level encrypted container, complete with its
// own filesystem (by default ext4). The backing storage for the dm-crypt
// container is currently a loopback device over a sparse file.
class BRILLO_EXPORT DmcryptContainer : public StorageContainer {
 public:
  DmcryptContainer(const DmcryptConfig& config,
                   std::unique_ptr<BackingDevice> backing_device,
                   const FileSystemKeyReference& key_reference,
                   Platform* platform,
                   Keyring* keyring,
                   std::unique_ptr<brillo::DeviceMapper> device_mapper);

  DmcryptContainer(const DmcryptConfig& config,
                   std::unique_ptr<BackingDevice> backing_device,
                   const FileSystemKeyReference& key_reference,
                   Platform* platform,
                   Keyring* keyring);

  ~DmcryptContainer() {}

  bool Exists() override;

  bool Purge() override;

  bool Reset() override;

  bool Setup(const FileSystemKey& encryption_key) override;

  bool RestoreKey(const FileSystemKey& encryption_key) override;

  bool Teardown() override;

  bool EvictKey() override;

  StorageContainerType GetType() const override {
    return StorageContainerType::kDmcrypt;
  }

  bool IsLazyTeardownSupported() const override { return true; }

  bool SetLazyTeardownWhenUnused() override;

  base::FilePath GetPath() const override;

  base::FilePath GetBackingLocation() const override;

 private:
  // Configuration for the encrypted container.
  const std::string dmcrypt_device_name_;
  const std::string dmcrypt_cipher_;

  const uint32_t iv_offset_;

  // Backing device for the encrypted container.
  std::unique_ptr<BackingDevice> backing_device_;

  // Key reference for filesystem key.
  FileSystemKeyReference key_reference_;

  Platform* platform_;
  Keyring* keyring_;
  std::unique_ptr<brillo::DeviceMapper> device_mapper_;
};

}  // namespace libstorage

#endif  // LIBSTORAGE_STORAGE_CONTAINER_DMCRYPT_CONTAINER_H_
