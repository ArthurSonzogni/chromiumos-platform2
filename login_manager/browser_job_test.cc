// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/browser_job.h"

#include <stdint.h>
#include <unistd.h>

#include <algorithm>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <base/command_line.h>
#include <base/functional/callback_helpers.h>
#include <base/logging.h>
#include <base/strings/string_util.h>
#include <brillo/process/process_reaper.h>
#include <chromeos/switches/chrome_switches.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "login_manager/mock_metrics.h"
#include "login_manager/mock_subprocess.h"
#include "login_manager/mock_system_utils.h"
#include "login_manager/subprocess.h"

namespace login_manager {

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::AnyOf;
using ::testing::Contains;
using ::testing::DoAll;
using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::testing::InSequence;
using ::testing::IsSupersetOf;
using ::testing::Mock;
using ::testing::Not;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::StrEq;

class BrowserJobTest : public ::testing::Test {
 public:
  BrowserJobTest() {}
  BrowserJobTest(const BrowserJobTest&) = delete;
  BrowserJobTest& operator=(const BrowserJobTest&) = delete;

  ~BrowserJobTest() override {}

  void SetUp() override;

 protected:
  static const char* kArgv[];
  static const char kUser[];
  static const char kHash[];
  static const char kChromeMountNamespacePath[];

  void ExpectArgsToContainFlag(const std::vector<std::string>& argv,
                               const char name[],
                               const char value[]) {
    EXPECT_THAT(argv, Contains(base::StringPrintf("%s%s", name, value)));
  }

  void ExpectArgsNotToContainFlag(const std::vector<std::string>& argv,
                                  const char name[],
                                  const char value[]) {
    EXPECT_THAT(argv, Not(Contains(base::StringPrintf("%s%s", name, value))));
  }

  void ExpectArgsToContainAll(const std::vector<std::string>& argv,
                              const std::vector<std::string>& contained) {
    for (auto it = contained.begin(); it != contained.end(); ++it) {
      EXPECT_THAT(argv, Contains(*it));
    }
  }

  std::vector<std::string> argv_;
  std::vector<std::string> env_;
  MockMetrics metrics_;
  MockSystemUtils system_utils_;
  brillo::ProcessReaper process_reaper_;
  std::unique_ptr<BrowserJob> job_;
};

// Default argument list for a job to use in mostly all test cases.
const char* BrowserJobTest::kArgv[] = {"zero", "one", "two"};

// Normal username to test session for.
const char BrowserJobTest::kUser[] = "test@gmail.com";
const char BrowserJobTest::kHash[] = "fake_hash";

// User session mount namespace for testing. Does not need to be an actual file.
const char BrowserJobTest::kChromeMountNamespacePath[] = "mnt_chrome";

void BrowserJobTest::SetUp() {
  argv_.assign(std::begin(kArgv), std::end(kArgv));
  job_.reset(new BrowserJob(
      argv_, env_, process_reaper_, &metrics_, &system_utils_,
      BrowserJob::Config{false, false, std::nullopt},
      std::make_unique<login_manager::Subprocess>(getuid(), &system_utils_)));
}

TEST_F(BrowserJobTest, InitializationTest) {
  EXPECT_FALSE(job_->removed_login_manager_flag_);
  std::vector<std::string> job_args = job_->ExportArgv();
  ASSERT_EQ(argv_.size(), job_args.size());
  ExpectArgsToContainAll(job_args, argv_);
}

TEST_F(BrowserJobTest, AbortAndKillAll) {
  const gid_t kFakeGid = 1000;
  const pid_t kFakePid = 4;
  EXPECT_CALL(system_utils_, GetGidAndGroups(getuid(), _, _))
      .WillOnce(DoAll(SetArgPointee<1>(kFakeGid), Return(true)));
  EXPECT_CALL(system_utils_, RunInMinijail(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<3>(kFakePid), Return(true)));
  EXPECT_CALL(system_utils_, ProcessGroupIsGone(kFakePid, _))
      .Times(3)
      .WillRepeatedly(Return(false));
  EXPECT_CALL(system_utils_, ProcessIsGone(kFakePid, _))
      .WillOnce(Return(false));
  EXPECT_CALL(system_utils_, kill(kFakePid, _, SIGABRT)).Times(1);
  EXPECT_CALL(system_utils_, kill(-kFakePid, _, SIGKILL)).Times(1);
  EXPECT_CALL(system_utils_, time(nullptr)).WillRepeatedly(Return(0));

  EXPECT_CALL(metrics_, HasRecordedChromeExec()).WillRepeatedly(Return(false));
  EXPECT_CALL(metrics_, RecordStats(_)).Times(AnyNumber());

  ASSERT_TRUE(job_->RunInBackground(base::DoNothing()));
  job_->AbortAndKillAll(base::Seconds(3));
}

TEST_F(BrowserJobTest, AbortAndKillAll_AlreadyGone) {
  const gid_t kFakeGid = 1000;
  const pid_t kFakePid = 4;
  EXPECT_CALL(system_utils_, GetGidAndGroups(getuid(), _, _))
      .WillOnce(DoAll(SetArgPointee<1>(kFakeGid), Return(true)));
  EXPECT_CALL(system_utils_, RunInMinijail(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<3>(kFakePid), Return(true)));
  EXPECT_CALL(system_utils_, ProcessGroupIsGone(kFakePid, _))
      .WillOnce(Return(true));
  EXPECT_CALL(system_utils_, ProcessIsGone(kFakePid, _)).Times(0);
  EXPECT_CALL(system_utils_, kill(_, _, _)).Times(0);
  EXPECT_CALL(system_utils_, time(nullptr)).WillRepeatedly(Return(0));

  EXPECT_CALL(metrics_, HasRecordedChromeExec()).WillRepeatedly(Return(false));
  EXPECT_CALL(metrics_, RecordStats(_)).Times(AnyNumber());

  ASSERT_TRUE(job_->RunInBackground(base::DoNothing()));
  job_->AbortAndKillAll(base::Seconds(3));
}

TEST_F(BrowserJobTest, AbortAndKillAll_BrowserGoneChildrenLive) {
  const gid_t kFakeGid = 1000;
  const pid_t kFakePid = 4;
  EXPECT_CALL(system_utils_, GetGidAndGroups(getuid(), _, _))
      .WillOnce(DoAll(SetArgPointee<1>(kFakeGid), Return(true)));
  EXPECT_CALL(system_utils_, RunInMinijail(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<3>(kFakePid), Return(true)));
  EXPECT_CALL(system_utils_, ProcessGroupIsGone(kFakePid, _))
      .Times(2)
      .WillRepeatedly(Return(false));
  EXPECT_CALL(system_utils_, ProcessIsGone(kFakePid, _)).WillOnce(Return(true));
  EXPECT_CALL(system_utils_, kill(kFakePid, _, _)).Times(0);
  EXPECT_CALL(system_utils_, kill(-kFakePid, _, SIGKILL)).Times(1);
  EXPECT_CALL(system_utils_, time(nullptr)).WillRepeatedly(Return(0));

  EXPECT_CALL(metrics_, HasRecordedChromeExec()).WillRepeatedly(Return(false));
  EXPECT_CALL(metrics_, RecordStats(_)).Times(AnyNumber());

  ASSERT_TRUE(job_->RunInBackground(base::DoNothing()));
  job_->AbortAndKillAll(base::Seconds(3));
}

TEST_F(BrowserJobTest, UnshareMountNamespaceForGuest) {
  MockSubprocess* mock_subp = new MockSubprocess();
  EXPECT_CALL(*mock_subp, UseNewMountNamespace());
  EXPECT_CALL(*mock_subp, ForkAndExec(_, _)).WillOnce(Return(true));

  EXPECT_CALL(system_utils_, time(nullptr)).WillRepeatedly(Return(0));

  EXPECT_CALL(metrics_, HasRecordedChromeExec()).WillRepeatedly(Return(false));
  EXPECT_CALL(metrics_, RecordStats(_)).Times(AnyNumber());

  std::unique_ptr<SubprocessInterface> p_subp(mock_subp);

  std::vector<std::string> argv{"zero", "one", "two",
                                BrowserJobInterface::kGuestSessionFlag};

  BrowserJob job(
      argv, env_, process_reaper_, &metrics_, &system_utils_,
      BrowserJob::Config{false /*isolate_guest_session*/,
                         false /*isolate_regular_session*/, std::nullopt},
      std::move(p_subp));

  ASSERT_TRUE(job.RunInBackground(base::DoNothing()));
}

TEST_F(BrowserJobTest, EnterMountNamespaceForGuest) {
  MockSubprocess* mock_subp = new MockSubprocess();
  EXPECT_CALL(*mock_subp, EnterExistingMountNamespace(base::FilePath(
                              BrowserJobTest::kChromeMountNamespacePath)));
  EXPECT_CALL(*mock_subp, ForkAndExec(_, _)).WillOnce(Return(true));

  EXPECT_CALL(system_utils_, time(nullptr)).WillRepeatedly(Return(0));

  EXPECT_CALL(metrics_, HasRecordedChromeExec()).WillRepeatedly(Return(false));
  EXPECT_CALL(metrics_, RecordStats(_)).Times(AnyNumber());

  std::unique_ptr<SubprocessInterface> p_subp(mock_subp);

  std::vector<std::string> argv{"zero", "one", "two",
                                BrowserJobInterface::kGuestSessionFlag};

  BrowserJob job(
      argv, env_, process_reaper_, &metrics_, &system_utils_,
      BrowserJob::Config{true /*isolate_guest_session*/,
                         false /*isolate_regular_session*/,
                         std::optional<base::FilePath>(
                             BrowserJobTest::kChromeMountNamespacePath)},
      std::move(p_subp));

  ASSERT_TRUE(job.RunInBackground(base::DoNothing()));
}

TEST_F(BrowserJobTest, EnterMountNamespaceForRegularUser) {
  MockSubprocess* mock_subp = new MockSubprocess();
  EXPECT_CALL(*mock_subp, EnterExistingMountNamespace(base::FilePath(
                              BrowserJobTest::kChromeMountNamespacePath)));
  EXPECT_CALL(*mock_subp, ForkAndExec(_, _)).WillOnce(Return(true));

  EXPECT_CALL(system_utils_, time(nullptr)).WillRepeatedly(Return(0));

  EXPECT_CALL(metrics_, HasRecordedChromeExec()).WillRepeatedly(Return(false));
  EXPECT_CALL(metrics_, RecordStats(_)).Times(AnyNumber());

  std::unique_ptr<SubprocessInterface> p_subp(mock_subp);

  BrowserJob job(
      argv_, env_, process_reaper_, &metrics_, &system_utils_,
      BrowserJob::Config{true /*isolate_guest_session*/,
                         true /*isolate_regular_session*/,
                         std::optional<base::FilePath>(
                             BrowserJobTest::kChromeMountNamespacePath)},
      std::move(p_subp));

  ASSERT_TRUE(job.RunInBackground(base::DoNothing()));
}

TEST_F(BrowserJobTest, ShouldStopTest) {
  EXPECT_CALL(system_utils_, time(nullptr))
      .WillRepeatedly(Return(BrowserJob::kRestartWindowSeconds));
  for (int i = 0; i < BrowserJob::kRestartTries - 1; ++i) {
    job_->RecordTime();
  }
  // We haven't yet saturated the list of start times, so...
  EXPECT_FALSE(job_->ShouldStop());

  // Go ahead and saturate.
  job_->RecordTime();
  EXPECT_NE(0, job_->start_times_.front());
  EXPECT_TRUE(job_->ShouldStop());
}

TEST_F(BrowserJobTest, ShouldNotStopTest) {
  EXPECT_CALL(system_utils_, time(nullptr))
      .WillOnce(Return(BrowserJob::kRestartWindowSeconds))
      .WillOnce(Return(3 * BrowserJob::kRestartWindowSeconds));
  job_->RecordTime();
  EXPECT_FALSE(job_->ShouldStop());
}

TEST_F(BrowserJobTest, ShouldDropExtraArgumentsTest) {
  EXPECT_CALL(system_utils_, time(nullptr))
      .WillRepeatedly(Return(BrowserJob::kRestartWindowSeconds));

  // Simulate restart kUseExtraArgsRuns - 1 times and no dropping.
  for (int i = 0; i < BrowserJob::kUseExtraArgsRuns - 1; ++i) {
    job_->RecordTime();
  }
  EXPECT_FALSE(job_->ShouldDropExtraArguments());

  // One more restart and extra args should be dropped.
  job_->RecordTime();
  EXPECT_TRUE(job_->ShouldDropExtraArguments());
}

TEST_F(BrowserJobTest, ShouldAddCrashLoopArgBeforeStopping) {
  const gid_t kFakeGid = 1000;
  const pid_t kFakePid = 4;
  EXPECT_CALL(system_utils_, GetGidAndGroups(getuid(), _, _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(kFakeGid), Return(true)));
  EXPECT_CALL(system_utils_, RunInMinijail(_, _, _, _))
      .WillRepeatedly(DoAll(SetArgPointee<3>(kFakePid), Return(true)));
  EXPECT_CALL(system_utils_, time(nullptr))
      .WillRepeatedly(Return(BrowserJob::kRestartWindowSeconds + 1));
  for (int i = 0; i < BrowserJob::kRestartTries - 1; ++i) {
    EXPECT_FALSE(job_->ShouldStop());
    EXPECT_TRUE(job_->RunInBackground(base::DoNothing()));
    EXPECT_THAT(
        job_->ExportArgv(),
        Not(Contains(HasSubstr(BrowserJobInterface::kCrashLoopBeforeFlag))));
    job_->AbortAndKillAll(base::Seconds(0));
  }

  EXPECT_FALSE(job_->ShouldStop());
  EXPECT_TRUE(job_->RunInBackground(base::DoNothing()));
  // 201 = 101 (the time system_utils_.time(nullptr) is returning) + 100
  // (kRestartWindowSeconds).
  ASSERT_EQ(BrowserJob::kRestartWindowSeconds, 100)
      << "Need to change expected value if kRestartWindowSeconds changes";
  ExpectArgsToContainFlag(job_->ExportArgv(),
                          BrowserJobInterface::kCrashLoopBeforeFlag, "201");
  job_->AbortAndKillAll(base::Seconds(0));
  EXPECT_TRUE(job_->ShouldStop());
}

// On the job's first run, it should have a one-time-flag.  That
// should get cleared and not used again.
TEST_F(BrowserJobTest, OneTimeBootFlags) {
  const gid_t kFakeGid = 1000;
  const pid_t kFakePid = 4;
  EXPECT_CALL(system_utils_, GetGidAndGroups(getuid(), _, _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(kFakeGid), Return(true)));
  EXPECT_CALL(system_utils_, RunInMinijail(_, _, _, _))
      .WillRepeatedly(DoAll(SetArgPointee<3>(kFakePid), Return(true)));
  EXPECT_CALL(system_utils_, time(nullptr)).WillRepeatedly(Return(0));

  EXPECT_CALL(metrics_, HasRecordedChromeExec())
      .WillOnce(Return(false))
      .WillOnce(Return(true));
  EXPECT_CALL(metrics_, RecordStats(StrEq(("chrome-exec")))).Times(2);

  ASSERT_TRUE(job_->RunInBackground(base::DoNothing()));
  ExpectArgsToContainFlag(job_->ExportArgv(),
                          BrowserJob::kFirstExecAfterBootFlag, "");

  ASSERT_TRUE(job_->RunInBackground(base::DoNothing()));
  ExpectArgsNotToContainFlag(job_->ExportArgv(),
                             BrowserJob::kFirstExecAfterBootFlag, "");
}

TEST_F(BrowserJobTest, RunBrowserTermMessage) {
  const gid_t kFakeGid = 1000;
  const pid_t kFakePid = 4;
  int signal = SIGKILL;
  EXPECT_CALL(system_utils_, GetGidAndGroups(getuid(), _, _))
      .WillOnce(DoAll(SetArgPointee<1>(kFakeGid), Return(true)));
  EXPECT_CALL(system_utils_, RunInMinijail(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<3>(kFakePid), Return(true)));
  EXPECT_CALL(system_utils_, kill(kFakePid, _, signal)).Times(1);
  EXPECT_CALL(system_utils_, time(nullptr)).WillRepeatedly(Return(0));

  EXPECT_CALL(metrics_, HasRecordedChromeExec()).WillRepeatedly(Return(false));
  EXPECT_CALL(metrics_, RecordStats(_)).Times(AnyNumber());

  std::string term_message("killdya");
  ASSERT_TRUE(job_->RunInBackground(base::DoNothing()));
  job_->Kill(signal, term_message);
}

TEST_F(BrowserJobTest, StartStopSessionTest) {
  job_->StartSession(kUser, kHash);

  std::vector<std::string> job_args = job_->ExportArgv();
  ASSERT_LT(argv_.size(), job_args.size());
  ExpectArgsToContainAll(job_args, argv_);
  ExpectArgsToContainFlag(job_args, BrowserJob::kLoginUserFlag, kUser);
  ExpectArgsToContainFlag(job_args, BrowserJob::kLoginProfileFlag, kHash);

  // Should remove login user flag.
  job_->StopSession();
  job_args = job_->ExportArgv();
  ASSERT_EQ(argv_.size(), job_args.size());
  ExpectArgsToContainAll(job_args, argv_);
}

TEST_F(BrowserJobTest, StartStopMultiSessionTest) {
  BrowserJob job(
      argv_, env_, process_reaper_, &metrics_, &system_utils_,
      BrowserJob::Config{false, false, std::nullopt},
      std::make_unique<login_manager::Subprocess>(1, &system_utils_));
  job.StartSession(kUser, kHash);

  std::vector<std::string> job_args = job.ExportArgv();
  ASSERT_EQ(argv_.size() + 2, job_args.size());
  ExpectArgsToContainAll(job_args, argv_);
  ExpectArgsToContainFlag(job_args, BrowserJob::kLoginUserFlag, kUser);
  ExpectArgsToContainFlag(job_args, BrowserJob::kLoginProfileFlag, kHash);

  // Start another session, expect the args to be unchanged.
  job.StartSession(kUser, kHash);
  job_args = job.ExportArgv();
  ASSERT_EQ(argv_.size() + 2, job_args.size());
  ExpectArgsToContainAll(job_args, argv_);
  ExpectArgsToContainFlag(job_args, BrowserJob::kLoginUserFlag, kUser);
  ExpectArgsToContainFlag(job_args, BrowserJob::kLoginProfileFlag, kHash);

  // Should remove login user and login profile flags.
  job.StopSession();
  job_args = job.ExportArgv();
  ASSERT_EQ(argv_.size(), job_args.size());
  ExpectArgsToContainAll(job_args, argv_);
}

TEST_F(BrowserJobTest, StartStopSessionFromLoginTest) {
  std::vector<std::string> argv = {"zero", "one", "two", "--login-manager"};
  BrowserJob job(
      argv, env_, process_reaper_, &metrics_, &system_utils_,
      BrowserJob::Config{false, false, std::nullopt},
      std::make_unique<login_manager::Subprocess>(1, &system_utils_));

  job.StartSession(kUser, kHash);

  std::vector<std::string> job_args = job.ExportArgv();
  ASSERT_EQ(argv.size() + 1, job_args.size());
  ExpectArgsToContainAll(
      job_args, std::vector<std::string>(argv.begin(), argv.end() - 1));
  ExpectArgsToContainFlag(job_args, BrowserJob::kLoginUserFlag, kUser);

  // Should remove login user/hash flags and append --login-manager flag back.
  job.StopSession();
  job_args = job.ExportArgv();
  ASSERT_EQ(argv.size(), job_args.size());
  ExpectArgsToContainAll(job_args, argv);
}

TEST_F(BrowserJobTest, SetArguments) {
  std::vector<std::string> new_args = {"--ichi", "--ni dfs", "--san"};
  job_->SetArguments(new_args);

  std::vector<std::string> job_args = job_->ExportArgv();
  ASSERT_EQ(new_args.size(), job_args.size());
  EXPECT_EQ(kArgv[0], job_args[0]);
  for (size_t i = 1; i < new_args.size(); ++i) {
    EXPECT_EQ(new_args[i], job_args[i]);
  }

  job_->StartSession(kUser, kHash);
  job_args = job_->ExportArgv();
  ExpectArgsToContainFlag(job_args, BrowserJob::kLoginUserFlag, kUser);
}

TEST_F(BrowserJobTest, SetExtraArguments) {
  std::vector<std::string> safe_args = {"--ichi", "--ni", "--san"};
  std::vector<std::string> unsafe_args = {"--no-sandbox", "-no-sandbox"};
  std::vector<std::string> extra_args = safe_args;
  extra_args.insert(extra_args.end(), unsafe_args.begin(), unsafe_args.end());
  job_->SetExtraArguments(extra_args);

  std::vector<std::string> job_args = job_->ExportArgv();
  ExpectArgsToContainAll(job_args, argv_);
  ExpectArgsToContainAll(job_args, safe_args);
  for (const std::string& unsafe : unsafe_args) {
    EXPECT_THAT(job_args, Not(Contains(unsafe)));
  }
}

TEST_F(BrowserJobTest, SetTestArguments) {
  const char* kTestArgs[] = {"--test", "--it", "--all"};
  std::vector<std::string> test_args(std::begin(kTestArgs),
                                     std::end(kTestArgs));
  job_->SetTestArguments(test_args);

  std::vector<std::string> job_args = job_->ExportArgv();
  ExpectArgsToContainAll(job_args, argv_);
  ExpectArgsToContainAll(job_args, test_args);
}

TEST_F(BrowserJobTest, SetTestArgumentsAndSetExtraArgumentsDontConflict) {
  const char* kTestArgs[] = {"--test", "--it", "--all"};
  std::vector<std::string> test_args(std::begin(kTestArgs),
                                     std::end(kTestArgs));
  job_->SetTestArguments(test_args);

  const char* kExtraArgs[] = {"--ichi", "--ni", "--san"};
  std::vector<std::string> extra_args(std::begin(kExtraArgs),
                                      std::end(kExtraArgs));
  job_->SetExtraArguments(extra_args);

  std::vector<std::string> job_args = job_->ExportArgv();
  ExpectArgsToContainAll(job_args, argv_);
  ExpectArgsToContainAll(job_args, test_args);
  ExpectArgsToContainAll(job_args, extra_args);

  const char* kNewTestArgs[] = {"--debugging=sucks", "--testing=rocks"};
  std::vector<std::string> new_test_args(std::begin(kNewTestArgs),
                                         std::end(kNewTestArgs));
  job_->SetTestArguments(new_test_args);
  job_args = job_->ExportArgv();
  ExpectArgsToContainAll(job_args, argv_);
  ExpectArgsToContainAll(job_args, new_test_args);
  ExpectArgsToContainAll(job_args, extra_args);
  EXPECT_THAT(job_args, Not(IsSupersetOf(test_args)));

  const char* kNewExtraArgs[] = {"--uno", "--dos"};
  std::vector<std::string> new_extra_args(std::begin(kNewExtraArgs),
                                          std::end(kNewExtraArgs));
  job_->SetExtraArguments(new_extra_args);
  job_args = job_->ExportArgv();
  ExpectArgsToContainAll(job_args, argv_);
  ExpectArgsToContainAll(job_args, new_test_args);
  ExpectArgsToContainAll(job_args, new_extra_args);
  EXPECT_THAT(job_args, Not(IsSupersetOf(extra_args)));
}

TEST_F(BrowserJobTest, FeatureFlags) {
  job_->SetFeatureFlags({"one", "two", "three"}, {{"name", "value"}});
  std::vector<std::string> job_args = job_->ExportArgv();
  // Also verify that the elements appear in alphabetical order.
  EXPECT_THAT(job_args,
              Contains(base::StringPrintf("--%s=[\"one\",\"three\",\"two\"]",
                                          chromeos::switches::kFeatureFlags)));
  EXPECT_THAT(job_args, Contains(base::StringPrintf(
                            "--%s={\"name\":\"value\"}",
                            chromeos::switches::kFeatureFlagsOriginList)));
}

TEST_F(BrowserJobTest, ExportArgv) {
  std::vector<std::string> argv(std::begin(kArgv), std::end(kArgv));
  BrowserJob job(
      argv, env_, process_reaper_, &metrics_, &system_utils_,
      BrowserJob::Config{false, false, std::nullopt},
      std::make_unique<login_manager::Subprocess>(1, &system_utils_));

  std::vector<std::string> extra_args = {"--ichi", "--ni", "--san"};
  argv.insert(argv.end(), extra_args.begin(), extra_args.end());
  job.SetExtraArguments(extra_args);
  EXPECT_EQ(argv, job.ExportArgv());
}

TEST_F(BrowserJobTest, SetAdditionalEnvironmentVariables) {
  std::vector<std::string> argv(std::begin(kArgv), std::end(kArgv));
  BrowserJob job(
      argv, {"A=a"}, process_reaper_, &metrics_, &system_utils_,
      BrowserJob::Config{false, false, std::nullopt},
      std::make_unique<login_manager::Subprocess>(1, &system_utils_));
  job.SetAdditionalEnvironmentVariables({"B=b", "C="});
  EXPECT_EQ((std::vector<std::string>{"A=a", "B=b", "C="}),
            job.ExportEnvironmentVariables());
}

TEST_F(BrowserJobTest, CombineVModuleArgs) {
  const char* kArg1 = "--first";
  const char* kArg2 = "--second_arg=blah";
  const char* kArg3 = "--third_arg=5";
  const char* kArg4 = "--last_arg";

  {
    // A testcase with 3 --vmodule flags.
    const char* kVmodule1 = "--vmodule=file1=1,file2=2";
    const char* kVmodule2 = "--vmodule=file3=3,file4=4,file5=5";
    const char* kVmodule3 = "--vmodule=file6=6";

    std::vector<std::string> argv = {kArg1,     kVmodule1, kArg2, kArg3,
                                     kVmodule2, kVmodule3, kArg4};
    BrowserJob job(
        argv, env_, process_reaper_, &metrics_, &system_utils_,
        BrowserJob::Config{false, false, std::nullopt},
        std::make_unique<login_manager::Subprocess>(1, &system_utils_));

    const char* kCombinedVmodule =
        "--vmodule=file1=1,file2=2,file3=3,file4=4,file5=5,file6=6";

    EXPECT_THAT(job.ExportArgv(),
                ElementsAre(kArg1, kArg2, kArg3, kArg4, kCombinedVmodule));
  }

  {
    // A testcase with 1 --vmodule flag.
    const char* kVmodule = "--vmodule=my_file=1";

    std::vector<std::string> argv = {kArg1, kVmodule, kArg2, kArg3, kArg4};
    BrowserJob job(
        argv, env_, process_reaper_, &metrics_, &system_utils_,
        BrowserJob::Config{false, false, std::nullopt},
        std::make_unique<login_manager::Subprocess>(1, &system_utils_));

    EXPECT_THAT(job.ExportArgv(),
                ElementsAre(kArg1, kArg2, kArg3, kArg4, kVmodule));
  }

  {
    // A testcase with no --vmodule flag.
    std::vector<std::string> argv = {kArg1, kArg2, kArg3, kArg4};

    BrowserJob job(
        argv, env_, process_reaper_, &metrics_, &system_utils_,
        BrowserJob::Config{false, false, std::nullopt},
        std::make_unique<login_manager::Subprocess>(1, &system_utils_));

    EXPECT_THAT(job.ExportArgv(), ElementsAre(kArg1, kArg2, kArg3, kArg4));
  }
}

TEST_F(BrowserJobTest, CombineFeatureArgs) {
  constexpr const char kArg1[] = "--first";
  constexpr const char kArg2[] = "--second";

  constexpr const char kEnable1[] = "--enable-features=1a,1b,4a";
  constexpr const char kEnable2[] = "--enable-features=2a,2b,5a";
  constexpr const char kEnable3[] = "--enable-features=3a,3b,6a";
  constexpr const char kCombinedEnable[] =
      "--enable-features=1a,1b,2a,2b,3a,3b";

  constexpr const char kDisable1[] = "--disable-features=4a,4b,2a";
  constexpr const char kDisable2[] = "--disable-features=5a,5b,3a";
  constexpr const char kDisable3[] = "--disable-features=6a,6b";
  constexpr const char kCombinedDisable[] =
      "--disable-features=4a,4b,5a,5b,6a,6b";

  constexpr const char kBlinkEnable1[] = "--enable-blink-features=7a,7b,10a";
  constexpr const char kBlinkEnable2[] = "--enable-blink-features=8a,8b,11a";
  constexpr const char kBlinkEnable3[] = "--enable-blink-features=9a,9b,12a";
  constexpr const char kCombinedBlinkEnable[] =
      "--enable-blink-features=7a,7b,8a,8b,9a,9b";

  constexpr const char kBlinkDisable1[] = "--disable-blink-features=10a,10b,8a";
  constexpr const char kBlinkDisable2[] = "--disable-blink-features=11a,11b,9a";
  constexpr const char kBlinkDisable3[] = "--disable-blink-features=12a,12b";
  constexpr const char kCombinedBlinkDisable[] =
      "--disable-blink-features=10a,10b,11a,11b,12a,12b";

  const std::vector<std::string> kArgv = {
      kEnable1, kDisable1, kBlinkEnable1, kBlinkDisable1, kArg1,
      kEnable2, kDisable2, kBlinkEnable2, kBlinkDisable2, kArg2,
      kEnable3, kDisable3, kBlinkEnable3, kBlinkDisable3,
  };
  BrowserJob job(
      kArgv, env_, process_reaper_, &metrics_, &system_utils_,
      BrowserJob::Config{false, false, std::nullopt},
      std::make_unique<login_manager::Subprocess>(1, &system_utils_));

  // --enable-features and --disable-features should be merged into args at the
  // end of the command line, but the original args should be preserved:
  // https://crbug.com/767266
  //
  // --enable-blink-features and --disable-blink-features should also be merged,
  // but the original args don't need to be preserved in that case (since
  // sentinel args aren't placed around them).
  const std::vector<std::string> kExpected = {
      kEnable1,
      kDisable1,
      kArg1,
      kEnable2,
      kDisable2,
      kArg2,
      kEnable3,
      kDisable3,
      kCombinedEnable,
      kCombinedDisable,
      kCombinedBlinkEnable,
      kCombinedBlinkDisable,
  };
  EXPECT_EQ(base::JoinString(job.ExportArgv(), " "),
            base::JoinString(kExpected, " "));
}

TEST_F(BrowserJobTest, CombineFeatureArgs_ResolveToEmpty) {
  constexpr const char kArg1[] = "--first";
  constexpr const char kArg2[] = "--second";

  constexpr const char kEnable[] = "--enable-features=1a,1b";
  constexpr const char kCombinedEnable[] = "--enable-features=";

  constexpr const char kDisable[] = "--disable-features=1a,1b";
  constexpr const char kCombinedDisable[] = "--disable-features=1a,1b";

  constexpr const char kBlinkEnable[] = "--enable-blink-features=2a,2b";

  constexpr const char kBlinkDisable[] = "--disable-blink-features=2a,2b";
  constexpr const char kCombinedBlinkDisable[] =
      "--disable-blink-features=2a,2b";

  const std::vector<std::string> kArgv = {
      kEnable, kDisable, kArg1, kBlinkEnable, kBlinkDisable, kArg2,
  };
  BrowserJob job(
      kArgv, env_, process_reaper_, &metrics_, &system_utils_,
      BrowserJob::Config{false, false, std::nullopt},
      std::make_unique<login_manager::Subprocess>(1, &system_utils_));

  // --enable-features and --disable-features should be merged into args at the
  // end of the command line, but the original args should be preserved:
  // https://crbug.com/767266. Since all enabled features are later disabled,
  // the --enable-features switch has an empty value, to override the previous
  // non-empty value.
  //
  // --enable-blink-features and --disable-blink-features should also be merged,
  // but the original args don't need to be preserved in that case (since
  // sentinel args aren't placed around them). Since all enabled blink features
  // are disabled, but original args are not preserved, there is no need to an
  // --enable-blink-features switch.
  const std::vector<std::string> kExpected = {
      kEnable,
      kDisable,
      kArg1,
      kArg2,
      kCombinedEnable,
      kCombinedDisable,
      kCombinedBlinkDisable,
  };
  EXPECT_EQ(base::JoinString(job.ExportArgv(), " "),
            base::JoinString(kExpected, " "));
}

TEST_F(BrowserJobTest, CombineFeatureArgs_Empty) {
  constexpr const char kArg1[] = "--first";
  constexpr const char kArg2[] = "--second";

  constexpr const char kEnable[] = "--enable-features=";
  constexpr const char kCombinedEnable[] = "--enable-features=";

  constexpr const char kBlinkEnable[] = "--enable-blink-features=";

  const std::vector<std::string> kArgv = {
      kEnable,
      kArg1,
      kBlinkEnable,
      kArg2,
  };
  BrowserJob job(
      kArgv, env_, process_reaper_, &metrics_, &system_utils_,
      BrowserJob::Config{false, false, std::nullopt},
      std::make_unique<login_manager::Subprocess>(1, &system_utils_));

  // --enable-features and --disable-features should be merged into args at the
  // end of the command line, but the original args should be preserved:
  // https://crbug.com/767266. Since no features are listed to enable, the
  // --enable-features switch has an empty value, matching the behavior when
  // non-empty. --disable-features is omitted since it was never present.
  //
  // --enable-blink-features and --disable-blink-features should also be merged,
  // but the original args don't need to be preserved in that case (since
  // sentinel args aren't placed around them). Since the blink features enable
  // flag is empty, but original args are not preserved, there is no need to an
  // --enable-blink-features switch.
  const std::vector<std::string> kExpected = {
      kEnable,
      kArg1,
      kArg2,
      kCombinedEnable,
  };
  EXPECT_EQ(base::JoinString(job.ExportArgv(), " "),
            base::JoinString(kExpected, " "));
}

}  // namespace login_manager
