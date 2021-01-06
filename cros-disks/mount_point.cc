// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/mount_point.h"

#include <utility>

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

// static
std::unique_ptr<MountPoint> MountPoint::Mount(MountPointData data,
                                              const Platform* platform,
                                              MountErrorType* error) {
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
  if (!released_) {
    MountErrorType error = Unmount();
    if (error != MOUNT_ERROR_NONE && error != MOUNT_ERROR_PATH_NOT_MOUNTED) {
      LOG(WARNING) << "Unmount error while destroying MountPoint("
                   << quote(path()) << "): " << error;
    }
  }
}

void MountPoint::Release() {
  released_ = true;
}

MountErrorType MountPoint::Unmount() {
  if (released_) {
    // TODO(amistry): "not mounted" and "already unmounted" are logically
    // different and should be treated differently. Introduce a new error code
    // to represent this distinction.
    return MOUNT_ERROR_PATH_NOT_MOUNTED;
  }
  MountErrorType error = UnmountImpl();
  if (error == MOUNT_ERROR_NONE || error == MOUNT_ERROR_PATH_NOT_MOUNTED) {
    released_ = true;
  }
  return error;
}

MountErrorType MountPoint::Remount(bool read_only) {
  if (released_) {
    return MOUNT_ERROR_PATH_NOT_MOUNTED;
  }
  int new_flags = (data_.flags & ~MS_RDONLY) | (read_only ? MS_RDONLY : 0);
  MountErrorType error = RemountImpl(new_flags);
  if (error == MOUNT_ERROR_NONE) {
    data_.flags = new_flags;
  }
  return error;
}

MountErrorType MountPoint::UnmountImpl() {
  // We take a 2-step approach to unmounting FUSE filesystems. First, try a
  // normal unmount. This lets the VFS flush any pending data and lets the
  // filesystem shut down cleanly. If the filesystem is busy, force unmount
  // the filesystem. This is done because there is no good recovery path the
  // user can take, and these filesystem are sometimes unmounted implicitly on
  // login/logout/suspend.

  MountErrorType error = platform_->Unmount(path().value(), 0 /* flags */);
  if (error != MOUNT_ERROR_PATH_ALREADY_MOUNTED) {
    // MOUNT_ERROR_PATH_ALREADY_MOUNTED is returned on EBUSY.
    return error;
  }

  // For FUSE filesystems, MNT_FORCE will cause the kernel driver to
  // immediately close the channel to the user-space driver program and cancel
  // all outstanding requests. However, if any program is still accessing the
  // filesystem, the umount2() will fail with EBUSY and the mountpoint will
  // still be attached. Since the mountpoint is no longer valid, use
  // MNT_DETACH to also force the mountpoint to be disconnected.
  // On a non-FUSE filesystem MNT_FORCE doesn't have effect, so it only
  // handles MNT_DETACH, but it's OK to pass MNT_FORCE too.
  LOG(WARNING) << "Mount point " << quote(path())
               << " is busy, using force unmount";
  return platform_->Unmount(path().value(), MNT_FORCE | MNT_DETACH);
}

MountErrorType MountPoint::RemountImpl(int flags) {
  return platform_->Mount(data_.source, data_.mount_path.value(),
                          data_.filesystem_type, flags | MS_REMOUNT,
                          data_.data);
}

}  // namespace cros_disks
