// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/user_collector.h"

// clang-format off
#include <bits/wordsize.h>
// clang-format on
#include <elf.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <base/files/file.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/memory/ref_counted.h>
#include <base/memory/scoped_refptr.h>
#include <base/strings/strcat.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/stringprintf.h>
#include <base/task/thread_pool.h>
#include <base/test/task_environment.h>
#include <brillo/syslog_logging.h>
#include <chromeos/constants/crash_reporter.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <metrics/metrics_library.h>
#include <metrics/metrics_library_mock.h>

#include "crash-reporter/constants.h"
#include "crash-reporter/crash_collection_status.h"
#include "crash-reporter/paths.h"
#include "crash-reporter/test_util.h"
#include "crash-reporter/vm_support.h"
#include "crash-reporter/vm_support_mock.h"

using base::FilePath;
using brillo::FindLog;
using ::testing::_;
using ::testing::AllOf;
using ::testing::EndsWith;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::Not;
using ::testing::Property;
using ::testing::Return;
using ::testing::StartsWith;

namespace {

const char kFilePath[] = "/my/path";

// Keep in sync with UserCollector::ShouldDump.
const char kChromeIgnoreMsg[] =
    "ignoring call by kernel - chrome crash; "
    "waiting for chrome to call us directly";

}  // namespace

class UserCollectorMock : public UserCollector {
 public:
  UserCollectorMock()
      : UserCollector(
            base::MakeRefCounted<
                base::RefCountedData<std::unique_ptr<MetricsLibraryInterface>>>(
                std::make_unique<MetricsLibraryMock>())) {}
  MOCK_METHOD(void, SetUpDBus, (), (override));
  MOCK_METHOD(std::vector<std::string>,
              GetCommandLine,
              (pid_t),
              (const, override));
  MOCK_METHOD(void, AnnounceUserCrash, (), (override));
  MOCK_METHOD(CrashCollectionStatus,
              ConvertCoreToMinidump,
              (pid_t pid,
               const base::FilePath&,
               const base::FilePath&,
               const base::FilePath&),
              (override));
};

class UserCollectorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    EXPECT_CALL(collector_, SetUpDBus()).WillRepeatedly(testing::Return());

    const std::vector<std::string> default_command_line = {"test_command",
                                                           "--test-arg"};
    EXPECT_CALL(collector_, GetCommandLine(testing::_))
        .WillRepeatedly(testing::Return(default_command_line));

    ASSERT_TRUE(scoped_temp_dir_.CreateUniqueTempDir());
    test_dir_ = scoped_temp_dir_.GetPath();
    paths::SetPrefixForTesting(test_dir_);

    const pid_t pid = getpid();
    collector_.Initialize(kFilePath, false, false, false);
    // Setup paths for output files.
    test_core_pattern_file_ = test_dir_.Append("core_pattern");
    collector_.set_core_pattern_file(test_core_pattern_file_.value());
    test_core_pipe_limit_file_ = test_dir_.Append("core_pipe_limit");
    collector_.set_core_pipe_limit_file(test_core_pipe_limit_file_.value());
    collector_.set_filter_path(test_dir_.Append("no_filter").value());
    crash_dir_ = test_dir_.Append("crash_dir");
    ASSERT_TRUE(base::CreateDirectory(crash_dir_));
    collector_.set_crash_directory_for_test(crash_dir_);
    pid_ = pid;

#if USE_KVM_GUEST
    // Since we're not testing the VM support, just have the VM always return
    // that we should dump from ShouldDump.
    VmSupport::SetForTesting(&vm_support_mock_);
    ON_CALL(vm_support_mock_, ShouldDump(_)).WillByDefault(Return(base::ok()));
#endif

    brillo::ClearLog();
  }

  void TearDown() override {
    paths::SetPrefixForTesting(base::FilePath());
#if USE_KVM_GUEST
    VmSupport::SetForTesting(nullptr);
#endif
  }

  void ExpectFileEquals(const char* golden, const FilePath& file_path) {
    std::string contents;
    EXPECT_TRUE(base::ReadFileToString(file_path, &contents));
    EXPECT_EQ(golden, contents);
  }

  std::vector<std::string> SplitLines(const std::string& lines) const {
    return base::SplitString(lines, "\n", base::KEEP_WHITESPACE,
                             base::SPLIT_WANT_ALL);
  }

  // Verify that the root directory is not writable. Several tests depend on
  // this fact, and are failing in ways that might be explained by having a
  // writable root directory.
  // Not using base::PathIsWritable because that doesn't actually check if the
  // user can write to a path :-/ See 'man 2 access'.
  static bool IsRootDirectoryWritable() {
    base::FilePath temp_file_path;
    if (!CreateTemporaryFileInDir(base::FilePath("/"), &temp_file_path)) {
      return false;
    }
    base::DeleteFile(temp_file_path);
    return true;
  }

  UserCollectorMock collector_;
  pid_t pid_;
  FilePath test_dir_;
  FilePath crash_dir_;
  FilePath test_core_pattern_file_;
  FilePath test_core_pipe_limit_file_;
  base::ScopedTempDir scoped_temp_dir_;
#if USE_KVM_GUEST
  VmSupportMock vm_support_mock_;
#endif
};

TEST_F(UserCollectorTest, EnableOK) {
  ASSERT_TRUE(collector_.Enable(false));
  ExpectFileEquals("|/my/path --user=%P:%s:%u:%g:%f", test_core_pattern_file_);
  ExpectFileEquals("4", test_core_pipe_limit_file_);
  EXPECT_TRUE(FindLog("Enabling user crash handling"));
}

TEST_F(UserCollectorTest, EnableNoPatternFileAccess) {
  // Basic checking:
  // Confirm we don't have junk left over from other tests.
  ASSERT_FALSE(base::PathExists(base::FilePath("/does_not_exist")));
  // We've seen strange problems that might be explained by having / writable.
  ASSERT_FALSE(IsRootDirectoryWritable());

  collector_.set_core_pattern_file("/does_not_exist");
  ASSERT_FALSE(collector_.Enable(false));
  EXPECT_TRUE(FindLog("Enabling user crash handling"));
  EXPECT_TRUE(FindLog("Unable to write /does_not_exist"));
}

TEST_F(UserCollectorTest, EnableNoPipeLimitFileAccess) {
  // Basic checking:
  // Confirm we don't have junk left over from other tests.
  ASSERT_FALSE(base::PathExists(base::FilePath("/does_not_exist")));
  // We've seen strange problems that might be explained by having / writable.
  ASSERT_FALSE(IsRootDirectoryWritable());

  collector_.set_core_pipe_limit_file("/does_not_exist");
  ASSERT_FALSE(collector_.Enable(false));
  // Core pattern should not be written if we cannot access the pipe limit
  // or otherwise we may set a pattern that results in infinite recursion.
  ASSERT_FALSE(base::PathExists(test_core_pattern_file_));
  EXPECT_TRUE(FindLog("Enabling user crash handling"));
  EXPECT_TRUE(FindLog("Unable to write /does_not_exist"));
}

TEST_F(UserCollectorTest, DisableOK) {
  ASSERT_TRUE(collector_.Disable());
  ExpectFileEquals("core", test_core_pattern_file_);
  EXPECT_TRUE(FindLog("Disabling user crash handling"));
}

TEST_F(UserCollectorTest, DisableNoFileAccess) {
  // Basic checking:
  // Confirm we don't have junk left over from other tests.
  ASSERT_FALSE(base::PathExists(base::FilePath("/does_not_exist")));
  // We've seen strange problems that might be explained by having / writable.
  ASSERT_FALSE(IsRootDirectoryWritable());

  collector_.set_core_pattern_file("/does_not_exist");
  ASSERT_FALSE(collector_.Disable());
  EXPECT_TRUE(FindLog("Disabling user crash handling"));
  EXPECT_TRUE(FindLog("Unable to write /does_not_exist"));
}

TEST_F(UserCollectorTest, ParseCrashAttributes) {
  std::optional<UserCollectorBase::CrashAttributes> attrs =
      UserCollectorBase::ParseCrashAttributes("123456:11:1000:2000:foobar");
  ASSERT_TRUE(attrs);
  EXPECT_EQ(123456, attrs->pid);
  EXPECT_EQ(11, attrs->signal);
  EXPECT_EQ(1000, attrs->uid);
  EXPECT_EQ(2000, attrs->gid);
  EXPECT_EQ("foobar", attrs->exec_name);

  attrs = UserCollectorBase::ParseCrashAttributes("4321:6:0:0:barfoo");
  ASSERT_TRUE(attrs);
  EXPECT_EQ(4321, attrs->pid);
  EXPECT_EQ(6, attrs->signal);
  EXPECT_EQ(0, attrs->uid);
  EXPECT_EQ(0, attrs->gid);
  EXPECT_EQ("barfoo", attrs->exec_name);

  EXPECT_FALSE(UserCollectorBase::ParseCrashAttributes("123456:11:1000"));
  EXPECT_FALSE(UserCollectorBase::ParseCrashAttributes("123456:11:1000:100"));

  attrs =
      UserCollectorBase::ParseCrashAttributes("123456:11:1000:100:exec:extra");
  ASSERT_TRUE(attrs);
  EXPECT_EQ("exec:extra", attrs->exec_name);

  EXPECT_FALSE(
      UserCollectorBase::ParseCrashAttributes("12345p:11:1000:100:foobar"));

  EXPECT_FALSE(
      UserCollectorBase::ParseCrashAttributes("123456:1 :1000:0:foobar"));

  EXPECT_FALSE(UserCollectorBase::ParseCrashAttributes("123456::::foobar"));
}

TEST_F(UserCollectorTest, ShouldDumpChromeOverridesDeveloperImage) {
  const std::vector<std::string> kChromeProcessNames = {
      "chrome",
      "supplied_Compositor",
      "supplied_PipelineThread",
      "Chrome_ChildIOThread",
      "supplied_Chrome_ChildIOT",
      "supplied_ChromotingClien",
      "supplied_LocalInputMonit"};
  // When handle_chrome_crashes is false, should ignore chrome processes.
  for (const std::string& chrome_process_name : kChromeProcessNames) {
    EXPECT_EQ(
        collector_.ShouldDump(pid_, false, chrome_process_name),
        base::unexpected(CrashCollectionStatus::kChromeCrashInUserCollector))
        << " for " << chrome_process_name;
  }

  // It's important that the returned status will resolve to the correct
  // message:
  EXPECT_EQ(kChromeIgnoreMsg,
            CrashCollectionStatusToString(
                CrashCollectionStatus::kChromeCrashInUserCollector));

  // Test that chrome crashes are handled when the "handle_chrome_crashes" flag
  // is set.
  for (const std::string& chrome_process_name : kChromeProcessNames) {
    EXPECT_EQ(collector_.ShouldDump(pid_, true, chrome_process_name),
              base::ok())
        << " for " << chrome_process_name;
  }
}

TEST_F(UserCollectorTest, ShouldDumpUserConsentProductionImage) {
  EXPECT_EQ(collector_.ShouldDump(pid_, false, "chrome-wm"), base::ok());
}

// HandleNonChromeCrashWithConsent tests that we will create a dmp file if we
// (a) have user consent to collect crash data and
// (b) the process is not a Chrome process.
TEST_F(UserCollectorTest, HandleNonChromeCrashWithConsent) {
  // Note the _ which is different from the - in the original |force_exec|
  // passed to HandleCrash. This is due to the CrashCollector::Sanitize call in
  // FormatDumpBasename.
  const std::string crash_prefix = crash_dir_.Append("chromeos_wm").value();
  int expected_mock_calls = 1;
  EXPECT_CALL(collector_, AnnounceUserCrash()).Times(expected_mock_calls);
  // NOTE: The '5' which appears in several strings below is the pid of the
  // simulated crashing process.
  EXPECT_CALL(collector_,
              ConvertCoreToMinidump(
                  5, FilePath("/tmp/crash_reporter/5"),
                  Property(&FilePath::value,
                           AllOf(StartsWith(crash_prefix), EndsWith("core"))),
                  Property(&FilePath::value,
                           AllOf(StartsWith(crash_prefix), EndsWith("dmp")))))
      .Times(expected_mock_calls)
      .WillRepeatedly(Return(CrashCollectionStatus::kSuccess));

  UserCollectorBase::CrashAttributes attrs;
  attrs.pid = 5;
  attrs.signal = 2;
  attrs.uid = 1000;
  attrs.gid = 1000;
  attrs.exec_name = "ignored";
  EXPECT_EQ(collector_.HandleCrash(attrs, "chromeos-wm"),
            CrashCollectionStatus::kSuccess);
  EXPECT_TRUE(test_util::DirectoryHasFileWithPatternAndContents(
      crash_dir_, "chromeos_wm.*.meta", "exec_name=chromeos-wm"));
  EXPECT_TRUE(FindLog("Received crash notification for chromeos-wm[5] sig 2"));
}

TEST_F(UserCollectorTest, HandleNonChromeCrashWithConsentAndSigsysNoSyscall) {
  // Note the _ which is different from the - in the original |force_exec|
  // passed to HandleCrash. This is due to the CrashCollector::Sanitize call in
  // FormatDumpBasename.
  const std::string crash_prefix = crash_dir_.Append("chromeos_wm").value();
  int expected_mock_calls = 1;
  EXPECT_CALL(collector_, AnnounceUserCrash()).Times(expected_mock_calls);
  // NOTE: The '5' which appears in several strings below is the pid of the
  // simulated crashing process.
  EXPECT_CALL(collector_,
              ConvertCoreToMinidump(
                  5, FilePath("/tmp/crash_reporter/5"),
                  Property(&FilePath::value,
                           AllOf(StartsWith(crash_prefix), EndsWith("core"))),
                  Property(&FilePath::value,
                           AllOf(StartsWith(crash_prefix), EndsWith("dmp")))))
      .Times(expected_mock_calls)
      .WillRepeatedly(Return(CrashCollectionStatus::kSuccess));

  UserCollectorBase::CrashAttributes attrs;
  attrs.pid = 5;
  attrs.signal = SIGSYS;
  attrs.uid = 1000;
  attrs.gid = 1000;
  attrs.exec_name = "ignored";
  // Should succeed even without /proc/[pid]/syscall
  EXPECT_EQ(collector_.HandleCrash(attrs, "chromeos-wm"),
            CrashCollectionStatus::kSuccess);
  EXPECT_TRUE(test_util::DirectoryHasFileWithPatternAndContents(
      crash_dir_, "chromeos_wm.*.meta", "exec_name=chromeos-wm"));
  EXPECT_TRUE(FindLog("Received crash notification for chromeos-wm[5] sig 31"));
}

#if USE_KVM_GUEST
// On VMs, confirm that if VmSupport::Get()->ShouldDump returns "don't dump",
// UserCollector will not create a crash dump
TEST_F(UserCollectorTest, HandleDoesNotDumpIfVmSupportSaysNotTo) {
  EXPECT_CALL(vm_support_mock_, ShouldDump(5))
      .WillOnce(Return(base::unexpected(
          CrashCollectionStatus::kVmProcessNotInRootNamespace)));
  const std::string crash_prefix = crash_dir_.Append("chromeos_wm").value();
  EXPECT_CALL(collector_, AnnounceUserCrash()).Times(0);
  EXPECT_CALL(collector_, ConvertCoreToMinidump(_, _, _, _)).Times(0);

  UserCollectorBase::CrashAttributes attrs;
  attrs.pid = 5;
  attrs.signal = 2;
  attrs.uid = 1000;
  attrs.gid = 1000;
  attrs.exec_name = "ignored";
  EXPECT_EQ(collector_.HandleCrash(attrs, "chromeos-wm"),
            CrashCollectionStatus::kVmProcessNotInRootNamespace);
  EXPECT_FALSE(test_util::DirectoryHasFileWithPatternAndContents(
      crash_dir_, "chromeos_wm.*.meta", "exec_name=chromeos-wm"));
}
#endif  // USE_KVM_GUEST

// HandleChromeCrashWithConsent tests that we do not attempt to create a dmp
// file if the process is named chrome. This is because we expect Chrome's own
// crash handling library (Breakpad or Crashpad) to call us directly -- see
// chrome_collector.h.
TEST_F(UserCollectorTest, HandleChromeCrashWithConsent) {
  EXPECT_CALL(collector_, AnnounceUserCrash()).Times(0);
  EXPECT_CALL(collector_, ConvertCoreToMinidump(_, _, _, _)).Times(0);

  UserCollectorBase::CrashAttributes attrs;
  attrs.pid = 5;
  attrs.signal = 2;
  attrs.uid = 1000;
  attrs.gid = 1000;
  attrs.exec_name = "ignored";
  EXPECT_EQ(collector_.HandleCrash(attrs, "chrome"),
            CrashCollectionStatus::kChromeCrashInUserCollector);
  EXPECT_FALSE(test_util::DirectoryHasFileWithPattern(
      crash_dir_, "chrome.*.meta", nullptr));
  EXPECT_TRUE(FindLog("Received crash notification for chrome[5] sig 2"));
  EXPECT_TRUE(FindLog(kChromeIgnoreMsg));
}

// HandleSuppliedChromeCrashWithConsent also tests that we do not attempt to
// create a dmp file if the process is named chrome. This differs only in the
// fact that we are using the kernel's supplied name instead of the |force_exec|
// name. This is actually much closer to the real usage.
TEST_F(UserCollectorTest, HandleSuppliedChromeCrashWithConsent) {
  EXPECT_CALL(collector_, AnnounceUserCrash()).Times(0);
  EXPECT_CALL(collector_, ConvertCoreToMinidump(_, _, _, _)).Times(0);

  UserCollectorBase::CrashAttributes attrs;
  attrs.pid = 5;
  attrs.signal = 2;
  attrs.uid = 1000;
  attrs.gid = 1000;
  attrs.exec_name = "chrome";
  EXPECT_EQ(collector_.HandleCrash(attrs, nullptr),
            CrashCollectionStatus::kChromeCrashInUserCollector);
  EXPECT_FALSE(test_util::DirectoryHasFileWithPattern(
      crash_dir_, "chrome.*.meta", nullptr));
  EXPECT_TRUE(
      FindLog("Received crash notification for supplied_chrome[5] sig 2"));
  EXPECT_TRUE(FindLog(kChromeIgnoreMsg));
}

TEST_F(UserCollectorTest, GetExecutableBaseNameFromPid) {
  // We want to use the real proc filesystem.
  paths::SetPrefixForTesting(base::FilePath());
  std::string base_name;
  base::FilePath exec_directory;
  EXPECT_FALSE(collector_.GetExecutableBaseNameAndDirectoryFromPid(
      0, &base_name, &exec_directory));
  EXPECT_TRUE(
      FindLog("ReadSymbolicLink failed - Path /proc/0 DirectoryExists: 0"));
  EXPECT_TRUE(FindLog("stat /proc/0/exe failed: -1 2"));

  brillo::ClearLog();
  pid_t my_pid = getpid();
  EXPECT_TRUE(collector_.GetExecutableBaseNameAndDirectoryFromPid(
      my_pid, &base_name, &exec_directory));
  EXPECT_FALSE(FindLog("Readlink failed"));
  EXPECT_EQ("crash_reporter_test", base_name);
  EXPECT_THAT(exec_directory.value(),
              HasSubstr("chromeos-base/crash-reporter"));
}

TEST_F(UserCollectorTest, GetFirstLineWithPrefix) {
  std::vector<std::string> lines;
  std::string line;

  EXPECT_FALSE(collector_.GetFirstLineWithPrefix(lines, "Name:", &line));
  EXPECT_EQ("", line);

  lines.push_back("Name:\tls");
  lines.push_back("State:\tR (running)");
  lines.push_back(" Foo:\t1000");

  line.clear();
  EXPECT_TRUE(collector_.GetFirstLineWithPrefix(lines, "Name:", &line));
  EXPECT_EQ(lines[0], line);

  line.clear();
  EXPECT_TRUE(collector_.GetFirstLineWithPrefix(lines, "State:", &line));
  EXPECT_EQ(lines[1], line);

  line.clear();
  EXPECT_FALSE(collector_.GetFirstLineWithPrefix(lines, "Foo:", &line));
  EXPECT_EQ("", line);

  line.clear();
  EXPECT_TRUE(collector_.GetFirstLineWithPrefix(lines, " Foo:", &line));
  EXPECT_EQ(lines[2], line);

  line.clear();
  EXPECT_FALSE(collector_.GetFirstLineWithPrefix(lines, "Bar:", &line));
  EXPECT_EQ("", line);
}

TEST_F(UserCollectorTest, GetIdFromStatus) {
  int id = 1;
  EXPECT_FALSE(collector_.GetIdFromStatus(UserCollector::kUserId,
                                          UserCollector::kIdEffective,
                                          SplitLines("nothing here"), &id));
  EXPECT_EQ(id, 1);

  // Not enough parameters.
  EXPECT_FALSE(
      collector_.GetIdFromStatus(UserCollector::kUserId, UserCollector::kIdReal,
                                 SplitLines("line 1\nUid:\t1\n"), &id));

  const std::vector<std::string> valid_contents =
      SplitLines("\nUid:\t1\t2\t3\t4\nGid:\t5\t6\t7\t8\n");
  EXPECT_TRUE(collector_.GetIdFromStatus(
      UserCollector::kUserId, UserCollector::kIdReal, valid_contents, &id));
  EXPECT_EQ(1, id);

  EXPECT_TRUE(collector_.GetIdFromStatus(UserCollector::kUserId,
                                         UserCollector::kIdEffective,
                                         valid_contents, &id));
  EXPECT_EQ(2, id);

  EXPECT_TRUE(collector_.GetIdFromStatus(UserCollector::kUserId,
                                         UserCollector::kIdFileSystem,
                                         valid_contents, &id));
  EXPECT_EQ(4, id);

  EXPECT_TRUE(collector_.GetIdFromStatus(UserCollector::kGroupId,
                                         UserCollector::kIdEffective,
                                         valid_contents, &id));
  EXPECT_EQ(6, id);

  EXPECT_TRUE(collector_.GetIdFromStatus(
      UserCollector::kGroupId, UserCollector::kIdSet, valid_contents, &id));
  EXPECT_EQ(7, id);

  EXPECT_FALSE(collector_.GetIdFromStatus(
      UserCollector::kGroupId, UserCollector::IdKind(5), valid_contents, &id));
  EXPECT_FALSE(collector_.GetIdFromStatus(
      UserCollector::kGroupId, UserCollector::IdKind(-1), valid_contents, &id));

  // Fail if junk after number
  EXPECT_FALSE(
      collector_.GetIdFromStatus(UserCollector::kUserId, UserCollector::kIdReal,
                                 SplitLines("Uid:\t1f\t2\t3\t4\n"), &id));
  EXPECT_TRUE(
      collector_.GetIdFromStatus(UserCollector::kUserId, UserCollector::kIdReal,
                                 SplitLines("Uid:\t1\t2\t3\t4\n"), &id));
  EXPECT_EQ(1, id);

  // Fail if more than 4 numbers.
  EXPECT_FALSE(
      collector_.GetIdFromStatus(UserCollector::kUserId, UserCollector::kIdReal,
                                 SplitLines("Uid:\t1\t2\t3\t4\t5\n"), &id));
}

TEST_F(UserCollectorTest, GetStateFromStatus) {
  std::string state;
  EXPECT_FALSE(
      collector_.GetStateFromStatus(SplitLines("nothing here"), &state));
  EXPECT_EQ("", state);

  EXPECT_TRUE(
      collector_.GetStateFromStatus(SplitLines("State:\tR (running)"), &state));
  EXPECT_EQ("R (running)", state);

  EXPECT_TRUE(collector_.GetStateFromStatus(
      SplitLines("Name:\tls\nState:\tZ (zombie)\n"), &state));
  EXPECT_EQ("Z (zombie)", state);
}

TEST_F(UserCollectorTest, ClobberContainerDirectory) {
  // Try a path that is not writable.
  ASSERT_FALSE(collector_.ClobberContainerDirectory(FilePath("/bad/path")));
  EXPECT_TRUE(FindLog("Could not create /bad/path"));
}

TEST_F(UserCollectorTest, CopyOffProcFilesBadPid) {
  // Makes searching for the log string a little easier.
  paths::SetPrefixForTesting(base::FilePath());
  FilePath container_path = test_dir_.Append("container");
  ASSERT_TRUE(collector_.ClobberContainerDirectory(container_path));

  ASSERT_FALSE(collector_.CopyOffProcFiles(0, container_path));
  EXPECT_TRUE(FindLog("Path /proc/0 does not exist"));
}

TEST_F(UserCollectorTest, CopyOffProcFilesOK) {
  // We want to use the real proc filesystem.
  paths::SetPrefixForTesting(base::FilePath());
  FilePath container_path = test_dir_.Append("container");
  ASSERT_TRUE(collector_.ClobberContainerDirectory(container_path));

  ASSERT_TRUE(collector_.CopyOffProcFiles(pid_, container_path));
  EXPECT_FALSE(FindLog("Could not copy"));
  static const struct {
    const char* name;
    bool exists;
  } kExpectations[] = {
      {"auxv", true},   {"cmdline", true}, {"environ", true},
      {"maps", true},   {"mem", false},    {"mounts", false},
      {"sched", false}, {"status", true},  {"syscall", true},
  };
  for (const auto& expectation : kExpectations) {
    EXPECT_EQ(expectation.exists,
              base::PathExists(container_path.Append(expectation.name)));
  }
}

TEST_F(UserCollectorTest, GetRustSignature) {
  // We want to use the real proc filesystem.
  paths::SetPrefixForTesting(base::FilePath());

  int fd = memfd_create("RUST_PANIC_SIG", MFD_CLOEXEC);
  char dat[] = "Rust panic signature\nignored lines\n...";
  int count = strlen(dat);
  EXPECT_EQ(count, write(fd, dat, count));

  std::string panic_sig;
  bool success = collector_.GetRustSignature(pid_, &panic_sig);
  EXPECT_EQ(0, close(fd));

  ASSERT_TRUE(success);
  EXPECT_EQ("Rust panic signature", panic_sig);
}

TEST_F(UserCollectorTest, ValidateProcFiles) {
  FilePath container_dir = test_dir_;

  // maps file not exists (i.e. GetFileSize fails)
  EXPECT_FALSE(collector_.ValidateProcFiles(container_dir));

  // maps file is empty
  FilePath maps_file = container_dir.Append("maps");
  ASSERT_TRUE(test_util::CreateFile(maps_file, ""));
  ASSERT_TRUE(base::PathExists(maps_file));
  EXPECT_FALSE(collector_.ValidateProcFiles(container_dir));

  // maps file is not empty
  const char data[] = "test data";
  ASSERT_TRUE(test_util::CreateFile(maps_file, data));
  ASSERT_TRUE(base::PathExists(maps_file));
  EXPECT_TRUE(collector_.ValidateProcFiles(container_dir));
}

TEST_F(UserCollectorTest, ValidateCoreFile) {
  FilePath core_file = test_dir_.Append("core");

  // Core file does not exist
  EXPECT_EQ(CrashCollectionStatus::kFailureOpeningCoreFile,
            collector_.ValidateCoreFile(core_file));
  char e_ident[EI_NIDENT];
  e_ident[EI_MAG0] = ELFMAG0;
  e_ident[EI_MAG1] = ELFMAG1;
  e_ident[EI_MAG2] = ELFMAG2;
  e_ident[EI_MAG3] = ELFMAG3;
#if __WORDSIZE == 32
  e_ident[EI_CLASS] = ELFCLASS32;
#elif __WORDSIZE == 64
  e_ident[EI_CLASS] = ELFCLASS64;
#else
#error Unknown/unsupported value of __WORDSIZE.
#endif

  // Core file has the expected header
  ASSERT_TRUE(
      test_util::CreateFile(core_file, std::string(e_ident, sizeof(e_ident))));
  EXPECT_EQ(CrashCollectionStatus::kSuccess,
            collector_.ValidateCoreFile(core_file));

#if __WORDSIZE == 64
  // 32-bit core file on 64-bit platform
  e_ident[EI_CLASS] = ELFCLASS32;
  ASSERT_TRUE(
      test_util::CreateFile(core_file, std::string(e_ident, sizeof(e_ident))));
  EXPECT_EQ(CrashCollectionStatus::kFailureUnsupported32BitCoreFile,
            collector_.ValidateCoreFile(core_file));
  e_ident[EI_CLASS] = ELFCLASS64;
#endif

  // Invalid core files
  ASSERT_TRUE(test_util::CreateFile(core_file,
                                    std::string(e_ident, sizeof(e_ident) - 1)));
  EXPECT_EQ(CrashCollectionStatus::kFailureReadingCoreFileHeader,
            collector_.ValidateCoreFile(core_file));

  e_ident[EI_MAG0] = 0;
  ASSERT_TRUE(
      test_util::CreateFile(core_file, std::string(e_ident, sizeof(e_ident))));
  EXPECT_EQ(CrashCollectionStatus::kBadCoreFileMagic,
            collector_.ValidateCoreFile(core_file));
}

TEST_F(UserCollectorTest, HandleSyscall) {
  const std::string exec = "placeholder";
  const std::string contents = std::to_string(SYS_read) + " col1 col2 col3";

  collector_.HandleSyscall(exec, contents);
  EXPECT_TRUE(
      base::Contains(collector_.extra_metadata_,
                     "seccomp_blocked_syscall_nr=" + std::to_string(SYS_read)));
  EXPECT_TRUE(base::Contains(collector_.extra_metadata_,
                             "seccomp_proc_pid_syscall=" + contents));
  EXPECT_TRUE(base::Contains(collector_.extra_metadata_,
                             std::string("seccomp_blocked_syscall_name=read")));
  EXPECT_TRUE(
      base::Contains(collector_.extra_metadata_,
                     std::string("sig=") + exec + "-seccomp-violation-read"));
}

TEST_F(UserCollectorTest, ComputeSeverity_SessionManagerExecutable) {
  CrashCollector::ComputedCrashSeverity computed_severity =
      collector_.ComputeSeverity("session_manager");

  EXPECT_EQ(computed_severity.crash_severity,
            CrashCollector::CrashSeverity::kFatal);
  EXPECT_EQ(computed_severity.product_group,
            CrashCollector::Product::kPlatform);
}

TEST_F(UserCollectorTest, ComputeSeverity_NotSessionManagerExecutable) {
  CrashCollector::ComputedCrashSeverity computed_severity =
      collector_.ComputeSeverity("not session_manager");

  EXPECT_EQ(computed_severity.crash_severity,
            CrashCollector::CrashSeverity::kError);
  EXPECT_EQ(computed_severity.product_group,
            CrashCollector::Product::kPlatform);
}

TEST_F(UserCollectorTest, ComputeSeverity_HandleEarlyChromeCrashes_Ash) {
  collector_.SetHandlingEarlyChromeCrashForTesting(true);

  CrashCollector::ComputedCrashSeverity computed_severity =
      collector_.ComputeSeverity("test exec name");

  EXPECT_EQ(computed_severity.crash_severity,
            CrashCollector::CrashSeverity::kFatal);
  EXPECT_EQ(computed_severity.product_group, CrashCollector::Product::kUi);
}

struct CopyStdinToCoreFileTestParams {
  std::string test_name;
  std::string input;
  std::optional<std::string> existing_file_contents;
  bool handling_early_chrome_crash;
  bool in_loose_mode;
  bool expected_result;
  // std::nullopt means we expect the file to not exist.
  std::optional<std::string> expected_file_contents;
};

// Creates a string with the indicated number of characters. Does not have a
// repeating pattern so that missed pieces can be detected.
std::string StringOfSize(int size, std::string_view flavor_text) {
  std::string result;
  // Reserve enough room that the last loop doesn't need a reallocation. The
  // worst case is that the previous loop got us to size - 1, so we append
  // flavor_text and the textual representation of an int. If int is 64-bit,
  // the largest int is 9,223,372,036,854,775,807, which is 19 digits long.
  result.reserve((size - 1) + flavor_text.size() + 19);
  while (result.size() < size) {
    base::StrAppend(&result,
                    {flavor_text, base::NumberToString(result.size())});
  }
  return result.substr(0, size);
}

class CopyStdinToCoreFileTest
    : public UserCollectorTest,
      public testing::WithParamInterface<CopyStdinToCoreFileTestParams> {
 public:
  // Generate the list of tests to run.
  static std::vector<CopyStdinToCoreFileTestParams>
  GetCopyStdinToCoreFileTestParams();

 protected:
  // Writes |param.input| to the given file descriptor. Run on a different
  // thread so that we don't deadlock trying to both read and write a pipe on
  // one thread.
  static void WriteToFileDescriptor(CopyStdinToCoreFileTestParams params,
                                    base::ScopedFD write_fd) {
    LOG(INFO) << "Writing on thread " << base::PlatformThread::CurrentId();
    // Don't CHECK on the result. For the OversizedCore test, the write may
    // fail when the read side of the pipe closes.
    if (!base::WriteFileDescriptor(write_fd.get(), params.input.c_str())) {
      PLOG(WARNING) << "base::WriteFileDescriptor failed";
    }
  }

 private:
  // Needed for base::ThreadPool::PostDelayedTask to work. Must be in
  // MULTIPLE_THREADS mode. Important that this is destructed after the
  // local variable |read_fd|, so that the read side of the pipe closes and
  // base::WriteFileDescriptor gives up before we try to join the threads.
  base::test::TaskEnvironment task_env_;
};

// static
std::vector<CopyStdinToCoreFileTestParams>
CopyStdinToCoreFileTest::GetCopyStdinToCoreFileTestParams() {
  std::string kSmallCore = "Hello I am core";

  constexpr int kHalfChromeCoreSize = UserCollector::kMaxChromeCoreSize / 2;
  const std::string kHalfSizeCore =
      StringOfSize(kHalfChromeCoreSize, "Count it up");

  const std::string kMaxSizeCore =
      StringOfSize(UserCollector::kMaxChromeCoreSize, "Take it... to the max!");

  constexpr int kOversizedChromeCoreSize =
      3 * UserCollector::kMaxChromeCoreSize / 2;
  const std::string kOversizedChromeCore =
      StringOfSize(kOversizedChromeCoreSize, "MORE!!!");

  const std::string kPreexistingFileContents = "Haha, already a file here!";

  return {
      // In non-handling_early_chrome_crash_ mode, all cores should be accepted
      // and written out.
      CopyStdinToCoreFileTestParams{/*test_name=*/"NormalSmall",
                                    /*input=*/kSmallCore,
                                    /*existing_file_contents=*/std::nullopt,
                                    /*handling_early_chrome_crash=*/false,
                                    /*in_loose_mode=*/false,
                                    /*expected_result=*/true,
                                    /*expected_file_contents=*/kSmallCore},
      CopyStdinToCoreFileTestParams{/*test_name=*/"NormalHalf",
                                    /*input=*/kHalfSizeCore,
                                    /*existing_file_contents=*/std::nullopt,
                                    /*handling_early_chrome_crash=*/false,
                                    /*in_loose_mode=*/false,
                                    /*expected_result=*/true,
                                    /*expected_file_contents=*/kHalfSizeCore},
      CopyStdinToCoreFileTestParams{/*test_name=*/"NormalMax",
                                    /*input=*/kMaxSizeCore,
                                    /*existing_file_contents=*/std::nullopt,
                                    /*handling_early_chrome_crash=*/false,
                                    /*in_loose_mode=*/false,
                                    /*expected_result=*/true,
                                    /*expected_file_contents=*/kMaxSizeCore},
      CopyStdinToCoreFileTestParams{
          /*test_name=*/"NormalOversize",
          /*input=*/kOversizedChromeCore,
          /*existing_file_contents=*/std::nullopt,
          /*handling_early_chrome_crash=*/false,
          /*in_loose_mode=*/false,
          /*expected_result=*/true,
          /*expected_file_contents=*/kOversizedChromeCore},
      // We remove the file on failure, even if it already existed, so
      // expected_file_contents is std::nullopt.
      CopyStdinToCoreFileTestParams{
          /*test_name=*/"NormalExistingFile",
          /*input=*/kSmallCore,
          /*existing_file_contents=*/kPreexistingFileContents,
          /*handling_early_chrome_crash=*/false,
          /*in_loose_mode=*/false,
          /*expected_result=*/false,
          /*expected_file_contents=*/std::nullopt},

      // In handling_early_chrome_crash_ mode, the oversized core should be
      // discarded.
      CopyStdinToCoreFileTestParams{/*test_name=*/"ChromeSmall",
                                    /*input=*/kSmallCore,
                                    /*existing_file_contents=*/std::nullopt,
                                    /*handling_early_chrome_crash=*/true,
                                    /*in_loose_mode=*/false,
                                    /*expected_result=*/true,
                                    /*expected_file_contents=*/kSmallCore},
      CopyStdinToCoreFileTestParams{/*test_name=*/"ChromeHalf",
                                    /*input=*/kHalfSizeCore,
                                    /*existing_file_contents=*/std::nullopt,
                                    /*handling_early_chrome_crash=*/true,
                                    /*in_loose_mode=*/false,
                                    /*expected_result=*/true,
                                    /*expected_file_contents=*/kHalfSizeCore},
      CopyStdinToCoreFileTestParams{/*test_name=*/"ChromeMax",
                                    /*input=*/kMaxSizeCore,
                                    /*existing_file_contents=*/std::nullopt,
                                    /*handling_early_chrome_crash=*/true,
                                    /*in_loose_mode=*/false,
                                    /*expected_result=*/true,
                                    /*expected_file_contents=*/kMaxSizeCore},
      CopyStdinToCoreFileTestParams{/*test_name=*/"ChromeOversize",
                                    /*input=*/kOversizedChromeCore,
                                    /*existing_file_contents=*/std::nullopt,
                                    /*handling_early_chrome_crash=*/true,
                                    /*in_loose_mode=*/false,
                                    /*expected_result=*/false,
                                    /*expected_file_contents=*/std::nullopt},
      CopyStdinToCoreFileTestParams{
          /*test_name=*/"ChromeExistingFile",
          /*input=*/kSmallCore,
          /*existing_file_contents=*/kPreexistingFileContents,
          /*handling_early_chrome_crash=*/true,
          /*in_loose_mode=*/false,
          /*expected_result=*/false,
          /*expected_file_contents=*/std::nullopt},

      // Loose mode tests: the oversized core should be accepted as well.
      CopyStdinToCoreFileTestParams{/*test_name=*/"ChromeLooseSmall",
                                    /*input=*/kSmallCore,
                                    /*existing_file_contents=*/std::nullopt,
                                    /*handling_early_chrome_crash=*/true,
                                    /*in_loose_mode=*/true,
                                    /*expected_result=*/true,
                                    /*expected_file_contents=*/kSmallCore},
      CopyStdinToCoreFileTestParams{
          /*test_name=*/"ChromeLooseOversize",
          /*input=*/kOversizedChromeCore,
          /*existing_file_contents=*/std::nullopt,
          /*handling_early_chrome_crash=*/true,
          /*in_loose_mode=*/true,
          /*expected_result=*/true,
          /*expected_file_contents=*/kOversizedChromeCore},
  };
}

INSTANTIATE_TEST_SUITE_P(
    CopyStdinToCoreFileTestSuite,
    CopyStdinToCoreFileTest,
    testing::ValuesIn(
        CopyStdinToCoreFileTest::GetCopyStdinToCoreFileTestParams()),
    [](const ::testing::TestParamInfo<CopyStdinToCoreFileTestParams>& info) {
      return info.param.test_name;
    });

TEST_P(CopyStdinToCoreFileTest, Test) {
  // Due to the difficulty of piping directly into stdin, we test a separate
  // function which has 99% of the code but which takes a pipe fd.
  CopyStdinToCoreFileTestParams params = GetParam();
  const base::FilePath kOutputPath = test_dir_.Append("output.txt");

  if (params.existing_file_contents) {
    ASSERT_TRUE(
        base::WriteFile(kOutputPath, params.existing_file_contents.value()));
  }

  if (params.in_loose_mode) {
    base::File::Error error;
    // Ensure util::IsReallyTestImage() returns true.
    const base::FilePath kFakeCrashReporterStateDirectory =
        paths::Get(paths::kCrashReporterStateDirectory);
    ASSERT_TRUE(base::CreateDirectoryAndGetError(
        kFakeCrashReporterStateDirectory, &error))
        << base::File::ErrorToString(error);
    const base::FilePath kLsbRelease =
        kFakeCrashReporterStateDirectory.Append(paths::kLsbRelease);
    ASSERT_TRUE(
        base::WriteFile(kLsbRelease, "CHROMEOS_RELEASE_TRACK=test-channel"));

    const base::FilePath kFakeRunStateDirectory =
        paths::Get(paths::kSystemRunStateDirectory);
    ASSERT_TRUE(
        base::CreateDirectoryAndGetError(kFakeRunStateDirectory, &error))
        << base::File::ErrorToString(error);
    const base::FilePath kLooseModeFile = kFakeRunStateDirectory.Append(
        paths::kRunningLooseChromeCrashEarlyTestFile);

    ASSERT_TRUE(test_util::CreateFile(kLooseModeFile, ""));
  }

  collector_.SetHandlingEarlyChromeCrashForTesting(
      params.handling_early_chrome_crash);

  int pipefd[2];
  ASSERT_EQ(pipe(pipefd), 0) << strerror(errno);
  base::ScopedFD read_fd(pipefd[0]);
  base::ScopedFD write_fd(pipefd[1]);

  // Spin off another thread to do the writing, to avoid deadlocks on writing
  // to the pipe.
  LOG(INFO) << "Preparing to launch write thread from thread "
            << base::PlatformThread::CurrentId();
  base::ThreadPool::PostTask(
      FROM_HERE, base::BindOnce(&CopyStdinToCoreFileTest::WriteToFileDescriptor,
                                params, std::move(write_fd)));

  LOG(INFO) << "Starting read on thread " << base::PlatformThread::CurrentId();

  EXPECT_EQ(collector_.CopyPipeToCoreFile(read_fd.get(), kOutputPath),
            params.expected_result);

  if (params.expected_file_contents) {
    std::string file_contents;
    EXPECT_TRUE(base::ReadFileToString(kOutputPath, &file_contents));
    EXPECT_EQ(file_contents, params.expected_file_contents);
  } else {
    EXPECT_FALSE(base::PathExists(kOutputPath));
  }
}

// Fixure for testing ShouldCaptureEarlyChromeCrash. Adds some extra setup
// that makes a basic fake set of /proc files, and has some extra functions
// to add other types of files.
class ShouldCaptureEarlyChromeCrashTest : public UserCollectorTest {
 protected:
  void SetUp() override {
    UserCollectorTest::SetUp();

    CHECK(base::CreateDirectory(
        paths::Get(crash_reporter::kCrashpadReadyDirectory)));
    CreateFakeProcess(kEarlyBrowserProcessID, browser_cmdline_,
                      UserCollector::kNormalCmdlineSeparator);
    CreateFakeProcess(kNormalBrowserProcessID, browser_cmdline_,
                      UserCollector::kNormalCmdlineSeparator);
    TouchCrashpadReadyFile(kNormalBrowserProcessID);
    CreateFakeProcess(
        kNormalRendererProcessID,
        {"/opt/google/chrome/chrome", "--log-level=1", "--enable-crashpad",
         "--crashpad-handler-pid=402", "--type=renderer"},
        UserCollector::kChromeSubprocessCmdlineSeparator);
    CreateFakeProcess(
        kCrashpadProcessID,
        {"/opt/google/chrome/chrome_crashpad_handler", "--monitor-self",
         "--database=/var/log/chrome/Crash Reports"
         "--annotation=channel=unknown"},
        UserCollector::kNormalCmdlineSeparator);
    CreateFakeProcess(kShillProcessID, {"/usr/bin/shill", "--log-level=0"},
                      UserCollector::kNormalCmdlineSeparator);
  }

  void TouchCrashpadReadyFile(pid_t pid) {
    CHECK(base::WriteFile(paths::Get(crash_reporter::kCrashpadReadyDirectory)
                              .Append(base::NumberToString(pid)),
                          ""));
  }

  base::FilePath GetProcessPath(pid_t pid) {
    return test_dir_.Append("proc").Append(base::NumberToString(pid));
  }

  // Given the argv cmdline that started a process, return the name that will
  // appear in /proc/pid/stat and /proc/pid/status.
  static std::string ProcNameFromCmdline(
      const std::vector<std::string>& cmdline) {
    CHECK(!cmdline.empty());
    base::FilePath exec_path(cmdline[0]);
    return exec_path.BaseName().value().substr(0, 15);
  }

  // Creates a fake /proc/|pid|/cmdline record of a process inside
  // test_dir_.Append("proc"). CHECK-fails on failure.
  void CreateFakeProcess(pid_t pid,
                         const std::vector<std::string>& cmdline,
                         char cmdline_separator) {
    base::FilePath proc = GetProcessPath(pid);
    base::File::Error error;
    CHECK(base::CreateDirectoryAndGetError(proc, &error))
        << ": " << base::File::ErrorToString(error);

    base::File cmdline_file(proc.Append("cmdline"),
                            base::File::FLAG_CREATE | base::File::FLAG_WRITE);
    CHECK(cmdline_file.IsValid())
        << ": " << base::File::ErrorToString(cmdline_file.error_details());

    for (const std::string& arg : cmdline) {
      CHECK_EQ(cmdline_file.WriteAtCurrentPos(arg.c_str(), arg.length()),
               arg.length());
      CHECK_EQ(cmdline_file.WriteAtCurrentPos(&cmdline_separator, 1), 1);
    }
    // Both Chrome and normal processes end with an extra \0.
    const char kNulByte = '\0';
    CHECK_EQ(cmdline_file.WriteAtCurrentPos(&kNulByte, 1), 1);
  }

  // The fake pid of the browser process which is still in early startup.
  static constexpr pid_t kEarlyBrowserProcessID = 100;
  // The fake pid of a different browser process which has initialized crashpad.
  static constexpr pid_t kNormalBrowserProcessID = 400;
  // The fake pid of the renderer process, which is the child of the 'normal'
  // browser.
  static constexpr pid_t kNormalRendererProcessID = 401;
  // The crashpad process that's a child of kNormalBrowserProcessID.
  static constexpr pid_t kCrashpadProcessID = 402;
  // The fake pid of a shill process which has nothing to do with Chrome
  static constexpr pid_t kShillProcessID = 501;

  // The commandline we use for all the browser processes. The tests give all
  // our browser processes the same commandline so that the difference in test
  // results is purely because of the /run/crash_reporter/crashpad_ready/ state.
  const std::vector<std::string> browser_cmdline_ = {
      "/opt/google/chrome/chrome", "--use-gl=egl", "--log-level=1",
      "--enable-crashpad", "--login-manager"};
};

TEST_F(ShouldCaptureEarlyChromeCrashTest, BasicTrue) {
  EXPECT_TRUE(collector_.ShouldCaptureEarlyChromeCrash("chrome",
                                                       kEarlyBrowserProcessID));
  EXPECT_TRUE(collector_.ShouldCaptureEarlyChromeCrash("supplied_chrome",
                                                       kEarlyBrowserProcessID));
}

TEST_F(ShouldCaptureEarlyChromeCrashTest, FalseIfCrashpadReady) {
  EXPECT_FALSE(collector_.ShouldCaptureEarlyChromeCrash(
      "chrome", kNormalBrowserProcessID));
}

TEST_F(ShouldCaptureEarlyChromeCrashTest, FalseIfRenderer) {
  EXPECT_FALSE(collector_.ShouldCaptureEarlyChromeCrash(
      "chrome", kNormalRendererProcessID));
}

TEST_F(ShouldCaptureEarlyChromeCrashTest, FalseIfNotChrome) {
  EXPECT_FALSE(
      collector_.ShouldCaptureEarlyChromeCrash("nacl", kEarlyBrowserProcessID));
  EXPECT_FALSE(collector_.ShouldCaptureEarlyChromeCrash(
      "chrome_crashpad_handler", kCrashpadProcessID));
  EXPECT_FALSE(collector_.ShouldCaptureEarlyChromeCrash(
      "supplied_chrome_crashpad", kCrashpadProcessID));
  EXPECT_FALSE(
      collector_.ShouldCaptureEarlyChromeCrash("shill", kShillProcessID));
}

TEST_F(ShouldCaptureEarlyChromeCrashTest, SetsUpForEarlyChromeCrashes) {
  collector_.BeginHandlingCrash(kEarlyBrowserProcessID, "chrome",
                                paths::Get("/opt/google/chrome"));

  // Ignored but we need something for ShouldDump().
  constexpr uid_t kUserUid = 1000;

  // We should be in early-chrome-crash mode, so ShouldDump should return
  // base::ok ("dump this crash") even for a chrome executable.
  EXPECT_EQ(collector_.ShouldDump(kEarlyBrowserProcessID, kUserUid, "chrome"),
            base::ok());

  EXPECT_THAT(collector_.get_extra_metadata_for_test(),
              AllOf(HasSubstr("upload_var_prod=Chrome_ChromeOS\n"),
                    HasSubstr("upload_var_early_chrome_crash=true\n"),
                    HasSubstr("upload_var_ptype=browser\n")));
}

TEST_F(ShouldCaptureEarlyChromeCrashTest, IgnoresNonEarlyBrowser) {
  collector_.BeginHandlingCrash(kNormalBrowserProcessID, "chrome",
                                paths::Get("/opt/google/chrome"));

  // Ignored but we need something for ShouldDump().
  constexpr uid_t kUserUid = 1000;

  EXPECT_EQ(
      collector_.ShouldDump(kNormalBrowserProcessID, kUserUid, "chrome"),
      base::unexpected(CrashCollectionStatus::kChromeCrashInUserCollector));

  EXPECT_THAT(collector_.get_extra_metadata_for_test(),
              AllOf(Not(HasSubstr("upload_var_prod=Chrome_ChromeOS\n")),
                    Not(HasSubstr("upload_var_early_chrome_crash=true\n")),
                    Not(HasSubstr("upload_var_ptype=browser\n"))));
}

TEST_F(ShouldCaptureEarlyChromeCrashTest, NoEffectIfNotChrome) {
  collector_.BeginHandlingCrash(kShillProcessID, "shill",
                                paths::Get("/usr/bin"));

  EXPECT_EQ(
      collector_.ShouldDump(kShillProcessID, constants::kRootUid, "shill"),
      base::ok());

  EXPECT_THAT(collector_.get_extra_metadata_for_test(),
              AllOf(Not(HasSubstr("upload_var_prod=Chrome_ChromeOS\n")),
                    Not(HasSubstr("upload_var_early_chrome_crash=true\n")),
                    Not(HasSubstr("upload_var_ptype=browser\n"))));
}
