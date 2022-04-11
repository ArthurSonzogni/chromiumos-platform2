// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/utils/fwupd_utils.h"

#include <optional>

#include <gtest/gtest.h>

namespace diagnostics {
namespace fwupd_utils {
namespace {

TEST(FwupdUtilsTest, DeviceContainsVendorId) {
  auto device_info = DeviceInfo();
  device_info.joined_vendor_id = "USB:0x1234|PCI:0x5678";

  EXPECT_TRUE(ContainsVendorId(device_info, "USB:0x1234"));
  EXPECT_TRUE(ContainsVendorId(device_info, "PCI:0x5678"));

  EXPECT_FALSE(ContainsVendorId(device_info, "USB:0x4321"));
}

TEST(FwupdUtilsTest, DeviceContainsVendorIdWrongFormat) {
  auto device_info = DeviceInfo();
  device_info.joined_vendor_id = "USB:0x1234|PCI:0x5678";

  EXPECT_FALSE(ContainsVendorId(device_info, "1234"));
}

TEST(FwupdUtilsTest, DeviceContainsVendorIdNull) {
  auto device_info = DeviceInfo();

  EXPECT_FALSE(ContainsVendorId(device_info, "USB:0x1234"));
  EXPECT_FALSE(ContainsVendorId(device_info, ""));
}

TEST(FwupdUtilsTest, InstanceIdToGuid) {
  EXPECT_EQ(InstanceIdToGuid("USB\\VID_0A5C&PID_6412&REV_0001"),
            "52fd36dc-5904-5936-b114-d98e9d410b25");
  EXPECT_EQ(InstanceIdToGuid("USB\\VID_0A5C&PID_6412"),
            "7a1ba7b9-6bcd-54a4-8a36-d60cc5ee935c");
  EXPECT_EQ(InstanceIdToGuid("USB\\VID_0A5C"),
            "ddfc8e56-df0d-582e-af12-c7fa171233dc");
}

TEST(FwupdUtilsTest, InstanceIdToGuidConversionFails) {
  EXPECT_EQ(InstanceIdToGuid(""), std::nullopt);
}

}  // namespace
}  // namespace fwupd_utils
}  // namespace diagnostics
