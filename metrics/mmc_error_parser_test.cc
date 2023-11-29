// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/logging.h>

#include <dbus/mock_bus.h>

#include <gtest/gtest.h>

#include "metrics/debugd_reader_mock.h"
#include "metrics/mmc_error_parser.h"

namespace chromeos_metrics {

const char kParserName[] = "mmc0";
const char kSecondParserName[] = "mmc1";
const char kPersistentDirName[] = "persistent";
const char kRuntimeDirName[] = "runtime";

class MmcErrorParserTest : public testing::Test {
 protected:
  void SetUp() override {
    dbus::Bus::Options options;

    options.bus_type = dbus::Bus::SYSTEM;
    bus_ = new dbus::MockBus(options);
    CHECK(dir_.CreateUniqueTempDir());
    SetUpParser(kParserName);
  }

  // Helper function to initialize parser_.
  // Can be called from the test case logic to reset the parser and possibly
  // change its name.
  void SetUpParser(std::string_view name) {
    std::unique_ptr<DebugdReaderMock> reader;
    reader.reset(new testing::StrictMock<DebugdReaderMock>(bus_.get(), "test"));
    EXPECT_CALL(*reader.get(), Read)
        .WillRepeatedly(testing::Invoke(this, &MmcErrorParserTest::MockRead));

    parser_ = MmcErrorParser::Create(dir_.GetPath().Append(kPersistentDirName),
                                     dir_.GetPath().Append(kRuntimeDirName),
                                     std::move(reader), name);
    CHECK(parser_);
  }

  std::unique_ptr<MmcErrorParser> parser_;
  base::ScopedTempDir dir_;
  std::string mmc_log_;

 private:
  std::optional<std::string> MockRead() {
    if (mmc_log_.empty()) {
      return std::nullopt;
    }
    return std::make_optional(mmc_log_);
  }
  scoped_refptr<dbus::MockBus> bus_;
};

// Check how the parser handler handles a nullopt response from the reader.
TEST_F(MmcErrorParserTest, ReadErrorTest) {
  parser_->Update();
  MmcErrorRecord record = parser_->GetAndClear();
  EXPECT_EQ(record.cmd_timeouts, 0);
  EXPECT_EQ(record.cmd_crcs, 0);
  EXPECT_EQ(record.data_timeouts, 0);
  EXPECT_EQ(record.data_crcs, 0);
}

// Check if all relevant backing files for PersistentIntegers exist.
// They should be created in the constructor.
TEST_F(MmcErrorParserTest, CreationTest) {
  const char test_mmc_log[] =
      "/sys/kernel/debug/mmc0/err_stats:\n"
      "# Command Timeout Occurred:\t 21\n"
      "# Command CRC Errors Occurred:\t 37\n"
      "# Data Timeout Occurred:\t 42\n"
      "# Data CRC Errors Occurred:\t 1";
  mmc_log_ = test_mmc_log;

  base::FilePath persistent_dir =
      dir_.GetPath().Append(kPersistentDirName).Append(kParserName);
  base::FilePath runtime_dir =
      dir_.GetPath().Append(kRuntimeDirName).Append(kParserName);

  // Call update to trigger the write.
  parser_->Update();
  EXPECT_TRUE(base::PathExists(persistent_dir.Append(kDataTimeoutName)));
  EXPECT_TRUE(base::PathExists(persistent_dir.Append(kDataCRCName)));
  EXPECT_TRUE(base::PathExists(persistent_dir.Append(kCmdTimeoutName)));
  EXPECT_TRUE(base::PathExists(persistent_dir.Append(kCmdCRCName)));
  EXPECT_TRUE(base::PathExists(runtime_dir.Append(kDataTimeoutName)));
  EXPECT_TRUE(base::PathExists(runtime_dir.Append(kDataCRCName)));
  EXPECT_TRUE(base::PathExists(runtime_dir.Append(kCmdTimeoutName)));
  EXPECT_TRUE(base::PathExists(runtime_dir.Append(kCmdCRCName)));
}

// Assign values to all metrics and check if they're parsed correctly.
// Call GetAndClear for the second time to test that we get zeroes.
TEST_F(MmcErrorParserTest, ParsingTest) {
  const char test_mmc_log[] =
      "/sys/kernel/debug/mmc0/err_stats:\n"
      "# Command Timeout Occurred:\t 21\n"
      "# Command CRC Errors Occurred:\t 37\n"
      "# Data Timeout Occurred:\t 42\n"
      "# Data CRC Errors Occurred:\t 1";
  MmcErrorRecord record;

  mmc_log_ = test_mmc_log;

  parser_->Update();
  record = parser_->GetAndClear();
  EXPECT_EQ(record.cmd_timeouts, 21);
  EXPECT_EQ(record.cmd_crcs, 37);
  EXPECT_EQ(record.data_timeouts, 42);
  EXPECT_EQ(record.data_crcs, 1);

  // record should be zeroes if the number of errors doesn't change.
  parser_->Update();
  record = parser_->GetAndClear();
  EXPECT_EQ(record.cmd_timeouts, 0);
  EXPECT_EQ(record.cmd_crcs, 0);
  EXPECT_EQ(record.data_timeouts, 0);
  EXPECT_EQ(record.data_crcs, 0);
}

// Parse a log that contains entries for two controllers.
// Check data parsing for both of them.
TEST_F(MmcErrorParserTest, TwoControllersTest) {
  const char test_mmc_log[] =
      "/sys/kernel/debug/mmc0/err_stats:\n"
      "# Command Timeout Occurred:\t 21\n"
      "# Command CRC Errors Occurred:\t 37\n"
      "# Data Timeout Occurred:\t 42\n"
      "# Data CRC Errors Occurred:\t 1\n\n"
      "/sys/kernel/debug/mmc1/err_stats:\n"
      "# Command Timeout Occurred:\t 121\n"
      "# Command CRC Errors Occurred:\t 137\n"
      "# Data Timeout Occurred:\t 142\n"
      "# Data CRC Errors Occurred:\t 11\n";
  MmcErrorRecord record;

  mmc_log_ = test_mmc_log;

  parser_->Update();
  record = parser_->GetAndClear();
  EXPECT_EQ(record.cmd_timeouts, 21);
  EXPECT_EQ(record.cmd_crcs, 37);
  EXPECT_EQ(record.data_timeouts, 42);
  EXPECT_EQ(record.data_crcs, 1);

  SetUpParser(kSecondParserName);

  parser_->Update();
  record = parser_->GetAndClear();
  EXPECT_EQ(record.cmd_timeouts, 121);
  EXPECT_EQ(record.cmd_crcs, 137);
  EXPECT_EQ(record.data_timeouts, 142);
  EXPECT_EQ(record.data_crcs, 11);
}

// Simulate metrics_daemon crash by re-creating the parser object.
// Then verify that the errors haven't been reported twice.
TEST_F(MmcErrorParserTest, CrashTest) {
  const char test_mmc_log[] =
      "/sys/kernel/debug/mmc0/err_stats:\n"
      "# Command Timeout Occurred:\t 21\n"
      "# Command CRC Errors Occurred:\t 37\n"
      "# Data Timeout Occurred:\t 42\n"
      "# Data CRC Errors Occurred:\t 1";
  MmcErrorRecord record;

  mmc_log_ = test_mmc_log;

  parser_->Update();
  record = parser_->GetAndClear();
  EXPECT_EQ(record.cmd_timeouts, 21);
  EXPECT_EQ(record.cmd_crcs, 37);
  EXPECT_EQ(record.data_timeouts, 42);
  EXPECT_EQ(record.data_crcs, 1);

  SetUpParser(kParserName);

  // Parser should be able to pick up that we cleared its state above.
  parser_->Update();
  record = parser_->GetAndClear();
  EXPECT_EQ(record.cmd_timeouts, 0);
  EXPECT_EQ(record.cmd_crcs, 0);
  EXPECT_EQ(record.data_timeouts, 0);
  EXPECT_EQ(record.data_crcs, 0);
}

}  // namespace chromeos_metrics
