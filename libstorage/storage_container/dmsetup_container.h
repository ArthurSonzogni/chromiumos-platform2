// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBSTORAGE_STORAGE_CONTAINER_DMSETUP_CONTAINER_H_
#define LIBSTORAGE_STORAGE_CONTAINER_DMSETUP_CONTAINER_H_

#include <memory>
#include <optional>
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
#include "libstorage/storage_container/storage_container.h"

namespace libstorage {

// `DmsetupContainer` is a block-level encrypted container, used to
// set up a dm-default or dm-crypt device.
// The backing storage for thatcontainer is a loopback device over a sparse
// file, a LVM Logical Volume or a device partition.
class BRILLO_EXPORT DmsetupContainer : public StorageContainer {
 public:
  DmsetupContainer(StorageContainerType type,
                   const DmsetupConfig& config,
                   std::unique_ptr<BackingDevice> backing_device,
                   const FileSystemKeyReference& key_reference,
                   Platform* platform,
                   Keyring* keyring,
                   std::unique_ptr<brillo::DeviceMapper> device_mapper);

  DmsetupContainer(StorageContainerType type,
                   const DmsetupConfig& config,
                   std::unique_ptr<BackingDevice> backing_device,
                   const FileSystemKeyReference& key_reference,
                   Platform* platform,
                   Keyring* keyring);

  ~DmsetupContainer() {}

  bool Exists() override;

  bool IsDeviceKeyValid() override;

  bool Purge() override;

  bool Reset() override;

  bool Setup(const FileSystemKey& encryption_key) override;

  bool Teardown() override;

  bool EvictKey() override;

  StorageContainerType GetType() const override { return dmsetup_type_; }

  bool IsLazyTeardownSupported() const override { return true; }

  bool SetLazyTeardownWhenUnused() override;

  base::FilePath GetPath() const override;

  base::FilePath GetBackingLocation() const override;

  static inline std::optional<const std::string> GetDmsetupType(
      StorageContainerType type) {
    switch (type) {
      case StorageContainerType::kDmcrypt:
        return "crypt";
      case StorageContainerType::kDmDefaultKey:
        return "default-key";
      default:
        return std::nullopt;
    }
  }

 private:
  // Configuration for the encrypted container.
  const std::string dmsetup_device_name_;
  const std::string dmsetup_cipher_;
  const StorageContainerType dmsetup_type_;

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

#endif  // LIBSTORAGE_STORAGE_CONTAINER_DMSETUP_CONTAINER_H_
