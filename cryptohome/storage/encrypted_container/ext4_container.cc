// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/storage/encrypted_container/ext4_container.h"

#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>

#include <absl/cleanup/cleanup.h>
#include <base/files/file_path.h>
#include <base/functional/bind.h>
#include <base/functional/callback_helpers.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>

#include "cryptohome/platform.h"

namespace cryptohome {

Ext4Container::Ext4Container(
    const Ext4FileSystemConfig& config,
    std::unique_ptr<EncryptedContainer> backing_container,
    Platform* platform)
    : mkfs_opts_(config.mkfs_opts),
      tune2fs_opts_(config.tune2fs_opts),
      backing_container_(std::move(backing_container)),
      platform_(platform) {}

bool Ext4Container::Purge() {
  return backing_container_->Purge();
}

bool Ext4Container::Exists() {
  if (!backing_container_->Exists())
    return false;

  // TODO(gwendal): Check there is a valid superblock.
  return true;
}

bool Ext4Container::Setup(const FileSystemKey& encryption_key) {
  // Check whether the kernel keyring provisioning is supported by the current
  // kernel.
  bool created = false;
  if (!backing_container_->Exists()) {
    LOG(INFO) << "Creating backing device for filesystem";
    created = true;
  }
  if (!backing_container_->Setup(encryption_key)) {
    LOG(ERROR) << "Failed to setup backing device";
    return false;
  }

  // Ensure that the dm-crypt device or the underlying backing device are
  // not left attached on the failure paths. If the backing device was created
  // during setup, purge it as well.
  absl::Cleanup device_cleanup_runner = [this, created]() {
    if (created) {
      Purge();
    } else {
      Teardown();
    }
  };

  base::FilePath backing(GetBackingLocation());
  if (created) {
    LOG(INFO) << "Running mke2fs on " << backing;
    if (!platform_->FormatExt4(backing, mkfs_opts_, 0)) {
      LOG(ERROR) << "Failed to format ext4 filesystem";
      return false;
    }
  }

  // Modify features depending on whether we already have the following enabled.
  LOG(INFO) << "Tuning filesystem features";
  if (!tune2fs_opts_.empty()) {
    if (!platform_->Tune2Fs(backing, tune2fs_opts_)) {
      return false;
    }
  }

  std::move(device_cleanup_runner).Cancel();
  return true;
}

}  // namespace cryptohome
