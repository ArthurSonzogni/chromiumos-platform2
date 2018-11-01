// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/crash_sender_util.h"

#include <stdlib.h>

#include <string>
#include <vector>

#include <base/command_line.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/macros.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/time/time.h>
#include <brillo/flag_helper.h>
#include <brillo/key_value_store.h>
#include <gtest/gtest.h>

#include "crash-reporter/crash_sender_paths.h"
#include "crash-reporter/paths.h"
#include "crash-reporter/test_util.h"

namespace util {
namespace {

// Prases the output file from fake_crash_sender.sh to a vector of items per
// line. Example:
//
// foo1 foo2
// bar1 bar2
//
// => [["foo1", "foo2"], ["bar1, "bar2"]]
//
std::vector<std::vector<std::string>> ParseFakeCrashSenderOutput(
    const std::string& contents) {
  std::vector<std::vector<std::string>> rows;

  std::vector<std::string> lines = base::SplitString(
      contents, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  for (const auto& line : lines) {
    std::vector<std::string> items =
        base::SplitString(line, base::kWhitespaceASCII, base::TRIM_WHITESPACE,
                          base::SPLIT_WANT_NONEMPTY);
    rows.push_back(items);
  }

  return rows;
}

// Helper function for calling GetBasePartOfCrashFile() concisely for tests.
std::string GetBasePartHelper(const std::string& file_name) {
  return GetBasePartOfCrashFile(base::FilePath(file_name)).value();
}

// Helper function for calling base::TouchFile() concisely for tests.
bool TouchFileHelper(const base::FilePath& file_name,
                     base::Time modified_time) {
  return base::TouchFile(file_name, modified_time, modified_time);
}

class CrashSenderUtilTest : public testing::Test {
 private:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    test_dir_ = temp_dir_.GetPath();
    paths::SetPrefixForTesting(test_dir_);
  }

  void TearDown() override {
    paths::SetPrefixForTesting(base::FilePath());

    // ParseCommandLine() sets the environment variables. Reset these here to
    // avoid side effects.
    for (const EnvPair& pair : kEnvironmentVariables)
      unsetenv(pair.name);

    // ParseCommandLine() uses base::CommandLine via
    // brillo::FlagHelper. Reset these here to avoid side effects.
    if (base::CommandLine::InitializedForCurrentProcess())
      base::CommandLine::Reset();
    brillo::FlagHelper::ResetForTesting();
  }

 protected:
  base::ScopedTempDir temp_dir_;
  base::FilePath test_dir_;
};

}  // namespace

TEST_F(CrashSenderUtilTest, ParseCommandLine_MalformedValue) {
  const char* argv[] = {"crash_sender", "-e", "WHATEVER"};
  EXPECT_DEATH(ParseCommandLine(arraysize(argv), argv),
               "Malformed value for -e: WHATEVER");
}

TEST_F(CrashSenderUtilTest, ParseCommandLine_UnknownVariable) {
  const char* argv[] = {"crash_sender", "-e", "FOO=123"};
  EXPECT_DEATH(ParseCommandLine(arraysize(argv), argv),
               "Unknown variable name: FOO");
}

TEST_F(CrashSenderUtilTest, ParseCommandLine_NoFlags) {
  const char* argv[] = {"crash_sender"};
  ParseCommandLine(arraysize(argv), argv);
  // By default, the value is 0.
  EXPECT_STREQ("0", getenv("FORCE_OFFICIAL"));
}

TEST_F(CrashSenderUtilTest, ParseCommandLine_HonorExistingValue) {
  setenv("FORCE_OFFICIAL", "1", 1 /* overwrite */);
  const char* argv[] = {"crash_sender"};
  ParseCommandLine(arraysize(argv), argv);
  EXPECT_STREQ("1", getenv("FORCE_OFFICIAL"));
}

TEST_F(CrashSenderUtilTest, ParseCommandLine_OverwriteDefaultValue) {
  const char* argv[] = {"crash_sender", "-e", "FORCE_OFFICIAL=1"};
  ParseCommandLine(arraysize(argv), argv);
  EXPECT_STREQ("1", getenv("FORCE_OFFICIAL"));
}

TEST_F(CrashSenderUtilTest, ParseCommandLine_OverwriteExistingValue) {
  setenv("FORCE_OFFICIAL", "1", 1 /* overwrite */);
  const char* argv[] = {"crash_sender", "-e", "FORCE_OFFICIAL=2"};
  ParseCommandLine(arraysize(argv), argv);
  EXPECT_STREQ("2", getenv("FORCE_OFFICIAL"));
}

TEST_F(CrashSenderUtilTest, ParseCommandLine_Usage) {
  const char* argv[] = {"crash_sender", "-h"};
  // The third parameter is empty because EXPECT_EXIT does not capture stdout
  // where the usage message is written to.
  EXPECT_EXIT(ParseCommandLine(arraysize(argv), argv),
              testing::ExitedWithCode(0), "");
}

TEST_F(CrashSenderUtilTest, IsMock) {
  EXPECT_FALSE(IsMock());
  ASSERT_TRUE(test_util::CreateFile(
      paths::GetAt(paths::kSystemRunStateDirectory, paths::kMockCrashSending),
      ""));
  EXPECT_TRUE(IsMock());
}

TEST_F(CrashSenderUtilTest, ShouldPauseSending) {
  EXPECT_FALSE(ShouldPauseSending());

  ASSERT_TRUE(test_util::CreateFile(paths::Get(paths::kPauseCrashSending), ""));
  EXPECT_FALSE(ShouldPauseSending());

  setenv("OVERRIDE_PAUSE_SENDING", "0", 1 /* overwrite */);
  EXPECT_TRUE(ShouldPauseSending());

  setenv("OVERRIDE_PAUSE_SENDING", "1", 1 /* overwrite */);
  EXPECT_FALSE(ShouldPauseSending());
}

TEST_F(CrashSenderUtilTest, CheckDependencies) {
  base::FilePath missing_path;

  const int permissions = 0755;  // rwxr-xr-x
  const base::FilePath kFind = paths::Get(paths::kFind);
  const base::FilePath kMetricsClient = paths::Get(paths::kMetricsClient);
  const base::FilePath kRestrictedCertificatesDirectory =
      paths::Get(paths::kRestrictedCertificatesDirectory);

  // kFind is the missing path.
  EXPECT_FALSE(CheckDependencies(&missing_path));
  EXPECT_EQ(kFind.value(), missing_path.value());

  // Create kFind and try again.
  ASSERT_TRUE(test_util::CreateFile(kFind, ""));
  ASSERT_TRUE(base::SetPosixFilePermissions(kFind, permissions));
  EXPECT_FALSE(CheckDependencies(&missing_path));
  EXPECT_EQ(kMetricsClient.value(), missing_path.value());

  // Create kMetricsClient and try again.
  ASSERT_TRUE(test_util::CreateFile(kMetricsClient, ""));
  ASSERT_TRUE(base::SetPosixFilePermissions(kMetricsClient, permissions));
  EXPECT_FALSE(CheckDependencies(&missing_path));
  EXPECT_EQ(kRestrictedCertificatesDirectory.value(), missing_path.value());

  // Create kRestrictedCertificatesDirectory and try again.
  ASSERT_TRUE(base::CreateDirectory(kRestrictedCertificatesDirectory));
  EXPECT_TRUE(CheckDependencies(&missing_path));
}

TEST_F(CrashSenderUtilTest, GetBasePartOfCrashFile) {
  EXPECT_EQ("1", GetBasePartHelper("1"));
  EXPECT_EQ("1.2", GetBasePartHelper("1.2"));
  EXPECT_EQ("1.2.3", GetBasePartHelper("1.2.3"));
  EXPECT_EQ("1.2.3.4", GetBasePartHelper("1.2.3.4"));
  EXPECT_EQ("1.2.3.4", GetBasePartHelper("1.2.3.4.log"));
  EXPECT_EQ("1.2.3.4", GetBasePartHelper("1.2.3.4.log"));
  EXPECT_EQ("1.2.3.4", GetBasePartHelper("1.2.3.4.log.tar"));
  EXPECT_EQ("1.2.3.4", GetBasePartHelper("1.2.3.4.log.tar.gz"));
  // Directory should be preserved.
  EXPECT_EQ("/d/1.2", GetBasePartHelper("/d/1.2"));
  EXPECT_EQ("/d/1.2.3.4", GetBasePartHelper("/d/1.2.3.4.log"));
  // Dots in directory name should not affect the function.
  EXPECT_EQ("/d.d.d.d/1.2.3.4", GetBasePartHelper("/d.d.d.d/1.2.3.4.log"));
}

TEST_F(CrashSenderUtilTest, RemoveOrphanedCrashFiles) {
  const base::FilePath crash_directory =
      paths::Get(paths::kSystemCrashDirectory);
  ASSERT_TRUE(base::CreateDirectory(crash_directory));

  const base::FilePath new_log = crash_directory.Append("0.0.0.0.log");
  const base::FilePath old1_log = crash_directory.Append("1.1.1.1.log");
  const base::FilePath old1_meta = crash_directory.Append("1.1.1.1.meta");
  const base::FilePath old2_log = crash_directory.Append("2.2.2.2.log");
  const base::FilePath old3_log = crash_directory.Append("3.3.3.3.log");
  const base::FilePath old4_log = crash_directory.Append("4.log");

  base::Time now = base::Time::Now();

  // new_log is new thus should not be removed.
  ASSERT_TRUE(test_util::CreateFile(new_log, ""));

  // old1_log is old but comes with the meta file thus should not be removed.
  ASSERT_TRUE(test_util::CreateFile(old1_log, ""));
  ASSERT_TRUE(test_util::CreateFile(old1_meta, ""));
  ASSERT_TRUE(TouchFileHelper(old1_log, now - base::TimeDelta::FromHours(24)));
  ASSERT_TRUE(TouchFileHelper(old1_meta, now - base::TimeDelta::FromHours(24)));

  // old2_log is old without the meta file thus should be removed.
  ASSERT_TRUE(test_util::CreateFile(old2_log, ""));
  ASSERT_TRUE(TouchFileHelper(old2_log, now - base::TimeDelta::FromHours(24)));

  // old3_log is very old without the meta file thus should be removed.
  ASSERT_TRUE(test_util::CreateFile(old3_log, ""));
  ASSERT_TRUE(TouchFileHelper(old3_log, now - base::TimeDelta::FromDays(365)));

  // old4_log is misnamed, but should be removed since it's old.
  ASSERT_TRUE(test_util::CreateFile(old4_log, ""));
  ASSERT_TRUE(TouchFileHelper(old4_log, now - base::TimeDelta::FromHours(24)));

  RemoveOrphanedCrashFiles(crash_directory);

  // Check what files were removed.
  EXPECT_TRUE(base::PathExists(new_log));
  EXPECT_TRUE(base::PathExists(old1_log));
  EXPECT_TRUE(base::PathExists(old1_meta));
  EXPECT_FALSE(base::PathExists(old2_log));
  EXPECT_FALSE(base::PathExists(old3_log));
  EXPECT_FALSE(base::PathExists(old4_log));
}

TEST_F(CrashSenderUtilTest, RemoveInvalidCrashFiles) {
  // TearDown() ensures that the variable will be unset.
  setenv("FORCE_OFFICIAL", "1", 1 /* overwrite */);

  const base::FilePath crash_directory =
      paths::Get(paths::kSystemCrashDirectory);
  ASSERT_TRUE(base::CreateDirectory(crash_directory));

  // These should be kept, since the payload is a known kind and exists.
  const base::FilePath good_meta = crash_directory.Append("good.meta");
  const base::FilePath good_log = crash_directory.Append("good.log");
  ASSERT_TRUE(test_util::CreateFile(good_meta, "payload=good.log\n"));
  ASSERT_TRUE(test_util::CreateFile(good_log, ""));

  // These should be kept, the payload path is absolute but should be handled
  // properly.
  const base::FilePath absolute_meta = crash_directory.Append("absolute.meta");
  const base::FilePath absolute_log = crash_directory.Append("absolute.log");
  ASSERT_TRUE(test_util::CreateFile(absolute_meta,
                                    "payload=" + absolute_log.value() + "\n"));
  ASSERT_TRUE(test_util::CreateFile(absolute_log, ""));

  // This should be removed, since metadata is corrupted.
  const base::FilePath corrupted_meta =
      crash_directory.Append("corrupted.meta");
  ASSERT_TRUE(test_util::CreateFile(corrupted_meta, "!@#$%^&*\n"));

  // This should be removed, since no payload info is recorded.
  const base::FilePath empty_meta = crash_directory.Append("empty.meta");
  ASSERT_TRUE(test_util::CreateFile(empty_meta, ""));

  // This should be removed, since the payload file does not exist.
  const base::FilePath nonexistent_meta =
      crash_directory.Append("nonexistent.meta");
  ASSERT_TRUE(
      test_util::CreateFile(nonexistent_meta, "payload=nonexistent.log\n"));

  // These should be removed, since the payload is an unknown kind.
  const base::FilePath unknown_meta = crash_directory.Append("unknown.meta");
  const base::FilePath unknown_xxx = crash_directory.Append("unknown.xxx");
  ASSERT_TRUE(test_util::CreateFile(unknown_meta, "payload=unknown.xxx\n"));
  ASSERT_TRUE(test_util::CreateFile(unknown_xxx, ""));

  RemoveInvalidCrashFiles(crash_directory);

  // Check what files were removed.
  EXPECT_TRUE(base::PathExists(good_meta));
  EXPECT_TRUE(base::PathExists(good_log));
  EXPECT_TRUE(base::PathExists(absolute_meta));
  EXPECT_TRUE(base::PathExists(absolute_log));
  EXPECT_FALSE(base::PathExists(empty_meta));
  EXPECT_FALSE(base::PathExists(corrupted_meta));
  EXPECT_FALSE(base::PathExists(nonexistent_meta));
  EXPECT_FALSE(base::PathExists(unknown_meta));
  EXPECT_FALSE(base::PathExists(unknown_xxx));

  // All crash files should be removed for an unofficial build.
  setenv("FORCE_OFFICIAL", "0", 1 /* overwrite */);
  RemoveInvalidCrashFiles(crash_directory);
  EXPECT_TRUE(base::IsDirectoryEmpty(crash_directory));
}

TEST_F(CrashSenderUtilTest, RemoveReportFiles) {
  const base::FilePath crash_directory =
      paths::Get(paths::kSystemCrashDirectory);
  ASSERT_TRUE(base::CreateDirectory(crash_directory));

  const base::FilePath foo_meta = crash_directory.Append("foo.meta");
  const base::FilePath foo_log = crash_directory.Append("foo.log");
  const base::FilePath foo_dmp = crash_directory.Append("foo.dmp");
  const base::FilePath bar_log = crash_directory.Append("bar.log");

  ASSERT_TRUE(test_util::CreateFile(foo_meta, ""));
  ASSERT_TRUE(test_util::CreateFile(foo_log, ""));
  ASSERT_TRUE(test_util::CreateFile(foo_dmp, ""));
  ASSERT_TRUE(test_util::CreateFile(bar_log, ""));

  // This should remove foo.*.
  RemoveReportFiles(foo_meta);
  // This should do nothing because the suffix is not ".meta".
  RemoveReportFiles(bar_log);

  // Check what files were removed.
  EXPECT_FALSE(base::PathExists(foo_meta));
  EXPECT_FALSE(base::PathExists(foo_log));
  EXPECT_FALSE(base::PathExists(foo_dmp));
  EXPECT_TRUE(base::PathExists(bar_log));
}

TEST_F(CrashSenderUtilTest, GetMetaFiles) {
  const base::FilePath crash_directory =
      paths::Get(paths::kSystemCrashDirectory);
  ASSERT_TRUE(base::CreateDirectory(crash_directory));

  // Use unsorted file names, to check that GetMetaFiles() sort files by
  // timestamps, not file names.
  const base::FilePath meta_1 = crash_directory.Append("a.meta");
  const base::FilePath meta_2 = crash_directory.Append("s.meta");
  const base::FilePath meta_3 = crash_directory.Append("d.meta");
  const base::FilePath meta_4 = crash_directory.Append("f.meta");
  // This one should not appear in the result.
  const base::FilePath metal_5 = crash_directory.Append("g.metal");

  ASSERT_TRUE(test_util::CreateFile(meta_1, ""));
  ASSERT_TRUE(test_util::CreateFile(meta_2, ""));
  ASSERT_TRUE(test_util::CreateFile(meta_3, ""));
  ASSERT_TRUE(test_util::CreateFile(meta_4, ""));
  ASSERT_TRUE(test_util::CreateFile(metal_5, ""));

  // Change timestamps so that meta_1 is the newest and metal_5 is the oldest.
  base::Time now = base::Time::Now();
  ASSERT_TRUE(TouchFileHelper(meta_1, now - base::TimeDelta::FromHours(1)));
  ASSERT_TRUE(TouchFileHelper(meta_2, now - base::TimeDelta::FromHours(2)));
  ASSERT_TRUE(TouchFileHelper(meta_3, now - base::TimeDelta::FromHours(3)));
  ASSERT_TRUE(TouchFileHelper(meta_4, now - base::TimeDelta::FromHours(4)));
  ASSERT_TRUE(TouchFileHelper(metal_5, now - base::TimeDelta::FromHours(5)));

  std::vector<base::FilePath> meta_files = GetMetaFiles(crash_directory);
  ASSERT_EQ(4, meta_files.size());
  // Confirm that files are sorted in the old-to-new order.
  EXPECT_EQ(meta_4.value(), meta_files[0].value());
  EXPECT_EQ(meta_3.value(), meta_files[1].value());
  EXPECT_EQ(meta_2.value(), meta_files[2].value());
  EXPECT_EQ(meta_1.value(), meta_files[3].value());
}

TEST_F(CrashSenderUtilTest, GetBaseNameFromMetadata) {
  brillo::KeyValueStore metadata;
  metadata.LoadFromString("");
  EXPECT_EQ("", GetBaseNameFromMetadata(metadata, "payload").value());

  metadata.LoadFromString("payload=test.log\n");
  EXPECT_EQ("test.log", GetBaseNameFromMetadata(metadata, "payload").value());

  metadata.LoadFromString("payload=/foo/test.log\n");
  EXPECT_EQ("test.log", GetBaseNameFromMetadata(metadata, "payload").value());
}

TEST_F(CrashSenderUtilTest, GetKindFromPayloadPath) {
  EXPECT_EQ("", GetKindFromPayloadPath(base::FilePath()));
  EXPECT_EQ("", GetKindFromPayloadPath(base::FilePath("foo")));
  EXPECT_EQ("log", GetKindFromPayloadPath(base::FilePath("foo.log")));
  // "dmp" is a special case.
  EXPECT_EQ("minidump", GetKindFromPayloadPath(base::FilePath("foo.dmp")));

  // ".gz" should be ignored.
  EXPECT_EQ("log", GetKindFromPayloadPath(base::FilePath("foo.log.gz")));
  EXPECT_EQ("minidump", GetKindFromPayloadPath(base::FilePath("foo.dmp.gz")));
  EXPECT_EQ("", GetKindFromPayloadPath(base::FilePath("foo.gz")));

  // The directory name should not afect the function.
  EXPECT_EQ("minidump",
            GetKindFromPayloadPath(base::FilePath("/1.2.3/foo.dmp.gz")));
}

TEST_F(CrashSenderUtilTest, ParseMetadata) {
  brillo::KeyValueStore metadata;
  EXPECT_TRUE(ParseMetadata("", &metadata));
  EXPECT_TRUE(ParseMetadata("log=test.log\n", &metadata));
  EXPECT_TRUE(ParseMetadata("#comment\nlog=test.log\n", &metadata));

  // Underscores, dashes, and periods should allowed, as Chrome uses them.
  // https://crbug.com/821530.
  EXPECT_TRUE(ParseMetadata("abcABC012_.-=test.log\n", &metadata));
  std::string value;
  EXPECT_TRUE(metadata.GetString("abcABC012_.-", &value));
  EXPECT_EQ("test.log", value);

  // Invalid metadata should be detected.
  EXPECT_FALSE(ParseMetadata("=test.log\n", &metadata));
  EXPECT_FALSE(ParseMetadata("***\n", &metadata));
  EXPECT_FALSE(ParseMetadata("***=test.log\n", &metadata));
  EXPECT_FALSE(ParseMetadata("log\n", &metadata));
}

TEST_F(CrashSenderUtilTest, Sender) {
  // Set up the mock sesssion manager client.
  auto mock =
      std::make_unique<org::chromium::SessionManagerInterfaceProxyMock>();
  test_util::SetActiveSessions(mock.get(),
                               {{"user1", "hash1"}, {"user2", "hash2"}});

  // Set up the output file for fake_crash_sender.sh.
  const base::FilePath output_file = test_dir_.Append("fake_crash_sender.out");
  setenv("FAKE_CRASH_SENDER_OUTPUT", output_file.value().c_str(),
         1 /* overwrite */);

  // Create crash directories.
  // The crash directory for "user1" is not present, thus should be skipped.
  const base::FilePath system_crash_directory =
      paths::Get(paths::kSystemCrashDirectory);
  ASSERT_TRUE(base::CreateDirectory(system_crash_directory));
  const base::FilePath user2_crash_directory =
      paths::Get("/home/user/hash2/crash");
  ASSERT_TRUE(base::CreateDirectory(user2_crash_directory));

  // Set up the sender.
  Sender::Options options;
  options.shell_script = base::FilePath("fake_crash_sender.sh");
  options.proxy = mock.release();
  Sender sender(options);
  ASSERT_TRUE(sender.Init());

  // Send crashes.
  EXPECT_TRUE(sender.SendCrashes(system_crash_directory));
  EXPECT_TRUE(sender.SendUserCrashes());

  // Check the output file from fake_crash_sender.sh.
  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(output_file, &contents));
  std::vector<std::vector<std::string>> rows =
      ParseFakeCrashSenderOutput(contents);
  ASSERT_EQ(2, rows.size());

  // The first run should be for the system crash directory.
  std::vector<std::string> row = rows[0];
  ASSERT_EQ(2, row.size());
  EXPECT_EQ(sender.temp_dir().value(), row[0]);
  EXPECT_EQ(system_crash_directory.value(), row[1]);

  // The second run should be for "user2".
  row = rows[1];
  ASSERT_EQ(2, row.size());
  EXPECT_EQ(sender.temp_dir().value(), row[0]);
  EXPECT_EQ(user2_crash_directory.value(), row[1]);
}

}  // namespace util
