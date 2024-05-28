// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/detailed_hardware_data.h"

#include <memory>
#include <optional>

#include <chromeos/constants/flex_hwis.h>
#include <base/files/file.h>
#include <base/files/scoped_temp_dir.h>
#include <gtest/gtest.h>
#include <policy/mock_device_policy.h>

#include "crash-reporter/paths.h"
#include "crash-reporter/test_util.h"

using ::testing::Contains;
using ::testing::IsEmpty;
using ::testing::Pair;
using ::testing::Return;

class DmiInfoTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    test_dir_ = temp_dir_.GetPath();
    paths::SetPrefixForTesting(test_dir_);
  }

  void TearDown() override { paths::SetPrefixForTesting(base::FilePath()); }

  void PopulateDmiFile(std::string filename, std::string content) {
    ASSERT_TRUE(test_util::CreateFile(
        test_dir_.Append("sys/class/dmi/id/").Append(filename), content));
  }

  base::ScopedTempDir temp_dir_;
  base::FilePath test_dir_;
};

TEST_F(DmiInfoTest, MissingDmiFiles) {
  auto dmi_info = detailed_hardware_data::DmiModelInfo();
  EXPECT_THAT(dmi_info, IsEmpty());
}

TEST_F(DmiInfoTest, EmptyDmiFiles) {
  PopulateDmiFile("product_name", "");
  PopulateDmiFile("product_version", "");
  PopulateDmiFile("sys_vendor", "");

  auto dmi_info = detailed_hardware_data::DmiModelInfo();
  EXPECT_THAT(dmi_info, Contains(Pair("chromeosflex_product_name", "")));
  EXPECT_THAT(dmi_info, Contains(Pair("chromeosflex_product_version", "")));
  EXPECT_THAT(dmi_info, Contains(Pair("chromeosflex_product_vendor", "")));
}

TEST_F(DmiInfoTest, WithOrWithoutNewline) {
  // Check a few variations of newline and whitespace.
  PopulateDmiFile("product_name", "with newline and trailing space \n");
  PopulateDmiFile("product_version", "without newline but trailing space ");
  PopulateDmiFile("sys_vendor", "double newline\n\n");

  auto dmi_info = detailed_hardware_data::DmiModelInfo();
  EXPECT_THAT(dmi_info, Contains(Pair("chromeosflex_product_name",
                                      "with newline and trailing space ")));
  EXPECT_THAT(dmi_info, Contains(Pair("chromeosflex_product_version",
                                      "without newline but trailing space ")));
  EXPECT_THAT(dmi_info, Contains(Pair("chromeosflex_product_vendor",
                                      "double newline\n")));
}

// > Strings must be encoded as UTF-8 with no byte order mark (BOM). For
// compatibility with older SMBIOS parsers, US-ASCII characters should be used.
// from SMBIOS reference spec, section 6.1.3 Text strings
// https://www.dmtf.org/sites/default/files/standards/documents/DSP0134_3.7.0.pdf
// So we probably don't need to test this, but it can't hurt?
TEST_F(DmiInfoTest, Utf8DmiFiles) {
  PopulateDmiFile("product_name", "Њ");
  PopulateDmiFile("product_version", "Ћ");
  PopulateDmiFile("sys_vendor", "Џ");

  auto dmi_info = detailed_hardware_data::DmiModelInfo();
  EXPECT_THAT(dmi_info, Contains(Pair("chromeosflex_product_name", "Њ")));
  EXPECT_THAT(dmi_info, Contains(Pair("chromeosflex_product_version", "Ћ")));
  EXPECT_THAT(dmi_info, Contains(Pair("chromeosflex_product_vendor", "Џ")));
}

class ComponentInfoTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    test_dir_ = temp_dir_.GetPath();
    paths::SetPrefixForTesting(test_dir_);
  }

  void TearDown() override { paths::SetPrefixForTesting(base::FilePath()); }

  void PopulateHwCacheFile(std::string filename, std::string content) {
    ASSERT_TRUE(test_util::CreateFile(
        test_dir_.Append(flex_hwis::kFlexHardwareCacheDir).Append(filename),
        content));
  }

  base::ScopedTempDir temp_dir_;
  base::FilePath test_dir_;
};

TEST_F(ComponentInfoTest, MissingHwFiles) {
  auto component_info = detailed_hardware_data::FlexComponentInfo();
  EXPECT_THAT(component_info, IsEmpty());
}

// Simple test of read/write all strings.
TEST_F(ComponentInfoTest, AllHwFields) {
  PopulateHwCacheFile(flex_hwis::kFlexBiosVersionKey, "N1MET37W");
  PopulateHwCacheFile(flex_hwis::kFlexCpuNameKey,
                      "Intel(R) Core(TM) i5-2520M CPU @ 2.50GHz");
  PopulateHwCacheFile(flex_hwis::kFlexEthernetIdKey, "pci:8086:1502");
  PopulateHwCacheFile(
      flex_hwis::kFlexEthernetNameKey,
      "Intel Corporation 82579LM Gigabit Network Connection (Lewisville)");
  PopulateHwCacheFile(flex_hwis::kFlexWirelessIdKey, "pci:8086:0085");
  PopulateHwCacheFile(flex_hwis::kFlexWirelessNameKey,
                      "Centrino Advanced-N 6205 [Taylor Peak]");
  PopulateHwCacheFile(flex_hwis::kFlexBluetoothIdKey, "usb:03f0:231d");
  PopulateHwCacheFile(flex_hwis::kFlexBluetoothNameKey,
                      "HP, Inc Broadcom 2070 Bluetooth Combo");
  PopulateHwCacheFile(flex_hwis::kFlexGpuIdKey, "pci:8086:0126");
  PopulateHwCacheFile(
      flex_hwis::kFlexGpuNameKey,
      "Intel Corporation 2nd Generation Core Processor Family Integrated "
      "Graphics Controller");
  PopulateHwCacheFile(flex_hwis::kFlexTouchpadStackKey, "libinput");
  PopulateHwCacheFile(flex_hwis::kFlexTpmVersionKey, "1.2");
  PopulateHwCacheFile(flex_hwis::kFlexTpmSpecLevelKey, "8589934594");
  PopulateHwCacheFile(flex_hwis::kFlexTpmManufacturerKey, "1229346816");

  auto component_info = detailed_hardware_data::FlexComponentInfo();

  EXPECT_THAT(component_info,
              Contains(Pair("chromeosflex_bios_version", "N1MET37W")));
  EXPECT_THAT(component_info,
              Contains(Pair("chromeosflex_cpu_name",
                            "Intel(R) Core(TM) i5-2520M CPU @ 2.50GHz")));
  EXPECT_THAT(component_info,
              Contains(Pair("chromeosflex_ethernet_id", "pci:8086:1502")));
  EXPECT_THAT(component_info,
              Contains(Pair("chromeosflex_ethernet_name",
                            "Intel Corporation 82579LM Gigabit Network "
                            "Connection (Lewisville)")));
  EXPECT_THAT(component_info,
              Contains(Pair("chromeosflex_wireless_id", "pci:8086:0085")));
  EXPECT_THAT(component_info,
              Contains(Pair("chromeosflex_wireless_name",
                            "Centrino Advanced-N 6205 [Taylor Peak]")));
  EXPECT_THAT(component_info,
              Contains(Pair("chromeosflex_bluetooth_id", "usb:03f0:231d")));
  EXPECT_THAT(component_info,
              Contains(Pair("chromeosflex_bluetooth_name",
                            "HP, Inc Broadcom 2070 Bluetooth Combo")));
  EXPECT_THAT(component_info,
              Contains(Pair("chromeosflex_gpu_id", "pci:8086:0126")));
  EXPECT_THAT(component_info,
              Contains(Pair("chromeosflex_gpu_name",
                            "Intel Corporation 2nd Generation Core Processor "
                            "Family Integrated Graphics Controller")));
  EXPECT_THAT(component_info,
              Contains(Pair("chromeosflex_touchpad", "libinput")));
  EXPECT_THAT(component_info,
              Contains(Pair("chromeosflex_tpm_version", "1.2")));
  EXPECT_THAT(component_info,
              Contains(Pair("chromeosflex_tpm_spec_level", "8589934594")));
  EXPECT_THAT(component_info,
              Contains(Pair("chromeosflex_tpm_manufacturer", "1229346816")));
}

TEST_F(ComponentInfoTest, TestMaxComponentSize) {
  // Longer than |kHardwareComponentMaxSize|.
  const std::string long_string(512, 'x');
  PopulateHwCacheFile(flex_hwis::kFlexBiosVersionKey, long_string);

  auto component_info = detailed_hardware_data::FlexComponentInfo();
  EXPECT_THAT(component_info, IsEmpty());
}

class ComponentInfoPolicyTest : public testing::Test {
 protected:
  void SetUp() override {
    device_policy_ = std::make_unique<policy::MockDevicePolicy>();
  }

  std::unique_ptr<policy::MockDevicePolicy> device_policy_;
};

TEST_F(ComponentInfoPolicyTest, TestEnrolledAllowed) {
  EXPECT_CALL(*device_policy_, IsEnterpriseEnrolled()).WillOnce(Return(true));
  EXPECT_CALL(*device_policy_, GetEnrolledHwDataUsageEnabled())
      .WillOnce(Return(true));

  EXPECT_TRUE(detailed_hardware_data::FlexComponentInfoAllowedByPolicy(
      *device_policy_));
}

TEST_F(ComponentInfoPolicyTest, TestEnrolledDisallowed) {
  EXPECT_CALL(*device_policy_, IsEnterpriseEnrolled()).WillOnce(Return(true));
  EXPECT_CALL(*device_policy_, GetEnrolledHwDataUsageEnabled())
      .WillOnce(Return(false));

  EXPECT_FALSE(detailed_hardware_data::FlexComponentInfoAllowedByPolicy(
      *device_policy_));
}

TEST_F(ComponentInfoPolicyTest, TestEnrolledNotReadable) {
  EXPECT_CALL(*device_policy_, IsEnterpriseEnrolled()).WillOnce(Return(true));
  EXPECT_CALL(*device_policy_, GetEnrolledHwDataUsageEnabled())
      .WillOnce(Return(std::nullopt));

  EXPECT_FALSE(detailed_hardware_data::FlexComponentInfoAllowedByPolicy(
      *device_policy_));
}

TEST_F(ComponentInfoPolicyTest, TestUnenrolledAllowed) {
  EXPECT_CALL(*device_policy_, IsEnterpriseEnrolled()).WillOnce(Return(false));
  EXPECT_CALL(*device_policy_, GetUnenrolledHwDataUsageEnabled())
      .WillOnce(Return(true));

  EXPECT_TRUE(detailed_hardware_data::FlexComponentInfoAllowedByPolicy(
      *device_policy_));
}

TEST_F(ComponentInfoPolicyTest, TestUnenrolledDisallowed) {
  EXPECT_CALL(*device_policy_, IsEnterpriseEnrolled()).WillOnce(Return(false));
  EXPECT_CALL(*device_policy_, GetUnenrolledHwDataUsageEnabled())
      .WillOnce(Return(false));

  EXPECT_FALSE(detailed_hardware_data::FlexComponentInfoAllowedByPolicy(
      *device_policy_));
}

TEST_F(ComponentInfoPolicyTest, TestUnenrolledNotReadable) {
  EXPECT_CALL(*device_policy_, IsEnterpriseEnrolled()).WillOnce(Return(false));
  EXPECT_CALL(*device_policy_, GetUnenrolledHwDataUsageEnabled())
      .WillOnce(Return(std::nullopt));

  EXPECT_FALSE(detailed_hardware_data::FlexComponentInfoAllowedByPolicy(
      *device_policy_));
}
