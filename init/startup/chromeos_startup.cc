// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "init/startup/chromeos_startup.h"

#include <fcntl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <filesystem>
#include <memory>
#include <utility>
#include <vector>

#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/functional/callback_helpers.h>
#include <base/logging.h>
#include <base/strings/string_split.h>
#include <base/strings/strcat.h>
#include <brillo/files/file_util.h>
#include <brillo/flag_helper.h>
#include <brillo/process/process.h>
#include <brillo/secure_blob.h>
#include <brillo/strings/string_utils.h>
#include <brillo/userdb_utils.h>
#include <libcrossystem/crossystem.h>
#include <libhwsec-foundation/tlcl_wrapper/tlcl_wrapper.h>
#include <libstorage/platform/platform.h>
#include <openssl/sha.h>
#include <vpd/vpd.h>

#include "init/encrypted_reboot_vault/encrypted_reboot_vault.h"
#include "init/file_attrs_cleaner.h"
#include "init/metrics/metrics.h"
#include "init/startup/constants.h"
#include "init/startup/factory_mode_mount_helper.h"
#include "init/startup/flags.h"
#include "init/startup/mount_helper.h"
#include "init/startup/mount_helper_factory.h"
#include "init/startup/security_manager.h"
#include "init/startup/standard_mount_helper.h"
#include "init/startup/startup_dep_impl.h"
#include "init/startup/stateful_mount.h"
#include "init/startup/test_mode_mount_helper.h"
#include "init/startup/uefi_startup.h"
#include "init/tpm_encryption/tpm.h"
#include "init/utils.h"

namespace {

constexpr char kHome[] = "home";
constexpr char kUnencrypted[] = "unencrypted";
constexpr char kVar[] = "var";
constexpr char kVarLog[] = "var/log";
constexpr char kChronos[] = "chronos";
constexpr char kUser[] = "user";
constexpr char kRoot[] = "root";

constexpr char kProcCmdline[] = "proc/cmdline";

constexpr int kVersionAttestationPcr = 13;

constexpr char kRunNamespaces[] = "run/namespaces";
constexpr char kRun[] = "run";
constexpr char kLock[] = "lock";
constexpr char kEmpty[] = "empty";
constexpr char kMedia[] = "media";
constexpr char kSysfs[] = "sys";

constexpr char kKernelConfig[] = "kernel/config";
constexpr char kKernelDebug[] = "kernel/debug";
constexpr char kKernelSecurity[] = "kernel/security";
constexpr char kKernelTracing[] = "kernel/tracing";
constexpr char kSysfsCpu[] = "devices/system/cpu";

constexpr char kTpmSimulator[] = "etc/init/tpm2-simulator.conf";

constexpr char kSELinuxEnforce[] = "fs/selinux/enforce";

constexpr char kBpf[] = "fs/bpf";
constexpr char kBpfAccessGrp[] = "bpf-access";

// This file is created by clobber-state after the transition to dev mode.
constexpr char kDevModeFile[] = ".developer_mode";
// Flag file indicating that encrypted stateful should be preserved across
// TPM clear. If the file is present, it's expected that TPM is not owned.
constexpr char kPreservationRequestFile[] = "preservation_request";
// This file is created after the TPM is owned/ready and before the
// enterprise enrollment.
constexpr char kCryptohomeKeyFile[] = "home/.shadow/cryptohome.key";
// This file should not exist on the newer system after the TPM is cleared.
constexpr char kEncStatefulNeedFinalizationFile[] =
    "encrypted.needs-finalization";
// File used to trigger a stateful reset. Contains arguments for the
// clobber-state" call. This file may exist at boot time, as some use cases
// operate by creating this file with the necessary arguments and then
// rebooting.
constexpr char kResetFile[] = "factory_install_reset";
// Flag file indicating that mount encrypted stateful failed last time.
// If the file is present and mount_encrypted failed again, machine would
// enter self-repair mode.
constexpr char kMountEncryptedFailedFile[] = "mount_encrypted_failed";
// Flag file indicating that PCR Extend operation failed.
// Currently this is for UMA/diagnostics, but in the future failure will
// result in reboot/self-repair.
constexpr char kVersionPCRExtendFailedFile[] = "version_pcr_extend_failed";
// kEncryptedStatefulMnt stores the path to the initial mount point for
// the encrypted stateful partition
constexpr char kEncryptedStatefulMnt[] = "encrypted";
// This value is threshold for determining that /var is full.
const int kVarFullThreshold = 10485760;

constexpr char kDaemonStore[] = "daemon-store";
constexpr char kDaemonStoreCache[] = "daemon-store-cache";
constexpr char kEtc[] = "etc";

constexpr char kDisableStatefulSecurityHard[] =
    "usr/share/cros/startup/disable_stateful_security_hardening";
constexpr char kDebugfsAccessGrp[] = "debugfs-access";

constexpr char kTpmFirmwareUpdateCleanup[] =
    "usr/sbin/tpm-firmware-update-cleanup";
constexpr char kTpmFirmwareUpdateRequestFlagFile[] =
    "unencrypted/preserve/tpm_firmware_update_request";

constexpr char kLibWhitelist[] = "lib/whitelist";
constexpr char kLibDevicesettings[] = "lib/devicesettings";

constexpr char kPreserve[] = "preserve";
const std::array<const char*, 4> kPreserveDirs = {
    "var/lib/servod",
    "usr/local/servod",
    "var/lib/device_health_profile",
    "usr/local/etc/wifi_creds",
};

}  // namespace

namespace startup {

// Process the arguments from included USE flags only.
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
  flags->lvm_migration = USE_LVM_MIGRATION;
  flags->lvm_stateful = USE_LVM_STATEFUL_PARTITION;

  flags->verbosity = 0;
}

// Process the arguments from included USE flags and command line arguments.
void ChromeosStartup::ParseFlags(Flags* flags, int argc, char* argv[]) {
  ParseFlags(flags);

  DEFINE_bool(v, false, "increase logging verbosity");
  DEFINE_bool(vv, false, "increase logging verbosity by two levels");
  brillo::FlagHelper::Init(argc, argv, "Tool run early during ChromeOS boot.");

  // It is ok that -v and -vv can be combined.
  flags->verbosity = (FLAGS_v ? 1 : 0) + (FLAGS_vv ? 2 : 0);
}

// We manage this base timestamp by hand. It isolates us from bad clocks on
// the system where this image was built/modified, and on the runtime image
// (in case a dev modified random paths while the clock was out of sync) or
// if the RTC is buggy or battery is dead.
// TODO(b/234157809): Our namespaces module doesn't support time namespaces
// currently. Add unittests for CheckClock once we add support.
void ChromeosStartup::CheckClock() {
  time_t cur_time;
  time(&cur_time);

  if (cur_time < kMinSecs || cur_time > kMaxSecs) {
    struct timespec stime;
    stime.tv_sec = kMinSecs;
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
  std::unique_ptr<brillo::Process> proc = platform_->CreateProcessInstance();
  proc->AddArg("/usr/sbin/sysctl");
  proc->AddArg("-q");
  proc->AddArg("--system");
  int status = proc->Run();
  if (status != 0) {
    LOG(WARNING) << "Failed to initialize kernel sysctl settings.";
  }
}

// Returns true if the TPM is owned or couldn't determine.
bool ChromeosStartup::IsTPMOwned() {
  encryption::Tpm tpm(tlcl_.get());
  bool owned;
  if (!tpm.IsOwned(&owned))
    return true;
  return owned;
}

// Returns if device needs to clobber even though there's no devmode file
// present and boot is in verified mode.
bool ChromeosStartup::NeedsClobberWithoutDevModeFile() {
  base::FilePath preservation_request =
      stateful_.Append(kPreservationRequestFile);
  base::FilePath cryptohome_key = stateful_.Append(kCryptohomeKeyFile);
  base::FilePath need_finalization =
      stateful_.Append(kEncStatefulNeedFinalizationFile);

  if (IsTPMOwned()) {
    return false;
  }

  if (platform_->FileExists(need_finalization)) {
    return true;
  }

  // This should only be supported on the non-TPM2 device.
  if (!USE_TPM2) {
    uid_t uid;
    if (platform_->GetOwnership(preservation_request, &uid, nullptr,
                                false /* follow_links */) &&
        (uid == getuid()))
      return false;
  }

  if (platform_->FileExists(cryptohome_key)) {
    return true;
  }

  return false;
}

// Returns true if the device is in transitioning between verified boot and dev
// mode. devsw_boot is the expected value of devsw_boot.
bool ChromeosStartup::IsDevToVerifiedModeTransition(int devsw_boot) {
  crossystem::Crossystem* crossystem = platform_->GetCrosssystem();
  std::optional<int> boot = crossystem->VbGetSystemPropertyInt(
      crossystem::Crossystem::kDevSwitchBoot);
  if (!boot || *boot != devsw_boot)
    return false;

  std::optional<std::string> dstr = crossystem->VbGetSystemPropertyString(
      crossystem::Crossystem::kMainFirmwareType);
  return dstr && dstr != "recovery";
}

// Walk the specified path and reset any file attributes (like immutable bit).
void ChromeosStartup::ForceCleanFileAttrs(const base::FilePath& path) {
  // No physical stateful partition available, usually due to initramfs
  // (recovery image, factory install shim or netboot. Do not check.
  if (state_dev_.empty()) {
    return;
  }

  std::vector<std::string> skip;
  bool status = file_attrs_cleaner::ScanDir(path.value(), skip);

  if (!status) {
    std::vector<std::string> args = {"keepimg", "preserve_lvs"};
    startup_dep_->Clobber(
        "self-repair", args,
        std::string("Bad file attrs under ").append(path.value()));
  }
}

// Checks if /var is close to being full.
// Returns true if there is less than 10MB of free space left in /var or if
// there are less than 100 inodes available on the underlying filesystem.
bool ChromeosStartup::IsVarFull() {
  struct statvfs st;
  base::FilePath var = root_.Append(kVar);
  if (!platform_->StatVFS(var, &st)) {
    PLOG(WARNING) << "Failed statvfs " << var.value();
    return false;
  }

  return (st.f_bavail < kVarFullThreshold / st.f_bsize || st.f_favail < 100);
}

ChromeosStartup::ChromeosStartup(
    std::unique_ptr<vpd::Vpd> vpd,
    const Flags& flags,
    const base::FilePath& root,
    const base::FilePath& stateful,
    const base::FilePath& lsb_file,
    libstorage::Platform* platform,
    StartupDep* startup_dep,
    std::unique_ptr<MountHelper> mount_helper,
    std::unique_ptr<hwsec_foundation::TlclWrapper> tlcl,
    init_metrics::InitMetrics* metrics)
    : platform_(platform),
      vpd_(std::move(vpd)),
      flags_(flags),
      lsb_file_(lsb_file),
      root_(root),
      stateful_(stateful),
      startup_dep_(startup_dep),
      mount_helper_(std::move(mount_helper)),
      tlcl_(std::move(tlcl)),
      metrics_(metrics) {
  stateful_mount_ = std::make_unique<StatefulMount>(
      flags_, root_, stateful_, platform_, startup_dep_, mount_helper_.get());
}

void ChromeosStartup::EarlySetup() {
  const base::FilePath sysfs = root_.Append(kSysfs);
  const base::FilePath empty;
  gid_t debugfs_grp;
  if (!brillo::userdb::GetGroupInfo(kDebugfsAccessGrp, &debugfs_grp)) {
    PLOG(WARNING) << "Can't get gid for " << kDebugfsAccessGrp;
  } else {
    char data[25];
    snprintf(data, sizeof(data), "mode=0750,uid=0,gid=%d", debugfs_grp);
    const base::FilePath debug = sysfs.Append(kKernelDebug);
    if (!platform_->Mount(empty, debug, "debugfs", kCommonMountFlags, data)) {
      // TODO(b/232901639): Improve failure reporting.
      PLOG(WARNING) << "Unable to mount " << debug.value();
    }
  }

  // Mount tracefs at /sys/kernel/tracing. On older kernels, tracing was part
  // of debugfs and was present at /sys/kernel/debug/tracing. Newer kernels
  // continue to automount it there when accessed via
  // /sys/kernel/debug/tracing/, but we avoid that where possible, to limit our
  // dependence on debugfs.
  const base::FilePath tracefs = sysfs.Append(kKernelTracing);
  // All users may need to access the tracing directory.
  if (!platform_->Mount(empty, tracefs, "tracefs", kCommonMountFlags,
                        "mode=0755")) {
    // TODO(b/232901639): Improve failure reporting.
    PLOG(WARNING) << "Unable to mount " << tracefs.value();
  }

  // Mount configfs, if present.
  const base::FilePath configfs = sysfs.Append(kKernelConfig);
  if (platform_->DirectoryExists(configfs)) {
    if (!platform_->Mount(empty, configfs, "configfs", kCommonMountFlags, "")) {
      // TODO(b/232901639): Improve failure reporting.
      PLOG(WARNING) << "Unable to mount " << configfs.value();
    }
  }

  // Mount bpffs for loading and pinning ebpf objects.
  gid_t bpffs_grp;
  if (!brillo::userdb::GetGroupInfo(kBpfAccessGrp, &bpffs_grp)) {
    PLOG(WARNING) << "Can't get gid for " << kBpfAccessGrp;
  } else {
    const std::string data =
        base::StrCat({"mode=0770,gid=", std::to_string(bpffs_grp)});
    const base::FilePath bpffs = sysfs.Append(kBpf);
    if (!platform_->Mount(empty, bpffs, "bpf", kCommonMountFlags,
                          data.c_str())) {
      // TODO(b/232901639): Improve failure reporting.
      PLOG(WARNING) << "Unable to mount " << bpffs.value();
    }
  }

  // Mount securityfs as it is used to configure inode security policies below.
  const base::FilePath securityfs = sysfs.Append(kKernelSecurity);
  if (!platform_->Mount(empty, securityfs, "securityfs", kCommonMountFlags,
                        "")) {
    // TODO(b/232901639): Improve failure reporting.
    PLOG(WARNING) << "Unable to mount " << securityfs.value();
  }

  if (!SetupLoadPinVerityDigests(platform_, root_, startup_dep_.get())) {
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
  enable_stateful_security_hardening_ =
      !platform_->FileExists(disable_sec_hard);
  if (enable_stateful_security_hardening_) {
    if (!ConfigureProcessMgmtSecurity(platform_, root_)) {
      PLOG(ERROR) << "Failed to configure process management security.";
    }
  } else {
    LOG(WARNING) << "Process management security disabled by flag file.";
  }
}

// Apply /mnt/stateful_partition specific tmpfiles.d configurations
void ChromeosStartup::TmpfilesConfiguration(
    const std::vector<std::string>& dirs) {
  std::unique_ptr<brillo::Process> tmpfiles =
      platform_->CreateProcessInstance();
  tmpfiles->AddArg("/usr/bin/systemd-tmpfiles");
  tmpfiles->AddArg("--create");
  tmpfiles->AddArg("--remove");
  tmpfiles->AddArg("--boot");
  for (std::string path : dirs) {
    tmpfiles->AddArg("--prefix");
    tmpfiles->AddArg(path);
  }
  if (tmpfiles->Run() != 0) {
    std::string msg =
        "tmpfiles.d failed for " + brillo::string_utils::Join(",", dirs);
    mount_helper_->CleanupMounts(msg);
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
  // is slow on some platforms, we want to do the additional checks only
  // after verified kDevModeFile existence.
  std::vector<std::string> clobber_args;
  std::string boot_alert_msg;
  std::string clobber_log_msg;
  base::FilePath reset_file = stateful_.Append(kResetFile);
  if (platform_->IsLink(reset_file) || platform_->FileExists(reset_file)) {
    boot_alert_msg = "power_wash";
    uid_t uid;
    platform_->GetOwnership(reset_file, &uid, nullptr,
                            false /* follow_links */);
    // If it's not a plain file owned by us, force a powerwash.
    if (uid != getuid() || platform_->IsLink(reset_file)) {
      clobber_log_msg =
          "Powerwash initiated by Reset file presence, but invalid";
    } else {
      std::string str;
      if (!platform_->ReadFileToString(reset_file, &str)) {
        PLOG(WARNING) << "Failed to read reset file";
        clobber_log_msg =
            "Powerwash initiated by Reset file presence, but unreadable";
      } else {
        clobber_log_msg = "Powerwash initiated by Reset file presence";
        std::vector<std::string> split_args = base::SplitString(
            str, " ", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
        for (const std::string& arg : split_args) {
          clobber_args.push_back(arg);
        }
      }
    }
    if (clobber_args.empty()) {
      clobber_args.push_back("keepimg");
    }
  } else if (state_dev_.empty()) {
    // No physical stateful partition available, usually due to initramfs
    // (recovery image, factory install shim or netboot). Do not wipe.
  } else if (IsDevToVerifiedModeTransition(0)) {
    uid_t uid;
    bool res = platform_->GetOwnership(dev_mode_allowed_file_, &uid, nullptr,
                                       false /* follow_links */);
    if ((res && uid == getuid()) || NeedsClobberWithoutDevModeFile()) {
      if (!DevIsDebugBuild()) {
        // We're transitioning from dev mode to verified boot.
        // When coming back from developer mode, we don't need to
        // clobber as aggressively. Fast will do the trick.
        boot_alert_msg = "leave_dev";
        clobber_args.push_back("fast");
        clobber_args.push_back("keepimg");
        std::string msg;
        if (res && uid == getuid()) {
          msg = "Leave developer mode, dev_mode file present";
        } else {
          msg = "Leave developer mode, no dev_mode file";
        }
        clobber_log_msg = msg;
      } else {
        // Only fast "clobber" the non-protected paths in debug build to
        // preserve the testing tools. We are not invoking clobber, cleaning up
        // stateful manually.
        clobber_log_msg = "Leave developer mode on a debug build";
        DevUpdateStatefulPartition("clobber");
      }
    }
  } else if (IsDevToVerifiedModeTransition(1)) {
    uid_t uid;
    bool res = platform_->GetOwnership(dev_mode_allowed_file_, &uid, nullptr,
                                       false /* follow_links */);
    if (!res || uid != getuid()) {
      if (!DevIsDebugBuild()) {
        // We're transitioning from verified boot to dev mode.
        boot_alert_msg = "enter_dev";
        clobber_args.push_back("keepimg");
        clobber_log_msg = "Enter developer mode";
      } else {
        // Only fast clobber the non-protected paths in debug build to preserve
        // the testing tools.
        clobber_log_msg = "Enter developer mode on a debug build";
        DevUpdateStatefulPartition("clobber");
        if (!platform_->FileExists(dev_mode_allowed_file_)) {
          if (!platform_->TouchFileDurable(dev_mode_allowed_file_)) {
            PLOG(WARNING) << "Failed to create file: "
                          << dev_mode_allowed_file_.value();
          }
        }
      }
    }
  }

  if (clobber_args.empty()) {
    if (!clobber_log_msg.empty()) {
      startup_dep_->ClobberLog(clobber_log_msg);
    }
  } else {
    startup_dep_->Clobber(boot_alert_msg, clobber_args, clobber_log_msg);
  }
}

// Mount /home.
void ChromeosStartup::MountHome() {
  const base::FilePath home = stateful_.Append(kHome);
  const base::FilePath home_root = root_.Append(kHome);
  mount_helper_->BindMountOrFail(home, home_root);
  // Remount /home with nosymfollow: bind mounts do not accept the option
  // within the same command.
  if (!platform_->Mount(base::FilePath(), home_root, "",
                        MS_REMOUNT | kCommonMountFlags | MS_NOSYMFOLLOW, "")) {
    PLOG(WARNING) << "Unable to remount " << home_root.value();
  }
}

// Start tpm2-simulator if it exists.
// TODO(b:261148112): Replace initctl call with logic to directly communicate
// with upstart.
void ChromeosStartup::StartTpm2Simulator() {
  base::FilePath tpm_simulator = root_.Append(kTpmSimulator);
  if (platform_->FileExists(tpm_simulator)) {
    std::unique_ptr<brillo::Process> ictl = platform_->CreateProcessInstance();
    ictl->AddArg("/sbin/initctl");
    ictl->AddArg("start");
    ictl->AddArg("tpm2-simulator");
    // Failure is fine, we just continue.
    ictl->Run();
  }
}

// Clean up after a TPM firmware update. This must happen before mounting
// stateful, which will initialize the TPM again.
void ChromeosStartup::CleanupTpm() {
  base::FilePath tpm_update_req =
      stateful_.Append(kTpmFirmwareUpdateRequestFlagFile);
  if (platform_->FileExists(tpm_update_req)) {
    base::FilePath tpm_cleanup = root_.Append(kTpmFirmwareUpdateCleanup);
    if (platform_->FileExists(tpm_cleanup)) {
      std::unique_ptr<brillo::Process> tpm_cleanup_process =
          platform_->CreateProcessInstance();
      tpm_cleanup_process->AddArg(tpm_cleanup.value());
      if (tpm_cleanup_process->Run() != 0) {
        PLOG(ERROR) << tpm_cleanup.value() << " failed.";
      }
    }
  }
}

bool ChromeosStartup::ExtendPCRForVersionAttestation() {
  if (USE_TPM_INSECURE_FALLBACK) {
    // Not needed on devices whereby the secure element is not mandatory.
    return true;
  }

  if (!USE_TPM2) {
    // Only TPM2.0 supported.
    return true;
  }

  base::FilePath cmdline_path = root_.Append(kProcCmdline);
  brillo::Blob cmdline;
  if (!platform_->ReadFile(cmdline_path, &cmdline)) {
    PLOG(WARNING) << "Failure to read /proc/cmdline for PCR Extension.";
    return false;
  }

  brillo::Blob digest(SHA256_DIGEST_LENGTH);
  SHA256(cmdline.data(), cmdline.size(), digest.data());

  if (tlcl_->Init() != 0) {
    PLOG(WARNING) << "Failure to init TlclWrapper.";
    return false;
  }

  base::ScopedClosureRunner close(base::BindOnce(
      [](hwsec_foundation::TlclWrapper* tlcl) {
        if (tlcl->Close() != 0) {
          PLOG(WARNING) << "Failed to shutdown TlclWrapper.";
        }
      },
      tlcl_.get()));

  if (tlcl_->Extend(kVersionAttestationPcr, digest, nullptr) != 0) {
    PLOG(WARNING) << "Failure to extend PCR with TlclWrapper.";
    return false;
  }

  return true;
}

// Move from /var/lib/whitelist to /var/lib/devicesettings if it is empty or
// non-existing. If /var/lib/devicesettings already exists, just remove
// /var/lib/whitelist.
// TODO(b/219506748): Remove the following lines by 2030 the latest. If there
// was a stepping stone to R99+ for all boards in between, or the number of
// devices using a version that did not have this code is less than the number
// of devices suffering from disk corruption, code can be removed earlier.
void ChromeosStartup::MoveToLibDeviceSettings() {
  base::FilePath whitelist = root_.Append(kVar).Append(kLibWhitelist);
  base::FilePath devicesettings = root_.Append(kVar).Append(kLibDevicesettings);
  // If the old whitelist dir still exists, try to migrate it.
  if (platform_->DirectoryExists(whitelist)) {
    if (platform_->IsDirectoryEmpty(whitelist)) {
      // If it is empty, delete it.
      if (!platform_->DeleteFile(whitelist)) {
        PLOG(WARNING) << "Failed to delete path " << whitelist.value();
      }
    } else if (platform_->DeleteFile(devicesettings)) {
      // If devicesettings didn't exist, or was empty, DeleteFile passed.
      // Rename the old path.
      if (!platform_->Rename(whitelist, devicesettings, false /* cros_fs */)) {
        PLOG(WARNING) << "Failed to move " << whitelist.value() << " to "
                      << devicesettings.value();
      }
    } else {
      // Both directories exist and are not empty. Do nothing.
      LOG(WARNING) << "Unable to move " << whitelist.value() << " to "
                   << devicesettings.value()
                   << ", both directories are not empty";
    }
  }
}

// Create daemon store folders.
// See
// https://chromium.googlesource.com/chromiumos/docs/+/HEAD/sandboxing.md#securely-mounting-daemon-store-folders.
void ChromeosStartup::CreateDaemonStore() {
  // Create /run/daemon-store and /run/daemon-store-cache based on
  // /etc/daemon-store.
  CreateDaemonStore(root_.Append(kRun).Append(kDaemonStore),
                    root_.Append(kEtc).Append(kDaemonStore));
  CreateDaemonStore(root_.Append(kRun).Append(kDaemonStoreCache),
                    root_.Append(kEtc).Append(kDaemonStore));
}

void ChromeosStartup::CreateDaemonStore(base::FilePath run_ds,
                                        base::FilePath etc_ds) {
  std::unique_ptr<libstorage::FileEnumerator> iter(platform_->GetFileEnumerator(
      etc_ds, false, base::FileEnumerator::FileType::DIRECTORIES));
  for (base::FilePath store = iter->Next(); !store.empty();
       store = iter->Next()) {
    base::FilePath rds = run_ds.Append(store.BaseName());
    if (!platform_->CreateDirectory(rds)) {
      PLOG(WARNING) << "mkdir failed for " << rds.value();
      continue;
    }
    if (!platform_->SetPermissions(rds, 0755)) {
      PLOG(WARNING) << "chmod failed for " << rds.value();
      continue;
    }
    platform_->Mount(rds, rds, "", MS_BIND, "");
    platform_->Mount(base::FilePath(), rds, "", MS_SHARED, "");
  }
}

// Remove /var/empty if it exists. Use /mnt/empty instead.
void ChromeosStartup::RemoveVarEmpty() {
  base::FilePath var_empty = root_.Append(kVar).Append(kEmpty);
  platform_->SetExtFileAttributes(var_empty, 0, FS_IMMUTABLE_FL);
  if (!platform_->DeletePathRecursively(var_empty)) {
    PLOG(WARNING) << "Failed to delete path " << var_empty.value();
  }
}

// Make sure that what gets written to /var/log stays in /var/log.
void ChromeosStartup::CheckVarLog() {
  base::FilePath varLog = root_.Append(kVarLog);
  std::unique_ptr<libstorage::FileEnumerator> var_iter(
      platform_->GetFileEnumerator(
          root_.Append(kVarLog), true,
          base::FileEnumerator::FileType::FILES |
              base::FileEnumerator::FileType::DIRECTORIES |
              base::FileEnumerator::FileType::SHOW_SYM_LINKS));
  for (base::FilePath path = var_iter->Next(); !path.empty();
       path = var_iter->Next()) {
    if (platform_->IsLink(path)) {
      base::FilePath realpath;
      if (!platform_->ReadLink(path, &realpath, true /* Normalize */) ||
          !varLog.IsParent(realpath)) {
        if (!platform_->DeleteFile(path)) {
          // Bail out and wipe on failure to remove a symlink.
          mount_helper_->CleanupMounts(
              "Failed to remove symlinks under /var/log");
        }
      }
    }
  }
}

// Restore file contexts for /var.
void ChromeosStartup::RestoreContextsForVar(
    void (*restorecon_func)(libstorage::Platform* platform_,
                            const base::FilePath& path,
                            const std::vector<base::FilePath>& exclude,
                            bool is_recursive,
                            bool set_digests)) {
  // Restore file contexts for /var.
  base::FilePath sysfs = root_.Append(kSysfs);
  base::FilePath selinux = sysfs.Append(kSELinuxEnforce);
  if (!platform_->FileExists(selinux)) {
    LOG(INFO) << selinux.value()
              << " does not exist, can not restore file contexts";
    return;
  }
  base::FilePath var = root_.Append(kVar);
  std::vector<base::FilePath> exc_empty;
  restorecon_func(platform_, var, exc_empty, true, true);

  // Restoring file contexts for sysfs. We only need to restore a sub directory
  // which requires regexp, because the kernel handles prefix match rules via
  // genfscon policy rules. Handling prefix match rules here in user space would
  // slow down boot significantly.
  base::FilePath sysfs_cpu = sysfs.Append(kSysfsCpu);
  restorecon_func(platform_, sysfs_cpu, exc_empty, true, false);

  // We cannot do recursive for .shadow since userdata is encrypted (including
  // file names) before user logs-in. Restoring context for it may mislabel
  // files if encrypted filename happens to match something.
  base::FilePath home = root_.Append(kHome);
  base::FilePath shadow = home.Append(".shadow");
  std::vector<base::FilePath> shadow_paths = {home, shadow};
  std::unique_ptr<libstorage::FileEnumerator> shadow_files(
      platform_->GetFileEnumerator(shadow, false,
                                   base::FileEnumerator::FileType::FILES, "*"));
  for (base::FilePath path = shadow_files->Next(); !path.empty();
       path = shadow_files->Next()) {
    shadow_paths.push_back(path);
  }
  std::unique_ptr<libstorage::FileEnumerator> shadow_dot(
      platform_->GetFileEnumerator(
          shadow, false, base::FileEnumerator::FileType::FILES, ".*"));
  for (base::FilePath path = shadow_dot->Next(); !path.empty();
       path = shadow_dot->Next()) {
    shadow_paths.push_back(path);
  }
  std::unique_ptr<libstorage::FileEnumerator> shadow_subdir(
      platform_->GetFileEnumerator(
          shadow, false, base::FileEnumerator::FileType::FILES, "*/*"));
  for (base::FilePath path = shadow_subdir->Next(); !path.empty();
       path = shadow_subdir->Next()) {
    shadow_paths.push_back(path);
  }
  for (auto path : shadow_paths) {
    restorecon_func(platform_, path, exc_empty, false, false);
  }

  // It's safe to recursively restorecon /home/{user,root,chronos} since
  // userdir is not bind-mounted here before logging in.
  std::array<base::FilePath, 3> h_paths = {
      home.Append(kUser), home.Append(kRoot), home.Append(kChronos)};
  for (auto h_path : h_paths) {
    restorecon_func(platform_, h_path, exc_empty, true, true);
  }
}

// Main function to run chromeos_startup.
int ChromeosStartup::Run() {
  crossystem::Crossystem* crossystem = platform_->GetCrosssystem();
  dev_mode_ = InDevMode(crossystem);

  // Make sure our clock is somewhat up-to-date. We don't need any resources
  // mounted below, so do this early on.
  CheckClock();

  // bootstat writes timings to tmpfs.
  bootstat_.LogEvent("pre-startup");

  EarlySetup();

  stateful_mount_->MountStateful();
  state_dev_ = stateful_mount_->GetStateDev();

  if (enable_stateful_security_hardening_) {
    // Block symlink traversal and opening of FIFOs on stateful. Note that we
    // set up exceptions for developer mode later on.
    BlockSymlinkAndFifo(platform_, root_, stateful_.value());
  }

  // Checks if developer mode is blocked.
  dev_mode_allowed_file_ = stateful_.Append(kDevModeFile);
  DevCheckBlockDevMode(dev_mode_allowed_file_);

  CheckForStatefulWipe();

  // Cleanup the file attributes in the unencrypted stateful directory.
  base::FilePath unencrypted = stateful_.Append(kUnencrypted);
  ForceCleanFileAttrs(unencrypted);

  std::vector<std::string> tmpfiles = {stateful_.value()};
  TmpfilesConfiguration(tmpfiles);

  MountHome();

  StartTpm2Simulator();

  CleanupTpm();

  base::FilePath encrypted_failed = stateful_.Append(kMountEncryptedFailedFile);
  if (!mount_helper_->DoMountVarAndHomeChronos()) {
    uid_t uid;
    if (!platform_->GetOwnership(encrypted_failed, &uid, nullptr,
                                 false /* follow_links */) ||
        (uid != getuid())) {
      platform_->TouchFileDurable(encrypted_failed);
    } else {
      crossystem->VbSetSystemPropertyInt("recovery_request", 1);
    }

    utils::Reboot();
    return 0;
  }

  if (platform_->FileExists(encrypted_failed))
    platform_->DeleteFile(encrypted_failed);

  base::FilePath pcr_extend_failed =
      stateful_.Append(kVersionPCRExtendFailedFile);
  if (!ExtendPCRForVersionAttestation()) {
    // At the moment we'll only log it but not force reboot or recovery.
    // TODO(b/278071784): Monitor if the failure occurs frequently and later
    // change this to reboot/send to recovery when it failed.
    platform_->TouchFileDurable(pcr_extend_failed);
  } else if (platform_->FileExists(pcr_extend_failed)) {
    platform_->DeleteFile(pcr_extend_failed);
  }

  base::FilePath encrypted_state_mnt = stateful_.Append(kEncryptedStatefulMnt);
  mount_helper_->RememberMount(encrypted_state_mnt);

  // Setup the encrypted reboot vault once the encrypted stateful partition
  // is available. If unlocking the encrypted reboot vault failed (due to
  // power loss/reboot/invalid vault), attempt to recreate the encrypted reboot
  // vault.
  if (flags_.encrypted_reboot_vault) {
    encrypted_reboot_vault::EncryptedRebootVault vault(platform_);
    if (!vault.UnlockVault())
      vault.CreateVault();
  }

  ForceCleanFileAttrs(root_.Append(kVar));
  ForceCleanFileAttrs(root_.Append(kHome).Append(kChronos));

  // If /var is too full, delete the logs so the device can boot successfully.
  // It is possible that the fullness of /var was not due to logs, but that
  // is very unlikely. If such a thing happens, we have a serious problem
  // which should not be covered up here.
  if (IsVarFull()) {
    brillo::DeletePathRecursively(root_.Append(kVarLog));
  }

  // Gather logs if needed. This might clear /var, so all init has to be after
  // this.
  DevGatherLogs();

  // Collect crash reports from early boot/mount failures.
  std::unique_ptr<brillo::Process> crash_reporter =
      platform_->CreateProcessInstance();
  crash_reporter->AddArg("/sbin/crash_reporter");
  crash_reporter->AddArg("--ephemeral_collect");
  if (!crash_reporter->Start()) {
    LOG(WARNING) << "Unable to collect early logs and crashes.";
  }

  if (enable_stateful_security_hardening_) {
    ConfigureFilesystemExceptions(platform_, root_);
  }

  std::vector<std::string> tmpfile_args = {root_.Append(kHome).value(),
                                           root_.Append(kVar).value()};
  TmpfilesConfiguration(tmpfile_args);

  MoveToLibDeviceSettings();

  MaybeRunUefiStartup(*UefiDelegate::Create(platform_, root_));

  // /run is tmpfs used for runtime data. Make sure /var/run and /var/lock
  // are bind-mounted to /run and /run/lock respectively for backwards
  // compatibility.
  // Bind mount /run to /var/run.
  const base::FilePath var = root_.Append(kVar);
  const base::FilePath root_run = root_.Append(kRun);
  mount_helper_->BindMountOrFail(root_run, var.Append(kRun));

  // Bind mount /run/lock to /var/lock.
  const base::FilePath root_run_lock = root_run.Append(kLock);
  mount_helper_->BindMountOrFail(root_run_lock, var.Append(kLock));

  CreateDaemonStore();

  RemoveVarEmpty();

  CheckVarLog();

  // MS_SHARED to give other namespaces access to mount points under /media.
  platform_->Mount(base::FilePath(kMedia), root_.Append(kMedia), "tmpfs",
                   MS_NOSUID | MS_NODEV | MS_NOEXEC, "");
  platform_->Mount(base::FilePath(), root_.Append(kMedia), "", MS_SHARED, "");

  std::vector<std::string> t_args = {root_.Append(kMedia).value()};
  TmpfilesConfiguration(t_args);

  RestoreContextsForVar(&utils::Restorecon);

  // Mount dev packages.
  DevMountPackages();
  RestorePreservedPaths();

  // Remount securityfs as readonly so that further modifications to inode
  // security policies are not possible but reading the kernel lockdown file is
  // still possible.
  const base::FilePath kernel_sec =
      root_.Append(kSysfs).Append(kKernelSecurity);
  if (!platform_->Mount(base::FilePath(), kernel_sec, "securityfs",
                        MS_REMOUNT | MS_RDONLY | kCommonMountFlags, "")) {
    PLOG(WARNING) << "Failed to remount " << kernel_sec << " as readonly.";
  }

  bootstat_.LogEvent("post-startup");

  return 0;
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
  crossystem::Crossystem* crossystem = platform_->GetCrosssystem();
  std::optional<int> devsw = crossystem->VbGetSystemPropertyInt(
      crossystem::Crossystem::kDevSwitchBoot);
  std::optional<int> debug =
      crossystem->VbGetSystemPropertyInt(crossystem::Crossystem::kDebugBuild);
  std::optional<int> rec_reason = crossystem->VbGetSystemPropertyInt(
      crossystem::Crossystem::kRecoveryReason);
  if (!devsw || !debug || !rec_reason) {
    LOG(WARNING) << "Failed to get boot information from crossystem";
    return;
  }
  if (!(devsw == 1 && debug == 0 && rec_reason == 0)) {
    DLOG(INFO) << "Debug build is already installed, ignore block_devmode";
    return;
  }

  bool block_devmode = false;
  // Checks ordered by run time.
  // 1. Try reading VPD through vpd library.
  // 2. Try crossystem.
  std::optional<std::string> val =
      vpd_->GetValue(vpd::VpdRw, crossystem::Crossystem::kBlockDevmode);
  if (val == "1") {
    block_devmode = true;
  } else {
    std::optional<int> crossys_block = crossystem->VbGetSystemPropertyInt(
        crossystem::Crossystem::kBlockDevmode);
    if (crossys_block == 1) {
      block_devmode = true;
    }
  }

  if (block_devmode) {
    // Put a flag file into place that will trigger a stateful partition wipe
    // after reboot in verified mode.
    if (!platform_->FileExists(dev_mode_file)) {
      platform_->TouchFileDurable(dev_mode_file);
    }

    startup_dep_->BootAlert("block_devmode");
  }
}

// Set dev_mode_ for tests.
void ChromeosStartup::SetDevMode(bool dev_mode) {
  dev_mode_ = dev_mode;
}

// Set dev_mode_allowed_file_ for tests.
void ChromeosStartup::SetDevModeAllowedFile(
    const base::FilePath& allowed_file) {
  dev_mode_allowed_file_ = allowed_file;
}

// Set state_dev_ for tests.
void ChromeosStartup::SetStateDev(const base::FilePath& state_dev) {
  state_dev_ = state_dev;
}

bool ChromeosStartup::DevIsDebugBuild() const {
  if (!dev_mode_) {
    return false;
  }
  return IsDebugBuild(platform_->GetCrosssystem());
}

bool ChromeosStartup::DevUpdateStatefulPartition(const std::string& args) {
  if (!dev_mode_) {
    return true;
  }
  return stateful_mount_->DevUpdateStatefulPartition(args);
}

void ChromeosStartup::DevGatherLogs() {
  if (dev_mode_) {
    stateful_mount_->DevGatherLogs(root_);
  }
}

void ChromeosStartup::DevMountPackages() {
  if (!dev_mode_) {
    return;
  }
  stateful_mount_->DevMountPackages();
}

void ChromeosStartup::RestorePreservedPaths() {
  if (!dev_mode_) {
    return;
  }
  base::FilePath preserve_dir =
      stateful_.Append(kUnencrypted).Append(kPreserve);
  for (const auto& path : kPreserveDirs) {
    base::FilePath src = preserve_dir.Append(path);
    if (platform_->DirectoryExists(src)) {
      const base::FilePath dst = root_.Append(path);
      platform_->CreateDirectory(dst);
      // |preserve_dir| is the unencrypted volume, |dst| is in the encrypted
      // volume, we need to cross filesystem boundaries.
      if (!platform_->Rename(src, dst, true)) {
        PLOG(WARNING) << "Failed to move " << src.value();
      }
    }
  }
}

}  // namespace startup
