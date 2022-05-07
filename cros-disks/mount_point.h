// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CROS_DISKS_MOUNT_POINT_H_
#define CROS_DISKS_MOUNT_POINT_H_

#include <sys/mount.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/callback.h>
#include <base/files/file_path.h>
#include <base/memory/weak_ptr.h>
#include <chromeos/dbus/service_constants.h>

#include "cros-disks/metrics.h"
#include "cros-disks/process.h"

namespace cros_disks {

// Holds information about a mount point.
struct MountPointData {
  // Mount point path.
  base::FilePath mount_path;
  // Source description used to mount.
  std::string source;
  // Source type.
  MountSourceType source_type = MOUNT_SOURCE_INVALID;
  // Filesystem type of the mount.
  std::string filesystem_type;
  // Flags of the mount point.
  int flags = 0;
  // Additional data passed during mount.
  std::string data;
  // Error state associated to this mount point.
  MountErrorType error = MOUNT_ERROR_NONE;
};

class Platform;

// Class representing a mount created by a mounter.
class MountPoint final {
 public:
  // Creates a MountPoint that is not actually mounted.
  static std::unique_ptr<MountPoint> CreateUnmounted(
      MountPointData data, const Platform* platform = nullptr);

  // Mounts a mount point. Returns a null pointer and sets *error in case of
  // error.
  static std::unique_ptr<MountPoint> Mount(MountPointData data,
                                           const Platform* platform,
                                           MountErrorType* error);

  explicit MountPoint(MountPointData data, const Platform* platform = nullptr);

  MountPoint(const MountPoint&) = delete;
  MountPoint& operator=(const MountPoint&) = delete;

  // Unmounts the mount point as a last resort, but as it's unable to handle
  // errors an explicit call to Unmount() is the better alternative.
  ~MountPoint() { Unmount(); }

  base::WeakPtr<MountPoint> GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

  // Unmounts right now.
  MountErrorType Unmount();

  // Remount with specified ro/rw.
  MountErrorType Remount(bool read_only);

  // Associates a Process object to this MountPoint.
  void SetProcess(std::unique_ptr<Process> process,
                  Metrics* const metrics,
                  std::string metrics_name,
                  std::vector<int> password_needed_exit_codes);

  // Sets the eject action, that will be called when this mount point is
  // successfully unmounted.
  void SetEject(base::OnceClosure eject) {
    DCHECK(!eject_);
    eject_ = std::move(eject);
    DCHECK(eject_);
  }

  // Callback called when the FUSE 'launcher' process finished.
  using LauncherExitCallback = base::OnceCallback<void(MountErrorType)>;
  void SetLauncherExitCallback(LauncherExitCallback callback) {
    DCHECK(!launcher_exit_callback_);
    launcher_exit_callback_ = std::move(callback);
    DCHECK(launcher_exit_callback_);
  }

  // Sets the source and source type.
  void SetSource(std::string source, MountSourceType source_type) {
    data_.source = std::move(source);
    DCHECK_EQ(MOUNT_SOURCE_INVALID, data_.source_type);
    data_.source_type = source_type;
    DCHECK_NE(MOUNT_SOURCE_INVALID, data_.source_type);
  }

  const base::FilePath& path() const { return data_.mount_path; }
  const std::string& source() const { return data_.source; }
  MountSourceType source_type() const { return data_.source_type; }
  const std::string& fstype() const { return data_.filesystem_type; }
  int flags() const { return data_.flags; }
  const std::string& data() const { return data_.data; }
  MountErrorType error() const { return data_.error; }
  bool is_read_only() const { return (data_.flags & MS_RDONLY) != 0; }
  bool is_mounted() const { return is_mounted_; }
  Process* process() const { return process_.get(); }

 private:
  // Converts the FUSE launcher's exit code into a MountErrorType.
  MountErrorType ConvertLauncherExitCodeToMountError(int exit_code) const;

  // Called when the 'launcher' process finished.
  void OnLauncherExit(int exit_code);

  // Mount point data.
  MountPointData data_;

  // Pointer to Platform implementation.
  const Platform* const platform_;

  // Process object holding the FUSE processes associated to this MountPoint.
  std::unique_ptr<Process> process_;

  // Eject action called after successfully unmounting this mount point.
  base::OnceClosure eject_;

  // Metrics object and name used to record the FUSE launcher exit code.
  Metrics* metrics_ = nullptr;
  std::string metrics_name_;

  // Set of FUSE launcher exit codes that are interpreted as
  // MOUNT_ERROR_NEED_PASSWORD.
  std::vector<int> password_needed_exit_codes_;

  // Callback called when the FUSE 'launcher' process finished.
  LauncherExitCallback launcher_exit_callback_;

  // Is this mount point actually mounted?
  bool is_mounted_ = true;

  // Should the mount point directory be eventually removed?
  bool must_remove_dir_ = platform_ != nullptr;

  base::WeakPtrFactory<MountPoint> weak_factory_{this};
};

}  // namespace cros_disks

#endif  // CROS_DISKS_MOUNT_POINT_H_
