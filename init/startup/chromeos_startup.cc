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
#include "init/utils.h"

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

constexpr char kTPMOwnedPath[] = "sys/class/tpm/tmp0/device/owned";
// This file is created by clobber-state after the transition to dev mode.
constexpr char kDevModeFile[] = ".developer_mode";
// Flag file indicating that encrypted stateful should be preserved across
// TPM clear. If the file is present, it's expected that TPM is not owned.
constexpr char kPreservationRequestFile[] = "preservation_request";
// This file is created after the TPM is initialized and the device is owned.
constexpr char kInstallAttributesFile[] = "home/.shadow/install_attributes.pb";
// File used to trigger a stateful reset. Contains arguments for the
// clobber-state" call. This file may exist at boot time, as some use cases
// operate by creating this file with the necessary arguments and then
// rebooting.
constexpr char kResetFile[] = "factory_install_reset";

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

// Returns if the TPM is owned or couldn't determine.
bool ChromeosStartup::IsTPMOwned() {
  int output = 0;
  base::FilePath owned = root_.Append(kTPMOwnedPath);
  // Check file contents
  if (!utils::ReadFileToInt(owned, &output)) {
    PLOG(WARNING) << "Could not determine TPM owned, failed to read "
                  << kTPMOwnedPath;
    return true;
  }
  if (output == 0) {
    return false;
  }
  return true;
}

// Returns if device needs to clobber even though there's no devmode file
// present and boot is in verified mode.
bool ChromeosStartup::NeedsClobberWithoutDevModeFile() {
  base::FilePath preservation_request =
      stateful_.Append(kPreservationRequestFile);
  base::FilePath install_attrs = stateful_.Append(kInstallAttributesFile);
  struct stat statbuf;
  if (!IsTPMOwned() &&
      (!platform_->Stat(preservation_request, &statbuf) ||
       statbuf.st_uid != getuid()) &&
      (platform_->Stat(install_attrs, &statbuf) &&
       statbuf.st_uid == getuid())) {
    return true;
  }
  return false;
}

// Returns if the device is in transitioning between verified boot and dev mode.
// devsw_boot is the expected value of devsw_boot.
bool ChromeosStartup::IsDevToVerifiedModeTransition(int devsw_boot) {
  int boot;
  std::string dstr;
  return (cros_system_->GetInt("devsw_boot", &boot) && boot == devsw_boot) &&
         (cros_system_->GetString("mainfw_type", &dstr) && dstr != "recovery");
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

// Check for whether we need a stateful wipe, and alert the user as
// necessary.
void ChromeosStartup::CheckForStatefulWipe() {
  // We can wipe for several different reasons:
  //  + User requested "power wash" which will create kResetFile.
  //  + Switch from verified mode to dev mode. We do this if we're in
  //    dev mode, and kDevModeFile doesn't exist. clobber-state
  //    in this case will create the file, to prevent re-wipe.
  //  + Switch from dev mode to verified mode. We do this if we're in
  //    verified mode, and kDevModeFile still exists. (This check
  //    isn't necessarily reliable.)
  //
  // Stateful wipe for dev mode switching is skipped if the build is a debug
  // build or if we've booted a non-recovery image in recovery mode (for
  // example, doing Esc-F3-Power on a Chromebook with DEV-signed firmware);
  // this protects various development use cases, most especially prototype
  // units or booting Chromium OS on non-Chrome hardware. And because crossystem
  // is slow on some platforms, we want to do the additional checks only after
  // verified kDevModeFile existence.
  std::vector<const char*> clobber_args;
  struct stat stbuf;
  std::string boot_alert_msg;
  std::string clobber_log_msg;
  base::FilePath reset_file(kResetFile);
  if ((lstat(reset_file.value().c_str(), &stbuf) == 0 &&
       S_ISLNK(stbuf.st_mode)) ||
      base::PathExists(reset_file)) {
    boot_alert_msg = "power_wash";
    // If it's not a plain file owned by us, force a powerwash.
    if (stbuf.st_uid != getuid() || !S_ISREG(stbuf.st_mode)) {
      clobber_args.push_back("keepimg");
    } else {
      std::string str;
      if (!base::ReadFileToString(reset_file, &str)) {
        PLOG(WARNING) << "Failed to read reset file";
      } else {
        clobber_args.push_back(str.c_str());
      }
    }
    if (clobber_args.empty()) {
      clobber_args.push_back("keepimg");
    }
  } else if (state_dev_.empty()) {
    // No physical stateful partition available, usually due to initramfs
    // (recovery image, factory install shim or netboot). Do not wipe.
  } else if (IsDevToVerifiedModeTransition(0)) {
    bool res = platform_->Stat(dev_mode_allowed_file_, &stbuf);
    if ((res && stbuf.st_uid == getuid()) || NeedsClobberWithoutDevModeFile()) {
      // We're transitioning from dev mode to verified boot.
      // When coming back from developer mode, we don't need to
      // clobber as aggressively. Fast will do the trick.
      boot_alert_msg = "leave_dev";
      clobber_args.push_back("fast");
      clobber_args.push_back("keepimg");
      std::string msg;
      if (res && stbuf.st_uid == getuid()) {
        msg = "'Leave developer mode, dev_mode file present'";
      } else {
        msg = "'Leave developer mode, no dev_mode file'";
      }
      clobber_log_msg = msg;
      if (!clobber_args.empty()) {
        platform_->Clobber(boot_alert_msg, clobber_args, clobber_log_msg);
      }

      // Only fast clobber the non-protected paths in debug build to preserve
      // the testing tools.
      if (DevIsDebugBuild()) {
        DevUpdateStatefulPartition("clobber");
        utils::Reboot();
        exit(0);
      }
    }
  } else if (IsDevToVerifiedModeTransition(1)) {
    if (!platform_->Stat(dev_mode_allowed_file_, &stbuf) ||
        stbuf.st_uid != getuid()) {
      // We're transitioning from verified boot to dev mode.
      boot_alert_msg = "enter_dev";
      clobber_args.push_back("keepimg");
      clobber_log_msg = "Enter developer mode";

      // Only fast clobber the non-protected paths in debug build to preserve
      // the testing tools.
      if (DevIsDebugBuild()) {
        DevUpdateStatefulPartition("clobber");
        if (!PathExists(dev_mode_allowed_file_)) {
          if (!base::WriteFile(dev_mode_allowed_file_, "")) {
            PLOG(WARNING) << "Failed to create file: "
                          << dev_mode_allowed_file_.value();
          }
        }
        utils::Reboot();
        exit(0);
      }
    }
  }

  if (!clobber_args.empty()) {
    platform_->Clobber(boot_alert_msg, clobber_args, clobber_log_msg);
  }
}

// Main function to run chromeos_startup.
int ChromeosStartup::Run() {
  dev_mode_ = platform_->InDevMode(cros_system_.get());

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
  state_dev_ = stateful_mount_->GetStateDev();

  if (enable_stateful_security_hardening_) {
    // Block symlink traversal and opening of FIFOs on stateful. Note that we
    // set up exceptions for developer mode later on.
    BlockSymlinkAndFifo(root_, stateful_.value());
  }

  // Checks if developer mode is blocked.
  dev_mode_allowed_file_ = stateful_.Append(kDevModeFile);
  DevCheckBlockDevMode(dev_mode_allowed_file_);

  CheckForStatefulWipe();

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

// Check whether the device is allowed to boot in dev mode.
// 1. If a debug build is already installed on the system, ignore block_devmode.
//    It is pointless in this case, as the device is already in a state where
//    the local user has full control.
// 2. According to recovery mode only boot with signed images, the block_devmode
//    could be ignored here -- otherwise factory shim will be blocked especially
//    that RMA center can't reset this device.
void ChromeosStartup::DevCheckBlockDevMode(
    const base::FilePath& dev_mode_file) const {
  if (!dev_mode_) {
    return;
  }
  int devsw;
  int debug;
  int rec_reason;
  if (!cros_system_->GetInt("devsw_boot", &devsw) ||
      !cros_system_->GetInt("debug_build", &debug) ||
      !cros_system_->GetInt("recovery_reason", &rec_reason)) {
    LOG(WARNING) << "Failed to get boot information from crossystem";
    return;
  }
  if (!(devsw == 1 && debug == 0 && rec_reason == 0)) {
    DLOG(INFO) << "Debug build is already installed, ignore block_devmode";
    return;
  }

  // The file indicates the system has booted in developer mode and must
  // initiate a wiping process in the next (normal mode) boot.
  base::FilePath vpd_block_dir = root_.Append("sys/firmware/vpd/rw");
  base::FilePath vpd_block_file = vpd_block_dir.Append("block_devmode");
  bool block_devmode = false;

  // Checks ordered by run time.
  // 1. Try reading VPD through /sys.
  // 2. Try crossystem.
  // 3. Re-read VPD directly from SPI flash (slow!) but only for systems
  //    that don't have VPD in sysplatform and only when NVRAM indicates that it
  //    has been cleared.
  int crossys_block;
  int nvram;
  int val;
  if (utils::ReadFileToInt(vpd_block_file, &val) && val == 1) {
    block_devmode = true;
  } else if (cros_system_->GetInt("block_devmode", &crossys_block) &&
             crossys_block == 1) {
    block_devmode = true;
  } else if (!base::DirectoryExists(vpd_block_dir) &&
             cros_system_->GetInt("nvram_cleared", &nvram) && nvram == 1) {
    std::string output;
    std::vector<std::string> args = {"-i", "RW_VPD", "-g", "block_devmode"};
    if (platform_->VpdSlow(args, &output) && output == "1") {
      block_devmode = true;
    }
  }

  if (block_devmode) {
    // Put a flag file into place that will trigger a stateful partition wipe
    // after reboot in verified mode.
    if (!PathExists(dev_mode_file)) {
      base::WriteFile(dev_mode_file, "");
    }

    platform_->BootAlert("block_devmode");
  }
}

// Set dev_mode_ for tests.
void ChromeosStartup::SetDevMode(bool dev_mode) {
  dev_mode_ = dev_mode;
}

bool ChromeosStartup::DevIsDebugBuild() const {
  if (!dev_mode_) {
    return false;
  }
  return platform_->IsDebugBuild(cros_system_.get());
}

bool ChromeosStartup::DevUpdateStatefulPartition(const std::string& args) {
  if (!dev_mode_) {
    return true;
  }
  return stateful_mount_->DevUpdateStatefulPartition(args);
}

}  // namespace startup
