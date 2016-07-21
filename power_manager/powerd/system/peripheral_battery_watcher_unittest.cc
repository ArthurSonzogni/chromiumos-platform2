// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/system/peripheral_battery_watcher.h"

#include <string>

#include <base/compiler_specific.h>
#include <base/files/file_util.h>
#include <base/files/file_path.h>
#include <base/files/scoped_temp_dir.h>
#include <base/strings/string_number_conversions.h>
#include <chromeos/dbus/service_constants.h>
#include <gtest/gtest.h>

#include "power_manager/common/test_main_loop_runner.h"
#include "power_manager/powerd/system/dbus_wrapper_stub.h"
#include "power_manager/proto_bindings/peripheral_battery_status.pb.h"

namespace power_manager {
namespace system {

using std::string;

namespace {

// Abort if it an expected battery update hasn't been received after this
// many milliseconds.
const int kUpdateTimeoutMs = 30 * 1000;

// Shorter update timeout to use when failure is expected.
const int kShortUpdateTimeoutMs = 1000;

const char kDeviceModelName[] = "Test HID Mouse";

class TestWrapper : public DBusWrapperStub {
 public:
  TestWrapper() {}
  virtual ~TestWrapper() {}

  // Runs |loop_| until battery status is sent through D-Bus.
  bool RunUntilSignalSent(int timeout_ms) {
    return loop_runner_.StartLoop(
        base::TimeDelta::FromMilliseconds(timeout_ms));
  }

  virtual void EmitBareSignal(const std::string& signal_name) {
    DBusWrapperStub::EmitBareSignal(signal_name);
    loop_runner_.StopLoop();
  }

  virtual void EmitSignalWithProtocolBuffer(const std::string& signal_name,
    const google::protobuf::MessageLite& protobuf) {
    DBusWrapperStub::EmitSignalWithProtocolBuffer(signal_name, protobuf);
    loop_runner_.StopLoop();
  }

 private:
  TestMainLoopRunner loop_runner_;

  DISALLOW_COPY_AND_ASSIGN(TestWrapper);
};

}  // namespace

class PeripheralBatteryWatcherTest : public ::testing::Test {
 public:
  PeripheralBatteryWatcherTest() {}
  virtual ~PeripheralBatteryWatcherTest() {}

  void SetUp() override {
    CHECK(temp_dir_.CreateUniqueTempDir());
    base::FilePath device_dir = temp_dir_.path().Append("hid-1-battery");
    CHECK(base::CreateDirectory(device_dir));
    scope_file_ = device_dir.Append("scope");
    WriteFile(scope_file_, "Device");
    model_name_file_ = device_dir.Append("model_name");
    WriteFile(model_name_file_, kDeviceModelName);
    capacity_file_ = device_dir.Append("capacity");
    battery_.set_battery_path_for_testing(temp_dir_.path());
  }

 protected:
  void WriteFile(const base::FilePath& path, const string& str) {
    ASSERT_EQ(str.size(), base::WriteFile(path, str.data(), str.size()));
  }

  // Temporary directory mimicking a /sys directory containing a set of sensor
  // devices.
  base::ScopedTempDir temp_dir_;

  base::FilePath scope_file_;
  base::FilePath capacity_file_;
  base::FilePath model_name_file_;

  TestWrapper test_wrapper_;

  PeripheralBatteryWatcher battery_;

 private:
  DISALLOW_COPY_AND_ASSIGN(PeripheralBatteryWatcherTest);
};

TEST_F(PeripheralBatteryWatcherTest, Basic) {
  std::string level = base::IntToString(80);
  WriteFile(capacity_file_, level);
  battery_.Init(&test_wrapper_);
  ASSERT_TRUE(test_wrapper_.RunUntilSignalSent(kUpdateTimeoutMs));
  EXPECT_EQ(1, test_wrapper_.num_sent_signals());
  PeripheralBatteryStatus proto;
  EXPECT_TRUE(
      test_wrapper_.GetSentSignal(0, kPeripheralBatteryStatusSignal, &proto));
  EXPECT_EQ(80, proto.level());
  EXPECT_EQ(kDeviceModelName, proto.name());
}

TEST_F(PeripheralBatteryWatcherTest, NoLevelReading) {
  battery_.Init(&test_wrapper_);
  // Without writing battery level to the capacity_file_, the loop
  // will timeout.
  ASSERT_FALSE(test_wrapper_.RunUntilSignalSent(kShortUpdateTimeoutMs));
}

}  // namespace system
}  // namespace power_manager
