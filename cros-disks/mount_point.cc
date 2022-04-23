// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/mount_point.h"

#include <utility>

#include <base/check.h>
#include <base/containers/contains.h>
#include <base/logging.h>

#include "cros-disks/error_logger.h"
#include "cros-disks/mounter.h"
#include "cros-disks/quote.h"

namespace cros_disks {

std::unique_ptr<MountPoint> MountPoint::CreateUnmounted(
    MountPointData data, const Platform* const platform) {
  std::unique_ptr<MountPoint> mount_point =
      std::make_unique<MountPoint>(std::move(data), platform);
  mount_point->is_mounted_ = false;
  return mount_point;
}

std::unique_ptr<MountPoint> MountPoint::Mount(MountPointData data,
                                              const Platform* const platform,
                                              MountErrorType* const error) {
  DCHECK(error);
  *error = platform->Mount(data.source, data.mount_path.value(),
                           data.filesystem_type, data.flags, data.data);

  if (*error != MOUNT_ERROR_NONE) {
    return nullptr;
  }

  return std::make_unique<MountPoint>(std::move(data), platform);
}

MountPoint::MountPoint(MountPointData data, const Platform* platform)
    : data_(std::move(data)), platform_(platform) {
  DCHECK(!path().empty());
}

MountPoint::~MountPoint() {
  Unmount();
}

MountErrorType MountPoint::Unmount() {
  MountErrorType error = MOUNT_ERROR_PATH_NOT_MOUNTED;

  if (is_mounted_) {
    error = UnmountImpl();
    if (error == MOUNT_ERROR_NONE || error == MOUNT_ERROR_PATH_NOT_MOUNTED) {
      is_mounted_ = false;

      if (eject_)
        std::move(eject_).Run();
    }
  }

  process_.reset();

  if (launcher_exit_callback_) {
    DCHECK_EQ(MOUNT_ERROR_IN_PROGRESS, data_.error);
    data_.error = MOUNT_ERROR_CANCELLED;
    std::move(launcher_exit_callback_).Run(MOUNT_ERROR_CANCELLED);
  }

  if (!is_mounted_ && must_remove_dir_ &&
      platform_->RemoveEmptyDirectory(data_.mount_path.value())) {
    LOG(INFO) << "Removed " << quote(data_.mount_path);
    must_remove_dir_ = false;
  }

  return error;
}

MountErrorType MountPoint::Remount(bool read_only) {
  if (!is_mounted_)
    return MOUNT_ERROR_PATH_NOT_MOUNTED;

  int flags = data_.flags;
  if (read_only) {
    flags |= MS_RDONLY;
  } else {
    flags &= ~MS_RDONLY;
  }

  const MountErrorType error = RemountImpl(flags);
  if (!error)
    data_.flags = flags;

  return error;
}

MountErrorType MountPoint::UnmountImpl() {
  // We take a 2-step approach to unmounting FUSE filesystems. First, try a
  // normal unmount. This lets the VFS flush any pending data and lets the
  // filesystem shut down cleanly. If the filesystem is busy, force unmount
  // the filesystem. This is done because there is no good recovery path the
  // user can take, and these filesystem are sometimes unmounted implicitly on
  // login/logout/suspend.

  const base::FilePath& mount_path = data_.mount_path;
  if (const MountErrorType error = platform_->Unmount(mount_path.value(), 0);
      error != MOUNT_ERROR_PATH_ALREADY_MOUNTED)
    return error;

  // The mount point couldn't be unmounted because it is BUSY.
  LOG(INFO) << "Forcefully unmounting " << quote(mount_path) << "...";

  // For FUSE filesystems, MNT_FORCE will cause the kernel driver to immediately
  // close the channel to the user-space driver program and cancel all
  // outstanding requests. However, if any program is still accessing the
  // filesystem, the umount2() will fail with EBUSY and the mountpoint will
  // still be attached. Since the mountpoint is no longer valid, use MNT_DETACH
  // to also force the mountpoint to be disconnected. On a non-FUSE filesystem
  // MNT_FORCE doesn't have effect, so it only handles MNT_DETACH, but it's OK
  // to pass MNT_FORCE too.
  return platform_->Unmount(mount_path.value(), MNT_FORCE | MNT_DETACH);
}

MountErrorType MountPoint::RemountImpl(int flags) {
  return platform_->Mount(data_.source, data_.mount_path.value(),
                          data_.filesystem_type, flags | MS_REMOUNT,
                          data_.data);
}

MountErrorType MountPoint::ConvertLauncherExitCodeToMountError(
    const int exit_code) const {
  if (exit_code == 0)
    return MOUNT_ERROR_NONE;

  if (base::Contains(password_needed_exit_codes_, exit_code))
    return MOUNT_ERROR_NEED_PASSWORD;

  return MOUNT_ERROR_MOUNT_PROGRAM_FAILED;
}

void MountPoint::OnLauncherExit(const int exit_code) {
  // Record the FUSE launcher's exit code in Metrics.
  if (metrics_ && !metrics_name_.empty())
    metrics_->RecordFuseMounterErrorCode(metrics_name_, exit_code);

  DCHECK_EQ(MOUNT_ERROR_IN_PROGRESS, data_.error);
  data_.error = ConvertLauncherExitCodeToMountError(exit_code);
  DCHECK_NE(MOUNT_ERROR_IN_PROGRESS, data_.error);

  if (launcher_exit_callback_)
    std::move(launcher_exit_callback_).Run(data_.error);
}

void MountPoint::SetProcess(std::unique_ptr<Process> process,
                            Metrics* const metrics,
                            std::string metrics_name,
                            std::vector<int> password_needed_exit_codes) {
  DCHECK(!process_);
  process_ = std::move(process);
  DCHECK(process_);

  DCHECK(!metrics_);
  metrics_ = metrics;
  DCHECK(metrics_name_.empty());
  metrics_name_ = std::move(metrics_name);

  password_needed_exit_codes_ = std::move(password_needed_exit_codes);

  DCHECK_EQ(MOUNT_ERROR_NONE, data_.error);
  data_.error = MOUNT_ERROR_IN_PROGRESS;

  process_->SetLauncherExitCallback(
      base::BindOnce(&MountPoint::OnLauncherExit, GetWeakPtr()));
}

}  // namespace cros_disks
