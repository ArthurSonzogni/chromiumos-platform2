// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>

#include <brillo/udev/mock_udev_device.h>

#include "diagnostics/common/file_test_utils.h"
#include "diagnostics/cros_healthd/utils/usb_utils.h"
#include "diagnostics/cros_healthd/utils/usb_utils_constants.h"

namespace diagnostics {
namespace {

using ::testing::Return;

constexpr char kFakePathUsbDevices[] =
    "sys/devices/pci0000:00/0000:00:14.0/usb1";
constexpr char kFakeUsbVendorName[] = "Usb Vendor";
constexpr auto kFakeUsbProductName = "Usb Product";
constexpr auto kFakeUsbFallbackVendorName = "Fallback Vendor Name";
constexpr auto kFakeUsbFallbackProductName = "Fallback Product Name";

class UsbUtilsTest : public BaseFileTest {
 public:
  UsbUtilsTest() = default;
  UsbUtilsTest(const UsbUtilsTest&) = delete;
  UsbUtilsTest& operator=(const UsbUtilsTest&) = delete;
  ~UsbUtilsTest() = default;

  void SetUp() override {
    CreateTestRoot();

    dev_ = std::make_unique<brillo::MockUdevDevice>();
    fake_dev_path_ = GetPathUnderRoot(kFakePathUsbDevices);

    SetFile({kFakePathUsbDevices, kFileUsbManufacturerName},
            kFakeUsbFallbackVendorName);
    SetFile({kFakePathUsbDevices, kFileUsbProductName},
            kFakeUsbFallbackProductName);
  }

  brillo::MockUdevDevice& mock_dev() {
    return *reinterpret_cast<brillo::MockUdevDevice*>(dev_.get());
  }

 protected:
  std::unique_ptr<brillo::UdevDevice> dev_;
  base::FilePath fake_dev_path_;
};

TEST_F(UsbUtilsTest, TestFetchVendor) {
  brillo::MockUdevDevice dev;
  EXPECT_CALL(mock_dev(), GetPropertyValue(kPropertieVendor))
      .WillOnce(Return(kFakeUsbVendorName));
  EXPECT_EQ(GetUsbVendorName(dev_), kFakeUsbVendorName);
}

TEST_F(UsbUtilsTest, TestFetchProduct) {
  brillo::MockUdevDevice dev;
  EXPECT_CALL(mock_dev(), GetPropertyValue(kPropertieProduct))
      .WillOnce(Return(kFakeUsbProductName));
  EXPECT_EQ(GetUsbProductName(dev_), kFakeUsbProductName);
}

TEST_F(UsbUtilsTest, TestFetchVendorFallback) {
  brillo::MockUdevDevice dev;
  EXPECT_CALL(mock_dev(), GetPropertyValue(kPropertieVendor))
      .WillOnce(Return(nullptr));
  EXPECT_CALL(mock_dev(), GetSysPath())
      .WillOnce(Return(fake_dev_path_.value().c_str()));
  EXPECT_EQ(GetUsbVendorName(dev_), kFakeUsbFallbackVendorName);
}

TEST_F(UsbUtilsTest, TestFetchProductFallback) {
  brillo::MockUdevDevice dev;
  EXPECT_CALL(mock_dev(), GetPropertyValue(kPropertieProduct))
      .WillOnce(Return(nullptr));
  EXPECT_CALL(mock_dev(), GetSysPath())
      .WillOnce(Return(fake_dev_path_.value().c_str()));
  EXPECT_EQ(GetUsbProductName(dev_), kFakeUsbFallbackProductName);
}

}  // namespace
}  // namespace diagnostics
