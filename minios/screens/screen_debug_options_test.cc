// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "minios/screens/screen_debug_options.h"

#include <linux/input-event-codes.h>
#include <sys/mount.h>

#include <memory>
#include <optional>
#include <utility>

#include <base/command_line.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <brillo/namespaces/mock_platform.h>
#include <brillo/namespaces/platform.h>
#include <brillo/udev/mock_udev.h>
#include <brillo/udev/mock_udev_device.h>
#include <brillo/udev/mock_udev_enumerate.h>
#include <brillo/udev/mock_udev_list_entry.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "minios/mock_blkid_wrapper.h"
#include "minios/mock_draw_interface.h"
#include "minios/mock_log_store_manager.h"
#include "minios/mock_process_manager.h"
#include "minios/mock_screen_controller.h"

using ::testing::_;
using ::testing::Contains;
using ::testing::NiceMock;
using ::testing::Optional;
using ::testing::Return;
using ::testing::StrEq;
using ::testing::StrictMock;

namespace minios {

class ScreenDebugOptionsTest : public ::testing::Test {
 protected:
  std::shared_ptr<MockDrawInterface> mock_draw_interface_ =
      std::make_shared<NiceMock<MockDrawInterface>>();
  MockDrawInterface* mock_draw_interface_ptr_ = mock_draw_interface_.get();
  StrictMock<MockScreenControllerInterface> mock_screen_controller_;
  std::shared_ptr<MockLogStoreManager> mock_log_store_manager =
      std::make_shared<StrictMock<MockLogStoreManager>>();
  std::shared_ptr<MockProcessManager> process_manager =
      std::make_shared<MockProcessManager>();

  std::shared_ptr<MockBlkIdWrapper> mock_blkid_wrapper_ =
      std::make_shared<NiceMock<MockBlkIdWrapper>>();

  std::shared_ptr<brillo::MockPlatform> mock_platform_ =
      std::make_shared<StrictMock<brillo::MockPlatform>>();

  ScreenDebugOptions screen_debug_options_{
      mock_draw_interface_,     mock_log_store_manager, process_manager,
      &mock_screen_controller_, mock_blkid_wrapper_,    mock_platform_};
};

TEST_F(ScreenDebugOptionsTest, GetState) {
  EXPECT_CALL(mock_screen_controller_, OnStateChanged);
  screen_debug_options_.Show();
  EXPECT_EQ(State::DEBUG_OPTIONS, screen_debug_options_.GetState().state());
}

TEST_F(ScreenDebugOptionsTest, ClearLogs) {
  EXPECT_CALL(*process_manager, RunCommand(_, _)).WillOnce(testing::Return(0));
  EXPECT_CALL(*mock_log_store_manager, ClearLogs())
      .WillOnce(testing::Return(true));
  screen_debug_options_.index_ =
      static_cast<int>(ScreenDebugOptions::ButtonIndex::kEraseLogs);
  screen_debug_options_.OnKeyPress(KEY_ENTER);
}

TEST_F(ScreenDebugOptionsTest, MoveForward) {
  EXPECT_CALL(mock_screen_controller_, OnForward(&screen_debug_options_));
  EXPECT_TRUE(screen_debug_options_.MoveForward(nullptr));
}

TEST_F(ScreenDebugOptionsTest, MoveBackward) {
  EXPECT_CALL(mock_screen_controller_, OnBackward(&screen_debug_options_));
  EXPECT_TRUE(screen_debug_options_.MoveBackward(nullptr));
}

TEST_F(ScreenDebugOptionsTest, MountFileSystemFailure) {
  base::ScopedTempDir temp_dir, device_dir;
  ASSERT_TRUE(device_dir.CreateUniqueTempDir());
  EXPECT_CALL(*mock_blkid_wrapper_, CheckAndGetTagValue(_, _))
      .WillOnce(Return(std::nullopt));

  EXPECT_FALSE(screen_debug_options_.MountRemovableDevice(
      temp_dir, device_dir.GetPath()));
}

TEST_F(ScreenDebugOptionsTest, MountRemovableDeviceTest) {
  base::ScopedTempDir temp_dir, device_dir;
  const std::string filesystem{"my_filesystem"};
  ASSERT_TRUE(device_dir.CreateUniqueTempDir());
  EXPECT_CALL(*mock_blkid_wrapper_, CheckAndGetTagValue(_, _))
      .WillOnce(Return(filesystem));
  EXPECT_CALL(*mock_platform_,
              Mount(StrEq(device_dir.GetPath().value()), _, StrEq(filesystem),
                    MS_NOEXEC | MS_NOSUID, nullptr))
      .WillOnce(Return(0));

  EXPECT_FALSE(screen_debug_options_.MountRemovableDevice(
      temp_dir, base::FilePath{"/some/path"}));
  EXPECT_TRUE(screen_debug_options_.MountRemovableDevice(temp_dir,
                                                         device_dir.GetPath()));
}

TEST_F(ScreenDebugOptionsTest, SaveLogsToDeviceTest) {
  base::ScopedTempDir temp_dir;
  base::FilePath removable_device;
  const std::string filesystem{"ntfs"};
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  ASSERT_TRUE(
      base::CreateTemporaryFileInDir(temp_dir.GetPath(), &removable_device));

  EXPECT_CALL(*mock_platform_,
              Mount(StrEq(removable_device.value()), _, StrEq(filesystem),
                    MS_NOEXEC | MS_NOSUID, nullptr))
      .WillOnce(Return(0));
  EXPECT_CALL(*mock_blkid_wrapper_, CheckAndGetTagValue(_, _))
      .WillOnce(Return(filesystem));
  EXPECT_CALL(
      *mock_log_store_manager,
      SaveLogs(LogStoreManagerInterface::LogDirection::RemovableDevice, _))
      .WillOnce(testing::Return(true));

  EXPECT_CALL(*mock_platform_, Unmount(_, true, nullptr))
      .WillOnce(Return(true));

  EXPECT_TRUE(screen_debug_options_.SaveLogsToDevice(removable_device));
}

TEST_F(ScreenDebugOptionsTest, UpdateStorageDevicesWithDisk) {
  auto mock_udev = std::make_unique<NiceMock<brillo::MockUdev>>();
  auto mock_udev_enumerate =
      std::make_unique<StrictMock<brillo::MockUdevEnumerate>>();

  // Setup match properties.
  EXPECT_CALL(*mock_udev_enumerate, AddMatchSubsystem(StrEq("block")))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_udev_enumerate,
              AddMatchProperty(StrEq("ID_FS_USAGE"), StrEq("filesystem")))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_udev_enumerate, ScanDevices()).WillOnce(Return(true));
  // Return nullptr, no devices found.
  EXPECT_CALL(*mock_udev_enumerate, GetListEntry()).WillOnce(Return(nullptr));
  EXPECT_CALL(*mock_udev, CreateEnumerate())
      .WillOnce(Return(std::move(mock_udev_enumerate)));

  screen_debug_options_.UpdateStorageDevices(std::move(mock_udev));

  EXPECT_EQ(screen_debug_options_.storage_devices_.size(), 1);
  EXPECT_EQ(screen_debug_options_.button_count_, 2);
}

TEST_F(ScreenDebugOptionsTest, DrawSaveResultSuccess) {
  screen_debug_options_.storage_devices_.emplace_back(kDiskStorageDevice,
                                                      std::nullopt);
  screen_debug_options_.index_ = 0;
  EXPECT_CALL(*mock_log_store_manager,
              SaveLogs(LogStoreManagerInterface::LogDirection::Disk, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_draw_interface_,
              ShowText(StrEq("Logs successfully saved to Disk"), _, _, _));
  EXPECT_CALL(mock_screen_controller_, OnStateChanged(_));

  screen_debug_options_.HandleDeviceSelection();
}

TEST_F(ScreenDebugOptionsTest, DrawSaveResultFailure) {
  screen_debug_options_.storage_devices_.emplace_back(kDiskStorageDevice,
                                                      std::nullopt);
  screen_debug_options_.index_ = 0;
  EXPECT_CALL(*mock_log_store_manager,
              SaveLogs(LogStoreManagerInterface::LogDirection::Disk, _))
      .WillOnce(Return(false));
  EXPECT_CALL(*mock_draw_interface_,
              ShowText(StrEq("Failed to save logs to Disk"), _, _, _));
  EXPECT_CALL(mock_screen_controller_, OnStateChanged(_));
  screen_debug_options_.HandleDeviceSelection();
}

}  // namespace minios
