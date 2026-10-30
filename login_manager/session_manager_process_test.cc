// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <sys/types.h>
#include <unistd.h>

#include <memory>
#include <optional>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/functional/bind.h>
#include <base/location.h>
#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <base/memory/ref_counted.h>
#include <base/strings/string_util.h>
#include <brillo/message_loops/base_message_loop.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/dbus.h>
#include <dbus/mock_object_proxy.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "login_manager/browser_job.h"
#include "login_manager/fake_browser_job.h"
#include "login_manager/fake_child_process.h"
#include "login_manager/mock_device_policy_service.h"
#include "login_manager/mock_file_checker.h"
#include "login_manager/mock_liveness_checker.h"
#include "login_manager/mock_metrics.h"
#include "login_manager/mock_session_manager.h"
#include "login_manager/system_utils_impl.h"
#include "power_manager/proto_bindings/suspend.pb.h"

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::AtLeast;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::Sequence;
using ::testing::UnorderedElementsAre;

namespace login_manager {

// Used as a fixture for the tests in this file.
// Gives useful shared functionality.
class SessionManagerProcessTest : public ::testing::Test {
 public:
  SessionManagerProcessTest()
      : manager_(nullptr),
        liveness_checker_(new MockLivenessChecker),
        session_manager_impl_(new MockSessionManager),
        must_destroy_mocks_(true) {}
  SessionManagerProcessTest(const SessionManagerProcessTest&) = delete;
  SessionManagerProcessTest& operator=(const SessionManagerProcessTest&) =
      delete;

  ~SessionManagerProcessTest() override {
    if (must_destroy_mocks_) {
      delete liveness_checker_;
      delete session_manager_impl_;
    }
  }

  void SetUp() override {
    brillo_loop_.SetAsCurrent();
    ASSERT_TRUE(tmpdir_.CreateUniqueTempDir());

    aborted_browser_pid_path_ = tmpdir_.GetPath().Append("aborted_browser_pid");
  }

  void TearDown() override {
    must_destroy_mocks_ = !manager_.get();
    manager_ = nullptr;
  }

 protected:
  // kFakeEmail is NOT const so that it can be passed to methods that
  // implement dbus calls, which (of necessity) take bare gchar*.
  static char kFakeEmail[];
  static const pid_t kFakePid;
  static const int kExit;

  void MockUtils() { manager_->test_api().set_system_utils(&system_utils_); }

  void ExpectShutdown() {
    EXPECT_CALL(*session_manager_impl_, AnnounceSessionStoppingIfNeeded())
        .Times(1);
    EXPECT_CALL(*session_manager_impl_, AnnounceSessionStopped()).Times(1);
  }

  void ExpectLivenessChecking() {
    EXPECT_CALL(*liveness_checker_, Start()).Times(AtLeast(1));
    EXPECT_CALL(*liveness_checker_, Stop()).Times(AtLeast(1));
  }

  void ExpectOneJobReRun(FakeBrowserJob* job, int exit_status) {
    EXPECT_CALL(*job, KillEverything(SIGKILL, _)).Times(AnyNumber());
    EXPECT_CALL(*session_manager_impl_, ShouldEndSession(_))
        .WillRepeatedly(Return(false));
    // Return false once to allow the job to rerun. We then return true to stop
    // the loop; otherwise, the test will keep restarting the job forever.
    EXPECT_CALL(*job, ShouldStop())
        .WillOnce(Return(false))
        .WillOnce(Return(true));

    // Browser shutdown time is not tracked if browser does not request stop.
    EXPECT_CALL(metrics_, SendBrowserShutdownTime(_)).Times(0);

    job->set_fake_child_process(std::make_unique<FakeChildProcess>(
        kFakePid, exit_status, manager_->test_api()));
  }

  void InitManager() {
    manager_ = new SessionManagerService(
        base::BindOnce(&SessionManagerProcessTest::CreateFakeBrowserJob,
                       base::Unretained(this)),
        GetChromeMagicFilePath(), std::nullopt, base::Seconds(3), false,
        base::TimeDelta(), 0, &metrics_, &system_utils_);
    manager_->test_api().set_liveness_checker(liveness_checker_);
    manager_->test_api().set_session_manager(session_manager_impl_);
    manager_->test_api().set_aborted_browser_pid_path(
        aborted_browser_pid_path_);
  }

  std::unique_ptr<BrowserJobInterface> CreateFakeBrowserJob(
      brillo::ProcessReaper& process_reaper) {
    CHECK(!fake_browser_job_);
    auto job = std::make_unique<FakeBrowserJob>("FakeBrowserJob");
    fake_browser_job_ = job.get();
    return job;
  }

  void SimpleRunManager() {
    ExpectShutdown();
    manager_->RunBrowser();
    brillo_loop_.Run();
  }

  void ForceRunLoop() { brillo_loop_.Run(); }

  FakeBrowserJob* CreateMockJobAndInitManager(bool schedule_exit) {
    InitManager();
    CHECK(fake_browser_job_);
    fake_browser_job_->set_schedule_exit(schedule_exit);
    fake_browser_job_->set_fake_child_process(
        std::make_unique<FakeChildProcess>(kFakePid, 0, manager_->test_api()));

    return fake_browser_job_;
  }

  int PackStatus(int status) { return __W_EXITCODE(status, 0); }
  int PackSignal(int signal) { return __W_EXITCODE(0, signal); }

  base::FilePath GetChromeMagicFilePath() {
    return tmpdir_.GetPath().Append("chrome_magic_file_path");
  }

  scoped_refptr<SessionManagerService> manager_;
  MockMetrics metrics_;
  SystemUtilsImpl system_utils_;
  base::FilePath aborted_browser_pid_path_;

  // These are bare pointers, not unique_ptrs, because we need to give them
  // to a SessionManagerService instance, but also be able to set expectations
  // on them after we hand them off.
  MockLivenessChecker* liveness_checker_;
  MockSessionManager* session_manager_impl_;
  FakeBrowserJob* fake_browser_job_ = nullptr;

 private:
  bool must_destroy_mocks_;
  base::ScopedTempDir tmpdir_;
  brillo::BaseMessageLoop brillo_loop_;
};

// static
char SessionManagerProcessTest::kFakeEmail[] = "cmasone@whaaat.org";
const pid_t SessionManagerProcessTest::kFakePid = 4;
const int SessionManagerProcessTest::kExit = 1;

class HandleSuspendReadinessMethodMatcher
    : public ::testing::MatcherInterface<dbus::MethodCall*> {
 public:
  HandleSuspendReadinessMethodMatcher(int delay_id, int suspend_id)
      : delay_id_(delay_id), suspend_id_(suspend_id) {}

  bool MatchAndExplain(
      dbus::MethodCall* method_call,
      ::testing::MatchResultListener* listener) const override {
    // Make sure we've got the right kind of method call.
    if (method_call->GetInterface() != power_manager::kPowerManagerInterface) {
      *listener << "interface was " << method_call->GetInterface();
      return false;
    }

    if (method_call->GetMember() !=
        power_manager::kHandleSuspendReadinessMethod) {
      *listener << "method name was " << method_call->GetMember();
      return false;
    }

    // Check proto for correctness.
    power_manager::SuspendReadinessInfo info;
    dbus::MessageReader reader(method_call);
    reader.PopArrayOfBytesAsProto(&info);
    if (info.delay_id() != delay_id_) {
      *listener << "delay ID was " << info.delay_id();
      return false;
    }
    if (info.suspend_id() != suspend_id_) {
      *listener << "suspend ID was " << info.suspend_id();
      return false;
    }

    return true;
  }

  void DescribeTo(::std::ostream* os) const override {
    *os << "HandleSuspendReadiness method call with delay ID " << delay_id_
        << " and suspend ID " << suspend_id_;
  }

  void DescribeNegationTo(::std::ostream* os) const override {
    *os << "non-HandleSuspendReadiness method call, or method call "
        << "not with delay ID " << delay_id_ << " and suspend ID "
        << suspend_id_;
  }

 private:
  const int delay_id_;
  const int suspend_id_;
};

inline testing::Matcher<dbus::MethodCall*> HandleSuspendReadinessMethod(
    int delay_id, int suspend_id) {
  return MakeMatcher(
      new HandleSuspendReadinessMethodMatcher(delay_id, suspend_id));
}

class StopAllVmsMethodMatcher
    : public ::testing::MatcherInterface<dbus::MethodCall*> {
 public:
  StopAllVmsMethodMatcher() = default;

  bool MatchAndExplain(
      dbus::MethodCall* method_call,
      ::testing::MatchResultListener* listener) const override {
    // Make sure we've got the right kind of method call.
    if (method_call->GetInterface() !=
        vm_tools::concierge::kVmConciergeInterface) {
      *listener << "interface was " << method_call->GetInterface();
      return false;
    }

    if (method_call->GetMember() != vm_tools::concierge::kStopAllVmsMethod) {
      *listener << "method name was " << method_call->GetMember();
      return false;
    }

    return true;
  }

  void DescribeTo(::std::ostream* os) const override {
    *os << "StopAllVms method call";
  }

  void DescribeNegationTo(::std::ostream* os) const override {
    *os << "non-StopAllVms method call";
  }
};

inline testing::Matcher<dbus::MethodCall*> StopAllVmsMethod() {
  return MakeMatcher(new StopAllVmsMethodMatcher());
}

// Browser processes get correctly terminated.
TEST_F(SessionManagerProcessTest, CleanupBrowser) {
  FakeBrowserJob* job = CreateMockJobAndInitManager(false);
  EXPECT_CALL(*job, Kill(SIGTERM, _)).Times(1);
  EXPECT_CALL(*job, AbortAndKillAll(_)).Times(1);
  job->RunInBackground(base::DoNothing());
  manager_->test_api().CleanupChildrenBeforeExit();
}

// Gracefully shut down while the browser is running.
TEST_F(SessionManagerProcessTest, BrowserRunningShutdown) {
  FakeBrowserJob* job = CreateMockJobAndInitManager(false);

  ExpectLivenessChecking();
  ExpectShutdown();

  // Expect the job to be killed.
  EXPECT_CALL(*job, Kill(SIGTERM, _)).Times(1);
  EXPECT_CALL(*job, AbortAndKillAll(_)).Times(1);

  brillo::MessageLoop::current()->PostTask(
      FROM_HERE,
      base::BindOnce(&SessionManagerService::RunBrowser, manager_.get()));

  brillo::MessageLoop::current()->PostTask(
      FROM_HERE,
      base::BindOnce(&SessionManagerService::ScheduleShutdown, manager_.get()));

  ForceRunLoop();
}

// If the browser exits and asks to stop, the session manager
// should not restart it.
TEST_F(SessionManagerProcessTest, ChildExitFlagFileStop) {
  FakeBrowserJob* job = CreateMockJobAndInitManager(true);
  manager_->test_api().set_exit_on_child_done(true);  // or it'll run forever.
  ExpectLivenessChecking();

  EXPECT_CALL(*job, KillEverything(SIGKILL, _)).Times(AnyNumber());
  EXPECT_CALL(*job, ShouldStop()).WillOnce(Return(false));
  EXPECT_CALL(metrics_,
              SendSessionExitType(LoginMetrics::SessionExitType::NORMAL_EXIT))
      .Times(1);
  // Browser shutdown time is track when browser request to stop.
  EXPECT_CALL(metrics_, SendBrowserShutdownTime(_)).Times(1);
  ASSERT_TRUE(base::WriteFile(GetChromeMagicFilePath(), ""));

  EXPECT_CALL(*session_manager_impl_, ShouldEndSession(_))
      .WillOnce(Return(false));

  SimpleRunManager();
}

// A child that exits with a signal should get re-run.
TEST_F(SessionManagerProcessTest, BadExitChildOnSignal) {
  FakeBrowserJob* job = CreateMockJobAndInitManager(true);
  ExpectLivenessChecking();
  ExpectOneJobReRun(job, PackSignal(SIGILL));
  SimpleRunManager();
}

// A child that exits badly should get re-run.
TEST_F(SessionManagerProcessTest, BadExitChild) {
  FakeBrowserJob* job = CreateMockJobAndInitManager(true);
  ExpectLivenessChecking();
  ExpectOneJobReRun(job, PackSignal(kExit));
  SimpleRunManager();
}

// A child that exits cleanly should get re-run.
TEST_F(SessionManagerProcessTest, CleanExitChild) {
  FakeBrowserJob* job = CreateMockJobAndInitManager(true);
  ExpectLivenessChecking();
  ExpectOneJobReRun(job, PackSignal(0));
  SimpleRunManager();
}

// If the browser exits while the screen is locked, the session manager
// should exit.
TEST_F(SessionManagerProcessTest, LockedExit) {
  FakeBrowserJob* job = CreateMockJobAndInitManager(true);
  ExpectLivenessChecking();

  EXPECT_CALL(*job, KillEverything(SIGKILL, _)).Times(AnyNumber());
  EXPECT_CALL(*job, ShouldStop()).Times(0);

  EXPECT_CALL(*session_manager_impl_, ShouldEndSession(_))
      .WillOnce(Return(true));
  EXPECT_CALL(metrics_,
              SendSessionExitType(LoginMetrics::SessionExitType::NORMAL_EXIT))
      .Times(1);
  // Browser shutdown time is not tracked if browser does not request stop.
  EXPECT_CALL(metrics_, SendBrowserShutdownTime(_)).Times(0);

  SimpleRunManager();
}

// Liveness checking should be started and stopped along with the browser.
TEST_F(SessionManagerProcessTest, LivenessCheckingStartStop) {
  FakeBrowserJob* job = CreateMockJobAndInitManager(true);
  {
    Sequence start_stop;
    EXPECT_CALL(*liveness_checker_, Start()).Times(2);
    EXPECT_CALL(*liveness_checker_, Stop()).Times(AtLeast(1));
  }
  EXPECT_CALL(metrics_, SendBrowserShutdownTime(_)).Times(0);
  ExpectOneJobReRun(job, PackSignal(0));
  SimpleRunManager();
}

// If the child indicates it should be stopped, the session manager must honor.
TEST_F(SessionManagerProcessTest, MustStopChild) {
  FakeBrowserJob* job = CreateMockJobAndInitManager(true);
  ExpectLivenessChecking();
  EXPECT_CALL(*job, KillEverything(SIGKILL, _)).Times(AnyNumber());
  // ShouldStop returning true indicates a login crash loop.
  EXPECT_CALL(*job, ShouldStop()).WillOnce(Return(true));
  EXPECT_CALL(*session_manager_impl_, ShouldEndSession(_))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(metrics_, SendSessionExitType(
                            LoginMetrics::SessionExitType::LOGIN_CRASH_LOOP))
      .Times(1);
  // Browser shutdown time is not tracked if browser does not request stop.
  EXPECT_CALL(metrics_, SendBrowserShutdownTime(_)).Times(0);

  SimpleRunManager();
}

TEST_F(SessionManagerProcessTest, TestWipeOnBadState) {
  CreateMockJobAndInitManager(true);

  EXPECT_CALL(*session_manager_impl_, Initialize()).WillOnce(Return(false));

  // Expect Powerwash to be triggered.
  EXPECT_CALL(*session_manager_impl_, InitiateDeviceWipe(_)).Times(1);
  EXPECT_CALL(*session_manager_impl_, Finalize()).Times(1);

  ASSERT_FALSE(manager_->test_api().InitializeImpl());
  ASSERT_EQ(SessionManagerService::MUST_WIPE_DEVICE, manager_->exit_code());
}

// When aborting the browser, the session manager should write the killed pid.
TEST_F(SessionManagerProcessTest, TestAbortedBrowserPidWritten) {
  FakeBrowserJob* job = CreateMockJobAndInitManager(false);
  EXPECT_CALL(*job, KillEverything(SIGKILL, _)).Times(AnyNumber());
  ASSERT_TRUE(job->RunInBackground(base::DoNothing()));

  manager_->AbortBrowserForHang();
  ASSERT_TRUE(base::PathExists(aborted_browser_pid_path_));
  std::string read_pid_str;
  ASSERT_TRUE(base::ReadFileToString(aborted_browser_pid_path_, &read_pid_str));
  int read_pid = atoi(read_pid_str.c_str());
  EXPECT_EQ(kFakePid, read_pid);
}

// When the vm_concierge service is running, stop all vms when the session ends.
TEST_F(SessionManagerProcessTest, StopAllVms) {
  FakeBrowserJob* job = CreateMockJobAndInitManager(true);
  scoped_refptr<dbus::MockObjectProxy> vm_concierge_proxy(
      new dbus::MockObjectProxy(nullptr, "", dbus::ObjectPath("/fake/vm")));
  manager_->test_api().set_vm_concierge_proxy(vm_concierge_proxy.get());
  manager_->test_api().set_vm_concierge_available(true);

  EXPECT_CALL(*vm_concierge_proxy.get(), CallMethod(StopAllVmsMethod(), _, _))
      .Times(AtLeast(1));

  ExpectLivenessChecking();
  ExpectOneJobReRun(job, PackSignal(0));

  SimpleRunManager();
}

TEST_F(SessionManagerProcessTest, InitializeShouldSetExtraCommandlineArgs) {
  FakeBrowserJob* job = CreateMockJobAndInitManager(/*schedule_exit=*/false);

  EXPECT_CALL(*session_manager_impl_, GetExtraCommandLineArguments)
      .WillOnce(Return(std::vector<std::string>{"--the-first-argument",
                                                "--the-second-argument"}));

  std::vector<std::string> actual_arguments;
  EXPECT_CALL(*job, SetExtraArguments).WillOnce(SaveArg<0>(&actual_arguments));

  manager_->test_api().InitializeBrowser();

  EXPECT_THAT(actual_arguments, UnorderedElementsAre("--the-first-argument",
                                                     "--the-second-argument"));
}

TEST_F(SessionManagerProcessTest, InitializeShouldSetFeatureFlags) {
  FakeBrowserJob* job = CreateMockJobAndInitManager(/*schedule_exit=*/false);

  EXPECT_CALL(*session_manager_impl_, GetFeatureFlags)
      .WillOnce(Return(
          std::vector<std::string>{"the-first-flag", "the-second-flag"}));

  std::vector<std::string> actual_flags;
  EXPECT_CALL(*job, SetFeatureFlags).WillOnce(SaveArg<0>(&actual_flags));

  manager_->test_api().InitializeBrowser();

  EXPECT_THAT(actual_flags,
              UnorderedElementsAre("the-first-flag", "the-second-flag"));
}

TEST_F(SessionManagerProcessTest,
       SetFlagsForUsersShouldKeepExtraCommandLineArguments) {
  FakeBrowserJob* job = CreateMockJobAndInitManager(/*schedule_exit=*/false);

  EXPECT_CALL(*session_manager_impl_, GetExtraCommandLineArguments)
      .WillOnce(Return(std::vector<std::string>{
          "the-first-extra-argument",
          "the-second-extra-argument",
      }));

  std::vector<std::string> actual_arguments;
  EXPECT_CALL(*job, SetExtraArguments).WillOnce(SaveArg<0>(&actual_arguments));

  manager_->SetFlagsForUser(
      "account-id", {"the-first-user-argument", "the-second-user-argument"});

  EXPECT_THAT(actual_arguments,
              UnorderedElementsAre(
                  "the-first-user-argument", "the-second-user-argument",
                  "the-first-extra-argument", "the-second-extra-argument"));
}

TEST_F(SessionManagerProcessTest,
       SetFeatureFlagsForUsersShouldForwardFeatureFlags) {
  FakeBrowserJob* job = CreateMockJobAndInitManager(/*schedule_exit=*/false);

  std::vector<std::string> actual_feature_flags;
  EXPECT_CALL(*job, SetFeatureFlags)
      .WillOnce(SaveArg<0>(&actual_feature_flags));

  manager_->SetFeatureFlagsForUser("account-id",
                                   {"the-first-flag", "the-second-flag"}, {});

  EXPECT_THAT(actual_feature_flags,
              UnorderedElementsAre("the-first-flag", "the-second-flag"));
}

TEST_F(SessionManagerProcessTest,
       SetFeatureFlagsForUsersShouldForwardOriginListFlags) {
  FakeBrowserJob* job = CreateMockJobAndInitManager(/*schedule_exit=*/false);

  std::map<std::string, std::string> actual_origin_list_flags;
  EXPECT_CALL(*job, SetFeatureFlags)
      .WillOnce(SaveArg<1>(&actual_origin_list_flags));

  manager_->SetFeatureFlagsForUser("account-id", {}, {{"origin", "flag"}});

  EXPECT_THAT(actual_origin_list_flags,
              UnorderedElementsAre(std::make_pair("origin", "flag")));
}

TEST_F(SessionManagerProcessTest,
       SetFeatureFlagsForUsersShouldResetExtraArguments) {
  FakeBrowserJob* job = CreateMockJobAndInitManager(/*schedule_exit=*/false);

  EXPECT_CALL(*session_manager_impl_, GetExtraCommandLineArguments)
      .WillOnce(Return(std::vector<std::string>{
          "the-first-extra-argument",
          "the-second-extra-argument",
      }));

  std::vector<std::string> actual_arguments;
  EXPECT_CALL(*job, SetExtraArguments).WillOnce(SaveArg<0>(&actual_arguments));

  manager_->SetFeatureFlagsForUser("account-id", {"the-flags"},
                                   {{"origin", "flag"}});

  EXPECT_THAT(actual_arguments,
              UnorderedElementsAre("the-first-extra-argument",
                                   "the-second-extra-argument"));
}

TEST_F(SessionManagerProcessTest, ShouldRunBrowser) {
  InitManager();
  EXPECT_TRUE(manager_->test_api().ShouldRunBrowser());
  // Create a file at a specific path then browser restart will be prevented.
  ASSERT_TRUE(base::WriteFile(GetChromeMagicFilePath(), ""));
  EXPECT_FALSE(manager_->test_api().ShouldRunBrowser());
}

TEST_F(SessionManagerProcessTest,
       HandleNameOwnerChanged_TracksAndRemovesConnections) {
  InitManager();
  // Unique connection names should be tracked when connected.
  manager_->test_api().HandleNameOwnerChanged(":1.42", "", ":1.42");
  fake_browser_job_->set_spawn_time(base::TimeTicks::Now() - base::Seconds(10));
  EXPECT_TRUE(manager_->test_api().IsSenderConnectedAfterBrowserSpawn(":1.42"));

  // Disconnection removes the connection.
  manager_->test_api().HandleNameOwnerChanged(":1.42", ":1.42", "");
  EXPECT_FALSE(
      manager_->test_api().IsSenderConnectedAfterBrowserSpawn(":1.42"));

  // Non-unique names (like service names) should be ignored.
  manager_->test_api().HandleNameOwnerChanged("org.chromium.SomeService", "",
                                              ":1.42");
  EXPECT_FALSE(manager_->test_api().IsSenderConnectedAfterBrowserSpawn(
      "org.chromium.SomeService"));
}

TEST_F(SessionManagerProcessTest,
       IsSenderConnectedAfterBrowserSpawn_NoBrowserRunning) {
  InitManager();
  fake_browser_job_->set_spawn_time(std::nullopt);
  manager_->test_api().set_connection_timestamp(":1.42",
                                                base::TimeTicks::Now());
  EXPECT_FALSE(
      manager_->test_api().IsSenderConnectedAfterBrowserSpawn(":1.42"));
}

TEST_F(SessionManagerProcessTest,
       IsSenderConnectedAfterBrowserSpawn_UnknownSender) {
  InitManager();
  fake_browser_job_->set_spawn_time(base::TimeTicks::Now());
  EXPECT_FALSE(
      manager_->test_api().IsSenderConnectedAfterBrowserSpawn(":1.999"));
}

TEST_F(SessionManagerProcessTest,
       IsSenderConnectedAfterBrowserSpawn_ConnectionBeforeBrowserSpawn) {
  InitManager();
  base::TimeTicks now = base::TimeTicks::Now();
  base::TimeTicks conn_time = now - base::Seconds(5);
  base::TimeTicks spawn_time = now;

  // A connection created before the browser was spawned (e.g. by a dead
  // attacker process whose PID was later recycled) must be rejected.
  manager_->test_api().set_connection_timestamp(":1.42", conn_time);
  fake_browser_job_->set_spawn_time(spawn_time);

  EXPECT_FALSE(
      manager_->test_api().IsSenderConnectedAfterBrowserSpawn(":1.42"));
}

TEST_F(SessionManagerProcessTest,
       IsSenderConnectedAfterBrowserSpawn_ConnectionAtOrAfterBrowserSpawn) {
  InitManager();
  base::TimeTicks now = base::TimeTicks::Now();
  base::TimeTicks spawn_time = now;
  base::TimeTicks conn_time = now + base::Seconds(1);

  fake_browser_job_->set_spawn_time(spawn_time);
  manager_->test_api().set_connection_timestamp(":1.42", conn_time);

  EXPECT_TRUE(manager_->test_api().IsSenderConnectedAfterBrowserSpawn(":1.42"));
}

namespace {

class ScopedTestDBusConnection {
 public:
  ScopedTestDBusConnection() {
    DBusError error;
    dbus_error_init(&error);
    server_ = dbus_server_listen("unix:tmpdir=/tmp", &error);
    CHECK(server_) << "dbus_server_listen failed: " << error.message;
    char* address = dbus_server_get_address(server_);
    CHECK(address);
    conn_ = dbus_connection_open_private(address, &error);
    dbus_free(address);
    CHECK(conn_) << "dbus_connection_open_private failed: " << error.message;
  }
  ScopedTestDBusConnection(const ScopedTestDBusConnection&) = delete;
  ScopedTestDBusConnection& operator=(const ScopedTestDBusConnection&) = delete;

  ~ScopedTestDBusConnection() {
    if (conn_) {
      dbus_connection_close(conn_);
      dbus_connection_unref(conn_);
    }
    if (server_) {
      dbus_server_disconnect(server_);
      dbus_server_unref(server_);
    }
  }

  DBusConnection* get() const { return conn_; }

 private:
  DBusServer* server_ = nullptr;
  DBusConnection* conn_ = nullptr;
};

}  // namespace

TEST_F(SessionManagerProcessTest, FilterMessage_NoPidCheckRequired) {
  InitManager();
  ScopedTestDBusConnection conn;
  DBusMessage* msg = dbus_message_new_method_call(
      "org.chromium.SessionManagerInterface", "/org/chromium/SessionManager",
      "org.chromium.SessionManagerInterface", "EnableChromeTesting");
  ASSERT_TRUE(msg);
  dbus_message_set_serial(msg, 1);
  EXPECT_EQ(DBUS_HANDLER_RESULT_NOT_YET_HANDLED,
            manager_->test_api().FilterMessage(conn.get(), msg));
  dbus_message_unref(msg);
}

TEST_F(SessionManagerProcessTest, FilterMessage_RestartJobNoSender) {
  InitManager();
  ScopedTestDBusConnection conn;
  DBusMessage* msg = dbus_message_new_method_call(
      "org.chromium.SessionManagerInterface", "/org/chromium/SessionManager",
      "org.chromium.SessionManagerInterface", "RestartJob");
  ASSERT_TRUE(msg);
  dbus_message_set_serial(msg, 1);
  EXPECT_EQ(DBUS_HANDLER_RESULT_HANDLED,
            manager_->test_api().FilterMessage(conn.get(), msg));
  dbus_message_unref(msg);
}

TEST_F(SessionManagerProcessTest,
       FilterMessage_RestartJobSenderConnectedBeforeBrowserSpawn) {
  InitManager();
  ScopedTestDBusConnection conn;
  base::TimeTicks now = base::TimeTicks::Now();
  manager_->test_api().set_connection_timestamp(":1.42",
                                                now - base::Seconds(10));
  fake_browser_job_->set_spawn_time(now);

  DBusMessage* msg = dbus_message_new_method_call(
      "org.chromium.SessionManagerInterface", "/org/chromium/SessionManager",
      "org.chromium.SessionManagerInterface", "RestartJob");
  ASSERT_TRUE(msg);
  dbus_message_set_serial(msg, 1);
  dbus_message_set_sender(msg, ":1.42");

  // Connection lifetime check fails -> handled and rejected.
  EXPECT_EQ(DBUS_HANDLER_RESULT_HANDLED,
            manager_->test_api().FilterMessage(conn.get(), msg));
  dbus_message_unref(msg);
}

TEST_F(SessionManagerProcessTest, FilterMessage_NameOwnerChangedSignal) {
  InitManager();
  ScopedTestDBusConnection conn;
  DBusMessage* msg = dbus_message_new_signal(
      "/org/freedesktop/DBus", "org.freedesktop.DBus", "NameOwnerChanged");
  ASSERT_TRUE(msg);
  dbus_message_set_sender(msg, DBUS_SERVICE_DBUS);

  const char* name = ":1.100";
  const char* old_owner = "";
  const char* new_owner = ":1.100";
  ASSERT_TRUE(dbus_message_append_args(
      msg, DBUS_TYPE_STRING, &name, DBUS_TYPE_STRING, &old_owner,
      DBUS_TYPE_STRING, &new_owner, DBUS_TYPE_INVALID));

  // Signal should be processed and not consumed.
  EXPECT_EQ(DBUS_HANDLER_RESULT_NOT_YET_HANDLED,
            manager_->test_api().FilterMessage(conn.get(), msg));
  dbus_message_unref(msg);

  // The connection should now be tracked.
  fake_browser_job_->set_spawn_time(base::TimeTicks::Now() - base::Seconds(5));
  EXPECT_TRUE(
      manager_->test_api().IsSenderConnectedAfterBrowserSpawn(":1.100"));
}

}  // namespace login_manager
