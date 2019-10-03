// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/fuse_mount_manager.h"

#include <sys/mount.h>

#include <utility>

#include <base/files/file_path.h>
#include <base/logging.h>
#include <brillo/process_reaper.h>

#include "cros-disks/drivefs_helper.h"
#include "cros-disks/fuse_helper.h"
#include "cros-disks/fuse_mounter.h"
#include "cros-disks/platform.h"
#include "cros-disks/quote.h"
#include "cros-disks/sshfs_helper.h"
#include "cros-disks/uri.h"

namespace cros_disks {

FUSEMountManager::FUSEMountManager(const std::string& mount_root,
                                   const std::string& working_dirs_root,
                                   Platform* platform,
                                   Metrics* metrics,
                                   brillo::ProcessReaper* process_reaper)
    : MountManager(mount_root, platform, metrics, process_reaper),
      working_dirs_root_(working_dirs_root) {}

FUSEMountManager::~FUSEMountManager() {
  UnmountAll();
}

bool FUSEMountManager::Initialize() {
  if (!MountManager::Initialize())
    return false;

  if (!platform()->DirectoryExists(working_dirs_root_) &&
      !platform()->CreateDirectory(working_dirs_root_)) {
    LOG(ERROR) << "Can't create writable FUSE directory";
    return false;
  }
  if (!platform()->SetOwnership(working_dirs_root_, getuid(), getgid()) ||
      !platform()->SetPermissions(working_dirs_root_, 0755)) {
    LOG(ERROR) << "Can't set up writable FUSE directory";
    return false;
  }

  // Register specific FUSE mount helpers here.
  RegisterHelper(std::make_unique<DrivefsHelper>(platform(), process_reaper()));
  RegisterHelper(std::make_unique<SshfsHelper>(platform(), process_reaper()));

  return true;
}

MountErrorType FUSEMountManager::DoMount(
    const std::string& source,
    const std::string& fuse_type,
    const std::vector<std::string>& options,
    const std::string& mount_path,
    MountOptions* applied_options) {
  CHECK(!mount_path.empty()) << "Invalid mount path argument";
  CHECK(Uri::IsUri(source)) << "Source argument is not URI";

  Uri uri = Uri::Parse(source);
  const FUSEHelper* selected_helper = nullptr;
  for (const auto& helper : helpers_) {
    if (helper->CanMount(uri)) {
      selected_helper = helper.get();
      break;
    }
  }

  if (!selected_helper) {
    LOG(ERROR) << "Cannot find suitable FUSE module for type "
               << quote(fuse_type) << " and source " << quote(source);
    return MOUNT_ERROR_UNKNOWN_FILESYSTEM;
  }

  // Make a temporary dir where the helper may keep stuff needed by the mounter
  // process.
  std::string path;
  if (!platform()->CreateTemporaryDirInDir(working_dirs_root_, ".", &path) ||
      !platform()->SetPermissions(path, 0755)) {
    LOG(ERROR) << "Cannot create working directory for FUSE module "
               << quote(selected_helper->type());
    return MOUNT_ERROR_DIRECTORY_CREATION_FAILED;
  }

  auto mounter = selected_helper->CreateMounter(
      base::FilePath(path), uri, base::FilePath(mount_path), options);
  if (!mounter) {
    LOG(ERROR) << "Invalid options for FUSE module "
               << quote(selected_helper->type()) << " and source "
               << quote(source);
    return MOUNT_ERROR_INVALID_MOUNT_OPTIONS;
  }

  return mounter->Mount();
}

MountErrorType FUSEMountManager::DoUnmount(
    const std::string& path, const std::vector<std::string>& options) {
  // DoUnmount() is always called with |path| being the mount path.
  CHECK(!path.empty()) << "Invalid path argument";

  LOG_IF(WARNING, !options.empty()) << "Ignoring non-empty unmount options";

  // We take a 2-step approach to unmounting network FUSE filesystems. First,
  // try a normal unmount. This lets the VFS flush any pending data and lets the
  // filesystem shut down cleanly. If the filesystem is busy, force unmount the
  // filesystem. This is done because there is no recovery path the user can
  // take, and these filesystem are generally mounted and unmounted implicitly
  // on login/logout/suspend. This action is similar to disk-based filesystems
  // which are lazy unmounted if a regular unmount fails because the filesystem
  // is busy.

  MountErrorType error = platform()->Unmount(path, 0 /* flags */);
  if (error != MOUNT_ERROR_PATH_ALREADY_MOUNTED) {
    // MOUNT_ERROR_PATH_ALREADY_MOUNTED is returned on EBUSY.
    return error;
  }

  // For FUSE filesystems, MNT_FORCE will cause the kernel driver to immediately
  // close the channel to the user-space driver program and cancel all
  // outstanding requests. However, if any program is still accessing the
  // filesystem, the umount2() will fail with EBUSY and the mountpoint will
  // still be attached. Since the mountpoint is no longer valid, use MNT_DETACH
  // to also force the mountpoint to be disconnected.
  LOG(WARNING) << "Mount point " << quote(path)
               << " is busy, using force unmount";
  return platform()->Unmount(path, MNT_FORCE | MNT_DETACH);
}

bool FUSEMountManager::CanMount(const std::string& source) const {
  if (!Uri::IsUri(source)) {
    return false;
  }
  Uri uri = Uri::Parse(source);
  for (const auto& helper : helpers_) {
    if (helper->CanMount(uri))
      return true;
  }
  return false;
}

std::string FUSEMountManager::SuggestMountPath(
    const std::string& source) const {
  if (!Uri::IsUri(source)) {
    return "";
  }
  Uri uri = Uri::Parse(source);
  for (const auto& helper : helpers_) {
    if (helper->CanMount(uri))
      return base::FilePath(mount_root())
          .Append(helper->GetTargetSuffix(uri))
          .value();
  }
  base::FilePath base_name = base::FilePath(source).BaseName();
  return base::FilePath(mount_root()).Append(base_name).value();
}

void FUSEMountManager::RegisterHelper(std::unique_ptr<FUSEHelper> helper) {
  helpers_.push_back(std::move(helper));
}

}  // namespace cros_disks
