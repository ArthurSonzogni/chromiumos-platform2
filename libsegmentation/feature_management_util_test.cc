// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include <brillo/file_utils.h>
#include <base/files/scoped_temp_dir.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "libsegmentation/feature_management_util.h"

namespace segmentation {

using ::testing::Return;

// Test fixture for testing feature management.
class FeatureManagementUtilTest : public ::testing::Test {
 public:
  FeatureManagementUtilTest() = default;
  ~FeatureManagementUtilTest() override = default;
};

TEST_F(FeatureManagementUtilTest, DecodeHwidFail) {
  // Test to check we are finding badly formatted HWID strings.
  EXPECT_FALSE(FeatureManagementUtil::DecodeHWID(""));
  EXPECT_FALSE(FeatureManagementUtil::DecodeHWID("ZZZZ"));
  EXPECT_FALSE(FeatureManagementUtil::DecodeHWID("ZZZ ZZZZ"));
  // Minimum viable HID: 1 bit, 1 EOS, 3 bit of padding, 8 bit of checksum.
  EXPECT_EQ(FeatureManagementUtil::DecodeHWID("ZZZ I6F")->size(), 1);
  EXPECT_EQ(FeatureManagementUtil::DecodeHWID("ZZZ Y6F")->size(), 1);
  // 13 bits but no end of String:
  EXPECT_FALSE(FeatureManagementUtil::DecodeHWID("ZZZ A6F"));
  EXPECT_FALSE(FeatureManagementUtil::DecodeHWID("REDRIX-ZZCR D3A-39-27K-E6B"));
  EXPECT_TRUE(FeatureManagementUtil::DecodeHWID("REDRIX-ZZCR D3A-39F-27K-E6B"));
}

TEST_F(FeatureManagementUtilTest, DecodeHwidValid) {
  EXPECT_EQ(FeatureManagementUtil::DecodeHWID("ZEROONE A2A-797").value(),
            "00000000000001111");
  EXPECT_EQ(
      FeatureManagementUtil::DecodeHWID("REDRIX-ZZCR D3A-39F-27K-E6B").value(),
      "00011001000001101111100101110101010101000");
}

class FeatureManagementUtilRootDevTest : public ::testing::Test {
 public:
  FeatureManagementUtilRootDevTest() = default;
  ~FeatureManagementUtilRootDevTest() override = default;

  void SetUp() override {
    EXPECT_TRUE(_root.CreateUniqueTempDir());
    base::FilePath vars =
        _root.GetPath().Append("usr/sbin/partition_vars.json");

    std::string vars_content = R"""(
{ "load_base_vars": {
   "DEFAULT_ROOTDEV": "
      /sys/devices/pci0000:00/0000:00:17.0/ata*/host*/target*/*/block/sd*
      /sys/devices/pci0000:00/0000:00:1c.*/0000:*:00.0/nvme/nvme*/nvme*n1
      /sys/devices/pci0000:00/0000:00:1a.0/mmc_host/mmc*/mmc*:000*/block/mmcblk*
      /sys/devices/pci0000:00/0000:00:1d.*/0000:*:00.0/nvme/nvme*/nvme*n1
      /sys/devices/pci0000:00/0000:00:06.*/0000:*:00.0/nvme/nvme*/nvme*n1
      /sys/devices/pci0000:00/0000:00:12.7/host*/target*/*/block/sd*"
    }
})""";
    ASSERT_TRUE(brillo::WriteStringToFile(vars, vars_content));
  }

 protected:
  base::ScopedTempDir _root;
};

TEST_F(FeatureManagementUtilRootDevTest, NoDefaultRoot) {
  // No path defined.
  EXPECT_FALSE(FeatureManagementUtil::GetDefaultRoot(_root.GetPath()));
}

TEST_F(FeatureManagementUtilRootDevTest, OneDefaultRoot) {
  // One good path defined.
  EXPECT_TRUE(brillo::TouchFile(
      _root.GetPath().Append("sys/devices/pci0000:00/0000:00:06.1/0000:03:00.0/"
                             "nvme/nvme0/nvme0n1/size")));
  EXPECT_TRUE(FeatureManagementUtil::GetDefaultRoot(_root.GetPath()));
}

TEST_F(FeatureManagementUtilRootDevTest, OneWrongRoot) {
  // One path defined, but does not match any globs.
  EXPECT_TRUE(brillo::TouchFile(
      _root.GetPath().Append("sys/devices/pci0000:00/0000:00:06.1/0000:03:00.0/"
                             "nvme/nvme0/nvme0n2/size")));
  EXPECT_FALSE(FeatureManagementUtil::GetDefaultRoot(_root.GetPath()));
}

}  // namespace segmentation
