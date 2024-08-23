// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libstorage/storage_container/ephemeral_container.h"

#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/functional/callback_helpers.h>
#include <libstorage/platform/platform.h>

#include "libstorage/storage_container/filesystem_key.h"
#include "libstorage/storage_container/ramdisk_device.h"
#include "libstorage/storage_container/storage_container.h"
#include "libstorage/storage_container/unencrypted_container.h"

namespace libstorage {

EphemeralContainer::EphemeralContainer(
    std::unique_ptr<RamdiskDevice> backing_device, Platform* platform)
    : UnencryptedContainer(std::move(backing_device), platform) {}

EphemeralContainer::~EphemeralContainer() {
  std::ignore = Teardown();
  std::ignore = Purge();
}

bool EphemeralContainer::Setup(const FileSystemKey& encryption_key) {
  // Clean any pre-existing backend device for the user.
  if (backing_device_->Exists()) {
    std::ignore = backing_device_->Teardown();
    if (!backing_device_->Purge()) {
      LOG(ERROR) << "Can't teardown previous backing store for the ephemeral.";
    }
  }
  return UnencryptedContainer::Setup(encryption_key);
}

}  // namespace libstorage
