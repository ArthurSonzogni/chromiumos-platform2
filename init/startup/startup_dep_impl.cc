// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "init/startup/startup_dep_impl.h"

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <memory>
#include <string>
#include <vector>

#include <base/containers/contains.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <brillo/blkdev_utils/storage_utils.h>
#include <brillo/files/file_util.h>
#include <brillo/key_value_store.h>
#include <brillo/process/process.h>
#include <libcrossystem/crossystem.h>
#include <libstorage/platform/platform.h>

#include "init/utils.h"

namespace {

constexpr char kProcCmdLine[] = "proc/cmdline";
constexpr char kFactoryDir[] = "mnt/stateful_partition/dev_image/factory";

}  // namespace

namespace startup {

StartupDep::StartupDep(libstorage::Platform* platform) : platform_(platform) {}

void StartupDep::BootAlert(const std::string& arg) {
  std::unique_ptr<brillo::Process> boot_alert =
      platform_->CreateProcessInstance();
  boot_alert->AddArg("/sbin/chromeos-boot-alert");
  boot_alert->AddArg(arg);
  int ret = boot_alert->Run();
  if (ret == 0) {
    return;
  } else if (ret < 0) {
    PLOG(ERROR) << "Failed to run chromeos-boot-alert";
  } else {
    LOG(WARNING) << "chromeos-boot-alert returned non zero exit code: " << ret;
  }
}

[[noreturn]] void StartupDep::Clobber(const std::vector<std::string>& args) {
  std::unique_ptr<brillo::Process> clobber = platform_->CreateProcessInstance();
  clobber->AddArg("/sbin/clobber-state");

  // Clobber should not be called with empty args, but to ensure that is
  // the case, use "keepimg" if nothing is specified.
  if (args.empty()) {
    clobber->AddArg("keepimg");
  } else {
    for (const std::string& arg : args) {
      clobber->AddArg(arg);
    }
  }

  int ret = clobber->Run();
  CHECK_NE(ret, 0);
  PLOG(ERROR) << "unable to run clobber-state; ret=" << ret;
  exit(1);
}

void StartupDep::ClobberLog(const std::string& msg) {
  std::unique_ptr<brillo::Process> log = platform_->CreateProcessInstance();
  log->AddArg("/sbin/clobber-log");
  log->AddArg("--");
  log->AddArg(msg);
  if (log->Run() != 0) {
    LOG(WARNING) << "clobber-log failed for message: " << msg;
  }
}

void StartupDep::Clobber(const std::string& boot_alert_msg,
                         const std::vector<std::string>& args,
                         const std::string& clobber_log_msg) {
  BootAlert(boot_alert_msg);
  ClobberLog(clobber_log_msg);
  Clobber(args);
}

void StartupDep::RemoveInBackground(const std::vector<base::FilePath>& paths) {
  pid_t pid = fork();
  if (pid == 0) {
    for (auto path : paths) {
      brillo::DeletePathRecursively(path);
    }
    exit(0);
  }
}

void StartupDep::AddClobberCrashReport(const std::vector<std::string> args) {
  std::unique_ptr<brillo::Process> crash = platform_->CreateProcessInstance();
  crash->AddArg("/sbin/crash_reporter");
  crash->AddArg("--early");
  crash->AddArg("--log_to_stderr");
  for (auto arg : args) {
    crash->AddArg(arg);
  }
  int ret = crash->Run();
  if (ret < 0) {
    PLOG(ERROR) << "Failed to run crash_reporter";
    return;
  } else if (ret != 0) {
    LOG(WARNING) << "crash_reporter returned non zero exit code: " << ret;
    return;
  }

  // TODO(sarthakkukreti): Delete this since clobbering handles things.
  sync();
}

void StartupDep::ClobberLogRepair(const base::FilePath& dev,
                                  const std::string& msg) {
  std::unique_ptr<brillo::Process> log_repair =
      platform_->CreateProcessInstance();
  log_repair->AddArg("/sbin/clobber-log");
  log_repair->AddArg("--repair");
  log_repair->AddArg(dev.value());
  log_repair->AddArg(msg);
  int status = log_repair->Run();
  if (status == 0) {
    return;
  } else if (status < 0) {
    PLOG(WARNING) << "Failed to run clobber-log";
  } else {
    LOG(WARNING) << "clobber-log returned non zero exit code: " << status;
  }
}

// Returns if we are running on a debug build.
bool IsDebugBuild(crossystem::Crossystem* crossystem) {
  std::optional<int> debug =
      crossystem->VbGetSystemPropertyInt(crossystem::Crossystem::kDebugBuild);
  return debug == 1;
}

// Determine if the device is in dev mode.
bool InDevMode(crossystem::Crossystem* crossystem) {
  // cros_debug equals one if we've booted in developer mode or we've booted
  // a developer image.
  std::optional<int> debug =
      crossystem->VbGetSystemPropertyInt(crossystem::Crossystem::kCrosDebug);
  return debug == 1;
}

// Determine if the device is using a test image.
bool IsTestImage(libstorage::Platform* platform,
                 const base::FilePath& lsb_file) {
  brillo::KeyValueStore store;
  std::string lsb_content;
  if (!platform->ReadFileToString(lsb_file, &lsb_content)) {
    PLOG(ERROR) << "Problem reading " << lsb_file.value();
    return false;
  }
  if (!store.LoadFromString(lsb_content)) {
    PLOG(ERROR) << "Problem parsing " << lsb_file.value();
    return false;
  }
  std::string value;
  if (!store.GetString("CHROMEOS_RELEASE_TRACK", &value)) {
    PLOG(ERROR) << "CHROMEOS_RELEASE_TRACK not found in " << lsb_file.value();
    return false;
  }
  return base::StartsWith(value, "test", base::CompareCase::SENSITIVE);
}

// Return if the device is in either in factory test mode or in factory
// installer mode.
bool IsFactoryMode(libstorage::Platform* platform,
                   const base::FilePath& base_dir) {
  // The path to factory enabled tag. If this path exists in a debug build,
  // we assume factory test mode.
  base::FilePath factory_dir = base_dir.Append(kFactoryDir);
  base::FilePath factory_tag = factory_dir.Append("enabled");
  std::optional<int> res = platform->GetCrosssystem()->VbGetSystemPropertyInt(
      crossystem::Crossystem::kDebugBuild);
  if (res == 1 && platform->FileExists(factory_tag))
    return true;

  std::string cmdline;
  if (!platform->ReadFileToString(base_dir.Append(kProcCmdLine), &cmdline)) {
    PLOG(ERROR) << "Failed to read proc command line";
    return false;
  }

  if (cmdline.find("cros_factory_install") != std::string::npos) {
    return true;
  }

  base::FilePath installer = base_dir.Append("root/.factory_installer");
  return platform->FileExists(installer);
}

}  // namespace startup
