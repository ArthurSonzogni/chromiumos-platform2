// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sys/mount.h>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/time/time.h>

#include "minios/minios.h"
#include "minios/process_manager.h"

namespace {

bool ForceCreateSymbolicLink(const base::FilePath& target,
                             const base::FilePath& symlink) {
  if (base::PathExists(symlink) && !base::DeleteFile(symlink))
    return false;
  return base::CreateSymbolicLink(target, symlink);
}

// Sanity checks the date (crosbug.com/13200).
bool InitClock() {
  auto now = base::Time::Now();
  base::Time::Exploded exploded;
  now.UTCExplode(&exploded);
  if (exploded.year >= 1970)
    return true;
  constexpr char kDayAfterUnixEpoch[] = "010200001970.00";
  return ProcessManager().RunCommand({"/bin/date", kDayAfterUnixEpoch},
                                     ProcessManager::IORedirection{
                                         .input = minios::kDebugConsole,
                                         .output = minios::kDebugConsole,
                                     }) != 0;
}

// Sets up all the common system mount points.
bool InitMounts() {
  if (mount("proc", "/proc", "proc", MS_NODEV | MS_NOEXEC | MS_NOSUID,
            nullptr) != 0) {
    PLOG(ERROR) << "Failed to mount proc";
    return false;
  }
  if (mount("sysfs", "/sys", "sysfs", MS_NODEV | MS_NOEXEC | MS_NOSUID,
            nullptr) != 0) {
    PLOG(ERROR) << "Failed to mount sysfs";
    return false;
  }
  if (mount("devtmpfs", "/dev", "devtmpfs", MS_NOSUID, "mode=0755") != 0) {
    PLOG(ERROR) << "Failed to mount dev";
    return false;
  }
  for (const auto& [target, symlink] :
       std::vector<std::pair<std::string, std::string>>{
           {"/proc/self/fd", "/dev/fd"},
           {"/proc/self/fd/0", "/dev/stdin"},
           {"/proc/self/fd/1", "/dev/stdout"},
           {"/proc/self/fd/2", "/dev/stderr"}}) {
    if (!ForceCreateSymbolicLink(base::FilePath(target),
                                 base::FilePath(symlink))) {
      PLOG(ERROR) << "Failed to create " << symlink << " symlink";
      return false;
    }
  }
  const base::FilePath kDevPts("/dev/pts");
  if (!base::PathExists(kDevPts) && !base::CreateDirectory(kDevPts)) {
    PLOG(ERROR) << "Failed to create /dev/pts directory";
    return false;
  }
  if (mount("devpts", "/dev/pts", "devpts", MS_NOEXEC | MS_NOSUID, nullptr) !=
      0) {
    PLOG(ERROR) << "Failed to mount devpts";
    return false;
  }
  if (mount("debugfs", "/sys/kernel/debug", "debugfs", 0, nullptr) != 0) {
    PLOG(ERROR) << "Failed to mount debugfs";
    return false;
  }
  return true;
}

}  // namespace

// This init runs steps required for upstart to start successfully.
int main() {
  if (!InitClock()) {
    LOG(ERROR) << "Failed to init clock.";
    return -1;
  }
  if (!InitMounts()) {
    LOG(ERROR) << "Failed to init mounts.";
    return -1;
  }
  return ProcessManager().RunCommand({"/init.sh"},
                                     ProcessManager::IORedirection{
                                         .input = "/dev/null",
                                         .output = minios::kLogFile,
                                     });
}
