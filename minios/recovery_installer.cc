// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "minios/recovery_installer.h"

#include <string>
#include <vector>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <unistd.h>

#include "minios/utils.h"

namespace minios {

constexpr char kUdevAdmCmd[] = "/usr/bin/udevadm";
constexpr char kSettle[] = "settle";

bool RecoveryInstaller::RepartitionDisk() {
  int return_code = 0;
  std::string stderr, stdout;
  bool partition_success = true;

  if (repartition_completed_) {
    LOG(INFO) << "Previously called repartition disk. Skipping.";
    return true;
  }
  base::FilePath console = GetLogConsole();
  const std::vector<std::string> cmd = {
      "/bin/chromeos-install", "--skip_rootfs", "--skip_dst_removable", "--yes",
#if defined(ARCH_x86) || defined(ARCH_amd64)
      "--pmbr_code=/usr/share/syslinux/gptmbr.bin"
#else
      "--pmbr_code=/dev/zero"
#endif
  };
  if (!process_manager_->RunCommandWithOutput(cmd, &return_code, &stdout,
                                              &stderr)) {
    partition_success = false;
    PLOG(WARNING) << "Repartitioning the disk failed";
  } else if (return_code != 0) {
    partition_success = false;
    LOG(ERROR) << "Repartitioning the disk failed, return_code=" << return_code;
  } else {
    repartition_completed_ = true;
    LOG(INFO) << "Successfully repartitioned disk.";
  }
  const auto& consolidated_output =
      "cmd=" + cmd[0] + "\nstdout=" + stdout + "\nstderr=" + stderr;

  if (!base::WriteFile(console, consolidated_output)) {
    PLOG(WARNING) << "Failed to write to console=" << console;
  }

  LOG(INFO) << consolidated_output;
  if (process_manager_->RunCommand({kUdevAdmCmd, kSettle}, {}) != 0) {
    LOG(ERROR) << "Udevadm settle failed.";
    return false;
  }

  return partition_success;
}

}  // namespace minios
