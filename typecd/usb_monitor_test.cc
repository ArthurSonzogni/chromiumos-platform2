// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "typecd/usb_monitor.h"

#include <string>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/strings/stringprintf.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "typecd/usb_device.h"

namespace {
constexpr char kBusnum[] = "1";
constexpr char kDevnum[] = "1";
constexpr int kTypecPortNum = 1;

}  // namespace

namespace typecd {

class UsbMonitorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(scoped_temp_dir_.CreateUniqueTempDir());
    temp_dir_ = scoped_temp_dir_.GetPath();
  }

 public:
  base::FilePath temp_dir_;
  base::ScopedTempDir scoped_temp_dir_;
};

// Test UsbDevice with correct information is created and removed in UsbMonitor.
TEST_F(UsbMonitorTest, TestDeviceAddAndRemove) {
  // Set up fake sysfs directory.
  auto usb_sysfs_path = temp_dir_.Append("1-1");
  ASSERT_TRUE(base::CreateDirectory(usb_sysfs_path));

  auto busnum_path = usb_sysfs_path.Append("busnum");
  ASSERT_TRUE(base::WriteFile(busnum_path, kBusnum, strlen(kBusnum)));

  auto devnum_path = usb_sysfs_path.Append("devnum");
  ASSERT_TRUE(base::WriteFile(devnum_path, kDevnum, strlen(kDevnum)));

  auto connector_dir_path = usb_sysfs_path.Append("port/connector");
  base::CreateDirectory(connector_dir_path);
  auto uevent_path = connector_dir_path.Append("uevent");
  auto uevent_typec_port =
      base::StringPrintf("TYPEC_PORT=port%d", kTypecPortNum);
  ASSERT_TRUE(base::WriteFile(uevent_path, uevent_typec_port.c_str(),
                              uevent_typec_port.length()));

  auto usb_monitor = std::make_unique<UsbMonitor>();

  // New USB device created and added to the map.
  usb_monitor->OnDeviceAddedOrRemoved(usb_sysfs_path, true);
  auto usb_device = usb_monitor->GetDevice(usb_sysfs_path.BaseName().value());
  EXPECT_NE(nullptr, usb_device);
  EXPECT_EQ(std::stoi(kBusnum), usb_device->GetBusnum());
  EXPECT_EQ(std::stoi(kDevnum), usb_device->GetDevnum());
  EXPECT_EQ(kTypecPortNum, usb_device->GetTypecPortNum());

  // USB device removed from the map.
  usb_monitor->OnDeviceAddedOrRemoved(usb_sysfs_path, false);
  EXPECT_EQ(nullptr, usb_monitor->GetDevice(usb_sysfs_path.BaseName().value()));
}

// Test UsbDevice created with no Type C port when given invalid uevent path.
TEST_F(UsbMonitorTest, TestInvalidUeventPath) {
  // Set up fake sysfs directory.
  auto usb_sysfs_path = temp_dir_.Append("1-1");
  ASSERT_TRUE(base::CreateDirectory(usb_sysfs_path));

  auto busnum_path = usb_sysfs_path.Append("busnum");
  ASSERT_TRUE(base::WriteFile(busnum_path, kBusnum, strlen(kBusnum)));

  auto devnum_path = usb_sysfs_path.Append("devnum");
  ASSERT_TRUE(base::WriteFile(devnum_path, kDevnum, strlen(kDevnum)));

  auto connector_dir_path = usb_sysfs_path.Append("invalid/path");
  base::CreateDirectory(connector_dir_path);
  auto uevent_path = connector_dir_path.Append("uevent");
  auto uevent_typec_port =
      base::StringPrintf("TYPEC_PORT=port%d", kTypecPortNum);
  ASSERT_TRUE(base::WriteFile(uevent_path, uevent_typec_port.c_str(),
                              uevent_typec_port.length()));

  auto usb_monitor = std::make_unique<UsbMonitor>();

  // New USB device created and added to the map.
  usb_monitor->OnDeviceAddedOrRemoved(usb_sysfs_path, true);
  auto usb_device = usb_monitor->GetDevice(usb_sysfs_path.BaseName().value());
  EXPECT_NE(nullptr, usb_device);
  EXPECT_EQ(std::stoi(kBusnum), usb_device->GetBusnum());
  EXPECT_EQ(std::stoi(kDevnum), usb_device->GetDevnum());
  // No Type C port set.
  EXPECT_EQ(-1, usb_device->GetTypecPortNum());
}

// Test UsbDevice created with no Type C port.
TEST_F(UsbMonitorTest, TestNoTypecPort) {
  // Set up fake sysfs directory.
  auto usb_sysfs_path = temp_dir_.Append("1-1");
  ASSERT_TRUE(base::CreateDirectory(usb_sysfs_path));

  auto busnum_path = usb_sysfs_path.Append("busnum");
  ASSERT_TRUE(base::WriteFile(busnum_path, kBusnum, strlen(kBusnum)));

  auto devnum_path = usb_sysfs_path.Append("devnum");
  ASSERT_TRUE(base::WriteFile(devnum_path, kDevnum, strlen(kDevnum)));

  auto usb_monitor = std::make_unique<UsbMonitor>();

  // New USB device created and added to the map.
  usb_monitor->OnDeviceAddedOrRemoved(usb_sysfs_path, true);
  auto usb_device = usb_monitor->GetDevice(usb_sysfs_path.BaseName().value());
  EXPECT_NE(nullptr, usb_device);
  EXPECT_EQ(std::stoi(kBusnum), usb_device->GetBusnum());
  EXPECT_EQ(std::stoi(kDevnum), usb_device->GetDevnum());
  // No Type C port set.
  EXPECT_EQ(-1, usb_device->GetTypecPortNum());
}

}  // namespace typecd
