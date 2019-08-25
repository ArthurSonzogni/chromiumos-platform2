// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/system/wakeup_device.h"

#include <limits>
#include <memory>
#include <string>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/strings/string_number_conversions.h>
#include <gtest/gtest.h>

#include "power_manager/common/power_constants.h"

namespace power_manager {
namespace system {
namespace {

// Creates and writes |val| to |sys_path|. Also creates all necessary parent
// directories.
void WriteFile(const base::FilePath& sys_path, const std::string& val) {
  ASSERT_TRUE(base::CreateDirectory(sys_path.DirName()));
  CHECK_EQ(base::WriteFile(sys_path, val.c_str(), val.length()), val.length());
}

}  // namespace
class WakeupDeviceTest : public testing::Test {
 public:
  WakeupDeviceTest() {
    CHECK(temp_dir_.CreateUniqueTempDir());
    std::string kTestSysPath = "sys/devices/pci0000:00/0000:00:14.0/usb1/1-2/";
    wakeup_device_path_ = temp_dir_.GetPath().Append(kTestSysPath);

    wakeup_attr_path_ = wakeup_device_path_.Append(kPowerWakeup);
    WriteFile(wakeup_attr_path_, "enabled");
    wakeup_count_attr_path_ =
        wakeup_device_path_.Append(WakeupDevice::kPowerWakeupCount);

    wakeup_device_ = WakeupDevice::CreateWakeupDevice(wakeup_device_path_);
    CHECK(wakeup_device_);
  }
  ~WakeupDeviceTest() override {}

 protected:
  std::unique_ptr<WakeupDeviceInterface> wakeup_device_;
  base::ScopedTempDir temp_dir_;

  base::FilePath wakeup_device_path_;
  base::FilePath wakeup_attr_path_;
  base::FilePath wakeup_count_attr_path_;

 private:
  DISALLOW_COPY_AND_ASSIGN(WakeupDeviceTest);
};

// An incremented wakeup_count value should result in proper identification of
// wakeup device.
TEST_F(WakeupDeviceTest, TestWakeupCountIncrement) {
  const std::string kWakeupCountBeforeSuspendStr = "1";
  WriteFile(wakeup_count_attr_path_, kWakeupCountBeforeSuspendStr);
  wakeup_device_->PrepareForSuspend();
  const std::string kWakeupCountAfterResumeStr = "2";
  WriteFile(wakeup_count_attr_path_, kWakeupCountAfterResumeStr);
  wakeup_device_->HandleResume();
  EXPECT_TRUE(wakeup_device_->CausedLastWake());
}

// A overflow of wakeup_count value should result in proper identification of
// wakeup device.
TEST_F(WakeupDeviceTest, TestWakeupCountOverflow) {
  const std::string kWakeupCountBeforeSuspendStr =
      base::NumberToString(std::numeric_limits<uint64_t>::max());
  WriteFile(wakeup_count_attr_path_, kWakeupCountBeforeSuspendStr);
  wakeup_device_->PrepareForSuspend();
  const std::string kWakeupCountAfterResumeStr =
      base::NumberToString(std::numeric_limits<uint64_t>::max() + 1);
  WriteFile(wakeup_count_attr_path_, kWakeupCountAfterResumeStr);
  wakeup_device_->HandleResume();
  EXPECT_TRUE(wakeup_device_->CausedLastWake());
}

// A empty wakeup_count file should result in proper identification of
// wakeup device.
TEST_F(WakeupDeviceTest, TestEmptyWakeupCountFile) {
  const std::string kWakeupCountBeforeSuspendStr = "";
  WriteFile(wakeup_count_attr_path_, kWakeupCountBeforeSuspendStr);
  wakeup_device_->PrepareForSuspend();
  const std::string kWakeupCountAfterResumeStr = "2";
  WriteFile(wakeup_count_attr_path_, kWakeupCountAfterResumeStr);
  wakeup_device_->HandleResume();
  EXPECT_TRUE(wakeup_device_->CausedLastWake());
}

// Failure to read the wakeup count before suspend should not mark the device as
// wake source.
TEST_F(WakeupDeviceTest, TestWakeupCountReadFailBeforeSuspend) {
  wakeup_device_->PrepareForSuspend();
  const std::string kWakeupCountAfterResumeStr = base::NumberToString(1);
  WriteFile(wakeup_count_attr_path_, kWakeupCountAfterResumeStr);
  wakeup_device_->HandleResume();
  EXPECT_FALSE(wakeup_device_->CausedLastWake());
}

// Failure to read the wakeup count after resume should not mark the device as
// wake source.
TEST_F(WakeupDeviceTest, TestWakeupCountReadFailAfterResume) {
  const std::string kWakeupCountBeforeSuspendStr = "1";
  WriteFile(wakeup_count_attr_path_, kWakeupCountBeforeSuspendStr);
  wakeup_device_->PrepareForSuspend();
  ASSERT_TRUE(base::DeleteFile(wakeup_count_attr_path_, false));
  wakeup_device_->HandleResume();
  EXPECT_FALSE(wakeup_device_->CausedLastWake());
}

}  // namespace system
}  // namespace power_manager
