// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/browser_job.h"

#include <stdint.h>
#include <unistd.h>

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <base/command_line.h>
#include <base/logging.h>
#include <base/strings/string_util.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "login_manager/mock_file_checker.h"
#include "login_manager/mock_metrics.h"
#include "login_manager/mock_subprocess.h"
#include "login_manager/mock_system_utils.h"
#include "login_manager/subprocess.h"

namespace login_manager {

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::ElementsAre;
using ::testing::InSequence;
using ::testing::Mock;
using ::testing::Return;
using ::testing::StrEq;

class BrowserJobTest : public ::testing::Test {
 public:
  BrowserJobTest() {}

  ~BrowserJobTest() override {}

  void SetUp() override;

 protected:
  static const char* kArgv[];
  static const char kUser[];
  static const char kHash[];

  void ExpectArgsToContainFlag(const std::vector<std::string>& argv,
                               const char name[],
                               const char value[]) {
    std::vector<std::string>::const_iterator user_flag = std::find(
        argv.begin(), argv.end(), base::StringPrintf("%s%s", name, value));
    EXPECT_NE(user_flag, argv.end()) << "argv should contain " << name << value;
  }

  void ExpectArgsNotToContainFlag(const std::vector<std::string>& argv,
                                  const char name[],
                                  const char value[]) {
    std::vector<std::string>::const_iterator user_flag = std::find(
        argv.begin(), argv.end(), base::StringPrintf("%s%s", name, value));
    EXPECT_EQ(user_flag, argv.end())
        << "argv shouldn't contain " << name << value;
  }

  void ExpectArgsToContainAll(const std::vector<std::string>& argv,
                              const std::vector<std::string>& contained) {
    std::set<std::string> argv_set(argv.begin(), argv.end());
    for (std::vector<std::string>::const_iterator it = contained.begin();
         it != contained.end(); ++it) {
      EXPECT_EQ(argv_set.count(*it), 1) << "argv should contain " << *it;
    }
  }

  std::vector<std::string> argv_;
  std::vector<std::string> env_;
  MockFileChecker checker_;
  MockMetrics metrics_;
  MockSystemUtils utils_;
  std::unique_ptr<BrowserJob> job_;

 private:
  DISALLOW_COPY_AND_ASSIGN(BrowserJobTest);
};

// Default argument list for a job to use in mostly all test cases.
const char* BrowserJobTest::kArgv[] = {"zero", "one", "two"};

// Normal username to test session for.
const char BrowserJobTest::kUser[] = "test@gmail.com";
const char BrowserJobTest::kHash[] = "fake_hash";

void BrowserJobTest::SetUp() {
  argv_ =
      std::vector<std::string>(kArgv, kArgv + arraysize(BrowserJobTest::kArgv));
  job_.reset(new BrowserJob(
      argv_, env_, &checker_, &metrics_, &utils_, BrowserJob::Config{false},
      std::make_unique<login_manager::Subprocess>(getuid(), &utils_)));
}

TEST_F(BrowserJobTest, InitializationTest) {
  EXPECT_FALSE(job_->removed_login_manager_flag_);
  std::vector<std::string> job_args = job_->ExportArgv();
  ASSERT_EQ(argv_.size(), job_args.size());
  ExpectArgsToContainAll(job_args, argv_);
}

TEST_F(BrowserJobTest, WaitAndAbort) {
  pid_t kDummyPid = 4;
  EXPECT_CALL(utils_, GetGidAndGroups(_, _, _)).WillOnce(Return(true));
  EXPECT_CALL(utils_, fork()).WillOnce(Return(kDummyPid));
  EXPECT_CALL(utils_, kill(-kDummyPid, _, SIGABRT)).Times(1);
  EXPECT_CALL(utils_, time(NULL)).WillRepeatedly(Return(0));
  EXPECT_CALL(utils_, ProcessGroupIsGone(kDummyPid, _)).WillOnce(Return(false));

  EXPECT_CALL(metrics_, HasRecordedChromeExec()).WillRepeatedly(Return(false));
  EXPECT_CALL(metrics_, RecordStats(_)).Times(AnyNumber());

  ASSERT_TRUE(job_->RunInBackground());
  job_->WaitAndAbort(base::TimeDelta::FromSeconds(3));
}

TEST_F(BrowserJobTest, WaitAndAbort_AlreadyGone) {
  pid_t kDummyPid = 4;
  EXPECT_CALL(utils_, GetGidAndGroups(_, _, _)).WillOnce(Return(true));
  EXPECT_CALL(utils_, fork()).WillOnce(Return(kDummyPid));
  EXPECT_CALL(utils_, time(NULL)).WillRepeatedly(Return(0));
  EXPECT_CALL(utils_, ProcessGroupIsGone(kDummyPid, _)).WillOnce(Return(true));

  EXPECT_CALL(metrics_, HasRecordedChromeExec()).WillRepeatedly(Return(false));
  EXPECT_CALL(metrics_, RecordStats(_)).Times(AnyNumber());

  ASSERT_TRUE(job_->RunInBackground());
  job_->WaitAndAbort(base::TimeDelta::FromSeconds(3));
}

TEST_F(BrowserJobTest, ShouldStopTest) {
  EXPECT_CALL(utils_, time(NULL))
      .WillRepeatedly(Return(BrowserJob::kRestartWindowSeconds));
  for (int i = 0; i < BrowserJob::kRestartTries - 1; ++i)
    job_->RecordTime();
  // We haven't yet saturated the list of start times, so...
  EXPECT_FALSE(job_->ShouldStop());

  // Go ahead and saturate.
  job_->RecordTime();
  EXPECT_NE(0, job_->start_times_.front());
  EXPECT_TRUE(job_->ShouldStop());
}

TEST_F(BrowserJobTest, ShouldNotStopTest) {
  EXPECT_CALL(utils_, time(NULL))
      .WillOnce(Return(BrowserJob::kRestartWindowSeconds))
      .WillOnce(Return(3 * BrowserJob::kRestartWindowSeconds));
  job_->RecordTime();
  EXPECT_FALSE(job_->ShouldStop());
}

TEST_F(BrowserJobTest, ShouldDropExtraArgumentsAndEnvironmentVariablesTest) {
  EXPECT_CALL(utils_, time(NULL))
      .WillRepeatedly(Return(BrowserJob::kRestartWindowSeconds));

  // Simulate restart kUseExtraArgsRuns - 1 times and no dropping.
  for (int i = 0; i < BrowserJob::kUseExtraArgsRuns - 1; ++i)
    job_->RecordTime();
  EXPECT_FALSE(job_->ShouldDropExtraArgumentsAndEnvironmentVariables());

  // One more restart and extra args and env vars should be dropped.
  job_->RecordTime();
  EXPECT_TRUE(job_->ShouldDropExtraArgumentsAndEnvironmentVariables());
}

TEST_F(BrowserJobTest, ShouldNotRunTest) {
  EXPECT_CALL(checker_, exists()).WillRepeatedly(Return(true));
  EXPECT_FALSE(job_->ShouldRunBrowser());
}

TEST_F(BrowserJobTest, ShouldRunTest) {
  EXPECT_CALL(checker_, exists()).WillRepeatedly(Return(false));
  EXPECT_TRUE(job_->ShouldRunBrowser());
}

TEST_F(BrowserJobTest, NullFileCheckerTest) {
  BrowserJob job(argv_, env_, NULL, &metrics_, &utils_,
                 BrowserJob::Config{false},
                 std::make_unique<login_manager::Subprocess>(1, &utils_));
  EXPECT_TRUE(job.ShouldRunBrowser());
}

// On the job's first run, it should have a one-time-flag.  That
// should get cleared and not used again.
TEST_F(BrowserJobTest, OneTimeBootFlags) {
  EXPECT_CALL(utils_, GetGidAndGroups(_, _, _)).WillRepeatedly(Return(true));
  EXPECT_CALL(utils_, fork()).WillRepeatedly(Return(1));
  EXPECT_CALL(utils_, time(NULL)).WillRepeatedly(Return(0));

  EXPECT_CALL(metrics_, HasRecordedChromeExec())
      .WillOnce(Return(false))
      .WillOnce(Return(true));
  EXPECT_CALL(metrics_, RecordStats(StrEq(("chrome-exec")))).Times(2);

  ASSERT_TRUE(job_->RunInBackground());
  ExpectArgsToContainFlag(job_->ExportArgv(),
                          BrowserJob::kFirstExecAfterBootFlag, "");

  ASSERT_TRUE(job_->RunInBackground());
  ExpectArgsNotToContainFlag(job_->ExportArgv(),
                             BrowserJob::kFirstExecAfterBootFlag, "");
}

TEST_F(BrowserJobTest, RunBrowserTermMessage) {
  pid_t kDummyPid = 4;
  int signal = SIGKILL;
  EXPECT_CALL(utils_, GetGidAndGroups(_, _, _)).WillOnce(Return(true));
  EXPECT_CALL(utils_, fork()).WillOnce(Return(kDummyPid));
  EXPECT_CALL(utils_, kill(kDummyPid, _, signal)).Times(1);
  EXPECT_CALL(utils_, time(NULL)).WillRepeatedly(Return(0));

  EXPECT_CALL(metrics_, HasRecordedChromeExec()).WillRepeatedly(Return(false));
  EXPECT_CALL(metrics_, RecordStats(_)).Times(AnyNumber());

  std::string term_message("killdya");
  ASSERT_TRUE(job_->RunInBackground());
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
  BrowserJob job(argv_, env_, &checker_, &metrics_, &utils_,
                 BrowserJob::Config{false},
                 std::make_unique<login_manager::Subprocess>(1, &utils_));
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
  const char* kArgvWithLoginFlag[] = {"zero", "one", "two", "--login-manager"};
  std::vector<std::string> argv(
      kArgvWithLoginFlag, kArgvWithLoginFlag + arraysize(kArgvWithLoginFlag));
  BrowserJob job(argv, env_, &checker_, &metrics_, &utils_,
                 BrowserJob::Config{false},
                 std::make_unique<login_manager::Subprocess>(1, &utils_));

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
  const char* kNewArgs[] = {"--ichi", "--ni dfs", "--san"};
  std::vector<std::string> new_args(kNewArgs, kNewArgs + arraysize(kNewArgs));
  job_->SetArguments(new_args);

  std::vector<std::string> job_args = job_->ExportArgv();
  ASSERT_EQ(new_args.size(), job_args.size());
  EXPECT_EQ(kArgv[0], job_args[0]);
  for (size_t i = 1; i < arraysize(kNewArgs); ++i) {
    EXPECT_EQ(kNewArgs[i], job_args[i]);
  }

  job_->StartSession(kUser, kHash);
  job_args = job_->ExportArgv();
  ExpectArgsToContainFlag(job_args, BrowserJob::kLoginUserFlag, kUser);
}

TEST_F(BrowserJobTest, SetExtraArguments) {
  const char* kExtraArgs[] = {"--ichi", "--ni", "--san"};
  std::vector<std::string> extra_args(kExtraArgs,
                                      kExtraArgs + arraysize(kExtraArgs));
  job_->SetExtraArguments(extra_args);

  std::vector<std::string> job_args = job_->ExportArgv();
  ExpectArgsToContainAll(job_args, argv_);
  ExpectArgsToContainAll(job_args, extra_args);
}

TEST_F(BrowserJobTest, ExportArgv) {
  std::vector<std::string> argv(kArgv, kArgv + arraysize(kArgv));
  BrowserJob job(argv, env_, &checker_, &metrics_, &utils_,
                 BrowserJob::Config{false},
                 std::make_unique<login_manager::Subprocess>(1, &utils_));

  const char* kExtraArgs[] = {"--ichi", "--ni", "--san"};
  std::vector<std::string> extra_args(kExtraArgs,
                                      kExtraArgs + arraysize(kExtraArgs));
  argv.insert(argv.end(), extra_args.begin(), extra_args.end());
  job.SetExtraArguments(extra_args);
  EXPECT_EQ(argv, job.ExportArgv());
}

TEST_F(BrowserJobTest, SetExtraEnvironmentVariables) {
  std::vector<std::string> argv(kArgv, kArgv + arraysize(kArgv));
  BrowserJob job(argv, {"A=a"}, &checker_, &metrics_, &utils_,
                 BrowserJob::Config{false},
                 std::make_unique<login_manager::Subprocess>(1, &utils_));
  job.SetExtraEnvironmentVariables({"B=b", "C="});
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

    const char* kMultipleVmoduleArgs[] = {kArg1,     kVmodule1, kArg2, kArg3,
                                          kVmodule2, kVmodule3, kArg4};

    std::vector<std::string> argv(
        kMultipleVmoduleArgs,
        kMultipleVmoduleArgs + arraysize(kMultipleVmoduleArgs));
    BrowserJob job(argv, env_, &checker_, &metrics_, &utils_,
                   BrowserJob::Config{false},
                   std::make_unique<login_manager::Subprocess>(1, &utils_));

    const char* kCombinedVmodule =
        "--vmodule=file1=1,file2=2,file3=3,file4=4,file5=5,file6=6";

    auto job_argv = job.ExportArgv();
    ASSERT_EQ(5, job_argv.size());
    EXPECT_EQ(kArg1, job_argv[0]);
    EXPECT_EQ(kArg2, job_argv[1]);
    EXPECT_EQ(kArg3, job_argv[2]);
    EXPECT_EQ(kArg4, job_argv[3]);
    EXPECT_EQ(kCombinedVmodule, job_argv[4]);
  }

  {
    // A testcase with no --vmodule flag.
    const char* kNoVmoduleArgs[] = {kArg1, kArg2, kArg3, kArg4};
    std::vector<std::string> argv(kNoVmoduleArgs,
                                  kNoVmoduleArgs + arraysize(kNoVmoduleArgs));

    BrowserJob job(argv, env_, &checker_, &metrics_, &utils_,
                   BrowserJob::Config{false},
                   std::make_unique<login_manager::Subprocess>(1, &utils_));

    auto job_argv = job.ExportArgv();
    ASSERT_EQ(4, job_argv.size());
    EXPECT_EQ(kArg1, job_argv[0]);
    EXPECT_EQ(kArg2, job_argv[1]);
    EXPECT_EQ(kArg3, job_argv[2]);
    EXPECT_EQ(kArg4, job_argv[3]);
  }
}

TEST_F(BrowserJobTest, CombineFeatureArgs) {
  constexpr const char kArg1[] = "--first";
  constexpr const char kArg2[] = "--second";

  constexpr const char kEnable1[] = "--enable-features=1a,1b";
  constexpr const char kEnable2[] = "--enable-features=2a,2b";
  constexpr const char kEnable3[] = "--enable-features=3a,3b";
  constexpr const char kCombinedEnable[] =
      "--enable-features=1a,1b,2a,2b,3a,3b";

  constexpr const char kDisable1[] = "--disable-features=4a,4b";
  constexpr const char kDisable2[] = "--disable-features=5a,5b";
  constexpr const char kDisable3[] = "--disable-features=6a,6b";
  constexpr const char kCombinedDisable[] =
      "--disable-features=4a,4b,5a,5b,6a,6b";

  constexpr const char kBlinkEnable1[] = "--enable-blink-features=7a,7b";
  constexpr const char kBlinkEnable2[] = "--enable-blink-features=8a,8b";
  constexpr const char kBlinkEnable3[] = "--enable-blink-features=9a,9b";
  constexpr const char kCombinedBlinkEnable[] =
      "--enable-blink-features=7a,7b,8a,8b,9a,9b";

  constexpr const char kBlinkDisable1[] = "--disable-blink-features=10a,10b";
  constexpr const char kBlinkDisable2[] = "--disable-blink-features=11a,11b";
  constexpr const char kBlinkDisable3[] = "--disable-blink-features=12a,12b";
  constexpr const char kCombinedBlinkDisable[] =
      "--disable-blink-features=10a,10b,11a,11b,12a,12b";

  const std::vector<std::string> kArgv = {
      kEnable1, kDisable1, kBlinkEnable1, kBlinkDisable1, kArg1,
      kEnable2, kDisable2, kBlinkEnable2, kBlinkDisable2, kArg2,
      kEnable3, kDisable3, kBlinkEnable3, kBlinkDisable3,
  };
  BrowserJob job(kArgv, env_, &checker_, &metrics_, &utils_,
                 BrowserJob::Config{false},
                 std::make_unique<login_manager::Subprocess>(1, &utils_));

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

}  // namespace login_manager
