// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/lockbox-cache-manager/lockbox-cache-manager.h"

#include <string>
#include <vector>

#include <base/logging.h>
#include <base/files/file_util.h>
#include <brillo/files/file_util.h>
#include <libhwsec-foundation/tpm/tpm_version.h>

namespace cryptohome {

LockboxCacheManager::LockboxCacheManager(const base::FilePath& root)
    : metrics_(std::make_unique<Metrics>()),
      platform_(std::make_unique<Platform>()),
      root_(root) {
  install_attrs_old_path_ = root_.Append(filepaths::kLocOldInstallAttrs);
  install_attrs_new_path_ = root_.Append(filepaths::kLocNewInstallAttrs);
  install_attrs_copy_path_ = root_.Append(filepaths::kLocCopyInstallAttrs);
  lockbox_cache_path_ = root_.Append(filepaths::kLocLockboxCache);
  lockbox_nvram_file_path_ = root_.Append(filepaths::kLocLockboxNvram);
}

// TODO(b/312392273): call the underlying library directly instead of calling
// the binary
void LockboxCacheManager::InvokeLockboxCacheTool() {
  std::string output;
  std::vector<std::string> argv = {
      "lockbox-cache", "--nvram=" + lockbox_nvram_file_path_.value(),
      "--cache=" + lockbox_cache_path_.value(),
      "--lockbox=" + install_attrs_new_path_.value()};

  if (!platform_->GetAppOutputAndError(std::move(argv), &output)) {
    LOG(WARNING) << output;
  }
}

bool LockboxCacheManager::Run() {
  // Only needed if tpm version is dynamic. Otherwise, no-op.
  PopulateLockboxNvramFile();

  auto migration_status = MigrateInstallAttributesIfNeeded();
  metrics_->ReportInstallAttributesMigrationStatus(migration_status);

  if (migration_status != MigrationStatus::kNotNeeded &&
      migration_status != MigrationStatus::kSuccess) {
    LOG(ERROR) << "Failed to migrate install-time attributes content!";
    return false;
  }

  // Pre-work is done. It's time for validation and cache creation.
  if (!CreateLockboxCache()) {
    LOG(WARNING) << "Failed to create lockbox-cache";
    return false;
  }

  // There are no other consumers of the nvram data, so remove it. DeleteFile()
  // returns true even if the source path is absent, but throws an error that
  // might be confusing. So we check the existence first.
  if (base::PathExists(lockbox_nvram_file_path_) &&
      !brillo::DeleteFile(lockbox_nvram_file_path_)) {
    LOG(ERROR) << "Failed to remove the nvram file!";
  }
  return true;
}

MigrationStatus LockboxCacheManager::MigrateInstallAttributesIfNeeded() {
  if (!base::PathExists(install_attrs_old_path_)) {
    LOG(INFO) << "No legacy install-attributes is found";
    return MigrationStatus::kNotNeeded;
  }
  LOG(INFO) << "Legacy install-attributes found. Attempting migration...";
  if (!base::PathExists(install_attrs_new_path_)) {
    // Make a copy of it to the new location first. The previous
    // install_attributes.pb will still remain at the next reboot if the
    // copy/rename process is interrupted in any way (i.e. unexpected reboot).
    // Thus, it will ultimately get back to this phase and carry on from here.
    if (!base::CreateDirectory(install_attrs_new_path_.DirName())) {
      LOG(ERROR) << "Failed to create "
                 << install_attrs_new_path_.DirName().value();
      return MigrationStatus::kMkdirFail;
    }
    if (!base::CopyFile(install_attrs_old_path_, install_attrs_copy_path_)) {
      LOG(ERROR) << "Failed to create the inter. copy of install-attributes!";
      return MigrationStatus::kCopyFail;
    }
    if (!base::Move(install_attrs_copy_path_, install_attrs_new_path_)) {
      LOG(ERROR) << "Failed to move the install-attributes file!";
      return MigrationStatus::kMoveFail;
    }
  }
  if (!brillo::DeleteFile(install_attrs_old_path_)) {
    LOG(ERROR) << "Failed to remove the old copy of install-attributes";
    return MigrationStatus::kDeleteFail;
  }
  LOG(INFO) << "Install-time attributes content migration successful";
  return MigrationStatus::kSuccess;
}

#if USE_TPM_DYNAMIC

// TODO(b/312392273): call the underlying library directly instead of calling
// the binary
void LockboxCacheManager::PopulateLockboxNvramFile() {
  // Use tpm_manager to read the NV space.
  // Note: tpm_manager should be available at this stage.

  static const char* kNvramIndex;
  TPM_SELECT_BEGIN;
  TPM1_SECTION({ kNvramIndex = "0x20000004"; });
  TPM2_SECTION({ kNvramIndex = "0x9da5b0"; });
  OTHER_TPM_SECTION({
    LOG(ERROR) << "Unsupported TPM platform.";
    kNvramIndex = "0x9da5b0";
  });
  TPM_SELECT_END;

  std::string index = "--index=" + std::string(kNvramIndex);
  std::string file = "--file=" + lockbox_nvram_file_path_.value();
  std::string output;
  if (!platform_->GetAppOutputAndError(
          {"tpm_manager_client", "read_space", index, file}, &output)) {
    LOG(WARNING) << "Failed to read nvram contents from nvram index: "
                 << output;
  }
}

bool LockboxCacheManager::CreateLockboxCache() {
  if (!base::PathExists(lockbox_nvram_file_path_)) {
    LOG(INFO) << "Missing " << lockbox_nvram_file_path_.value()
              << ", may be intended if "
                 "lockbox nvram contents are empty.";
    return true;
  }
  int64_t file_size;
  if (base::GetFileSize(lockbox_nvram_file_path_, &file_size) &&
      file_size > 0) {
    InvokeLockboxCacheTool();
    return true;
  }
  // For TPM-less devices and legacy CR1 devices, pretend like lockbox is
  // supported.
  if (base::PathExists(install_attrs_new_path_)) {
    return base::CopyFile(install_attrs_new_path_, lockbox_cache_path_);
  }
  return true;
}

#else

// no-op
void LockboxCacheManager::PopulateLockboxNvramFile() {}

bool LockboxCacheManager::CreateLockboxCache() {
  if (!base::PathExists(lockbox_nvram_file_path_)) {
    LOG(INFO) << "Missing " << lockbox_nvram_file_path_.value()
              << ", may be intended if "
                 "lockbox nvram contents are empty.";
    return true;
  }
  if (platform_->IsOwnedByRoot(lockbox_nvram_file_path_.value())) {
    InvokeLockboxCacheTool();
    return true;
  }
  LOG(ERROR) << lockbox_nvram_file_path_.value() << " is not owned by root!";
  return false;
}

#endif

}  // namespace cryptohome
