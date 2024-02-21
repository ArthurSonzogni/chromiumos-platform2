// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <map>
#include <memory>
#include <string>

#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/files/scoped_temp_dir.h>
#include <dbus/debugd/dbus-constants.h>

#include <gtest/gtest.h>

#include "debugd/src/binary_log_tool.h"

namespace {

constexpr std::string_view kExpectedWiFiDumpTestData = "test data";
constexpr int kIncorrectBinaryLogType = -1;

}  // namespace

namespace debugd {

class BinaryLogToolTest : public testing::Test {
 protected:
  base::FilePath wifi_logs_;

  std::unique_ptr<BinaryLogTool> binary_log_tool_;

 private:
  void SetUp() override {
    CHECK(tmp_dir_.CreateUniqueTempDir());
    wifi_logs_ = tmp_dir_.GetPath().Append("wifi_logs.txt");

    binary_log_tool_ = std::make_unique<BinaryLogTool>();
  }

  base::ScopedTempDir tmp_dir_;
};

// This test requests a FeedbackBinaryLogType of type WIFI_FIRMWARE_DUMP to
// GetBinaryLogs(). Verify that GetBinaryLogs() writes the correct binary log
// data to the file descriptor.
TEST_F(BinaryLogToolTest, GetBinaryLogsWritesWiFiDumpToFD) {
  // Create a scoped file in a {...} block, so the file is closed once the
  // GetBinaryLogs() writes to it and we can verify its contents.
  {
    base::ScopedFILE file(base::OpenFile(wifi_logs_, "w"));
    ASSERT_NE(file, nullptr);

    std::map<FeedbackBinaryLogType, base::ScopedFD> outfds;
    outfds[FeedbackBinaryLogType::WIFI_FIRMWARE_DUMP] =
        base::ScopedFD(fileno(file.get()));

    binary_log_tool_->GetBinaryLogs("test_username", outfds);
  }

  std::string test_data;
  base::ReadFileToString(wifi_logs_, &test_data);

  EXPECT_EQ(test_data, kExpectedWiFiDumpTestData);
}

// This test requests an invalid FeedbackBinaryLogType to GetBinaryLogs().
// Verify that nothing is written to the file descriptor.
TEST_F(BinaryLogToolTest, IncorrectBinaryLogTypeDoesNotWriteToFD) {
  // Create a scoped file in a {...} block, so the file is closed once the
  // GetBinaryLogs() writes to it and we can verify its contents.
  {
    base::ScopedFILE file(base::OpenFile(wifi_logs_, "w"));
    ASSERT_NE(file, nullptr);

    std::map<FeedbackBinaryLogType, base::ScopedFD> outfds;
    outfds[FeedbackBinaryLogType(kIncorrectBinaryLogType)] =
        base::ScopedFD(fileno(file.get()));

    binary_log_tool_->GetBinaryLogs("test_username", outfds);
  }

  std::string test_data;
  base::ReadFileToString(wifi_logs_, &test_data);

  EXPECT_EQ(test_data, "");
}

}  // namespace debugd
