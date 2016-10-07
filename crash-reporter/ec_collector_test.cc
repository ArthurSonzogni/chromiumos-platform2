// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/strings/stringprintf.h>
#include <brillo/syslog_logging.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "crash-reporter/ec_collector.h"

using base::FilePath;
using brillo::FindLog;

namespace {

const char kECPanicInfo[] = "panicinfo";
const char kDevCoredumpDirectory[] = "cros_ec";

int s_crashes = 0;

void CountCrash() {
  ++s_crashes;
}

bool s_consent_given = true;

bool IsMetrics() {
  return s_consent_given;
}

}  // namespace

class ECCollectorMock : public ECCollector {
 public:
  MOCK_METHOD0(SetUpDBus, void());
};

class ECCollectorTest : public ::testing::Test {
 protected:
  void PreparePanicInfo(bool present, bool stale) {
    FilePath panicinfo_path = collector_.debugfs_path_.Append(kECPanicInfo);

    if (present) {
      char data[116];
      for (unsigned int i = 0; i < sizeof(data); i++)
        data[i] = i;

      if (stale)
        data[PANIC_DATA_FLAGS_BYTE] = PANIC_DATA_FLAG_OLD_HOSTCMD;
      else
        data[PANIC_DATA_FLAGS_BYTE] = ~PANIC_DATA_FLAG_OLD_HOSTCMD;

      ASSERT_EQ(collector_.WriteNewFile(panicinfo_path, data, sizeof(data)),
                static_cast<int>(sizeof(data)));
    } else {
      base::DeleteFile(panicinfo_path, false);
    }
  }

  base::ScopedTempDir temp_dir_generator_;

  ECCollectorMock collector_;

 private:
  void SetUp() override {
    s_consent_given = true;

    EXPECT_CALL(collector_, SetUpDBus()).WillRepeatedly(testing::Return());

    collector_.Initialize(CountCrash, IsMetrics);

    ASSERT_TRUE(temp_dir_generator_.CreateUniqueTempDir());

    collector_.ForceCrashDirectory(temp_dir_generator_.path());

    FilePath debugfs_path =
        temp_dir_generator_.path().Append(kDevCoredumpDirectory);
    ASSERT_TRUE(base::CreateDirectory(debugfs_path));
    collector_.debugfs_path_ = debugfs_path;
  }
};

TEST_F(ECCollectorTest, TestNoConsent) {
  s_consent_given = false;
  PreparePanicInfo(true, false);
  ASSERT_TRUE(collector_.Collect());
  ASSERT_TRUE(FindLog("(ignoring - no consent)"));
  ASSERT_EQ(0, s_crashes);
}

TEST_F(ECCollectorTest, TestNoCrash) {
  PreparePanicInfo(false, false);
  ASSERT_FALSE(collector_.Collect());
  ASSERT_EQ(0, s_crashes);
}

TEST_F(ECCollectorTest, TestStale) {
  PreparePanicInfo(true, true);
  ASSERT_FALSE(collector_.Collect());
  ASSERT_TRUE(FindLog("Old EC crash"));
  ASSERT_EQ(0, s_crashes);
}

TEST_F(ECCollectorTest, TestGood) {
  PreparePanicInfo(true, false);
  ASSERT_TRUE(collector_.Collect());
  ASSERT_TRUE(FindLog("(handling)"));
  ASSERT_EQ(1, s_crashes);
  /* TODO(drinkcat): Test crash file content */
}
