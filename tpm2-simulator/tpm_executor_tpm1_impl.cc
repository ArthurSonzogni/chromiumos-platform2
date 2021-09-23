// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <string>
#include <vector>

#include <base/files/file.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/posix/eintr_wrapper.h>
#include <brillo/file_utils.h>
#include <brillo/process/process.h>
#include <brillo/userdb_utils.h>
#include <libtpms/tpm_error.h>
#include <libtpms/tpm_library.h>
#include <linux/vtpm_proxy.h>
#include <sys/stat.h>

#include "tpm2-simulator/tpm_command_utils.h"
#include "tpm2-simulator/tpm_executor_tpm1_impl.h"

namespace {

constexpr char kSimulatorUser[] = "tpm2-simulator";
constexpr char kNVChipPath[] = "NVChip";
constexpr char kEnvTpmPath[] = "TPM_PATH";
constexpr char kTpmDataPath[] = "NVChip_mount";
constexpr size_t kNVChipSize = 1024 * 1024;  // 1MB.

constexpr unsigned char kStartupCommand[] = {
    0x80, 0x01, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x01, 0x44, 0x00, 0x00};

bool Tune2Fs(const base::FilePath& file, const std::vector<std::string>& opts) {
  brillo::ProcessImpl tune_process;
  tune_process.AddArg("/sbin/tune2fs");
  for (const auto& arg : opts)
    tune_process.AddArg(arg);

  tune_process.AddArg(file.value());

  // Close unused file descriptors in child process.
  tune_process.SetCloseUnusedFileDescriptors(true);

  // Avoid polluting the parent process' stdout.
  tune_process.RedirectOutput("/dev/null");

  int rc = tune_process.Run();
  if (rc != 0) {
    LOG(ERROR) << "Can't tune ext4: " << file.value() << ", error: " << rc;
    return false;
  }
  return true;
}

bool FormatExt4(const base::FilePath& file) {
  brillo::ProcessImpl format_process;
  format_process.AddArg("/sbin/mkfs.ext4");

  format_process.AddArg(file.value());

  // No need to emit output.
  format_process.AddArg("-q");

  // Close unused file descriptors in child process.
  format_process.SetCloseUnusedFileDescriptors(true);

  // Avoid polluting the parent process' stdout.
  format_process.RedirectOutput("/dev/null");

  int rc = format_process.Run();
  if (rc != 0) {
    LOG(ERROR) << "Can't format '" << file.value()
               << "' as ext4, exit status: " << rc;
    return false;
  }

  // Tune the formatted filesystem:
  // -c 0: Disable max mount count checking.
  // -i 0: Disable filesystem checking.
  return Tune2Fs(file, {"-c", "0", "-i", "0"});
}

bool MountLoopbackFile(const base::FilePath& file,
                       const base::FilePath& mount_point) {
  brillo::ProcessImpl mount_process;
  mount_process.AddArg("/bin/mount");

  mount_process.AddArg("-o");
  mount_process.AddArg("loop");

  mount_process.AddArg(file.value());
  mount_process.AddArg(mount_point.value());

  // Close unused file descriptors in child process.
  mount_process.SetCloseUnusedFileDescriptors(true);

  // Avoid polluting the parent process' stdout.
  mount_process.RedirectOutput("/dev/null");

  int rc = mount_process.Run();
  if (rc != 0) {
    LOG(ERROR) << "Can't mount '" << file.value() << "' to '"
               << mount_point.value() << "', exit status: " << rc;
    return false;
  }
  return true;
}

bool ChownDirectoryContents(const base::FilePath& dir, uid_t uid, gid_t gid) {
  base::FileEnumerator ent_enum(dir, false, base::FileEnumerator::FILES);
  for (base::FilePath path = ent_enum.Next(); !path.empty();
       path = ent_enum.Next()) {
    if (HANDLE_EINTR(chown(path.value().c_str(), uid, gid)) < 0) {
      PLOG(ERROR) << "Failed to chown " << path.value();
      return false;
    }
  }
  return true;
}

}  // namespace

namespace tpm2_simulator {

void TpmExecutorTpm1Impl::InitializeVTPM() {
  if (!base::PathExists(base::FilePath(kNVChipPath))) {
    if (!brillo::WriteStringToFile(base::FilePath(kNVChipPath),
                                   std::string(kNVChipSize, '\0'))) {
      LOG(ERROR) << "Failed to create the NVChip";
      return;
    }
    if (!FormatExt4(base::FilePath(kNVChipPath))) {
      LOG(ERROR) << "Failed to format the NVChip to ext4";
      return;
    }
  }

  if (!base::PathExists(base::FilePath(kTpmDataPath))) {
    if (!base::CreateDirectory(base::FilePath(kTpmDataPath))) {
      LOG(ERROR) << "Failed to create the NVChip mount point";
      return;
    }
  }

  uid_t uid;
  gid_t gid;
  if (!brillo::userdb::GetUserInfo(kSimulatorUser, &uid, &gid)) {
    LOG(ERROR) << "Failed to lookup the user name.";
    return;
  }
  if (HANDLE_EINTR(chown(kNVChipPath, uid, gid)) < 0) {
    PLOG(ERROR) << "Failed to chown the NVChip.";
    return;
  }
  if (HANDLE_EINTR(chown(kTpmDataPath, uid, gid)) < 0) {
    PLOG(ERROR) << "Failed to chown the NVChip mount point.";
    return;
  }

  if (HANDLE_EINTR(unshare(CLONE_NEWNS)) < 0) {
    PLOG(ERROR) << "Failed to unshare.";
    return;
  }

  if (!MountLoopbackFile(base::FilePath(kNVChipPath),
                         base::FilePath(kTpmDataPath))) {
    LOG(ERROR) << "Failed to mount the NVChip";
    return;
  }

  setenv(kEnvTpmPath, kTpmDataPath, 0);
  TPM_RESULT res;
  res = TPMLIB_ChooseTPMVersion(TPMLIB_TPM_VERSION_1_2);
  if (res) {
    LOG(ERROR) << "TPMLIB_ChooseTPMVersion failed with: " << res;
    return;
  }

  res = TPMLIB_MainInit();
  if (res) {
    LOG(ERROR) << "TPMLIB_MainInit failed with: " << res;
    return;
  }

  RunCommand(std::string(reinterpret_cast<const char*>(kStartupCommand),
                         sizeof(kStartupCommand)));

  if (HANDLE_EINTR(chown(kTpmDataPath, uid, gid)) < 0) {
    PLOG(ERROR) << "Failed to chown the NVChip mount point after mount.";
    return;
  }

  if (!ChownDirectoryContents(base::FilePath(kTpmDataPath), uid, gid)) {
    LOG(ERROR)
        << "Failed to chown the NVChip mount point contents after mount.";
    return;
  }

  LOG(INFO) << "vTPM Initialize.";
}

size_t TpmExecutorTpm1Impl::GetCommandSize(const std::string& command) {
  uint32_t size;
  if (!ExtractCommandSize(command, &size)) {
    LOG(ERROR) << "Command too small.";
    return command.size();
  }
  return size;
}

std::string TpmExecutorTpm1Impl::RunCommand(const std::string& command) {
  unsigned char* rbuffer = nullptr;
  uint32_t rlength;
  uint32_t rtotal = 0;

  CommandHeader header;
  if (!ExtractCommandHeader(command, &header)) {
    LOG(ERROR) << "Command too small.";
    return CreateCommandWithCode(TPM_SUCCESS);
  }

  if (header.code == TPM_ORD_SET_LOCALITY) {
    // Ignoring TPM_ORD_SET_LOCALITY command.
    return CreateCommandWithCode(TPM_SUCCESS);
  }

  std::string command_copy = command;
  unsigned char* command_ptr =
      reinterpret_cast<unsigned char*>(command_copy.data());

  TPM_RESULT res;
  res =
      TPMLIB_Process(&rbuffer, &rlength, &rtotal, command_ptr, command.size());

  return std::string(reinterpret_cast<char*>(rbuffer), rlength);
}

}  // namespace tpm2_simulator
