// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/clobber_state_collector.h"

#include <memory>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/memory/ref_counted.h>
#include <base/memory/scoped_refptr.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <metrics/metrics_library.h>
#include <metrics/metrics_library_mock.h>

#include "crash-reporter/crash_collection_status.h"
#include "crash-reporter/test_util.h"

namespace {

using ::testing::HasSubstr;
using ::testing::Not;
using ::testing::Return;

// Contentes of /run/tmpfiles.log that will produce a clobbber report.
constexpr const char kTmpfilesLogErrorContents[] =
    "/usr/lib/tmpfiles.d/vm_tools.conf:35: Duplicate line for path"
    "\"/run/arc/sdcard\", ignoring.\n"
    "contents of tmpfiles.log\n";

// Log config file name.
const char kLogConfigFileName[] = "log_config_file";
const char kTmpfilesLogName[] = "tmpfiles.log";

// A bunch of random rules to put into the log config file.
const char kLogConfigFileContents[] =
    "clobber-state=echo 'found clobber.log'\n";

class ClobberStateCollectorMock : public ClobberStateCollector {
 public:
  ClobberStateCollectorMock()
      : ClobberStateCollector(
            base::MakeRefCounted<
                base::RefCountedData<std::unique_ptr<MetricsLibraryInterface>>>(
                std::make_unique<MetricsLibraryMock>())) {}
  MOCK_METHOD(void, SetUpDBus, (), (override));

  void set_tmpfiles_log(const base::FilePath& tmpfiles_log) {
    tmpfiles_log_ = tmpfiles_log;
  }
};

void Initialize(ClobberStateCollectorMock* collector,
                base::ScopedTempDir* scoped_tmp_dir,
                const std::string& contents,
                base::FilePath& crash_directory) {
  ASSERT_TRUE(scoped_tmp_dir->CreateUniqueTempDir());
  EXPECT_CALL(*collector, SetUpDBus()).WillRepeatedly(Return());
  base::FilePath log_config_path =
      scoped_tmp_dir->GetPath().Append(kLogConfigFileName);
  ASSERT_TRUE(test_util::CreateFile(log_config_path, kLogConfigFileContents));

  base::FilePath tmpfiles_log_path =
      scoped_tmp_dir->GetPath().Append(kTmpfilesLogName);
  ASSERT_TRUE(test_util::CreateFile(tmpfiles_log_path, contents));
  collector->set_tmpfiles_log(tmpfiles_log_path);

  collector->Initialize(false);

  crash_directory = scoped_tmp_dir->GetPath().Append("crash");
  ASSERT_TRUE(base::CreateDirectory(crash_directory));
  collector->set_crash_directory_for_test(crash_directory);
  collector->set_log_config_path(log_config_path.value());
}

TEST(ClobberStateCollectorTest, TestClobberState) {
  ClobberStateCollectorMock collector;
  base::ScopedTempDir tmp_dir;
  base::FilePath crash_directory;
  base::FilePath meta_path;
  base::FilePath report_path;
  std::string report_contents;

  Initialize(&collector, &tmp_dir, kTmpfilesLogErrorContents, crash_directory);

  EXPECT_EQ(collector.Collect(), CrashCollectionStatus::kSuccess);

  // Check report collection.
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      crash_directory, "clobber_state.*.meta", &meta_path));
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      crash_directory, "clobber_state.*.log", &report_path));

  // Check meta contents.
  std::string meta_contents;
  EXPECT_TRUE(base::ReadFileToString(meta_path, &meta_contents));
  EXPECT_THAT(meta_contents, HasSubstr("sig=contents of tmpfiles.log"));

  // Check report contents.
  EXPECT_TRUE(base::ReadFileToString(report_path, &report_contents));
  EXPECT_EQ("found clobber.log\n", report_contents);
}

TEST(ClobberStateCollectorTest, TestClobberState_WarningOnly) {
  ClobberStateCollectorMock collector;
  base::ScopedTempDir tmp_dir;
  base::FilePath crash_directory;
  base::FilePath meta_path;
  base::FilePath report_path;
  std::string report_contents;

  constexpr const char kTmpfilesContents[] =
      "/usr/lib/tmpfiles.d/vm_tools.conf:35: Duplicate line for path "
      "\"/run/arc/sdcard\", ignoring.\n"
      "/usr/lib/tmpfiles.d/vm_tools.conf:36: Duplicate line for path "
      "\"/run/camera\", ignoring.\n"
      "/usr/lib/tmpfiles.d/vm_tools.conf:38: Duplicate line for path "
      "\"/run/perfetto\", ignoring.\nignoring.";
  Initialize(&collector, &tmp_dir, kTmpfilesContents, crash_directory);

  EXPECT_EQ(collector.Collect(), CrashCollectionStatus::kSuccess);

  // Check report collection.
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      crash_directory, "clobber_state.*.meta", &meta_path));
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      crash_directory, "clobber_state.*.log", &report_path));

  // Check meta contents.
  std::string meta_contents;
  EXPECT_TRUE(base::ReadFileToString(meta_path, &meta_contents));
  EXPECT_THAT(meta_contents, HasSubstr(std::string("sig=") + kNoErrorLogged));

  // Check report contents.
  EXPECT_TRUE(base::ReadFileToString(report_path, &report_contents));
  EXPECT_EQ("found clobber.log\n", report_contents);
}

TEST(ClobberStateCollectorTest, TestClobberState_KnownIssue) {
  static constexpr const struct {
    const char* log;
    const char* sig;
  } test_cases[] = {
      {"Failed to create directory or subvolume "
       "\"/var/lib/metrics/structured\": Bad message",
       "sig=Bad message"},
      {"Failed to create directory or subvolume "
       "\"/var/lib/metrics/structured/chromium\": Input/output error",
       "sig=Input/output error"},
      {"\tFailed to create directory or subvolume \"/var/log/vmlog\": "
       "No space left on device",
       "sig=No space left on device"},
      {"rm_rf(/var/lib/dbus/machine-id): Read-only file system",
       "sig=Read-only file system"},
      {"/usr/lib/tmpfiles.d/vm_tools.conf:35: Duplicate line for path"
       "\"/run/arc/sdcard\", ignoring.\n"
       "Failed to open directory 'cras': Structure needs cleaning\n",
       "sig=Structure needs cleaning"},
  };

  for (const auto& test_case : test_cases) {
    ClobberStateCollectorMock collector;
    base::ScopedTempDir tmp_dir;
    base::FilePath crash_directory;
    base::FilePath meta_path;
    base::FilePath report_path;
    std::string report_contents;
    Initialize(&collector, &tmp_dir, test_case.log, crash_directory);

    EXPECT_EQ(collector.Collect(), CrashCollectionStatus::kSuccess);

    // Check report collection.
    EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
        crash_directory, "clobber_state.*.meta", &meta_path));
    EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
        crash_directory, "clobber_state.*.log", &report_path));

    // Check meta contents.
    std::string meta_contents;
    EXPECT_TRUE(base::ReadFileToString(meta_path, &meta_contents));
    EXPECT_THAT(meta_contents, HasSubstr(test_case.sig));

    // Check report contents.
    EXPECT_TRUE(base::ReadFileToString(report_path, &report_contents));
    EXPECT_EQ("found clobber.log\n", report_contents);
  }
}

TEST(ClobberStateCollectorTest,
     TestClobberState_GetCreatedCrashDirectoryByEuidFailure) {
  ClobberStateCollectorMock collector;
  base::ScopedTempDir tmp_dir;
  base::FilePath crash_directory;
  base::FilePath meta_path;
  base::FilePath report_path;
  std::string report_contents;

  Initialize(&collector, &tmp_dir, kTmpfilesLogErrorContents, crash_directory);

  collector.force_get_created_crash_directory_by_euid_status_for_test(
      CrashCollectionStatus::kGetDefaultUserInfoFailed, false);

  EXPECT_EQ(collector.Collect(),
            CrashCollectionStatus::kGetDefaultUserInfoFailed);

  // Check report collection.
  EXPECT_FALSE(test_util::DirectoryHasFileWithPattern(crash_directory, "*.meta",
                                                      &meta_path))
      << meta_path;
  EXPECT_FALSE(test_util::DirectoryHasFileWithPattern(crash_directory, "*.log",
                                                      &report_path))
      << report_path;
}

TEST(ClobberStateCollectorTest, TestClobberState_GetLogContentsFailure) {
  ClobberStateCollectorMock collector;
  base::ScopedTempDir tmp_dir;
  base::FilePath crash_directory;
  base::FilePath meta_path;
  base::FilePath report_path;
  std::string report_contents;

  Initialize(&collector, &tmp_dir, kTmpfilesLogErrorContents, crash_directory);
  // Erase the log config entry that GetLogContents() needs.
  base::FilePath log_config_path = tmp_dir.GetPath().Append(kLogConfigFileName);
  ASSERT_TRUE(test_util::CreateFile(log_config_path, "foo=bar\n"));

  EXPECT_EQ(collector.Collect(),
            CrashCollectionStatus::kExecNotConfiguredForLog);

  // Check report collection.
  EXPECT_FALSE(test_util::DirectoryHasFileWithPattern(crash_directory, "*.meta",
                                                      &meta_path))
      << meta_path;
  EXPECT_FALSE(test_util::DirectoryHasFileWithPattern(crash_directory, "*.log",
                                                      &report_path))
      << report_path;
}

}  // namespace
