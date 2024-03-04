// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/at_exit.h>
#include <base/check.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/timer/elapsed_timer.h>
#include <brillo/cryptohome.h>
#include <brillo/files/safe_fd.h>
#include <brillo/flag_helper.h>
#include <brillo/syslog_logging.h>

namespace {

constexpr char kExecName[] = "extended-updates-arc-remove-data";

// Copied from ArcSetup::RemoveStaleDataDirectory().
// TODO(niwa): Consolidate into a shared utility for broader reuse.
bool SafeRemoveDir(const base::FilePath& path) {
  constexpr int kRmdirMaxDepth = 768;

  brillo::SafeFD root_fd = brillo::SafeFD::Root().first;
  brillo::SafeFD::SafeFDResult parent_dir =
      root_fd.OpenExistingDir(path.DirName());
  if (brillo::SafeFD::IsError(parent_dir.second)) {
    if (parent_dir.second != brillo::SafeFD::Error::kDoesNotExist) {
      LOG(ERROR) << "Errors while removing data from " << path
                 << ": failed to open the parent directory";
    }
    return false;
  }

  brillo::SafeFD::Error err =
      parent_dir.first.Rmdir(path.BaseName().value(), true /*recursive*/,
                             kRmdirMaxDepth, true /*keep_going*/);
  if (brillo::SafeFD::IsError(err) &&
      err != brillo::SafeFD::Error::kDoesNotExist) {
    LOG(ERROR) << "Errors while removing data from " << path
               << ": failed to remove the directory";
    return false;
  }
  return true;
}

// Removes /home/root/<user_hash>/{android-data,android-data-old}
bool RemoveAndroidDataDirs(const std::string& chromeos_user) {
  brillo::cryptohome::home::Username username(chromeos_user);
  const base::FilePath root_path =
      brillo::cryptohome::home::GetRootPath(username);
  CHECK(!root_path.empty() && base::DirectoryExists(root_path));

  const base::FilePath android_data_dir = root_path.Append("android-data");
  const base::FilePath android_data_old_dir =
      root_path.Append("android-data-old");

  bool success = true;
  if (base::DirectoryExists(android_data_dir)) {
    LOG(INFO) << "Removing " << android_data_dir;
    success &= SafeRemoveDir(android_data_dir);
  }
  if (base::DirectoryExists(android_data_old_dir)) {
    LOG(INFO) << "Removing " << android_data_old_dir;
    success &= SafeRemoveDir(android_data_old_dir);
  }
  return success;
}

}  // namespace

int main(int argc, char** argv) {
  DEFINE_string(chromeos_user, "", "Target user name (CHROMEOS_USER)");

  base::ElapsedTimer timer;
  base::AtExitManager at_exit;

  brillo::FlagHelper::Init(argc, argv, kExecName);
  brillo::OpenLog(kExecName, true /*log_pid*/);
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogHeader |
                  brillo::kLogToStderrIfTty);

  CHECK(!FLAGS_chromeos_user.empty()) << "Must specify --chromeos_user";

  const bool success = RemoveAndroidDataDirs(FLAGS_chromeos_user);

  // TODO(b/327140239): Support data removal for devices using virtio-blk /data

  LOG(INFO) << kExecName << " took "
            << timer.Elapsed().InMillisecondsRoundedUp() << "ms";
  return success ? 0 : 1;
}
