// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "installer/chromeos_legacy.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <base/files/file_enumerator.h>
#include <brillo/files/file_util.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/strings/string_util.h"
#include "installer/chromeos_install_config.h"
#include "installer/chromeos_legacy_private.h"
#include "installer/mock_platform.h"

using ::testing::_;
using ::testing::Expectation;
using ::testing::Return;

namespace {

constexpr Guid kRootAGuid = {{{0xcc6f2e74,
                               0x8803,
                               0x7843,
                               0xb6,
                               0x74,
                               {0x84, 0x81, 0xef, 0x4c, 0xf6, 0x73}}}};

std::string ReadFileToString(const base::FilePath& path) {
  std::string contents;
  CHECK(base::ReadFileToString(path, &contents));
  return contents;
}

// this string is a grub file stripped down to (mostly) just what we update.
constexpr std::string_view kExampleGrubCfgFile =
    "unrelated line\n"
    "\n"
    "  linuxefi /syslinux/vmlinuz.A cros_efi cros_debug "
    "root=PARTUUID=CC6F2E74-8803-7843-B674-8481EF4CF673\n"
    "  linux /syslinux/vmlinuz.B cros_efi cros_debug "
    " root=PARTUUID=5BFD65FE-0398-804A-B090-A201E022A7C6\n"
    "  linuxefi /syslinux/vmlinuz.A cros_efi cros_debug "
    "root=/dev/dm-0 dm=\"DM verity=A\"\n"
    "  linuxefi /syslinux/vmlinuz.B cros_efi cros_debug "
    "root=/dev/dm-0 dm=\"DM verity=B\"\n"
    "  linux (hd0,3)/boot/vmlinuz quiet console=tty2 init=/sbin/init "
    "rootwait ro noresume loglevel=1 noinitrd "
    "root=/dev/sdb3 i915.modeset=1 cros_efi cros_debug\n";

constexpr std::string_view kGrubCfgExpectedResult =
    "unrelated line\n"
    "\n"
    "  linux /syslinux/vmlinuz.A cros_efi cros_debug "
    "root=PARTUUID=fake_root_uuid\n"
    "  linux /syslinux/vmlinuz.B cros_efi cros_debug "
    " root=PARTUUID=5BFD65FE-0398-804A-B090-A201E022A7C6\n"
    "  linux /syslinux/vmlinuz.A cros_efi cros_debug "
    "root=/dev/dm-0 dm=\"verity args\"\n"
    "  linux /syslinux/vmlinuz.B cros_efi cros_debug "
    "root=/dev/dm-0 dm=\"DM verity=B\"\n"
    "  linux (hd0,3)/boot/vmlinuz quiet console=tty2 init=/sbin/init "
    "rootwait ro noresume loglevel=1 noinitrd "
    "root=/dev/sdb3 i915.modeset=1 cros_efi cros_debug\n";

// Example of a real rootfs grub.cfg, stripped down to just the slot-A lines.
constexpr std::string_view kRootGrubCfgNoVerity =
    "linux /syslinux/vmlinuz.A quiet init=/sbin/init rootwait ro noresume "
    " loglevel=7 noinitrd console= kvm-intel.vmentry_l1d_flush=always "
    " i915.modeset=1 cros_efi cros_debug root=/dev/$linuxpartA \n";
constexpr std::string_view kRootGrubCfgVerity =
    "linux /syslinux/vmlinuz.A quiet init=/sbin/init rootwait ro noresume "
    " loglevel=7 noinitrd console= kvm-intel.vmentry_l1d_flush=always "
    " dm_verity.error_behavior=3 dm_verity.max_bios=-1 dm_verity.dev_wait=1 "
    " i915.modeset=1 cros_efi cros_debug root=/dev/dm-0 dm=\"DMTABLEA\" \n";

// Very stripped-down grub.cfg, used in tests as the grub.cfg on the ESP
// prior to updating.
constexpr std::string_view kEspOriginalGrubCfg =
    "linux /syslinux/vmlinuz.A "
    " root=PARTUUID=CC6F2E74-8803-7843-B674-8481EF4CF673 \n"
    ""
    "linux /syslinux/vmlinuz.A root=/dev/dm-0 dm=\"orig DM args\" \n";

// Result of updating kEspOriginalGrubCfg with kRootGrubCfg, plus the DM
// args read from DumpKernelConfig.
constexpr std::string_view kEspUpdatedGrubCfg =
    "linux /syslinux/vmlinuz.A quiet init=/sbin/init rootwait ro noresume "
    " loglevel=7 noinitrd console= kvm-intel.vmentry_l1d_flush=always "
    " i915.modeset=1 cros_efi cros_debug"
    " root=PARTUUID=CC6F2E74-8803-7843-B674-8481EF4CF673 \n"
    ""
    "linux /syslinux/vmlinuz.A quiet init=/sbin/init rootwait ro noresume "
    " loglevel=7 noinitrd console= kvm-intel.vmentry_l1d_flush=always "
    " dm_verity.error_behavior=3 dm_verity.max_bios=-1 dm_verity.dev_wait=1 "
    " i915.modeset=1 cros_efi cros_debug root=/dev/dm-0 dm=\"dm args\" \n";

class EfiGrubCfgTest : public ::testing::Test {
 public:
  void SetUp() override {
    CHECK(scoped_temp_dir_.CreateUniqueTempDir());
    cfg_path_ = scoped_temp_dir_.GetPath().Append("boot.cfg");
  }

 protected:
  base::ScopedTempDir scoped_temp_dir_;
  base::FilePath cfg_path_;
};

TEST_F(EfiGrubCfgTest, LoadCfg) {
  CHECK(base::WriteFile(cfg_path_, kExampleGrubCfgFile));

  EfiGrubCfg cfg;
  EXPECT_TRUE(cfg.LoadFile(cfg_path_));
  EXPECT_EQ(cfg.ToString(), kExampleGrubCfgFile);

  EXPECT_FALSE(cfg.LoadFile(scoped_temp_dir_.GetPath()));
}

TEST_F(EfiGrubCfgTest, EfiGrubCfgFullReplace) {
  CHECK(base::WriteFile(cfg_path_, kExampleGrubCfgFile));

  EfiGrubCfg cfg;
  EXPECT_TRUE(cfg.LoadFile(cfg_path_));
  cfg.UpdateBootParameters(BootSlot::A, "fake_root_uuid", "verity args");
  // Confirm full string has proper replacements of arguments
  // as well as linuxefi replaced with linux.
  EXPECT_EQ(cfg.ToString(), kGrubCfgExpectedResult);
}

TEST_F(EfiGrubCfgTest, GetKernelCommand) {
  CHECK(base::WriteFile(
      cfg_path_,
      "unrelated line\n"
      "\n"
      "  linux /syslinux/vmlinuz.A cros_efi cros_debug "
      "root=PARTUUID=fake_root_uuid\n"
      "  linux /syslinux/vmlinuz.B cros_efi cros_debug "
      "root=PARTUUID=5BFD65FE-0398-804A-B090-A201E022A7C6\n"
      "  linux /syslinux/vmlinuz.A cros_efi cros_debug "
      "root=/dev/dm-0 dm=\"verity args\"\n"
      "  linux /syslinux/vmlinuz.B cros_efi cros_debug "
      "root=/dev/dm-0 dm=\"DM verity=B\"\n"
      "  linux (hd0,3)/boot/vmlinuz quiet console=tty2 init=/sbin/init "
      "rootwait ro noresume loglevel=1 noinitrd "
      "root=/dev/sdb3 i915.modeset=1 cros_efi cros_debug\n"));

  EfiGrubCfg cfg;
  ASSERT_TRUE(cfg.LoadFile(cfg_path_));

  EXPECT_EQ(cfg.GetKernelCommand(BootSlot::A, EfiGrubCfg::DmOption::None),
            "  linux /syslinux/vmlinuz.A cros_efi cros_debug "
            "root=PARTUUID=fake_root_uuid");

  EXPECT_EQ(cfg.GetKernelCommand(BootSlot::A, EfiGrubCfg::DmOption::Present),
            "  linux /syslinux/vmlinuz.A cros_efi cros_debug "
            "root=/dev/dm-0 dm=\"verity args\"");

  EXPECT_EQ(cfg.GetKernelCommand(BootSlot::B, EfiGrubCfg::DmOption::None),
            "  linux /syslinux/vmlinuz.B cros_efi cros_debug "
            "root=PARTUUID=5BFD65FE-0398-804A-B090-A201E022A7C6");

  EXPECT_EQ(cfg.GetKernelCommand(BootSlot::B, EfiGrubCfg::DmOption::Present),
            "  linux /syslinux/vmlinuz.B cros_efi cros_debug "
            "root=/dev/dm-0 dm=\"DM verity=B\"");
}

TEST_F(EfiGrubCfgTest, FixupLinuxEfi) {
  CHECK(base::WriteFile(
      cfg_path_,
      // Example legacy cfg with linuxefi specified.
      "  linuxefi /syslinux/vmlinuz.A root=PARTUUID=xyz\n"
      "  linux /syslinux/vmlinuz.B root=PARTUUID=zzz\n"
      "  linuxefi /syslinux/vmlinuz.A root=/dev/dm-0 dm=\"DM verity=A\"\n"
      "  linuxefi /syslinux/vmlinuz.B root=/dev/dm-0 dm=\"DM verity=B\"\n"
      "  linux (hd0,3)/boot/vmlinuz quiet console=tty2 init=/sbin/init "
      "rootwait ro noresume loglevel=1 noinitrd "
      "root=/dev/sdb3 i915.modeset=1 cros_efi cros_debug\n"));

  constexpr std::string_view expected =
      "  linux /syslinux/vmlinuz.A root=PARTUUID=xyz\n"
      "  linux /syslinux/vmlinuz.B root=PARTUUID=fake_root_uuid\n"
      "  linux /syslinux/vmlinuz.A root=/dev/dm-0 dm=\"DM verity=A\"\n"
      "  linux /syslinux/vmlinuz.B root=/dev/dm-0 dm=\"verity args\"\n"
      "  linux (hd0,3)/boot/vmlinuz quiet console=tty2 init=/sbin/init "
      "rootwait ro noresume loglevel=1 noinitrd "
      "root=/dev/sdb3 i915.modeset=1 cros_efi cros_debug\n";

  EfiGrubCfg cfg;
  ASSERT_TRUE(cfg.LoadFile(cfg_path_));

  // UpdateBootParameters is expected to replace all linuxefi commands.
  cfg.UpdateBootParameters(BootSlot::B, "fake_root_uuid", "verity args");
  EXPECT_EQ(cfg.ToString(), expected);
}

TEST_F(EfiGrubCfgTest, ReplaceKernelCommand) {
  std::vector<std::string> lines = {
      "nothing to see here",
      "",
      "  linux /syslinux/vmlinuz.A dm=\"A dm args\" moreargs cros_efi",
      "  linux /syslinux/vmlinuz.B norootb moreargs cros_efi",
      "  linux /syslinux/vmlinuz.A noroota moreargs cros_efi",
      "  linux /syslinux/vmlinuz.B dm=\"B dm args\"",
      "  linux /syslinux/vmlinuz.B dm=\"B dm args_two\"",
      "trailing line"};

  CHECK(base::WriteFile(cfg_path_, base::JoinString(lines, "\n")));

  EfiGrubCfg cfg;
  ASSERT_TRUE(cfg.LoadFile(cfg_path_));
  // Replace an entry with a "A" slot dm= entry.
  const std::string test_a_dm =
      "linux /syslinux/vmlinuz.A dm=\"with verity\" trailing options";
  EXPECT_TRUE(cfg.ReplaceKernelCommand(
      BootSlot::A, EfiGrubCfg::DmOption::Present, test_a_dm));
  EXPECT_EQ(cfg.GetKernelCommand(BootSlot::A, EfiGrubCfg::DmOption::Present),
            test_a_dm);

  // Confirm the expected line was replaced.
  lines[2] = test_a_dm;
  EXPECT_EQ(cfg.ToString(), base::JoinString(lines, "\n"));

  const std::string test_b_dm =
      "linux /syslinux/vmlinuz.B dm=\" verity args\" trailing options";
  EXPECT_TRUE(cfg.ReplaceKernelCommand(
      BootSlot::B, EfiGrubCfg::DmOption::Present, test_b_dm));
  EXPECT_EQ(cfg.GetKernelCommand(BootSlot::B, EfiGrubCfg::DmOption::Present),
            test_b_dm);

  // Check that all B dm= lines are replaced.
  // Unknown if this is a requirement however the original code
  // would have worked this way.
  lines[5] = test_b_dm;
  lines[6] = test_b_dm;
  EXPECT_EQ(cfg.ToString(), base::JoinString(lines, "\n"));
}

class PostInstallTest : public ::testing::Test {
 public:
  void SetUp() override {
    CHECK(temp_dir_.CreateUniqueTempDir());

    install_config_.root = Partition(base::FilePath("/dev/sda3"),
                                     temp_dir_.GetPath().Append("root"));
    install_config_.boot = Partition(base::FilePath("/dev/sda12"),
                                     temp_dir_.GetPath().Append("boot"));
    install_config_.bios_type = BiosType::kLegacy;
    install_config_.slot = "A";

    rootfs_boot_ = install_config_.root.mount().Append("boot");
    esp_ = install_config_.boot.mount();

    CHECK(base::CreateDirectory(rootfs_boot_.Append("syslinux")));
    CHECK(base::CreateDirectory(rootfs_boot_.Append("efi/boot")));
    CHECK(base::CreateDirectory(esp_.Append("syslinux")));
    CHECK(base::CreateDirectory(esp_.Append("efi/boot")));

    // Create files in the rootfs boot dir.
    // Create source kernel.
    CHECK(base::WriteFile(rootfs_boot_.Append("vmlinuz"), "vmlinuz"));
    // Create syslinux configs.
    CHECK(base::WriteFile(rootfs_boot_.Append("syslinux/root.A.cfg"),
                          "root=HDROOTA dm=\"DMTABLEA\""));
    CHECK(base::WriteFile(rootfs_boot_.Append("syslinux/root.B.cfg"),
                          "root=HDROOTB dm=\"DMTABLEB\""));
    CHECK(base::WriteFile(rootfs_boot_.Append("syslinux/syslinux.cfg"),
                          "syslinux_cfg"));
    // Create EFI bootloader files.
    CHECK(base::WriteFile(rootfs_boot_.Append("efi/boot/bootia32.efi"),
                          "bootia32_efi"));
    CHECK(base::WriteFile(rootfs_boot_.Append("efi/boot/bootx64.efi"),
                          "bootx64_efi"));
    CHECK(base::WriteFile(rootfs_boot_.Append("efi/boot/bootx64.sig"),
                          "bootx64_sig"));
    CHECK(base::WriteFile(
        rootfs_boot_.Append("efi/boot/grub.cfg"),
        std::string(kRootGrubCfgNoVerity) + std::string(kRootGrubCfgVerity)));

    // Create files on the ESP.
    CHECK(
        base::WriteFile(esp_.Append("efi/boot/grub.cfg"), kEspOriginalGrubCfg));

    EXPECT_CALL(platform_, DumpKernelConfig(_)).WillRepeatedly([this]() {
      return kernel_config_;
    });

    EXPECT_CALL(platform_, GetPartitionUniqueId(_, PartitionNum::ROOT_A))
        .WillRepeatedly(Return(kRootAGuid));
  }

 protected:
  base::ScopedTempDir temp_dir_;
  InstallConfig install_config_;
  MockPlatform platform_;

  // Path of the `<rootfs>/boot` directory.
  base::FilePath rootfs_boot_;
  // Path of the ESP mount point.
  base::FilePath esp_;

  std::string kernel_config_{"dm=\"dm args\""};
};

class UpdateEfiBootloadersTest : public PostInstallTest {};

TEST_F(UpdateEfiBootloadersTest, Success) {
  // Create some files that won't be copied since they don't have ".efi"
  // or ".sig" extensions.
  CHECK(base::WriteFile(rootfs_boot_.Append("efi/boot/bootx64.EFI"), ""));
  CHECK(base::WriteFile(rootfs_boot_.Append("efi/boot/bootx64.txt"), ""));
  CHECK(base::WriteFile(rootfs_boot_.Append("efi/boot/bootx64.efi.bak"), ""));
  CHECK(base::WriteFile(rootfs_boot_.Append("efi/boot/definition"), ""));
  CHECK(base::WriteFile(rootfs_boot_.Append("efi/boot/efi.txt"), ""));

  EXPECT_TRUE(UpdateEfiBootloaders(platform_, install_config_));

  // Check files were copied as expected.
  EXPECT_EQ(ReadFileToString(esp_.Append("efi/boot/bootia32.efi")),
            "bootia32_efi");
  EXPECT_EQ(ReadFileToString(esp_.Append("efi/boot/bootx64.efi")),
            "bootx64_efi");
  EXPECT_EQ(ReadFileToString(esp_.Append("efi/boot/bootx64.sig")),
            "bootx64_sig");

  // Check that only those files were copied.
  base::FileEnumerator file_enum(esp_.Append("efi/boot"), /*recursive=*/false,
                                 base::FileEnumerator::FILES);
  int num_files = 0;
  file_enum.ForEach(
      [&num_files](const base::FilePath& item) { num_files += 1; });
  // 3 files copied, plus grub.cfg already present.
  const int expected_num_files = 4;
  EXPECT_EQ(num_files, expected_num_files);
}

TEST_F(UpdateEfiBootloadersTest, InvalidDestDir) {
  CHECK(brillo::DeletePathRecursively(esp_.Append("efi/boot")));

  // The destination directory does not exist, so the copy operation
  // will fail.
  EXPECT_FALSE(UpdateEfiBootloaders(platform_, install_config_));
}

class UpdateLegacyKernelTest : public PostInstallTest {};

// Test a successful slot-A update.
TEST_F(UpdateLegacyKernelTest, SlotA) {
  install_config_.slot = "A";
  CHECK(base::WriteFile(esp_.Append("syslinux/vmlinuz.A"), "kern_a_old"));
  CHECK(base::WriteFile(esp_.Append("syslinux/vmlinuz.B"), "kern_b_old"));

  EXPECT_TRUE(UpdateLegacyKernel(install_config_));
  // "A" kernel updated, "B" unchanged.
  EXPECT_EQ(ReadFileToString(esp_.Append("syslinux/vmlinuz.A")), "vmlinuz");
  EXPECT_EQ(ReadFileToString(esp_.Append("syslinux/vmlinuz.B")), "kern_b_old");
}

// Test a successful slot-B update.
TEST_F(UpdateLegacyKernelTest, SlotB) {
  install_config_.slot = "B";
  CHECK(base::WriteFile(esp_.Append("syslinux/vmlinuz.A"), "kern_a_old"));
  CHECK(base::WriteFile(esp_.Append("syslinux/vmlinuz.B"), "kern_b_old"));

  EXPECT_TRUE(UpdateLegacyKernel(install_config_));
  // "B" kernel updated, "A" unchanged.
  EXPECT_EQ(ReadFileToString(esp_.Append("syslinux/vmlinuz.A")), "kern_a_old");
  EXPECT_EQ(ReadFileToString(esp_.Append("syslinux/vmlinuz.B")), "vmlinuz");
}

// Test that an update fails if the source kernel is missing.
TEST_F(UpdateLegacyKernelTest, ErrorMissingSource) {
  CHECK(brillo::DeleteFile(rootfs_boot_.Append("vmlinuz")));
  CHECK(base::WriteFile(esp_.Append("syslinux/vmlinuz.A"), "kern_a_old"));
  EXPECT_FALSE(UpdateLegacyKernel(install_config_));
}

// Test that a fresh install returns success if the source kernel is
// missing and the install type is legacy.
TEST_F(UpdateLegacyKernelTest, MissingSourceLegacyInstall) {
  install_config_.bios_type = BiosType::kLegacy;
  install_config_.is_install = true;
  CHECK(brillo::DeleteFile(rootfs_boot_.Append("vmlinuz")));
  CHECK(base::WriteFile(esp_.Append("syslinux/vmlinuz.A"), "kern_a_old"));

  EXPECT_TRUE(UpdateLegacyKernel(install_config_));
}

// Test that a fresh install returns success if the source kernel is
// missing and the install type is EFI.
TEST_F(UpdateLegacyKernelTest, MissingSourceEfiInstall) {
  install_config_.bios_type = BiosType::kEFI;
  install_config_.is_install = true;
  CHECK(brillo::DeleteFile(rootfs_boot_.Append("vmlinuz")));
  CHECK(base::WriteFile(esp_.Append("syslinux/vmlinuz.A"), "kern_a_old"));

  EXPECT_TRUE(UpdateLegacyKernel(install_config_));
}

// Test that a fresh legacy install does copy the kernel if the source
// exists.
TEST_F(UpdateLegacyKernelTest, LegacyInstallCopy) {
  install_config_.bios_type = BiosType::kLegacy;
  install_config_.is_install = true;
  CHECK(base::WriteFile(esp_.Append("syslinux/vmlinuz.A"), "kern_a_old"));

  EXPECT_TRUE(UpdateLegacyKernel(install_config_));
  EXPECT_EQ(ReadFileToString(esp_.Append("syslinux/vmlinuz.A")), "vmlinuz");
}

class RunLegacyPostInstallTest : public PostInstallTest {};

// Test successful call to RunLegacyPostInstall.
TEST_F(RunLegacyPostInstallTest, Success) {
  EXPECT_TRUE(RunLegacyPostInstall(platform_, install_config_));

  // Syslinux files were copied.
  EXPECT_EQ(ReadFileToString(esp_.Append("syslinux/syslinux.cfg")),
            "syslinux_cfg");
  // Syslinux root config variables were updated as expected.
  EXPECT_EQ(
      ReadFileToString(esp_.Append("syslinux/root.A.cfg")),
      "root=PARTUUID=CC6F2E74-8803-7843-B674-8481EF4CF673 dm=\"dm args\"");
  // Kernel was copied.
  EXPECT_EQ(ReadFileToString(esp_.Append("syslinux/vmlinuz.A")), "vmlinuz");
}

// Test that RunLegacyPostInstall does not clobber existing syslinux
// files when copying.
TEST_F(RunLegacyPostInstallTest, NoClobber) {
  // Create a syslinux config file that should not be clobbered by
  // RunLegacyPostInstall.
  CHECK(base::WriteFile(esp_.Append("syslinux/root.B.cfg"), "old B cfg"));

  EXPECT_TRUE(RunLegacyPostInstall(platform_, install_config_));

  // Existing config not clobbered.
  CHECK(base::WriteFile(esp_.Append("syslinux/root.B.cfg"), "old B cfg"));
}

// Test that RunLegacyPostInstall fails if the source syslinux directory
// is missing.
TEST_F(RunLegacyPostInstallTest, ErrorMissingSourceSyslinuxDir) {
  CHECK(brillo::DeletePathRecursively(rootfs_boot_.Append("syslinux")));
  EXPECT_FALSE(RunLegacyPostInstall(platform_, install_config_));
}

// Test that RunLegacyPostInstall fails if the source kernel is missing.
TEST_F(RunLegacyPostInstallTest, ErrorMissingKernel) {
  CHECK(brillo::DeleteFile(rootfs_boot_.Append("vmlinuz")));
  EXPECT_FALSE(RunLegacyPostInstall(platform_, install_config_));
}

// Test that RunLegacyPostInstall fails if the syslinux config is
// missing.
TEST_F(RunLegacyPostInstallTest, ErrorMissingSyslinuxConfig) {
  CHECK(brillo::DeleteFile(rootfs_boot_.Append("syslinux/root.A.cfg")));
  EXPECT_FALSE(RunLegacyPostInstall(platform_, install_config_));
}

// Test that RunLegacyPostInstall fails if the syslinux config is
// missing the HDROOT variable.
TEST_F(RunLegacyPostInstallTest, ErrorMissingSyslinuxHdroot) {
  CHECK(base::WriteFile(rootfs_boot_.Append("syslinux/root.A.cfg"),
                        "dm=\"DMTABLEA\""));
  EXPECT_FALSE(RunLegacyPostInstall(platform_, install_config_));
}

// Test that RunLegacyPostInstall fails if the syslinux config is
// missing the DMTABLE variable.
TEST_F(RunLegacyPostInstallTest, ErrorMissingSyslinuxDmtable) {
  CHECK(base::WriteFile(rootfs_boot_.Append("syslinux/root.A.cfg"),
                        "root=HDROOTA"));
  EXPECT_FALSE(RunLegacyPostInstall(platform_, install_config_));
}

// Test that RunLegacyPostInstall fails if the kernel config has no "dm" arg.
TEST_F(RunLegacyPostInstallTest, ErrorMissingDmArg) {
  kernel_config_ = "";
  EXPECT_FALSE(RunLegacyPostInstall(platform_, install_config_));
}

class UpdateEfiGrubCfgTest : public PostInstallTest {};

// Test successful call to UpdateEfiGrubCfg.
TEST_F(UpdateEfiGrubCfgTest, Success) {
  EXPECT_TRUE(UpdateEfiGrubCfg(platform_, install_config_));
  EXPECT_EQ(ReadFileToString(esp_.Append("efi/boot/grub.cfg")),
            kEspUpdatedGrubCfg);
}

// Test that UpdateEfiGrubCfg fails with an invalid slot.
TEST_F(UpdateEfiGrubCfgTest, ErrorInvalidSlot) {
  install_config_.slot = "C";
  EXPECT_FALSE(UpdateEfiGrubCfg(platform_, install_config_));
}

// Test that UpdateEfiGrubCfg fails if the ESP grub.cfg is missing.
TEST_F(UpdateEfiGrubCfgTest, ErrorMissingEspConfig) {
  CHECK(brillo::DeleteFile(esp_.Append("efi/boot/grub.cfg")));
  EXPECT_FALSE(UpdateEfiGrubCfg(platform_, install_config_));
}

// Test that UpdateEfiGrubCfg fails if the rootfs grub.cfg is missing.
TEST_F(UpdateEfiGrubCfgTest, ErrorMissingRootfsConfig) {
  CHECK(brillo::DeleteFile(rootfs_boot_.Append("efi/boot/grub.cfg")));
  EXPECT_FALSE(UpdateEfiGrubCfg(platform_, install_config_));
}

// Test that UpdateEfiGrubCfg fails if the rootfs grub.cfg is missing
// the entry with verity enabled.
TEST_F(UpdateEfiGrubCfgTest, ErrorMissingRootfsVerityEntry) {
  CHECK(base::WriteFile(rootfs_boot_.Append("efi/boot/grub.cfg"),
                        kRootGrubCfgNoVerity));
  EXPECT_FALSE(UpdateEfiGrubCfg(platform_, install_config_));
}

// Test that UpdateEfiGrubCfg fails if the rootfs grub.cfg is missing
// the entry without verity.
TEST_F(UpdateEfiGrubCfgTest, ErrorMissingRootfsNonVerityEntry) {
  CHECK(base::WriteFile(rootfs_boot_.Append("efi/boot/grub.cfg"),
                        kRootGrubCfgVerity));
  EXPECT_FALSE(UpdateEfiGrubCfg(platform_, install_config_));
}

// Test that UpdateEfiGrubCfg fails if the ESP grub.cfg is missing
// the entry with verity enabled.
TEST_F(UpdateEfiGrubCfgTest, ErrorMissingEspVerityEntry) {
  CHECK(
      base::WriteFile(esp_.Append("efi/boot/grub.cfg"), kRootGrubCfgNoVerity));
  EXPECT_FALSE(UpdateEfiGrubCfg(platform_, install_config_));
}

// Test that UpdateEfiGrubCfg fails if the ESP grub.cfg is missing
// the entry without verity.
TEST_F(UpdateEfiGrubCfgTest, ErrorMissingEspNonVerityEntry) {
  CHECK(base::WriteFile(esp_.Append("efi/boot/grub.cfg"), kRootGrubCfgVerity));
  EXPECT_FALSE(UpdateEfiGrubCfg(platform_, install_config_));
}

class RunEfiPostInstallTest : public PostInstallTest {};

// Test a successful call to RunEfiPostInstall.
TEST_F(RunEfiPostInstallTest, Success) {
  CHECK(base::WriteFile(esp_.Append("syslinux/vmlinuz.A"), "kern_a_old"));
  CHECK(
      base::WriteFile(esp_.Append("efi/boot/bootx64.efi"), "bootx64_efi_old"));
  EXPECT_TRUE(RunEfiPostInstall(platform_, install_config_));

  // Kernel was updated.
  EXPECT_EQ(ReadFileToString(esp_.Append("syslinux/vmlinuz.A")), "vmlinuz");
  // Bootloader was updated.
  EXPECT_EQ(ReadFileToString(esp_.Append("efi/boot/bootx64.efi")),
            "bootx64_efi");
  // Grub config was updated.
  EXPECT_EQ(ReadFileToString(esp_.Append("efi/boot/grub.cfg")),
            kEspUpdatedGrubCfg);
}

// Test that RunEfiPostInstall fails if UpdateLegacyKernel fails.
TEST_F(RunEfiPostInstallTest, ErrorUpdateLegacyKernel) {
  CHECK(brillo::DeleteFile(rootfs_boot_.Append("vmlinuz")));
  EXPECT_FALSE(RunEfiPostInstall(platform_, install_config_));
}

// Test that RunEfiPostInstall fails if UpdateEfiBootloaders fails.
TEST_F(RunEfiPostInstallTest, ErrorUpdateEfiBootloaders) {
  CHECK(brillo::DeletePathRecursively(esp_.Append("efi/boot")));
  EXPECT_FALSE(RunEfiPostInstall(platform_, install_config_));
}

// Test that RunEfiPostInstall fails if UpdateEfiGrubCfg fails.
TEST_F(RunEfiPostInstallTest, ErrorUpdateEfiGrubCfg) {
  CHECK(brillo::DeleteFile(esp_.Append("efi/boot/grub.cfg")));
  EXPECT_FALSE(RunEfiPostInstall(platform_, install_config_));
}

class MaybeDeleteLegacyKernelsTest : public PostInstallTest {
  void SetUp() override {
    PostInstallTest::SetUp();

    install_config_.bios_type = BiosType::kEFI;

    // Create legacy kernels on the ESP.
    CHECK(base::WriteFile(esp_.Append("syslinux/vmlinuz.A"), "kern_a"));
    CHECK(base::WriteFile(esp_.Append("syslinux/vmlinuz.B"), "kern_a"));

    // Create crdyboot on the ESP.
    CHECK(base::WriteFile(esp_.Append("efi/boot/crdybootx64.efi"), "crdyboot"));
  }
};

// Test that MaybeDeleteLegacyKernels deletes the kernels on an update
// booted via crdyboot.
TEST_F(MaybeDeleteLegacyKernelsTest, UpdateWithCrdyboot) {
  install_config_.is_update = true;
  EXPECT_TRUE(MaybeDeleteLegacyKernels(install_config_));

  EXPECT_FALSE(base::PathExists(esp_.Append("syslinux/vmlinuz.A")));
  EXPECT_FALSE(base::PathExists(esp_.Append("syslinux/vmlinuz.B")));
}

// Test that MaybeDeleteLegacyKernels does nothing if the update was not
// booted via crdyboot.
TEST_F(MaybeDeleteLegacyKernelsTest, UpdateWithoutCrdyboot) {
  CHECK(brillo::DeleteFile(esp_.Append("efi/boot/crdybootx64.efi")));

  install_config_.is_update = true;
  EXPECT_TRUE(MaybeDeleteLegacyKernels(install_config_));

  // Kernel was not deleted.
  EXPECT_TRUE(base::PathExists(esp_.Append("syslinux/vmlinuz.A")));
}

// Test that MaybeDeleteLegacyKernels does nothing for fresh installs.
TEST_F(MaybeDeleteLegacyKernelsTest, FreshInstall) {
  install_config_.is_update = false;
  EXPECT_TRUE(MaybeDeleteLegacyKernels(install_config_));

  // Kernel was not deleted.
  EXPECT_TRUE(base::PathExists(esp_.Append("syslinux/vmlinuz.A")));
}

class RunNonChromebookPostInstallTest : public PostInstallTest {};

// Test that RunNonChromebookPostInstall fails with bios_type kSecure.
TEST_F(RunNonChromebookPostInstallTest, ErrorSecure) {
  install_config_.bios_type = BiosType::kSecure;
  EXPECT_FALSE(RunNonChromebookPostInstall(platform_, install_config_));
}

// Test a successful call to RunNonChromebookPostInstall with bios_type
// kLegacy.
TEST_F(RunNonChromebookPostInstallTest, Legacy) {
  install_config_.bios_type = BiosType::kLegacy;
  EXPECT_TRUE(RunNonChromebookPostInstall(platform_, install_config_));

  // A syslinux file was copied.
  EXPECT_EQ(ReadFileToString(esp_.Append("syslinux/syslinux.cfg")),
            "syslinux_cfg");
  if (USE_POSTINSTALL_CONFIG_EFI_AND_LEGACY) {
    // A UEFI bootloader was copied.
    EXPECT_EQ(ReadFileToString(esp_.Append("efi/boot/bootx64.efi")),
              "bootx64_efi");
  } else {
    // A UEFI bootloader was not copied.
    EXPECT_FALSE(base::PathExists(esp_.Append("efi/boot/bootx64.efi")));
  }
}

// Test that an error in RunLegacyPostInstall is fatal with bios_type
// kLegacy.
TEST_F(RunNonChromebookPostInstallTest, ErrorLegacy) {
  install_config_.bios_type = BiosType::kLegacy;
  CHECK(brillo::DeleteFile(rootfs_boot_.Append("syslinux/root.A.cfg")));

  EXPECT_FALSE(RunNonChromebookPostInstall(platform_, install_config_));
}

// Test that an error from RunEfiPostInstall is not fatal with bios_type
// kLegacy.
TEST_F(RunNonChromebookPostInstallTest, LegacyNonFatalUefiError) {
  install_config_.bios_type = BiosType::kLegacy;
  CHECK(brillo::DeletePathRecursively(esp_.Append("efi/boot")));

  EXPECT_TRUE(RunNonChromebookPostInstall(platform_, install_config_));
}

// Test a successful call to RunNonChromebookPostInstall with bios_type
// kEFI.
TEST_F(RunNonChromebookPostInstallTest, Uefi) {
  install_config_.bios_type = BiosType::kEFI;
  EXPECT_TRUE(RunNonChromebookPostInstall(platform_, install_config_));

  // A UEFI bootloader was copied.
  EXPECT_EQ(ReadFileToString(esp_.Append("efi/boot/bootx64.efi")),
            "bootx64_efi");
  if (USE_POSTINSTALL_CONFIG_EFI_AND_LEGACY) {
    // A syslinux file was copied.
    EXPECT_EQ(ReadFileToString(esp_.Append("syslinux/syslinux.cfg")),
              "syslinux_cfg");
  } else {
    // A syslinux file was not copied.
    EXPECT_FALSE(base::PathExists(esp_.Append("syslinux/syslinux.cfg")));
  }
}

// Test that RunNonChromebookPostInstall conditionally deletes legacy
// kernels.
TEST_F(RunNonChromebookPostInstallTest, UefiKernelDelete) {
  CHECK(base::WriteFile(esp_.Append("syslinux/vmlinuz.A"), "kern_a"));
  CHECK(base::WriteFile(esp_.Append("efi/boot/crdybootx64.efi"), "crdyboot"));
  install_config_.bios_type = BiosType::kEFI;
  install_config_.is_update = true;
  EXPECT_TRUE(RunNonChromebookPostInstall(platform_, install_config_));

  if (USE_POSTINSTALL_CONFIG_EFI_AND_LEGACY) {
    // The kernel was deleted.
    EXPECT_FALSE(base::PathExists(esp_.Append("syslinux/vmlinuz.A")));
  } else {
    // The kernel still exists.
    EXPECT_TRUE(base::PathExists(esp_.Append("syslinux/vmlinuz.A")));
  }
}

// Test that an error in RunEfiPostInstall is fatal with bios_type
// kEFI.
TEST_F(RunNonChromebookPostInstallTest, ErrorUefi) {
  install_config_.bios_type = BiosType::kEFI;
  CHECK(brillo::DeletePathRecursively(esp_.Append("efi/boot")));

  EXPECT_FALSE(RunNonChromebookPostInstall(platform_, install_config_));
}

// Test that an error from RunLegacyPostInstall is not fatal with
// bios_type kEFI.
TEST_F(RunNonChromebookPostInstallTest, UefiNonFatalLegacyError) {
  install_config_.bios_type = BiosType::kEFI;
  CHECK(brillo::DeleteFile(rootfs_boot_.Append("syslinux/root.A.cfg")));

  EXPECT_TRUE(RunNonChromebookPostInstall(platform_, install_config_));
}

TEST(GrubQuirkTest, MatchSuccess) {
  MockPlatform platform_;

  EXPECT_CALL(platform_, ReadDmi(DmiKey::kSysVendor)).WillOnce(Return("Acer"));
  EXPECT_CALL(platform_, ReadDmi(DmiKey::kProductName))
      .WillOnce(Return("TravelMate Spin B3"));

  EXPECT_TRUE(CheckRequiresGrubQuirk(platform_));
}

TEST(GrubQuirkTest, NoValue) {
  MockPlatform platform_;

  EXPECT_CALL(platform_, ReadDmi(_)).WillRepeatedly(Return(std::nullopt));

  EXPECT_FALSE(CheckRequiresGrubQuirk(platform_));
}

TEST(GrubQuirkTest, WrongProduct) {
  MockPlatform platform_;

  EXPECT_CALL(platform_, ReadDmi(DmiKey::kSysVendor)).WillOnce(Return("Acer"));
  EXPECT_CALL(platform_, ReadDmi(DmiKey::kProductName))
      .WillOnce(Return("Not A TravelMate"));

  EXPECT_FALSE(CheckRequiresGrubQuirk(platform_));
}

TEST(GrubQuirkTest, WrongVendor) {
  MockPlatform platform_;

  EXPECT_CALL(platform_, ReadDmi(DmiKey::kSysVendor))
      .WillOnce(Return(std::nullopt));
  EXPECT_CALL(platform_, ReadDmi(DmiKey::kProductName))
      .WillOnce(Return("TravelMate Spin B3"));

  EXPECT_FALSE(CheckRequiresGrubQuirk(platform_));
}

class UpdateEfiBootloadersQuirkedTest : public PostInstallTest {
  void SetUp() override {
    PostInstallTest::SetUp();

    install_config_.bios_type = BiosType::kEFI;

    // Create crdyboot in the source.
    CHECK(base::WriteFile(rootfs_boot_.Append("efi/boot/crdybootx64.efi"),
                          "crdyboot"));
  }

 protected:
  // Set expectations that the DMI information will
  // result in a required grub quirk.
  void ExpectGrubMatchDMI() {
    EXPECT_CALL(platform_, ReadDmi(DmiKey::kSysVendor))
        .WillRepeatedly(Return("Acer"));
    EXPECT_CALL(platform_, ReadDmi(DmiKey::kProductName))
        .WillRepeatedly(Return("TravelMate Spin B3"));
  }
};

TEST_F(UpdateEfiBootloadersQuirkedTest, SuccessApplied) {
  ExpectGrubMatchDMI();

  EXPECT_TRUE(UpdateEfiBootloaders(platform_, install_config_));

  EXPECT_EQ(ReadFileToString(esp_.Append("efi/boot/bootx64.efi")),
            "bootx64_efi");
  // Confirm grubx64.efi matches the contents of bootx64.efi.
  // This is the case when the quirk applies.
  EXPECT_EQ(ReadFileToString(esp_.Append("efi/boot/grubx64.efi")),
            "bootx64_efi");
}

TEST_F(UpdateEfiBootloadersQuirkedTest, NoCrdyboot) {
  ExpectGrubMatchDMI();

  CHECK(brillo::DeleteFile(rootfs_boot_.Append("efi/boot/crdybootx64.efi")));

  EXPECT_TRUE(UpdateEfiBootloaders(platform_, install_config_));

  EXPECT_EQ(ReadFileToString(esp_.Append("efi/boot/bootx64.efi")),
            "bootx64_efi");
  // The grubx64.efi should not be created if crdyboot isn't in use.
  EXPECT_FALSE(base::PathExists(esp_.Append("efi/boot/grubx64.efi")));
}

TEST_F(UpdateEfiBootloadersQuirkedTest, NoQuirkNeeded) {
  EXPECT_CALL(platform_, ReadDmi(DmiKey::kSysVendor)).WillOnce(Return("Acer"));
  // Product name does not match a quirk.
  EXPECT_CALL(platform_, ReadDmi(DmiKey::kProductName))
      .WillOnce(Return("Not A TravelMate"));

  EXPECT_TRUE(UpdateEfiBootloaders(platform_, install_config_));

  EXPECT_EQ(ReadFileToString(esp_.Append("efi/boot/bootx64.efi")),
            "bootx64_efi");
  // The grub path isn't created when the quirk isn't applied.
  EXPECT_FALSE(base::PathExists(esp_.Append("efi/boot/grubx64.efi")));
}

}  // namespace
