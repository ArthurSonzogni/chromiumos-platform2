// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/storage/encrypted_container/ephemeral_container.h"

#include <memory>
#include <utility>

#include <base/callback_helpers.h>
#include <base/files/file_path.h>

#include "cryptohome/platform.h"
#include "cryptohome/storage/encrypted_container/encrypted_container.h"
#include "cryptohome/storage/encrypted_container/filesystem_key.h"
#include "cryptohome/storage/encrypted_container/ramdisk_device.h"

namespace cryptohome {

EphemeralContainer::EphemeralContainer(
    std::unique_ptr<RamdiskDevice> backing_device, Platform* platform)
    : backing_device_(std::move(backing_device)), platform_(platform) {}

EphemeralContainer::EphemeralContainer(
    std::unique_ptr<FakeBackingDevice> backing_device, Platform* platform)
    : backing_device_(std::move(backing_device)), platform_(platform) {}

EphemeralContainer::~EphemeralContainer() {
  ignore_result(Teardown());
  ignore_result(Purge());
}

bool EphemeralContainer::Exists() {
  return backing_device_->Exists();
}

bool EphemeralContainer::Purge() {
  return backing_device_->Purge();
}

bool EphemeralContainer::Setup(const FileSystemKey& encryption_key,
                               bool create) {
  // This is a validity check. Higher level code shouldn't even try using an
  // ephemeral container with keys, or try to re-use an existing one.
  if (encryption_key != FileSystemKey()) {
    LOG(ERROR) << "Encryption key for ephemeral must be empty";
    return false;
  }
  if (!create) {
    LOG(ERROR) << "Ephemeral setup should always have 'create' flag set";
    return false;
  }

  base::ScopedClosureRunner cleanup(base::BindOnce(
      [](EphemeralContainer* container, BackingDevice* backing_device) {
        // Try purging backing device even if teardown failed.
        ignore_result(container->Teardown());
        ignore_result(container->Purge());
      },
      base::Unretained(this), base::Unretained(backing_device_.get())));

  // Create and setup the backing device the backing device.
  if (!backing_device_->Create()) {
    LOG(ERROR) << "Can't create backing store for the mount.";
    return false;
  }
  if (!backing_device_->Setup()) {
    LOG(ERROR) << "Can't setup backing store for the mount.";
    return false;
  }

  // Format the device. At this point, even if the backing device was already
  // present, it will lose all of its content.
  base::Optional<base::FilePath> backing_device_path =
      backing_device_->GetPath();
  if (!backing_device_path.has_value()) {
    LOG(ERROR) << "Failed to get backing device path";
    return false;
  }
  if (!platform_->FormatExt4(*backing_device_path, kDefaultExt4FormatOpts, 0)) {
    LOG(ERROR) << "Can't format ephemeral backing device as ext4";
    return false;
  }

  ignore_result(cleanup.Release());

  return true;
}

bool EphemeralContainer::Teardown() {
  // Try purging backing device even if teardown failed.
  ignore_result(backing_device_->Teardown());
  return backing_device_->Purge();
}

bool EphemeralContainer::SetLazyTeardownWhenUnused() {
  // TODO(dlunev): decide if it needs to be implemented.
  return false;
}

base::FilePath EphemeralContainer::GetBackingLocation() const {
  if (backing_device_ != nullptr && backing_device_->GetPath().has_value()) {
    return *(backing_device_->GetPath());
  }
  return base::FilePath();
}

}  // namespace cryptohome
