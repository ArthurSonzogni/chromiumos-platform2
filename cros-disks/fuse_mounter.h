// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CROS_DISKS_FUSE_MOUNTER_H_
#define CROS_DISKS_FUSE_MOUNTER_H_

#include <string>

#include "cros-disks/mounter.h"

namespace cros_disks {

class Platform;

// A class for mounting a device file using a FUSE mount program.
class FUSEMounter : public Mounter {
 public:
  FUSEMounter(const std::string& source_path,
              const std::string& target_path,
              const std::string& filesystem_type,
              const MountOptions& mount_options,
              const Platform* platform,
              const std::string& mount_program_path,
              const std::string& mount_user,
              const std::string& seccomp_policy,
              bool permit_network_access);

 protected:
  // Mounts a device file using the FUSE mount program at |mount_program_path_|.
  MountErrorType MountImpl() override;

  // An object that provides platform service.
  const Platform* const platform_;

  // Path of the FUSE mount program.
  const std::string mount_program_path_;

  // User to run the FUSE mount program as.
  const std::string mount_user_;

  // If not empty the path to BPF seccomp filter policy.
  const std::string seccomp_policy_;

  // Whether to leave network access to the mount program.
  const bool permit_network_access_;
};

}  // namespace cros_disks

#endif  // CROS_DISKS_FUSE_MOUNTER_H_
