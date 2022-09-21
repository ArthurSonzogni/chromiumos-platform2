// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "installer/chromeos_install_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include "installer/chromeos_postinst.h"
#include "installer/inst_util.h"

using std::string;

void TestConfigureInstall(const string& install_dev,
                          const string& install_dir,
                          bool expected_success,
                          const string& expected_slot,
                          const string& expected_root,
                          const string& expected_kernel,
                          const string& expected_boot) {
  InstallConfig install_config;

  BiosType expected_bios = BiosType::kSecure;
  DeferUpdateAction defer_update_action = DeferUpdateAction::kAuto;

  EXPECT_EQ(ConfigureInstall(install_dev, install_dir, expected_bios,
                             defer_update_action, &install_config),
            expected_success);

  if (!expected_success)
    return;

  EXPECT_EQ(install_config.slot, expected_slot);
  EXPECT_EQ(install_config.root.device(), expected_root);
  EXPECT_EQ(install_config.kernel.device(), expected_kernel);
  EXPECT_EQ(install_config.boot.device(), expected_boot);
  EXPECT_EQ(install_config.bios_type, expected_bios);
  EXPECT_EQ(install_config.defer_update_action, DeferUpdateAction::kAuto);
}

void TestStrToBiosType(string name,
                       bool expected_success,
                       BiosType expected_result) {
  BiosType bios_type = BiosType::kUnknown;

  EXPECT_EQ(StrToBiosType(name, &bios_type), expected_success);

  if (!expected_success)
    return;

  EXPECT_EQ(bios_type, expected_result);
}

void TestKernelConfigToBiosType(string kernel_config,
                                bool expected_success,
                                BiosType expected_result) {
  BiosType bios_type = BiosType::kUnknown;

  EXPECT_EQ(KernelConfigToBiosType(kernel_config, &bios_type),
            expected_success);

  if (!expected_success)
    return;

  EXPECT_EQ(bios_type, expected_result);
}

class InstallConfigTest : public ::testing::Test {};

TEST(InstallConfigTest, ConfigureInstallTest) {
  TestConfigureInstall("/dev/sda3", "/mnt", true, "A", "/dev/sda3", "/dev/sda2",
                       "/dev/sda12");
  TestConfigureInstall("/dev/sda5", "/mnt", true, "B", "/dev/sda5", "/dev/sda4",
                       "/dev/sda12");
  TestConfigureInstall("/dev/mmcblk0p3", "/mnt", true, "A", "/dev/mmcblk0p3",
                       "/dev/mmcblk0p2", "/dev/mmcblk0p12");
  TestConfigureInstall("/dev/mmcblk0p5", "/mnt", true, "B", "/dev/mmcblk0p5",
                       "/dev/mmcblk0p4", "/dev/mmcblk0p12");
  TestConfigureInstall("/dev/sda2", "/mnt", false, "", "", "", "");
  TestConfigureInstall("/dev/sda", "/mnt", false, "", "", "", "");
}

TEST(InstallConfigTest, StrToBiosTypeTest) {
  TestStrToBiosType("secure", true, BiosType::kSecure);
  TestStrToBiosType("uboot", true, BiosType::kUBoot);
  TestStrToBiosType("legacy", true, BiosType::kLegacy);
  TestStrToBiosType("efi", true, BiosType::kEFI);
  TestStrToBiosType("fuzzy", false, BiosType::kUnknown);
}

TEST(InstallConfigTest, KernelConfigToBiosTypeTest) {
  BiosType legacy_bios = BiosType::kLegacy;
#ifdef __arm__
  legacy_bios = BiosType::kUBoot;
#endif

  TestKernelConfigToBiosType("kernel_config cros_secure", true,
                             BiosType::kSecure);
  TestKernelConfigToBiosType("cros_legacy kernel_config", true, legacy_bios);
  TestKernelConfigToBiosType("kernel_config cros_efi foo", true,
                             BiosType::kEFI);
  TestKernelConfigToBiosType("kernel_config", false, BiosType::kUnknown);
}
