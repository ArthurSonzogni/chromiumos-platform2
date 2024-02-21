// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
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
#include <brillo/blkdev_utils/lvm.h>
#include <brillo/blkdev_utils/storage_utils.h>
#include <brillo/files/file_util.h>
#include <brillo/key_value_store.h>
#include <brillo/process/process.h>
#include <libcrossystem/crossystem.h>

#include "init/startup/startup_dep_impl.h"
#include "init/utils.h"

namespace {

constexpr char kProcCmdLine[] = "proc/cmdline";
constexpr char kFactoryDir[] = "mnt/stateful_partition/dev_image/factory";

const size_t kMaxReadSize = 4 * 1024;

const int kNoResumeFromHibernatePending = 0x23;

}  // namespace

namespace startup {

bool StartupDep::Stat(const base::FilePath& path, struct stat* st) {
  return stat(path.value().c_str(), st) == 0;
}

bool StartupDep::Statvfs(const base::FilePath& path, struct statvfs* st) {
  return statvfs(path.value().c_str(), st) == 0;
}

bool StartupDep::Lstat(const base::FilePath& path, struct stat* st) {
  return lstat(path.value().c_str(), st) == 0;
}

bool StartupDep::Mount(const base::FilePath& src,
                       const base::FilePath& dst,
                       const std::string& type,
                       const unsigned long flags,  // NOLINT(runtime/int)
                       const std::string& data) {
  return mount(src.value().c_str(), dst.value().c_str(), type.c_str(), flags,
               data.c_str()) == 0;
}

bool StartupDep::Mount(const std::string& src,
                       const base::FilePath& dst,
                       const std::string& type,
                       const unsigned long flags,  // NOLINT(runtime/int)
                       const std::string& data) {
  return mount(src.c_str(), dst.value().c_str(), type.c_str(), flags,
               data.c_str()) == 0;
}

bool StartupDep::Umount(const base::FilePath& path) {
  return !umount(path.value().c_str());
}

base::ScopedFD StartupDep::Open(const base::FilePath& pathname, int flags) {
  return base::ScopedFD(HANDLE_EINTR(open(pathname.value().c_str(), flags)));
}

// NOLINTNEXTLINE(runtime/int)
int StartupDep::Ioctl(int fd, unsigned long request, int* arg1) {
  return ioctl(fd, request, arg1);
}

bool StartupDep::Fchown(int fd, uid_t owner, gid_t group) {
  return fchown(fd, owner, group) == 0;
}

int StartupDep::MountEncrypted(const std::vector<std::string>& args,
                               std::string* output) {
  brillo::ProcessImpl mount_enc;
  mount_enc.AddArg("/usr/sbin/mount-encrypted");
  for (auto arg : args) {
    mount_enc.AddArg(arg);
  }
  if (output) {
    mount_enc.RedirectOutputToMemory(true);
  }

  int status = mount_enc.Run();
  if (output) {
    *output = mount_enc.GetOutputString(STDOUT_FILENO);
  }
  return status;
}

void StartupDep::BootAlert(const std::string& arg) {
  brillo::ProcessImpl boot_alert;
  boot_alert.AddArg("/sbin/chromeos-boot-alert");
  boot_alert.AddArg(arg);
  int ret = boot_alert.Run();
  if (ret == 0) {
    return;
  } else if (ret < 0) {
    PLOG(ERROR) << "Failed to run chromeos-boot-alert";
  } else {
    LOG(WARNING) << "chromeos-boot-alert returned non zero exit code: " << ret;
  }
}

[[noreturn]] void StartupDep::Clobber(const std::vector<std::string> args) {
  brillo::ProcessImpl clobber;
  clobber.AddArg("/sbin/clobber-state");

  // Clobber should not be called with empty args, but to ensure that is
  // the case, use "keepimg" if nothing is specified.
  if (args.empty()) {
    clobber.AddArg("keepimg");
  } else {
    for (const std::string& arg : args) {
      clobber.AddArg(arg);
    }
  }

  int ret = clobber.Run();
  CHECK_NE(ret, 0);
  PLOG(ERROR) << "unable to run clobber-state; ret=" << ret;
  exit(1);
}

void StartupDep::ClobberLog(const std::string& msg) {
  brillo::ProcessImpl log;
  log.AddArg("/sbin/clobber-log");
  log.AddArg("--");
  log.AddArg(msg);
  if (log.Run() != 0) {
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

// Run command, cmd_path.
void StartupDep::RunProcess(const base::FilePath& cmd_path) {
  brillo::ProcessImpl proc;
  proc.AddArg(cmd_path.value());
  int res = proc.Run();
  if (res == 0) {
    return;
  } else if (res < 0) {
    PLOG(ERROR) << "Failed to run " << cmd_path.value();
  } else {
    LOG(WARNING) << "Process " << cmd_path.value()
                 << " returned non zero exit code: " << res;
  }
}

bool StartupDep::RunHiberman(const base::FilePath& output_file) {
  brillo::ProcessImpl hiberman;
  hiberman.AddArg("/sbin/minijail0");
  hiberman.AddArg("-v");
  hiberman.AddArg("--");
  hiberman.AddArg("/usr/sbin/hiberman");
  hiberman.AddArg("resume-init");
  hiberman.AddArg("-v");
  hiberman.RedirectOutput(output_file);
  int ret = hiberman.Run();
  if (ret == 0) {
    return true;
  } else if (ret < 0) {
    PLOG(ERROR) << "Failed to run hiberman";
    return false;
  }
  if (ret != kNoResumeFromHibernatePending)
    LOG(WARNING) << "hiberman returned non zero exit code: " << ret;
  return false;
}

void StartupDep::AddClobberCrashReport(const std::vector<std::string> args) {
  brillo::ProcessImpl crash;
  crash.AddArg("/sbin/crash_reporter");
  crash.AddArg("--early");
  crash.AddArg("--log_to_stderr");
  for (auto arg : args) {
    crash.AddArg(arg);
  }
  int ret = crash.Run();
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

std::optional<base::FilePath> StartupDep::GetRootDevicePartitionPath(
    const std::string& partition_label) {
  base::FilePath root_dev;
  if (!utils::GetRootDevice(&root_dev, /*strip_partition=*/true)) {
    LOG(WARNING) << "Unable to get root device";
    return std::nullopt;
  }

  const int esp_partition_num =
      utils::GetPartitionNumber(root_dev, partition_label);
  if (esp_partition_num == -1) {
    LOG(WARNING) << "Unable to get partition number for label "
                 << partition_label;
    return std::nullopt;
  }

  return brillo::AppendPartition(root_dev, esp_partition_num);
}

void StartupDep::ReplayExt4Journal(const base::FilePath& dev) {
  brillo::ProcessImpl e2fsck;
  e2fsck.AddArg("/sbin/e2fsck");
  e2fsck.AddArg("-p");
  e2fsck.AddArg("-E");
  e2fsck.AddArg("journal_only");
  e2fsck.AddArg(dev.value());
  int ret = e2fsck.Run();
  if (ret == 0) {
    return;
  } else if (ret < 0) {
    PLOG(WARNING) << "Failed to run e2fsck";
  } else {
    LOG(WARNING) << "e2fsck returned non zero exit code: " << ret;
  }
}

void StartupDep::ClobberLogRepair(const base::FilePath& dev,
                                  const std::string& msg) {
  brillo::ProcessImpl log_repair;
  log_repair.AddArg("/sbin/clobber-log");
  log_repair.AddArg("--repair");
  log_repair.AddArg(dev.value());
  log_repair.AddArg(msg);
  int status = log_repair.Run();
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
bool IsTestImage(const base::FilePath& lsb_file) {
  brillo::KeyValueStore store;
  if (!store.Load(lsb_file)) {
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

// Return if the device is in factory test mode.
bool IsFactoryTestMode(crossystem::Crossystem* crossystem,
                       const base::FilePath& base_dir) {
  // The path to factory enabled tag. If this path exists in a debug build,
  // we assume factory test mode.
  base::FilePath factory_dir = base_dir.Append(kFactoryDir);
  base::FilePath factory_tag = factory_dir.Append("enabled");
  struct stat statbuf;
  std::optional<int> res =
      crossystem->VbGetSystemPropertyInt(crossystem::Crossystem::kDebugBuild);
  if (res == 1 && stat(factory_tag.value().c_str(), &statbuf) == 0 &&
      S_ISREG(statbuf.st_mode)) {
    return true;
  }
  return false;
}

// Return if the device is in either in factory test mode or in factory
// installer mode.
bool IsFactoryMode(crossystem::Crossystem* crossystem,
                   const base::FilePath& base_dir) {
  if (IsFactoryTestMode(crossystem, base_dir))
    return true;

  std::string cmdline;
  if (!base::ReadFileToStringWithMaxSize(base_dir.Append(kProcCmdLine),
                                         &cmdline, kMaxReadSize)) {
    PLOG(ERROR) << "Failed to read proc command line";
    return false;
  }

  if (cmdline.find("cros_factory_install") != std::string::npos) {
    return true;
  }

  struct stat statbuf;
  base::FilePath installer = base_dir.Append("root/.factory_installer");
  if (stat(installer.value().c_str(), &statbuf) == 0 &&
      S_ISREG(statbuf.st_mode)) {
    return true;
  }
  return false;
}

}  // namespace startup
