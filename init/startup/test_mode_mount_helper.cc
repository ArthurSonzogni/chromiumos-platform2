// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "init/startup/test_mode_mount_helper.h"

#include <sys/stat.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/values.h>
#include <brillo/files/file_util.h>
#include <brillo/process/process.h>
#include <libstorage/platform/platform.h>

#include "init/startup/flags.h"
#include "init/startup/mount_helper.h"
#include "init/startup/mount_var_home_interface.h"
#include "init/startup/security_manager.h"
#include "init/startup/startup_dep_impl.h"

namespace {

constexpr char kNoEarlyKeyFile[] = ".no_early_system_key";
constexpr char kSysKeyBackupFile[] = "unencrypted/preserve/system.key";

}  // namespace

namespace startup {

// Constructor for TestModeMountHelper when the device is
// not in dev mode.
TestModeMountHelper::TestModeMountHelper(
    libstorage::Platform* platform,
    StartupDep* startup_dep,
    const Flags& flags,
    const base::FilePath& root,
    const base::FilePath& stateful,
    std::unique_ptr<MountVarAndHomeChronosInterface> impl,
    std::unique_ptr<libstorage::StorageContainerFactory>
        storage_container_factory)
    : MountHelper(platform,
                  startup_dep,
                  flags,
                  root,
                  stateful,
                  std::move(impl),
                  std::move(storage_container_factory)) {}

base::FilePath TestModeMountHelper::GetKeyBackupFile() {
  // If this a TPM 2.0 device that supports encrypted stateful, creates and
  // persists a system key into NVRAM and backs the key up if it doesn't exist.
  // If the call create_system_key is successful, mount_var_and_home_chronos
  // will skip the normal system key generation procedure; otherwise, it will
  // generate and persist a key via its normal workflow.
  base::FilePath no_early = stateful_.Append(kNoEarlyKeyFile);
  if (flags_.sys_key_util && !platform_->FileExists(no_early)) {
    LOG(INFO) << "Creating System Key";
    return stateful_.Append(kSysKeyBackupFile);
  }
  return base::FilePath();
}

bool TestModeMountHelper::DoMountVarAndHomeChronos(
    std::optional<encryption::EncryptionKey> key) {
  bool ret = MountVarAndHomeChronos(key);
  if (!ret) {
    // Try to re-construct encrypted folders, otherwise such a failure will lead
    // to wiping whole stateful partition (including all helpful programs in
    // /usr/local/bin and sshd).
    std::string msg("Failed mounting var and home/chronos; re-created.");
    startup_dep_->ClobberLog(msg);

    std::vector<std::string> crash_args{"--mount_failure",
                                        "--mount_device='encstateful'"};
    startup_dep_->AddClobberCrashReport(crash_args);
    base::FilePath backup = stateful_.Append("corrupted_encryption");
    brillo::DeletePathRecursively(backup);
    platform_->CreateDirectory(backup);
    if (!platform_->SetPermissions(backup, 0755)) {
      PLOG(WARNING) << "chmod failed for " << backup.value();
    }

    std::unique_ptr<libstorage::FileEnumerator> enumerator(
        platform_->GetFileEnumerator(stateful_, false /* recursive */,
                                     base::FileEnumerator::FILES,
                                     "encrypted.*"));
    for (base::FilePath path = enumerator->Next(); !path.empty();
         path = enumerator->Next()) {
      base::FilePath to_path = backup.Append(path.BaseName());
      platform_->Rename(path, to_path, true /* cros_fs */);
    }

    ret = MountVarAndHomeChronos(key);
  }
  return ret;
}

}  // namespace startup
