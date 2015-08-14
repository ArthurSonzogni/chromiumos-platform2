// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/update_attempter.h"

#include <stdint.h>

#include <memory>

#include <base/files/file_util.h>
#include <chromeos/bind_lambda.h>
#include <chromeos/dbus/service_constants.h>
#include <chromeos/make_unique_ptr.h>
#include <chromeos/message_loops/glib_message_loop.h>
#include <chromeos/message_loops/message_loop.h>
#include <chromeos/message_loops/message_loop_utils.h>
#include <gtest/gtest.h>
#include <policy/libpolicy.h>
#include <policy/mock_device_policy.h>

#include "update_engine/fake_clock.h"
#include "update_engine/fake_prefs.h"
#include "update_engine/fake_system_state.h"
#include "update_engine/filesystem_verifier_action.h"
#include "update_engine/install_plan.h"
#include "update_engine/mock_action.h"
#include "update_engine/mock_action_processor.h"
#include "update_engine/mock_http_fetcher.h"
#include "update_engine/mock_p2p_manager.h"
#include "update_engine/mock_payload_state.h"
#include "update_engine/mock_prefs.h"
#include "update_engine/postinstall_runner_action.h"
#include "update_engine/prefs.h"
#include "update_engine/test_utils.h"
#include "update_engine/utils.h"

using base::Time;
using base::TimeDelta;
using chromeos::MessageLoop;
using org::chromium::LibCrosServiceInterfaceProxyMock;
using org::chromium::UpdateEngineLibcrosProxyResolvedInterfaceProxyMock;
using std::string;
using std::unique_ptr;
using testing::DoAll;
using testing::InSequence;
using testing::Ne;
using testing::NiceMock;
using testing::Property;
using testing::Return;
using testing::ReturnPointee;
using testing::SaveArg;
using testing::SetArgumentPointee;
using testing::_;

namespace chromeos_update_engine {

// Test a subclass rather than the main class directly so that we can mock out
// methods within the class. There're explicit unit tests for the mocked out
// methods.
class UpdateAttempterUnderTest : public UpdateAttempter {
 public:
  UpdateAttempterUnderTest(SystemState* system_state,
                           LibCrosProxy* libcros_proxy,
                           org::chromium::debugdProxyInterface* debugd_proxy,
                           const string& update_completed_marker)
      : UpdateAttempter(system_state, libcros_proxy, debugd_proxy,
                        update_completed_marker) {}

  // Wrap the update scheduling method, allowing us to opt out of scheduled
  // updates for testing purposes.
  void ScheduleUpdates() override {
    schedule_updates_called_ = true;
    if (do_schedule_updates_) {
      UpdateAttempter::ScheduleUpdates();
    } else {
      LOG(INFO) << "[TEST] Update scheduling disabled.";
    }
  }
  void EnableScheduleUpdates() { do_schedule_updates_ = true; }
  void DisableScheduleUpdates() { do_schedule_updates_ = false; }

  // Indicates whether ScheduleUpdates() was called.
  bool schedule_updates_called() const { return schedule_updates_called_; }

  // Need to expose forced_omaha_url_ so we can test it.
  const string& forced_omaha_url() const { return forced_omaha_url_; }

 private:
  bool schedule_updates_called_ = false;
  bool do_schedule_updates_ = true;
};

class UpdateAttempterTest : public ::testing::Test {
 protected:
  UpdateAttempterTest()
      : service_interface_mock_(new LibCrosServiceInterfaceProxyMock()),
        ue_proxy_resolved_interface_mock_(
            new NiceMock<UpdateEngineLibcrosProxyResolvedInterfaceProxyMock>()),
        libcros_proxy_(
            chromeos::make_unique_ptr(service_interface_mock_),
            chromeos::make_unique_ptr(ue_proxy_resolved_interface_mock_)) {
    // Override system state members.
    fake_system_state_.set_connection_manager(&mock_connection_manager);
    fake_system_state_.set_update_attempter(&attempter_);
    loop_.SetAsCurrent();

    // Finish initializing the attempter.
    attempter_.Init();
    // Don't run setgoodkernel command.
    attempter_.skip_set_good_kernel_ = true;
  }

  void SetUp() override {
    CHECK(utils::MakeTempDirectory("UpdateAttempterTest-XXXXXX", &test_dir_));

    EXPECT_EQ(nullptr, attempter_.dbus_adaptor_);
    EXPECT_NE(nullptr, attempter_.system_state_);
    EXPECT_EQ(0, attempter_.http_response_code_);
    EXPECT_EQ(utils::kCpuSharesNormal, attempter_.shares_);
    EXPECT_EQ(MessageLoop::kTaskIdNull, attempter_.manage_shares_id_);
    EXPECT_FALSE(attempter_.download_active_);
    EXPECT_EQ(UPDATE_STATUS_IDLE, attempter_.status_);
    EXPECT_EQ(0.0, attempter_.download_progress_);
    EXPECT_EQ(0, attempter_.last_checked_time_);
    EXPECT_EQ("0.0.0.0", attempter_.new_version_);
    EXPECT_EQ(0, attempter_.new_payload_size_);
    processor_ = new NiceMock<MockActionProcessor>();
    attempter_.processor_.reset(processor_);  // Transfers ownership.
    prefs_ = fake_system_state_.mock_prefs();

    // Set up store/load semantics of P2P properties via the mock PayloadState.
    actual_using_p2p_for_downloading_ = false;
    EXPECT_CALL(*fake_system_state_.mock_payload_state(),
                SetUsingP2PForDownloading(_))
        .WillRepeatedly(SaveArg<0>(&actual_using_p2p_for_downloading_));
    EXPECT_CALL(*fake_system_state_.mock_payload_state(),
                GetUsingP2PForDownloading())
        .WillRepeatedly(ReturnPointee(&actual_using_p2p_for_downloading_));
    actual_using_p2p_for_sharing_ = false;
    EXPECT_CALL(*fake_system_state_.mock_payload_state(),
                SetUsingP2PForSharing(_))
        .WillRepeatedly(SaveArg<0>(&actual_using_p2p_for_sharing_));
    EXPECT_CALL(*fake_system_state_.mock_payload_state(),
                GetUsingP2PForDownloading())
        .WillRepeatedly(ReturnPointee(&actual_using_p2p_for_sharing_));
  }

  void TearDown() override {
    test_utils::RecursiveUnlinkDir(test_dir_);
  }

 public:
  void ScheduleQuitMainLoop();

  // Callbacks to run the different tests from the main loop.
  void UpdateTestStart();
  void UpdateTestVerify();
  void RollbackTestStart(bool enterprise_rollback, bool valid_slot);
  void RollbackTestVerify();
  void PingOmahaTestStart();
  void ReadScatterFactorFromPolicyTestStart();
  void DecrementUpdateCheckCountTestStart();
  void NoScatteringDoneDuringManualUpdateTestStart();
  void P2PNotEnabledStart();
  void P2PEnabledStart();
  void P2PEnabledInteractiveStart();
  void P2PEnabledStartingFailsStart();
  void P2PEnabledHousekeepingFailsStart();

  bool actual_using_p2p_for_downloading() {
    return actual_using_p2p_for_downloading_;
  }
  bool actual_using_p2p_for_sharing() {
    return actual_using_p2p_for_sharing_;
  }

  // TODO(deymo): Replace this with a FakeMessageLoop. Some of these tests use a
  // real LibcurlHttpFetcher, which still requires a GlibMessageLoop.
  chromeos::GlibMessageLoop loop_;

  FakeSystemState fake_system_state_;
  org::chromium::debugdProxyMock debugd_proxy_mock_;
  LibCrosServiceInterfaceProxyMock* service_interface_mock_;
  UpdateEngineLibcrosProxyResolvedInterfaceProxyMock*
      ue_proxy_resolved_interface_mock_;
  LibCrosProxy libcros_proxy_;
  UpdateAttempterUnderTest attempter_{&fake_system_state_,
                                      &libcros_proxy_,
                                      &debugd_proxy_mock_,
                                      ""};

  NiceMock<MockActionProcessor>* processor_;
  NiceMock<MockPrefs>* prefs_;  // Shortcut to fake_system_state_->mock_prefs().
  NiceMock<MockConnectionManager> mock_connection_manager;

  string test_dir_;

  bool actual_using_p2p_for_downloading_;
  bool actual_using_p2p_for_sharing_;
};

void UpdateAttempterTest::ScheduleQuitMainLoop() {
  loop_.PostTask(FROM_HERE, base::Bind([this] { this->loop_.BreakLoop(); }));
}

TEST_F(UpdateAttempterTest, ActionCompletedDownloadTest) {
  unique_ptr<MockHttpFetcher> fetcher(new MockHttpFetcher("", 0, nullptr));
  fetcher->FailTransfer(503);  // Sets the HTTP response code.
  DownloadAction action(prefs_, nullptr, fetcher.release());
  EXPECT_CALL(*prefs_, GetInt64(kPrefsDeltaUpdateFailures, _)).Times(0);
  attempter_.ActionCompleted(nullptr, &action, ErrorCode::kSuccess);
  EXPECT_EQ(503, attempter_.http_response_code());
  EXPECT_EQ(UPDATE_STATUS_FINALIZING, attempter_.status());
  ASSERT_EQ(nullptr, attempter_.error_event_.get());
}

TEST_F(UpdateAttempterTest, ActionCompletedErrorTest) {
  MockAction action;
  EXPECT_CALL(action, Type()).WillRepeatedly(Return("MockAction"));
  attempter_.status_ = UPDATE_STATUS_DOWNLOADING;
  EXPECT_CALL(*prefs_, GetInt64(kPrefsDeltaUpdateFailures, _))
      .WillOnce(Return(false));
  attempter_.ActionCompleted(nullptr, &action, ErrorCode::kError);
  ASSERT_NE(nullptr, attempter_.error_event_.get());
}

TEST_F(UpdateAttempterTest, ActionCompletedOmahaRequestTest) {
  unique_ptr<MockHttpFetcher> fetcher(new MockHttpFetcher("", 0, nullptr));
  fetcher->FailTransfer(500);  // Sets the HTTP response code.
  OmahaRequestAction action(&fake_system_state_, nullptr,
                            fetcher.release(), false);
  ObjectCollectorAction<OmahaResponse> collector_action;
  BondActions(&action, &collector_action);
  OmahaResponse response;
  response.poll_interval = 234;
  action.SetOutputObject(response);
  EXPECT_CALL(*prefs_, GetInt64(kPrefsDeltaUpdateFailures, _)).Times(0);
  attempter_.ActionCompleted(nullptr, &action, ErrorCode::kSuccess);
  EXPECT_EQ(500, attempter_.http_response_code());
  EXPECT_EQ(UPDATE_STATUS_IDLE, attempter_.status());
  EXPECT_EQ(234, attempter_.server_dictated_poll_interval_);
  ASSERT_TRUE(attempter_.error_event_.get() == nullptr);
}

TEST_F(UpdateAttempterTest, ConstructWithUpdatedMarkerTest) {
  string test_update_completed_marker;
  CHECK(utils::MakeTempFile(
      "update_attempter_unittest-update_completed_marker-XXXXXX",
      &test_update_completed_marker,
      nullptr));
  ScopedPathUnlinker completed_marker_unlinker(test_update_completed_marker);
  const base::FilePath marker(test_update_completed_marker);
  EXPECT_EQ(0, base::WriteFile(marker, "", 0));
  UpdateAttempterUnderTest attempter(&fake_system_state_,
                                     nullptr,
                                     &debugd_proxy_mock_,
                                     test_update_completed_marker);
  EXPECT_EQ(UPDATE_STATUS_UPDATED_NEED_REBOOT, attempter.status());
}

TEST_F(UpdateAttempterTest, GetErrorCodeForActionTest) {
  extern ErrorCode GetErrorCodeForAction(AbstractAction* action,
                                              ErrorCode code);
  EXPECT_EQ(ErrorCode::kSuccess,
            GetErrorCodeForAction(nullptr, ErrorCode::kSuccess));

  FakeSystemState fake_system_state;
  OmahaRequestAction omaha_request_action(&fake_system_state, nullptr,
                                          nullptr, false);
  EXPECT_EQ(ErrorCode::kOmahaRequestError,
            GetErrorCodeForAction(&omaha_request_action, ErrorCode::kError));
  OmahaResponseHandlerAction omaha_response_handler_action(&fake_system_state_);
  EXPECT_EQ(ErrorCode::kOmahaResponseHandlerError,
            GetErrorCodeForAction(&omaha_response_handler_action,
                                  ErrorCode::kError));
  FilesystemVerifierAction filesystem_verifier_action(
      &fake_system_state_, PartitionType::kRootfs);
  EXPECT_EQ(ErrorCode::kFilesystemVerifierError,
            GetErrorCodeForAction(&filesystem_verifier_action,
                                  ErrorCode::kError));
  PostinstallRunnerAction postinstall_runner_action;
  EXPECT_EQ(ErrorCode::kPostinstallRunnerError,
            GetErrorCodeForAction(&postinstall_runner_action,
                                  ErrorCode::kError));
  MockAction action_mock;
  EXPECT_CALL(action_mock, Type()).WillOnce(Return("MockAction"));
  EXPECT_EQ(ErrorCode::kError,
            GetErrorCodeForAction(&action_mock, ErrorCode::kError));
}

TEST_F(UpdateAttempterTest, DisableDeltaUpdateIfNeededTest) {
  attempter_.omaha_request_params_->set_delta_okay(true);
  EXPECT_CALL(*prefs_, GetInt64(kPrefsDeltaUpdateFailures, _))
      .WillOnce(Return(false));
  attempter_.DisableDeltaUpdateIfNeeded();
  EXPECT_TRUE(attempter_.omaha_request_params_->delta_okay());
  EXPECT_CALL(*prefs_, GetInt64(kPrefsDeltaUpdateFailures, _))
      .WillOnce(DoAll(
          SetArgumentPointee<1>(UpdateAttempter::kMaxDeltaUpdateFailures - 1),
          Return(true)));
  attempter_.DisableDeltaUpdateIfNeeded();
  EXPECT_TRUE(attempter_.omaha_request_params_->delta_okay());
  EXPECT_CALL(*prefs_, GetInt64(kPrefsDeltaUpdateFailures, _))
      .WillOnce(DoAll(
          SetArgumentPointee<1>(UpdateAttempter::kMaxDeltaUpdateFailures),
          Return(true)));
  attempter_.DisableDeltaUpdateIfNeeded();
  EXPECT_FALSE(attempter_.omaha_request_params_->delta_okay());
  EXPECT_CALL(*prefs_, GetInt64(_, _)).Times(0);
  attempter_.DisableDeltaUpdateIfNeeded();
  EXPECT_FALSE(attempter_.omaha_request_params_->delta_okay());
}

TEST_F(UpdateAttempterTest, MarkDeltaUpdateFailureTest) {
  EXPECT_CALL(*prefs_, GetInt64(kPrefsDeltaUpdateFailures, _))
      .WillOnce(Return(false))
      .WillOnce(DoAll(SetArgumentPointee<1>(-1), Return(true)))
      .WillOnce(DoAll(SetArgumentPointee<1>(1), Return(true)))
      .WillOnce(DoAll(
          SetArgumentPointee<1>(UpdateAttempter::kMaxDeltaUpdateFailures),
          Return(true)));
  EXPECT_CALL(*prefs_, SetInt64(Ne(kPrefsDeltaUpdateFailures), _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*prefs_, SetInt64(kPrefsDeltaUpdateFailures, 1)).Times(2);
  EXPECT_CALL(*prefs_, SetInt64(kPrefsDeltaUpdateFailures, 2));
  EXPECT_CALL(*prefs_, SetInt64(kPrefsDeltaUpdateFailures,
                               UpdateAttempter::kMaxDeltaUpdateFailures + 1));
  for (int i = 0; i < 4; i ++)
    attempter_.MarkDeltaUpdateFailure();
}

TEST_F(UpdateAttempterTest, ScheduleErrorEventActionNoEventTest) {
  EXPECT_CALL(*processor_, EnqueueAction(_)).Times(0);
  EXPECT_CALL(*processor_, StartProcessing()).Times(0);
  EXPECT_CALL(*fake_system_state_.mock_payload_state(), UpdateFailed(_))
      .Times(0);
  OmahaResponse response;
  string url1 = "http://url1";
  response.payload_urls.push_back(url1);
  response.payload_urls.push_back("https://url");
  EXPECT_CALL(*(fake_system_state_.mock_payload_state()), GetCurrentUrl())
      .WillRepeatedly(Return(url1));
  fake_system_state_.mock_payload_state()->SetResponse(response);
  attempter_.ScheduleErrorEventAction();
  EXPECT_EQ(url1, fake_system_state_.mock_payload_state()->GetCurrentUrl());
}

TEST_F(UpdateAttempterTest, ScheduleErrorEventActionTest) {
  EXPECT_CALL(*processor_,
              EnqueueAction(Property(&AbstractAction::Type,
                                     OmahaRequestAction::StaticType())));
  EXPECT_CALL(*processor_, StartProcessing());
  ErrorCode err = ErrorCode::kError;
  EXPECT_CALL(*fake_system_state_.mock_payload_state(), UpdateFailed(err));
  attempter_.error_event_.reset(new OmahaEvent(OmahaEvent::kTypeUpdateComplete,
                                               OmahaEvent::kResultError,
                                               err));
  attempter_.ScheduleErrorEventAction();
  EXPECT_EQ(UPDATE_STATUS_REPORTING_ERROR_EVENT, attempter_.status());
}

namespace {
// Actions that will be built as part of an update check.
const string kUpdateActionTypes[] = {  // NOLINT(runtime/string)
  OmahaRequestAction::StaticType(),
  OmahaResponseHandlerAction::StaticType(),
  FilesystemVerifierAction::StaticType(),
  FilesystemVerifierAction::StaticType(),
  OmahaRequestAction::StaticType(),
  DownloadAction::StaticType(),
  OmahaRequestAction::StaticType(),
  FilesystemVerifierAction::StaticType(),
  FilesystemVerifierAction::StaticType(),
  PostinstallRunnerAction::StaticType(),
  OmahaRequestAction::StaticType()
};

// Actions that will be built as part of a user-initiated rollback.
const string kRollbackActionTypes[] = {  // NOLINT(runtime/string)
  InstallPlanAction::StaticType(),
  PostinstallRunnerAction::StaticType(),
};

}  // namespace

void UpdateAttempterTest::UpdateTestStart() {
  attempter_.set_http_response_code(200);

  // Expect that the device policy is loaded by the UpdateAttempter at some
  // point by calling RefreshDevicePolicy.
  policy::MockDevicePolicy* device_policy = new policy::MockDevicePolicy();
  attempter_.policy_provider_.reset(new policy::PolicyProvider(device_policy));
  EXPECT_CALL(*device_policy, LoadPolicy())
      .Times(testing::AtLeast(1)).WillRepeatedly(Return(true));

  {
    InSequence s;
    for (size_t i = 0; i < arraysize(kUpdateActionTypes); ++i) {
      EXPECT_CALL(*processor_,
                  EnqueueAction(Property(&AbstractAction::Type,
                                         kUpdateActionTypes[i])));
    }
    EXPECT_CALL(*processor_, StartProcessing());
  }

  attempter_.Update("", "", "", "", false, false);
  loop_.PostTask(FROM_HERE,
                 base::Bind(&UpdateAttempterTest::UpdateTestVerify,
                            base::Unretained(this)));
}

void UpdateAttempterTest::UpdateTestVerify() {
  EXPECT_EQ(0, attempter_.http_response_code());
  EXPECT_EQ(&attempter_, processor_->delegate());
  EXPECT_EQ(arraysize(kUpdateActionTypes), attempter_.actions_.size());
  for (size_t i = 0; i < arraysize(kUpdateActionTypes); ++i) {
    EXPECT_EQ(kUpdateActionTypes[i], attempter_.actions_[i]->Type());
  }
  EXPECT_EQ(attempter_.response_handler_action_.get(),
            attempter_.actions_[1].get());
  DownloadAction* download_action =
      dynamic_cast<DownloadAction*>(attempter_.actions_[5].get());
  ASSERT_NE(nullptr, download_action);
  EXPECT_EQ(&attempter_, download_action->delegate());
  EXPECT_EQ(UPDATE_STATUS_CHECKING_FOR_UPDATE, attempter_.status());
  loop_.BreakLoop();
}

void UpdateAttempterTest::RollbackTestStart(
    bool enterprise_rollback, bool valid_slot) {
  // Create a device policy so that we can change settings.
  policy::MockDevicePolicy* device_policy = new policy::MockDevicePolicy();
  attempter_.policy_provider_.reset(new policy::PolicyProvider(device_policy));

  EXPECT_CALL(*device_policy, LoadPolicy()).WillRepeatedly(Return(true));
  fake_system_state_.set_device_policy(device_policy);

  if (!valid_slot) {
    // References bootable kernels in fake_hardware.h
    string rollback_kernel = "/dev/sdz2";
    LOG(INFO) << "Test Mark Unbootable: " << rollback_kernel;
    fake_system_state_.fake_hardware()->MarkKernelUnbootable(
        rollback_kernel);
  }

  bool is_rollback_allowed = false;

  // We only allow rollback on devices that are not enterprise enrolled and
  // which have a valid slot to rollback to.
  if (!enterprise_rollback && valid_slot) {
     is_rollback_allowed = true;
  }

  if (enterprise_rollback) {
    // We return an empty owner as this is an enterprise.
    EXPECT_CALL(*device_policy, GetOwner(_)).WillRepeatedly(
        DoAll(SetArgumentPointee<0>(string("")),
        Return(true)));
  } else {
    // We return a fake owner as this is an owned consumer device.
    EXPECT_CALL(*device_policy, GetOwner(_)).WillRepeatedly(
        DoAll(SetArgumentPointee<0>(string("fake.mail@fake.com")),
        Return(true)));
  }

  if (is_rollback_allowed) {
    InSequence s;
    for (size_t i = 0; i < arraysize(kRollbackActionTypes); ++i) {
      EXPECT_CALL(*processor_,
                  EnqueueAction(Property(&AbstractAction::Type,
                                         kRollbackActionTypes[i])));
    }
    EXPECT_CALL(*processor_, StartProcessing());

    EXPECT_TRUE(attempter_.Rollback(true));
    loop_.PostTask(FROM_HERE,
                   base::Bind(&UpdateAttempterTest::RollbackTestVerify,
                              base::Unretained(this)));
  } else {
    EXPECT_FALSE(attempter_.Rollback(true));
    loop_.BreakLoop();
  }
}

void UpdateAttempterTest::RollbackTestVerify() {
  // Verifies the actions that were enqueued.
  EXPECT_EQ(&attempter_, processor_->delegate());
  EXPECT_EQ(arraysize(kRollbackActionTypes), attempter_.actions_.size());
  for (size_t i = 0; i < arraysize(kRollbackActionTypes); ++i) {
    EXPECT_EQ(kRollbackActionTypes[i], attempter_.actions_[i]->Type());
  }
  EXPECT_EQ(UPDATE_STATUS_ATTEMPTING_ROLLBACK, attempter_.status());
  InstallPlanAction* install_plan_action =
        dynamic_cast<InstallPlanAction*>(attempter_.actions_[0].get());
  InstallPlan* install_plan = install_plan_action->install_plan();
  // Matches fake_hardware.h -> rollback should move from kernel/boot device
  // pair to other pair.
  EXPECT_EQ(install_plan->install_path, string("/dev/sdz3"));
  EXPECT_EQ(install_plan->kernel_install_path, string("/dev/sdz2"));
  EXPECT_EQ(install_plan->powerwash_required, true);
  loop_.BreakLoop();
}

TEST_F(UpdateAttempterTest, UpdateTest) {
  UpdateTestStart();
  loop_.Run();
}

TEST_F(UpdateAttempterTest, RollbackTest) {
  loop_.PostTask(FROM_HERE,
                 base::Bind(&UpdateAttempterTest::RollbackTestStart,
                            base::Unretained(this),
                            false, true));
  loop_.Run();
}

TEST_F(UpdateAttempterTest, InvalidSlotRollbackTest) {
  loop_.PostTask(FROM_HERE,
                 base::Bind(&UpdateAttempterTest::RollbackTestStart,
                            base::Unretained(this),
                            false, false));
  loop_.Run();
}

TEST_F(UpdateAttempterTest, EnterpriseRollbackTest) {
  loop_.PostTask(FROM_HERE,
                 base::Bind(&UpdateAttempterTest::RollbackTestStart,
                            base::Unretained(this),
                            true, true));
  loop_.Run();
}

void UpdateAttempterTest::PingOmahaTestStart() {
  EXPECT_CALL(*processor_,
              EnqueueAction(Property(&AbstractAction::Type,
                                     OmahaRequestAction::StaticType())));
  EXPECT_CALL(*processor_, StartProcessing());
  attempter_.PingOmaha();
  ScheduleQuitMainLoop();
}

TEST_F(UpdateAttempterTest, PingOmahaTest) {
  EXPECT_FALSE(attempter_.waiting_for_scheduled_check_);
  EXPECT_FALSE(attempter_.schedule_updates_called());
  // Disable scheduling of subsequnet checks; we're using the DefaultPolicy in
  // testing, which is more permissive than we want to handle here.
  attempter_.DisableScheduleUpdates();
  loop_.PostTask(FROM_HERE,
                 base::Bind(&UpdateAttempterTest::PingOmahaTestStart,
                            base::Unretained(this)));
  chromeos::MessageLoopRunMaxIterations(&loop_, 100);
  EXPECT_EQ(UPDATE_STATUS_UPDATED_NEED_REBOOT, attempter_.status());
  EXPECT_TRUE(attempter_.schedule_updates_called());
}

TEST_F(UpdateAttempterTest, CreatePendingErrorEventTest) {
  MockAction action;
  const ErrorCode kCode = ErrorCode::kDownloadTransferError;
  attempter_.CreatePendingErrorEvent(&action, kCode);
  ASSERT_NE(nullptr, attempter_.error_event_.get());
  EXPECT_EQ(OmahaEvent::kTypeUpdateComplete, attempter_.error_event_->type);
  EXPECT_EQ(OmahaEvent::kResultError, attempter_.error_event_->result);
  EXPECT_EQ(
      static_cast<ErrorCode>(static_cast<int>(kCode) |
                             static_cast<int>(ErrorCode::kTestOmahaUrlFlag)),
      attempter_.error_event_->error_code);
}

TEST_F(UpdateAttempterTest, CreatePendingErrorEventResumedTest) {
  OmahaResponseHandlerAction *response_action =
      new OmahaResponseHandlerAction(&fake_system_state_);
  response_action->install_plan_.is_resume = true;
  attempter_.response_handler_action_.reset(response_action);
  MockAction action;
  const ErrorCode kCode = ErrorCode::kInstallDeviceOpenError;
  attempter_.CreatePendingErrorEvent(&action, kCode);
  ASSERT_NE(nullptr, attempter_.error_event_.get());
  EXPECT_EQ(OmahaEvent::kTypeUpdateComplete, attempter_.error_event_->type);
  EXPECT_EQ(OmahaEvent::kResultError, attempter_.error_event_->result);
  EXPECT_EQ(
      static_cast<ErrorCode>(
          static_cast<int>(kCode) |
          static_cast<int>(ErrorCode::kResumedFlag) |
          static_cast<int>(ErrorCode::kTestOmahaUrlFlag)),
      attempter_.error_event_->error_code);
}

TEST_F(UpdateAttempterTest, P2PNotStartedAtStartupWhenNotEnabled) {
  MockP2PManager mock_p2p_manager;
  fake_system_state_.set_p2p_manager(&mock_p2p_manager);
  mock_p2p_manager.fake().SetP2PEnabled(false);
  EXPECT_CALL(mock_p2p_manager, EnsureP2PRunning()).Times(0);
  attempter_.UpdateEngineStarted();
}

TEST_F(UpdateAttempterTest, P2PNotStartedAtStartupWhenEnabledButNotSharing) {
  MockP2PManager mock_p2p_manager;
  fake_system_state_.set_p2p_manager(&mock_p2p_manager);
  mock_p2p_manager.fake().SetP2PEnabled(true);
  EXPECT_CALL(mock_p2p_manager, EnsureP2PRunning()).Times(0);
  attempter_.UpdateEngineStarted();
}

TEST_F(UpdateAttempterTest, P2PStartedAtStartupWhenEnabledAndSharing) {
  MockP2PManager mock_p2p_manager;
  fake_system_state_.set_p2p_manager(&mock_p2p_manager);
  mock_p2p_manager.fake().SetP2PEnabled(true);
  mock_p2p_manager.fake().SetCountSharedFilesResult(1);
  EXPECT_CALL(mock_p2p_manager, EnsureP2PRunning());
  attempter_.UpdateEngineStarted();
}

TEST_F(UpdateAttempterTest, P2PNotEnabled) {
  loop_.PostTask(FROM_HERE,
                 base::Bind(&UpdateAttempterTest::P2PNotEnabledStart,
                            base::Unretained(this)));
  loop_.Run();
}

void UpdateAttempterTest::P2PNotEnabledStart() {
  // If P2P is not enabled, check that we do not attempt housekeeping
  // and do not convey that p2p is to be used.
  MockP2PManager mock_p2p_manager;
  fake_system_state_.set_p2p_manager(&mock_p2p_manager);
  mock_p2p_manager.fake().SetP2PEnabled(false);
  EXPECT_CALL(mock_p2p_manager, PerformHousekeeping()).Times(0);
  attempter_.Update("", "", "", "", false, false);
  EXPECT_FALSE(actual_using_p2p_for_downloading_);
  EXPECT_FALSE(actual_using_p2p_for_sharing());
  ScheduleQuitMainLoop();
}

TEST_F(UpdateAttempterTest, P2PEnabledStartingFails) {
  loop_.PostTask(FROM_HERE,
                 base::Bind(&UpdateAttempterTest::P2PEnabledStartingFailsStart,
                            base::Unretained(this)));
  loop_.Run();
}

void UpdateAttempterTest::P2PEnabledStartingFailsStart() {
  // If p2p is enabled, but starting it fails ensure we don't do
  // any housekeeping and do not convey that p2p should be used.
  MockP2PManager mock_p2p_manager;
  fake_system_state_.set_p2p_manager(&mock_p2p_manager);
  mock_p2p_manager.fake().SetP2PEnabled(true);
  mock_p2p_manager.fake().SetEnsureP2PRunningResult(false);
  mock_p2p_manager.fake().SetPerformHousekeepingResult(false);
  EXPECT_CALL(mock_p2p_manager, PerformHousekeeping()).Times(0);
  attempter_.Update("", "", "", "", false, false);
  EXPECT_FALSE(actual_using_p2p_for_downloading());
  EXPECT_FALSE(actual_using_p2p_for_sharing());
  ScheduleQuitMainLoop();
}

TEST_F(UpdateAttempterTest, P2PEnabledHousekeepingFails) {
  loop_.PostTask(
      FROM_HERE,
      base::Bind(&UpdateAttempterTest::P2PEnabledHousekeepingFailsStart,
                 base::Unretained(this)));
  loop_.Run();
}

void UpdateAttempterTest::P2PEnabledHousekeepingFailsStart() {
  // If p2p is enabled, starting it works but housekeeping fails, ensure
  // we do not convey p2p is to be used.
  MockP2PManager mock_p2p_manager;
  fake_system_state_.set_p2p_manager(&mock_p2p_manager);
  mock_p2p_manager.fake().SetP2PEnabled(true);
  mock_p2p_manager.fake().SetEnsureP2PRunningResult(true);
  mock_p2p_manager.fake().SetPerformHousekeepingResult(false);
  EXPECT_CALL(mock_p2p_manager, PerformHousekeeping());
  attempter_.Update("", "", "", "", false, false);
  EXPECT_FALSE(actual_using_p2p_for_downloading());
  EXPECT_FALSE(actual_using_p2p_for_sharing());
  ScheduleQuitMainLoop();
}

TEST_F(UpdateAttempterTest, P2PEnabled) {
  loop_.PostTask(FROM_HERE,
                 base::Bind(&UpdateAttempterTest::P2PEnabledStart,
                            base::Unretained(this)));
  loop_.Run();
}

void UpdateAttempterTest::P2PEnabledStart() {
  MockP2PManager mock_p2p_manager;
  fake_system_state_.set_p2p_manager(&mock_p2p_manager);
  // If P2P is enabled and starting it works, check that we performed
  // housekeeping and that we convey p2p should be used.
  mock_p2p_manager.fake().SetP2PEnabled(true);
  mock_p2p_manager.fake().SetEnsureP2PRunningResult(true);
  mock_p2p_manager.fake().SetPerformHousekeepingResult(true);
  EXPECT_CALL(mock_p2p_manager, PerformHousekeeping());
  attempter_.Update("", "", "", "", false, false);
  EXPECT_TRUE(actual_using_p2p_for_downloading());
  EXPECT_TRUE(actual_using_p2p_for_sharing());
  ScheduleQuitMainLoop();
}

TEST_F(UpdateAttempterTest, P2PEnabledInteractive) {
  loop_.PostTask(FROM_HERE,
                 base::Bind(&UpdateAttempterTest::P2PEnabledInteractiveStart,
                            base::Unretained(this)));
  loop_.Run();
}

void UpdateAttempterTest::P2PEnabledInteractiveStart() {
  MockP2PManager mock_p2p_manager;
  fake_system_state_.set_p2p_manager(&mock_p2p_manager);
  // For an interactive check, if P2P is enabled and starting it
  // works, check that we performed housekeeping and that we convey
  // p2p should be used for sharing but NOT for downloading.
  mock_p2p_manager.fake().SetP2PEnabled(true);
  mock_p2p_manager.fake().SetEnsureP2PRunningResult(true);
  mock_p2p_manager.fake().SetPerformHousekeepingResult(true);
  EXPECT_CALL(mock_p2p_manager, PerformHousekeeping());
  attempter_.Update("", "", "", "", false, true /* interactive */);
  EXPECT_FALSE(actual_using_p2p_for_downloading());
  EXPECT_TRUE(actual_using_p2p_for_sharing());
  ScheduleQuitMainLoop();
}

TEST_F(UpdateAttempterTest, ReadScatterFactorFromPolicy) {
  loop_.PostTask(
      FROM_HERE,
      base::Bind(&UpdateAttempterTest::ReadScatterFactorFromPolicyTestStart,
                 base::Unretained(this)));
  loop_.Run();
}

// Tests that the scatter_factor_in_seconds value is properly fetched
// from the device policy.
void UpdateAttempterTest::ReadScatterFactorFromPolicyTestStart() {
  int64_t scatter_factor_in_seconds = 36000;

  policy::MockDevicePolicy* device_policy = new policy::MockDevicePolicy();
  attempter_.policy_provider_.reset(new policy::PolicyProvider(device_policy));

  EXPECT_CALL(*device_policy, LoadPolicy()).WillRepeatedly(Return(true));
  fake_system_state_.set_device_policy(device_policy);

  EXPECT_CALL(*device_policy, GetScatterFactorInSeconds(_))
      .WillRepeatedly(DoAll(
          SetArgumentPointee<0>(scatter_factor_in_seconds),
          Return(true)));

  attempter_.Update("", "", "", "", false, false);
  EXPECT_EQ(scatter_factor_in_seconds, attempter_.scatter_factor_.InSeconds());

  ScheduleQuitMainLoop();
}

TEST_F(UpdateAttempterTest, DecrementUpdateCheckCountTest) {
  loop_.PostTask(
      FROM_HERE,
      base::Bind(&UpdateAttempterTest::DecrementUpdateCheckCountTestStart,
                 base::Unretained(this)));
  loop_.Run();
}

void UpdateAttempterTest::DecrementUpdateCheckCountTestStart() {
  // Tests that the scatter_factor_in_seconds value is properly fetched
  // from the device policy and is decremented if value > 0.
  int64_t initial_value = 5;
  FakePrefs fake_prefs;
  attempter_.prefs_ = &fake_prefs;

  fake_system_state_.fake_hardware()->SetIsOOBEComplete(Time::UnixEpoch());

  EXPECT_TRUE(fake_prefs.SetInt64(kPrefsUpdateCheckCount, initial_value));

  int64_t scatter_factor_in_seconds = 10;

  policy::MockDevicePolicy* device_policy = new policy::MockDevicePolicy();
  attempter_.policy_provider_.reset(new policy::PolicyProvider(device_policy));

  EXPECT_CALL(*device_policy, LoadPolicy()).WillRepeatedly(Return(true));
  fake_system_state_.set_device_policy(device_policy);

  EXPECT_CALL(*device_policy, GetScatterFactorInSeconds(_))
      .WillRepeatedly(DoAll(
          SetArgumentPointee<0>(scatter_factor_in_seconds),
          Return(true)));

  attempter_.Update("", "", "", "", false, false);
  EXPECT_EQ(scatter_factor_in_seconds, attempter_.scatter_factor_.InSeconds());

  // Make sure the file still exists.
  EXPECT_TRUE(fake_prefs.Exists(kPrefsUpdateCheckCount));

  int64_t new_value;
  EXPECT_TRUE(fake_prefs.GetInt64(kPrefsUpdateCheckCount, &new_value));
  EXPECT_EQ(initial_value - 1, new_value);

  EXPECT_TRUE(
      attempter_.omaha_request_params_->update_check_count_wait_enabled());

  // However, if the count is already 0, it's not decremented. Test that.
  initial_value = 0;
  EXPECT_TRUE(fake_prefs.SetInt64(kPrefsUpdateCheckCount, initial_value));
  attempter_.Update("", "", "", "", false, false);
  EXPECT_TRUE(fake_prefs.Exists(kPrefsUpdateCheckCount));
  EXPECT_TRUE(fake_prefs.GetInt64(kPrefsUpdateCheckCount, &new_value));
  EXPECT_EQ(initial_value, new_value);

  ScheduleQuitMainLoop();
}

TEST_F(UpdateAttempterTest, NoScatteringDoneDuringManualUpdateTestStart) {
  loop_.PostTask(FROM_HERE, base::Bind(
      &UpdateAttempterTest::NoScatteringDoneDuringManualUpdateTestStart,
      base::Unretained(this)));
  loop_.Run();
}

void UpdateAttempterTest::NoScatteringDoneDuringManualUpdateTestStart() {
  // Tests that no scattering logic is enabled if the update check
  // is manually done (as opposed to a scheduled update check)
  int64_t initial_value = 8;
  FakePrefs fake_prefs;
  attempter_.prefs_ = &fake_prefs;

  fake_system_state_.fake_hardware()->SetIsOOBEComplete(Time::UnixEpoch());
  fake_system_state_.set_prefs(&fake_prefs);

  EXPECT_TRUE(fake_prefs.SetInt64(kPrefsWallClockWaitPeriod, initial_value));
  EXPECT_TRUE(fake_prefs.SetInt64(kPrefsUpdateCheckCount, initial_value));

  // make sure scatter_factor is non-zero as scattering is disabled
  // otherwise.
  int64_t scatter_factor_in_seconds = 50;

  policy::MockDevicePolicy* device_policy = new policy::MockDevicePolicy();
  attempter_.policy_provider_.reset(new policy::PolicyProvider(device_policy));

  EXPECT_CALL(*device_policy, LoadPolicy()).WillRepeatedly(Return(true));
  fake_system_state_.set_device_policy(device_policy);

  EXPECT_CALL(*device_policy, GetScatterFactorInSeconds(_))
      .WillRepeatedly(DoAll(
          SetArgumentPointee<0>(scatter_factor_in_seconds),
          Return(true)));

  // Trigger an interactive check so we can test that scattering is disabled.
  attempter_.Update("", "", "", "", false, true);
  EXPECT_EQ(scatter_factor_in_seconds, attempter_.scatter_factor_.InSeconds());

  // Make sure scattering is disabled for manual (i.e. user initiated) update
  // checks and all artifacts are removed.
  EXPECT_FALSE(
      attempter_.omaha_request_params_->wall_clock_based_wait_enabled());
  EXPECT_FALSE(fake_prefs.Exists(kPrefsWallClockWaitPeriod));
  EXPECT_EQ(0, attempter_.omaha_request_params_->waiting_period().InSeconds());
  EXPECT_FALSE(
      attempter_.omaha_request_params_->update_check_count_wait_enabled());
  EXPECT_FALSE(fake_prefs.Exists(kPrefsUpdateCheckCount));

  ScheduleQuitMainLoop();
}

// Checks that we only report daily metrics at most every 24 hours.
TEST_F(UpdateAttempterTest, ReportDailyMetrics) {
  FakeClock fake_clock;
  FakePrefs fake_prefs;

  fake_system_state_.set_clock(&fake_clock);
  fake_system_state_.set_prefs(&fake_prefs);

  Time epoch = Time::FromInternalValue(0);
  fake_clock.SetWallclockTime(epoch);

  // If there is no kPrefsDailyMetricsLastReportedAt state variable,
  // we should report.
  EXPECT_TRUE(attempter_.CheckAndReportDailyMetrics());
  // We should not report again if no time has passed.
  EXPECT_FALSE(attempter_.CheckAndReportDailyMetrics());

  // We should not report if only 10 hours has passed.
  fake_clock.SetWallclockTime(epoch + TimeDelta::FromHours(10));
  EXPECT_FALSE(attempter_.CheckAndReportDailyMetrics());

  // We should not report if only 24 hours - 1 sec has passed.
  fake_clock.SetWallclockTime(epoch + TimeDelta::FromHours(24) -
                              TimeDelta::FromSeconds(1));
  EXPECT_FALSE(attempter_.CheckAndReportDailyMetrics());

  // We should report if 24 hours has passed.
  fake_clock.SetWallclockTime(epoch + TimeDelta::FromHours(24));
  EXPECT_TRUE(attempter_.CheckAndReportDailyMetrics());

  // But then we should not report again..
  EXPECT_FALSE(attempter_.CheckAndReportDailyMetrics());

  // .. until another 24 hours has passed
  fake_clock.SetWallclockTime(epoch + TimeDelta::FromHours(47));
  EXPECT_FALSE(attempter_.CheckAndReportDailyMetrics());
  fake_clock.SetWallclockTime(epoch + TimeDelta::FromHours(48));
  EXPECT_TRUE(attempter_.CheckAndReportDailyMetrics());
  EXPECT_FALSE(attempter_.CheckAndReportDailyMetrics());

  // .. and another 24 hours
  fake_clock.SetWallclockTime(epoch + TimeDelta::FromHours(71));
  EXPECT_FALSE(attempter_.CheckAndReportDailyMetrics());
  fake_clock.SetWallclockTime(epoch + TimeDelta::FromHours(72));
  EXPECT_TRUE(attempter_.CheckAndReportDailyMetrics());
  EXPECT_FALSE(attempter_.CheckAndReportDailyMetrics());

  // If the span between time of reporting and present time is
  // negative, we report. This is in order to reset the timestamp and
  // avoid an edge condition whereby a distant point in the future is
  // in the state variable resulting in us never ever reporting again.
  fake_clock.SetWallclockTime(epoch + TimeDelta::FromHours(71));
  EXPECT_TRUE(attempter_.CheckAndReportDailyMetrics());
  EXPECT_FALSE(attempter_.CheckAndReportDailyMetrics());

  // In this case we should not update until the clock reads 71 + 24 = 95.
  // Check that.
  fake_clock.SetWallclockTime(epoch + TimeDelta::FromHours(94));
  EXPECT_FALSE(attempter_.CheckAndReportDailyMetrics());
  fake_clock.SetWallclockTime(epoch + TimeDelta::FromHours(95));
  EXPECT_TRUE(attempter_.CheckAndReportDailyMetrics());
  EXPECT_FALSE(attempter_.CheckAndReportDailyMetrics());
}

TEST_F(UpdateAttempterTest, BootTimeInUpdateMarkerFile) {
  const string update_completed_marker = test_dir_ + "/update-completed-marker";
  UpdateAttempterUnderTest attempter{&fake_system_state_,
                                     nullptr,  // libcros_proxy
                                     &debugd_proxy_mock_,
                                     update_completed_marker};

  FakeClock fake_clock;
  fake_clock.SetBootTime(Time::FromTimeT(42));
  fake_system_state_.set_clock(&fake_clock);

  Time boot_time;
  EXPECT_FALSE(attempter.GetBootTimeAtUpdate(&boot_time));

  attempter.WriteUpdateCompletedMarker();

  EXPECT_TRUE(attempter.GetBootTimeAtUpdate(&boot_time));
  EXPECT_EQ(boot_time.ToTimeT(), 42);
}

TEST_F(UpdateAttempterTest, AnyUpdateSourceAllowedUnofficial) {
  fake_system_state_.fake_hardware()->SetIsOfficialBuild(false);
  EXPECT_TRUE(attempter_.IsAnyUpdateSourceAllowed());
}

TEST_F(UpdateAttempterTest, AnyUpdateSourceAllowedOfficialDevmode) {
  fake_system_state_.fake_hardware()->SetIsOfficialBuild(true);
  fake_system_state_.fake_hardware()->SetIsNormalBootMode(false);
  EXPECT_CALL(debugd_proxy_mock_, QueryDevFeatures(_, _, _))
      .WillRepeatedly(DoAll(SetArgumentPointee<0>(0), Return(true)));
  EXPECT_TRUE(attempter_.IsAnyUpdateSourceAllowed());
}

TEST_F(UpdateAttempterTest, AnyUpdateSourceDisallowedOfficialNormal) {
  fake_system_state_.fake_hardware()->SetIsOfficialBuild(true);
  fake_system_state_.fake_hardware()->SetIsNormalBootMode(true);
  // debugd should not be queried in this case.
  EXPECT_CALL(debugd_proxy_mock_, QueryDevFeatures(_, _, _)).Times(0);
  EXPECT_FALSE(attempter_.IsAnyUpdateSourceAllowed());
}

TEST_F(UpdateAttempterTest, AnyUpdateSourceDisallowedDebugdDisabled) {
  using debugd::DEV_FEATURES_DISABLED;
  fake_system_state_.fake_hardware()->SetIsOfficialBuild(true);
  fake_system_state_.fake_hardware()->SetIsNormalBootMode(false);
  EXPECT_CALL(debugd_proxy_mock_, QueryDevFeatures(_, _, _))
      .WillRepeatedly(
          DoAll(SetArgumentPointee<0>(DEV_FEATURES_DISABLED), Return(true)));
  EXPECT_FALSE(attempter_.IsAnyUpdateSourceAllowed());
}

TEST_F(UpdateAttempterTest, AnyUpdateSourceDisallowedDebugdFailure) {
  fake_system_state_.fake_hardware()->SetIsOfficialBuild(true);
  fake_system_state_.fake_hardware()->SetIsNormalBootMode(false);
  EXPECT_CALL(debugd_proxy_mock_, QueryDevFeatures(_, _, _))
      .WillRepeatedly(Return(false));
  EXPECT_FALSE(attempter_.IsAnyUpdateSourceAllowed());
}

TEST_F(UpdateAttempterTest, CheckForUpdateAUTest) {
  fake_system_state_.fake_hardware()->SetIsOfficialBuild(true);
  fake_system_state_.fake_hardware()->SetIsNormalBootMode(true);
  attempter_.CheckForUpdate("", "autest", true);
  EXPECT_EQ(chromeos_update_engine::kAUTestOmahaUrl,
            attempter_.forced_omaha_url());
}

TEST_F(UpdateAttempterTest, CheckForUpdateScheduledAUTest) {
  fake_system_state_.fake_hardware()->SetIsOfficialBuild(true);
  fake_system_state_.fake_hardware()->SetIsNormalBootMode(true);
  attempter_.CheckForUpdate("", "autest-scheduled", true);
  EXPECT_EQ(chromeos_update_engine::kAUTestOmahaUrl,
            attempter_.forced_omaha_url());
}

}  // namespace chromeos_update_engine
