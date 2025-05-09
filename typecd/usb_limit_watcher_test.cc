// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "typecd/usb_limit_watcher.h"

#include <memory>

#include <base/files/scoped_temp_dir.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "typecd/mock_dbus_manager.h"
#include "typecd/utils.h"

using ::testing::_;
using ::testing::DoAll;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::StrictMock;

namespace {
constexpr char kValidUsbDevice1[] = "2-3";
constexpr char kValidUsbDevice2[] = "3-1.2.4.1.4";
constexpr char kInvalidUsbDevice1[] = "3-a";
constexpr char kInvalidUsbDevice2[] = "31.2";
}  // namespace

namespace typecd {

class UsbLimitWatcherTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Create a DBusObject.
    dbus_object_ = std::make_unique<brillo::dbus_utils::DBusObject>(
        nullptr, nullptr, dbus::ObjectPath(kTypecdServicePath));

    // Create UsbLimitWatcher and MockDBusManager instances.
    dbus_manager_ =
        std::make_unique<StrictMock<MockDBusManager>>(dbus_object_.get());
    usb_limit_watcher_ = std::make_unique<UsbLimitWatcher>();
    usb_limit_watcher_->SetDBusManager(dbus_manager_.get());
  }

 public:
  std::unique_ptr<brillo::dbus_utils::DBusObject> dbus_object_;
  std::unique_ptr<MockDBusManager> dbus_manager_;
  std::unique_ptr<UsbLimitWatcher> usb_limit_watcher_;
};

// Test to check that NotifyUsbLimit is called when the USB device count
// exceeds the device limit on MT8196 platforms.
TEST_F(UsbLimitWatcherTest, UsbDeviceLimitReached) {
  if (GetUsbDeviceCount(base::FilePath(kUsbDeviceDir), kMTk8196UsbDeviceRe) >=
      kMTk8196DeviceLimit) {
    EXPECT_CALL(*dbus_manager_, NotifyUsbLimit(UsbLimitType::kDeviceLimit))
        .Times(1);
  } else {
    EXPECT_CALL(*dbus_manager_, NotifyUsbLimit(UsbLimitType::kDeviceLimit))
        .Times(0);
  }

  usb_limit_watcher_->OnUsbDeviceAdded();
}

// This isn't strictly a test on the UsbLimitWatcher class, but the class
// heavily relies on GetUsbDeviceCount(). This tests that helper function
// correctly counts USB devices.
TEST_F(UsbLimitWatcherTest, GetUsbDeviceCount) {
  base::FilePath temp_dir_;
  base::ScopedTempDir scoped_temp_dir_;

  ASSERT_TRUE(scoped_temp_dir_.CreateUniqueTempDir());
  temp_dir_ = scoped_temp_dir_.GetPath();

  ASSERT_EQ(0, GetUsbDeviceCount(temp_dir_, kMTk8196UsbDeviceRe));

  // USB device count does not increase with invalid device name.
  auto usb_device = temp_dir_.Append(kInvalidUsbDevice1);
  base::WriteFile(usb_device, "");
  ASSERT_EQ(0, GetUsbDeviceCount(temp_dir_, kMTk8196UsbDeviceRe));

  // USB device count does not increase with invalid device name.
  usb_device = temp_dir_.Append(kInvalidUsbDevice2);
  base::WriteFile(usb_device, "");
  ASSERT_EQ(0, GetUsbDeviceCount(temp_dir_, kMTk8196UsbDeviceRe));

  // USB device count increments after adding valid USB device.
  usb_device = temp_dir_.Append(kValidUsbDevice1);
  base::WriteFile(usb_device, "");
  ASSERT_EQ(1, GetUsbDeviceCount(temp_dir_, kMTk8196UsbDeviceRe));

  // USB device count increments after adding valid USB device.
  usb_device = temp_dir_.Append(kValidUsbDevice2);
  base::WriteFile(usb_device, "");
  ASSERT_EQ(2, GetUsbDeviceCount(temp_dir_, kMTk8196UsbDeviceRe));
}

}  // namespace typecd
