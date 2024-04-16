// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBSTORAGE_STORAGE_CONTAINER_UNENCRYPTED_CONTAINER_H_
#define LIBSTORAGE_STORAGE_CONTAINER_UNENCRYPTED_CONTAINER_H_

#include <memory>
#include <utility>

#include <brillo/brillo_export.h>
#include <libstorage/platform/platform.h>

#include "libstorage/storage_container/fake_backing_device.h"
#include "libstorage/storage_container/filesystem_key.h"
#include "libstorage/storage_container/ramdisk_device.h"
#include "libstorage/storage_container/storage_container.h"
#include "libstorage/storage_container/unencrypted_container.h"

namespace libstorage {

class BRILLO_EXPORT UnencryptedContainer : public StorageContainer {
 public:
  UnencryptedContainer(std::unique_ptr<BackingDevice> backing_device,
                       Platform* platform);

  ~UnencryptedContainer() override = default;

  bool Exists() override;

  bool Purge() override;

  bool Setup(const FileSystemKey& encryption_key) override;

  bool Reset() override;

  bool Teardown() override;

  StorageContainerType GetType() const override {
    return StorageContainerType::kUnencrypted;
  }

  base::FilePath GetPath() const override { return GetBackingLocation(); }

  base::FilePath GetBackingLocation() const override;

 protected:
  const std::unique_ptr<BackingDevice> backing_device_;
};

}  // namespace libstorage

#endif  // LIBSTORAGE_STORAGE_CONTAINER_UNENCRYPTED_CONTAINER_H_
