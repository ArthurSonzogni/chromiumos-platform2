// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/mount_point.h"

#include <utility>

#include <base/check.h>
#include <base/logging.h>

#include "cros-disks/error_logger.h"
#include "cros-disks/mounter.h"
#include "cros-disks/quote.h"

namespace cros_disks {

// static
std::unique_ptr<MountPoint> MountPoint::CreateLeaking(
    const base::FilePath& path) {
  auto mount_point =
      std::make_unique<MountPoint>(MountPointData{path}, nullptr);
  mount_point->Release();
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

void MountPoint::Release() {
  released_ = true;
}

MountErrorType MountPoint::Unmount() {
  if (released_)
    return MOUNT_ERROR_PATH_NOT_MOUNTED;

  const MountErrorType error = UnmountImpl();
  if (!error || error == MOUNT_ERROR_PATH_NOT_MOUNTED) {
    released_ = true;

    if (eject_)
      std::move(eject_).Run();

    platform_->RemoveEmptyDirectory(data_.mount_path.value());
  }

  return error;
}

MountErrorType MountPoint::Remount(bool read_only) {
  if (released_)
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

}  // namespace cros_disks
