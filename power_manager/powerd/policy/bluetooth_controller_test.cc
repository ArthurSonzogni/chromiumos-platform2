// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/policy/bluetooth_controller.h"

#include <memory>
#include <string>

#include <base/check_op.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/strings/string_piece.h>
#include <gtest/gtest.h>

#include "power_manager/powerd/system/udev_stub.h"

namespace power_manager::policy {

class BluetoothControllerTest : public ::testing::Test {
 public:
  BluetoothControllerTest() = default;
  BluetoothControllerTest(const BluetoothControllerTest&) = delete;
  BluetoothControllerTest& operator=(const BluetoothControllerTest&) = delete;

 protected:
  static constexpr char kBtDeepDir[] = "usb/1-6/1-6:1.0/bluetooth/hci0";
  static constexpr char kBtWakeDir[] = "usb/1-6/";
  static constexpr char kPowerDir[] = "power";
  static constexpr char kValidDirPrefix[] = "valid";
  static constexpr char kInvalidDirPrefix[] = "invalid";
  static constexpr char kErrorContents[] = "file-read-error";

  void Init(bool with_existing_valid_device = false) {
    PrepareTestFiles();
    controller_ = std::make_unique<BluetoothController>();
    if (with_existing_valid_device) {
      system::UdevDeviceInfo info = ConstructDeviceInfo(true);
      udev_.AddSubsystemDevice(BluetoothController::kUdevSubsystem, info,
                               {"/dev/foobar"});
    }
    controller_->Init(&udev_);
  }

  void PrepareTestFiles() {
    base::FilePath unused;
    ASSERT_TRUE(base::CreateNewTempDirectory(unused.value(), &file_prefix_));

    base::FilePath valid_deep_dir =
        file_prefix_.Append(kValidDirPrefix).Append(kBtDeepDir);
    base::FilePath valid_power_dir = file_prefix_.Append(kValidDirPrefix)
                                         .Append(kBtWakeDir)
                                         .Append(kPowerDir);
    base::FilePath valid_control_file =
        file_prefix_.Append(kValidDirPrefix)
            .Append(kBtWakeDir)
            .Append(BluetoothController::kAutosuspendSysattr);
    base::FilePath invalid_deep_dir =
        file_prefix_.Append(kInvalidDirPrefix).Append(kBtDeepDir);

    base::StringPiece autosuspend_enabled(
        BluetoothController::kAutosuspendEnabled);

    // Add all directories including the "power/control" file in the valid path.
    ASSERT_TRUE(base::CreateDirectory(valid_deep_dir));
    ASSERT_TRUE(base::CreateDirectory(valid_power_dir));
    ASSERT_TRUE(base::WriteFile(valid_control_file, autosuspend_enabled));
    ASSERT_TRUE(base::CreateDirectory(invalid_deep_dir));
  }

  system::UdevDeviceInfo ConstructDeviceInfo(bool valid) {
    base::FilePath syspath =
        file_prefix_.Append(valid ? kValidDirPrefix : kInvalidDirPrefix)
            .Append(kBtDeepDir);
    base::FilePath wake_path =
        file_prefix_.Append(valid ? kValidDirPrefix : kInvalidDirPrefix)
            .Append(kBtWakeDir);

    system::UdevDeviceInfo info = {
        BluetoothController::kUdevSubsystem, BluetoothController::kUdevDevtype,
        "", std::string(syspath.value().data(), syspath.value().size()),
        wake_path};

    return info;
  }

  void SendUdevEvent(system::UdevEvent::Action action, bool valid) {
    system::UdevDeviceInfo device_info = ConstructDeviceInfo(valid);
    udev_.NotifySubsystemObservers({device_info, action});
  }

  std::string GetControlPathContents(bool valid) {
    std::string out;
    base::FilePath filepath =
        file_prefix_.Append(valid ? kValidDirPrefix : kInvalidDirPrefix)
            .Append(kBtWakeDir)
            .Append(BluetoothController::kAutosuspendSysattr);

    if (!base::ReadFileToString(filepath, &out)) {
      out = kErrorContents;
    }

    return out;
  }

  base::FilePath file_prefix_;
  system::UdevStub udev_;
  std::unique_ptr<BluetoothController> controller_;
};

TEST_F(BluetoothControllerTest, AutosuspendQuirkApplied) {
  Init();

  // Valid path should start with autosuspend enabled
  SendUdevEvent(system::UdevEvent::Action::ADD, true);
  EXPECT_EQ(GetControlPathContents(true),
            BluetoothController::kAutosuspendEnabled);

  // Disable when applying quirk and enable when unapplying quirk.
  controller_->ApplyAutosuspendQuirk();
  EXPECT_EQ(GetControlPathContents(true),
            BluetoothController::kAutosuspendDisabled);
  controller_->UnapplyAutosuspendQuirk();
  EXPECT_EQ(GetControlPathContents(true),
            BluetoothController::kAutosuspendEnabled);
}

TEST_F(BluetoothControllerTest, RemoveEventHandled) {
  Init();

  SendUdevEvent(system::UdevEvent::Action::ADD, true);
  SendUdevEvent(system::UdevEvent::Action::REMOVE, true);
  controller_->ApplyAutosuspendQuirk();
  EXPECT_EQ(GetControlPathContents(true),
            BluetoothController::kAutosuspendEnabled);
}

TEST_F(BluetoothControllerTest, IgnoreNoControlFile) {
  Init();

  SendUdevEvent(system::UdevEvent::Action::ADD, false);
  EXPECT_EQ(GetControlPathContents(false), kErrorContents);

  controller_->ApplyAutosuspendQuirk();
  EXPECT_EQ(GetControlPathContents(false), kErrorContents);
}

TEST_F(BluetoothControllerTest, UseDeviceFromInit) {
  Init(true);

  EXPECT_EQ(GetControlPathContents(true),
            BluetoothController::kAutosuspendEnabled);

  // Disable when applying quirk and enable when unapplying quirk.
  controller_->ApplyAutosuspendQuirk();
  EXPECT_EQ(GetControlPathContents(true),
            BluetoothController::kAutosuspendDisabled);
  controller_->UnapplyAutosuspendQuirk();
  EXPECT_EQ(GetControlPathContents(true),
            BluetoothController::kAutosuspendEnabled);
}

}  // namespace power_manager::policy
