// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/executor/utils/sandboxed_process.h"

#include <inttypes.h>

#include <string>
#include <vector>

#include <base/files/file_util.h>
#include <base/strings/stringprintf.h>

namespace diagnostics {

SandboxedProcess::SandboxedProcess() = default;

SandboxedProcess::~SandboxedProcess() = default;

SandboxedProcess::SandboxedProcess(
    const std::vector<std::string>& command,
    const std::string& seccomp_filename,
    const std::string& user,
    uint64_t capabilities_mask,
    const std::vector<base::FilePath>& readonly_mount_points,
    const std::vector<base::FilePath>& writable_mount_points)
    : command_(command), readonly_mount_points_(readonly_mount_points) {
  auto seccompe_file =
      base::FilePath(kSeccompPolicyDirectory).Append(seccomp_filename);
  sandbox_arguments_ = {
      // Enter a new VFS mount namespace.
      "-v",
      // Remount /proc readonly.
      "-r",
      // Run inside a new IPC namespace.
      "-l",
      // Enter a new network namespace.
      "-e",
      // Create a new UTS/hostname namespace.
      "--uts",
      // Set user.
      "-u",
      user,
      // Set group. The group is assume to be the same as user.
      "-g",
      user,
      // Inherit all the supplementary groups of the user specified with -u.
      "-G",
      // Restrict capabilities.
      "-c",
      base::StringPrintf("0x%" PRIx64, capabilities_mask),
      // Set seccomp policy file.
      "-S",
      seccompe_file.value(),
      // Set the process’s no_new_privs bit.
      "-n",
  };
  for (const base::FilePath& f : writable_mount_points) {
    sandbox_arguments_.push_back("-b");
    sandbox_arguments_.push_back(f.value() + "," + f.value() + ",1");
  }
}

SandboxedProcess::SandboxedProcess(
    const std::vector<std::string>& command,
    const std::string& seccomp_filename,
    const std::vector<base::FilePath>& readonly_mount_points)
    : SandboxedProcess(command,
                       seccomp_filename,
                       kCrosHealthdSandboxUser,
                       /*capabilities_mask=*/0x0,
                       readonly_mount_points,
                       /*writable_mount_points=*/{}) {}

void SandboxedProcess::AddArg(const std::string& arg) {
  command_.push_back(arg);
}

bool SandboxedProcess::Start() {
  PrepareSandboxArguments();

  BrilloProcessAddArg(kMinijailBinary);
  for (const std::string& arg : sandbox_arguments_) {
    BrilloProcessAddArg(arg);
  }
  BrilloProcessAddArg("--");
  for (const std::string& arg : command_) {
    BrilloProcessAddArg(arg);
  }
  return BrilloProcessStart();
}

// Prepares some arguments which need to be handled before use.
void SandboxedProcess::PrepareSandboxArguments() {
  for (const base::FilePath& f : readonly_mount_points_) {
    if (!IsPathExists(f))
      continue;
    sandbox_arguments_.push_back("-b");
    sandbox_arguments_.push_back(f.value());
  }
}

void SandboxedProcess::BrilloProcessAddArg(const std::string& arg) {
  brillo::ProcessImpl::AddArg(arg);
}

bool SandboxedProcess::BrilloProcessStart() {
  return brillo::ProcessImpl::Start();
}

// Checks if a file exist. For mocking.
bool SandboxedProcess::IsPathExists(const base::FilePath& path) const {
  return base::PathExists(path);
}

}  // namespace diagnostics
