// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "installer/chromeos_legacy.h"

#include <base/files/file_enumerator.h>
#include <brillo/files/file_util.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/strings/string_util.h"
#include "installer/chromeos_install_config.h"
#include "installer/mock_platform.h"

using std::string;
using ::testing::_;
using ::testing::Expectation;
using ::testing::Return;

namespace {

std::string ReadFileToString(const base::FilePath& path) {
  std::string contents;
  CHECK(base::ReadFileToString(path, &contents));
  return contents;
}

// this string is a grub file stripped down to (mostly) just what we update.
const char kExampleGrubCfgFile[] =
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

const char kGrubCfgExpectedResult[] =
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

  string expected =
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
  string test_a_dm =
      "linux /syslinux/vmlinuz.A dm=\"with verity\" trailing options";
  EXPECT_TRUE(cfg.ReplaceKernelCommand(
      BootSlot::A, EfiGrubCfg::DmOption::Present, test_a_dm));
  EXPECT_EQ(cfg.GetKernelCommand(BootSlot::A, EfiGrubCfg::DmOption::Present),
            test_a_dm);

  // Confirm the expected line was replaced.
  lines[2] = test_a_dm;
  EXPECT_EQ(cfg.ToString(), base::JoinString(lines, "\n"));

  string test_b_dm =
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

class UpdateEfiBootloadersTest : public ::testing::Test {
 public:
  void SetUp() override {
    CHECK(temp_dir_.CreateUniqueTempDir());

    const auto root_dir = temp_dir_.GetPath().Append("root");
    const auto boot_dir = temp_dir_.GetPath().Append("boot");
    install_config_.root = Partition(base::FilePath(), root_dir);
    install_config_.boot = Partition(base::FilePath(), boot_dir);

    src_dir_ = root_dir.Append("boot/efi/boot");
    dst_dir_ = boot_dir.Append("efi/boot");
  }

 protected:
  base::ScopedTempDir temp_dir_;
  InstallConfig install_config_;
  base::FilePath src_dir_;
  base::FilePath dst_dir_;
};

TEST_F(UpdateEfiBootloadersTest, Success) {
  CHECK(base::CreateDirectory(src_dir_));
  CHECK(base::CreateDirectory(dst_dir_));

  // These files will be copied due to ".efi" and ".sig" extensions.
  CHECK(base::WriteFile(src_dir_.Append("bootia32.efi"), "123"));
  CHECK(base::WriteFile(src_dir_.Append("bootx64.efi"), "456"));
  CHECK(base::WriteFile(src_dir_.Append("bootx64.sig"), "789"));

  // These files won't be copied.
  CHECK(base::WriteFile(src_dir_.Append("bootx64.EFI"), ""));
  CHECK(base::WriteFile(src_dir_.Append("bootx64.txt"), ""));
  CHECK(base::WriteFile(src_dir_.Append("bootx64.efi.bak"), ""));
  CHECK(base::WriteFile(src_dir_.Append("definition"), ""));
  CHECK(base::WriteFile(src_dir_.Append("efi.txt"), ""));

  EXPECT_TRUE(UpdateEfiBootloaders(install_config_));

  // Check files were copied as expected.
  std::string contents;
  EXPECT_TRUE(
      base::ReadFileToString(dst_dir_.Append("bootia32.efi"), &contents));
  EXPECT_EQ(contents, "123");
  EXPECT_TRUE(
      base::ReadFileToString(dst_dir_.Append("bootx64.efi"), &contents));
  EXPECT_EQ(contents, "456");
  EXPECT_TRUE(
      base::ReadFileToString(dst_dir_.Append("bootx64.sig"), &contents));
  EXPECT_EQ(contents, "789");

  // Check that only those files were copied.
  base::FileEnumerator file_enum(dst_dir_, /*recursive=*/false,
                                 base::FileEnumerator::FILES);
  int num_files = 0;
  file_enum.ForEach(
      [&num_files](const base::FilePath& item) { num_files += 1; });
  EXPECT_EQ(num_files, 3);
}

TEST_F(UpdateEfiBootloadersTest, InvalidDestDir) {
  CHECK(base::CreateDirectory(src_dir_));
  CHECK(base::WriteFile(src_dir_.Append("bootx64.efi"), ""));

  // The destination directory does not exist, so the copy operation
  // will fail.
  EXPECT_FALSE(UpdateEfiBootloaders(install_config_));
}

class UpdateLegacyKernelTest : public ::testing::Test {
 public:
  void SetUp() override {
    CHECK(temp_dir_.CreateUniqueTempDir());

    const auto root_dir = temp_dir_.GetPath().Append("root");
    const auto boot_dir = temp_dir_.GetPath().Append("boot");
    install_config_.root = Partition(base::FilePath(), root_dir);
    install_config_.boot = Partition(base::FilePath(), boot_dir);
    install_config_.bios_type = BiosType::kLegacy;
    install_config_.slot = "A";

    src_dir_ = root_dir.Append("boot");
    dst_dir_ = boot_dir.Append("syslinux");

    CHECK(base::CreateDirectory(src_dir_));
    CHECK(base::CreateDirectory(dst_dir_));
  }

 protected:
  base::ScopedTempDir temp_dir_;
  InstallConfig install_config_;
  base::FilePath src_dir_;
  base::FilePath dst_dir_;
};

// Test a successful slot-A update.
TEST_F(UpdateLegacyKernelTest, SlotA) {
  install_config_.slot = "A";
  CHECK(base::WriteFile(src_dir_.Append("vmlinuz"), "new"));
  CHECK(base::WriteFile(dst_dir_.Append("vmlinuz.A"), "kern_a_old"));
  CHECK(base::WriteFile(dst_dir_.Append("vmlinuz.B"), "kern_b_old"));

  EXPECT_TRUE(UpdateLegacyKernel(install_config_));
  // "A" kernel updated, "B" unchanged.
  EXPECT_EQ(ReadFileToString(dst_dir_.Append("vmlinuz.A")), "new");
  EXPECT_EQ(ReadFileToString(dst_dir_.Append("vmlinuz.B")), "kern_b_old");
}

// Test a successful slot-B update.
TEST_F(UpdateLegacyKernelTest, SlotB) {
  install_config_.slot = "B";
  CHECK(base::WriteFile(src_dir_.Append("vmlinuz"), "new"));
  CHECK(base::WriteFile(dst_dir_.Append("vmlinuz.A"), "kern_a_old"));
  CHECK(base::WriteFile(dst_dir_.Append("vmlinuz.B"), "kern_b_old"));

  EXPECT_TRUE(UpdateLegacyKernel(install_config_));
  // "B" kernel updated, "A" unchanged.
  EXPECT_EQ(ReadFileToString(dst_dir_.Append("vmlinuz.A")), "kern_a_old");
  EXPECT_EQ(ReadFileToString(dst_dir_.Append("vmlinuz.B")), "new");
}

// Test that an update fails if the source kernel is missing.
TEST_F(UpdateLegacyKernelTest, ErrorMissingSource) {
  CHECK(base::WriteFile(dst_dir_.Append("vmlinuz.A"), "kern_a_old"));
  EXPECT_FALSE(UpdateLegacyKernel(install_config_));
}

// Test that a fresh install returns success if the source kernel is
// missing and the install type is legacy.
TEST_F(UpdateLegacyKernelTest, MissingSourceLegacyInstall) {
  install_config_.bios_type = BiosType::kLegacy;
  install_config_.is_install = true;
  CHECK(base::WriteFile(dst_dir_.Append("vmlinuz.A"), "kern_a_old"));

  EXPECT_TRUE(UpdateLegacyKernel(install_config_));
}

// Test that a fresh install returns success if the source kernel is
// missing and the install type is EFI.
TEST_F(UpdateLegacyKernelTest, MissingSourceEfiInstall) {
  install_config_.bios_type = BiosType::kEFI;
  install_config_.is_install = true;
  CHECK(base::WriteFile(dst_dir_.Append("vmlinuz.A"), "kern_a_old"));

  EXPECT_TRUE(UpdateLegacyKernel(install_config_));
}

// Test that a fresh legacy install does copy the kernel if the source
// exists.
TEST_F(UpdateLegacyKernelTest, LegacyInstallCopy) {
  install_config_.bios_type = BiosType::kLegacy;
  install_config_.is_install = true;
  CHECK(base::WriteFile(src_dir_.Append("vmlinuz"), "new"));
  CHECK(base::WriteFile(dst_dir_.Append("vmlinuz.A"), "kern_a_old"));

  EXPECT_TRUE(UpdateLegacyKernel(install_config_));
  EXPECT_EQ(ReadFileToString(dst_dir_.Append("vmlinuz.A")), "new");
}

class PostInstallTest : public ::testing::Test {
 public:
  void SetUp() override {
    CHECK(temp_dir_.CreateUniqueTempDir());

    install_config_.root =
        Partition(base::FilePath(), temp_dir_.GetPath().Append("root"));
    install_config_.boot =
        Partition(base::FilePath(), temp_dir_.GetPath().Append("boot"));
    install_config_.bios_type = BiosType::kLegacy;
    install_config_.slot = "A";

    rootfs_boot_ = install_config_.root.mount().Append("boot");
    esp_ = install_config_.boot.mount();

    CHECK(base::CreateDirectory(rootfs_boot_.Append("syslinux")));
    CHECK(base::CreateDirectory(esp_.Append("syslinux")));

    // Create source kernel.
    CHECK(base::WriteFile(rootfs_boot_.Append("vmlinuz"), "vmlinuz"));
    // Create syslinux configs.
    CHECK(base::WriteFile(rootfs_boot_.Append("syslinux/root.A.cfg"),
                          "root=HDROOTA dm=\"DMTABLEA\""));
    CHECK(base::WriteFile(rootfs_boot_.Append("syslinux/syslinux.cfg"),
                          "syslinux_cfg"));

    EXPECT_CALL(platform_, DumpKernelConfig(_)).WillRepeatedly([this]() {
      return kernel_config_;
    });
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

// Test successful call to RunLegacyPostInstall.
TEST_F(PostInstallTest, LegacyPostInstallSuccess) {
  EXPECT_TRUE(RunLegacyPostInstall(platform_, install_config_));

  // Syslinux files were copied.
  EXPECT_EQ(ReadFileToString(esp_.Append("syslinux/syslinux.cfg")),
            "syslinux_cfg");
  // Kernel was copied.
  EXPECT_EQ(ReadFileToString(esp_.Append("syslinux/vmlinuz.A")), "vmlinuz");
}

// Test that RunLegacyPostInstall fails if the source syslinux directory
// is missing.
TEST_F(PostInstallTest, LegacyPostInstallMissingSourceSyslinuxDir) {
  CHECK(brillo::DeleteFile(rootfs_boot_.Append("syslinux/root.A.cfg")));
  CHECK(brillo::DeleteFile(rootfs_boot_.Append("syslinux/syslinux.cfg")));
  CHECK(brillo::DeleteFile(rootfs_boot_.Append("syslinux")));
  EXPECT_FALSE(RunLegacyPostInstall(platform_, install_config_));
}

// Test that RunLegacyPostInstall fails if the source kernel is missing.
TEST_F(PostInstallTest, LegacyPostInstallMissingKernel) {
  CHECK(brillo::DeleteFile(rootfs_boot_.Append("vmlinuz")));
  EXPECT_FALSE(RunLegacyPostInstall(platform_, install_config_));
}

// Test that RunLegacyPostInstall fails if the syslinux config is
// missing.
TEST_F(PostInstallTest, LegacyPostInstallMissingSyslinuxConfig) {
  CHECK(brillo::DeleteFile(rootfs_boot_.Append("syslinux/root.A.cfg")));
  EXPECT_FALSE(RunLegacyPostInstall(platform_, install_config_));
}

// Test that RunLegacyPostInstall fails if the syslinux config is
// missing the HDROOT variable.
TEST_F(PostInstallTest, LegacyPostInstallMissingSyslinuxHdroot) {
  CHECK(base::WriteFile(rootfs_boot_.Append("syslinux/root.A.cfg"),
                        "dm=\"DMTABLEA\""));
  EXPECT_FALSE(RunLegacyPostInstall(platform_, install_config_));
}

// Test that RunLegacyPostInstall fails if the syslinux config is
// missing the DMTABLE variable.
TEST_F(PostInstallTest, LegacyPostInstallMissingSyslinuxDmtable) {
  CHECK(base::WriteFile(rootfs_boot_.Append("syslinux/root.A.cfg"),
                        "root=HDROOTA"));
  EXPECT_FALSE(RunLegacyPostInstall(platform_, install_config_));
}

// Test that RunLegacyPostInstall fails if the kernel config has no "dm" arg.
TEST_F(PostInstallTest, LegacyPostInstallMissingDmArg) {
  kernel_config_ = "";
  EXPECT_FALSE(RunLegacyPostInstall(platform_, install_config_));
}

}  // namespace
