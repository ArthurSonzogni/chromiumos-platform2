// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBSTORAGE_STORAGE_CONTAINER_EPHEMERAL_CONTAINER_H_
#define LIBSTORAGE_STORAGE_CONTAINER_EPHEMERAL_CONTAINER_H_

#include <memory>

#include <brillo/brillo_export.h>
#include <libstorage/platform/platform.h>

#include "libstorage/storage_container/fake_backing_device.h"
#include "libstorage/storage_container/filesystem_key.h"
#include "libstorage/storage_container/ramdisk_device.h"
#include "libstorage/storage_container/storage_container.h"

namespace libstorage {

// EphemeralContainer accepts a ramdisk backing device and ensures its purge
// upon container's teardown.
class BRILLO_EXPORT EphemeralContainer final : public StorageContainer {
 public:
  // Unlike other containers, it forces a specific backing device type top
  // enforce that only ramdisk backed devices are used.
  EphemeralContainer(std::unique_ptr<RamdiskDevice> backing_device,
                     Platform* platform);

  ~EphemeralContainer() override;

  bool Exists() override;

  bool Purge() override;

  bool Setup(const FileSystemKey& encryption_key) override;

  bool Reset() override;

  bool Teardown() override;

  StorageContainerType GetType() const override {
    return StorageContainerType::kEphemeral;
  }

  base::FilePath GetPath() const override { return GetBackingLocation(); }

  base::FilePath GetBackingLocation() const override;

 private:
  // A private constructor with FakeBackingDevice for tests.
  EphemeralContainer(std::unique_ptr<FakeBackingDevice> backing_device,
                     Platform* platform);

  const std::unique_ptr<BackingDevice> backing_device_;
  Platform* platform_;

  friend class EphemeralContainerTest;
};

}  // namespace libstorage

#endif  // LIBSTORAGE_STORAGE_CONTAINER_EPHEMERAL_CONTAINER_H_
