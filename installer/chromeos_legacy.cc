// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "installer/chromeos_legacy.h"

#include <stdio.h>
#include <unistd.h>

#include <optional>
#include <string>
#include <vector>

#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <brillo/files/file_util.h>

#include "installer/chromeos_legacy_private.h"
#include "installer/efi_boot_management.h"
#include "installer/inst_util.h"
#include "installer/platform.h"

using std::string;
using std::vector;

namespace {

struct GrubQuirkEntry {
  std::string_view sys_vendor;
  std::string_view product_name;
};

// Vendor and product_name prefix to check for the grub workaround
// for b:431021440.
constexpr GrubQuirkEntry kGrubQuirk =
    GrubQuirkEntry{.sys_vendor = "Acer", .product_name = "TravelMate"};
}  // namespace

// String matching the kernel boot lines in grub.cfg files.
const std::string CommandPatternForSlot(BootSlot slot) {
  switch (slot) {
    case BootSlot::A:
      return "/syslinux/vmlinuz.A";
    case BootSlot::B:
      return "/syslinux/vmlinuz.B";
  }
}

std::string EfiGrubCfg::GetKernelCommand(BootSlot slot,
                                         EfiGrubCfg::DmOption dm) const {
  const string kernel_pattern = CommandPatternForSlot(slot);
  const bool want_empty_dm = dm == EfiGrubCfg::DmOption::None;
  for (const auto& line : file_lines_) {
    if (line.find(kernel_pattern) == string::npos) {
      continue;
    }

    if (ExtractKernelArg(line, "dm").empty() == want_empty_dm) {
      return line;
    }
  }
  return "";
}

bool EfiGrubCfg::ReplaceKernelCommand(BootSlot slot,
                                      EfiGrubCfg::DmOption dm,
                                      std::string cmd) {
  const string kernel_pattern = CommandPatternForSlot(slot);
  const bool want_empty_dm = dm == EfiGrubCfg::DmOption::None;
  bool did_set = false;
  for (auto& line : file_lines_) {
    if (line.find(kernel_pattern) == string::npos) {
      continue;
    }

    if (ExtractKernelArg(line, "dm").empty() == want_empty_dm) {
      DLOG(INFO) << "Replacing: " << line;
      line = cmd;
      // Continue to replace all matching lines.
      // It is not expected that there are multiple entries
      // however replace them if they occur.
      did_set = true;
    }
  }
  return did_set;
}

bool EfiGrubCfg::LoadFile(const base::FilePath& path) {
  string grub_src;
  if (!base::ReadFileToString(path, &grub_src)) {
    PLOG(ERROR) << "Unable to read grub template file: " << path.value();
    return false;
  }
  // Split the file contents into lines.
  file_lines_ = base::SplitString(grub_src, "\n", base::KEEP_WHITESPACE,
                                  base::SPLIT_WANT_ALL);
  return true;
}

std::string EfiGrubCfg::ToString() const {
  return base::JoinString(file_lines_, "\n");
}

bool EfiGrubCfg::UpdateBootParameters(BootSlot slot,
                                      const string& root_uuid,
                                      const string& verity_args) {
  const string kernel_pattern = CommandPatternForSlot(slot);
  for (auto& line : file_lines_) {
    // Convert all "linuxefi" grub commands to "linux" for the updated
    // version of grub.
    base::ReplaceFirstSubstringAfterOffset(&line, 0, "linuxefi", "linux");

    if (line.find(kernel_pattern) == string::npos) {
      continue;
    }

    DLOG(INFO) << "Updating command: " << line;
    if (ExtractKernelArg(line, "dm").empty()) {
      // If it's an unverified boot line, just set the root partition to boot.
      if (!SetKernelArg("root", "PARTUUID=" + root_uuid, &line)) {
        LOG(ERROR) << "Unable to update unverified root flag in " << line;
        return false;
      }
    } else if (!SetKernelArg("dm", verity_args, &line)) {
      LOG(INFO) << "Unable to update verified dm flag.";
      return false;
    }
  }
  return true;
}

bool UpdateLegacyKernel(const InstallConfig& install_config) {
  const base::FilePath root_mount(install_config.root.mount());
  const base::FilePath boot_mount(install_config.boot.mount());

  const base::FilePath kernel_from = root_mount.Append("boot/vmlinuz");
  const base::FilePath kernel_to =
      boot_mount.Append("syslinux").Append("vmlinuz." + install_config.slot);

  // In the event of a typical install, `kernel_from` may not exist.
  // There is an expectation that `include_vmlinuz` be added to the board
  // overlay's `profiles/base/make.defaults` as a `USE=` flag. Without this,
  // `src/scripts/build_library/base_image_util.sh` will move the Kernel during
  // `build_image`.
  if (install_config.is_install &&
      (install_config.bios_type == BiosType::kLegacy ||
       install_config.bios_type == BiosType::kEFI)) {
    // This is a non-fatal condition. The new Kernel is already present at the
    // destination. Log a warning and continue.
    if (!base::PathExists(kernel_from) && base::PathExists(kernel_to)) {
      LOG(WARNING) << "Legacy Kernel '" << kernel_from
                   << "' does not exist. Consider adding "
                   << "`USE=\"${USE} include_vmlinuz\"` "
                   << "to the board's `make.defaults`.";
      return true;
    }
  }
  // In any other scenario (like an update), ensure we copy the new Kernel.
  return base::CopyFile(kernel_from, kernel_to);
}

string ExpandVerityArguments(const string& kernel_config,
                             const string& root_uuid) {
  string kernel_config_dm = ExtractKernelArg(kernel_config, "dm");

  // The verity config from the kernel contains short hand symbols for
  // partition names that we have to expand to specific UUIDs.

  // %U+1 -> XXX-YYY-ZZZ
  ReplaceAll(&kernel_config_dm, "%U+1", root_uuid);

  // PARTUUID=%U/PARTNROFF=1 -> PARTUUID=XXX-YYY-ZZZ
  ReplaceAll(&kernel_config_dm, "%U/PARTNROFF=1", root_uuid);

  return kernel_config_dm;
}

bool RunLegacyPostInstall(const Platform& platform,
                          const InstallConfig& install_config) {
  const base::FilePath root_mount(install_config.root.mount());
  const base::FilePath root_syslinux = root_mount.Append("boot/syslinux");
  const base::FilePath boot_mount(install_config.boot.mount());
  const base::FilePath boot_syslinux = boot_mount.Append("syslinux");
  LOG(INFO) << "Running LegacyPostInstall.";

  if (RunCommand({"cp", "-nR", root_syslinux.value(), boot_mount.value()}) !=
      0) {
    return false;
  }

  if (!UpdateLegacyKernel(install_config)) {
    return false;
  }

  string kernel_config =
      platform.DumpKernelConfig(install_config.kernel.device());
  base::FilePath kernel_config_root =
      base::FilePath(ExtractKernelArg(kernel_config, "root"));

  // Prepare the new default.cfg

  string verity_enabled =
      (IsReadonly(kernel_config_root) ? "chromeos-vhd" : "chromeos-hd");

  string default_syslinux_cfg = base::StringPrintf(
      "DEFAULT %s.%s\n", verity_enabled.c_str(), install_config.slot.c_str());

  const base::FilePath syslinux_cfg = boot_syslinux.Append("default.cfg");
  if (!base::WriteFile(syslinux_cfg, default_syslinux_cfg)) {
    return false;
  }

  // Prepare the new root.A/B.cfg

  const base::FilePath old_root_cfg_file =
      root_syslinux.Append("root." + install_config.slot + ".cfg");
  const base::FilePath new_root_cfg_file =
      boot_syslinux.Append(old_root_cfg_file.BaseName());

  // Copy over the unmodified version for this release...
  if (!base::CopyFile(old_root_cfg_file, new_root_cfg_file)) {
    return false;
  }

  // Insert the proper root device for non-verity boots
  const string root_opt = "PARTUUID=" + install_config.root.uuid(platform);
  if (!ReplaceInFile("HDROOT" + install_config.slot, root_opt,
                     new_root_cfg_file)) {
    return false;
  }

  string kernel_config_dm =
      ExpandVerityArguments(kernel_config, install_config.root.uuid(platform));

  if (kernel_config_dm.empty()) {
    LOG(ERROR) << "Failed to extract Verity arguments.";
    return false;
  }

  // Insert the proper verity options for verity boots
  if (!ReplaceInFile("DMTABLE" + install_config.slot, kernel_config_dm,
                     new_root_cfg_file)) {
    return false;
  }

  return true;
}

// Copy a file from the root partition to the boot partition.
bool CopyBootFile(const InstallConfig& install_config,
                  const std::string& src,
                  const std::string& dst) {
  bool result = true;
  const base::FilePath root_mount(install_config.root.mount());
  const base::FilePath boot_mount(install_config.boot.mount());
  const base::FilePath src_path = root_mount.Append(src);
  const base::FilePath dst_path = boot_mount.Append(dst);

  // If the source file file exists, copy it into place, else do nothing.
  if (base::PathExists(src_path)) {
    LOG(INFO) << "Copying " << src_path << " to " << dst_path;
    result = base::CopyFile(src_path, dst_path);
  } else {
    LOG(INFO) << "Not present to install: " << src_path;
  }
  return result;
}

bool RunLegacyUBootPostInstall(const InstallConfig& install_config) {
  bool result = true;
  LOG(INFO) << "Running LegacyUBootPostInstall.";

  result &= CopyBootFile(install_config,
                         "boot/boot-" + install_config.slot + ".scr.uimg",
                         "u-boot/boot.scr.uimg");
  result &= CopyBootFile(
      install_config, "boot/uEnv." + install_config.slot + ".txt", "uEnv.txt");
  result &= CopyBootFile(install_config, "boot/MLO", "MLO");
  result &= CopyBootFile(install_config, "boot/u-boot.img", "u-boot.img");

  return result;
}

bool UpdateEfiBootloaders(const Platform& platform,
                          const InstallConfig& install_config) {
  bool result = true;
  const base::FilePath src_dir =
      install_config.root.mount().Append("boot/efi/boot");
  const base::FilePath dest_dir =
      install_config.boot.mount().Append("efi/boot");
  base::FileEnumerator file_enum(src_dir, /*recursive=*/false,
                                 base::FileEnumerator::FILES);
  for (auto src = file_enum.Next(); !src.empty(); src = file_enum.Next()) {
    // Filter files by extension.
    // .efi files are UEFI executables.
    // .sig files are detached signatures.
    const auto extension = src.FinalExtension();
    if (extension != ".efi" && extension != ".sig") {
      continue;
    }

    const base::FilePath dest = dest_dir.Append(src.BaseName());
    if (!base::CopyFile(src, dest)) {
      result = false;
    }
  }

  // The grub workaround must only be applied for crdyboot builds.
  // crdybootx64.efi will only exist when crdyboot is in use.
  if (base::PathExists(dest_dir.Append("crdybootx64.efi")) &&
      CheckRequiresGrubQuirk(platform)) {
    // This is copying the shim for crdyboot, bootx64.efi (crdyshim), to
    // the additional path grubx64.efi.
    // On buggy firmware (those matching CheckRequiresGrubQuirk) the
    // firmware makes it difficult to enter its menus if this path
    // does not exist (b:431021440).
    // crdyshim will load and verify crdyboot from either of
    // these locations.
    if (!base::CopyFile(dest_dir.Append("bootx64.efi"),
                        dest_dir.Append("grubx64.efi"))) {
      result = false;
    }
  }

  return result;
}

// Convert a slot string into the BootSlot enum value.
// Returns false when the slot_string is not a valid enum value.
bool StringToSlot(const std::string& slot_string, BootSlot* slot) {
  if (slot_string == "A") {
    *slot = BootSlot::A;
  } else if (slot_string == "B") {
    *slot = BootSlot::B;
  } else {
    return false;
  }
  return true;
}

// Modifies the slot's command line arguments in the boot
// grub.cfg for the update.
//
// The rootfs and dm= arguments will be taken from target kernel.
// The rest of the kernel parameters will come from the grub.cfg
// template in the target rootfs.
//
// Returns true if the boot grub.cfg file was successfully updated.
bool UpdateEfiGrubCfg(const Platform& platform,
                      const InstallConfig& install_config) {
  // Of the form: PARTUUID=XXX-YYY-ZZZ
  string kernel_config =
      platform.DumpKernelConfig(install_config.kernel.device());
  string root_uuid = install_config.root.uuid(platform);
  string kernel_config_dm = ExpandVerityArguments(kernel_config, root_uuid);

  BootSlot slot;
  if (!StringToSlot(install_config.slot, &slot)) {
    LOG(ERROR) << "Invalid slot value.";
    return false;
  }

  // Path to the target grub.cfg to be updated in the EFI partition.
  const base::FilePath boot_grub_path =
      install_config.boot.mount().Append("efi/boot/grub.cfg");
  // Grub.cfg source in the new root filesystem.
  const base::FilePath root_grub_path =
      install_config.root.mount().Append("boot/efi/boot/grub.cfg");

  EfiGrubCfg boot_cfg;
  if (!boot_cfg.LoadFile(boot_grub_path)) {
    LOG(ERROR) << "Unable to read the target grub config.";
    return false;
  }

  EfiGrubCfg root_cfg;
  if (!root_cfg.LoadFile(root_grub_path)) {
    LOG(ERROR) << "Unable to read the source grub kernel config. ";
    return false;
  }

  // Extract the dm and non-dm kernel command lines from the grub config
  // on new rootfs.
  string dm_entry =
      root_cfg.GetKernelCommand(slot, EfiGrubCfg::DmOption::Present);
  if (dm_entry.empty()) {
    LOG(ERROR) << "Unable to to find dm entry from the root grub.cfg";
    return false;
  }
  string no_dm_entry =
      root_cfg.GetKernelCommand(slot, EfiGrubCfg::DmOption::None);
  if (no_dm_entry.empty()) {
    LOG(ERROR) << "Unable to to find non-dm entry from the root grub.cfg";
    return false;
  }

  // Replace the kernel command lines with those taken from the root's
  // grub.cfg.
  if (!boot_cfg.ReplaceKernelCommand(slot, EfiGrubCfg::DmOption::Present,
                                     dm_entry)) {
    LOG(ERROR) << "Unable to update the grub kernel boot options.";
    return false;
  }
  if (!boot_cfg.ReplaceKernelCommand(slot, EfiGrubCfg::DmOption::None,
                                     no_dm_entry)) {
    LOG(ERROR) << "Unable to update the grub kernel boot options.";
    return false;
  }

  // Update the root partition parameters in the boot grub.cfg.
  if (!boot_cfg.UpdateBootParameters(slot, root_uuid, kernel_config_dm)) {
    LOG(ERROR) << "Unable to update the rootfs grub configuration.";
    return false;
  }

  // Write out the new grub.cfg.
  if (!base::WriteFile(boot_grub_path, boot_cfg.ToString())) {
    PLOG(ERROR) << "Unable to write boot menu file: " << boot_grub_path;
    return false;
  }
  return true;
}

bool CheckRequiresGrubQuirk(const Platform& platform) {
  std::optional<std::string> vendor = platform.ReadDmi(DmiKey::kSysVendor);
  std::optional<std::string> product_name =
      platform.ReadDmi(DmiKey::kProductName);

  // If there is no vendor or product name the quirk can't
  // match.
  if (!vendor.has_value() || !product_name.has_value()) {
    // Warn as it is expected this DMI information is available
    // on reven.
    LOG(WARNING) << "Unable to read DMI information.";
    return false;
  }

  if (vendor == kGrubQuirk.sys_vendor &&
      base::StartsWith(*product_name, kGrubQuirk.product_name)) {
    LOG(INFO) << "Matched for the grub copy quirk.";
    return true;
  }
  return false;
}

bool RunEfiPostInstall(const Platform& platform,
                       const InstallConfig& install_config) {
  LOG(INFO) << "Running EfiPostInstall.";

  // Update the kernel we are about to use.
  if (!UpdateLegacyKernel(install_config)) {
    return false;
  }

  if (!UpdateEfiBootloaders(platform, install_config)) {
    return false;
  }

  // Update the grub.cfg configuration files.
  if (!UpdateEfiGrubCfg(platform, install_config)) {
    return false;
  }

  if (!UpdateEfiBootEntries(install_config)) {
    return false;
  }

  // We finished.
  return true;
}

// If crdyboot is in use and the post install context is an update (not
// a fresh install), delete the legacy kernel files from the ESP.
//
// This frees up more space on the ESP for UEFI firmware updates.
//
// Precondition: the boot mode must be kEFI.
//
// Returns true on success (including if there are no files to delete),
// false otherwise.
bool MaybeDeleteLegacyKernels(const InstallConfig& install_config) {
  CHECK_EQ(install_config.bios_type, BiosType::kEFI);

  // Only delete the legacy kernel for updates, not fresh installs.
  //
  // For installs, the user could be booting the USB in a different mode
  // from the mode the installed system is ultimately booted in. See
  // crrev.com/c/3389662 and the linked bugs for details.
  //
  // Also, fresh installs have a larger ESP already, so freeing up space
  // is not necessary.
  if (!install_config.is_update) {
    return true;
  }

  // Only delete the legacy kernel if crdyboot is in use. Grub still
  // uses the legacy kernel.
  const base::FilePath crdyboot_path =
      install_config.boot.mount().Append("efi/boot/crdybootx64.efi");
  if (!base::PathExists(crdyboot_path)) {
    return true;
  }

  const base::FilePath syslinux_dir =
      install_config.boot.mount().Append("syslinux");

  // Note: DeleteFile does not return an error if the file does not exist.
  return brillo::DeleteFile(syslinux_dir.Append("vmlinuz.A")) &&
         brillo::DeleteFile(syslinux_dir.Append("vmlinuz.B"));
}

bool RunNonChromebookPostInstall(const Platform& platform,
                                 const InstallConfig& install_config) {
  switch (install_config.bios_type) {
    case BiosType::kUnknown:
    case BiosType::kSecure:
      return false;

    case BiosType::kUBoot:
      // The Arm platform only uses U-Boot, but may set cros_legacy to mean
      // U-Boot without secure boot modifications. This may need handling.
      if (!RunLegacyUBootPostInstall(install_config)) {
        LOG(ERROR) << "Legacy PostInstall failed.";
        return false;
      }
      return true;

    case BiosType::kLegacy:
      if (!RunLegacyPostInstall(platform, install_config)) {
        LOG(ERROR) << "Legacy PostInstall failed.";
        return false;
      }

      // Configure EFI entries in addition to the legacy.
      // Allows devices that can boot installers in legacy
      // but will boot the installed target in EFI mode.
      // Errors here are not necessarily fatal as the common
      // case is the machine will boot successfully from legacy.
      if (USE_POSTINSTALL_CONFIG_EFI_AND_LEGACY) {
        if (!RunEfiPostInstall(platform, install_config)) {
          LOG(WARNING) << "Ignored secondary EFI PostInstall failure.";
        }
      }

      return true;

    case BiosType::kEFI:
      if (!RunEfiPostInstall(platform, install_config)) {
        LOG(ERROR) << "EFI PostInstall failed.";
        return false;
      }

      // Optionally update the legacy boot entries to support
      // devices that can boot from the USB in EFI mode with the
      // installed disk booting in legacy mode.
      if (USE_POSTINSTALL_CONFIG_EFI_AND_LEGACY) {
        if (!RunLegacyPostInstall(platform, install_config)) {
          LOG(WARNING) << "Ignored secondary Legacy PostInstall failure.";
        }

        // Conditionally free up space on the ESP for UEFI firmware updates.
        if (!MaybeDeleteLegacyKernels(install_config)) {
          LOG(WARNING) << "Ignored MaybeDeleteLegacyKernels failure";
        }
      }

      return true;
  }
}
