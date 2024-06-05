// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "init/startup/mount_helper.h"

#include <sys/mount.h>

#include <memory>
#include <optional>
#include <stack>
#include <string>
#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/threading/platform_thread.h>
#include <base/values.h>
#include <brillo/process/process.h>
#include <libstorage/platform/platform.h>

#include "init/startup/flags.h"
#include "init/startup/mount_var_home_impl.h"
#include "init/startup/mount_var_home_interface.h"
#include "init/startup/startup_dep_impl.h"

namespace startup {

MountHelper::MountHelper(libstorage::Platform* platform,
                         StartupDep* startup_dep,
                         const Flags& flags,
                         const base::FilePath& root,
                         const base::FilePath& stateful,
                         std::unique_ptr<MountVarAndHomeChronosInterface> impl)
    : platform_(platform),
      startup_dep_(startup_dep),
      flags_(flags),
      root_(root),
      stateful_(stateful),
      impl_(std::move(impl)) {}

MountHelper::MountHelper(libstorage::Platform* platform,
                         StartupDep* startup_dep,
                         const Flags& flags,
                         const base::FilePath& root,
                         const base::FilePath& stateful)
    : MountHelper(platform,
                  startup_dep,
                  flags,
                  root,
                  stateful,
                  std::make_unique<MountVarAndHomeChronosImpl>(
                      platform, startup_dep, root, stateful)) {}

MountHelper::~MountHelper() = default;

// Adds mounts to undo_mount stack.
void MountHelper::RememberMount(const base::FilePath& mount) {
  mount_stack_.push(mount);
}

void MountHelper::CleanupMountsStack(std::vector<base::FilePath>* mnts) {
  // On failure unmount all saved mount points and repair stateful.
  base::FilePath encrypted = stateful_.Append("encrypted");
  while (!mount_stack_.empty()) {
    base::FilePath mnt = mount_stack_.top();
    mnts->push_back(mnt);
    if (mnt == encrypted) {
      DoUmountVarAndHomeChronos();
    } else {
      platform_->Unmount(mnt, false, nullptr);
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
  startup_dep_->BootAlert("self_repair");

  std::string mounts_str;
  for (base::FilePath mount : mounts) {
    mounts_str.append(mount.value());
    mounts_str.append(", ");
  }
  std::string message = "Self-repair incoherent stateful partition: " + msg +
                        ". History: " + mounts_str;
  LOG(INFO) << message;
  startup_dep_->ClobberLog(message);

  base::FilePath tmpfiles = root_.Append("run/tmpfiles.log");
  std::unique_ptr<brillo::Process> append_log =
      platform_->CreateProcessInstance();
  append_log->AddArg("/sbin/clobber-log");
  append_log->AddArg("--append_logfile");
  append_log->AddArg(tmpfiles.value());
  if (!append_log->Run()) {
    LOG(WARNING) << "clobber-log --append_logfile failed";
  }

  std::vector<std::string> crash_args{"--clobber_state"};
  startup_dep_->AddClobberCrashReport(crash_args);

  std::vector<std::string> argv{"fast", "keepimg", "preserve_lvs"};
  startup_dep_->Clobber(argv);
}

// Used to mount essential mount points for the system from the stateful
// or encrypted stateful partition.
// On failure, clobbers the stateful partition.
void MountHelper::BindMountOrFail(const base::FilePath& source,
                                  const base::FilePath& target) {
  if (platform_->DirectoryExists(source) &&
      platform_->DirectoryExists(target)) {
    if (platform_->Mount(source, target, "", MS_BIND, "")) {
      // Push it on the undo stack if we fail later.
      RememberMount(target);
      return;
    }
  }
  std::string msg =
      "Failed to bind mount " + source.value() + ", " + target.value();
  CleanupMounts(msg);
}

bool MountHelper::MountVarAndHomeChronosEncrypted() {
  return impl_->MountEncrypted();
}

bool MountHelper::UmountVarAndHomeChronosEncrypted() {
  return impl_->UmountEncrypted();
}

bool MountHelper::MountVarAndHomeChronosUnencrypted() {
  return impl_->MountUnencrypted();
}

bool MountHelper::UmountVarAndHomeChronosUnencrypted() {
  return impl_->UmountUnencrypted();
}

bool MountHelper::MountVarAndHomeChronos() {
  if (flags_.encstateful) {
    return MountVarAndHomeChronosEncrypted();
  }
  return MountVarAndHomeChronosUnencrypted();
}

bool MountHelper::DoUmountVarAndHomeChronos() {
  if (flags_.encstateful) {
    return UmountVarAndHomeChronosEncrypted();
  }
  return UmountVarAndHomeChronosUnencrypted();
}

}  // namespace startup
