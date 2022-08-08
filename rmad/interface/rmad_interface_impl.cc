// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/interface/rmad_interface_impl.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/memory/scoped_refptr.h>
#include <base/time/time.h>
#include <base/values.h>

#include "rmad/constants.h"
#include "rmad/metrics/metrics_utils_impl.h"
#include "rmad/proto_bindings/rmad.pb.h"
#include "rmad/system/fake_runtime_probe_client.h"
#include "rmad/system/fake_shill_client.h"
#include "rmad/system/fake_tpm_manager_client.h"
#include "rmad/system/power_manager_client_impl.h"
#include "rmad/system/runtime_probe_client_impl.h"
#include "rmad/system/shill_client_impl.h"
#include "rmad/system/tpm_manager_client_impl.h"
#include "rmad/utils/cmd_utils_impl.h"
#include "rmad/utils/dbus_utils.h"
#include "rmad/utils/fake_cmd_utils.h"

namespace rmad {

namespace {

const RmadState::StateCase kInitialStateCase = RmadState::kWelcome;

const char kCroslogCmd[] = "/usr/sbin/croslog";

const char kInitctlCmd[] = "/sbin/initctl";
const std::vector<std::string> kWaitServices = {"system-services"};
const int kWaitServicesPollInterval = 1;  // 1 second.
const int kWaitServicesRetries = 10;

constexpr char kMetricsSummaryDivider[] = "\n====================\n\n";

}  // namespace

RmadInterfaceImpl::RmadInterfaceImpl()
    : RmadInterface(),
      external_utils_initialized_(false),
      current_state_case_(RmadState::STATE_NOT_SET),
      test_mode_(false) {}

RmadInterfaceImpl::RmadInterfaceImpl(
    scoped_refptr<JsonStore> json_store,
    std::unique_ptr<StateHandlerManager> state_handler_manager,
    std::unique_ptr<RuntimeProbeClient> runtime_probe_client,
    std::unique_ptr<ShillClient> shill_client,
    std::unique_ptr<TpmManagerClient> tpm_manager_client,
    std::unique_ptr<PowerManagerClient> power_manager_client,
    std::unique_ptr<CmdUtils> cmd_utils,
    std::unique_ptr<MetricsUtils> metrics_utils)
    : RmadInterface(),
      json_store_(json_store),
      state_handler_manager_(std::move(state_handler_manager)),
      runtime_probe_client_(std::move(runtime_probe_client)),
      shill_client_(std::move(shill_client)),
      tpm_manager_client_(std::move(tpm_manager_client)),
      power_manager_client_(std::move(power_manager_client)),
      cmd_utils_(std::move(cmd_utils)),
      metrics_utils_(std::move(metrics_utils)),
      external_utils_initialized_(true),
      current_state_case_(RmadState::STATE_NOT_SET),
      test_mode_(false) {}

bool RmadInterfaceImpl::StoreStateHistory() {
  std::vector<int> state_history;
  for (auto s : state_history_) {
    state_history.push_back(RmadState::StateCase(s));
  }
  return json_store_->SetValue(kStateHistory, state_history);
}

void RmadInterfaceImpl::InitializeExternalUtils(
    scoped_refptr<DaemonCallback> daemon_callback) {
  json_store_ = base::MakeRefCounted<JsonStore>(
      base::FilePath(kDefaultJsonStoreFilePath));
  state_handler_manager_ = std::make_unique<StateHandlerManager>(json_store_);
  if (test_mode_) {
    state_handler_manager_->RegisterFakeStateHandlers(daemon_callback);
    const base::FilePath test_dir_path =
        base::FilePath(kDefaultWorkingDirPath).AppendASCII(kTestDirPath);
    runtime_probe_client_ = std::make_unique<fake::FakeRuntimeProbeClient>();
    shill_client_ = std::make_unique<fake::FakeShillClient>();
    tpm_manager_client_ =
        std::make_unique<fake::FakeTpmManagerClient>(test_dir_path);
    // Still use the real power_manager.
    power_manager_client_ =
        std::make_unique<PowerManagerClientImpl>(GetSystemBus());
    cmd_utils_ = std::make_unique<fake::FakeCmdUtils>();
  } else {
    state_handler_manager_->RegisterStateHandlers(daemon_callback);
    runtime_probe_client_ =
        std::make_unique<RuntimeProbeClientImpl>(GetSystemBus());
    shill_client_ = std::make_unique<ShillClientImpl>(GetSystemBus());
    tpm_manager_client_ =
        std::make_unique<TpmManagerClientImpl>(GetSystemBus());
    power_manager_client_ =
        std::make_unique<PowerManagerClientImpl>(GetSystemBus());
    cmd_utils_ = std::make_unique<CmdUtilsImpl>();
  }
}

bool RmadInterfaceImpl::WaitForServices() {
  if (test_mode_) {
    return true;
  }
  CHECK(external_utils_initialized_);
  std::string output;
  for (int i = 0; i < kWaitServicesRetries; ++i) {
    LOG(INFO) << "Checking services";
    bool all_running = true;
    for (const std::string& service : kWaitServices) {
      cmd_utils_->GetOutput({kInitctlCmd, "status", service}, &output);
      if (output.find("running") == std::string::npos) {
        all_running = false;
        break;
      }
    }
    if (all_running) {
      return true;
    }
    sleep(kWaitServicesPollInterval);
  }
  return false;
}

bool RmadInterfaceImpl::StartFromInitialState() {
  current_state_case_ = kInitialStateCase;
  state_history_.push_back(current_state_case_);
  if (!MetricsUtils::UpdateStateMetricsOnStateTransition(
          json_store_, RmadState::STATE_NOT_SET, kInitialStateCase,
          base::Time::Now().ToDoubleT())) {
    LOG(ERROR) << "Failed to initialize timestamp for the initial state.";
    // TODO(genechang): Send a signal to Chrome that the json store failed
    //                  so a message can be displayed.
  }
  if (!StoreStateHistory()) {
    LOG(ERROR) << "Could not store initial state";
    // TODO(chenghan): Send a signal to Chrome that the json store failed so
    //                 a message can be displayed.
    return false;
  }
  return true;
}

bool RmadInterfaceImpl::SetUp(scoped_refptr<DaemonCallback> daemon_callback) {
  // Initialize external utilities if needed.
  if (!external_utils_initialized_) {
    InitializeExternalUtils(daemon_callback);
    external_utils_initialized_ = true;
    metrics_utils_ = std::make_unique<MetricsUtilsImpl>();
  }
  // Wait for system services to be ready.
  if (!WaitForServices()) {
    return false;
  }
  // Initialize |current state_|, |state_history_|, and |can_abort_| flag.
  current_state_case_ = RmadState::STATE_NOT_SET;
  state_history_.clear();
  can_abort_ = true;
  // Something's wrong with the state file. Try to clear it.
  if (json_store_->ReadOnly()) {
    LOG(WARNING) << "Corrupted RMA state file. Trying to fix it";
    if (!json_store_->Clear() || !json_store_->InitFromFile()) {
      LOG(ERROR) << "Failed to fix RMA state file";
      return false;
    }
  }
  DCHECK(!json_store_->ReadOnly());
  if (json_store_->GetReadError() != JsonStore::READ_ERROR_NO_SUCH_FILE) {
    if (std::vector<int> state_history;
        json_store_->GetReadError() == JsonStore::READ_ERROR_NONE &&
        json_store_->GetValue(kStateHistory, &state_history) &&
        state_history.size()) {
      for (int state : state_history) {
        // Reject any state that does not have a handler.
        if (RmadState::StateCase s = RmadState::StateCase(state);
            auto handler = state_handler_manager_->GetStateHandler(s)) {
          state_history_.push_back(s);
          can_abort_ &= handler->IsRepeatable();
        } else {
          // TODO(chenghan): Return to welcome screen with an error implying
          //                 an unsupported state.
          LOG(ERROR) << "Missing handler for state " << state << ".";
        }
      }
    }
    if (state_history_.size() > 0) {
      current_state_case_ = state_history_.back();
    } else {
      LOG(WARNING) << "Could not read state history from json store, reset to "
                      "initial state.";
      if (!StartFromInitialState()) {
        return false;
      }
    }
  } else if (RoVerificationStatus status;
             tpm_manager_client_->GetRoVerificationStatus(&status) &&
             (status == RoVerificationStatus::PASS ||
              status == RoVerificationStatus::UNSUPPORTED_TRIGGERED)) {
    VLOG(1) << "RO verification triggered";
    if (!StartFromInitialState()) {
      return false;
    }

    if (!json_store_->SetValue(kRoFirmwareVerified,
                               status == RoVerificationStatus::PASS)) {
      LOG(ERROR) << "Could not store RO firmware verification status";
    }
  }

  double current_timestamp = base::Time::Now().ToDoubleT();
  if (!MetricsUtils::SetMetricsValue(json_store_, kSetupTimestamp,
                                     current_timestamp)) {
    LOG(ERROR) << "Could not store setup time";
    return false;
  }
  if (double first_setup_time;
      !MetricsUtils::GetMetricsValue(json_store_, kFirstSetupTimestamp,
                                     &first_setup_time) &&
      !MetricsUtils::SetMetricsValue(json_store_, kFirstSetupTimestamp,
                                     current_timestamp)) {
    LOG(ERROR) << "Could not store first setup time";
    return false;
  }

  // If we are in the RMA process:
  //   1. Disable cellular to prevent accidentally using it.
  //   2. Start monitoring test files if we are running in test mode.
  // TODO(chenghan): Disable cellular in a separate thread to shorten the
  //                 response time.
  if (current_state_case_ != RmadState::STATE_NOT_SET) {
    if (ComponentsWithIdentifier components;
        runtime_probe_client_->ProbeCategories({RMAD_COMPONENT_CELLULAR},
                                               &components) &&
        components.size() > 0) {
      LOG(INFO) << "Disabling cellular network";
      CHECK(shill_client_->DisableCellular());
    }
    if (test_mode_) {
      ClearTestRequests();
      test_mode_monitor_timer_.Start(FROM_HERE, kTestModeMonitorInterval, this,
                                     &RmadInterfaceImpl::MonitorTestRequests);
    }
  }

  return true;
}

RmadErrorCode RmadInterfaceImpl::GetInitializedStateHandler(
    RmadState::StateCase state_case,
    scoped_refptr<BaseStateHandler>* state_handler) const {
  auto handler = state_handler_manager_->GetStateHandler(state_case);
  if (!handler) {
    LOG(INFO) << "No registered state handler for state " << state_case;
    return RMAD_ERROR_STATE_HANDLER_MISSING;
  }
  if (RmadErrorCode init_error = handler->InitializeState();
      init_error != RMAD_ERROR_OK) {
    LOG(INFO) << "Failed to initialize current state " << state_case;
    return init_error;
  }
  *state_handler = handler;
  return RMAD_ERROR_OK;
}

void RmadInterfaceImpl::TryTransitionNextStateFromCurrentState() {
  LOG(INFO) << "Trying a state transition using current state";
  TransitionNextStateInternal(TransitionNextStateRequest(), true);
}

void RmadInterfaceImpl::GetCurrentState(GetStateCallback callback) {
  GetStateReply reply = GetCurrentStateInternal();
  ReplyCallback(std::move(callback), reply);
}

GetStateReply RmadInterfaceImpl::GetCurrentStateInternal() {
  GetStateReply reply;
  scoped_refptr<BaseStateHandler> state_handler;

  if (current_state_case_ == RmadState::STATE_NOT_SET) {
    reply.set_error(RMAD_ERROR_RMA_NOT_REQUIRED);
  } else if (RmadErrorCode error = GetInitializedStateHandler(
                 current_state_case_, &state_handler);
             error != RMAD_ERROR_OK) {
    reply.set_error(error);
  } else {
    LOG(INFO) << "Get current state succeeded: " << current_state_case_;
    reply.set_error(RMAD_ERROR_OK);
    reply.set_allocated_state(new RmadState(state_handler->GetState(true)));
    reply.set_can_go_back(CanGoBack());
    reply.set_can_abort(CanAbort());
  }

  return reply;
}

void RmadInterfaceImpl::TransitionNextState(
    const TransitionNextStateRequest& request, GetStateCallback callback) {
  GetStateReply reply = TransitionNextStateInternal(request, false);
  ReplyCallback(std::move(callback), reply);
}

GetStateReply RmadInterfaceImpl::TransitionNextStateInternal(
    const TransitionNextStateRequest& request, bool try_at_boot) {
  GetStateReply reply;
  if (current_state_case_ == RmadState::STATE_NOT_SET) {
    reply.set_error(RMAD_ERROR_RMA_NOT_REQUIRED);
    return reply;
  }

  scoped_refptr<BaseStateHandler> current_state_handler, next_state_handler;
  if (RmadErrorCode error = GetInitializedStateHandler(current_state_case_,
                                                       &current_state_handler);
      error != RMAD_ERROR_OK) {
    LOG(ERROR) << "Current state initialization failed";
    reply.set_error(error);
    return reply;
  }

  // Initialize the default reply.
  reply.set_error(RMAD_ERROR_NOT_SET);
  reply.set_allocated_state(new RmadState(current_state_handler->GetState()));
  reply.set_can_go_back(CanGoBack());
  reply.set_can_abort(CanAbort());

  auto [next_state_case_error, next_state_case] =
      try_at_boot ? current_state_handler->TryGetNextStateCaseAtBoot()
                  : current_state_handler->GetNextStateCase(request.state());
  if (next_state_case_error != RMAD_ERROR_OK) {
    LOG(INFO) << "Transitioning to next state rejected by state "
              << current_state_case_;
    CHECK(next_state_case == current_state_case_)
        << "State transition should not happen with errors.";
    // Staying at the same state. Run it again.
    current_state_handler->RunState();
    reply.set_error(next_state_case_error);
    return reply;
  }

  CHECK(next_state_case != current_state_case_)
      << "Staying at the same state without errors.";

  if (RmadErrorCode error =
          GetInitializedStateHandler(next_state_case, &next_state_handler);
      error != RMAD_ERROR_OK) {
    // Staying at the same state. Run it again.
    current_state_handler->RunState();
    reply.set_error(error);
    return reply;
  }

  // Transition to next state.
  LOG(INFO) << "Transition to next state succeeded: from "
            << current_state_case_ << " to " << next_state_case;
  current_state_handler->CleanUpState();
  // Append next state to stack.
  state_history_.push_back(next_state_case);
  if (!StoreStateHistory()) {
    // TODO(chenghan): Add error replies when failed to write |json_store_|.
    LOG(ERROR) << "Could not store history";
  }

  // Update state metrics.
  if (!MetricsUtils::UpdateStateMetricsOnStateTransition(
          json_store_, current_state_case_, next_state_case,
          base::Time::Now().ToDoubleT())) {
    // TODO(genechang): Add error replies when failed to update state metrics
    //                  in |json_store| -> |metrics| -> |state_metrics|.
    LOG(ERROR) << "Could not update state metrics.";
  }

  // Update state and run it.
  current_state_case_ = next_state_case;
  next_state_handler->RunState();
  // This is a one-way transition. |can_abort| cannot go from false to
  // true, unless we restart the whole RMA process.
  can_abort_ &= next_state_handler->IsRepeatable();

  reply.set_error(RMAD_ERROR_OK);
  reply.set_allocated_state(new RmadState(next_state_handler->GetState(true)));
  reply.set_can_go_back(CanGoBack());
  reply.set_can_abort(CanAbort());
  return reply;
}

void RmadInterfaceImpl::TransitionPreviousState(GetStateCallback callback) {
  GetStateReply reply = TransitionPreviousStateInternal();
  ReplyCallback(std::move(callback), reply);
}

GetStateReply RmadInterfaceImpl::TransitionPreviousStateInternal() {
  GetStateReply reply;
  if (current_state_case_ == RmadState::STATE_NOT_SET) {
    reply.set_error(RMAD_ERROR_RMA_NOT_REQUIRED);
    return reply;
  }

  scoped_refptr<BaseStateHandler> current_state_handler, prev_state_handler;
  if (RmadErrorCode error = GetInitializedStateHandler(current_state_case_,
                                                       &current_state_handler);
      error != RMAD_ERROR_OK) {
    LOG(ERROR) << "Current state initialization failed";
    reply.set_error(error);
    return reply;
  }

  // Initialize the default reply.
  reply.set_error(RMAD_ERROR_NOT_SET);
  reply.set_allocated_state(new RmadState(current_state_handler->GetState()));
  reply.set_can_go_back(CanGoBack());
  reply.set_can_abort(CanAbort());

  if (!CanGoBack()) {
    LOG(INFO) << "Cannot go back to previous state";
    // Staying at the same state. Run it again.
    current_state_handler->RunState();
    reply.set_error(RMAD_ERROR_TRANSITION_FAILED);
    return reply;
  }

  RmadState::StateCase prev_state_case = *std::prev(state_history_.end(), 2);
  if (RmadErrorCode error =
          GetInitializedStateHandler(prev_state_case, &prev_state_handler);
      error != RMAD_ERROR_OK) {
    // Staying at the same state. Run it again.
    current_state_handler->RunState();
    reply.set_error(error);
    return reply;
  }

  // Transition to previous state.
  LOG(INFO) << "Transition to previous state succeeded: from "
            << current_state_case_ << " to " << prev_state_case;
  current_state_handler->CleanUpState();
  // Remove current state from stack.
  state_history_.pop_back();
  if (!StoreStateHistory()) {
    LOG(ERROR) << "Could not store history";
  }

  // Update state metrics.
  if (!MetricsUtils::UpdateStateMetricsOnStateTransition(
          json_store_, current_state_case_, prev_state_case,
          base::Time::Now().ToDoubleT())) {
    // TODO(genechang): Add error replies when failed to update state metrics
    //                  in |json_store| -> |metrics| -> |state_metrics|.
    LOG(ERROR) << "Could not update state metrics.";
  }

  // Update state and run it.
  current_state_case_ = prev_state_case;
  prev_state_handler->RunState();

  reply.set_error(RMAD_ERROR_OK);
  reply.set_allocated_state(new RmadState(prev_state_handler->GetState(true)));
  reply.set_can_go_back(CanGoBack());
  reply.set_can_abort(CanAbort());
  return reply;
}

void RmadInterfaceImpl::AbortRma(AbortRmaCallback callback) {
  AbortRmaReply reply;
  if (current_state_case_ == RmadState::STATE_NOT_SET) {
    reply.set_error(RMAD_ERROR_RMA_NOT_REQUIRED);
  } else if (can_abort_) {
    VLOG(1) << "AbortRma: Abort allowed.";
    if (!MetricsUtils::UpdateStateMetricsOnAbort(
            json_store_, current_state_case_, base::Time::Now().ToDoubleT())) {
      // TODO(genechang): Add error replies when failed to update state metrics
      //                  in |json_store| -> |metrics| -> |state_metrics|.
      LOG(ERROR) << "AbortRma: Failed to update state metrics.";
    }
    if (!metrics_utils_->Record(json_store_, false)) {
      // TODO(genechang): Add error replies when failed to record metrics.
      LOG(ERROR) << "AbortRma: Failed to generate and record metrics.";
    }
    if (json_store_->ClearAndDeleteFile()) {
      current_state_case_ = RmadState::STATE_NOT_SET;
      reply.set_error(RMAD_ERROR_RMA_NOT_REQUIRED);
    } else {
      LOG(ERROR) << "AbortRma: Failed to clear RMA state file";
      reply.set_error(RMAD_ERROR_ABORT_FAILED);
    }
  } else {
    VLOG(1) << "AbortRma: Failed to abort.";
    reply.set_error(RMAD_ERROR_ABORT_FAILED);
  }

  ReplyCallback(std::move(callback), reply);
}

void RmadInterfaceImpl::GetLog(GetLogCallback callback) {
  GetLogReply reply;
  std::string metrics_string =
      MetricsUtils::GetMetricsSummaryAsString(json_store_);
  std::string log_string;
  if (cmd_utils_->GetOutput({kCroslogCmd, "--identifier=rmad"}, &log_string)) {
    reply.set_error(RMAD_ERROR_OK);
    reply.set_log(metrics_string + kMetricsSummaryDivider + log_string);
    if (!MetricsUtils::UpdateStateMetricsOnGetLog(json_store_,
                                                  current_state_case_)) {
      // TODO(genechang): Add error replies when failed to update state metrics
      //                  in |json_store| -> |metrics| -> |state_metrics|.
      LOG(ERROR) << "GetLog: Failed to update state metrics.";
    }
  } else {
    LOG(ERROR) << "Failed to generate logs";
    reply.set_error(RMAD_ERROR_CANNOT_GET_LOG);
  }

  ReplyCallback(std::move(callback), reply);
}

void RmadInterfaceImpl::SaveLog(SaveLogCallback callback) {
  SaveLogReply reply;
  reply.set_error(RMAD_ERROR_OK);
  reply.set_save_path("fake_path");

  if (!MetricsUtils::UpdateStateMetricsOnSaveLog(json_store_,
                                                 current_state_case_)) {
    // TODO(genechang): Add error replies when failed to update state metrics
    //                  in |json_store| -> |metrics| -> |state_metrics|.
    LOG(ERROR) << "SaveLog: Failed to update state metrics.";
  }
  ReplyCallback(std::move(callback), reply);
}

void RmadInterfaceImpl::RecordBrowserActionMetric(
    const RecordBrowserActionMetricRequest& browser_action,
    RecordBrowserActionMetricCallback callback) {
  std::vector<std::string> additional_activities;
  // Ignore the return value, since it may not have been set yet.
  MetricsUtils::GetMetricsValue(json_store_, kAdditionalActivities,
                                &additional_activities);

  // TODO(genechang): Add a table to map all actions to metrics to simplify it.
  if (browser_action.diagnostics()) {
    additional_activities.push_back(
        AdditionalActivity_Name(RMAD_ADDITIONAL_ACTIVITY_DIAGNOSTICS));
  }

  if (browser_action.os_update()) {
    additional_activities.push_back(
        AdditionalActivity_Name(RMAD_ADDITIONAL_ACTIVITY_OS_UPDATE));
  }

  RecordBrowserActionMetricReply reply;
  if (MetricsUtils::SetMetricsValue(json_store_, kAdditionalActivities,
                                    additional_activities)) {
    reply.set_error(RMAD_ERROR_OK);
  } else {
    reply.set_error(RMAD_ERROR_CANNOT_RECORD_BROWSER_ACTION);
  }

  ReplyCallback(std::move(callback), reply);
}

bool RmadInterfaceImpl::CanGoBack() const {
  if (state_history_.size() > 1) {
    const auto current_state_handler =
        state_handler_manager_->GetStateHandler(state_history_.back());
    const auto prev_state_handler = state_handler_manager_->GetStateHandler(
        *std::prev(state_history_.end(), 2));
    CHECK(current_state_handler);
    CHECK(prev_state_handler);
    return (current_state_handler->IsRepeatable() &&
            prev_state_handler->IsRepeatable());
  }
  return false;
}

void RmadInterfaceImpl::ClearTestRequests() {
  // Check if powerwash or cutoff is requested in test mode. The files are
  // created in the test directory so they are not picked up by the init script
  // rmad.conf.
  const base::FilePath test_dir_path =
      base::FilePath(kDefaultWorkingDirPath).AppendASCII(kTestDirPath);
  // Check if powerwash is requested.
  const base::FilePath powerwash_request_file_path =
      test_dir_path.AppendASCII(kPowerwashRequestFilePath);
  if (base::PathExists(powerwash_request_file_path)) {
    base::DeleteFile(powerwash_request_file_path);
    LOG(INFO) << "Powerwash requested and ignored";
  }
  // Check if cutoff is requested.
  const base::FilePath cutoff_request_file_path =
      test_dir_path.AppendASCII(kCutoffRequestFilePath);
  if (base::PathExists(cutoff_request_file_path)) {
    base::DeleteFile(cutoff_request_file_path);
    LOG(INFO) << "Cutoff requested and ignored";
  }
}

void RmadInterfaceImpl::MonitorTestRequests() {
  const base::FilePath test_dir_path =
      base::FilePath(kDefaultWorkingDirPath).AppendASCII(kTestDirPath);
  // Check if reboot is requested.
  const base::FilePath reboot_request_file_path =
      test_dir_path.AppendASCII(fake::kRebootRequestFilePath);
  if (base::PathExists(reboot_request_file_path)) {
    base::DeleteFile(reboot_request_file_path);
    power_manager_client_->Restart();
  }
  // Check if shutdown is requested.
  const base::FilePath shutdown_request_file_path =
      test_dir_path.AppendASCII(fake::kShutdownRequestFilePath);
  if (base::PathExists(shutdown_request_file_path)) {
    base::DeleteFile(shutdown_request_file_path);
    power_manager_client_->Shutdown();
  }
}

}  // namespace rmad
