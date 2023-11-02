//
// Copyright (C) 2015 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include "update_engine/cros/boot_control_chromeos.h"

#include <base/strings/stringprintf.h>
#include <brillo/blkdev_utils/lvm.h>
#include <brillo/blkdev_utils/lvm_device.h>
#include <brillo/blkdev_utils/mock_lvm.h>
#include <gtest/gtest.h>

using std::string;

namespace chromeos_update_engine {

class BootControlChromeOSTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // We don't run Init() for bootctl_, we set its internal values instead.
    bootctl_.num_slots_ = 2;
    bootctl_.current_slot_ = 0;
    bootctl_.boot_disk_name_ = "/dev/null";
  }

  BootControlChromeOS bootctl_;  // BootControlChromeOS under test.
};

TEST_F(BootControlChromeOSTest, GetFirstInactiveSlot) {
  bootctl_.current_slot_ = 0;
  EXPECT_EQ(1, bootctl_.GetFirstInactiveSlot());
  bootctl_.current_slot_ = 1;
  EXPECT_EQ(0, bootctl_.GetFirstInactiveSlot());
}

TEST_F(BootControlChromeOSTest, SysfsBlockDeviceTest) {
  EXPECT_EQ("/sys/block/sda", bootctl_.SysfsBlockDevice("/dev/sda"));
  EXPECT_EQ("", bootctl_.SysfsBlockDevice("/foo/sda"));
  EXPECT_EQ("", bootctl_.SysfsBlockDevice("/dev/foo/bar"));
  EXPECT_EQ("", bootctl_.SysfsBlockDevice("/"));
  EXPECT_EQ("", bootctl_.SysfsBlockDevice("./"));
  EXPECT_EQ("", bootctl_.SysfsBlockDevice(""));
}

TEST_F(BootControlChromeOSTest, GetPartitionNumberTest) {
  // The partition name should not be case-sensitive.
  EXPECT_EQ(2, bootctl_.GetPartitionNumber("kernel", 0));
  EXPECT_EQ(2, bootctl_.GetPartitionNumber("boot", 0));
  EXPECT_EQ(2, bootctl_.GetPartitionNumber("KERNEL", 0));
  EXPECT_EQ(2, bootctl_.GetPartitionNumber("BOOT", 0));

  EXPECT_EQ(3, bootctl_.GetPartitionNumber("ROOT", 0));
  EXPECT_EQ(3, bootctl_.GetPartitionNumber("system", 0));

  EXPECT_EQ(3, bootctl_.GetPartitionNumber("ROOT", 0));
  EXPECT_EQ(3, bootctl_.GetPartitionNumber("system", 0));

  // Slot B.
  EXPECT_EQ(4, bootctl_.GetPartitionNumber("KERNEL", 1));
  EXPECT_EQ(5, bootctl_.GetPartitionNumber("ROOT", 1));

  // Slot C doesn't exists.
  EXPECT_EQ(-1, bootctl_.GetPartitionNumber("KERNEL", 2));
  EXPECT_EQ(-1, bootctl_.GetPartitionNumber("ROOT", 2));

  // MiniOS slots.
  EXPECT_EQ(10, bootctl_.GetPartitionNumber("minios", 1));
  EXPECT_EQ(10, bootctl_.GetPartitionNumber("MINIOS", 1));
  EXPECT_EQ(9, bootctl_.GetPartitionNumber("minios", 0));
  EXPECT_EQ(9, bootctl_.GetPartitionNumber("MINIOS", 0));

  // Non A/B partitions are ignored.
  EXPECT_EQ(-1, bootctl_.GetPartitionNumber("OEM", 0));
  EXPECT_EQ(-1, bootctl_.GetPartitionNumber("A little panda", 0));
}

TEST_F(BootControlChromeOSTest, ParseDlcPartitionNameTest) {
  string id, package;

  EXPECT_TRUE(bootctl_.ParseDlcPartitionName("dlc/id/package", &id, &package));
  EXPECT_EQ(id, "id");
  EXPECT_EQ(package, "package");

  EXPECT_FALSE(
      bootctl_.ParseDlcPartitionName("dlc-foo/id/package", &id, &package));
  EXPECT_FALSE(
      bootctl_.ParseDlcPartitionName("dlc-foo/id/package/", &id, &package));
  EXPECT_FALSE(bootctl_.ParseDlcPartitionName("dlc/id", &id, &package));
  EXPECT_FALSE(bootctl_.ParseDlcPartitionName("dlc/id/", &id, &package));
  EXPECT_FALSE(bootctl_.ParseDlcPartitionName("dlc//package", &id, &package));
  EXPECT_FALSE(bootctl_.ParseDlcPartitionName("dlc", &id, &package));
  EXPECT_FALSE(bootctl_.ParseDlcPartitionName("foo", &id, &package));
}

TEST_F(BootControlChromeOSTest, GetMiniOSVersionTest) {
  const std::string kKey = std::string(kMiniOSVersionKey) + "=";
  const std::string kVersion = "4018.0.0.1";
  auto key_value = kKey + kVersion;

  // Normal input.
  std::string output = kKey + kVersion;
  std::string value;
  EXPECT_TRUE(bootctl_.GetMiniOSVersion(output, &value));
  EXPECT_EQ(value, kVersion);

  // Extra white space on both sides.
  output = base::StringPrintf("   %s    key=value", key_value.c_str());
  EXPECT_TRUE(bootctl_.GetMiniOSVersion(output, &value));
  EXPECT_EQ(value, kVersion);

  // Quotes on both sides.
  output = base::StringPrintf("  \"%s\"", key_value.c_str());
  EXPECT_TRUE(bootctl_.GetMiniOSVersion(output, &value));
  EXPECT_EQ(value, kVersion);

  // Quotes and spaces.
  output = base::StringPrintf("%s\"  ", key_value.c_str());
  EXPECT_TRUE(bootctl_.GetMiniOSVersion(output, &value));
  EXPECT_EQ(value, kVersion);

  // Badly formatted in the value of another key.
  output = base::StringPrintf("cros_list=\"%s \"", key_value.c_str());
  EXPECT_TRUE(bootctl_.GetMiniOSVersion(output, &value));
  EXPECT_EQ(value, kVersion);

  // With other key value pairs.
  output = base::StringPrintf("noinitrd version=60   %s\" \'kern_guid=78",
                              key_value.c_str());
  EXPECT_TRUE(bootctl_.GetMiniOSVersion(output, &value));
  EXPECT_EQ(value, kVersion);

  // Key but no value.
  output = base::StringPrintf("\"%s", kKey.c_str());
  EXPECT_FALSE(bootctl_.GetMiniOSVersion(output, &value));

  // Caps should not match.
  output = "CROS_minios_version=" + kVersion;
  EXPECT_FALSE(bootctl_.GetMiniOSVersion(output, &value));

  // No kKey-value separator.
  output = "cros_minios_version" + value;
  EXPECT_FALSE(bootctl_.GetMiniOSVersion(output, &value));
}

#if USE_LVM_STATEFUL_PARTITION
TEST_F(BootControlChromeOSTest, IsLvmStackEnabledTest) {
  std::optional<brillo::PhysicalVolume> opt;
  opt = brillo::PhysicalVolume(base::FilePath("/foo/bar"), nullptr);
  brillo::MockLogicalVolumeManager mock_lvm;
  EXPECT_CALL(mock_lvm, GetPhysicalVolume(_)).WillOnce(Return(opt));
  EXPECT_TRUE(bootctl_.IsLvmStackEnabled(&mock_lvm));

  // Check caching too.
  EXPECT_TRUE(bootctl_.IsLvmStackEnabled(&mock_lvm));
}

TEST_F(BootControlChromeOSTest, IsLvmStackEnabledInvalidPhysicalVolumeTest) {
  std::optional<brillo::PhysicalVolume> opt;
  opt = brillo::PhysicalVolume(base::FilePath(), nullptr);
  brillo::MockLogicalVolumeManager mock_lvm;
  EXPECT_CALL(mock_lvm, GetPhysicalVolume(_)).WillOnce(Return(opt));
  EXPECT_FALSE(bootctl_.IsLvmStackEnabled(&mock_lvm));

  // Check caching too.
  EXPECT_FALSE(bootctl_.IsLvmStackEnabled(&mock_lvm));
}
#endif  // USE_LVM_STATEFUL_PARTITION

}  // namespace chromeos_update_engine
