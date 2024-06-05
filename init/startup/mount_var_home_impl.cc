// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "init/startup/mount_var_home_impl.h"

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

#include "init/startup/mount_helper.h"
#include "init/startup/startup_dep_impl.h"

namespace {

constexpr char kVar[] = "var";
constexpr char kHomeChronos[] = "home/chronos";
constexpr char kMountEncryptedLog[] = "run/mount_encrypted/mount-encrypted.log";

}  // namespace

namespace startup {

MountVarAndHomeChronosImpl::MountVarAndHomeChronosImpl(
    libstorage::Platform* platform,
    StartupDep* startup_dep,
    const base::FilePath& root,
    const base::FilePath& stateful)
    : platform_(platform),
      startup_dep_(startup_dep),
      root_(root),
      stateful_(stateful) {}

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
bool MountVarAndHomeChronosImpl::MountEncrypted() {
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
bool MountVarAndHomeChronosImpl::UmountEncrypted() {
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
  return ret == 0;
}

// Bind mount /var and /home/chronos. All function arguments are ignored.
bool MountVarAndHomeChronosImpl::MountUnencrypted() {
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
bool MountVarAndHomeChronosImpl::UmountUnencrypted() {
  bool ret = false;
  if (platform_->Unmount(root_.Append(kVar), false, nullptr)) {
    ret = true;
  }
  if (platform_->Unmount(root_.Append(kHomeChronos), false, nullptr)) {
    ret = true;
  }
  return ret;
}

}  // namespace startup
