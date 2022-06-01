// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include <base/bind.h>
#include <base/callback_helpers.h>
#include <base/command_line.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/logging.h>
#include <base/process/launch.h>
#include <brillo/syslog_logging.h>
#include <libhwsec-foundation/tpm/tpm_version.h>
#include <linux/limits.h>
#include <rootdev/rootdev.h>
#include <tpm_manager-client/tpm_manager/dbus-constants.h>
#if USE_TPM2
#include <trunks/trunks_factory_impl.h>
#endif

#include "tpm_manager/server/dbus_service.h"
#include "tpm_manager/server/local_data_store.h"
#include "tpm_manager/server/local_data_store_impl.h"
#include "tpm_manager/server/tpm_manager_service.h"

namespace {

constexpr char kLogToStderrSwitch[] = "log_to_stderr";
constexpr char kNoPreinitFlagFile[] = "/run/tpm_manager/no_preinit";

constexpr char kIsRunningFromInstaller[] = "is_running_from_installer";
constexpr char kInstallerYes[] = "yes\n";

constexpr char kDevDir[] = "/dev/";
constexpr char kSysBlock[] = "/sys/block/";
constexpr char kRemovable[] = "removable";

std::string GetBootDeviceName() {
  char path[PATH_MAX];
  int ret = rootdev(path, sizeof(path), /* full resolution = */ true,
                    /* remove partition = */ true);
  if (ret != 0) {
    LOG(WARNING) << "rootdev failed with error code: " << ret;
    return "";
  }

  std::string boot_path(path);
  if (boot_path.substr(0, sizeof(kDevDir) - 1) != kDevDir) {
    LOG(WARNING) << "Unknown device prefix: " << boot_path;
    return "";
  }

  return boot_path.substr(sizeof(kDevDir) - 1);
}

bool IsBootFromRemoveableDevice() {
  base::FilePath file =
      base::FilePath(kSysBlock).Append(GetBootDeviceName()).Append(kRemovable);

  std::string file_content;

  if (!base::ReadFileToString(file, &file_content)) {
    return false;
  }

  std::string removable_str;
  base::TrimWhitespaceASCII(file_content, base::TRIM_ALL, &removable_str);

  int removable = 0;
  if (!base::StringToInt(removable_str, &removable)) {
    LOG(WARNING) << "removable is not a number: " << removable_str;
    return false;
  }

  return removable;
}

bool PreformPreinit() {
  if (base::PathExists(base::FilePath(kNoPreinitFlagFile))) {
    return false;
  }

  if (USE_OS_INSTALL_SERVICE) {
    // We should not preinit the TPM if we are running the OS from the
    // installer.
    std::string output;
    if (!base::GetAppOutput({kIsRunningFromInstaller}, &output)) {
      LOG(ERROR) << "Failed to run is_running_from_installer";
    }

    if (output == kInstallerYes) {
      return false;
    }

    return true;
  }

  // Normal ChromeOS case.
  if (IsBootFromRemoveableDevice()) {
    // Don't preform preinit when we are booting from removable device.
    // Because we may not store the data at correct location.
    return false;
  }

  return true;
}

}  // namespace

int main(int argc, char* argv[]) {
  base::CommandLine::Init(argc, argv);
  base::CommandLine* cl = base::CommandLine::ForCurrentProcess();
  int flags = brillo::kLogToSyslog;
  if (cl->HasSwitch(kLogToStderrSwitch)) {
    flags |= brillo::kLogToStderr;
  }
  brillo::InitLog(flags);

  tpm_manager::LocalDataStoreImpl local_data_store;
  bool perform_preinit = PreformPreinit();

  std::unique_ptr<tpm_manager::TpmManagerService> tpm_manager_service{
      new tpm_manager::TpmManagerService(perform_preinit, &local_data_store)};

  // From now on, the ownership of |tpm_manager_service| is transferred from
  // main function to |ipc_service|.
  tpm_manager::DBusService ipc_service(std::move(tpm_manager_service),
                                       &local_data_store);

  LOG(INFO) << "Starting TPM Manager...";
  return ipc_service.Run();
}
