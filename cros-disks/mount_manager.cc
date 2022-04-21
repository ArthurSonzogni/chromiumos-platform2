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

void MountManager::Mount(const std::string& source,
                         const std::string& filesystem_type,
                         std::vector<std::string> options,
                         MountCallback callback) {
  DCHECK(callback);

  // Source is not necessary a path, but if it is let's resolve it to
  // some real underlying object.
  std::string real_path;
  if (Uri::IsUri(source) || !ResolvePath(source, &real_path)) {
    real_path = source;
  }

  if (real_path.empty()) {
    LOG(ERROR) << "Cannot mount an invalid path: " << redact(source);
    std::move(callback).Run("", MOUNT_ERROR_INVALID_ARGUMENT);
    return;
  }

  if (RemoveParamsEqualTo(&options, "remount")) {
    // Remount an already-mounted drive.
    std::string mount_path;
    const MountErrorType error =
        Remount(real_path, filesystem_type, std::move(options), &mount_path);
    return std::move(callback).Run(mount_path, error);
  }

  // Mount a new drive.
  MountNewSource(real_path, filesystem_type, std::move(options),
                 std::move(callback));
}

MountErrorType MountManager::Remount(const std::string& source,
                                     const std::string& /*filesystem_type*/,
                                     std::vector<std::string> options,
                                     std::string* mount_path) {
  MountPoint* const mount_point = FindMountBySource(source);
  if (!mount_point) {
    LOG(WARNING) << "Not currently mounted: " << quote(source);
    return MOUNT_ERROR_PATH_NOT_MOUNTED;
  }

  bool read_only = IsReadOnlyMount(options);

  // Perform the underlying mount operation.
  if (const MountErrorType error = mount_point->Remount(read_only)) {
    LOG(ERROR) << "Cannot remount " << quote(source) << ": " << error;
    return error;
  }

  *mount_path = mount_point->path().value();
  LOG(INFO) << "Remounted " << quote(source) << " on " << quote(*mount_path);
  return MOUNT_ERROR_NONE;
}

void MountManager::MountNewSource(const std::string& source,
                                  const std::string& filesystem_type,
                                  std::vector<std::string> options,
                                  MountCallback callback) {
  DCHECK(callback);

  if (const MountPoint* const mp = FindMountBySource(source)) {
    LOG(ERROR) << redact(source) << " is already mounted on "
               << redact(mp->path());
    return std::move(callback).Run(
        mp->path().value(), GetMountErrorOfReservedMountPath(mp->path()));
  }

  // Extract the mount label string from the passed options.
  std::string label;
  if (const base::StringPiece key = "mountlabel";
      GetParamValue(options, key, &label))
    RemoveParamsWithSameName(&options, key);

  // Create a directory and set up its ownership/permissions for mounting
  // the source path. If an error occurs, ShouldReserveMountPathOnError()
  // is not called to reserve the mount path as a reserved mount path still
  // requires a proper mount directory.
  base::FilePath mount_path;
  if (const MountErrorType error =
          CreateMountPathForSource(source, label, &mount_path))
    return std::move(callback).Run("", error);

  // Perform the underlying mount operation. If an error occurs,
  // ShouldReserveMountPathOnError() is called to check if the mount path
  // should be reserved.
  MountErrorType error = MOUNT_ERROR_UNKNOWN;
  std::unique_ptr<MountPoint> mount_point =
      DoMount(source, filesystem_type, std::move(options), mount_path, &error);

  // Check for both mount_point and error here, since there might be (incorrect)
  // mounters that return no MountPoint and no error (crbug.com/1317877 and
  // crbug.com/1317878).
  if (!mount_point || error) {
    if (!error) {
      LOG(ERROR) << "Mounter for " << redact(source) << " of type "
                 << quote(filesystem_type)
                 << " returned no MountPoint and no error";
      error = MOUNT_ERROR_UNKNOWN;
    } else if (mount_point) {
      LOG(ERROR) << "Mounter for " << redact(source) << " of type "
                 << quote(filesystem_type)
                 << " returned both a mount point and " << error;
      mount_point.reset();
    }

    if (!ShouldReserveMountPathOnError(error)) {
      platform_->RemoveEmptyDirectory(mount_path.value());
      return std::move(callback).Run("", error);
    }

    LOG(INFO) << "Reserving mount path " << quote(mount_path) << " for "
              << quote(source);
    DCHECK(!mount_point);
    ReserveMountPath(mount_path, error);
    // Create dummy mount point to associate with the mount path.
    mount_point = MountPoint::CreateUnmounted(
        {.mount_path = mount_path, .source = source});
  }

  DCHECK(mount_point);
  DCHECK_EQ(mount_point->path(), mount_path);

  // For some mounters, the string stored in |mount_point->source()| is
  // different from |source|.
  // DCHECK_EQ(mount_point->source(), source);
  const auto [it, ok] =
      mount_states_.try_emplace(source, std::move(mount_point));
  DCHECK(ok);
  return std::move(callback).Run(mount_path.value(), error);
}

MountErrorType MountManager::Unmount(const std::string& path) {
  // Look for a matching mount point, either by source path or by mount path.
  MountPoint* const mount_point =
      FindMountBySource(path) ?: FindMountByMountPath(base::FilePath(path));
  if (!mount_point)
    return MOUNT_ERROR_PATH_NOT_MOUNTED;

  MountErrorType error = MOUNT_ERROR_NONE;
  if (IsMountPathReserved(mount_point->path())) {
    LOG(INFO) << "Removing mount path '" << mount_point->path()
              << "' from the reserved list";
    UnreserveMountPath(mount_point->path());
    platform_->RemoveEmptyDirectory(mount_point->path().value());
  } else {
    error = mount_point->Unmount();
    if (error && error != MOUNT_ERROR_PATH_NOT_MOUNTED)
      return error;
  }

  for (auto it = mount_states_.begin(); it != mount_states_.end(); ++it) {
    if (it->second.get() == mount_point) {
      mount_states_.erase(it);
      break;
    }
  }

  return error;
}

bool MountManager::UnmountAll() {
  bool all_umounted = true;

  // Enumerate all the mount paths and then unmount, as calling Unmount()
  // modifies the cache.

  std::vector<std::string> paths;
  paths.reserve(mount_states_.size());
  for (const auto& [source, mount_point] : mount_states_) {
    paths.push_back(source);
  }

  for (const auto& source : paths) {
    if (Unmount(source) != MOUNT_ERROR_NONE) {
      all_umounted = false;
    }
  }

  return all_umounted;
}

bool MountManager::ResolvePath(const std::string& path,
                               std::string* real_path) {
  return platform_->GetRealPath(path, real_path);
}

MountPoint* MountManager::FindMountBySource(const std::string& source) const {
  const auto it = mount_states_.find(source);
  if (it == mount_states_.end())
    return nullptr;
  return it->second.get();
}

MountPoint* MountManager::FindMountByMountPath(
    const base::FilePath& path) const {
  for (const auto& [source, mount_point] : mount_states_) {
    DCHECK(mount_point);
    if (mount_point->path() == path)
      return mount_point.get();
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
  DCHECK(mount_path);
  DCHECK(mount_path->empty());

  *mount_path = base::FilePath(SuggestMountPath(source));
  if (!label.empty()) {
    // Replace the basename(|actual_mount_path|) with |label|.
    *mount_path = mount_path->DirName().Append(label);
  }

  if (!IsValidMountPath(*mount_path)) {
    LOG(ERROR) << "Mount path " << quote(*mount_path) << " is invalid";
    return MOUNT_ERROR_INVALID_PATH;
  }

  std::unordered_set<std::string> reserved_paths;
  for (const auto& entry : reserved_mount_paths_) {
    reserved_paths.insert(entry.first.value());
  }

  std::string path = mount_path->value();
  if (!platform_->CreateOrReuseEmptyDirectoryWithFallback(
          &path, kMaxNumMountTrials, reserved_paths)) {
    LOG(ERROR) << "Cannot create directory " << quote(*mount_path)
               << " to mount " << quote(source);
    return MOUNT_ERROR_DIRECTORY_CREATION_FAILED;
  }

  *mount_path = base::FilePath(path);
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
                                    MountErrorType error) {
  const auto [it, ok] =
      reserved_mount_paths_.try_emplace(std::move(mount_path), error);
  LOG_IF(WARNING, !ok && it->second != error)
      << "Cannot update error associated to reserved mount path "
      << redact(mount_path) << " from " << it->second << " to " << error;
}

void MountManager::UnreserveMountPath(const base::FilePath& mount_path) {
  reserved_mount_paths_.erase(mount_path);
}

std::vector<MountEntry> MountManager::GetMountEntries() const {
  std::vector<MountEntry> mount_entries;
  mount_entries.reserve(mount_states_.size());
  for (const auto& [source, mount_point] : mount_states_) {
    DCHECK(mount_point);
    mount_entries.push_back(
        {GetMountErrorOfReservedMountPath(mount_point->path()), source,
         GetMountSourceType(), mount_point->path().value(),
         mount_point->is_read_only()});
  }
  return mount_entries;
}

bool MountManager::ShouldReserveMountPathOnError(MountErrorType error) const {
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
