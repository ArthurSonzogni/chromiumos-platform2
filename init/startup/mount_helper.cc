// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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
#include "init/startup/mount_helper.h"
#include "init/startup/startup_dep_impl.h"

namespace {

constexpr char kVar[] = "var";
constexpr char kHomeChronos[] = "home/chronos";
constexpr char kMountEncryptedLog[] = "run/mount_encrypted/mount-encrypted.log";

}  // namespace

namespace startup {

MountHelper::MountHelper(libstorage::Platform* platform,
                         StartupDep* startup_dep,
                         const Flags& flags,
                         const base::FilePath& root,
                         const base::FilePath& stateful,
                         const bool dev_mode)
    : platform_(platform),
      startup_dep_(startup_dep),
      flags_(flags),
      root_(root),
      stateful_(stateful),
      dev_mode_(dev_mode) {}

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

// Create, possibly migrate from, the unencrypted stateful partition, and bind
// mount the /var and /home/chronos mounts from the encrypted filesystem
// /mnt/stateful_partition/encrypted, all managed by the "mount-encrypted"
// helper. Takes the same arguments as mount-encrypted. Since /var is managed by
// mount-encrypted, it should not be created in the unencrypted stateful
// partition. Its mount point in the root filesystem exists already from the
// rootfs image. Since /home is still mounted from the unencrypted stateful
// partition, having /home/chronos already doesn't matter. It will be created by
// mount-encrypted if it is missing. These mounts inherit nodev,noexec,nosuid
// from the encrypted filesystem /mnt/stateful_partition/encrypted.
bool MountHelper::MountVarAndHomeChronosEncrypted() {
  base::FilePath mount_enc_log = root_.Append(kMountEncryptedLog);
  std::string output;
  int status =
      startup_dep_->MountEncrypted(std::vector<std::string>(), &output);
  std::string existing_log;
  platform_->ReadFileToString(mount_enc_log, &existing_log);
  existing_log.append(output);
  platform_->WriteStringToFile(mount_enc_log, existing_log);
  return status == 0;
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

  // If both directories are on the same device, the encrypted stateful
  // partition is not mounted.
  if (parent.st_dev == encrypted.st_dev) {
    return true;
  }

  std::unique_ptr<brillo::Process> umount = platform_->CreateProcessInstance();
  umount->AddArg("/usr/sbin/mount-encrypted");
  umount->AddArg("umount");
  int ret = 0;
  for (int i = 0; i < 10; i++) {
    ret = umount->Run();
    if (ret == 0) {
      break;
    }
    base::PlatformThread::Sleep(base::Milliseconds(100));
  }
  return ret == 0;
}

// Bind mount /var and /home/chronos. All function arguments are ignored.
bool MountHelper::MountVarAndHomeChronosUnencrypted() {
  base::FilePath var = stateful_.Append(kVar);
  if (!platform_->CreateDirectory(var)) {
    return false;
  }

  if (!platform_->SetPermissions(var, 0755)) {
    PLOG(WARNING) << "chmod failed for " << var.value();
    return false;
  }

  if (!platform_->Mount(var, root_.Append(kVar), "", MS_BIND, "")) {
    return false;
  }
  if (!platform_->Mount(stateful_.Append(kHomeChronos),
                        root_.Append(kHomeChronos), "", MS_BIND, "")) {
    platform_->Unmount(root_.Append(kVar), false, nullptr);
    return false;
  }
  return true;
}

// Unmount bind mounts for /var and /home/chronos.
bool MountHelper::UmountVarAndHomeChronosUnencrypted() {
  bool ret = false;
  if (platform_->Unmount(root_.Append(kVar), false, nullptr)) {
    ret = true;
  }
  if (platform_->Unmount(root_.Append(kHomeChronos), false, nullptr)) {
    ret = true;
  }
  return ret;
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
