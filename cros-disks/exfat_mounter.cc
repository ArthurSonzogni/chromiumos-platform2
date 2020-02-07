// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/exfat_mounter.h"

#include "cros-disks/platform.h"

namespace cros_disks {
namespace {

// Expected location of the exfat-fuse executable.
const char kMountProgramPath[] = "/usr/sbin/mount.exfat-fuse";

const char kMountUser[] = "fuse-exfat";

}  // namespace

const char ExFATMounter::kMounterType[] = "exfat";

ExFATMounter::ExFATMounter(const std::string& filesystem_type,
                           const MountOptions& mount_options,
                           const Platform* platform,
                           brillo::ProcessReaper* process_reaper)
    : FUSEMounter(filesystem_type,
                  mount_options,
                  platform,
                  process_reaper,
                  kMountProgramPath,
                  kMountUser,
                  "",
                  {},
                  false) {}

}  // namespace cros_disks
