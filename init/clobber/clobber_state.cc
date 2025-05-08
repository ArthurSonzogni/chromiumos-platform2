// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "init/clobber/clobber_state.h"

#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <unistd.h>

#include <base/check.h>

// Keep after <sys/mount.h> to avoid build errors.
#include <linux/fs.h>

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <base/bits.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/functional/callback_helpers.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <brillo/blkdev_utils/get_backing_block_device.h>
#include <brillo/blkdev_utils/lvm.h>
#include <brillo/blkdev_utils/storage_device.h>
#include <brillo/blkdev_utils/storage_utils.h>
#include <brillo/cryptohome.h>
#include <brillo/files/file_util.h>
#include <brillo/process/process.h>
#include <chromeos/constants/imageloader.h>
#include <chromeos/secure_erase_file/secure_erase_file.h>
#include <libcrossystem/crossystem.h>
#include <libdlcservice/utils.h>
#include <libstorage/platform/platform.h>
#include <rootdev/rootdev.h>

#include "init/clobber/clobber_state_log.h"
#include "init/encrypted_reboot_vault/encrypted_reboot_vault.h"
#include "init/libpreservation/preservation.h"
#include "init/utils.h"

namespace {

constexpr char kStatefulPath[] = "/mnt/stateful_partition";
constexpr char kPowerWashCountPath[] = "unencrypted/preserve/powerwash_count";
constexpr char kLastPowerWashTimePath[] =
    "unencrypted/preserve/last_powerwash_time";
constexpr char kBioWashPath[] = "/usr/bin/bio_wash";
constexpr char kPreservedFilesTarPath[] = "/tmp/preserve.tar";
constexpr char kStatefulClobberLogPath[] = "unencrypted/clobber.log";
constexpr char kMountEncryptedPath[] = "/usr/sbin/mount-encrypted";
constexpr char kRollbackFileForPstorePath[] =
    "/var/lib/oobe_config_save/data_for_pstore";
constexpr char kPstoreInputPath[] = "/dev/pmsg0";
// Keep file names in sync with update_engine prefs.
const char* kUpdateEnginePrefsFiles[] = {"last-active-ping-day",
                                         "last-roll-call-ping-day"};
constexpr char kUpdateEnginePrefsPath[] = "var/lib/update_engine/prefs/";
constexpr char kUpdateEnginePreservePath[] =
    "unencrypted/preserve/update_engine/prefs/";
// CrOS Private Computing (go/chromeos-data-pc) will save the device last
// active dates in different use cases into a file.
constexpr char kPsmDeviceActiveLocalPrefPath[] =
    "var/lib/private_computing/last_active_dates";
constexpr char kPsmDeviceActivePreservePath[] =
    "unencrypted/preserve/last_active_dates";
constexpr char kFlexLocalPath[] = "var/lib/flex_id/";
constexpr char kFlexPreservePath[] = "unencrypted/preserve/flex/";
const char* kFlexFiles[] = {"flex_id", "flex_state_key"};
constexpr base::TimeDelta kMinClobberDuration = base::Minutes(5);

// The presence of this file indicates that crash report collection across
// clobber is disabled in developer mode.
constexpr char kDisableClobberCrashCollectionPath[] =
    "/run/disable-clobber-crash-collection";
// The presence of this file indicates that the kernel supports ext4 directory
// level encryption.
constexpr char kExt4DircryptoSupportedPath[] =
    "/sys/fs/ext4/features/encryption";

// Attempt to save logs from the boot when the clobber happened into the
// stateful partition.
void CollectClobberCrashReports() {
  brillo::ProcessImpl crash_reporter_early_collect;
  crash_reporter_early_collect.AddArg("/sbin/crash_reporter");
  crash_reporter_early_collect.AddArg("--early");
  crash_reporter_early_collect.AddArg("--log_to_stderr");
  crash_reporter_early_collect.AddArg("--preserve_across_clobber");
  crash_reporter_early_collect.AddArg("--boot_collect");
  if (crash_reporter_early_collect.Run() != 0) {
    LOG(WARNING) << "Unable to collect logs and crashes from current run.";
  }

  return;
}

bool BindMountVarAndHome() {
  std::vector<std::pair<base::FilePath, base::FilePath>> bind_mounts = {
      {base::FilePath("/mnt/stateful_partition/var"), base::FilePath("/var")},
      {base::FilePath("/mnt/stateful_partition/home"),
       base::FilePath("/home")}};

  for (auto& [source, target] : bind_mounts) {
    if (!base::PathExists(source)) {
      LOG(WARNING) << "Source path " << source.value()
                   << " does not exist, skipping bind mount.";
      continue;
    }
    if (mount(source.value().c_str(), target.value().c_str(), nullptr, MS_BIND,
              nullptr) != 0) {
      PLOG(ERROR) << "Failed to bind mount " << source.value() << " to "
                  << target.value();
      return false;
    }
    LOG(INFO) << "Bind mounted " << source.value() << " to " << target.value();
  }

  return true;
}

bool UnmountVarAndHome() {
  std::vector<base::FilePath> targets = {base::FilePath("/var"),
                                         base::FilePath("/home")};
  for (const auto& target : targets) {
    if (umount(target.value().c_str()) != 0) {
      PLOG(ERROR) << "Failed to unmount " << target.value();
      continue;
    }
    LOG(INFO) << "Unmounted " << target.value();
  }
  return true;
}

bool MountEncryptedStateful() {
  libstorage::Platform platform;
  // For default-key-stateful layout, encrypted stateful is simply bind mounts
  // from the /mnt/stateful_partition.
  // Note: this assumes that DEFAULT_KEY_STATEFUL is enabled only on devices
  // with LVM_STATEFUL_PARTITION enabled.
  if (USE_DEFAULT_KEY_STATEFUL &&
      !platform.IsStatefulLogicalVolumeSupported()) {
    return BindMountVarAndHome();
  }
  brillo::ProcessImpl mount_encstateful;
  mount_encstateful.AddArg(kMountEncryptedPath);
  if (mount_encstateful.Run() != 0) {
    PLOG(ERROR) << "Failed to mount encrypted stateful.";
    return false;
  }
  return true;
}

void UnmountEncryptedStateful() {
  libstorage::Platform platform;
  // For default-key-stateful layout, encrypted stateful is simply bind mounts
  // from the /mnt/stateful_partition.
  // Note: this assumes that DEFAULT_KEY_STATEFUL is enabled only on devices
  // with LVM_STATEFUL_PARTITION enabled.
  if (USE_DEFAULT_KEY_STATEFUL &&
      !platform.IsStatefulLogicalVolumeSupported()) {
    UnmountVarAndHome();
    return;
  }
  for (int attempts = 0; attempts < 10; ++attempts) {
    brillo::ProcessImpl umount_encstateful;
    umount_encstateful.AddArg(kMountEncryptedPath);
    umount_encstateful.AddArg("umount");
    if (umount_encstateful.Run()) {
      return;
    }
  }
  PLOG(ERROR) << "Failed to unmount encrypted stateful.";
}

void UnmountStateful(const base::FilePath& stateful) {
  LOG(INFO) << "Unmounting stateful partition";
  for (int attempts = 0; attempts < 10; ++attempts) {
    int ret = umount(stateful.value().c_str());
    if (ret) {
      // Disambiguate failures from busy or already unmounted stateful partition
      // from other generic failures.
      if (errno == EBUSY) {
        PLOG(ERROR) << "Failed to unmount busy stateful partition";
        base::PlatformThread::Sleep(base::Milliseconds(200));
        continue;
      } else if (errno != EINVAL) {
        PLOG(ERROR) << "Unable to unmount " << stateful;
      } else {
        PLOG(INFO) << "Stateful partition already unmounted";
      }
    }
    return;
  }
}

void MoveRollbackFileToPstore() {
  const base::FilePath file_for_pstore(kRollbackFileForPstorePath);

  std::string data;
  if (!base::ReadFileToString(file_for_pstore, &data)) {
    if (errno != ENOENT) {
      PLOG(ERROR) << "Failed to read rollback data for pstore.";
    }
    return;
  }

  if (!base::AppendToFile(base::FilePath(kPstoreInputPath), data + "\n")) {
    if (errno == ENOENT) {
      PLOG(WARNING)
          << "Could not write rollback data because /dev/pmsg0 does not exist.";
    } else {
      PLOG(ERROR) << "Failed to write rollback data to pstore.";
    }
  }
  // The rollback file will be lost on tpm reset, so we do not need to
  // delete it manually.
}

}  // namespace

// static
ClobberState::Arguments ClobberState::ParseArgv(
    int argc, char const* const argv[], bool metadata_partition_needed) {
  Arguments args;
  if (argc <= 1) {
    return args;
  }

  // Due to historical usage, the command line parsing is a bit weird.
  // We split the first argument into multiple keywords.
  std::vector<std::string> split_args = base::SplitString(
      argv[1], " ", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  for (int i = 2; i < argc; ++i) {
    split_args.push_back(argv[i]);
  }

  for (const std::string& arg : split_args) {
    if (arg == "factory") {
      args.factory_wipe = true;
      // Factory mode implies fast wipe.
      args.fast_wipe = true;
    } else if (arg == "fast") {
      args.fast_wipe = true;
    } else if (arg == "keepimg") {
      args.keepimg = true;
    } else if (arg == "safe") {
      args.safe_wipe = true;
    } else if (arg == "rollback") {
      args.rollback_wipe = true;
    } else if (base::StartsWith(
                   arg, "reason=", base::CompareCase::INSENSITIVE_ASCII)) {
      args.reason = arg;
    } else if (arg == "rma") {
      args.rma_wipe = true;
    } else if (arg == "ad_migration") {
      args.ad_migration_wipe = true;
    } else if (arg == "preserve_lvs") {
      args.preserve_lvs = USE_LVM_STATEFUL_PARTITION;
    } else if (arg == "disable_lvm_install" || USE_DISABLE_LVM_INSTALL) {
      args.disable_lvm_install = true;
    } else if (arg == "default_key_migration_wipe") {
      args.default_key_migration_wipe = true;
      args.fast_wipe = true;
      args.keepimg = true;
      args.safe_wipe = true;
      args.disable_lvm_install = true;
    } else if (arg == "dry_run") {
      args.dry_run = true;
    }
  }

  // Use default key wipe if enabled for the board.
  if (USE_DEFAULT_KEY_STATEFUL && metadata_partition_needed) {
    args.default_key_migration_wipe = true;
    args.disable_lvm_install = true;
  }

  if (args.disable_lvm_install) {
    args.preserve_lvs = false;
  }

  LOG(INFO) << "Factory wipe: " << args.factory_wipe;
  LOG(INFO) << "Fast wipe: " << args.fast_wipe;
  LOG(INFO) << "Keepimg: " << args.keepimg;
  LOG(INFO) << "Safe wipe: " << args.safe_wipe;
  LOG(INFO) << "Rollback wipe: " << args.rollback_wipe;
  LOG(INFO) << "Reason: " << args.reason;
  LOG(INFO) << "RMA wipe: " << args.rma_wipe;
  LOG(INFO) << "AD migration wipe: " << args.ad_migration_wipe;
  LOG(INFO) << "Disable LVM install: " << args.disable_lvm_install;
  LOG(INFO) << "Default key migration wipe: "
            << args.default_key_migration_wipe;
  LOG(INFO) << "Dry run: " << args.dry_run;

  return args;
}

// static
bool ClobberState::IncrementFileCounter(const base::FilePath& path) {
  int value;
  if (!utils::ReadFileToInt(path, &value) || value < 0 || value >= INT_MAX) {
    return base::WriteFile(path, "1\n");
  }

  std::string new_value = std::to_string(value + 1);
  new_value.append("\n");
  return base::WriteFile(path, new_value);
}

// static
bool ClobberState::WriteLastPowerwashTime(const base::FilePath& path,
                                          const base::Time& time) {
  return base::WriteFile(path, base::StringPrintf("%ld\n", time.ToTimeT()));
}

// static
int ClobberState::PreserveFiles(
    const base::FilePath& preserved_files_root,
    const std::vector<base::FilePath>& preserved_files,
    const base::FilePath& tar_file_path) {
  // Remove any stale tar files from previous clobber-state runs.
  brillo::DeleteFile(tar_file_path);

  // We want to preserve permissions and recreate the directory structure
  // for all of the files in |preserved_files|. In order to do so we run tar
  // --no-recursion and specify the names of each of the parent directories.
  // For example for home/.shadow/install_attributes.pb
  // we pass to tar home, home/.shadow, home/.shadow/install_attributes.pb.
  std::vector<std::string> paths_to_tar;
  for (const base::FilePath& path : preserved_files) {
    // All paths should be relative to |preserved_files_root|.
    if (path.IsAbsolute()) {
      LOG(WARNING) << "Non-relative path " << path.value()
                   << " passed to PreserveFiles, ignoring.";
      continue;
    }
    if (!base::PathExists(preserved_files_root.Append(path))) {
      continue;
    }
    base::FilePath current = path;
    while (current != base::FilePath(base::FilePath::kCurrentDirectory)) {
      // List of paths is built in an order that is reversed from what we want
      // (parent directories first), but will then be passed to tar in reverse
      // order.
      //
      // e.g. for home/.shadow/install_attributes.pb, |paths_to_tar| will have
      // home/.shadow/install_attributes.pb, then home/.shadow, then home.
      paths_to_tar.push_back(current.value());
      current = current.DirName();
    }
  }

  // We can't create an empty tar file.
  if (paths_to_tar.size() == 0) {
    LOG(INFO)
        << "PreserveFiles found no files to preserve, no tar file created.";
    return 0;
  }

  brillo::ProcessImpl tar;
  tar.AddArg("/bin/tar");
  tar.AddArg("-c");
  tar.AddStringOption("-f", tar_file_path.value());
  tar.AddStringOption("-C", preserved_files_root.value());
  tar.AddArg("--no-recursion");
  tar.AddArg("--");

  // Add paths in reverse order because we built up the list of paths backwards.
  for (auto it = paths_to_tar.rbegin(); it != paths_to_tar.rend(); ++it) {
    tar.AddArg(*it);
  }
  return tar.Run();
}

// static
bool ClobberState::GetDevicesToWipe(
    const base::FilePath& root_disk,
    const base::FilePath& root_device,
    const ClobberWipe::PartitionNumbers& partitions,
    ClobberState::DeviceWipeInfo* wipe_info_out) {
  if (!wipe_info_out) {
    LOG(ERROR) << "wipe_info_out must be non-null";
    return false;
  }

  if (partitions.root_a < 0 || partitions.root_b < 0 ||
      partitions.kernel_a < 0 || partitions.kernel_b < 0 ||
      partitions.stateful < 0) {
    LOG(ERROR) << "Invalid partition numbers for GetDevicesToWipe";
    return false;
  }

  if (root_disk.empty()) {
    LOG(ERROR) << "Invalid root disk for GetDevicesToWipe";
    return false;
  }

  if (root_device.empty()) {
    LOG(ERROR) << "Invalid root device for GetDevicesToWipe";
    return false;
  }

  std::string base_device;
  int active_root_partition;
  if (!utils::GetDevicePathComponents(root_device, &base_device,
                                      &active_root_partition)) {
    LOG(ERROR) << "Extracting partition number and base device from "
                  "root_device failed: "
               << root_device.value();
    return false;
  }

  ClobberState::DeviceWipeInfo wipe_info;
  if (active_root_partition == partitions.root_a) {
    wipe_info.inactive_root_device =
        base::FilePath(base_device + std::to_string(partitions.root_b));
    wipe_info.inactive_kernel_device =
        base::FilePath(base_device + std::to_string(partitions.kernel_b));
    wipe_info.active_kernel_partition = partitions.kernel_a;
  } else if (active_root_partition == partitions.root_b) {
    wipe_info.inactive_root_device =
        base::FilePath(base_device + std::to_string(partitions.root_a));
    wipe_info.inactive_kernel_device =
        base::FilePath(base_device + std::to_string(partitions.kernel_a));
    wipe_info.active_kernel_partition = partitions.kernel_b;
  } else {
    LOG(ERROR) << "Active root device partition number ("
               << active_root_partition
               << ") does not match either root partition number: "
               << partitions.root_a << ", " << partitions.root_b;
    return false;
  }

  base::FilePath kernel_device;
  wipe_info.stateful_partition_device =
      base::FilePath(base_device + std::to_string(partitions.stateful));

  if (active_root_partition == partitions.root_a) {
    kernel_device =
        base::FilePath(base_device + std::to_string(partitions.kernel_a));
  } else if (active_root_partition == partitions.root_b) {
    kernel_device =
        base::FilePath(base_device + std::to_string(partitions.kernel_b));
  }

  if (USE_DEFAULT_KEY_STATEFUL && partitions.cros_metadata != -1) {
    wipe_info.cros_metadata_device =
        base::FilePath(base_device + std::to_string(partitions.cros_metadata));
  }

  *wipe_info_out = wipe_info;
  return true;
}

// static
void ClobberState::RemoveVpdKeys() {
  constexpr std::array<const char*, 2> keys_to_remove{
      // This key is used for caching the feature level.
      // Need to remove it, as it must be recalculated when re-entering normal
      // mode.
      "feature_device_info",
      // This key is used to store LVM migration status.
      // Need to remove this to prevent incoherence.
      "thinpool_migration_status",
  };
  for (auto key : keys_to_remove) {
    brillo::ProcessImpl vpd;
    vpd.AddArg("/usr/sbin/vpd");
    vpd.AddStringOption("-i", "RW_VPD");
    vpd.AddStringOption("-d", key);
    // Do not report failures as the key might not even exist in the VPD.
    vpd.RedirectOutputToMemory(true);
    vpd.Run();
    init::AppendToLog("vpd", vpd.GetOutputString(STDOUT_FILENO));
  }
}

ClobberState::ClobberState(const Arguments& args,
                           std::unique_ptr<crossystem::Crossystem> cros_system,
                           std::unique_ptr<ClobberUi> ui,
                           std::unique_ptr<ClobberWipe> clobber_wipe,
                           std::unique_ptr<ClobberLvm> clobber_lvm)
    : args_(args),
      cros_system_(std::move(cros_system)),
      ui_(std::move(ui)),
      stateful_(kStatefulPath),
      root_path_("/"),
      clobber_lvm_(std::move(clobber_lvm)),
      clobber_wipe_(std::move(clobber_wipe)),
      weak_ptr_factory_(this) {}

std::vector<base::FilePath> ClobberState::GetPreservedFilesList() {
  std::optional<int> debug_build =
      cros_system_->VbGetSystemPropertyInt(crossystem::Crossystem::kDebugBuild);
  std::set<std::string> stateful_paths =
      libpreservation::GetPreservationFileList(
          args_.safe_wipe, args_.ad_migration_wipe, args_.rollback_wipe,
          args_.rma_wipe, debug_build == 1, false);

  if (args_.factory_wipe || args_.default_key_migration_wipe) {
    for (auto& path :
         libpreservation::GetFactoryPreservationPathList(stateful_)) {
      stateful_paths.insert(path);
    }
  }

  std::vector<base::FilePath> preserved_files;
  for (const std::string& path : stateful_paths) {
    preserved_files.push_back(base::FilePath(path));
  }

  return preserved_files;
}

int ClobberState::CreateFileSystem(const std::string& filesystem_device) {
  brillo::ProcessImpl mkfs;
  mkfs.AddArg("/sbin/mkfs.ext4");
  // Check if encryption is supported. If yes, enable the flag during mkfs.
  if (base::PathExists(base::FilePath(kExt4DircryptoSupportedPath))) {
    mkfs.AddStringOption("-O", "encrypt");
  }
  mkfs.AddArg(filesystem_device);
  // TODO(wad) tune2fs.
  mkfs.RedirectOutputToMemory(true);
  LOG(INFO) << "Creating file system";
  int ret = mkfs.Run();
  init::AppendToLog("mkfs.ext4 ", mkfs.GetOutputString(STDOUT_FILENO));
  return ret;
}

void ClobberState::PreserveEncryptedFiles() {
  // Preserve Update Engine prefs when the device is powerwashed.
  base::FilePath ue_prefs_path(root_path_.Append(kUpdateEnginePrefsPath));
  base::FilePath ue_preserve_prefs_path(
      stateful_.Append(kUpdateEnginePreservePath));
  if (base::CreateDirectory(ue_preserve_prefs_path)) {
    for (const auto* ue_prefs_filename : kUpdateEnginePrefsFiles) {
      base::FilePath ue_prefs_file(ue_prefs_path.Append(ue_prefs_filename));
      base::FilePath ue_preserved_prefs_file(
          ue_preserve_prefs_path.Append(ue_prefs_filename));
      if (!base::CopyFile(ue_prefs_file, ue_preserved_prefs_file)) {
        LOG(ERROR) << "Error copying file. Source: " << ue_prefs_file
                   << " Target: " << ue_preserved_prefs_file;
      }
    }
  } else {
    LOG(ERROR) << "Error creating directory: " << ue_preserve_prefs_path;
  }

  // Preserve the psm device active dates when the device is powerwashed.
  base::FilePath psm_local_pref_file(
      root_path_.Append(kPsmDeviceActiveLocalPrefPath));
  base::FilePath psm_preserved_pref_file(
      stateful_.Append(kPsmDeviceActivePreservePath));
  if (!base::CopyFile(psm_local_pref_file, psm_preserved_pref_file)) {
    LOG(ERROR) << "Error copying file. Source: " << psm_local_pref_file
               << " Target: " << psm_preserved_pref_file;
  }

  // Preserve the files used to identify ChromeOS Flex devices.
  base::FilePath flex_path(root_path_.Append(kFlexLocalPath));
  base::FilePath flex_preserve_path(stateful_.Append(kFlexPreservePath));
  if (base::CreateDirectory(flex_preserve_path)) {
    for (const auto* flex_filename : kFlexFiles) {
      base::FilePath flex_file(flex_path.Append(flex_filename));
      base::FilePath flex_preserved_file(
          flex_preserve_path.Append(flex_filename));
      if (!base::CopyFile(flex_file, flex_preserved_file)) {
        LOG(ERROR) << "Error copying file. Source: " << flex_file
                   << " Target: " << flex_preserved_file;
      }
    }
  } else {
    LOG(ERROR) << "Error creating directory: " << flex_preserve_path;
  }
}

int ClobberState::Run() {
  DCHECK(cros_system_);

  wipe_start_time_ = base::TimeTicks::Now();

  // Defer callback to relocate log file back to stateful partition so that it
  // will be preserved after a reboot.
  base::ScopedClosureRunner relocate_clobber_state_log(base::BindRepeating(
      [](base::FilePath stateful_path) {
        base::Move(base::FilePath(init::kClobberLogPath),
                   stateful_path.Append("unencrypted/clobber-state.log"));
      },
      stateful_));

  // Check if this powerwash was triggered by a session manager request.
  // StartDeviceWipe D-Bus call is restricted to "chronos" so it is probably
  // safe to assume that such requests were initiated by the user.
  bool user_triggered_powerwash =
      (args_.reason.find("session_manager_dbus_request") != std::string::npos);

  // Allow crash preservation across clobber if the device is in developer mode.
  // For testing purposes, use a tmpfs path to disable collection.
  bool preserve_dev_mode_crash_reports =
      IsInDeveloperMode() &&
      !base::PathExists(base::FilePath(kDisableClobberCrashCollectionPath));

  // Check if sensitive files should be preserved. Sensitive files should be
  // preserved if any of the following conditions are met:
  // 1. The device is in developer mode and crash report collection is allowed.
  // 2. The request doesn't originate from a user-triggered powerwash.
  bool preserve_sensitive_files =
      !user_triggered_powerwash || preserve_dev_mode_crash_reports;

  // True if we should ensure that this powerwash takes at least 5 minutes.
  // Saved here because we may switch to using a fast wipe later, but we still
  // want to enforce the delay in that case.
  bool should_force_delay = !args_.fast_wipe && !args_.factory_wipe;

  LOG(INFO) << "Beginning clobber-state run";

  // As we move factory wiping from release image to factory test image,
  // clobber-state will be invoked directly under a tmpfs. GetRootDevice cannot
  // report correct output under such a situation. Therefore, the output is
  // preserved then assigned to environment variables ROOT_DEV/ROOT_DISK for
  // clobber-state. For other cases, the environment variables will be empty and
  // it falls back to using GetRootDevice.
  const char* root_disk_cstr = getenv("ROOT_DISK");
  if (root_disk_cstr != nullptr) {
    root_disk_ = base::FilePath(root_disk_cstr);
  } else {
    root_disk_ = utils::GetRootDevice(/*strip_partition=*/true);
  }

  base::FilePath root_device;
  const char* root_device_cstr = getenv("ROOT_DEV");
  if (root_device_cstr != nullptr) {
    root_device = base::FilePath(root_device_cstr);
  } else {
    root_device = utils::GetRootDevice(/*strip_partition=*/false);
  }

  LOG(INFO) << "Root disk: " << root_disk_.value();
  LOG(INFO) << "Root device: " << root_device.value();

  partitions_.stateful = utils::GetPartitionNumber(root_disk_, "STATE");
  partitions_.root_a = utils::GetPartitionNumber(root_disk_, "ROOT-A");
  partitions_.root_b = utils::GetPartitionNumber(root_disk_, "ROOT-B");
  partitions_.kernel_a = utils::GetPartitionNumber(root_disk_, "KERN-A");
  partitions_.kernel_b = utils::GetPartitionNumber(root_disk_, "KERN-B");
  partitions_.cros_metadata =
      utils::GetPartitionNumber(root_disk_, "POWERWASH-DATA");

  if (!GetDevicesToWipe(root_disk_, root_device, partitions_, &wipe_info_)) {
    LOG(ERROR) << "Getting devices to wipe failed, aborting run";
    return 1;
  }

  LOG(INFO) << "Stateful device: "
            << wipe_info_.stateful_partition_device.value();
  LOG(INFO) << "Inactive root device: "
            << wipe_info_.inactive_root_device.value();
  LOG(INFO) << "Inactive kernel device: "
            << wipe_info_.inactive_kernel_device.value();

  // Most effective means of destroying user data is run at the start: Throwing
  // away the key to encrypted stateful by requesting the TPM to be cleared at
  // next boot.
  if (!cros_system_->VbSetSystemPropertyInt(
          crossystem::Crossystem::kClearTpmOwnerRequest, 1)) {
    LOG(ERROR) << "Requesting TPM wipe via crossystem failed";
  }

  // In cases where biometric sensors are available, reset the internal entropy
  // used by those sensors for encryption, to render related data/templates etc.
  // undecipherable.
  if (!ClearBiometricSensorEntropy()) {
    LOG(ERROR) << "Clearing biometric sensor internal entropy failed";
  }

  // Try to mount encrypted stateful to save some files from there.
  bool encrypted_stateful_mounted = false;

  // Update Engine and OOBE config utilities require preservation of files in
  // /var across powerwash. Attempt to mount the encrypted stateful partition
  // if:
  // 1. The encrypted stateful partition is enabled on the device.
  // 2. clobber-state is not running in factory mode: mount-encrypted is not
  //    accessible within the factory environment.
  // Failure to mount the encrypted stateful partition prevents the preservation
  // of these files across powerwash, but functionally does not affect clobber.
  encrypted_stateful_mounted =
      USE_ENCRYPTED_STATEFUL && !args_.factory_wipe && MountEncryptedStateful();

  if (args_.safe_wipe) {
    IncrementFileCounter(stateful_.Append(kPowerWashCountPath));
    if (encrypted_stateful_mounted) {
      PreserveEncryptedFiles();
    }
  }

  // Clear clobber log if needed.
  if (!preserve_sensitive_files) {
    brillo::DeleteFile(stateful_.Append(kStatefulClobberLogPath));
  }

  // Attempt to preserve installed powerwash-safe DLCs if switching to
  // default_key_stateful from LVM.
  if (args_.default_key_migration_wipe) {
    clobber_lvm_->MigratePowerwashSafeDlcs(
        wipe_info_.stateful_partition_device,
        wipe_info_.active_kernel_partition == partitions_.kernel_a
            ? dlcservice::PartitionSlot::A
            : dlcservice::PartitionSlot::B);
  }

  std::vector<base::FilePath> preserved_files = GetPreservedFilesList();
  for (const base::FilePath& fp : preserved_files) {
    LOG(INFO) << "Preserving file: " << fp.value();
  }

  base::FilePath preserved_tar_file(kPreservedFilesTarPath);
  int ret = PreserveFiles(stateful_, preserved_files, preserved_tar_file);
  if (ret) {
    LOG(ERROR) << "Preserving files failed with code " << ret;
  }

  if (encrypted_stateful_mounted) {
    // Preserve a rollback data file separately as it's sensitive and must not
    // be stored unencrypted on the hard drive.
    if (args_.rollback_wipe) {
      MoveRollbackFileToPstore();
    }
    UnmountEncryptedStateful();
  }

  brillo::ProcessImpl log_preserve;
  log_preserve.AddArg("/sbin/clobber-log");
  log_preserve.AddArg("--preserve");
  log_preserve.AddArg("clobber-state");

  if (args_.factory_wipe) {
    log_preserve.AddArg("factory");
  }
  if (args_.fast_wipe) {
    log_preserve.AddArg("fast");
  }
  if (args_.keepimg) {
    log_preserve.AddArg("keepimg");
  }
  if (args_.safe_wipe) {
    log_preserve.AddArg("safe");
  }
  if (args_.rollback_wipe) {
    log_preserve.AddArg("rollback");
  }
  if (!args_.reason.empty()) {
    log_preserve.AddArg(args_.reason);
  }
  if (args_.rma_wipe) {
    log_preserve.AddArg("rma");
  }
  if (args_.ad_migration_wipe) {
    log_preserve.AddArg("ad_migration");
  }
  if (args_.disable_lvm_install) {
    log_preserve.AddArg("disable_lvm_install");
  }
  if (args_.default_key_migration_wipe) {
    log_preserve.AddArg("default_key_migration_wipe");
  }

  log_preserve.RedirectOutputToMemory(true);
  log_preserve.Run();
  init::AppendToLog("clobber-log", log_preserve.GetOutputString(STDOUT_FILENO));

  AttemptSwitchToFastWipe(
      clobber_wipe_->IsRotational(wipe_info_.stateful_partition_device));

  // Make sure the stateful partition has been unmounted.
  UnmountStateful(stateful_);

  // Ready for wiping.
  clobber_wipe_->SetPartitionInfo(partitions_);
  clobber_wipe_->SetFastWipe(args_.fast_wipe);

  // Reset metadata partition.
  if (!clobber_wipe_->WipeDevice(wipe_info_.cros_metadata_device)) {
    LOG(ERROR) << "Unable to wipe device "
               << wipe_info_.cros_metadata_device.value();
  }

  base::ScopedClosureRunner reset_stateful(base::BindOnce(
      &ClobberState::ResetStatefulPartition, weak_ptr_factory_.GetWeakPtr()));

  if (args_.preserve_lvs) {
    dlcservice::PartitionSlot slot =
        wipe_info_.active_kernel_partition == partitions_.kernel_a
            ? dlcservice::PartitionSlot::A
            : dlcservice::PartitionSlot::B;
    if (!clobber_lvm_->PreserveLogicalVolumesWipe(
            wipe_info_.stateful_partition_device,
            clobber_lvm_->PreserveLogicalVolumesWipeArgs(slot))) {
      args_.preserve_lvs = false;
      LOG(WARNING) << "Preserve logical volumes wipe failed "
                   << "(falling back to default LVM stateful wipe).";
    } else {
      LOG(INFO) << "Preserve logical volumes, skipping device level wipe.";
      reset_stateful.ReplaceClosure(base::DoNothing());
    }
  }

  reset_stateful.RunAndReset();

  // `preserve_lvs` precedence check over creating a blank LVM setup.
  std::optional<base::FilePath> new_stateful_filesystem_device;
  if (args_.preserve_lvs) {
    new_stateful_filesystem_device =
        clobber_lvm_->CreateLogicalVolumeStackForPreserved(
            wipe_info_.stateful_partition_device);
  } else if (USE_LVM_STATEFUL_PARTITION && !args_.disable_lvm_install) {
    new_stateful_filesystem_device = clobber_lvm_->CreateLogicalVolumeStack(
        wipe_info_.stateful_partition_device);
  } else {
    // Set up the stateful filesystem on top of the stateful partition.
    new_stateful_filesystem_device = wipe_info_.stateful_partition_device;
  }
  if (new_stateful_filesystem_device) {
    wipe_info_.stateful_filesystem_device = *new_stateful_filesystem_device;
  } else {
    LOG(ERROR) << "Unable to create stateful device";
    // Give an empty value, we are going to fail all the following steps and
    // reach reboot.
    wipe_info_.stateful_filesystem_device = base::FilePath("");
  }

  // If the chromeos metadata partition cannot be formatted (eg. due to open
  // files), reboot and let the next boot repair the device. This prevents the
  // device from falling back to the legacy layout. The stateful partition will
  // already be wiped at this point so the next boot will be able to reset both
  // filesystems.
  if (USE_DEFAULT_KEY_STATEFUL && args_.default_key_migration_wipe) {
    ret = CreateFileSystem(wipe_info_.cros_metadata_device.value());
    if (ret) {
      LOG(ERROR) << "Unable to create stateful file system. Error code: "
                 << ret;
      Reboot();
      return ret;
    }
  }

  ret = CreateFileSystem(wipe_info_.stateful_filesystem_device.value());
  if (ret) {
    LOG(ERROR) << "Unable to create stateful file system. Error code: " << ret;
  }

  // Mount the fresh image for last minute additions.
  if (mount(wipe_info_.stateful_filesystem_device.value().c_str(),
            stateful_.value().c_str(), "ext4", 0, nullptr) != 0) {
    PLOG(ERROR) << "Unable to mount stateful partition at "
                << stateful_.value();
  }

  if (base::PathExists(preserved_tar_file)) {
    brillo::ProcessImpl tar;
    tar.AddArg("/bin/tar");
    tar.AddStringOption("-C", stateful_.value());
    tar.AddArg("-x");
    tar.AddStringOption("-f", preserved_tar_file.value());
    tar.RedirectOutputToMemory(true);
    ret = tar.Run();
    init::AppendToLog("tar", tar.GetOutputString(STDOUT_FILENO));
    if (ret != 0) {
      LOG(WARNING) << "Restoring preserved files failed with code " << ret;
    }
    base::WriteFile(stateful_.Append("unencrypted/.powerwash_completed"), "");
    // TODO(b/190143108) Add one unit test in the context of
    // ClobberState::Run() to check the powerwash time file existence.
    if (!WriteLastPowerwashTime(stateful_.Append(kLastPowerWashTimePath),
                                base::Time::Now())) {
      PLOG(WARNING) << "Write the last_powerwash_time to file failed";
    }
  }

  brillo::ProcessImpl log_restore;
  log_restore.AddArg("/sbin/clobber-log");
  log_restore.AddArg("--restore");
  log_restore.AddArg("clobber-state");
  log_restore.RedirectOutputToMemory(true);
  ret = log_restore.Run();
  init::AppendToLog("clobber-log", log_restore.GetOutputString(STDOUT_FILENO));
  if (ret != 0) {
    LOG(WARNING) << "Restoring clobber.log failed with code " << ret;
  }

  // Attempt to collect crashes into the reboot vault crash directory. Do not
  // collect crashes if this is a user triggered or a factory powerwash.
  if (preserve_sensitive_files && !args_.factory_wipe) {
    libstorage::Platform platform;
    encrypted_reboot_vault::EncryptedRebootVault vault(&platform);
    if (vault.UnlockVault()) {
      CollectClobberCrashReports();
    }
  }

  // Remove keys that may alter device state.
  RemoveVpdKeys();

  if (!args_.keepimg) {
    utils::EnsureKernelIsBootable(root_disk_,
                                  wipe_info_.active_kernel_partition);
    clobber_wipe_->WipeDevice(wipe_info_.inactive_root_device);
    clobber_wipe_->WipeDevice(wipe_info_.inactive_kernel_device);
  }

  // Ensure that we've run for at least 5 minutes if this run requires it.
  if (should_force_delay) {
    ForceDelay();
  }

  // Check if we're in developer mode, and if so, create developer mode marker
  // file so that we don't run clobber-state again after reboot.
  if (!MarkDeveloperMode()) {
    LOG(ERROR) << "Creating developer mode marker file failed.";
  }

  // Schedule flush of filesystem caches to disk.
  sync();

  LOG(INFO) << "clobber-state has completed";
  relocate_clobber_state_log.RunAndReset();

  // Factory wipe should stop here.
  if (args_.factory_wipe) {
    return 0;
  }

  // If everything worked, reboot.
  Reboot();
  // This return won't actually be reached unless reboot fails.
  return 0;
}

bool ClobberState::IsInDeveloperMode() {
  std::optional<int> dev_mode_flag = cros_system_->VbGetSystemPropertyInt(
      crossystem::Crossystem::kDevSwitchBoot);
  // No flag or not in dev mode:
  if (!dev_mode_flag || *dev_mode_flag != 1) {
    return false;
  }
  std::optional<std::string> firmware_name =
      cros_system_->VbGetSystemPropertyString(
          crossystem::Crossystem::kMainFirmwareActive);
  // We are running ChromeOS firmware and we are not in recovery:
  return firmware_name && *firmware_name != "recovery";
}

bool ClobberState::MarkDeveloperMode() {
  if (IsInDeveloperMode()) {
    return base::WriteFile(stateful_.Append(".developer_mode"), "");
  }

  return true;
}

void ClobberState::AttemptSwitchToFastWipe(bool is_rotational) {
  // On a non-fast wipe, rotational drives take too long. Override to run them
  // through "fast" mode. Sensitive contents should already
  // be encrypted.
  if (!args_.fast_wipe && is_rotational) {
    LOG(INFO) << "Stateful device is on rotational disk, shredding files";
    ShredRotationalStatefulFiles();
    args_.fast_wipe = true;
    LOG(INFO) << "Switching to fast wipe";
  }

  // Do not use legacy salt as a fast_wipe allowence marker on devices which
  // allow non-tpm fallback for encryption.
  if (!USE_TPM_INSECURE_FALLBACK) {
    if (!brillo::cryptohome::home::IsLegacySystemSalt(stateful_)) {
      args_.fast_wipe = true;
      LOG(INFO) << "No legacy salt file, switching to fast wipe";
      return;
    }
  }

  // For drives that support secure erasure, wipe the stateful key material, and
  // then run the drives through "fast" mode.
  //
  // Note: currently only eMMC-based SSDs are supported.
  if (!args_.fast_wipe) {
    LOG(INFO) << "Attempting to wipe key material";
    if (WipeKeyMaterial()) {
      LOG(INFO) << "Wiping key material succeeded";
      args_.fast_wipe = true;
      LOG(INFO) << "Switching to fast wipe";
    } else {
      LOG(INFO) << "Wiping key material failed";
    }
  }
}

void ClobberState::ShredRotationalStatefulFiles() {
  // Directly remove things that are already encrypted (which are also the
  // large things), or are static from images.
  brillo::DeleteFile(stateful_.Append("encrypted.block"));
  brillo::DeletePathRecursively(stateful_.Append("var_overlay"));
  brillo::DeletePathRecursively(stateful_.Append("dev_image"));

  base::FileEnumerator shadow_files(
      stateful_.Append("home/.shadow"),
      /*recursive=*/true, base::FileEnumerator::FileType::DIRECTORIES);
  for (base::FilePath path = shadow_files.Next(); !path.empty();
       path = shadow_files.Next()) {
    if (path.BaseName() == base::FilePath("vault")) {
      brillo::DeletePathRecursively(path);
    }
  }

  // Shred everything else. We care about contents not filenames, so do not
  // use "-u" since metadata updates via fdatasync dominate the shred time.
  // Note that if the count-down is interrupted, the reset file continues
  // to exist, which correctly continues to indicate a needed wipe.
  brillo::ProcessImpl shred;
  shred.AddArg("/usr/bin/shred");
  shred.AddArg("--force");
  shred.AddArg("--zero");
  base::FileEnumerator stateful_files(stateful_, /*recursive=*/true,
                                      base::FileEnumerator::FileType::FILES);
  for (base::FilePath path = stateful_files.Next(); !path.empty();
       path = stateful_files.Next()) {
    shred.AddArg(path.value());
  }
  shred.RedirectOutputToMemory(true);
  shred.Run();
  init::AppendToLog("shred", shred.GetOutputString(STDOUT_FILENO));

  sync();
}

bool ClobberState::WipeKeyMaterial() {
  // Delete all of the top-level key files.
  std::vector<std::string> key_files{
      "encrypted.key", "encrypted.needs-finalization",
      "home/.shadow/cryptohome.key", "home/.shadow/salt",
      "home/.shadow/salt.sum"};
  bool found_file = false;
  for (const std::string& str : key_files) {
    base::FilePath path = stateful_.Append(str);
    if (base::PathExists(path)) {
      found_file = true;
      if (!clobber_wipe_->SecureErase(path)) {
        LOG(ERROR) << "Securely erasing file failed: " << path.value();
        return false;
      }
    }
  }

  // Delete user-specific keyfiles in individual user shadow directories.
  base::FileEnumerator directories(stateful_.Append("home/.shadow"),
                                   /*recursive=*/false,
                                   base::FileEnumerator::FileType::DIRECTORIES);
  for (base::FilePath user_dir = directories.Next(); !user_dir.empty();
       user_dir = directories.Next()) {
    std::vector<base::FilePath> files_to_erase;
    // Find old-style vault keyset files. This support can be removed once
    // cryptohomed no longer has support for reading from VaultKeyset files.
    base::FileEnumerator vk_files(user_dir, /*recursive=*/false,
                                  base::FileEnumerator::FileType::FILES);
    for (base::FilePath file = vk_files.Next(); !file.empty();
         file = vk_files.Next()) {
      if (file.RemoveFinalExtension().BaseName() == base::FilePath("master")) {
        files_to_erase.push_back(std::move(file));
      }
    }
    // Find new-style auth factor files.
    base::FileEnumerator af_files(user_dir.Append("auth_factors"),
                                  /*recursive=*/false,
                                  base::FileEnumerator::FileType::FILES);
    for (base::FilePath file = af_files.Next(); !file.empty();
         file = af_files.Next()) {
      files_to_erase.push_back(std::move(file));
    }
    // Find user secret stashes.
    base::FileEnumerator uss_files(
        user_dir.Append("user_secret_stash"),
        /*recursive=*/false, base::FileEnumerator::FileType::FILES, "uss.*");
    for (base::FilePath file = uss_files.Next(); !file.empty();
         file = uss_files.Next()) {
      files_to_erase.push_back(std::move(file));
    }
    // Try to erase all of the found files.
    for (const base::FilePath& file : files_to_erase) {
      found_file = true;
      if (!clobber_wipe_->SecureErase(file)) {
        LOG(ERROR) << "Securely erasing file failed: " << file.value();
        return false;
      }
    }
  }

  // If no files were found, then we can't say whether or not secure erase
  // works. Assume it doesn't.
  if (!found_file) {
    LOG(WARNING) << "No files existed to attempt secure erase";
    return false;
  }

  return clobber_wipe_->DropCaches();
}

void ClobberState::ForceDelay() {
  base::TimeDelta elapsed = base::TimeTicks::Now() - wipe_start_time_;
  LOG(INFO) << "Clobber has already run for " << elapsed.InSeconds()
            << " seconds";
  base::TimeDelta remaining = kMinClobberDuration - elapsed;
  if (remaining <= base::Seconds(0)) {
    LOG(INFO) << "Skipping forced delay";
    return;
  }
  LOG(INFO) << "Forcing a delay of " << remaining.InSeconds() << " seconds";
  if (!ui_->ShowCountdownTimer(remaining)) {
    // If showing the timer failed, we still want to make sure that we don't
    // run for less than |kMinClobberDuration|.
    base::PlatformThread::Sleep(remaining);
  }
}

void ClobberState::SetArgsForTest(const ClobberState::Arguments& args) {
  args_ = args;
}

ClobberState::Arguments ClobberState::GetArgsForTest() {
  return args_;
}

void ClobberState::SetStatefulForTest(const base::FilePath& stateful_path) {
  stateful_ = stateful_path;
}

void ClobberState::SetRootPathForTest(const base::FilePath& root_path) {
  root_path_ = root_path;
}

bool ClobberState::ClearBiometricSensorEntropy() {
  if (base::PathExists(base::FilePath(kBioWashPath))) {
    brillo::ProcessImpl bio_wash;
    bio_wash.AddArg(kBioWashPath);
    return bio_wash.Run() == 0;
  }
  // Return true here so that we don't report spurious failures on platforms
  // without the bio_wash executable.
  return true;
}

void ClobberState::Reboot() {
  brillo::ProcessImpl proc;
  proc.AddArg("/sbin/shutdown");
  proc.AddArg("-r");
  proc.AddArg("now");
  int ret = proc.Run();
  if (ret == 0) {
    // Wait for reboot to finish (it's an async call).
    sleep(60 * 60 * 24);
  }
  // If we've reached here, reboot (probably) failed.
  LOG(ERROR) << "Requesting reboot failed with failure code " << ret;
}

void ClobberState::ResetStatefulPartition() {
  // Attempt to remove the logical volume stack unconditionally: this covers the
  // situation where a device may rollback to a version that doesn't support
  // the LVM stateful partition setup.
  if (clobber_lvm_) {
    clobber_lvm_->RemoveLogicalVolumeStack(
        wipe_info_.stateful_partition_device);
  }

  // Destroy user data: wipe the stateful partition.
  if (!clobber_wipe_->WipeDevice(wipe_info_.stateful_partition_device)) {
    LOG(ERROR) << "Unable to wipe device "
               << wipe_info_.stateful_partition_device.value();
  }
}
