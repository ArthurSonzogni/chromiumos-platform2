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
#include "libstorage/storage_container/unencrypted_container.h"

namespace libstorage {

// Ephemeral container are unencrypted container backed exclusively by a RAM
// disk. It is used by cryptohome for ephemeral users, as it is guaranteed
// the data is purged upon container's teardown.
class EphemeralContainer : public UnencryptedContainer {
 public:
  // Unlike other containers, it forces a specific backing device type top
  // enforce that only ramdisk backed devices are used.
  EphemeralContainer(std::unique_ptr<RamdiskDevice> backing_device,
                     Platform* platform);

  ~EphemeralContainer() override;

  bool Setup(const FileSystemKey& encryption_key) override;

  StorageContainerType GetType() const override {
    return StorageContainerType::kEphemeral;
  }
};

}  // namespace libstorage

#endif  // LIBSTORAGE_STORAGE_CONTAINER_EPHEMERAL_CONTAINER_H_
