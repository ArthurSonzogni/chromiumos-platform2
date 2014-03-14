// Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <vector>

#include <base/compiler_specific.h>
#include <base/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <gtest/gtest.h>

#include "power_manager/powerd/system/display/display_watcher.h"
#include "power_manager/powerd/system/udev_observer.h"
#include "power_manager/powerd/system/udev_stub.h"

namespace power_manager {
namespace system {

namespace {

// Stub implementation of DisplayWatcherObserver.
class TestObserver : public DisplayWatcherObserver {
 public:
  TestObserver() : num_display_changes_(0) {}
  virtual ~TestObserver() {}

  int num_display_changes() const { return num_display_changes_; }

  // DisplayWatcherObserver implementation:
  virtual void OnDisplaysChanged(const std::vector<DisplayInfo>& displays)
      OVERRIDE {
    num_display_changes_++;
  }

 private:
  // Number of times that OnDisplaysChanged() has been called.
  int num_display_changes_;

  DISALLOW_COPY_AND_ASSIGN(TestObserver);
};

}  // namespace

class DisplayWatcherTest : public testing::Test {
 public:
  DisplayWatcherTest() {
    CHECK(drm_dir_.CreateUniqueTempDir());
    watcher_.set_sysfs_drm_path_for_testing(drm_dir_.path());
    CHECK(device_dir_.CreateUniqueTempDir());
    watcher_.set_i2c_dev_path_for_testing(device_dir_.path());
  }
  virtual ~DisplayWatcherTest() {}

 protected:
  // Creates a directory named |device_name| in |device_dir_| and adds a symlink
  // to it in |drm_dir_|. Returns the path to the directory.
  base::FilePath CreateDrmDevice(const std::string& device_name) {
    base::FilePath device_path = device_dir_.path().Append(device_name);
    CHECK(base::CreateDirectory(device_path));
    CHECK(base::CreateSymbolicLink(
              device_path, drm_dir_.path().Append(device_name)));
    return device_path;
  }

  // Creates a file named |device_name| in |device_dir_|. Returns the path to
  // the file.
  base::FilePath CreateI2CDevice(const std::string& device_name) {
    base::FilePath device_path = device_dir_.path().Append(device_name);
    CHECK_EQ(file_util::WriteFile(device_path, "\n", 1), 1);
    return device_path;
  }

  // Notifies |watcher_| about a Udev event to trigger a rescan of displays.
  void NotifyAboutUdevEvent() {
    watcher_.OnUdevEvent("system", DisplayWatcher::kDrmUdevSubsystem,
                         UdevObserver::ACTION_CHANGE);
  }

  // Directory with symlinks to DRM devices.
  base::ScopedTempDir drm_dir_;

  // Directory holding device information symlinked to from the above
  // directories.
  base::ScopedTempDir device_dir_;

  UdevStub udev_;
  DisplayWatcher watcher_;
};

TEST_F(DisplayWatcherTest, DisplayStatus) {
  watcher_.Init(&udev_);
  EXPECT_EQ(static_cast<size_t>(0), watcher_.GetDisplays().size());

  // Disconnected if there's no status file.
  base::FilePath device_path = CreateDrmDevice("card0-DP-1");
  NotifyAboutUdevEvent();
  EXPECT_EQ(static_cast<size_t>(0), watcher_.GetDisplays().size());

  // Disconnected if the status file doesn't report the connected state.
  const char kDisconnected[] = "disconnected";
  base::FilePath status_path =
      device_path.Append(DisplayWatcher::kDrmStatusFile);
  ASSERT_TRUE(file_util::WriteFile(status_path, kDisconnected,
      strlen(kDisconnected)));
  NotifyAboutUdevEvent();
  EXPECT_EQ(static_cast<size_t>(0), watcher_.GetDisplays().size());

  // The display should be reported when the device's status goes to
  // "connected".
  ASSERT_TRUE(file_util::WriteFile(status_path,
      DisplayWatcher::kDrmStatusConnected,
      strlen(DisplayWatcher::kDrmStatusConnected)));
  NotifyAboutUdevEvent();
  ASSERT_EQ(static_cast<size_t>(1), watcher_.GetDisplays().size());

  // A trailing newline should be okay.
  std::string kConnectedNewline(DisplayWatcher::kDrmStatusConnected);
  kConnectedNewline += "\n";
  ASSERT_TRUE(file_util::WriteFile(status_path, kConnectedNewline.c_str(),
      kConnectedNewline.size()));
  NotifyAboutUdevEvent();
  ASSERT_EQ(static_cast<size_t>(1), watcher_.GetDisplays().size());
  EXPECT_EQ(drm_dir_.path().Append(device_path.BaseName()).value(),
            watcher_.GetDisplays()[0].drm_path.value());

  // Add a second disconnected device.
  base::FilePath second_device_path = CreateDrmDevice("card0-DP-0");
  base::FilePath second_status_path =
      second_device_path.Append(DisplayWatcher::kDrmStatusFile);
  ASSERT_TRUE(file_util::WriteFile(second_status_path, kDisconnected,
      strlen(kDisconnected)));
  NotifyAboutUdevEvent();
  ASSERT_EQ(static_cast<size_t>(1), watcher_.GetDisplays().size());
  EXPECT_EQ(drm_dir_.path().Append(device_path.BaseName()).value(),
            watcher_.GetDisplays()[0].drm_path.value());

  // Connect the second device. It should be reported first since devices are
  // sorted alphabetically.
  ASSERT_TRUE(file_util::WriteFile(second_status_path,
      DisplayWatcher::kDrmStatusConnected,
      strlen(DisplayWatcher::kDrmStatusConnected)));
  NotifyAboutUdevEvent();
  ASSERT_EQ(static_cast<size_t>(2), watcher_.GetDisplays().size());
  EXPECT_EQ(drm_dir_.path().Append(second_device_path.BaseName()).value(),
            watcher_.GetDisplays()[0].drm_path.value());
  EXPECT_EQ(drm_dir_.path().Append(device_path.BaseName()).value(),
            watcher_.GetDisplays()[1].drm_path.value());

  // Disconnect both devices and create a new device that has a
  // "connected" status but doesn't match the expected naming pattern for a
  // video card.
  ASSERT_TRUE(file_util::WriteFile(status_path, kDisconnected,
      strlen(kDisconnected)));
  ASSERT_TRUE(file_util::WriteFile(second_status_path, kDisconnected,
      strlen(kDisconnected)));
  base::FilePath misnamed_device_path = CreateDrmDevice("control32");
  base::FilePath misnamed_status_path =
      misnamed_device_path.Append(DisplayWatcher::kDrmStatusFile);
  ASSERT_TRUE(file_util::WriteFile(misnamed_status_path,
      kConnectedNewline.c_str(), kConnectedNewline.size()));
  NotifyAboutUdevEvent();
  EXPECT_EQ(static_cast<size_t>(0), watcher_.GetDisplays().size());
}

TEST_F(DisplayWatcherTest, I2CDevices) {
  // Create a single connected device with no I2C device.
  base::FilePath device_path = CreateDrmDevice("card0-DP-1");
  base::FilePath status_path =
      device_path.Append(DisplayWatcher::kDrmStatusFile);
  ASSERT_TRUE(file_util::WriteFile(status_path,
      DisplayWatcher::kDrmStatusConnected,
      strlen(DisplayWatcher::kDrmStatusConnected)));

  watcher_.Init(&udev_);
  ASSERT_EQ(static_cast<size_t>(1), watcher_.GetDisplays().size());
  EXPECT_EQ("", watcher_.GetDisplays()[0].i2c_path.value());

  // Create an I2C directory within the DRM directory and check that the I2C
  // device's path is set.
  const char kI2CName[] = "i2c-3";
  base::FilePath i2c_path = CreateI2CDevice(kI2CName);
  base::FilePath drm_i2c_path = device_path.Append(kI2CName);
  ASSERT_TRUE(base::CreateDirectory(drm_i2c_path));
  NotifyAboutUdevEvent();
  ASSERT_EQ(static_cast<size_t>(1), watcher_.GetDisplays().size());
  EXPECT_EQ(i2c_path.value(), watcher_.GetDisplays()[0].i2c_path.value());

  // If the I2C device doesn't actually exist, the path shouldn't be set.
  ASSERT_TRUE(base::DeleteFile(i2c_path, false));
  NotifyAboutUdevEvent();
  ASSERT_EQ(static_cast<size_t>(1), watcher_.GetDisplays().size());
  EXPECT_EQ("", watcher_.GetDisplays()[0].i2c_path.value());

  // Create a device with a bogus name and check that it doesn't get returned.
  const char kBogusName[] = "i3c-1";
  base::FilePath bogus_path = CreateI2CDevice(kBogusName);
  ASSERT_TRUE(base::CreateDirectory(device_path.Append(kBogusName)));
  ASSERT_TRUE(base::DeleteFile(drm_i2c_path, false));
  NotifyAboutUdevEvent();
  ASSERT_EQ(static_cast<size_t>(1), watcher_.GetDisplays().size());
  EXPECT_EQ("", watcher_.GetDisplays()[0].i2c_path.value());
}

TEST_F(DisplayWatcherTest, Observer) {
  // The observer shouldn't be notified when Init() is called without any
  // displays present.
  TestObserver observer;
  watcher_.AddObserver(&observer);
  watcher_.Init(&udev_);
  EXPECT_EQ(0, observer.num_display_changes());

  // It also shouldn't be notified in response to a Udev event if nothing
  // changed.
  NotifyAboutUdevEvent();
  EXPECT_EQ(0, observer.num_display_changes());

  // After adding a display, the observer should be notified.
  base::FilePath device_path = CreateDrmDevice("card0-DP-1");
  base::FilePath status_path =
      device_path.Append(DisplayWatcher::kDrmStatusFile);
  ASSERT_TRUE(file_util::WriteFile(status_path,
      DisplayWatcher::kDrmStatusConnected,
      strlen(DisplayWatcher::kDrmStatusConnected)));
  NotifyAboutUdevEvent();
  EXPECT_EQ(1, observer.num_display_changes());

  // It shouldn't be notified for another no-op Udev event.
  NotifyAboutUdevEvent();
  EXPECT_EQ(1, observer.num_display_changes());

  // After the device is disconnected, the observer should be notified one more
  // time.
  ASSERT_TRUE(base::DeleteFile(status_path, false));
  NotifyAboutUdevEvent();
  EXPECT_EQ(2, observer.num_display_changes());

  watcher_.RemoveObserver(&observer);
}

}  // namespace system
}  // namespace power_manager
