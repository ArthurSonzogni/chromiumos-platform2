// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/system/usb_device_info.h"

#include <base/files/file_util.h>
#include <gtest/gtest.h>
#include <memory>
#include "base/files/file_path.h"
#include "diagnostics/base/file_test_utils.h"

namespace diagnostics {

namespace {

constexpr char kUSBDeviceInfoFileContent[] =
    "# This is a comment line\n"
    " \n"
    "\n"
    "18d1:4e11 mobile\n"
    "0bda:0138 sd\n";

}  // namespace

class USBDeviceInfoTest : public BaseFileTest {
 public:
  void SetUp() override {
    ASSERT_TRUE(WriteFileAndCreateParentDirs(
        GetRootDir().Append(kRelativeUSBDeviceInfoFile),
        kUSBDeviceInfoFileContent));
    info_ = std::make_unique<USBDeviceInfo>();
  }

 protected:
  std::unique_ptr<USBDeviceInfo> info_;
};

TEST_F(USBDeviceInfoTest, GetDeviceMediaType) {
  EXPECT_EQ(DeviceType::kMobile, info_->GetDeviceMediaType("18d1", "4e11"));
  EXPECT_EQ(DeviceType::kSD, info_->GetDeviceMediaType("0bda", "0138"));
  EXPECT_EQ(DeviceType::kUSB, info_->GetDeviceMediaType("1234", "5678"));
}

TEST_F(USBDeviceInfoTest, ConvertToDeviceMediaType) {
  EXPECT_EQ(DeviceType::kMobile, info_->ConvertToDeviceMediaType("mobile"));
  EXPECT_EQ(DeviceType::kSD, info_->ConvertToDeviceMediaType("sd"));
  EXPECT_EQ(DeviceType::kUSB, info_->ConvertToDeviceMediaType("usb"));
  EXPECT_EQ(DeviceType::kUSB, info_->ConvertToDeviceMediaType(""));
  EXPECT_EQ(DeviceType::kUSB, info_->ConvertToDeviceMediaType("foo"));
}

TEST_F(USBDeviceInfoTest, IsLineSkippable) {
  EXPECT_TRUE(info_->IsLineSkippable(""));
  EXPECT_TRUE(info_->IsLineSkippable("  "));
  EXPECT_TRUE(info_->IsLineSkippable("\t"));
  EXPECT_TRUE(info_->IsLineSkippable("#"));
  EXPECT_TRUE(info_->IsLineSkippable("# this is a comment"));
  EXPECT_TRUE(info_->IsLineSkippable(" # this is a comment"));
  EXPECT_TRUE(info_->IsLineSkippable("# this is a comment "));
  EXPECT_TRUE(info_->IsLineSkippable("\t#this is a comment"));
  EXPECT_FALSE(info_->IsLineSkippable("this is not a comment"));
}

}  // namespace diagnostics
