// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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
#include "init/startup/security_manager.h"
#include "init/startup/startup_dep_impl.h"
#include "init/startup/test_mode_mount_helper.h"

namespace {

// Flag file indicating that mount encrypted stateful failed last time.
// If the file is present and mount_encrypted failed again, machine would
// enter self-repair mode.
constexpr char kMountEncryptedFailedFile[] = "mount_encrypted_failed";
constexpr char kSysKeyLogFile[] = "run/create_system_key.log";

}  // namespace

namespace startup {

// Constructor for TestModeMountHelper when the device is
// not in dev mode.
TestModeMountHelper::TestModeMountHelper(libstorage::Platform* platform,
                                         StartupDep* startup_dep,
                                         const startup::Flags& flags,
                                         const base::FilePath& root,
                                         const base::FilePath& stateful,
                                         const bool dev_mode)
    : startup::MountHelper(
          platform, startup_dep, flags, root, stateful, dev_mode) {}

bool TestModeMountHelper::DoMountVarAndHomeChronos() {
  // If this a TPM 2.0 device that supports encrypted stateful, creates and
  // persists a system key into NVRAM and backs the key up if it doesn't exist.
  // If the call create_system_key is successful, mount_var_and_home_chronos
  // will skip the normal system key generation procedure; otherwise, it will
  // generate and persist a key via its normal workflow.
  std::optional<bool> system_key = flags_.sys_key_util;
  bool sys_key = system_key.value_or(false);
  if (sys_key) {
    LOG(INFO) << "Creating System Key";
    std::string output;
    CreateSystemKey(platform_, root_, stateful_, startup_dep_, &output);
    base::FilePath log_file = root_.Append(kSysKeyLogFile);
    platform_->WriteStringToFile(log_file, output);
  }

  base::FilePath encrypted_failed = stateful_.Append(kMountEncryptedFailedFile);
  bool ret;
  uid_t uid;
  if (!platform_->GetOwnership(encrypted_failed, &uid, nullptr,
                               false /* follow_links */) ||
      (uid != getuid())) {
    // Try to use the original handler in chromeos_startup.
    // It should not wipe whole stateful partition in this case.
    return MountVarAndHomeChronos();
  }

  ret = MountVarAndHomeChronos();
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
                                     base::FileEnumerator::FILES));
    for (base::FilePath path = enumerator->Next(); !path.empty();
         path = enumerator->Next()) {
      if (path.BaseName().value().rfind("encrypted.", 0) == 0) {
        base::FilePath to_path = backup.Append(path.BaseName());
        platform_->Rename(path, to_path, true /* cros_fs */);
      }
    }

    return MountVarAndHomeChronos();
  }
  return true;
}

startup::MountHelperType TestModeMountHelper::GetMountHelperType() const {
  return startup::MountHelperType::kTestMode;
}

}  // namespace startup
