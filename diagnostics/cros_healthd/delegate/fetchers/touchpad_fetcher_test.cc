// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/strings/strcat.h>
#include <brillo/udev/mock_udev.h>
#include <brillo/udev/mock_udev_device.h>
#include <brillo/udev/mock_udev_enumerate.h>
#include <brillo/udev/mock_udev_list_entry.h>
#include <brillo/udev/udev_device.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "diagnostics/base/file_test_utils.h"
#include "diagnostics/base/file_utils.h"
#include "diagnostics/cros_healthd/delegate/fetchers/constants.h"
#include "diagnostics/cros_healthd/delegate/fetchers/touchpad_fetcher.h"

namespace diagnostics {
namespace {

constexpr char kUsbPropertyValue[] = "usb";
constexpr char kSysnamePropertyValue[] = "event6";
constexpr char kDevnamePropertyValue[] = "/dev/input/event16";
constexpr char kDevpathPropertyValue[] =
    "/devices/platform/i8042/serio1/input/input10/event9";
constexpr char kFakePsmouseProtocol[] = "FakeProtocol";
constexpr char kFakePsmouseProtocolPath[] =
    "sys/bus/serio/devices/serio1/protocol";
constexpr char kFakeMajorValue[] = "999";
constexpr char kFakeMinorValue[] = "999";
constexpr char kFakeDriverSymlink[] =
    "sys/dev/char/999:999/device/device/driver";
constexpr char kFakeDriverTarget[] = "/bus/drivers/fakedriver";
constexpr char kFakePsmouseDriverTarget[] = "/bus/serio/drivers/psmouse";

class TouchpadFetcherTest : public BaseFileTest {
 protected:
  TouchpadFetcherTest() = default;
  TouchpadFetcherTest(const TouchpadFetcherTest&) = delete;
  TouchpadFetcherTest& operator=(const TouchpadFetcherTest&) = delete;
  ~TouchpadFetcherTest() = default;

  std::string GetBasePath() {
    return base::StrCat({GetRootDir().value(), "/"});
  }

  void SetUp() override {
    dev_ = std::make_unique<brillo::MockUdevDevice>();
    udev_ = std::make_unique<brillo::MockUdev>();
    udev_enumerate_ = std::make_unique<brillo::MockUdevEnumerate>();
    udev_list_entry_ = std::make_unique<brillo::MockUdevListEntry>();
  }

  void CreateDriverSymlink(base::FilePath target) {
    SetSymbolicLink(target, base::FilePath({kFakeDriverSymlink}));
  }

  std::unique_ptr<brillo::MockUdevDevice> dev_;
  std::unique_ptr<brillo::MockUdev> udev_;
  std::unique_ptr<brillo::MockUdevListEntry> udev_list_entry_;
  std::unique_ptr<brillo::MockUdevEnumerate> udev_enumerate_;
};

TEST_F(TouchpadFetcherTest, NoUdevReturnsError) {
  auto result = PopulateTouchpadDevices(nullptr, GetBasePath());
  EXPECT_FALSE(result.has_value());
  EXPECT_GT(result.error().size(), 0);
}

TEST_F(TouchpadFetcherTest, FailedAddMatchSubsystemReturnsError) {
  EXPECT_CALL(*udev_enumerate_,
              AddMatchSubsystem(testing::StrEq(kSubsystemInput)))
      .WillOnce(testing::Return(false));

  EXPECT_CALL(*udev_, CreateEnumerate())
      .WillOnce(testing::Return(std::move(udev_enumerate_)));

  auto result = PopulateTouchpadDevices(std::move(udev_), GetBasePath());
  EXPECT_FALSE(result.has_value());
  EXPECT_GT(result.error().size(), 0);
}

TEST_F(TouchpadFetcherTest, FailedScanDevicesReturnsError) {
  EXPECT_CALL(*udev_enumerate_,
              AddMatchSubsystem(testing::StrEq(kSubsystemInput)))
      .WillOnce(testing::Return(true));

  EXPECT_CALL(*udev_enumerate_, ScanDevices()).WillOnce(testing::Return(false));

  EXPECT_CALL(*udev_, CreateEnumerate())
      .WillOnce(testing::Return(std::move(udev_enumerate_)));

  auto result = PopulateTouchpadDevices(std::move(udev_), GetBasePath());
  EXPECT_FALSE(result.has_value());
  EXPECT_GT(result.error().size(), 0);
}

TEST_F(TouchpadFetcherTest, NoDeviceInSyspathReturnsEmptyDeviceVector) {
  const char* fake_sys_path = "/path/to/device";
  EXPECT_CALL(*udev_list_entry_, GetName())
      .WillOnce(testing::Return(fake_sys_path));

  EXPECT_CALL(*udev_enumerate_, GetListEntry())
      .WillOnce(testing::Return(std::move(udev_list_entry_)));

  EXPECT_CALL(*udev_enumerate_,
              AddMatchSubsystem(testing::StrEq(kSubsystemInput)))
      .WillOnce(testing::Return(true));

  EXPECT_CALL(*udev_enumerate_, ScanDevices()).WillOnce(testing::Return(true));

  EXPECT_CALL(*udev_, CreateEnumerate())
      .WillOnce(testing::Return(std::move(udev_enumerate_)));

  EXPECT_CALL(*udev_, CreateDeviceFromSysPath(testing::StrEq(fake_sys_path)))
      .WillOnce(testing::ReturnNull());

  auto result = PopulateTouchpadDevices(std::move(udev_), GetBasePath());
  EXPECT_TRUE(result.has_value());
  EXPECT_EQ(result.value().size(), 0);
}

TEST_F(TouchpadFetcherTest, UsbDeviceReturnsEmptyDeviceVector) {
  const char* fake_sys_path = "/path/to/device";
  EXPECT_CALL(*udev_list_entry_, GetName())
      .WillOnce(testing::Return(fake_sys_path));

  EXPECT_CALL(*udev_enumerate_, GetListEntry())
      .WillOnce(testing::Return(std::move(udev_list_entry_)));

  EXPECT_CALL(*udev_enumerate_,
              AddMatchSubsystem(testing::StrEq(kSubsystemInput)))
      .WillOnce(testing::Return(true));

  EXPECT_CALL(*udev_enumerate_, ScanDevices()).WillOnce(testing::Return(true));

  EXPECT_CALL(*udev_, CreateEnumerate())
      .WillOnce(testing::Return(std::move(udev_enumerate_)));

  EXPECT_CALL(*dev_, GetPropertyValue(touchpad::kUdevPropertyIdInputTouchpad))
      .WillOnce(testing::Return("1"));
  EXPECT_CALL(*dev_, GetPropertyValue(touchpad::kUdevPropertyIdBus))
      .WillOnce(testing::Return(kUsbPropertyValue));
  EXPECT_CALL(*dev_, GetSysName())
      .WillOnce(testing::Return(kSysnamePropertyValue));

  EXPECT_CALL(*udev_, CreateDeviceFromSysPath(testing::StrEq(fake_sys_path)))
      .WillOnce(testing::Return(std::move(dev_)));

  auto result = PopulateTouchpadDevices(std::move(udev_), GetBasePath());
  EXPECT_TRUE(result.has_value());
  EXPECT_EQ(result.value().size(), 0);
}

TEST_F(TouchpadFetcherTest,
       InternalDeviceNonDeviceHandlerReturnsEmptyDeviceVector) {
  const char* fake_sys_path = "/path/to/device";
  EXPECT_CALL(*udev_list_entry_, GetName())
      .WillOnce(testing::Return(fake_sys_path));

  EXPECT_CALL(*udev_enumerate_, GetListEntry())
      .WillOnce(testing::Return(std::move(udev_list_entry_)));

  EXPECT_CALL(*udev_enumerate_,
              AddMatchSubsystem(testing::StrEq(kSubsystemInput)))
      .WillOnce(testing::Return(true));

  EXPECT_CALL(*udev_enumerate_, ScanDevices()).WillOnce(testing::Return(true));

  EXPECT_CALL(*udev_, CreateEnumerate())
      .WillOnce(testing::Return(std::move(udev_enumerate_)));

  EXPECT_CALL(*dev_, GetPropertyValue(touchpad::kUdevPropertyIdInputTouchpad))
      .WillOnce(testing::Return("1"));
  EXPECT_CALL(*dev_, GetPropertyValue(touchpad::kUdevPropertyIdBus))
      .WillOnce(testing::Return(kUsbPropertyValue));
  EXPECT_CALL(*dev_, GetSysName()).WillOnce(testing::Return("input7"));

  EXPECT_CALL(*udev_, CreateDeviceFromSysPath(testing::StrEq(fake_sys_path)))
      .WillOnce(testing::Return(std::move(dev_)));

  auto result = PopulateTouchpadDevices(std::move(udev_), GetBasePath());
  EXPECT_TRUE(result.has_value());
  EXPECT_EQ(result.value().size(), 0);
}

TEST_F(TouchpadFetcherTest, NoMajorMinorNumbersReturnsError) {
  const char* fake_sys_path = "/path/to/device";
  EXPECT_CALL(*udev_list_entry_, GetName())
      .WillOnce(testing::Return(fake_sys_path));

  EXPECT_CALL(*udev_enumerate_, GetListEntry())
      .WillOnce(testing::Return(std::move(udev_list_entry_)));

  EXPECT_CALL(*udev_enumerate_,
              AddMatchSubsystem(testing::StrEq(kSubsystemInput)))
      .WillOnce(testing::Return(true));

  EXPECT_CALL(*udev_enumerate_, ScanDevices()).WillOnce(testing::Return(true));

  EXPECT_CALL(*udev_, CreateEnumerate())
      .WillOnce(testing::Return(std::move(udev_enumerate_)));

  EXPECT_CALL(*dev_, GetPropertyValue(touchpad::kUdevPropertyIdInputTouchpad))
      .WillOnce(testing::Return("1"));
  EXPECT_CALL(*dev_, GetPropertyValue(touchpad::kUdevPropertyIdBus))
      .WillOnce(testing::Return(""));
  EXPECT_CALL(*dev_, GetSysName())
      .WillOnce(testing::Return(kSysnamePropertyValue));

  EXPECT_CALL(*dev_, GetPropertyValue(touchpad::kUdevPropertyMajor))
      .WillOnce(testing::Return(""));
  EXPECT_CALL(*dev_, GetPropertyValue(touchpad::kUdevPropertyMinor))
      .WillOnce(testing::Return(""));

  EXPECT_CALL(*udev_, CreateDeviceFromSysPath(testing::StrEq(fake_sys_path)))
      .WillOnce(testing::Return(std::move(dev_)));

  auto result = PopulateTouchpadDevices(std::move(udev_), GetBasePath());
  EXPECT_FALSE(result.has_value());
  EXPECT_GT(result.error().size(), 0);
}

TEST_F(TouchpadFetcherTest, NoDriverSymlinkReturnsError) {
  const char* fake_sys_path = "/path/to/device";
  EXPECT_CALL(*udev_list_entry_, GetName())
      .WillOnce(testing::Return(fake_sys_path));

  EXPECT_CALL(*udev_enumerate_, GetListEntry())
      .WillOnce(testing::Return(std::move(udev_list_entry_)));

  EXPECT_CALL(*udev_enumerate_,
              AddMatchSubsystem(testing::StrEq(kSubsystemInput)))
      .WillOnce(testing::Return(true));

  EXPECT_CALL(*udev_enumerate_, ScanDevices()).WillOnce(testing::Return(true));

  EXPECT_CALL(*udev_, CreateEnumerate())
      .WillOnce(testing::Return(std::move(udev_enumerate_)));

  EXPECT_CALL(*dev_, GetPropertyValue(touchpad::kUdevPropertyIdInputTouchpad))
      .WillOnce(testing::Return("1"));
  EXPECT_CALL(*dev_, GetPropertyValue(touchpad::kUdevPropertyIdBus))
      .WillOnce(testing::Return(""));
  EXPECT_CALL(*dev_, GetSysName())
      .WillOnce(testing::Return(kSysnamePropertyValue));

  EXPECT_CALL(*dev_, GetPropertyValue(touchpad::kUdevPropertyMajor))
      .WillOnce(testing::Return(kFakeMajorValue));
  EXPECT_CALL(*dev_, GetPropertyValue(touchpad::kUdevPropertyMinor))
      .WillOnce(testing::Return(kFakeMinorValue));

  EXPECT_CALL(*udev_, CreateDeviceFromSysPath(testing::StrEq(fake_sys_path)))
      .WillOnce(testing::Return(std::move(dev_)));

  auto result = PopulateTouchpadDevices(std::move(udev_), GetBasePath());
  EXPECT_FALSE(result.has_value());
  EXPECT_GT(result.error().size(), 0);
}

TEST_F(TouchpadFetcherTest, NonPsmouseDriverReturnsDevice) {
  const char* fake_sys_path = "/path/to/device";

  CreateDriverSymlink(base::FilePath{kFakeDriverTarget});

  EXPECT_CALL(*udev_list_entry_, GetName())
      .WillOnce(testing::Return(fake_sys_path));

  EXPECT_CALL(*udev_enumerate_, GetListEntry())
      .WillOnce(testing::Return(std::move(udev_list_entry_)));

  EXPECT_CALL(*udev_enumerate_,
              AddMatchSubsystem(testing::StrEq(kSubsystemInput)))
      .WillOnce(testing::Return(true));

  EXPECT_CALL(*udev_enumerate_, ScanDevices()).WillOnce(testing::Return(true));

  EXPECT_CALL(*udev_, CreateEnumerate())
      .WillOnce(testing::Return(std::move(udev_enumerate_)));

  EXPECT_CALL(*dev_, GetPropertyValue(touchpad::kUdevPropertyIdInputTouchpad))
      .WillOnce(testing::Return("1"));
  EXPECT_CALL(*dev_, GetPropertyValue(touchpad::kUdevPropertyIdBus))
      .WillOnce(testing::Return(""));
  EXPECT_CALL(*dev_, GetSysName())
      .WillOnce(testing::Return(kSysnamePropertyValue));

  EXPECT_CALL(*dev_, GetPropertyValue(touchpad::kUdevPropertyMajor))
      .WillOnce(testing::Return(kFakeMajorValue));
  EXPECT_CALL(*dev_, GetPropertyValue(touchpad::kUdevPropertyMinor))
      .WillOnce(testing::Return(kFakeMinorValue));

  EXPECT_CALL(*dev_, GetPropertyValue(touchpad::kUdevPropertyDevname))
      .WillOnce(testing::Return(kDevnamePropertyValue));

  EXPECT_CALL(*udev_, CreateDeviceFromSysPath(testing::StrEq(fake_sys_path)))
      .WillOnce(testing::Return(std::move(dev_)));

  auto result = PopulateTouchpadDevices(std::move(udev_), GetBasePath());

  EXPECT_TRUE(result.has_value());
  EXPECT_EQ(result.value().size(), 1);
  auto touchpad_device = std::move(result.value()[0]);
  EXPECT_EQ(touchpad_device->driver_name, "fakedriver");
  auto input_device = std::move(touchpad_device->input_device);
  EXPECT_EQ(input_device->name, kDevnamePropertyValue);
  EXPECT_EQ(input_device->physical_location, fake_sys_path);
  ASSERT_TRUE(input_device->is_enabled);
}

TEST_F(TouchpadFetcherTest, PsmouseDriverNoProtocolReturnsDevice) {
  const char* fake_sys_path = "/path/to/device";

  CreateDriverSymlink(base::FilePath{kFakePsmouseDriverTarget});

  EXPECT_CALL(*udev_list_entry_, GetName())
      .WillOnce(testing::Return(fake_sys_path));

  EXPECT_CALL(*udev_enumerate_, GetListEntry())
      .WillOnce(testing::Return(std::move(udev_list_entry_)));

  EXPECT_CALL(*udev_enumerate_,
              AddMatchSubsystem(testing::StrEq(kSubsystemInput)))
      .WillOnce(testing::Return(true));

  EXPECT_CALL(*udev_enumerate_, ScanDevices()).WillOnce(testing::Return(true));

  EXPECT_CALL(*udev_, CreateEnumerate())
      .WillOnce(testing::Return(std::move(udev_enumerate_)));

  EXPECT_CALL(*dev_, GetPropertyValue(touchpad::kUdevPropertyIdInputTouchpad))
      .WillOnce(testing::Return("1"));
  EXPECT_CALL(*dev_, GetPropertyValue(touchpad::kUdevPropertyIdBus))
      .WillOnce(testing::Return(""));
  EXPECT_CALL(*dev_, GetSysName())
      .WillOnce(testing::Return(kSysnamePropertyValue));

  EXPECT_CALL(*dev_, GetPropertyValue(touchpad::kUdevPropertyMajor))
      .WillOnce(testing::Return(kFakeMajorValue));
  EXPECT_CALL(*dev_, GetPropertyValue(touchpad::kUdevPropertyMinor))
      .WillOnce(testing::Return(kFakeMinorValue));

  EXPECT_CALL(*dev_, GetPropertyValue(touchpad::kUdevPropertyDevname))
      .WillOnce(testing::Return(kDevnamePropertyValue));

  EXPECT_CALL(*dev_, GetPropertyValue(touchpad::kUdevPropertyDevpath))
      .WillOnce(testing::Return(kDevpathPropertyValue));

  EXPECT_CALL(*udev_, CreateDeviceFromSysPath(testing::StrEq(fake_sys_path)))
      .WillOnce(testing::Return(std::move(dev_)));

  auto result = PopulateTouchpadDevices(std::move(udev_), GetBasePath());

  EXPECT_TRUE(result.has_value());
  EXPECT_EQ(result.value().size(), 1);
  auto touchpad_device = std::move(result.value()[0]);
  EXPECT_EQ(touchpad_device->driver_name, "psmouse");
  auto input_device = std::move(touchpad_device->input_device);
  EXPECT_EQ(input_device->name, kDevnamePropertyValue);
  EXPECT_EQ(input_device->physical_location, fake_sys_path);
  ASSERT_TRUE(input_device->is_enabled);
}

TEST_F(TouchpadFetcherTest, PsmouseDriverWithProtocolReturnsDevice) {
  const char* fake_sys_path = "/path/to/device";
  SetFile(base::FilePath{kFakePsmouseProtocolPath}, kFakePsmouseProtocol);

  CreateDriverSymlink(base::FilePath{kFakePsmouseDriverTarget});

  EXPECT_CALL(*udev_list_entry_, GetName())
      .WillOnce(testing::Return(fake_sys_path));

  EXPECT_CALL(*udev_enumerate_, GetListEntry())
      .WillOnce(testing::Return(std::move(udev_list_entry_)));

  EXPECT_CALL(*udev_enumerate_,
              AddMatchSubsystem(testing::StrEq(kSubsystemInput)))
      .WillOnce(testing::Return(true));

  EXPECT_CALL(*udev_enumerate_, ScanDevices()).WillOnce(testing::Return(true));

  EXPECT_CALL(*udev_, CreateEnumerate())
      .WillOnce(testing::Return(std::move(udev_enumerate_)));

  EXPECT_CALL(*dev_, GetPropertyValue(touchpad::kUdevPropertyIdInputTouchpad))
      .WillOnce(testing::Return("1"));
  EXPECT_CALL(*dev_, GetPropertyValue(touchpad::kUdevPropertyIdBus))
      .WillOnce(testing::Return(""));
  EXPECT_CALL(*dev_, GetSysName())
      .WillOnce(testing::Return(kSysnamePropertyValue));

  EXPECT_CALL(*dev_, GetPropertyValue(touchpad::kUdevPropertyMajor))
      .WillOnce(testing::Return(kFakeMajorValue));
  EXPECT_CALL(*dev_, GetPropertyValue(touchpad::kUdevPropertyMinor))
      .WillOnce(testing::Return(kFakeMinorValue));

  EXPECT_CALL(*dev_, GetPropertyValue(touchpad::kUdevPropertyDevname))
      .WillOnce(testing::Return(kDevnamePropertyValue));

  EXPECT_CALL(*dev_, GetPropertyValue(touchpad::kUdevPropertyDevpath))
      .WillOnce(testing::Return(kDevpathPropertyValue));

  EXPECT_CALL(*udev_, CreateDeviceFromSysPath(testing::StrEq(fake_sys_path)))
      .WillOnce(testing::Return(std::move(dev_)));

  auto result = PopulateTouchpadDevices(std::move(udev_), GetBasePath());

  EXPECT_TRUE(result.has_value());
  EXPECT_EQ(result.value().size(), 1);
  auto touchpad_device = std::move(result.value()[0]);
  EXPECT_EQ(touchpad_device->driver_name, "FakeProtocol psmouse");
  auto input_device = std::move(touchpad_device->input_device);
  EXPECT_EQ(input_device->name, kDevnamePropertyValue);
  EXPECT_EQ(input_device->physical_location, fake_sys_path);
  ASSERT_TRUE(input_device->is_enabled);
}

TEST_F(TouchpadFetcherTest, EmptyEntryNameReturnsEmptyDeviceVector) {
  const char* get_name_return = "";
  EXPECT_CALL(*udev_list_entry_, GetName())
      .WillOnce(testing::Return(get_name_return));

  EXPECT_CALL(*udev_enumerate_, GetListEntry())
      .WillOnce(testing::Return(std::move(udev_list_entry_)));

  EXPECT_CALL(*udev_enumerate_,
              AddMatchSubsystem(testing::StrEq(kSubsystemInput)))
      .WillOnce(testing::Return(true));

  EXPECT_CALL(*udev_enumerate_, ScanDevices()).WillOnce(testing::Return(true));

  EXPECT_CALL(*udev_, CreateEnumerate())
      .WillOnce(testing::Return(std::move(udev_enumerate_)));

  auto result = PopulateTouchpadDevices(std::move(udev_), GetBasePath());
  EXPECT_TRUE(result.has_value());
  EXPECT_EQ(result.value().size(), 0);
}
}  // namespace
}  // namespace diagnostics
