// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libstorage/storage_container/ext4_container.h"

#include <map>
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

#include <libstorage/platform/platform.h>
#include <metrics/metrics_library.h>

extern "C" {
#include <ext2fs/ext2fs.h>
}

namespace libstorage {

namespace {

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
// Taken from chromium/src/tools/metrics/histograms/enums.xml
enum class UmaFsckResult {
  kUnexpected = 0,
  kCombinedError = 1,
  kNoErrors = 2,
  kErrorsCorrected = 3,
  kSystemShouldReboot = 4,
  kErrorsLeftUncorrected = 5,
  kOperationalError = 6,
  kUsageError = 7,
  kCancelled = 8,
  kSharedLibraryError = 9,
  kMaxValue = kSharedLibraryError,
};

// Helper function that maps an fsck result to it's respective enum.
std::vector<UmaFsckResult> MapFsckResultToEnum(int fsck_result) {
  std::vector<UmaFsckResult> errors;
  if (fsck_result == 0) {
    errors.push_back(UmaFsckResult::kNoErrors);
    return errors;
  }

  const std::map<int, UmaFsckResult> all_fsck_errors = {
      {FSCK_ERROR_CORRECTED, UmaFsckResult::kErrorsCorrected},
      {FSCK_SYSTEM_SHOULD_REBOOT, UmaFsckResult::kSystemShouldReboot},
      {FSCK_ERRORS_LEFT_UNCORRECTED, UmaFsckResult::kErrorsLeftUncorrected},
      {FSCK_OPERATIONAL_ERROR, UmaFsckResult::kOperationalError},
      {FSCK_USAGE_OR_SYNTAX_ERROR, UmaFsckResult::kUsageError},
      {FSCK_USER_CANCELLED, UmaFsckResult::kCancelled},
      {FSCK_SHARED_LIB_ERROR, UmaFsckResult::kSharedLibraryError},
  };
  for (auto error : all_fsck_errors) {
    if (fsck_result & error.first) {
      errors.push_back(error.second);
      fsck_result &= ~error.first;
    }
  }
  if (fsck_result != 0)
    errors.push_back(UmaFsckResult::kUnexpected);
  if (errors.size() > 1)
    errors.push_back(UmaFsckResult::kCombinedError);
  return errors;
}

bool ReadSuperBlock(Platform* platform,
                    const base::FilePath device_file,
                    struct ext2_super_block* super_block) {
  base::File device_raw_file;
  platform->InitializeFile(&device_raw_file, device_file,
                           base::File::FLAG_OPEN | base::File::FLAG_READ);
  if (!device_raw_file.IsValid()) {
    PLOG(ERROR) << "unable to open: " << device_file;
    return false;
  }
  if (device_raw_file.Read(SUPERBLOCK_OFFSET,
                           reinterpret_cast<char*>(super_block),
                           SUPERBLOCK_SIZE) != SUPERBLOCK_SIZE) {
    PLOG(ERROR) << "unable to read superblock from: " << device_file;
    return false;
  }
  return true;
}

std::string GetMetricsPrefix(const base::FilePath backing) {
  // Order is important.
  // Stateful backend device differs depending on wether LVM is used or not.
  // User data is only available on LVM.
  const std::map<std::string, std::string> tracked = {
      {"encstateful", "Platform.FileSystem.EncStateful"},
      {"stateful", "Platform.FileSystem.Stateful"},
      {"unencrypted", "Platform.FileSystem.Stateful"},
      {"-data", "Platform.FileSystem.UserData"}};

  if (backing.empty())
    return std::string();

  for (auto tracked_entry : tracked) {
    if (backing.value().find(tracked_entry.first) != std::string::npos) {
      return tracked_entry.second;
    }
  }
  return std::string();
}
}  // namespace

Ext4Container::Ext4Container(
    const Ext4FileSystemConfig& config,
    std::unique_ptr<StorageContainer> backing_container,
    Platform* platform,
    MetricsLibraryInterface* metrics)
    : mkfs_opts_(config.mkfs_opts),
      tune2fs_opts_(config.tune2fs_opts),
      recovery_(config.recovery),
      backing_container_(std::move(backing_container)),
      platform_(platform),
      metrics_(metrics),
      metrics_prefix_(GetMetricsPrefix(GetBackingLocation())),
      blk_count_(0) {}

bool Ext4Container::Purge() {
  return backing_container_->Purge();
}

bool Ext4Container::Exists() {
  if (!backing_container_->Exists())
    return false;

  // TODO(gwendal): Check there is a valid superblock by checking the signature,
  // sb->s_magic == EXT2_SUPER_MAGIC.
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

    if (rc) {
      // Legacy UMA
      SendBool("_RecoveryNeeded", fsck_err & FSCK_ERROR_CORRECTED);
      SendBool("_FsckNeeded", false);
    } else {
      // Legacy UMA
      SendBool("_FsckNeeded", true);
      SendBool("_RecoveryNeeded", true);
    }
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

    // Finally, print the overall results of the last fsck.
    for (auto fsck_error : MapFsckResultToEnum(fsck_err))
      SendEnum(".fsckResult", fsck_error);
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

  struct ext2_super_block super_block;
  if (!ReadSuperBlock(platform_, backing, &super_block))
    return false;

  // Gather Filesystem errors from superblock.
  SendSample("_ErrorCount", super_block.s_error_count, 0, 100000, 20);

  blk_count_ = ext2fs_blocks_count(&super_block);

  std::move(device_cleanup_runner).Cancel();
  return true;
}

bool Ext4Container::Resize(int64_t size_in_bytes) {
  if (size_in_bytes % kExt4BlockSize) {
    LOG(WARNING) << "Only multiple of block allowed: requested size: "
                 << size_in_bytes << "will be truncated.";
  }

  base::FilePath device = GetBackingLocation();
  uint64_t device_size_in_bytes;
  if (!platform_->GetBlkSize(device, &device_size_in_bytes) ||
      device_size_in_bytes < kExt4BlockSize) {
    PLOG(ERROR) << "Failed to get block device size";
    return false;
  }

  uint64_t desired_blk_count;
  if (size_in_bytes == 0) {
    desired_blk_count = device_size_in_bytes / kExt4BlockSize;
  } else {
    desired_blk_count = size_in_bytes / kExt4BlockSize;
    if (desired_blk_count > device_size_in_bytes / kExt4BlockSize) {
      LOG(ERROR) << "Resizing the underlying device is not supported yet. "
                    "Requested size "
                 << size_in_bytes << " greater than block size "
                 << device_size_in_bytes << ".";
      return false;
    }
  }

  if (blk_count_ != desired_blk_count) {
    LOG(INFO) << "Filesystem resized for " << device << " from "
              << blk_count_ * kExt4BlockSize << " bytes to "
              << desired_blk_count * kExt4BlockSize << " bytes.";
    if (!platform_->ResizeFilesystem(device, desired_blk_count)) {
      PLOG(ERROR) << "Filesystem resize failed for " << device;
      return false;
    }
    blk_count_ = desired_blk_count;
  }
  return true;
}

}  // namespace libstorage
