// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sys/mount.h>

#include <memory>
#include <stack>
#include <string>
#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/values.h>
#include <brillo/process/process.h>

#include "init/clobber_state.h"
#include "init/crossystem.h"
#include "init/crossystem_impl.h"
#include "init/startup/flags.h"
#include "init/startup/mount_helper.h"
#include "init/startup/platform_impl.h"

namespace {

constexpr char kVar[] = "var";
constexpr char kHomeChronos[] = "home/chronos";

}  // namespace

namespace startup {

MountHelper::MountHelper(std::unique_ptr<Platform> platform,
                         const Flags& flags,
                         const base::FilePath& root,
                         const base::FilePath& stateful)
    : platform_(std::move(platform)),
      flags_(flags),
      root_(root),
      stateful_(stateful) {}

void MountHelper::CleanupMountsStack(std::vector<base::FilePath>* mnts) {
  // On failure unmount all saved mount points and repair stateful.
  base::FilePath encrypted = stateful_.Append("encrypted");
  while (!mount_stack_.empty()) {
    base::FilePath mnt = mount_stack_.top();
    mnts->push_back(mnt);
    if (mnt == encrypted) {
      DoUmountVarAndHomeChronos();
    } else {
      platform_->Umount(mnt);
    }
    mount_stack_.pop();
  }
}

// Unmounts the incomplete mount setup during the failure path. Failure to
// set up mounts results in the entire stateful partition getting wiped
// using clobber-state.
void MountHelper::CleanupMounts(const std::string& msg) {
  // On failure unmount all saved mount points and repair stateful.
  std::vector<base::FilePath> mounts;
  CleanupMountsStack(&mounts);

  // Leave /mnt/stateful_partition mounted for clobber-state to handle.
  platform_->BootAlert("self_repair");

  std::string mounts_str;
  for (base::FilePath mount : mounts) {
    mounts_str.append(mount.value());
    mounts_str.append(", ");
  }
  std::string message = "Self-repair incoherent stateful partition: " + msg +
                        ". History: " + mounts_str;
  LOG(INFO) << message;
  platform_->ClobberLog(message);

  base::FilePath tmpfiles = root_.Append("run/tmpfiles.log");
  brillo::ProcessImpl append_log;
  append_log.AddArg("/sbin/clobber-log");
  append_log.AddArg("--append_logfile");
  append_log.AddArg(tmpfiles.value());
  if (!append_log.Run()) {
    PLOG(WARNING) << "clobber-log --append_logfile failed";
  }

  std::vector<std::string> crash_args{"--clobber_state"};
  platform_->AddClobberCrashReport(crash_args);

  std::vector<std::string> argv{"fast", "keepimg"};
  platform_->Clobber(argv);
}

// Give mount-encrypted umount 10 times to retry, otherwise
// it will fail with "device is busy" because lazy umount does not finish
// clearing all reference points yet. Check crbug.com/p/21345.
bool MountHelper::UmountVarAndHomeChronosEncrypted() {
  // Check if the encrypted stateful partition is mounted.
  base::FilePath mount_enc = stateful_.Append("encrypted");
  struct stat encrypted, parent;
  if (lstat(stateful_.value().c_str(), &parent)) {
    return false;
  }
  if (lstat(mount_enc.value().c_str(), &encrypted)) {
    return false;
  }
  if (parent.st_dev != encrypted.st_dev) {
    return false;
  }

  brillo::ProcessImpl umount;
  umount.AddArg("/usr/sbin/mount-encrypted");
  umount.AddArg("umount");
  int ret = 0;
  for (int i = 0; i < 10; i++) {
    ret = umount.Run();
    if (ret == 0) {
      break;
    }
    base::PlatformThread::Sleep(base::Milliseconds(100));
  }
  return !ret;
}

// Unmount bind mounts for /var and /home/chronos.
bool MountHelper::UmountVarAndHomeChronosUnencrypted() {
  bool ret = false;
  if (platform_->Umount(root_.Append(kVar))) {
    ret = true;
  }
  if (platform_->Umount(root_.Append(kHomeChronos))) {
    ret = true;
  }
  return ret;
}

bool MountHelper::DoUmountVarAndHomeChronos() {
  std::optional<bool> encrypted = GetFlags().encstateful;
  bool encrypted_state = encrypted.value_or(false);
  if (encrypted_state) {
    return UmountVarAndHomeChronosEncrypted();
  }
  return UmountVarAndHomeChronosUnencrypted();
}

void MountHelper::SetMountStackForTest(
    const std::stack<base::FilePath>& mount_stack) {
  mount_stack_ = mount_stack;
}

Flags MountHelper::GetFlags() {
  return flags_;
}

}  // namespace startup
