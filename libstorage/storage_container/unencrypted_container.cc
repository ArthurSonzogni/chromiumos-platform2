// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libstorage/storage_container/unencrypted_container.h"

#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <absl/cleanup/cleanup.h>
#include <base/files/file_path.h>
#include <base/functional/callback_helpers.h>
#include <libstorage/platform/platform.h>

#include "libstorage/storage_container/filesystem_key.h"
#include "libstorage/storage_container/ramdisk_device.h"
#include "libstorage/storage_container/storage_container.h"

namespace libstorage {

UnencryptedContainer::UnencryptedContainer(
    std::unique_ptr<BackingDevice> backing_device, Platform* platform)
    : backing_device_(std::move(backing_device)) {}

bool UnencryptedContainer::Exists() {
  return backing_device_->Exists();
}

bool UnencryptedContainer::Purge() {
  return backing_device_->Purge();
}

bool UnencryptedContainer::Setup(const FileSystemKey& encryption_key) {
  // This is a validity check. Higher level code shouldn't even try using an
  // unencrypted container with keys, or try to re-use an existing one.
  if (encryption_key != FileSystemKey()) {
    LOG(ERROR) << "Encryption key for unencrypted must be empty";
    return false;
  }

  bool created = false;
  if (!backing_device_->Exists()) {
    LOG(INFO) << "Creating backing device for " << GetPath();
    if (!backing_device_->Create()) {
      LOG(ERROR) << "Failed to create backing device";
      return false;
    }
    created = true;
  }

  // Ensure that the dm-crypt device or the underlying backing device are
  // not left attached on the failure paths. If the backing device was created
  // during setup, purge it as well.
  absl::Cleanup cleanup = [this, created]() {
    if (created) {
      Purge();
    } else {
      Teardown();
    }
  };
  if (!backing_device_->Setup()) {
    LOG(ERROR) << "Can't setup backing store for the mount.";
    return false;
  }

  std::move(cleanup).Cancel();
  return true;
}

bool UnencryptedContainer::Reset() {
  // Reset should never be called for unencrypted containers.
  LOG(ERROR) << "Reset not supported on unencrypted containers";
  return false;
}

bool UnencryptedContainer::Teardown() {
  // Try purging backing device even if teardown failed.
  std::ignore = backing_device_->Teardown();
  return backing_device_->Purge();
}

base::FilePath UnencryptedContainer::GetBackingLocation() const {
  if (backing_device_ != nullptr && backing_device_->GetPath().has_value()) {
    return *(backing_device_->GetPath());
  }
  return base::FilePath();
}

}  // namespace libstorage
