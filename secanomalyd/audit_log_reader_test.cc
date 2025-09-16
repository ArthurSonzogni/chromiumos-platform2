// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "secanomalyd/audit_log_reader.h"

#include <memory>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <gtest/gtest.h>
#include <re2/re2.h>

namespace secanomalyd {

using ReaderRun = std::vector<LogRecord>;

std::unique_ptr<AuditLogReader> InitializeAuditLogReaderForTest(
    const std::string& input_file_name) {
  base::FilePath base_path = base::FilePath(getenv("SRC")).Append("testdata");
  return std::make_unique<AuditLogReader>(base_path.Append(input_file_name));
}

void ReaderTest(const std::unique_ptr<AuditLogReader>& r,
                const ReaderRun& want) {
  ReaderRun got{};
  LogRecord record;
  while (r->GetNextEntry(&record)) {
    got.push_back(record);
  }
  ASSERT_EQ(want.size(), got.size());

  for (int i = 0; i < want.size(); i++) {
    EXPECT_EQ(want[i].tag, got[i].tag);
    EXPECT_EQ(want[i].message, got[i].message);
    EXPECT_EQ(want[i].timestamp.ToTimeT(), got[i].timestamp.ToTimeT());
  }
}

TEST(AuditLogReaderTest, AuditLogReaderTest) {
  auto ar = InitializeAuditLogReaderForTest("TEST_AUDIT_LOG");

  LogRecord e1{.tag = kAVCRecordTag,
               .message =
                   R"(avc:  denied  { module_request } for  pid=1795 )"
                   R"(comm="init" kmod="fs-cgroup2" scontext=u:r:init:s0 )"
                   R"(tcontext=u:r:kernel:s0 tclass=system permissive=0)",
               .timestamp = base::Time::FromTimeT(1588751099)};
  LogRecord e2{.tag = kAVCRecordTag,
               .message =
                   R"(ChromeOS LSM: memfd execution attempt, )"
                   R"(cmd="/usr/bin/memfd_test /usr/sbin/trunks_client", )"
                   R"(pid=666)",
               .timestamp = base::Time::FromTimeT(1589342085)};
  LogRecord e3 = {.tag = kAVCRecordTag,
                  .message = R"(ChromeOS LSM: memfd execution attempt, )"
                             R"(cmd=(null), pid=777)",
                  .timestamp = base::Time::FromTimeT(1629139959)};

  ReaderRun want{std::move(e1), std::move(e2), std::move(e3)};
  ReaderTest(ar, want);
}

// Ensures a kernel emitted memfd execution audit record is detected and parsed
// correctly, and the executable name is correctly parsed.
TEST(AuditLogReaderTest, IsMemfdExecutionTest) {
  std::string cmd;
  EXPECT_TRUE(secanomalyd::IsMemfdExecutionAttempt(
      R"(ChromeOS LSM: memfd execution attempt, cmd="/usr/bin/memfd_test )"
      R"(/usr/sbin/bad_bin", pid=666)",
      cmd));
  EXPECT_EQ(cmd, "/usr/bin/memfd_test");
  EXPECT_TRUE(secanomalyd::IsMemfdExecutionAttempt(
      R"(ChromeOS LSM: memfd execution attempt, cmd="/usr/bin/memfd_test")"
      R"(, pid=666)",
      cmd));
  EXPECT_EQ(cmd, "/usr/bin/memfd_test");
  EXPECT_TRUE(secanomalyd::IsMemfdExecutionAttempt(
      R"(ChromeOS LSM: memfd execution attempt, cmd="/usr/bin/memfd_test )"
      R"(--some-flag some_value --another_flag", pid=666)",
      cmd));
  EXPECT_EQ(cmd, "/usr/bin/memfd_test");
  EXPECT_TRUE(secanomalyd::IsMemfdExecutionAttempt(
      R"(ChromeOS LSM: memfd execution attempt, cmd="bad_executable" )"
      R"(, pid=666)",
      cmd));
  EXPECT_EQ(cmd, "bad_executable");
  EXPECT_TRUE(secanomalyd::IsMemfdExecutionAttempt(
      R"(ChromeOS LSM: memfd execution attempt, cmd=, pid=777)", cmd));
  EXPECT_EQ(cmd, secanomalyd::kUnknownExePath);
  EXPECT_TRUE(secanomalyd::IsMemfdExecutionAttempt(
      R"(ChromeOS LSM: memfd execution attempt, cmd="", pid=777)", cmd));
  EXPECT_EQ(cmd, secanomalyd::kUnknownExePath);
  EXPECT_TRUE(secanomalyd::IsMemfdExecutionAttempt(
      R"(ChromeOS LSM: memfd execution attempt, cmd=(null), pid=777)", cmd));
  EXPECT_EQ(cmd, secanomalyd::kUnknownExePath);
  EXPECT_TRUE(secanomalyd::IsMemfdExecutionAttempt(
      R"(ChromeOS LSM: memfd execution attempt, pid=666)", cmd));
  EXPECT_EQ(cmd, secanomalyd::kUnknownExePath);
  EXPECT_FALSE(secanomalyd::IsMemfdExecutionAttempt(
      R"(avc:  denied  { module_request } for  pid=1795 comm="init")", cmd));
  EXPECT_FALSE(secanomalyd::IsMemfdExecutionAttempt(
      R"(ChromeOS LSM: other event in the future, field="value")", cmd));
  EXPECT_FALSE(secanomalyd::IsMemfdExecutionAttempt(
      R"(======== Some Gibberish ======)", cmd));
  EXPECT_FALSE(secanomalyd::IsMemfdExecutionAttempt("", cmd));
}

}  // namespace secanomalyd
