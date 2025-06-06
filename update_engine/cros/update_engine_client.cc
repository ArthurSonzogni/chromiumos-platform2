// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <sysexits.h>
#include <unistd.h>

#include <memory>
#include <string>
#include <vector>

#include <base/command_line.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/task/single_thread_task_runner.h>
#include <base/threading/platform_thread.h>
#include <base/time/time.h>
#include <brillo/daemons/daemon.h>
#include <brillo/flag_helper.h>
#include <brillo/key_value_store.h>

#include "update_engine/client.h"
#include "update_engine/common/error_code.h"
#include "update_engine/common/error_code_utils.h"
#include "update_engine/cros/omaha_utils.h"
#include "update_engine/status_update_handler.h"
#include "update_engine/update_status.h"
#include "update_engine/update_status_utils.h"

using brillo::KeyValueStore;
using chromeos_update_engine::DateToString;
using chromeos_update_engine::DateType;
using chromeos_update_engine::ErrorCode;
using chromeos_update_engine::UpdateEngineStatusToString;
using chromeos_update_engine::UpdateStatusToString;
using chromeos_update_engine::utils::ErrorCodeToString;
using std::string;
using std::unique_ptr;
using std::vector;
using update_engine::UpdateEngineStatus;
using update_engine::UpdateStatus;

namespace {

// Constant to signal that we need to continue running the daemon after
// initialization.
const int kContinueRunning = -1;

// The ShowStatus request will be retried `kShowStatusRetryCount` times at
// `kShowStatusRetryInterval` second intervals on failure.
const int kShowStatusRetryCount = 30;
constexpr base::TimeDelta kShowStatusRetryInterval = base::Seconds(2);

class UpdateEngineClient : public brillo::Daemon {
 public:
  UpdateEngineClient(int argc, char** argv) : argc_(argc), argv_(argv) {}
  UpdateEngineClient(const UpdateEngineClient&) = delete;
  UpdateEngineClient& operator=(const UpdateEngineClient&) = delete;

  ~UpdateEngineClient() override = default;

 protected:
  int OnInit() override {
    int ret = Daemon::OnInit();
    if (ret != EX_OK) {
      return ret;
    }

    client_ = update_engine::UpdateEngineClient::CreateInstance();

    if (!client_) {
      LOG(ERROR) << "UpdateEngineService not available.";
      return 1;
    }

    // We can't call QuitWithExitCode from OnInit(), so we delay the execution
    // of the ProcessFlags method after the Daemon initialization is done.
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&UpdateEngineClient::ProcessFlagsAndExit,
                                  base::Unretained(this)));
    return EX_OK;
  }

 private:
  // Show the status of the update engine in stdout.
  bool ShowStatus();

  // Return whether we need to reboot. 0 if reboot is needed, 1 if an error
  // occurred, 2 if no reboot is needed.
  int GetNeedReboot();

  // Main method that parses and triggers all the actions based on the passed
  // flags. Returns the exit code of the program of kContinueRunning if it
  // should not exit.
  int ProcessFlags();

  // Processes the flags and exits the program accordingly.
  void ProcessFlagsAndExit();

  // Copy of argc and argv passed to main().
  int argc_;
  char** argv_;

  // Library-based client
  unique_ptr<update_engine::UpdateEngineClient> client_;

  // Pointers to handlers for cleanup
  vector<unique_ptr<update_engine::StatusUpdateHandler>> handlers_;
};

class ExitingStatusUpdateHandler : public update_engine::StatusUpdateHandler {
 public:
  ~ExitingStatusUpdateHandler() override = default;

  void IPCError(const string& error) override;
};

void ExitingStatusUpdateHandler::IPCError(const string& error) {
  LOG(ERROR) << error;
  exit(1);
}

class WatchingStatusUpdateHandler : public ExitingStatusUpdateHandler {
 public:
  ~WatchingStatusUpdateHandler() override = default;

  void HandleStatusUpdate(const UpdateEngineStatus& status) override;
};

void WatchingStatusUpdateHandler::HandleStatusUpdate(
    const UpdateEngineStatus& status) {
  LOG(INFO) << "Got status update: " << UpdateEngineStatusToString(status);
}

bool UpdateEngineClient::ShowStatus() {
  UpdateEngineStatus status;
  int retry_count = kShowStatusRetryCount;
  while (retry_count > 0) {
    if (client_->GetStatus(&status)) {
      break;
    }
    if (--retry_count == 0) {
      return false;
    }
    LOG(WARNING)
        << "Failed to get the update_engine status. This can happen when the"
           " update_engine is busy doing a heavy operation or if the"
           " update-engine service is down. If it doesn't resolve, a restart of"
           " the update-engine service is needed."
           " Will try "
        << retry_count << " more times!";
    base::PlatformThread::Sleep(kShowStatusRetryInterval);
  }

  printf("%s", UpdateEngineStatusToString(status).c_str());

  return true;
}

int UpdateEngineClient::GetNeedReboot() {
  UpdateEngineStatus status;
  if (!client_->GetStatus(&status)) {
    return 1;
  }

  if (status.status == UpdateStatus::UPDATED_NEED_REBOOT) {
    return 0;
  }

  return 2;
}

class UpdateWaitHandler : public ExitingStatusUpdateHandler {
 public:
  explicit UpdateWaitHandler(bool exit_on_error,
                             update_engine::UpdateEngineClient* client)
      : exit_on_error_(exit_on_error), client_(client) {}

  ~UpdateWaitHandler() override = default;

  void HandleStatusUpdate(const UpdateEngineStatus& status) override;

 private:
  bool exit_on_error_;
  update_engine::UpdateEngineClient* client_;
};

void UpdateWaitHandler::HandleStatusUpdate(const UpdateEngineStatus& status) {
  if (exit_on_error_ && status.status == UpdateStatus::IDLE) {
    int last_attempt_error = static_cast<int>(ErrorCode::kSuccess);
    ErrorCode code = ErrorCode::kSuccess;
    if (client_ && client_->GetLastAttemptError(&last_attempt_error)) {
      code = static_cast<ErrorCode>(last_attempt_error);
    }

    LOG(ERROR) << "Update failed, current operation is "
               << UpdateStatusToString(status.status) << ", last error code is "
               << ErrorCodeToString(code) << "(" << last_attempt_error << ")";
    exit(1);
  }
  if (status.status == UpdateStatus::UPDATED_NEED_REBOOT) {
    LOG(INFO) << "Update succeeded -- reboot needed.";
    exit(0);
  }
}

class InstallWaitHandler : public ExitingStatusUpdateHandler {
 public:
  explicit InstallWaitHandler(update_engine::UpdateEngineClient* client)
      : client_(client) {}

  ~InstallWaitHandler() override = default;

  void HandleStatusUpdate(const UpdateEngineStatus& status) override;

 private:
  update_engine::UpdateEngineClient* client_;
};

void InstallWaitHandler::HandleStatusUpdate(const UpdateEngineStatus& status) {
  if (status.status == UpdateStatus::IDLE) {
    auto success = static_cast<int>(ErrorCode::kSuccess);
    auto last_attempt_error = success;
    ErrorCode code = ErrorCode::kSuccess;
    if (client_ && client_->GetLastAttemptError(&last_attempt_error)) {
      code = static_cast<ErrorCode>(last_attempt_error);
    }

    if (last_attempt_error == success) {
      LOG(INFO) << "Install succeeded.";
      exit(0);
    }
    LOG(ERROR) << "Install failed, current operation is "
               << UpdateStatusToString(status.status) << ", last error code is "
               << ErrorCodeToString(code) << "(" << last_attempt_error << ")";
    exit(1);
  }
}

int UpdateEngineClient::ProcessFlags() {
  DEFINE_string(app_version, "", "Force the current app version.");
  DEFINE_string(channel, "",
                "Set the target channel. The device will be powerwashed if the "
                "target channel is more stable than the current channel unless "
                "--nopowerwash is specified.");
  DEFINE_bool(check_for_update, false, "Initiate check for updates.");
  DEFINE_bool(apply_deferred_update, false,
              "Apply the deferred update if there is one.");
  DEFINE_string(cohort_hint, "",
                "Set the current cohort hint to the passed value.");
  DEFINE_string(dlc, "", "The ID/name of the DLC to install.");
  DEFINE_bool(follow, false,
              "Wait for any update operations to complete."
              "Exit status is 0 if the update succeeded, and 1 otherwise.");
  DEFINE_bool(install, false, "Set to perform an installation.");
  DEFINE_bool(scaled, false, "Set to perform a scaled installation.");
  DEFINE_bool(interactive, true, "Mark the update request as interactive.");
  DEFINE_string(omaha_url, "", "The URL of the Omaha update server.");
  DEFINE_string(p2p_update, "",
                "Enables (\"yes\") or disables (\"no\") the peer-to-peer update"
                " sharing.");
  DEFINE_bool(powerwash, true,
              "When performing rollback or channel change, "
              "do a powerwash or allow it respectively.");
  DEFINE_bool(reboot, false, "Initiate a reboot if needed.");
  DEFINE_bool(is_reboot_needed, false,
              "Exit status 0 if reboot is needed, "
              "2 if reboot is not needed or 1 if an error occurred.");
  DEFINE_bool(block_until_reboot_is_needed, false,
              "Blocks until reboot is "
              "needed. Returns non-zero exit status if an error occurred.");
  DEFINE_bool(reset_status, false, "Sets the status in update_engine to idle.");
  DEFINE_bool(rollback, false,
              "Perform a rollback to the previous partition. The device will "
              "be powerwashed unless --nopowerwash is specified.");
  DEFINE_bool(can_rollback, false,
              "Shows whether rollback partition "
              "is available.");
  DEFINE_bool(show_channel, false, "Show the current and target channels.");
  DEFINE_bool(show_cohort_hint, false, "Show the current cohort hint.");
  DEFINE_bool(show_p2p_update, false,
              "Show the current setting for peer-to-peer update sharing.");
  DEFINE_bool(show_update_over_cellular, false,
              "Show the current setting for updates over cellular networks.");
  DEFINE_bool(status, false, "Print the status to stdout.");
  DEFINE_bool(update, false,
              "Forces an update and waits for it to complete. "
              "Implies --follow.");
  DEFINE_string(update_over_cellular, "",
                "Enables (\"yes\") or disables (\"no\") the updates over "
                "cellular networks.");
  DEFINE_bool(watch_for_updates, false,
              "Listen for status updates and print them to the screen.");
  DEFINE_bool(prev_version, false,
              "Show the previous OS version used before the update reboot.");
  DEFINE_bool(last_attempt_error, false, "Show the last attempt error.");
  DEFINE_bool(eol_status, false, "Show the current end-of-life status.");
  DEFINE_string(
      enable_feature, "",
      "Give the name of the feature to enable, ex.\"feature-repeated-updates\" "
      "to continue checking for updates while waiting for reboot.");
  DEFINE_string(disable_feature, "",
                "Give the name of the feature to disable, "
                "ex.\"feature-repeated-updates\".");
  DEFINE_bool(skip_applying, false,
              "Skip applying updates, only check if there are updates.");
  DEFINE_string(is_feature_enabled, "", "Shows the current value of feature.");
  DEFINE_int32(set_status, -1,
               "Override status of the update engine with a value in"
               "Operation of update_engine.proto. Used for testing.");
  DEFINE_bool(force_fw_update, false,
              "Forces a fw update with the OS update check.");
  DEFINE_bool(migrate, false, "Set to perform a migration.");

  // Boilerplate init commands.
  base::CommandLine::Init(argc_, argv_);
  brillo::FlagHelper::Init(argc_, argv_, "A/B Update Engine Client");

  // Ensure there are no positional arguments.
  const vector<string> positional_args =
      base::CommandLine::ForCurrentProcess()->GetArgs();
  if (!positional_args.empty()) {
    LOG(ERROR) << "Found a positional argument '" << positional_args.front()
               << "'. If you want to pass a value to a flag, pass it as "
                  "--flag=value.";
    return 1;
  }

  if (FLAGS_set_status != -1) {
    const int32_t max_value = static_cast<int32_t>(UpdateStatus::MAX);
    if (FLAGS_set_status < 0 || FLAGS_set_status > max_value) {
      LOG(ERROR) << "Passed value is not a valid update state."
                 << "Needs to be between 0 and " << max_value << ".";
      return 1;
    }

    if (!client_->SetStatus(static_cast<UpdateStatus>(FLAGS_set_status))) {
      LOG(ERROR) << "Setting update status failed.";
      return 1;
    }
    LOG(INFO) << "Overriding update status to "
              << chromeos_update_engine::UpdateStatusToString(
                     static_cast<UpdateStatus>(FLAGS_set_status));
    return 0;
  }

  // Update the status if requested.
  if (FLAGS_reset_status) {
    LOG(INFO) << "Setting Update Engine status to idle ...";

    if (client_->ResetStatus()) {
      LOG(INFO) << "ResetStatus succeeded; to undo partition table changes "
                   "run:\n"
                   "(D=$(rootdev -s -d) P=$(rootdev -s); cgpt p -i$(($(echo "
                   "${P#$D} | sed 's/^[^0-9]*//')-1)) $D;)";
    } else {
      LOG(ERROR) << "ResetStatus failed";
      return 1;
    }
  }

  // Changes the current update over cellular network setting.
  if (!FLAGS_update_over_cellular.empty()) {
    bool allowed = FLAGS_update_over_cellular == "yes";
    if (!allowed && FLAGS_update_over_cellular != "no") {
      LOG(ERROR) << "Unknown option: \"" << FLAGS_update_over_cellular
                 << "\". Please specify \"yes\" or \"no\".";
    } else {
      if (!client_->SetUpdateOverCellularPermission(allowed)) {
        LOG(ERROR) << "Error setting the update over cellular setting.";
        return 1;
      }
    }
  }

  // Show the current update over cellular network setting.
  if (FLAGS_show_update_over_cellular) {
    bool allowed;

    if (!client_->GetUpdateOverCellularPermission(&allowed)) {
      LOG(ERROR) << "Error getting the update over cellular setting.";
      return 1;
    }

    LOG(INFO) << "Current update over cellular network setting: "
              << (allowed ? "ENABLED" : "DISABLED");
  }

  // Change/show the cohort hint.
  bool set_cohort_hint =
      base::CommandLine::ForCurrentProcess()->HasSwitch("cohort_hint");
  if (set_cohort_hint) {
    LOG(INFO) << "Setting cohort hint to: \"" << FLAGS_cohort_hint << "\"";
    if (!client_->SetCohortHint(FLAGS_cohort_hint)) {
      LOG(ERROR) << "Error setting the cohort hint.";
      return 1;
    }
  }

  if (FLAGS_show_cohort_hint || set_cohort_hint) {
    string cohort_hint;
    if (!client_->GetCohortHint(&cohort_hint)) {
      LOG(ERROR) << "Error getting the cohort hint.";
      return 1;
    }

    LOG(INFO) << "Current cohort hint: \"" << cohort_hint << "\"";
  }

  if (!FLAGS_powerwash && !FLAGS_rollback && FLAGS_channel.empty()) {
    LOG(ERROR) << "powerwash flag only makes sense rollback or channel change";
    return 1;
  }

  // Change the P2P enabled setting.
  if (!FLAGS_p2p_update.empty()) {
    bool enabled = FLAGS_p2p_update == "yes";
    if (!enabled && FLAGS_p2p_update != "no") {
      LOG(ERROR) << "Unknown option: \"" << FLAGS_p2p_update
                 << "\". Please specify \"yes\" or \"no\".";
    } else {
      if (!client_->SetP2PUpdatePermission(enabled)) {
        LOG(ERROR) << "Error setting the peer-to-peer update setting.";
        return 1;
      }
    }
  }

  // Show the rollback availability.
  if (FLAGS_can_rollback) {
    string rollback_partition;

    if (!client_->GetRollbackPartition(&rollback_partition)) {
      LOG(ERROR) << "Error while querying rollback partition availability.";
      return 1;
    }

    bool can_rollback = true;
    if (rollback_partition.empty()) {
      rollback_partition = "UNAVAILABLE";
      can_rollback = false;
    } else {
      rollback_partition = "AVAILABLE: " + rollback_partition;
    }

    LOG(INFO) << "Rollback partition: " << rollback_partition;
    if (!can_rollback) {
      return 1;
    }
  }

  // Show the current P2P enabled setting.
  if (FLAGS_show_p2p_update) {
    bool enabled;

    if (!client_->GetP2PUpdatePermission(&enabled)) {
      LOG(ERROR) << "Error getting the peer-to-peer update setting.";
      return 1;
    }

    LOG(INFO) << "Current update using P2P setting: "
              << (enabled ? "ENABLED" : "DISABLED");
  }

  // First, update the target channel if requested.
  if (!FLAGS_channel.empty()) {
    if (!client_->SetTargetChannel(FLAGS_channel, FLAGS_powerwash)) {
      LOG(ERROR) << "Error setting the channel.";
      return 1;
    }

    LOG(INFO) << "Channel permanently set to: " << FLAGS_channel;
  }

  // Show the current and target channels if requested.
  if (FLAGS_show_channel) {
    string current_channel;
    string target_channel;

    if (!client_->GetChannel(&current_channel)) {
      LOG(ERROR) << "Error getting the current channel.";
      return 1;
    }

    if (!client_->GetTargetChannel(&target_channel)) {
      LOG(ERROR) << "Error getting the target channel.";
      return 1;
    }

    LOG(INFO) << "Current Channel: " << current_channel;

    if (!target_channel.empty()) {
      LOG(INFO) << "Target Channel (pending update): " << target_channel;
    }
  }

  if (FLAGS_apply_deferred_update) {
    update_engine::ApplyUpdateConfig config;
    config.set_done_action(update_engine::UpdateDoneAction::REBOOT);
    if (!client_->ApplyDeferredUpdateAdvanced(config)) {
      LOG(ERROR) << "Apply deferred update failed.";
      return 1;
    }
    return 0;
  }

  if (FLAGS_install) {
    if (FLAGS_dlc.empty()) {
      LOG(ERROR) << "Must pass in a DLC when performing an install.";
      return 1;
    }

    update_engine::InstallParams install_params;
    install_params.set_id(FLAGS_dlc);
    install_params.set_omaha_url(FLAGS_omaha_url);
    install_params.set_scaled(FLAGS_scaled);

    if (!client_->Install(install_params)) {
      LOG(ERROR) << "Failed to install DLC=" << FLAGS_dlc;
      return 1;
    }

    LOG(INFO) << "Waiting for install to complete.";

    auto handler = new InstallWaitHandler(client_.get());
    handlers_.emplace_back(handler);
    client_->RegisterStatusUpdateHandler(handler);
    return kContinueRunning;
  }

  if (FLAGS_migrate) {
    if (!client_->Migrate()) {
      LOG(ERROR) << "Failed to perform the migration";
      return 1;
    }
    return 0;
  }

  bool do_update_request = FLAGS_check_for_update || FLAGS_update ||
                           !FLAGS_app_version.empty() ||
                           !FLAGS_omaha_url.empty();
  if (FLAGS_update) {
    FLAGS_follow = true;
  }

  if (do_update_request && FLAGS_rollback) {
    LOG(ERROR) << "Incompatible flags specified with rollback."
               << "Rollback should not include update-related flags.";
    return 1;
  }

  if (FLAGS_rollback) {
    LOG(INFO) << "Requesting rollback.";
    if (!client_->Rollback(FLAGS_powerwash)) {
      LOG(ERROR) << "Rollback request failed.";
      return 1;
    }
  }

  if (!FLAGS_enable_feature.empty() && !FLAGS_disable_feature.empty() &&
      FLAGS_enable_feature == FLAGS_disable_feature) {
    LOG(ERROR) << "Cannot both enable and disable feature: "
               << FLAGS_disable_feature;
    return 1;
  }

  if (!FLAGS_enable_feature.empty()) {
    LOG(INFO) << "Requesting to enable feature " << FLAGS_enable_feature;
    if (!client_->ToggleFeature(FLAGS_enable_feature, true)) {
      LOG(ERROR) << "Enabling feature failed.";
      return 1;
    }
  }

  if (!FLAGS_disable_feature.empty()) {
    LOG(INFO) << "Requesting to disable feature " << FLAGS_disable_feature;
    if (!client_->ToggleFeature(FLAGS_disable_feature, false)) {
      LOG(ERROR) << "Disabling feature failed.";
      return 1;
    }
  }

  if (!FLAGS_is_feature_enabled.empty()) {
    bool enabled = false;
    if (!client_->IsFeatureEnabled(FLAGS_is_feature_enabled, &enabled)) {
      LOG(ERROR) << "Could not retrieve feature value.";
      return 1;
    }
    printf("%s", enabled ? "true" : "false");
  }

  // Initiate an update check, if necessary.
  if (do_update_request) {
    LOG_IF(WARNING, FLAGS_reboot) << "-reboot flag ignored.";
    string app_version = FLAGS_app_version;
    if (FLAGS_update && app_version.empty()) {
      app_version = "ForcedUpdate";
      LOG(INFO) << "Forcing an update by setting app_version to ForcedUpdate.";
    }
    LOG(INFO) << "Initiating update check.";
    update_engine::UpdateParams update_params;
    update_params.set_app_version(app_version);
    update_params.set_omaha_url(FLAGS_omaha_url);
    update_params.set_skip_applying(FLAGS_skip_applying);
    update_params.mutable_update_flags()->set_non_interactive(
        !FLAGS_interactive);
    update_params.set_force_fw_update(FLAGS_force_fw_update);
    if (!client_->Update(update_params)) {
      LOG(ERROR) << "Error checking for update.";
      return 1;
    }
  }

  // These final options are all mutually exclusive with one another.
  if (FLAGS_follow + FLAGS_watch_for_updates + FLAGS_reboot + FLAGS_status +
          FLAGS_is_reboot_needed + FLAGS_block_until_reboot_is_needed >
      1) {
    LOG(ERROR) << "Multiple exclusive options selected. "
               << "Select only one of --follow, --watch_for_updates, --reboot, "
               << "--is_reboot_needed, --block_until_reboot_is_needed, "
               << "or --status.";
    return 1;
  }

  if (FLAGS_status) {
    LOG(INFO) << "Querying Update Engine status...";
    if (!ShowStatus()) {
      LOG(ERROR) << "Failed to query status";
      return 1;
    }
    return 0;
  }

  if (FLAGS_follow) {
    LOG(INFO) << "Waiting for update to complete.";
    auto handler = new UpdateWaitHandler(true, client_.get());
    handlers_.emplace_back(handler);
    client_->RegisterStatusUpdateHandler(handler);
    return kContinueRunning;
  }

  if (FLAGS_watch_for_updates) {
    LOG(INFO) << "Watching for status updates.";
    auto handler = new WatchingStatusUpdateHandler();
    handlers_.emplace_back(handler);
    client_->RegisterStatusUpdateHandler(handler);
    return kContinueRunning;
  }

  if (FLAGS_reboot) {
    LOG(INFO) << "Requesting a reboot...";
    client_->RebootIfNeeded();
    return 0;
  }

  if (FLAGS_prev_version) {
    string prev_version;

    if (!client_->GetPrevVersion(&prev_version)) {
      LOG(ERROR) << "Error getting previous version.";
    } else {
      LOG(INFO) << "Previous version = " << prev_version;
    }
  }

  if (FLAGS_is_reboot_needed) {
    int ret = GetNeedReboot();

    if (ret == 1) {
      LOG(ERROR) << "Could not query the current operation.";
    }

    return ret;
  }

  if (FLAGS_block_until_reboot_is_needed) {
    auto handler = new UpdateWaitHandler(false, nullptr);
    handlers_.emplace_back(handler);
    client_->RegisterStatusUpdateHandler(handler);
    return kContinueRunning;
  }

  if (FLAGS_last_attempt_error) {
    int last_attempt_error;
    if (!client_->GetLastAttemptError(&last_attempt_error)) {
      LOG(ERROR) << "Error getting last attempt error.";
    } else {
      ErrorCode code = static_cast<ErrorCode>(last_attempt_error);

      KeyValueStore last_attempt_error_store;
      last_attempt_error_store.SetString(
          "ERROR_CODE", base::NumberToString(last_attempt_error));
      last_attempt_error_store.SetString("ERROR_MESSAGE",
                                         ErrorCodeToString(code));
      printf("%s", last_attempt_error_store.SaveToString().c_str());
    }
  }

  if (FLAGS_eol_status) {
    UpdateEngineStatus status;
    if (!client_->GetStatus(&status)) {
      LOG(ERROR) << "Error GetStatus() for getting EOL info.";
    } else {
      DateType eol_date_code = status.eol_date;

      KeyValueStore eol_status_store;
      eol_status_store.SetString("EOL_DATE", DateToString(eol_date_code));
      printf("%s", eol_status_store.SaveToString().c_str());
    }
  }

  return 0;
}

void UpdateEngineClient::ProcessFlagsAndExit() {
  int ret = ProcessFlags();
  if (ret != kContinueRunning) {
    QuitWithExitCode(ret);
  }
}

}  // namespace

int main(int argc, char** argv) {
  UpdateEngineClient client(argc, argv);
  return client.Run();
}
