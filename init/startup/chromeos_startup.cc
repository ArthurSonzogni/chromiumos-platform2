// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#include <memory>
#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <brillo/blkdev_utils/lvm.h>
#include <brillo/process/process.h>
#include <brillo/userdb_utils.h>

#include "init/crossystem.h"
#include "init/crossystem_impl.h"
#include "init/startup/chromeos_startup.h"
#include "init/startup/constants.h"
#include "init/startup/flags.h"
#include "init/startup/platform_impl.h"
#include "init/startup/security_manager.h"
#include "init/startup/stateful_mount.h"

namespace {

constexpr char kTracingOn[] = "sys/kernel/tracing/tracing_on";

// The "/." ensures we trigger the automount, instead of just examining the
// mount point.
// TODO(b/244186883): remove this.
constexpr char kSysKernelDebugTracingDir[] = "sys/kernel/debug/tracing/.";

constexpr char kRunNamespaces[] = "run/namespaces";
constexpr char kSysKernelConfig[] = "sys/kernel/config";
constexpr char kSysKernelDebug[] = "sys/kernel/debug";
constexpr char kSysKernelSecurity[] = "sys/kernel/security";
constexpr char kSysKernelTracing[] = "sys/kernel/tracing";

constexpr char kDisableStatefulSecurityHard[] =
    "usr/share/cros/startup/disable_stateful_security_hardening";
constexpr char kDebugfsAccessGrp[] = "debugfs-access";

}  // namespace

namespace startup {

// Process the arguments from included USE flags.
void ChromeosStartup::ParseFlags(Flags* flags) {
  flags->direncryption = USE_DIRENCRYPTION;
  flags->fsverity = USE_FSVERITY;
  flags->prjquota = USE_PRJQUOTA;
  flags->encstateful = USE_ENCRYPTED_STATEFUL;
  if (flags->encstateful) {
    flags->sys_key_util = USE_TPM2;
  }
  // Note: encrypted_reboot_vault is disabled only for Gale
  // to be able to use openssl 1.1.1.
  flags->encrypted_reboot_vault = USE_ENCRYPTED_REBOOT_VAULT;
  flags->lvm_stateful = USE_LVM_STATEFUL_PARTITION;
}

// We manage this base timestamp by hand. It isolates us from bad clocks on
// the system where this image was built/modified, and on the runtime image
// (in case a dev modified random paths while the clock was out of sync).
// TODO(b/234157809): Our namespaces module doesn't support time namespaces
// currently. Add unittests for CheckClock once we add support.
void ChromeosStartup::CheckClock() {
  time_t cur_time;
  time(&cur_time);

  if (cur_time < kBaseSecs) {
    struct timespec stime;
    stime.tv_sec = kBaseSecs;
    stime.tv_nsec = 0;
    if (clock_settime(CLOCK_REALTIME, &stime) != 0) {
      // TODO(b/232901639): Improve failure reporting.
      PLOG(WARNING) << "Unable to set time.";
    }
  }
}

void ChromeosStartup::Sysctl() {
  // Initialize kernel sysctl settings early so that they take effect for boot
  // processes.
  brillo::ProcessImpl proc;
  proc.AddArg("/usr/sbin/sysctl");
  proc.AddArg("-q");
  proc.AddArg("--system");
  int status = proc.Run();
  if (status != 0) {
    LOG(WARNING) << "Failed to initialize kernel sysctl settings.";
  }
}

ChromeosStartup::ChromeosStartup(std::unique_ptr<CrosSystem> cros_system,
                                 const Flags& flags,
                                 const base::FilePath& root,
                                 const base::FilePath& stateful,
                                 const base::FilePath& lsb_file,
                                 const base::FilePath& proc_file,
                                 std::unique_ptr<Platform> platform)
    : cros_system_(std::move(cros_system)),
      flags_(flags),
      lsb_file_(lsb_file),
      proc_(proc_file),
      root_(root),
      stateful_(stateful),
      platform_(std::move(platform)) {}

void ChromeosStartup::EarlySetup() {
  gid_t debugfs_grp;
  if (!brillo::userdb::GetGroupInfo(kDebugfsAccessGrp, &debugfs_grp)) {
    PLOG(WARNING) << "Can't get gid for " << kDebugfsAccessGrp;
  } else {
    char data[25];
    snprintf(data, sizeof(data), "mode=0750,uid=0,gid=%d", debugfs_grp);
    const base::FilePath debug = root_.Append(kSysKernelDebug);
    if (!platform_->Mount("debugfs", debug, "debugfs", kCommonMountFlags,
                          data)) {
      // TODO(b/232901639): Improve failure reporting.
      PLOG(WARNING) << "Unable to mount " << debug.value();
    }
  }

  // HACK(b/244186883): ensure we trigger the /sys/kernel/debug/tracing
  // automount now (before we set 0755 below), because otherwise the kernel may
  // change its permissions whenever it eventually does get automounted.
  // TODO(b/244186883): remove this.
  struct stat st;
  const base::FilePath debug_tracing = root_.Append(kSysKernelDebugTracingDir);
  // Ignore errors.
  platform_->Stat(debug_tracing, &st);

  // Mount tracefs at /sys/kernel/tracing. On older kernels, tracing was part
  // of debugfs and was present at /sys/kernel/debug/tracing. Newer kernels
  // continue to automount it there when accessed via
  // /sys/kernel/debug/tracing/, but we avoid that where possible, to limit our
  // dependence on debugfs.
  const base::FilePath tracefs = root_.Append(kSysKernelTracing);
  // All users may need to access the tracing directory.
  if (!platform_->Mount("tracefs", tracefs, "tracefs", kCommonMountFlags,
                        "mode=0755")) {
    // TODO(b/232901639): Improve failure reporting.
    PLOG(WARNING) << "Unable to mount " << tracefs.value();
  }

  // /sys/kernel/tracing/tracing_on is 1 right after tracefs is initialized in
  // the kernel. Set it to a reasonable initial state of 0 after debugfs is
  // mounted. This needs to be done early during boot to avoid interference
  // with ureadahead that uses ftrace to build the list of files to preload in
  // the block cache. Android's init running in the ARC++ container sets this
  // file to 0, and we set it to 0 here so the the initial state of tracing_on
  // is always 0 regardless of ARC++.
  const base::FilePath tracing = root_.Append(kTracingOn);
  if (platform_->Stat(tracing, &st) && S_ISREG(st.st_mode)) {
    if (!base::WriteFile(tracing, "0")) {
      PLOG(WARNING) << "Failed to write to " << tracing.value();
    }
  }

  // Mount configfs, if present.
  const base::FilePath sys_config = root_.Append(kSysKernelConfig);
  if (base::DirectoryExists(sys_config)) {
    if (!platform_->Mount("configfs", sys_config, "configfs", kCommonMountFlags,
                          "")) {
      // TODO(b/232901639): Improve failure reporting.
      PLOG(WARNING) << "Unable to mount " << sys_config.value();
    }
  }

  // Mount securityfs as it is used to configure inode security policies below.
  const base::FilePath sys_security = root_.Append(kSysKernelSecurity);
  if (!platform_->Mount("securityfs", sys_security, "securityfs",
                        kCommonMountFlags, "")) {
    // TODO(b/232901639): Improve failure reporting.
    PLOG(WARNING) << "Unable to mount " << sys_security.value();
  }

  if (!SetupLoadPinVerityDigests(root_, platform_.get())) {
    LOG(WARNING) << "Failed to setup LoadPin verity digests.";
  }

  // Initialize kernel sysctl settings early so that they take effect for boot
  // processes.
  Sysctl();

  // Protect a bind mount to the Chrome mount namespace.
  const base::FilePath namespaces = root_.Append(kRunNamespaces);
  if (!platform_->Mount(namespaces, namespaces, "", MS_BIND, "") ||
      !platform_->Mount(base::FilePath(), namespaces, "", MS_PRIVATE, "")) {
    PLOG(WARNING) << "Unable to mount " << namespaces.value();
  }

  const base::FilePath disable_sec_hard =
      root_.Append(kDisableStatefulSecurityHard);
  enable_stateful_security_hardening_ = !base::PathExists(disable_sec_hard);
  if (!enable_stateful_security_hardening_ &&
      !ConfigureProcessMgmtSecurity(root_)) {
    PLOG(WARNING) << "Failed to configure process management security.";
  }
}

// Main function to run chromeos_startup.
int ChromeosStartup::Run() {
  // Make sure our clock is somewhat up-to-date. We don't need any resources
  // mounted below, so do this early on.
  CheckClock();

  // bootstat writes timings to tmpfs.
  bootstat_.LogEvent("pre-startup");

  EarlySetup();

  stateful_mount_ = std::make_unique<StatefulMount>(
      flags_, root_, stateful_, platform_.get(),
      std::make_unique<brillo::LogicalVolumeManager>());
  stateful_mount_->MountStateful();

  if (enable_stateful_security_hardening_) {
    // Block symlink traversal and opening of FIFOs on stateful. Note that we
    // set up exceptions for developer mode later on.
    BlockSymlinkAndFifo(root_, stateful_.value());
  }

  int ret = RunChromeosStartupScript();
  if (ret) {
    // TODO(b/232901639): Improve failure reporting.
    PLOG(WARNING) << "chromeos_startup.sh returned with code " << ret;
  }

  // Unmount securityfs so that further modifications to inode security
  // policies are not possible
  const base::FilePath kernel_sec = root_.Append(kSysKernelSecurity);
  if (!platform_->Umount(kernel_sec)) {
    PLOG(WARNING) << "Failed to umount: " << kernel_sec;
  }

  bootstat_.LogEvent("post-startup");

  return ret;
}

// Temporary function during the migration of the code. Run the bash
// version of chromeos_startup, which has been copied to chromeos_startup.sh
// to allow editing without effecting existing script. As more functionality
// moves to c++, it will be removed from chromeos_startup.sh.
int ChromeosStartup::RunChromeosStartupScript() {
  brillo::ProcessImpl proc;
  proc.AddArg("/sbin/chromeos_startup.sh");
  return proc.Run();
}

}  // namespace startup
