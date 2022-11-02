// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <memory>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <brillo/process/process.h>

#include "init/clobber_state.h"
#include "init/crossystem.h"
#include "init/crossystem_impl.h"
#include "init/startup/platform_impl.h"
#include "init/utils.h"

namespace startup {

bool Platform::Stat(const base::FilePath& path, struct stat* st) {
  return stat(path.value().c_str(), st) == 0;
}

bool Platform::Mount(const base::FilePath& src,
                     const base::FilePath& dst,
                     const std::string& type,
                     const unsigned long flags,  // NOLINT(runtime/int)
                     const std::string& data) {
  return mount(src.value().c_str(), dst.value().c_str(), type.c_str(), flags,
               data.c_str()) == 0;
}

bool Platform::Mount(const std::string& src,
                     const base::FilePath& dst,
                     const std::string& type,
                     const unsigned long flags,  // NOLINT(runtime/int)
                     const std::string& data) {
  return mount(src.c_str(), dst.value().c_str(), type.c_str(), flags,
               data.c_str()) == 0;
}

bool Platform::Umount(const base::FilePath& path) {
  return !umount(path.value().c_str());
}

base::ScopedFD Platform::Open(const base::FilePath& pathname, int flags) {
  return base::ScopedFD(HANDLE_EINTR(open(pathname.value().c_str(), flags)));
}

// NOLINTNEXTLINE(runtime/int)
int Platform::Ioctl(int fd, unsigned long request, int* arg1) {
  return ioctl(fd, request, arg1);
}

void Platform::BootAlert(const std::string& arg) {
  brillo::ProcessImpl boot_alert;
  boot_alert.AddArg("/sbin/chromeos-boot-alert");
  boot_alert.AddArg(arg);
  int ret = boot_alert.Run();
  if (ret != 0) {
    PLOG(WARNING) << "chromeos-boot-alert failed with code " << ret;
  }
}

[[noreturn]] void Platform::Clobber(const std::vector<std::string> args) {
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

bool Platform::VpdSlow(const std::vector<std::string>& args,
                       std::string* output) {
  brillo::ProcessImpl vpd;
  vpd.AddArg("/usr/sbin/vpd");
  for (const std::string& arg : args) {
    vpd.AddArg(arg);
  }
  vpd.RedirectUsingMemory(STDOUT_FILENO);

  if (vpd.Run() == 0) {
    *output = vpd.GetOutputString(STDOUT_FILENO);
    return true;
  }
  return false;
}

void Platform::ClobberLog(const std::string& msg) {
  brillo::ProcessImpl log;
  log.AddArg("/sbin/clobber-log");
  log.AddArg("--");
  log.AddArg(msg);
  if (log.Run() != 0) {
    LOG(WARNING) << "clobber-log failed for message: " << msg;
  }
}

void Platform::Clobber(const std::string& boot_alert_msg,
                       const std::vector<std::string>& args,
                       const std::string& clobber_log_msg) {
  BootAlert(boot_alert_msg);
  ClobberLog(clobber_log_msg);
  Clobber(args);
}

void Platform::RemoveInBackground(const std::vector<base::FilePath>& paths) {
  pid_t pid = fork();
  if (pid == 0) {
    for (auto path : paths) {
      base::DeletePathRecursively(path);
    }
    exit(0);
  }
}

bool Platform::RunHiberman(const base::FilePath& output_file) {
  brillo::ProcessImpl hiberman;
  hiberman.AddArg("/usr/sbin/hiberman");
  hiberman.AddArg("resume-init");
  hiberman.AddArg("-v");
  hiberman.RedirectOutput(output_file.value());
  int ret = hiberman.Run();
  if (ret != 0) {
    PLOG(WARNING) << "hiberman failed with code " << ret;
    return false;
  }
  return true;
}

void Platform::AddClobberCrashReport(const std::string& dev) {
  brillo::ProcessImpl crash;
  crash.AddArg("crash_reporter");
  crash.AddArg("--early");
  crash.AddArg("--log_to_stderr");
  crash.AddArg("--mount_failure");
  crash.AddArg("--mount_device=" + dev);
  int ret = crash.Run();
  if (ret != 0) {
    PLOG(WARNING) << "crash_reporter failed with code " << ret;
    return;
  }

  // TODO(sarthakkukreti): Delete this since clobbering handles things.
  sync();
}

void Platform::ReplayExt4Journal(const base::FilePath& dev) {
  brillo::ProcessImpl e2fsck;
  e2fsck.AddArg("/sbin/e2fsck");
  e2fsck.AddArg("-p");
  e2fsck.AddArg("-E");
  e2fsck.AddArg("journal_only");
  e2fsck.AddArg(dev.value());
  int ret = e2fsck.Run();
  if (ret != 0) {
    PLOG(WARNING) << "e2fsck failed with code " << ret;
  }
}

void Platform::ClobberLogRepair(const base::FilePath& dev,
                                const std::string& msg) {
  brillo::ProcessImpl log_repair;
  log_repair.AddArg("/sbin/clobber-log");
  log_repair.AddArg("--repair");
  log_repair.AddArg(dev.value());
  log_repair.AddArg(msg);
  int status = log_repair.Run();
  if (status != 0) {
    PLOG(WARNING) << "Repairing clobber.log failed with code " << status;
  }
}

// Returns if we are running on a debug build.
bool Platform::IsDebugBuild(CrosSystem* const cros_system) {
  int debug;
  if (cros_system->GetInt("debug_build", &debug) && debug == 1) {
    return true;
  } else {
    return false;
  }
}

// Determine if the device is in dev mode.
bool Platform::InDevMode(CrosSystem* cros_system) {
  // cros_debug equals one if we've booted in developer mode or we've booted
  // a developer image.
  int debug;
  return (cros_system->GetInt("cros_debug", &debug) && debug == 1);
}

}  // namespace startup
