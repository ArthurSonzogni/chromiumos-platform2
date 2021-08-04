// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "minios/recovery_installer.h"

#include <errno.h>

#include <base/logging.h>

#include "minios/minios.h"

namespace minios {

bool RecoveryInstaller::RepartitionDisk() {
  if (repartition_completed_) {
    LOG(INFO) << "Previously called repartition disk. Skipping.";
    return true;
  }

  if (!process_manager_->RunCommand(
          {"/bin/chromeos-install", "--skip_rootfs", "--skip_dst_removable",
           "--skip_dst_removable", "--yes",
           "--pmbr_code=/usr/share/syslinux/gptmbr.bin"},
          ProcessManager::IORedirection{
              .input = minios::kDebugConsole,
              .output = minios::kDebugConsole,
          })) {
    PLOG(WARNING) << "Repartitioning the disk failed";
    // TODO(b/187206298): Chromeos-install script returns EBFD. Ignore for now.
    if (errno != EBADF)
      return false;
    LOG(INFO)
        << "Ignoring the Bad File Descriptor error and continuing recovery.";
  }
  repartition_completed_ = true;
  LOG(INFO) << "Successfully repartitioned disk.";
  return true;
}

}  // namespace minios
