// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_util.h>

#include "installer/cgpt_manager.h"
#include "installer/chromeos_install_config.h"
#include "installer/chromeos_legacy.h"
#include "installer/chromeos_postinst.h"
#include "installer/chromeos_setimage.h"
#include "installer/inst_util.h"
#include "installer/slow_boot_notify.h"

using std::string;

namespace {
const char kStatefulMount[] = "/mnt/stateful_partition";

bool GetKernelCommandLine(string* kernel_cmd_line) {
  if (!base::ReadFileToString(base::FilePath("/proc/cmdline"),
                              kernel_cmd_line)) {
    LOG(ERROR) << "Can't read kernel commandline options";
    return false;
  }
  return true;
}
}  // namespace

bool ConfigureInstall(const string& install_dev,
                      const string& install_dir,
                      BiosType bios_type,
                      DeferUpdateAction defer_update_action,
                      InstallConfig* install_config) {
  Partition root = Partition(install_dev, install_dir);

  string slot;
  switch (root.number()) {
    case PART_NUM_ROOT_A:
      slot = "A";
      break;
    case PART_NUM_ROOT_B:
      slot = "B";
      break;
    default:
      LOG(ERROR) << "Not a valid target partition number: " << root.number();
      return false;
  }

  string kernel_dev = MakePartitionDev(root.base_device(), root.number() - 1);

  string boot_dev = MakePartitionDev(root.base_device(), PART_NUM_EFI_SYSTEM);

  // if we don't know the bios type, detect it. Errors are logged
  // by the detect method.
  if (bios_type == kBiosTypeUnknown && !DetectBiosType(&bios_type)) {
    return false;
  }

  // Put the actual values on the result structure
  install_config->slot = slot;
  install_config->root = root;
  install_config->kernel = Partition(kernel_dev);
  install_config->boot = Partition(boot_dev);
  install_config->bios_type = bios_type;
  install_config->defer_update_action = defer_update_action;

  return true;
}

bool IsRunningMiniOS() {
  string kernel_cmd_line;
  return GetKernelCommandLine(&kernel_cmd_line) &&
         kernel_cmd_line.find("cros_minios") != string::npos;
}

bool DetectBiosType(BiosType* bios_type) {
  string kernel_cmd_line;
  return GetKernelCommandLine(&kernel_cmd_line) &&
         KernelConfigToBiosType(kernel_cmd_line, bios_type);
}

bool KernelConfigToBiosType(const string& kernel_config, BiosType* type) {
  if (kernel_config.find("cros_secure") != string::npos) {
    *type = kBiosTypeSecure;
    return true;
  }

  if (kernel_config.find("cros_legacy") != string::npos) {
#ifdef __arm__
    // The Arm platform only uses U-Boot, but may set cros_legacy to mean
    // U-Boot without our secure boot modifications.
    *type = kBiosTypeUBoot;
#else
    *type = kBiosTypeLegacy;
#endif
    return true;
  }

  if (kernel_config.find("cros_efi") != string::npos) {
    *type = kBiosTypeEFI;
    return true;
  }

  // No recognized bios type was found
  LOG(ERROR) << "No recognized cros_XXX bios option on kernel command line.";
  return false;
}

namespace {

// Run the cr50 script with the given args. Returns zero on success, exit code
// on failure.
//
// script_name the script in /usr/share/cros to run
// script_arg the args to run the script with
//
int RunCr50Script(const string& install_dir,
                  const string& script_name,
                  const string& script_arg) {
  string script = install_dir + "/usr/share/cros/" + script_name;
  if (access(script.c_str(), X_OK)) {
    // The script is not there, means no cr50 present either, nothing to do.
    return 0;
  }
  return RunCommand({script, script_arg});
}

// Updates firmware. We must activate new firmware only after new kernel is
// actived (installed and made bootable), otherwise new firmware with all old
// kernels may lead to recovery screen (due to new key).
// TODO(hungte) Replace the shell execution by native code (crosbug.com/25407).
// Note that this returns an exit code, not bool success/failure.
int FirmwareUpdate(const InstallConfig& install_config, bool is_update) {
  string install_dir = install_config.root.mount();
  string command = install_dir + "/usr/sbin/chromeos-firmwareupdate";
  if (access(command.c_str(), X_OK) != 0) {
    LOG(INFO) << "No firmware updates available.";
    // Return success.
    return 0;
  }

  string mode;
  if (is_update) {
    switch (install_config.defer_update_action) {
      case DeferUpdateAction::kAuto:
        // Background auto update by Update Engine.
        mode = "autoupdate";
        break;
      case DeferUpdateAction::kHold:
        mode = "deferupdate_hold";
        break;
      case DeferUpdateAction::kApply:
        mode = "deferupdate_apply";
        break;
    }
  } else {
    // Recovery image, or from command "chromeos-install".
    mode = "recovery";
  }

  LOG(INFO) << "Fimrware update with mode=" << mode;
  int result = RunCommand({command, "--mode=" + mode});

  // Next step after postinst may take a lot of time (eg, disk wiping)
  // and people may confuse that as 'firmware update takes a long wait',
  // we explicitly prompt here.
  if (result == 0) {
    LOG(INFO) << "Firmware update completed.";
  } else if (result == 3) {
    LOG(INFO) << "Firmware can't be updated. Booted from RW Firmware B"
                 " with error code: "
              << result;
  } else if (result == 4) {
    LOG(INFO) << "RO Firmware needs update, but is really marked RO."
                 " with error code: "
              << result;
  } else {
    LOG(INFO) << "Firmware update failed with error code: " << result;
  }

  return result;
}

// Fix the unencrypted permission. The permission on this file have been
// deployed with wrong values (0766 for the permission) and/or the wrong
// uid:gid.
void FixUnencryptedPermission() {
  string unencrypted_dir = string(kStatefulMount) + "/unencrypted";
  LOG(INFO) << "Checking permission of " << unencrypted_dir;
  struct stat unencrypted_stat;
  const mode_t target_mode =
      S_IFDIR | S_IRWXU | (S_IRGRP | S_IXGRP) | (S_IROTH | S_IXOTH);  // 040755
  if (stat(unencrypted_dir.c_str(), &unencrypted_stat) != 0) {
    PLOG(ERROR) << "Couldn't check the current permission, ignored";
  } else if (unencrypted_stat.st_uid == 0 && unencrypted_stat.st_gid == 0 &&
             unencrypted_stat.st_mode == target_mode) {
    LOG(INFO) << "Permission is ok.";
  } else {
    bool ok = true;
    // chmod(2) only takes the last four octal digits, so we flip the IFDIR bit.
    if (chmod(unencrypted_dir.c_str(), target_mode ^ S_IFDIR) != 0) {
      PLOG(ERROR) << "chmod failed";
      ok = false;
    }
    if (chown(unencrypted_dir.c_str(), 0, 0) != 0) {
      PLOG(ERROR) << "chown failed";
      ok = false;
    }
    if (ok)
      LOG(INFO) << "Permission changed successfully.";
  }
}

// Do board specific post install stuff, if available.
bool RunBoardPostInstall(const string& install_dir) {
  string script = install_dir + "/usr/sbin/board-postinst";

  if (access(script.c_str(), X_OK)) {
    return true;
  }

  int result = RunCommand({script, install_dir});

  if (result)
    LOG(ERROR) << "Board post install failed, result: " << result;
  else
    LOG(INFO) << "Board post install succeeded.";

  return result == 0;
}

bool UpdatePartitionTable(CgptManager& cgpt_manager,
                          const InstallConfig& install_config,
                          bool is_update) {
  LOG(INFO) << "Updating Partition Table Attributes using CgptManager...";

  int result = cgpt_manager.SetHighestPriority(install_config.kernel.number());
  if (result != kCgptSuccess) {
    LOG(ERROR) << "Unable to set highest priority for kernel: "
               << install_config.kernel.number();
    return false;
  }

  // If it's not an update, pre-mark the first boot as successful
  // since we can't fall back on the old install.
  bool new_kern_successful = !is_update;
  result = cgpt_manager.SetSuccessful(install_config.kernel.number(),
                                      new_kern_successful);
  if (result != kCgptSuccess) {
    LOG(ERROR) << "Unable to set successful to " << new_kern_successful
               << " for kernel: " << install_config.kernel.number();
    return false;
  }

  int numTries = 6;
  result =
      cgpt_manager.SetNumTriesLeft(install_config.kernel.number(), numTries);
  if (result != kCgptSuccess) {
    LOG(ERROR) << "Unable to set NumTriesLeft to " << numTries
               << " for kernel: " << install_config.kernel.number();
    return false;
  }

  LOG(INFO) << "Updated kernel " << install_config.kernel.number()
            << " with Successful: " << new_kern_successful
            << " and NumTriesLeft: " << numTries;
  return true;
}

bool RollbackPartitionTable(CgptManager& cgpt_manager,
                            const InstallConfig& install_config) {
  // In all these checks below, we continue even if there's a failure
  // so as to cleanup as much as possible.
  bool new_kern_successful = false;
  bool rollback_successful = true;
  int result =
      cgpt_manager.SetSuccessful(install_config.kernel.number(), false);
  if (result != kCgptSuccess) {
    rollback_successful = false;
    LOG(ERROR) << "Unable to set successful to " << new_kern_successful
               << " for kernel: " << install_config.kernel.number();
  }

  int numTries = 0;
  result =
      cgpt_manager.SetNumTriesLeft(install_config.kernel.number(), numTries);
  if (result != kCgptSuccess) {
    rollback_successful = false;
    LOG(ERROR) << "Unable to set NumTriesLeft to " << numTries
               << " for kernel: " << install_config.kernel.number();
  }

  int priority = 0;
  result = cgpt_manager.SetPriority(install_config.kernel.number(), priority);
  if (result != kCgptSuccess) {
    rollback_successful = false;
    LOG(ERROR) << "Unable to set Priority to " << priority
               << " for kernel: " << install_config.kernel.number();
  }

  if (rollback_successful)
    LOG(INFO) << "Successfully updated GPT with all settings to rollback.";

  return rollback_successful;
}

// Do post install stuff.
//
// Install kernel, set up the proper bootable partition in
// GPT table, update firmware if necessary and possible.
//
// install_config defines the root, kernel and boot partitions.
//
bool ChromeosChrootPostinst(const InstallConfig& install_config,
                            int* exit_code) {
  // Extract External ENVs
  bool is_factory_install = getenv("IS_FACTORY_INSTALL");
  bool is_recovery_install = getenv("IS_RECOVERY_INSTALL");
  bool is_install = getenv("IS_INSTALL");
  bool is_update = !is_factory_install && !is_recovery_install && !is_install &&
                   !IsRunningMiniOS();

  switch (install_config.defer_update_action) {
    case DeferUpdateAction::kAuto:
    case DeferUpdateAction::kHold: {
      // TODO(dgarrett): Remove when chromium:216338 is fixed.
      // If this FS was mounted read-write, we can't do deltas from it. Mark the
      // FS as such
      Touch(install_config.root.mount() +
            "/.nodelta");  // Ignore Error on purpose

      LOG(INFO) << "Setting boot target to " << install_config.root.device()
                << ": Partition " << install_config.root.number() << ", Slot "
                << install_config.slot;

      if (!SetImage(install_config)) {
        LOG(ERROR) << "SetImage failed.";
        return false;
      }

      // This cache file might be invalidated, and will be recreated on next
      // boot. Error ignored, since we don't care if it didn't exist to start
      // with.
      string network_driver_cache = "/var/lib/preload-network-drivers";
      LOG(INFO) << "Clearing network driver boot cache: "
                << network_driver_cache;
      unlink(network_driver_cache.c_str());

      break;
    }
    case DeferUpdateAction::kApply:
      break;
  }

  LOG(INFO) << "Syncing filesystems before changing boot order...";
  LoggingTimerStart();
  sync();
  LoggingTimerFinish();

  CgptManager cgpt_manager;

  int result = cgpt_manager.Initialize(install_config.root.base_device());
  if (result != kCgptSuccess) {
    LOG(ERROR) << "Unable to initialize CgptManager().";
    return false;
  }

  switch (install_config.defer_update_action) {
    case DeferUpdateAction::kApply:
      LOG(INFO) << "Updating partition table for deferred update APPLY.";
      [[fallthrough]];
    case DeferUpdateAction::kAuto: {
      if (!UpdatePartitionTable(cgpt_manager, install_config, is_update)) {
        LOG(ERROR) << "UpdatePartitionTable failed.";
        return false;
      }
      // At this point in the script, the new partition has been marked bootable
      // and a reboot will boot into it. Thus, it's important that any future
      // errors in this script do not cause this script to return failure unless
      // in factory mode.
      FixUnencryptedPermission();

      // We have a new image, making the ureadahead pack files
      // out-of-date.  Delete the files so that ureadahead will
      // regenerate them on the next reboot.
      // WARNING: This doesn't work with upgrade from USB, rather than full
      // install/recovery. We don't have support for it as it'll increase the
      // complexity here, and only developers do upgrade from USB.
      if (!RemovePackFiles("/var/lib/ureadahead")) {
        LOG(ERROR) << "RemovePackFiles Failed.";
      }

      // Create a file indicating that the install is completed. The file
      // will be used in /sbin/chromeos_startup to run tasks on the next boot.
      // See comments above about removing ureadahead files.
      string install_completed = string(kStatefulMount) + "/.install_completed";
      if (!Touch(install_completed)) {
        PLOG(ERROR) << "Touch(" << install_completed.c_str() << ") failed.";
      }

      // If present, remove firmware checking completion file to force a disk
      // firmware check at reboot.
      string disk_fw_check_complete =
          string(kStatefulMount) +
          "/unencrypted/cache/.disk_firmware_upgrade_completed";
      unlink(disk_fw_check_complete.c_str());

      if (!is_factory_install &&
          !RunBoardPostInstall(install_config.root.mount())) {
        LOG(ERROR) << "Failed to perform board specific post install script.";
        return false;
      }

      break;
    }
    case DeferUpdateAction::kHold:
      LOG(INFO) << "Skipping partition table update for deferred update HOLD.";
      break;
  }

  // In postinst in future, we may provide an option (ex, --update_firmware).
  string firmware_tag_file =
      (install_config.root.mount() + "/root/.force_update_firmware");

  bool attempt_firmware_update =
      (!is_factory_install && (access(firmware_tag_file.c_str(), 0) == 0));

  // In factory process, firmware is either pre-flashed or assigned by
  // mini-omaha server, and we don't want to try updates inside postinst.
  if (attempt_firmware_update) {
    base::FilePath fspm_main;
    if (CreateTemporaryFile(&fspm_main))
      SlowBootNotifyPreFwUpdate(fspm_main);

    *exit_code = FirmwareUpdate(install_config, is_update);
    if (*exit_code == 0) {
      base::FilePath fspm_next;
      if (CreateTemporaryFile(&fspm_next))
        SlowBootNotifyPostFwUpdate(fspm_next);

      if (SlowBootNotifyRequired(fspm_main, fspm_next)) {
        base::FilePath slow_boot_req_file(string(kStatefulMount) +
                                          "/etc/slow_boot_required");
        if (WriteFile(slow_boot_req_file, "1", 1) != 1)
          PLOG(ERROR) << "Unable to write to file:"
                      << slow_boot_req_file.value();
      }
      base::DeleteFile(fspm_main);
      base::DeleteFile(fspm_next);
    } else {
      base::DeleteFile(fspm_main);

      LOG(INFO)
          << "Rolling back update due to failure calling firmware updater";
      // Note: This will only rollback the ChromeOS verified boot target.
      // The assumption is that systems running firmware autoupdate
      // are not running legacy (non-ChromeOS) firmware. If the firmware
      // updater crashes or writes corrupt data rather than gracefully
      // failing, we'll probably need to recover with a recovery image.
      if (!RollbackPartitionTable(cgpt_manager, install_config)) {
        LOG(ERROR) << "RollbackPartitionTable failed.";
      }

      return false;
    }
  }

  // Don't modify Cr50 in factory.
  if (!is_factory_install) {
    int result = 0;

    // Check the device state to determine if the board id should be set.
    if (RunCr50Script(install_config.root.mount(), "cr50-set-board-id.sh",
                      "check_device")) {
      LOG(INFO) << "Skip setting board id";
    } else {
      // Set the board id with unknown phase.
      result = RunCr50Script(install_config.root.mount(),
                             "cr50-set-board-id.sh", "unknown");
      // cr50 set board id failure is not a reason to interrupt installation.
      if (result)
        LOG(ERROR) << "ignored: cr50-set-board-id failure: " << result;
    }

    result = RunCr50Script(install_config.root.mount(), "cr50-update.sh",
                           install_config.root.mount());
    // cr50 update failure is not a reason for interrupting installation.
    if (result)
      LOG(WARNING) << "ignored: cr50-update failure: " << result;
    LOG(INFO) << "cr50 setup complete.";
  }

  if (cgpt_manager.Finalize()) {
    LOG(ERROR) << "Failed to write GPT changes back.";
    return false;
  }

  printf("ChromeosChrootPostinst complete\n");
  return true;
}

}  // namespace

bool RunPostInstall(const string& install_dev,
                    const string& install_dir,
                    BiosType bios_type,
                    DeferUpdateAction defer_update_action,
                    int* exit_code) {
  InstallConfig install_config;

  if (!ConfigureInstall(install_dev, install_dir, bios_type,
                        defer_update_action, &install_config)) {
    LOG(ERROR) << "Configure failed.";
    return false;
  }

  // Log how we are configured.
  LOG(INFO) << "PostInstall Configured: " << install_config.slot.c_str() << ", "
            << install_config.root.device() << ", "
            << install_config.kernel.device() << ", "
            << install_config.boot.device();

  string uname;
  if (GetKernelInfo(&uname)) {
    LOG(INFO) << "Current Kernel Info: " << uname.c_str();
  }

  string lsb_contents;
  // If we can read the lsb-release we are updating TO, log it
  if (base::ReadFileToString(
          base::FilePath(install_config.root.mount()).Append("etc/lsb-release"),
          &lsb_contents)) {
    LOG(INFO) << "lsb-release inside the new rootfs:\n" << lsb_contents.c_str();
  }

  if (!ChromeosChrootPostinst(install_config, exit_code)) {
    LOG(ERROR) << "PostInstall Failed.";
    return false;
  }

  LOG(INFO) << "Syncing filesystem at end of postinst...";
  sync();

  switch (install_config.defer_update_action) {
    case DeferUpdateAction::kAuto:
      // Sync doesn't appear to sync out cgpt changes, so
      // let them flush themselves. (chromium-os:35992)
      sleep(10);
      break;
    case DeferUpdateAction::kApply:
      // Need to reduce the amount of time as much as possible for defer update
      // APPLY action as it will be noticeable to users.
      break;
    case DeferUpdateAction::kHold:
      return true;
  }

  // If we are installing to a ChromeOS Bios, we are done.
  if (install_config.bios_type == kBiosTypeSecure)
    return true;

  install_config.boot.set_mount("/tmp/boot_mnt");

  if (!base::CreateDirectory(base::FilePath(install_config.boot.mount()))) {
    return false;
  }

  // Mount the EFI system partition.
  LOG(INFO) << "mount " << install_config.boot.device() << " to "
            << install_config.boot.mount();
  if (mount(install_config.boot.device().c_str(),
            install_config.boot.mount().c_str(), "vfat",
            MS_NODEV | MS_NOEXEC | MS_NOSUID, nullptr) != 0) {
    PLOG(ERROR) << "Failed to mount " << install_config.boot.device() << " to "
                << install_config.boot.mount();
    return false;
  }

  bool success = true;

  switch (install_config.bios_type) {
    case kBiosTypeUnknown:
    case kBiosTypeSecure:
      LOG(ERROR) << "Unexpected BiosType: " << install_config.bios_type;
      success = false;
      break;

    case kBiosTypeUBoot:
      // The Arm platform only uses U-Boot, but may set cros_legacy to mean
      // U-Boot without secure boot modifications. This may need handling.
      if (!RunLegacyUBootPostInstall(install_config)) {
        LOG(ERROR) << "Legacy PostInstall failed.";
        success = false;
      }
      break;

    case kBiosTypeLegacy:
      if (!RunLegacyPostInstall(install_config)) {
        LOG(ERROR) << "Legacy PostInstall failed.";
        success = false;
        break;
      }

      // Configure EFI entries in addition to the legacy.
      // Allows devices that can boot installers in legacy
      // but will boot the installed target in EFI mode.
      // Errors here are not necessarily fatal as the common
      // case is the machine will boot successfully from legacy.
      if (USE_POSTINSTALL_CONFIG_EFI_AND_LEGACY) {
        if (!RunEfiPostInstall(install_config)) {
          LOG(WARNING) << "Ignored secondary EFI PostInstall failure.";
        }
      }

      break;

    case kBiosTypeEFI:
      if (!RunEfiPostInstall(install_config)) {
        LOG(ERROR) << "EFI PostInstall failed.";
        success = false;
        break;
      }

      // Optionally update the legacy boot entries to support
      // devices that can boot from the USB in EFI mode with the
      // installed disk booting in legacy mode.
      if (USE_POSTINSTALL_CONFIG_EFI_AND_LEGACY) {
        if (!RunLegacyPostInstall(install_config)) {
          LOG(WARNING) << "Ignored secondary Legacy PostInstall failure.";
        }
      }

      break;
  }

  // Unmount the EFI system partition.
  LOG(INFO) << "umount " << install_config.boot.mount();
  if (umount(install_config.boot.mount().c_str()) != 0) {
    PLOG(ERROR) << "Failed to unmount " << install_config.boot.mount();
    success = false;
  }

  return success;
}
