// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <grp.h>
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>
#include <memory>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <dbus/mock_bus.h>
#include <gtest/gtest.h>
#include <cryptohome/proto_bindings/rpc.pb.h>
#include <cryptohome-client-test/cryptohome/dbus-proxy-mocks.h>

#include "debugd/src/log_tool.h"

using testing::_;
using testing::Invoke;
using testing::Return;
using testing::WithArg;

namespace {
bool WriteFile(const base::FilePath& path, const std::string& contents) {
  return base::CreateDirectory(path.DirName()) &&
         base::WriteFile(path, contents.c_str(), contents.length()) ==
             contents.length();
}
}  // namespace

namespace debugd {

class FakeLog : public LogTool::Log {
 public:
  MOCK_METHOD(std::string, GetLogData, (), (const, override));
};

class LogToolTest : public testing::Test {
 protected:
  std::unique_ptr<LogTool> log_tool_;
  base::ScopedTempDir temp_dir_;

  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    log_tool_ = std::unique_ptr<LogTool>(new LogTool(
        new dbus::MockBus(dbus::Bus::Options()),
        std::make_unique<org::chromium::CryptohomeInterfaceProxyMock>(),
        std::make_unique<FakeLog>(), temp_dir_.GetPath()));

    ON_CALL(*GetFakeLog(), GetLogData).WillByDefault(Return("fake"));
  }

  FakeLog* GetFakeLog() {
    return static_cast<FakeLog*>(log_tool_->arc_bug_report_log_.get());
  }

  std::string GetArcBugReport(const std::string& username, bool* is_backup) {
    return log_tool_->GetArcBugReport(username, is_backup);
  }

  org::chromium::CryptohomeInterfaceProxyMock* GetCryptHomeProxy() {
    return static_cast<org::chromium::CryptohomeInterfaceProxyMock*>(
        log_tool_->cryptohome_proxy_.get());
  }

  void SetArcBugReportBackup(const std::string& userhash) {
    log_tool_->arc_bug_report_backups_.insert(userhash);
  }
};

TEST_F(LogToolTest, GetArcBugReport_ReturnsContents_WhenFileExists) {
  std::string userhash = "0abcdef1230abcdef1230abcdef1230abcdef123";
  base::FilePath logPath =
      temp_dir_.GetPath().Append(userhash).Append("arc-bugreport.log");
  EXPECT_TRUE(WriteFile(logPath, "test"));
  EXPECT_TRUE(base::PathExists(logPath));
  SetArcBugReportBackup(userhash);
  EXPECT_CALL(*GetCryptHomeProxy(), GetSanitizedUsername("username", _, _, _))
      .WillOnce(WithArg<1>(Invoke([&userhash](std::string* out_sanitized) {
        *out_sanitized = userhash;
        return true;
      })));

  bool is_backup;
  std::string report = GetArcBugReport("username", &is_backup);

  EXPECT_EQ(report, "test");
  EXPECT_TRUE(is_backup);
}

TEST_F(LogToolTest, GetArcBugReport_Succeeds_WhenIsBackupIsNull) {
  std::string userhash = "0abcdef1230abcdef1230abcdef1230abcdef123";
  base::FilePath logPath =
      temp_dir_.GetPath().Append(userhash).Append("arc-bugreport.log");
  EXPECT_TRUE(WriteFile(logPath, "test"));
  SetArcBugReportBackup(userhash);
  EXPECT_CALL(*GetCryptHomeProxy(), GetSanitizedUsername("username", _, _, _))
      .WillOnce(WithArg<1>(Invoke([&userhash](std::string* out_sanitized) {
        *out_sanitized = userhash;
        return true;
      })));

  std::string report = GetArcBugReport("username", nullptr /*is_backup*/);

  EXPECT_EQ(report, "test");
}

TEST_F(LogToolTest, GetArcBugReport_DeletesFile_WhenBackupNotSet) {
  std::string userhash = "0abcdef1230abcdef1230abcdef1230abcdef123";
  base::FilePath logPath =
      temp_dir_.GetPath().Append(userhash).Append("arc-bugreport.log");
  EXPECT_TRUE(WriteFile(logPath, "test"));
  EXPECT_TRUE(base::PathExists(logPath));
  EXPECT_CALL(*GetFakeLog(), GetLogData);
  EXPECT_CALL(*GetCryptHomeProxy(), GetSanitizedUsername("username", _, _, _))
      .WillOnce(WithArg<1>(Invoke([&userhash](std::string* out_sanitized) {
        *out_sanitized = userhash;
        return true;
      })));

  bool is_backup;
  std::string report = GetArcBugReport("username", &is_backup);

  EXPECT_EQ(report, "fake");
  EXPECT_FALSE(is_backup);
  EXPECT_FALSE(base::PathExists(logPath));
}

TEST_F(LogToolTest, DeleteArcBugReportBackup) {
  std::string userhash = "0abcdef1230abcdef1230abcdef1230abcdef123";
  base::FilePath logPath = temp_dir_.GetPath()
                            .Append(userhash)
                            .Append("arc-bugreport.log");
  EXPECT_TRUE(WriteFile(logPath, userhash));
  EXPECT_TRUE(base::PathExists(logPath));
  log_tool_->DeleteArcBugReportBackup(userhash);
  EXPECT_FALSE(base::PathExists(logPath));
}

TEST_F(LogToolTest, EncodeString) {
  // U+1F600 GRINNING FACE
  constexpr const char kGrinningFaceUTF8[] = "\xF0\x9F\x98\x80";
  constexpr const char kGrinningFaceBase64[] = "<base64>: 8J+YgA==";
  EXPECT_EQ(kGrinningFaceUTF8,
            LogTool::EncodeString(kGrinningFaceUTF8,
                                  LogTool::Encoding::kAutodetect));
  EXPECT_EQ(
      kGrinningFaceUTF8,
      LogTool::EncodeString(kGrinningFaceUTF8, LogTool::Encoding::kUtf8));
  EXPECT_EQ(
      kGrinningFaceBase64,
      LogTool::EncodeString(kGrinningFaceUTF8, LogTool::Encoding::kBase64));

  // .xz Stream Header Magic Bytes
  constexpr const char kXzStreamHeaderMagicBytes[] = "\xFD\x37\x7A\x58\x5A\x00";
  constexpr const char kXzStreamHeaderMagicUTF8[] =
      "\xEF\xBF\xBD"
      "7zXZ";
  constexpr const char kXzStreamHeaderMagicBase64[] = "<base64>: /Td6WFo=";
  EXPECT_EQ(kXzStreamHeaderMagicBase64,
            LogTool::EncodeString(kXzStreamHeaderMagicBytes,
                                  LogTool::Encoding::kAutodetect));
  EXPECT_EQ(kXzStreamHeaderMagicUTF8,
            LogTool::EncodeString(kXzStreamHeaderMagicBytes,
                                  LogTool::Encoding::kUtf8));
  EXPECT_EQ(kXzStreamHeaderMagicBase64,
            LogTool::EncodeString(kXzStreamHeaderMagicBytes,
                                  LogTool::Encoding::kBase64));

  EXPECT_EQ(kXzStreamHeaderMagicBytes,
            LogTool::EncodeString(kXzStreamHeaderMagicBytes,
                                  LogTool::Encoding::kBinary));
}

class LogTest : public testing::Test {
 protected:
  void SetUp() override {
    std::vector<char> buf(1024);

    uid_t uid = getuid();
    struct passwd pw_entry;
    struct passwd* pw_result;
    ASSERT_EQ(getpwuid_r(uid, &pw_entry, &buf[0], buf.size(), &pw_result), 0);
    ASSERT_NE(pw_result, nullptr);
    user_name_ = pw_entry.pw_name;

    gid_t gid = getgid();
    struct group gr_entry;
    struct group* gr_result;
    ASSERT_EQ(getgrgid_r(gid, &gr_entry, &buf[0], buf.size(), &gr_result), 0);
    ASSERT_NE(gr_result, nullptr);
    group_name_ = gr_entry.gr_name;
  }

  std::string user_name_;
  std::string group_name_;
};

TEST_F(LogTest, GetFileLogData) {
  base::ScopedTempDir temp;
  ASSERT_TRUE(temp.CreateUniqueTempDir());

  base::FilePath file_one = temp.GetPath().Append("test/file_one");
  ASSERT_TRUE(WriteFile(file_one, "test_one_contents"));
  const LogTool::Log log_one(LogTool::Log::kFile, "test_log_one",
                             file_one.value(), user_name_, group_name_);
  EXPECT_EQ(log_one.GetLogData(), "test_one_contents");

  base::FilePath file_two = temp.GetPath().Append("test/file_two");
  ASSERT_TRUE(WriteFile(file_two, ""));
  const LogTool::Log log_two(LogTool::Log::kFile, "test_log_two",
                             file_two.value(), user_name_, group_name_);
  EXPECT_EQ(log_two.GetLogData(), "<empty>");

  base::FilePath file_three = temp.GetPath().Append("test/file_three");
  ASSERT_TRUE(WriteFile(file_three, "long input value"));
  const LogTool::Log log_three(LogTool::Log::kFile, "test_log_three",
                               file_three.value(), user_name_, group_name_, 5);
  EXPECT_EQ(log_three.GetLogData(), "value");
}

TEST_F(LogTest, GetCommandLogData) {
  LogTool::Log log_one(LogTool::Log::kCommand, "test_log_one", "printf ''",
                       user_name_, group_name_);
  log_one.DisableMinijailForTest();
  EXPECT_EQ(log_one.GetLogData(), "<empty>");

  LogTool::Log log_two(LogTool::Log::kCommand, "test_log_two",
                       "printf 'test_output'", user_name_, group_name_);
  log_two.DisableMinijailForTest();
  EXPECT_EQ(log_two.GetLogData(), "test_output");

  LogTool::Log log_three(LogTool::Log::kCommand, "test_log_three",
                         "echo a,b,c | cut -d, -f2", user_name_,
                         group_name_);
  log_three.DisableMinijailForTest();
  EXPECT_EQ(log_three.GetLogData(), "b\n");
}
}  // namespace debugd
