// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include <base/logging.h>
#include <base/files/file_path.h>
#include <base/files/scoped_temp_dir.h>
#include <brillo/process/process.h>
#include <gtest/gtest.h>

#include "crash-reporter/test_util.h"

namespace {

// Run the kernel_log_collector.sh script. Returns true if the script is
// successful, false otherwise.
std::string RunKernelLogCollector(const std::string& pattern,
                                  const std::string& duration_seconds,
                                  const std::string& messages_filename) {
  const base::FilePath testdata_dir("testdata");

  brillo::ProcessImpl proc;
  proc.AddArg("kernel_log_collector.sh");
  proc.AddArg(pattern);
  proc.AddArg("30");
  proc.AddArg(testdata_dir.Append(messages_filename).value());

  proc.RedirectUsingMemory(STDOUT_FILENO);

  const auto code = proc.Run();
  // Can't ASSERT in a non-void function, so EXPECT instead. Almost as clear.
  EXPECT_EQ(code, 0);

  return proc.GetOutputString(STDOUT_FILENO);
}

}  // namespace

TEST(KernelLogCollectorTest, Basic) {
  // Test with some real logs pulled from a VM.
  const std::string output =
      RunKernelLogCollector("ectool", "30", "var_log_messages_basic");

  EXPECT_EQ(
      output,
      "2024-06-11T15:36:29.046596Z NOTICE kernel: [   14.159104] Lockdown: "
      "ectool: raw io port access is restricted; see man kernel_lockdown.7\n"
      "2024-06-11T15:36:29.055575Z NOTICE kernel: [   14.168041] Lockdown: "
      "ectool: raw io port access is restricted; see man kernel_lockdown.7\n"
      "END-OF-LOG\n");
}

// We switched away from this time format sometime after kernel 4.14
// Including this just to have an example of the old format checked in.
TEST(KernelLogCollectorTest, OldTimeFormat) {
  // Test with old logs adapted from user feedback.
  const std::string output =
      RunKernelLogCollector(".*", "30", "var_log_messages_old");

  EXPECT_EQ(output, "END-OF-LOG\n");
}

// Make sure we can still get output when the log is empty.
TEST(KernelLogCollectorTest, EmptyLog) {
  const std::string output =
      RunKernelLogCollector(".*", "30", "var_log_messages_empty");

  EXPECT_EQ(output, "END-OF-LOG\n");
}

// Make sure it doesn't read older than we ask for.
TEST(KernelLogCollectorTest, Time) {
  const std::string output =
      RunKernelLogCollector(".*", "30", "var_log_messages_timing");

  EXPECT_EQ(output,
            "2024-06-11T12:01:45.0000Z NOTICE kernel: [   55.000000]"
            " fake - t minus 15s\n"
            "2024-06-11T12:02:00.0000Z NOTICE kernel: [   70.000000]"
            " fake - t minus 0\n"
            "END-OF-LOG\n");
}
