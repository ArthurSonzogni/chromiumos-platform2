// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Implements cros-disks::MountManager. See mount-manager.h for details.

#include "cros-disks/mount_manager.h"

#include <sys/mount.h>
#include <unistd.h>

#include <algorithm>
#include <unordered_set>
#include <utility>

#include <base/check.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/containers/contains.h>
#include <base/strings/string_util.h>

#include "cros-disks/error_logger.h"
#include "cros-disks/mount_options.h"
#include "cros-disks/mounter.h"
#include "cros-disks/platform.h"
#include "cros-disks/quote.h"
#include "cros-disks/uri.h"

namespace cros_disks {
namespace {

// Permissions to set on the mount root directory (u+rwx,og+rx).
const mode_t kMountRootDirectoryPermissions =
    S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
// Prefix of the mount label option.
const char kMountOptionMountLabelPrefix[] = "mountlabel";
// Literal for mount option: "remount".
const char kMountOptionRemount[] = "remount";
// Maximum number of trials on creating a mount directory using
// Platform::CreateOrReuseEmptyDirectoryWithFallback().
// A value of 100 seems reasonable and enough to handle directory name
// collisions under common scenarios.
const unsigned kMaxNumMountTrials = 100;

}  // namespace

MountManager::MountManager(const std::string& mount_root,
                           Platform* platform,
                           Metrics* metrics,
                           brillo::ProcessReaper* process_reaper)
    : mount_root_(base::FilePath(mount_root)),
      platform_(platform),
      metrics_(metrics),
      process_reaper_(process_reaper) {
  CHECK(!mount_root_.empty()) << "Invalid mount root directory";
  CHECK(mount_root_.IsAbsolute()) << "Mount root not absolute path";
  CHECK(platform_) << "Invalid platform object";
  CHECK(metrics_) << "Invalid metrics object";
}

MountManager::~MountManager() {
  // UnmountAll() should be called from a derived class instead of this base
  // class as UnmountAll() calls MountPoint::Unmount() which may call back into
  // a derived class.
}

bool MountManager::Initialize() {
  return platform_->CreateDirectory(mount_root_.value()) &&
         platform_->SetOwnership(mount_root_.value(), getuid(), getgid()) &&
         platform_->SetPermissions(mount_root_.value(),
                                   kMountRootDirectoryPermissions);
}

bool MountManager::StartSession() {
  return true;
}

bool MountManager::StopSession() {
  return UnmountAll();
}

MountErrorType MountManager::Mount(const std::string& source_path,
                                   const std::string& filesystem_type,
                                   std::vector<std::string> options,
                                   std::string* mount_path) {
  // Source is not necessary a path, but if it is let's resolve it to
  // some real underlying object.
  std::string real_path;
  if (Uri::IsUri(source_path) || !ResolvePath(source_path, &real_path)) {
    real_path = source_path;
  }

  if (real_path.empty()) {
    LOG(ERROR) << "Failed to mount an invalid path";
    return MOUNT_ERROR_INVALID_ARGUMENT;
  }
  if (!mount_path) {
    LOG(ERROR) << "Invalid mount path argument";
    return MOUNT_ERROR_INVALID_ARGUMENT;
  }

  if (RemoveParamsEqualTo(&options, kMountOptionRemount) == 0) {
    return MountNewSource(real_path, filesystem_type, std::move(options),
                          mount_path);
  } else {
    return Remount(real_path, filesystem_type, std::move(options), mount_path);
  }
}

MountErrorType MountManager::Remount(const std::string& source_path,
                                     const std::string& /*filesystem_type*/,
                                     std::vector<std::string> options,
                                     std::string* mount_path) {
  MountPoint* mount_point = FindMountBySource(source_path);
  if (!mount_point) {
    LOG(WARNING) << "Path " << quote(source_path) << " is not mounted yet";
    return MOUNT_ERROR_PATH_NOT_MOUNTED;
  }

  bool read_only = IsReadOnlyMount(options);

  // Perform the underlying mount operation.
  MountErrorType error_type = mount_point->Remount(read_only);
  if (error_type != MOUNT_ERROR_NONE) {
    LOG(ERROR) << "Cannot remount path " << quote(source_path) << ": "
               << error_type;
    return error_type;
  }

  *mount_path = mount_point->path().value();
  LOG(INFO) << "Path " << quote(source_path) << " on " << quote(*mount_path)
            << " is remounted";
  return error_type;
}

MountErrorType MountManager::MountNewSource(const std::string& source_path,
                                            const std::string& filesystem_type,
                                            std::vector<std::string> options,
                                            std::string* mount_path) {
  MountPoint* mp = FindMountBySource(source_path);
  if (mp) {
    // TODO(dats): Some obscure legacy. Why is this even needed?
    if (mount_path->empty() || mp->path().value() == *mount_path) {
      LOG(WARNING) << "Source " << redact(source_path)
                   << " is already mounted to " << redact(mp->path());
      *mount_path = mp->path().value();
      return GetMountErrorOfReservedMountPath(mp->path());
    }
    LOG(ERROR) << "Source " << redact(source_path) << " is already mounted to "
               << redact(mp->path());
    return MOUNT_ERROR_PATH_ALREADY_MOUNTED;
  }

  std::string mount_label;
  if (GetParamValue(options, kMountOptionMountLabelPrefix, &mount_label)) {
    RemoveParamsWithSameName(&options, kMountOptionMountLabelPrefix);
  }

  // Create a directory and set up its ownership/permissions for mounting
  // the source path. If an error occurs, ShouldReserveMountPathOnError()
  // is not called to reserve the mount path as a reserved mount path still
  // requires a proper mount directory.
  base::FilePath actual_mount_path(*mount_path);
  MountErrorType error =
      CreateMountPathForSource(source_path, mount_label, &actual_mount_path);
  if (error != MOUNT_ERROR_NONE) {
    return error;
  }

  // Perform the underlying mount operation. If an error occurs,
  // ShouldReserveMountPathOnError() is called to check if the mount path
  // should be reserved.
  MountErrorType error_type = MOUNT_ERROR_UNKNOWN;
  std::unique_ptr<MountPoint> mount_point =
      DoMount(source_path, filesystem_type, std::move(options),
              base::FilePath(actual_mount_path), &error_type);
  if (error_type == MOUNT_ERROR_NONE) {
    LOG(INFO) << "Path " << quote(source_path) << " is mounted to "
              << quote(actual_mount_path);
    DCHECK(mount_point);
  } else if (ShouldReserveMountPathOnError(error_type)) {
    LOG(INFO) << "Reserving mount path " << quote(actual_mount_path) << " for "
              << quote(source_path);
    DCHECK(!mount_point);
    ReserveMountPath(actual_mount_path, error_type);
    // Create dummy mount point to associate with the mount path.
    mount_point = MountPoint::CreateLeaking(base::FilePath(actual_mount_path));
  } else {
    LOG(ERROR) << "Cannot mount " << redact(source_path) << " of type "
               << quote(filesystem_type) << ": " << error_type;
    platform_->RemoveEmptyDirectory(actual_mount_path.value());
    return error_type;
  }

  mount_states_.insert({source_path, std::move(mount_point)});
  *mount_path = actual_mount_path.value();
  return error_type;
}

MountErrorType MountManager::Unmount(const std::string& path) {
  // Determine whether the path is a source path or a mount path.
  // Is path a source path?
  MountPoint* mount_point = FindMountBySource(path);
  if (!mount_point) {
    // Not a source path. Is path a mount path?
    mount_point = FindMountByMountPath(base::FilePath(path));
    if (!mount_point) {
      // Not a mount path either.
      return MOUNT_ERROR_PATH_NOT_MOUNTED;
    }
  }

  MountErrorType error_type = MOUNT_ERROR_NONE;
  if (IsMountPathReserved(mount_point->path())) {
    LOG(INFO) << "Removing mount path '" << mount_point->path()
              << "' from the reserved list";
    UnreserveMountPath(mount_point->path());
  } else {
    error_type = mount_point->Unmount();

    switch (error_type) {
      case MOUNT_ERROR_NONE:
        LOG(INFO) << "Unmounted " << quote(mount_point->path());
        break;

      case MOUNT_ERROR_PATH_NOT_MOUNTED:
        LOG(WARNING) << "Not mounted " << quote(mount_point->path());
        break;

      default:
        LOG(ERROR) << "Cannot unmount " << quote(mount_point->path()) << ": "
                   << error_type;
        return error_type;
    }
  }

  platform_->RemoveEmptyDirectory(mount_point->path().value());
  for (auto it = mount_states_.begin(); it != mount_states_.end(); ++it) {
    if (it->second.get() == mount_point) {
      mount_states_.erase(it);
      break;
    }
  }
  return error_type;
}

bool MountManager::UnmountAll() {
  bool all_umounted = true;

  // Enumerate all the mount paths and then unmount, as calling Unmount()
  // modifies the cache.

  std::vector<std::string> paths;
  paths.reserve(mount_states_.size());
  for (const auto& entry : mount_states_) {
    paths.push_back(entry.second->path().value());
  }

  for (const auto& source_path : paths) {
    if (Unmount(source_path) != MOUNT_ERROR_NONE) {
      all_umounted = false;
    }
  }

  return all_umounted;
}

bool MountManager::ResolvePath(const std::string& path,
                               std::string* real_path) {
  return platform_->GetRealPath(path, real_path);
}

MountPoint* MountManager::FindMountBySource(const std::string& source) {
  const auto it = mount_states_.find(source);
  if (it == mount_states_.end())
    return nullptr;
  return it->second.get();
}

MountPoint* MountManager::FindMountByMountPath(const base::FilePath& path) {
  for (auto& entry : mount_states_) {
    if (entry.second->path() == path)
      return entry.second.get();
  }
  return nullptr;
}

bool MountManager::RemoveMount(MountPoint* mount_point) {
  for (auto it = mount_states_.begin(); it != mount_states_.end(); ++it) {
    if (it->second.get() == mount_point) {
      mount_states_.erase(it);
      return true;
    }
  }
  return false;
}

MountErrorType MountManager::CreateMountPathForSource(
    const std::string& source,
    const std::string& label,
    base::FilePath* mount_path) {
  base::FilePath actual_mount_path = *mount_path;
  if (actual_mount_path.empty()) {
    actual_mount_path = base::FilePath(SuggestMountPath(source));
    if (!label.empty()) {
      // Replace the basename(|actual_mount_path|) with |label|.
      actual_mount_path = actual_mount_path.DirName().Append(label);
    }
  }

  if (!IsValidMountPath(base::FilePath(actual_mount_path))) {
    LOG(ERROR) << "Mount path " << quote(actual_mount_path) << " is invalid";
    return MOUNT_ERROR_INVALID_PATH;
  }

  bool mount_path_created;
  if (!mount_path->empty()) {
    mount_path_created =
        !IsMountPathReserved(actual_mount_path) &&
        platform_->CreateOrReuseEmptyDirectory(actual_mount_path.value());
  } else {
    std::unordered_set<std::string> reserved_paths;
    for (const auto& entry : reserved_mount_paths_) {
      reserved_paths.insert(entry.first.value());
    }
    std::string path = actual_mount_path.value();
    mount_path_created = platform_->CreateOrReuseEmptyDirectoryWithFallback(
        &path, kMaxNumMountTrials, reserved_paths);
    if (mount_path_created)
      actual_mount_path = base::FilePath(path);
  }
  if (!mount_path_created) {
    LOG(ERROR) << "Cannot create directory " << quote(actual_mount_path)
               << " to mount " << quote(source);
    return MOUNT_ERROR_DIRECTORY_CREATION_FAILED;
  }

  *mount_path = actual_mount_path;
  return MOUNT_ERROR_NONE;
}

bool MountManager::IsMountPathReserved(const base::FilePath& mount_path) const {
  return base::Contains(reserved_mount_paths_, mount_path);
}

MountErrorType MountManager::GetMountErrorOfReservedMountPath(
    const base::FilePath& mount_path) const {
  const auto it = reserved_mount_paths_.find(mount_path);
  return it != reserved_mount_paths_.end() ? it->second : MOUNT_ERROR_NONE;
}

void MountManager::ReserveMountPath(base::FilePath mount_path,
                                    MountErrorType error_type) {
  reserved_mount_paths_.insert({std::move(mount_path), error_type});
}

void MountManager::UnreserveMountPath(const base::FilePath& mount_path) {
  reserved_mount_paths_.erase(mount_path);
}

std::vector<MountEntry> MountManager::GetMountEntries() const {
  std::vector<MountEntry> mount_entries;
  mount_entries.reserve(mount_states_.size());
  for (const auto& entry : mount_states_) {
    const std::string& source_path = entry.first;
    const MountPoint& mount_point = *entry.second;

    mount_entries.push_back(
        {GetMountErrorOfReservedMountPath(mount_point.path()), source_path,
         GetMountSourceType(), mount_point.path().value(),
         mount_point.is_read_only()});
  }
  return mount_entries;
}

bool MountManager::ShouldReserveMountPathOnError(
    MountErrorType error_type) const {
  return false;
}

bool MountManager::IsPathImmediateChildOfParent(const base::FilePath& path,
                                                const base::FilePath& parent) {
  std::vector<std::string> path_components, parent_components;
  path.StripTrailingSeparators().GetComponents(&path_components);
  parent.StripTrailingSeparators().GetComponents(&parent_components);
  if (path_components.size() != parent_components.size() + 1)
    return false;

  if (path_components.back() == base::FilePath::kCurrentDirectory ||
      path_components.back() == base::FilePath::kParentDirectory) {
    return false;
  }

  return std::equal(parent_components.begin(), parent_components.end(),
                    path_components.begin());
}

bool MountManager::IsValidMountPath(const base::FilePath& mount_path) const {
  return IsPathImmediateChildOfParent(mount_path, mount_root_);
}

}  // namespace cros_disks
