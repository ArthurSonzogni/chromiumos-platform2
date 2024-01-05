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
      recovery_(config.recovery),
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

  int fsck_err;
  base::FilePath backing(GetBackingLocation());
  if (!created) {
    // Check filesystem with e2fsck preen option. Since we are formating with no
    // time or mount count no deep check will be attempted by preen option.
    bool rc = platform_->Fsck(backing, FsckOption::kPreen, &fsck_err);

    if ((!rc) && (fsck_err & FSCK_ERRORS_LEFT_UNCORRECTED)) {
      // Only go deeper if when we are sure we have more filesystem error to
      // correct. Skip when we have fsck internal errors, we could slow down
      // boot/mount unnecessarily.
      LOG(WARNING) << backing
                   << ": needs more filesystem cleanup: error returned: "
                   << fsck_err;
      switch (recovery_) {
        case RecoveryType::kEnforceCleaning:
          platform_->Fsck(backing, FsckOption::kFull, &fsck_err);
          break;
        case RecoveryType::kPurge:
          LOG(WARNING) << backing << ": is beeing recreated";
          Purge();
          if (!backing_container_->Setup(encryption_key)) {
            LOG(ERROR) << "Failed to recreate backing device";
            return false;
          }
          created = true;
          break;
        case RecoveryType::kDoNothing:
          break;
      }
    }
    LOG_IF(ERROR, (fsck_err & ~FSCK_ERROR_CORRECTED) != FSCK_SUCCESS)
        << backing
        << ": fsck found uncorrected errors: error returned: " << fsck_err;
  }

  if (created) {
    LOG(INFO) << "Running mke2fs on " << backing;
    if (!platform_->FormatExt4(backing, mkfs_opts_, 0)) {
      LOG(ERROR) << "Failed to format ext4 filesystem";
      return false;
    }
    fsck_err = FSCK_SUCCESS;
  }

  // Modify features depending on whether we already have the following enabled.
  LOG(INFO) << "Tuning filesystem features";
  if (!tune2fs_opts_.empty()) {
    if (!platform_->Tune2Fs(backing, tune2fs_opts_)) {
      if (created) {
        PLOG(ERROR) << backing
                    << ": Failed to tune on a newly created filesystem.";
        return false;
      }

      if (recovery_ == RecoveryType::kEnforceCleaning) {
        PLOG(ERROR) << backing
                    << ": Failed to tune, deep fsck already ran:" << fsck_err;
        return false;
      }

      PLOG(WARNING) << backing
                    << ": Failed to tune ext4 filesystem - continue anyway.";
    }
  }

  std::move(device_cleanup_runner).Cancel();
  return true;
}

}  // namespace cryptohome
