// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/storage/arc_disk_quota.h"

#include <optional>

#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <brillo/cryptohome.h>

#include "cryptohome/projectid_config.h"

namespace cryptohome {

namespace {

bool IsAndroidProjectId(int project_id) {
  return (project_id >= kProjectIdForAndroidFilesStart &&
          project_id <= kProjectIdForAndroidFilesEnd) ||
         (project_id >= kProjectIdForAndroidAppsStart &&
          project_id <= kProjectIdForAndroidAppsEnd);
}

}  // namespace

ArcDiskQuota::ArcDiskQuota(HomeDirs* homedirs,
                           Platform* platform,
                           const base::FilePath& home)
    : homedirs_(homedirs), platform_(platform), home_(home) {}

ArcDiskQuota::~ArcDiskQuota() = default;

void ArcDiskQuota::Initialize() {
  device_ = GetDevice();
}

bool ArcDiskQuota::IsQuotaSupported() const {
  if (device_.empty()) {
    LOG(ERROR) << "No quota mount is found.";
    return false;
  }

  int cnt = homedirs_->GetUnmountedAndroidDataCount();
  if (cnt != 0) {
    // Quota is not supported if there are one or more unmounted Android users.
    // (b/181159107)
    return false;
  }

  return true;
}

int64_t ArcDiskQuota::GetCurrentSpaceForUid(uid_t android_uid) const {
  if (android_uid < kAndroidUidStart || android_uid > kAndroidUidEnd) {
    LOG(ERROR) << "Android uid " << android_uid
               << " is outside the allowed query range";
    return -1;
  }
  if (device_.empty()) {
    LOG(ERROR) << "No quota mount is found";
    return -1;
  }
  uid_t real_uid = android_uid + kArcContainerShiftUid;
  int64_t current_space =
      platform_->GetQuotaCurrentSpaceForUid(device_, real_uid);
  if (current_space == -1) {
    PLOG(ERROR) << "Failed to get disk stats for uid: " << real_uid;
    return -1;
  }
  return current_space;
}

int64_t ArcDiskQuota::GetCurrentSpaceForGid(gid_t android_gid) const {
  if (android_gid < kAndroidGidStart || android_gid > kAndroidGidEnd) {
    LOG(ERROR) << "Android gid " << android_gid
               << " is outside the allowed query range";
    return -1;
  }
  if (device_.empty()) {
    LOG(ERROR) << "No quota mount is found";
    return -1;
  }
  gid_t real_gid = android_gid + kArcContainerShiftGid;
  int64_t current_space =
      platform_->GetQuotaCurrentSpaceForGid(device_, real_gid);
  if (current_space == -1) {
    PLOG(ERROR) << "Failed to get disk stats for gid: " << real_gid;
    return -1;
  }
  return current_space;
}

int64_t ArcDiskQuota::GetCurrentSpaceForProjectId(int project_id) const {
  if (!IsAndroidProjectId(project_id)) {
    LOG(ERROR) << "Project id " << project_id
               << " is outside the allowed query range";
    return -1;
  }
  if (device_.empty()) {
    LOG(ERROR) << "No quota mount is found";
    return -1;
  }
  int64_t current_space =
      platform_->GetQuotaCurrentSpaceForProjectId(device_, project_id);
  if (current_space == -1) {
    PLOG(ERROR) << "Failed to get disk stats for project id: " << project_id;
    return -1;
  }
  return current_space;
}

bool ArcDiskQuota::SetProjectId(int project_id,
                                SetProjectIdAllowedPathType parent_path,
                                const base::FilePath& child_path,
                                const std::string& obfuscated_username) const {
  if (!IsAndroidProjectId(project_id)) {
    LOG(ERROR) << "Project id " << project_id
               << " is outside the allowed query range";
    return false;
  }

  if (child_path.ReferencesParent()) {
    LOG(ERROR) << "child_path contains \"..\" : " << child_path;
    return false;
  }

  if (child_path.IsAbsolute()) {
    LOG(ERROR) << "child_path is an absolute path : " << child_path;
    return false;
  }

  MountError mount_error;
  if (!homedirs_->CryptohomeExists(obfuscated_username, &mount_error)) {
    if (mount_error != MOUNT_ERROR_NONE) {
      LOG(ERROR) << "Failed to check cryptohome existence for : "
                 << obfuscated_username << " error = " << mount_error;
    } else {
      LOG(ERROR) << "A cryptohome vault doesn't exist for : "
                 << obfuscated_username;
    }
    return false;
  }

  base::FilePath path;
  switch (parent_path) {
    case PATH_DOWNLOADS:
      // /home/user/<obfuscated_username>/Downloads/<child_path>
      path = brillo::cryptohome::home::GetUserPathPrefix()
                 .Append(obfuscated_username)
                 .Append(kUserDownloadsDir)
                 .Append(child_path);
      break;
    case PATH_ANDROID_DATA:
      // /home/root/<obfuscated_username>/android-data/<child_path>
      path = brillo::cryptohome::home::GetRootPathPrefix()
                 .Append(obfuscated_username)
                 .Append(kAndroidDataDir)
                 .Append(child_path);
      break;
  }

  if (path.empty()) {
    LOG(ERROR) << "Invalid parent path type : " << parent_path;
    return false;
  }

  return platform_->SetQuotaProjectId(project_id, path);
}

bool ArcDiskQuota::SetMediaRWDataFileProjectId(int project_id,
                                               int fd,
                                               int* out_error) const {
  if (!IsAndroidProjectId(project_id)) {
    LOG(ERROR) << "Project id " << project_id
               << " is outside the allowed query range";
    *out_error = EINVAL;
    return false;
  }
  std::optional<std::string> context = platform_->GetSELinuxContextOfFD(fd);
  if (!context) {
    LOG(ERROR) << "Failed to get the SELinux context of FD.";
    *out_error = EIO;
    return false;
  }
  if (*context != kMediaRWDataFileSELinuxContext) {
    LOG(ERROR) << "Unexpected SELinux context: " << *context;
    *out_error = EPERM;
    return false;
  }
  return platform_->SetQuotaProjectIdWithFd(project_id, fd, out_error);
}

base::FilePath ArcDiskQuota::GetDevice() {
  std::string device;
  if (!platform_->FindFilesystemDevice(home_, &device)) {
    LOG(ERROR) << "Home device is not found.";
    return base::FilePath();
  }

  // Check if the device is mounted with quota option.
  if (platform_->GetQuotaCurrentSpaceForUid(base::FilePath(device), 0) < 0) {
    LOG(ERROR) << "Device is not mounted with quota feature enabled.";
    return base::FilePath();
  }

  return base::FilePath(device);
}

}  // namespace cryptohome
