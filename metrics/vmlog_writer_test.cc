// Copyright (c) 2010 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <utime.h>

#include <sstream>
#include <string>
#include <vector>

#include <base/at_exit.h>
#include <base/files/file_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/stringprintf.h>
#include <chromeos/dbus/service_constants.h>
#include <gtest/gtest.h>

#include "metrics/metrics_daemon.h"
#include "metrics/metrics_library_mock.h"
#include "metrics/persistent_integer_mock.h"
#include "metrics/vmlog_writer.h"

using base::FilePath;
using base::StringPrintf;
using base::Time;
using base::TimeDelta;
using base::TimeTicks;
using std::string;
using std::vector;
using ::testing::_;
using ::testing::AnyNumber;
using ::testing::AtLeast;
using ::testing::Return;
using ::testing::StrictMock;

namespace chromeos_metrics {

TEST(VmlogWriterTest, ParseVmStats) {
  const char kVmStats[] =
    "pswpin 1345\n"
    "pswpout 8896\n"
    "foo 100\n"
    "bar 200\n"
    "pgmajfault 42\n"
    "pgmajfault_a 3838\n"
    "pgmajfault_f 66\n"
    "etcetc 300\n";
  std::istringstream input_stream(kVmStats);
  struct VmstatRecord stats;
  EXPECT_TRUE(VmStatsParseStats(&input_stream, &stats));
  EXPECT_EQ(stats.page_faults_, 42);
  EXPECT_EQ(stats.anon_page_faults_, 3838);
  EXPECT_EQ(stats.file_page_faults_, 66);
  EXPECT_EQ(stats.swap_in_, 1345);
  EXPECT_EQ(stats.swap_out_, 8896);
}

TEST(VmlogWriterTest, ParseVmStatsOptionalMissing) {
  const char kVmStats[] =
    "pswpin 1345\n"
    "pswpout 8896\n"
    "foo 100\n"
    "bar 200\n"
    "pgmajfault 42\n"
    // pgmajfault_a and pgmajfault_f are optional.
    // The default value when missing is 0.
    // "pgmajfault_a 3838\n"
    // "pgmajfault_f 66\n"
    "etcetc 300\n";
  std::istringstream input_stream(kVmStats);
  struct VmstatRecord stats;
  EXPECT_TRUE(VmStatsParseStats(&input_stream, &stats));
  EXPECT_EQ(stats.anon_page_faults_, 0);
  EXPECT_EQ(stats.file_page_faults_, 0);
}

TEST(VmlogWriterTest, ParseAmdgpuFrequency) {
  const char kAmdgpuSclkFrequency[] =
      "0: 200Mhz\n"
      "1: 300Mhz\n"
      "2: 400Mhz *\n"
      "3: 480Mhz\n"
      "4: 553Mhz\n"
      "5: 626Mhz\n"
      "6: 685Mhz\n"
      "7: 720Mhz\n";
  std::istringstream input_stream(kAmdgpuSclkFrequency);
  std::stringstream selected_frequency;
  EXPECT_TRUE(ParseAmdgpuFrequency(selected_frequency, input_stream));
  EXPECT_EQ(selected_frequency.str(), " 400");
}

TEST(VmlogWriterTest, ParseAmdgpuFrequencyMissing) {
  const char kAmdgpuSclkFrequency[] =
      "0: 200Mhz\n"
      "1: 300Mhz\n"
      "2: 400Mhz\n"
      "3: 480Mhz\n"
      "4: 553Mhz\n"
      "5: 626Mhz\n"
      "6: 685Mhz\n"
      "7: 720Mhz\n";
  std::istringstream input_stream(kAmdgpuSclkFrequency);
  std::stringstream selected_frequency;
  EXPECT_FALSE(ParseAmdgpuFrequency(selected_frequency, input_stream));
  EXPECT_EQ(selected_frequency.str(), "");
}

TEST(VmlogWriterTest, ParseCpuTime) {
  const char kProcStat[] =
      "cpu  9440559 4101628 4207468 764635735 5162045 0 132368 0 0 0";
  std::istringstream input_stream(kProcStat);
  struct CpuTimeRecord record;
  EXPECT_TRUE(ParseCpuTime(&input_stream, &record));
  EXPECT_EQ(record.non_idle_time_, 17882023);
  EXPECT_EQ(record.total_time_, 787679803);
}

TEST(VmlogWriterTest, VmlogRotation) {
  base::FilePath temp_directory;
  EXPECT_TRUE(base::CreateNewTempDirectory("", &temp_directory));

  base::FilePath log_path = temp_directory.Append("log");
  base::FilePath rotated_path = temp_directory.Append("rotated");
  base::FilePath latest_symlink_path = temp_directory.Append("vmlog.1.LATEST");
  base::FilePath previous_symlink_path = temp_directory.Append(
      "vmlog.1.PREVIOUS");

  // VmlogFile expects to create its output files.
  base::DeleteFile(log_path, false);
  base::DeleteFile(rotated_path, false);

  std::string header_string("header\n");
  VmlogFile l(log_path, rotated_path, 500, header_string);

  EXPECT_FALSE(base::PathExists(latest_symlink_path));

  std::string x_400(400, 'x');
  EXPECT_TRUE(l.Write(x_400));

  std::string buf;
  EXPECT_TRUE(base::ReadFileToString(log_path, &buf));
  EXPECT_EQ(header_string.size() + x_400.size(), buf.size());
  EXPECT_FALSE(base::ReadFileToString(rotated_path, &buf));

  std::string y_200(200, 'y');
  EXPECT_TRUE(l.Write(y_200));

  EXPECT_TRUE(base::ReadFileToString(log_path, &buf));
  EXPECT_EQ(header_string.size() + y_200.size(), buf.size());
  EXPECT_TRUE(base::ReadFileToString(rotated_path, &buf));
  EXPECT_EQ(header_string.size() + x_400.size(), buf.size());

  EXPECT_TRUE(base::PathExists(latest_symlink_path));
  base::FilePath symlink_target;
  EXPECT_TRUE(base::ReadSymbolicLink(latest_symlink_path,
                                     &symlink_target));
  EXPECT_EQ(rotated_path.value(), symlink_target.value());

  // Test log rotation for vmlog.1 files when a writer is created.
  // We use a zero log_interval to prevent writes from happening.
  EXPECT_TRUE(base::PathExists(latest_symlink_path));
  EXPECT_FALSE(base::PathExists(previous_symlink_path));

  VmlogWriter writer(temp_directory, base::TimeDelta());
  EXPECT_FALSE(base::PathExists(latest_symlink_path));
  EXPECT_TRUE(base::PathExists(previous_symlink_path));

  EXPECT_TRUE(base::ReadSymbolicLink(previous_symlink_path,
                                     &symlink_target));
  EXPECT_EQ(rotated_path.value(), symlink_target.value());
}

TEST(VmlogWriterTest, WriteCallbackSuccess) {
  base::FilePath tempdir;
  EXPECT_TRUE(base::CreateNewTempDirectory("", &tempdir));

  // Create a VmlogWriter with a zero log_interval to avoid scheduling write
  // callbacks.
  VmlogWriter writer(tempdir, base::TimeDelta());
  writer.WriteCallback();

  EXPECT_TRUE(base::PathExists(writer.vmlog_->live_path_));
  EXPECT_FALSE(base::PathExists(writer.vmlog_->rotated_path_));
}

}  // namespace chromeos_metrics
