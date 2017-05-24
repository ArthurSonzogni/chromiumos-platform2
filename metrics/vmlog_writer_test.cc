// Copyright (c) 2010 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <utime.h>

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

class VmlogWriterTest : public testing::Test {};

TEST_F(VmlogWriterTest, ParseVmStats) {
  const char kVmStats[] =
    "pswpin 1345\n"
    "pswpout 8896\n"
    "foo 100\n"
    "bar 200\n"
    "pgmajfault 42\n"
    "pgmajfault_a 3838\n"
    "pgmajfault_f 66\n"
    "etcetc 300\n";
  struct VmstatRecord stats;
  EXPECT_TRUE(VmStatsParseStats(kVmStats, &stats));
  EXPECT_EQ(stats.page_faults_, 42);
  EXPECT_EQ(stats.anon_page_faults_, 3838);
  EXPECT_EQ(stats.file_page_faults_, 66);
  EXPECT_EQ(stats.swap_in_, 1345);
  EXPECT_EQ(stats.swap_out_, 8896);
}

TEST_F(VmlogWriterTest, ParseVmStatsOptionalMissing) {
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
  struct VmstatRecord stats;
  EXPECT_TRUE(VmStatsParseStats(kVmStats, &stats));
  EXPECT_EQ(stats.anon_page_faults_, 0);
  EXPECT_EQ(stats.file_page_faults_, 0);
}

TEST_F(VmlogWriterTest, VmlogRotation) {
  base::FilePath temp_directory;
  EXPECT_TRUE(base::GetTempDir(&temp_directory));

  base::FilePath log_path = temp_directory.Append("log");
  base::FilePath rotated_path = temp_directory.Append("rotated");

  // VmlogFile expects to create its output files.
  base::DeleteFile(log_path, false);
  base::DeleteFile(rotated_path, false);

  std::string header_string("header\n");
  VmlogFile l(log_path, rotated_path, 500, header_string);

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
}

TEST_F(VmlogWriterTest, WriteCallbackSuccess) {
  base::FilePath tempdir;
  EXPECT_TRUE(base::GetTempDir(&tempdir));

  // Create a VmlogWriter with a zero log_interval to avoid scheduling write
  // callbacks.
  VmlogWriter writer(tempdir, base::TimeDelta());
  writer.WriteCallback();

  EXPECT_TRUE(base::PathExists(writer.vmlog_->live_path_));
  EXPECT_FALSE(base::PathExists(writer.vmlog_->rotated_path_));
}

}  // namespace chromeos_metrics
