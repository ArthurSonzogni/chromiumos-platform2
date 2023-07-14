// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef CRYPTOHOME_STORAGE_MOUNT_HELPER_INTERFACE_H_
#define CRYPTOHOME_STORAGE_MOUNT_HELPER_INTERFACE_H_

#include <string>

#include <base/files/file_path.h>
#include <brillo/secure_blob.h>
#include <chromeos/dbus/service_constants.h>

#include "cryptohome/storage/error.h"
#include "cryptohome/storage/mount_constants.h"
#include "cryptohome/username.h"

namespace cryptohome {
// Objects that implement MountHelperInterface can perform mount operations.
// This interface will be used as we transition all cryptohome mounts to be
// performed out-of-process.
class MountHelperInterface {
 public:
  virtual ~MountHelperInterface() {}

  // Ephemeral mounts cannot be performed twice, so cryptohome needs to be able
  // to check whether an ephemeral mount can be performed.
  virtual bool CanPerformEphemeralMount() const = 0;

  // Returns whether an ephemeral mount has been performed.
  virtual bool MountPerformed() const = 0;

  // Returns whether |path| is currently mounted as part of the ephemeral mount.
  virtual bool IsPathMounted(const base::FilePath& path) const = 0;

  // Carries out an ephemeral mount for user |username|.
  virtual StorageStatus PerformEphemeralMount(
      const Username& username,
      const base::FilePath& ephemeral_loop_device) = 0;

  // Unmounts the mount point.
  virtual void UnmountAll() = 0;

  // Carries out mount operations for a regular cryptohome.
  virtual StorageStatus PerformMount(MountType mount_type,
                                     const Username& username,
                                     const std::string& fek_signature,
                                     const std::string& fnek_signature) = 0;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_STORAGE_MOUNT_HELPER_INTERFACE_H_
