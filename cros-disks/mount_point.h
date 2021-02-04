// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CROS_DISKS_MOUNT_POINT_H_
#define CROS_DISKS_MOUNT_POINT_H_

#include <sys/mount.h>

#include <memory>
#include <string>

#include <base/files/file_path.h>
#include <chromeos/dbus/service_constants.h>

namespace cros_disks {

// Holds information about a mount point.
struct MountPointData {
  // Mount point path.
  base::FilePath mount_path;
  // Source description used to mount.
  std::string source = {};
  // Filesystem type of the mount.
  std::string filesystem_type = {};
  // Flags of the mount point.
  int flags = 0;
  // Additional data passed during mount.
  std::string data = {};
};

class Platform;

// Class representing a mount created by a mounter.
class MountPoint {
 public:
  // Creates a MountPoint that does nothing on unmount and 'leaks' the mount
  // point.
  static std::unique_ptr<MountPoint> CreateLeaking(const base::FilePath& path);

  static std::unique_ptr<MountPoint> Mount(MountPointData data,
                                           const Platform* platform,
                                           MountErrorType* error);

  explicit MountPoint(MountPointData data, const Platform* platform = nullptr);

  MountPoint(const MountPoint&) = delete;
  MountPoint& operator=(const MountPoint&) = delete;

  // Unmounts the mount point as a last resort, but as it's unable to handle
  // errors an explicit call to Unmount() is the better alternative.
  virtual ~MountPoint();

  // Releases (leaks) the ownership of the mount point.
  // Until all places handle ownership of mount points properly
  // it's necessary to be able to leave the mount alone.
  virtual void Release();

  // Unmounts right now.
  MountErrorType Unmount();

  // Remount with specified ro/rw.
  MountErrorType Remount(bool read_only);

  const base::FilePath& path() const { return data_.mount_path; }
  const std::string& source() const { return data_.source; }
  const std::string& fstype() const { return data_.filesystem_type; }
  int flags() const { return data_.flags; }
  const std::string& data() const { return data_.data; }
  bool is_read_only() const { return (data_.flags & MS_RDONLY) == MS_RDONLY; }

 protected:
  // Unmounts the mount point. If MOUNT_ERROR_NONE is returned, will only be
  // called once, regardless of the number of times Unmount() is called. If
  // Release() is called, this function will not be called.
  virtual MountErrorType UnmountImpl();

  // Remounts with new flags. Only called if mount is assumed to be mounted.
  virtual MountErrorType RemountImpl(int flags);

  MountPointData data_;
  const Platform* platform_;

 private:
  bool released_ = false;
};

}  // namespace cros_disks

#endif  // CROS_DISKS_MOUNT_POINT_H_
