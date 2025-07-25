// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/cros/update_attempter.h"

#include <stdint.h>

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <base/base64.h>
#include <base/files/file_util.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/rand_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/time/time.h>
#include <brillo/data_encoding.h>
#include <brillo/errors/error_codes.h>
#include <brillo/message_loops/message_loop.h>
#include <chromeos/constants/imageloader.h>
#include <cros_installer/inst_util.h>
#include <policy/device_policy.h>
#include <policy/libpolicy.h>
#include <update_engine/dbus-constants.h>

#include "update_engine/certificate_checker.h"
#include "update_engine/common/boot_control_interface.h"
#include "update_engine/common/constants.h"
#include "update_engine/common/dlcservice_interface.h"
#include "update_engine/common/excluder_interface.h"
#include "update_engine/common/hardware_interface.h"
#include "update_engine/common/metrics_reporter_interface.h"
#include "update_engine/common/platform_constants.h"
#include "update_engine/common/prefs.h"
#include "update_engine/common/prefs_interface.h"
#include "update_engine/common/subprocess.h"
#include "update_engine/common/system_state.h"
#include "update_engine/common/utils.h"
#include "update_engine/cros/download_action_chromeos.h"
#include "update_engine/cros/install_action.h"
#include "update_engine/cros/metrics_reporter_omaha.h"
#include "update_engine/cros/omaha_request_action.h"
#include "update_engine/cros/omaha_request_params.h"
#include "update_engine/cros/omaha_response_handler_action.h"
#include "update_engine/cros/omaha_utils.h"
#include "update_engine/cros/p2p_manager.h"
#include "update_engine/cros/payload_state_interface.h"
#include "update_engine/cros/power_manager_interface.h"
#include "update_engine/libcurl_http_fetcher.h"
#include "update_engine/payload_consumer/filesystem_verifier_action.h"
#include "update_engine/payload_consumer/postinstall_runner_action.h"
#include "update_engine/update_boot_flags_action.h"
#include "update_engine/update_manager/enterprise_update_disabled_policy_impl.h"
#include "update_engine/update_manager/omaha_request_params_policy.h"
#include "update_engine/update_manager/update_manager.h"
#include "update_engine/update_status_utils.h"

using base::FilePath;
using base::Time;
using base::TimeDelta;
using base::TimeTicks;
using brillo::MessageLoop;
using chromeos_update_manager::CalculateStagingCase;
using chromeos_update_manager::EnterpriseUpdateDisabledPolicyImpl;
using chromeos_update_manager::EvalStatus;
using chromeos_update_manager::OmahaRequestParamsPolicy;
using chromeos_update_manager::StagingCase;
using chromeos_update_manager::UpdateCheckAllowedPolicy;
using chromeos_update_manager::UpdateCheckAllowedPolicyData;
using chromeos_update_manager::UpdateCheckParams;
using std::map;
using std::string;
using std::vector;
using update_engine::FeatureInternalList;
using update_engine::UpdateAttemptFlags;
using update_engine::UpdateEngineStatus;

namespace chromeos_update_engine {

const int UpdateAttempter::kMaxDeltaUpdateFailures = 3;

namespace {
const int kMaxConsecutiveObeyProxyRequests = 20;

// Minimum threshold to broadcast an status update in progress and time.
const double kBroadcastThresholdProgress = 0.01;  // 1%
constexpr TimeDelta kBroadcastThreshold = base::Seconds(10);

// By default autest bypasses scattering. If we want to test scattering,
// use kScheduledAUTestURLRequest. The URL used is same in both cases, but
// different params are passed to CheckForUpdate().
const char kAUTestURLRequest[] = "autest";
const char kScheduledAUTestURLRequest[] = "autest-scheduled";

const char kMigrationDlcId[] = "migration-dlc";

constexpr unsigned int kPartitionNumberBootA = 13;
constexpr char kPartitionNameBootA[] = "boot_a";
constexpr char kPartitionNameRoot[] = "root";

constexpr std::string_view kPartitionsAttributePrefix = "_PARTITIONS_";

string ConvertToString(ProcessMode op) {
  switch (op) {
    case ProcessMode::UPDATE:
      return "update";
    case ProcessMode::INSTALL:
      return "install";
    case ProcessMode::SCALED_INSTALL:
      return "scaled install";
    case ProcessMode::FORCE_OTA_INSTALL:
      return "force OTA install";
    case ProcessMode::MIGRATE:
      return "migration install";
  }
}

}  // namespace

ErrorCode GetErrorCodeForAction(AbstractAction* action, ErrorCode code) {
  if (code != ErrorCode::kError) {
    return code;
  }

  const string type = action->Type();
  if (type == OmahaRequestAction::StaticType()) {
    return ErrorCode::kOmahaRequestError;
  }
  if (type == OmahaResponseHandlerAction::StaticType()) {
    return ErrorCode::kOmahaResponseHandlerError;
  }
  if (type == FilesystemVerifierAction::StaticType()) {
    return ErrorCode::kFilesystemVerifierError;
  }
  if (type == PostinstallRunnerAction::StaticType()) {
    return ErrorCode::kPostinstallRunnerError;
  }

  return code;
}

UpdateAttempter::UpdateAttempter(CertificateChecker* cert_checker)
    : processor_(new ActionProcessor()),
      cert_checker_(cert_checker),
      rollback_metrics_(
          std::make_unique<oobe_config::EnterpriseRollbackMetricsHandler>()),
      weak_ptr_factory_(this) {}

UpdateAttempter::~UpdateAttempter() {
  // Prevent any DBus communication from UpdateAttempter when shutting down the
  // daemon.
  ClearObservers();

  // CertificateChecker might not be initialized in unittests.
  if (cert_checker_) {
    cert_checker_->SetObserver(nullptr);
  }
  // Release ourselves as the ActionProcessor's delegate to prevent
  // re-scheduling the updates due to the processing stopped.
  processor_->set_delegate(nullptr);
}

void UpdateAttempter::Init() {
  // Pulling from the SystemState can only be done after construction, since
  // this is an aggregate of various objects (such as the UpdateAttempter),
  // which requires them all to be constructed prior to it being used.
  prefs_ = SystemState::Get()->prefs();
  omaha_request_params_ = SystemState::Get()->request_params();
  excluder_ = CreateExcluder();

  if (cert_checker_) {
    cert_checker_->SetObserver(this);
  }

  // In case of update_engine restart without a reboot we need to restore the
  // reboot needed state.
  if (GetBootTimeAtUpdate(nullptr)) {
    if (prefs_->Exists(kPrefsDeferredUpdateCompleted)) {
      status_ = UpdateStatus::UPDATED_BUT_DEFERRED;
    } else {
      status_ = UpdateStatus::UPDATED_NEED_REBOOT;
    }

    // Check if the pending update should be invalidated due to the enterprise
    // invalidation after update_engine restart.
    if (status_ == UpdateStatus::UPDATED_NEED_REBOOT) {
      MessageLoop::current()->PostTask(
          FROM_HERE,
          base::BindOnce(
              base::IgnoreResult(
                  &UpdateAttempter::ScheduleEnterpriseUpdateInvalidationCheck),
              weak_ptr_factory_.GetWeakPtr()));
    }
  } else {
    // Send metric before deleting prefs. Metric tells us how many times the
    // inactive partition was updated before the reboot.
    ReportConsecutiveUpdateMetric();

    status_ = UpdateStatus::IDLE;
    prefs_->Delete(kPrefsLastFp, {kDlcPrefsSubDir});
    prefs_->Delete(kPrefsConsecutiveUpdateCount);
  }
}

bool UpdateAttempter::IsUpdating() {
  return pm_ == ProcessMode::UPDATE;
}

bool UpdateAttempter::ScheduleEnterpriseUpdateInvalidationCheck() {
  if (enterprise_update_invalidation_check_scheduled_) {
    LOG(WARNING)
        << "Enterprise update invalidation check is already scheduled.";
    return false;
  }
  if (IsMigration()) {
    LOG(WARNING) << "Skip enterprise update invalidation check for migration.";
    return false;
  }
  enterprise_update_invalidation_check_scheduled_ = true;

  LOG(INFO) << "Scheduling enterprise update invalidation check.";
  SystemState::Get()->update_manager()->PolicyRequest(
      std::make_unique<EnterpriseUpdateDisabledPolicyImpl>(), nullptr,
      base::BindOnce(&UpdateAttempter::OnEnterpriseUpdateInvalidationCheck,
                     weak_ptr_factory_.GetWeakPtr()));

  return true;
}

void UpdateAttempter::OnEnterpriseUpdateInvalidationCheck(
    EvalStatus eval_status) {
  enterprise_update_invalidation_check_scheduled_ = false;

  if (eval_status == EvalStatus::kSucceeded &&
      status_ == UpdateStatus::UPDATED_NEED_REBOOT) {
    LOG(INFO) << "Received enterprise update invalidation signal. "
              << "Invalidating the pending update.";
    bool invalidation_result = InvalidateUpdate();
    SystemState::Get()
        ->metrics_reporter()
        ->ReportEnterpriseUpdateInvalidatedResult(invalidation_result);
    ResetUpdateStatus();
  }

  return;
}

bool UpdateAttempter::ScheduleUpdates(const ScheduleUpdatesParams& params) {
  // Overrides based off of `ScheduleUpdateParams`.
  auto UpdateCheckAllowedPolicyDataOverrider = [this, params]() {
    LOG(INFO) << "Overriding scheduled update check allowed policy data.";
    policy_data_->update_check_params.force_fw_update = params.force_fw_update;
  };

  if (IsBusyOrUpdateScheduled()) {
    // Ignoring other special cases of auto scenarios, allow override only while
    // policy hasn't been evaluated.
    if (status_ == UpdateStatus::IDLE) {
      UpdateCheckAllowedPolicyDataOverrider();
    }
    return false;
  }

  // We limit the async policy request to a reasonably short time, to avoid a
  // starvation due to a transient bug.
  policy_data_.reset(new UpdateCheckAllowedPolicyData());
  UpdateCheckAllowedPolicyDataOverrider();

  SystemState::Get()->update_manager()->PolicyRequest(
      std::make_unique<UpdateCheckAllowedPolicy>(),
      policy_data_,  // Do not move because we don't want transfer of ownership.
      base::BindOnce(&UpdateAttempter::OnUpdateScheduled,
                     weak_ptr_factory_.GetWeakPtr()));

  waiting_for_scheduled_check_ = true;
  return true;
}

bool UpdateAttempter::StartUpdater() {
  // Initiate update checks.
  ScheduleUpdates();

  // Start the rootfs integrity check.
  RootfsIntegrityCheck();

  // Keep this after kicking off rootfs integrity check.
  auto update_boot_flags_action = std::make_unique<UpdateBootFlagsAction>(
      SystemState::Get()->boot_control(), SystemState::Get()->hardware());
  aux_processor_.EnqueueAction(std::move(update_boot_flags_action));
  // Update boot flags after delay.
  MessageLoop::current()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&ActionProcessor::StartProcessing,
                     base::Unretained(&aux_processor_)),
      base::Seconds(60));

  // Broadcast the update engine status on startup to ensure consistent system
  // state on crashes.
  MessageLoop::current()->PostTask(
      FROM_HERE, base::BindOnce(&UpdateAttempter::BroadcastStatus,
                                weak_ptr_factory_.GetWeakPtr()));

  MessageLoop::current()->PostTask(
      FROM_HERE, base::BindOnce(&UpdateAttempter::UpdateEngineStarted,
                                weak_ptr_factory_.GetWeakPtr()));
  return true;
}

void UpdateAttempter::CertificateChecked(ServerToCheck server_to_check,
                                         CertificateCheckResult result) {
  SystemState::Get()->metrics_reporter()->ReportCertificateCheckMetrics(
      server_to_check, result);
}

bool UpdateAttempter::CheckAndReportDailyMetrics() {
  int64_t stored_value;
  Time now = SystemState::Get()->clock()->GetWallclockTime();
  if (SystemState::Get()->prefs()->Exists(kPrefsDailyMetricsLastReportedAt) &&
      SystemState::Get()->prefs()->GetInt64(kPrefsDailyMetricsLastReportedAt,
                                            &stored_value)) {
    Time last_reported_at = Time::FromInternalValue(stored_value);
    TimeDelta time_reported_since = now - last_reported_at;
    if (time_reported_since.InSeconds() < 0) {
      LOG(WARNING) << "Last reported daily metrics "
                   << utils::FormatTimeDelta(time_reported_since) << " ago "
                   << "which is negative. Either the system clock is wrong or "
                   << "the kPrefsDailyMetricsLastReportedAt state variable "
                   << "is wrong.";
      // In this case, report daily metrics to reset.
    } else {
      if (time_reported_since.InSeconds() < 24 * 60 * 60) {
        LOG(INFO) << "Last reported daily metrics "
                  << utils::FormatTimeDelta(time_reported_since) << " ago.";
        return false;
      }
      LOG(INFO) << "Last reported daily metrics "
                << utils::FormatTimeDelta(time_reported_since) << " ago, "
                << "which is more than 24 hours ago.";
    }
  }

  LOG(INFO) << "Reporting daily metrics.";
  SystemState::Get()->prefs()->SetInt64(kPrefsDailyMetricsLastReportedAt,
                                        now.ToInternalValue());

  ReportOSAge();

  return true;
}

void UpdateAttempter::ReportConsecutiveUpdateMetric() {
  int64_t num_consecutive_updates = 0;
  SystemState::Get()->prefs()->GetInt64(kPrefsConsecutiveUpdateCount,
                                        &num_consecutive_updates);
  if (num_consecutive_updates != 0) {
    SystemState::Get()->metrics_reporter()->ReportConsecutiveUpdateCount(
        num_consecutive_updates);
  }
}

void UpdateAttempter::ReportOSAge() {
  struct stat sb;
  if (stat("/etc/lsb-release", &sb) != 0) {
    PLOG(ERROR) << "Error getting file status for /etc/lsb-release "
                << "(Note: this may happen in some unit tests)";
    return;
  }

  Time lsb_release_timestamp = Time::FromTimeSpec(sb.st_ctim);
  Time now = SystemState::Get()->clock()->GetWallclockTime();
  TimeDelta age = now - lsb_release_timestamp;
  if (age.InSeconds() < 0) {
    LOG(ERROR) << "The OS age (" << utils::FormatTimeDelta(age)
               << ") is negative. Maybe the clock is wrong? "
               << "(Note: this may happen in some unit tests.)";
    return;
  }

  SystemState::Get()->metrics_reporter()->ReportDailyMetrics(age);
}

void UpdateAttempter::Update(const UpdateCheckParams& params) {
  // This is normally called frequently enough so it's appropriate to use as a
  // hook for reporting daily metrics.
  // TODO(garnold) This should be hooked to a separate (reliable and consistent)
  // timeout event.
  CheckAndReportDailyMetrics();

  fake_update_success_ = false;
  if (status_ == UpdateStatus::UPDATED_NEED_REBOOT) {
    if (!IsRepeatedUpdatesEnabled()) {
      // Although we have applied an update, we still want to ping Omaha
      // to ensure the number of active statistics is accurate.
      //
      // Also convey to the UpdateEngine.Check.Result metric that we're
      // not performing an update check because of this.
      LOG(INFO) << "Not updating b/c we already updated and we're waiting for "
                << "reboot, we'll ping Omaha instead";
      SystemState::Get()->metrics_reporter()->ReportUpdateCheckMetrics(
          metrics::CheckResult::kRebootPending, metrics::CheckReaction::kUnset,
          metrics::DownloadErrorCode::kUnset);
      PingOmaha();
      return;
    }
    LOG(INFO) << "Already updated but checking to see if there are more recent "
                 "updates available.";
  } else if (status_ == UpdateStatus::UPDATED_BUT_DEFERRED) {
    // Update is already deferred, don't proceed with repeated updates.
    // Although we have applied an update, we still want to ping Omaha
    // to ensure the number of active statistics is accurate.
    //
    // Also convey to the UpdateEngine.Check.Result metric that we're
    // not performing an update check because of this.
    LOG(INFO) << "Not updating b/c we deferred update, ping Omaha instead";
    // TODO(kimjae): Add label for metric.
    SystemState::Get()->metrics_reporter()->ReportUpdateCheckMetrics(
        metrics::CheckResult::kDeferredUpdate, metrics::CheckReaction::kUnset,
        metrics::DownloadErrorCode::kUnset);
    PingOmaha();
    return;
  } else if (status_ != UpdateStatus::IDLE) {
    // Update in progress. Do nothing.
    return;
  }

  if (!CalculateUpdateParams(params)) {
    return;
  }

  BuildUpdateActions(params);

  SetStatusAndNotify(UpdateStatus::CHECKING_FOR_UPDATE);

  // Update the last check time here; it may be re-updated when an Omaha
  // response is received, but this will prevent us from repeatedly scheduling
  // checks in the case where a response is not received.
  UpdateLastCheckedTime();

  ScheduleProcessingStart();
}

void UpdateAttempter::Install() {
  CHECK(!processor_->IsRunning());
  processor_->set_delegate(this);

  if (dlc_ids_.size() != 1) {
    LOG(ERROR) << "Could not kick off installation.";
    return;
  }
  const auto& dlc_id = dlc_ids_[0];

  if (pm_ == ProcessMode::MIGRATE) {
    auto* boot_control = SystemState::Get()->boot_control();
    auto last_slot = boot_control->GetHighestOffsetSlot(kPartitionNameRoot);
    auto inactive_slot = boot_control->GetFirstInactiveSlot();
    if (inactive_slot == BootControlInterface::kInvalidSlot ||
        last_slot == BootControlInterface::kInvalidSlot) {
      LOG(ERROR) << "Unable to determine installation slot for the migration.";
      return;
    }
    if (inactive_slot != last_slot) {
      LOG(ERROR) << "Migration DLC must be installed in the last slot: "
                 << boot_control->SlotName(last_slot)
                 << ". First inactive slot: "
                 << boot_control->SlotName(inactive_slot);
      return;
    }
  }

  auto http_fetcher = std::make_unique<LibcurlHttpFetcher>(
      GetProxyResolver(), SystemState::Get()->hardware());
  auto install_action = std::make_unique<InstallAction>(
      std::move(http_fetcher), dlc_id,
      /*slotting=*/pm_ == ProcessMode::FORCE_OTA_INSTALL ? kForceOTASlotting
                                                         : kDefaultSlotting,
      /*target=*/pm_ == ProcessMode::MIGRATE ? InstallAction::kRoot
                                             : InstallAction::kStateful);
  install_action->set_delegate(this);
  SetOutPipe(install_action.get());
  processor_->EnqueueAction(std::move(install_action));

  // Simply go into CHECKING status.
  SetStatusAndNotify(UpdateStatus::CHECKING_FOR_UPDATE);

  // Start limiting the cpu now as the next action to run should be
  // installations per scheduling.
  cpu_limiter_.StartLimiter();

  ScheduleProcessingStart();
}

void UpdateAttempter::RefreshDevicePolicy() {
  // Lazy initialize the policy provider, or reload the latest policy data.
  if (!policy_provider_.get()) {
    policy_provider_.reset(new policy::PolicyProvider());
  }
  policy_provider_->Reload();

  const policy::DevicePolicy* device_policy = nullptr;
  if (policy_provider_->device_policy_is_loaded()) {
    device_policy = &policy_provider_->GetDevicePolicy();
  }

  if (device_policy) {
    LOG(INFO) << "Device policies/settings present";
  } else {
    LOG(INFO) << "No device policies/settings present.";
  }

  SystemState::Get()->set_device_policy(device_policy);
  SystemState::Get()->p2p_manager()->SetDevicePolicy(device_policy);
}

void UpdateAttempter::CalculateP2PParams(bool interactive) {
  bool use_p2p_for_downloading = false;
  bool use_p2p_for_sharing = false;

  // Never use p2p for downloading in interactive checks unless the developer
  // has opted in for it via a marker file.
  //
  // (Why would a developer want to opt in? If they are working on the
  // update_engine or p2p codebases so they can actually test their code.)

  if (!SystemState::Get()->p2p_manager()->IsP2PEnabled()) {
    LOG(INFO) << "p2p is not enabled - disallowing p2p for both"
              << " downloading and sharing.";
  } else {
    // Allow p2p for sharing, even in interactive checks.
    use_p2p_for_sharing = true;
    if (!interactive) {
      LOG(INFO) << "Non-interactive check - allowing p2p for downloading";
      use_p2p_for_downloading = true;
    } else {
      LOG(INFO) << "Forcibly disabling use of p2p for downloading "
                << "since this update attempt is interactive.";
    }
  }

  PayloadStateInterface* const payload_state =
      SystemState::Get()->payload_state();
  payload_state->SetUsingP2PForDownloading(use_p2p_for_downloading);
  payload_state->SetUsingP2PForSharing(use_p2p_for_sharing);
}

bool UpdateAttempter::CalculateUpdateParams(const UpdateCheckParams& params) {
  http_response_code_ = 0;
  PayloadStateInterface* const payload_state =
      SystemState::Get()->payload_state();

  // Refresh the policy before computing all the update parameters.
  RefreshDevicePolicy();

  // Check whether we need to clear the rollback-happened preference after
  // policy is available again.
  UpdateRollbackHappened();

  CalculateStagingParams(params.interactive);
  // If staging_wait_time_ wasn't set, staging is off, use scattering instead.
  if (staging_wait_time_.InSeconds() == 0) {
    CalculateScatteringParams(params.interactive);
  }

  CalculateP2PParams(params.interactive);
  if (payload_state->GetUsingP2PForDownloading() ||
      payload_state->GetUsingP2PForSharing()) {
    // OK, p2p is to be used - start it and perform housekeeping.
    if (!StartP2PAndPerformHousekeeping()) {
      // If this fails, disable p2p for this attempt
      LOG(INFO) << "Forcibly disabling use of p2p since starting p2p or "
                << "performing housekeeping failed.";
      payload_state->SetUsingP2PForDownloading(false);
      payload_state->SetUsingP2PForSharing(false);
    }
  }

  if (!omaha_request_params_->Init(forced_app_version_, forced_omaha_url_,
                                   params)) {
    LOG(ERROR) << "Unable to initialize Omaha request params.";
    return false;
  }
  // Get all the policy related omaha request params. This should potentially
  // replace the Init() function call above in the future.
  SystemState::Get()->update_manager()->PolicyRequest(
      std::make_unique<OmahaRequestParamsPolicy>(), nullptr);

  // The function |CalculateDlcParams| makes use of the function |GetAppId| from
  // |OmahaRequestParams|, so to ensure that the return from |GetAppId|
  // doesn't change, no changes to the values |download_channel_|,
  // |image_props_.product_id| and |image_props_.canary_product_id| from
  // |omaha_request_params_| shall be made below this line.
  CalculateDlcParams();

  LOG(INFO) << "target_version_prefix = "
            << omaha_request_params_->target_version_prefix()
            << ", rollback_allowed = "
            << omaha_request_params_->rollback_allowed()
            << ", scatter_factor_in_seconds = "
            << utils::FormatSecs(scatter_factor_.InSeconds());

  LOG(INFO) << "Wall Clock Based Wait Enabled = "
            << omaha_request_params_->wall_clock_based_wait_enabled()
            << ", Update Check Count Wait Enabled = "
            << omaha_request_params_->update_check_count_wait_enabled()
            << ", Waiting Period = "
            << utils::FormatSecs(
                   omaha_request_params_->waiting_period().InSeconds());

  LOG(INFO) << "Use p2p For Downloading = "
            << payload_state->GetUsingP2PForDownloading()
            << ", Use p2p For Sharing = "
            << payload_state->GetUsingP2PForSharing();

  obeying_proxies_ = true;
  if (proxy_manual_checks_ == 0) {
    LOG(INFO) << "forced to obey proxies";
    // If forced to obey proxies, every 20th request will not use proxies
    proxy_manual_checks_++;
    LOG(INFO) << "proxy manual checks: " << proxy_manual_checks_;
    if (proxy_manual_checks_ >= kMaxConsecutiveObeyProxyRequests) {
      proxy_manual_checks_ = 0;
      obeying_proxies_ = false;
    }
  } else if (base::RandInt(0, 4) == 0) {
    obeying_proxies_ = false;
  }
  LOG_IF(INFO, !obeying_proxies_)
      << "To help ensure updates work, this update check we are ignoring the "
      << "proxy settings and using direct connections.";

  DisableDeltaUpdateIfNeeded();
  DetermineExtendedUpdateValue();
  return true;
}

void UpdateAttempter::CalculateScatteringParams(bool interactive) {
  // Take a copy of the old scatter value before we update it, as
  // we need to update the waiting period if this value changes.
  TimeDelta old_scatter_factor = scatter_factor_;
  const policy::DevicePolicy* device_policy =
      SystemState::Get()->device_policy();
  if (device_policy) {
    int64_t new_scatter_factor_in_secs = 0;
    device_policy->GetScatterFactorInSeconds(&new_scatter_factor_in_secs);
    if (new_scatter_factor_in_secs < 0) {  // sanitize input, just in case.
      new_scatter_factor_in_secs = 0;
    }
    scatter_factor_ = base::Seconds(new_scatter_factor_in_secs);
  }

  bool is_scatter_enabled = false;
  if (scatter_factor_.InSeconds() == 0) {
    LOG(INFO) << "Scattering disabled since scatter factor is set to 0";
  } else if (interactive) {
    LOG(INFO) << "Scattering disabled as this is an interactive update check";
  } else if (SystemState::Get()->hardware()->IsOOBEEnabled() &&
             !SystemState::Get()->hardware()->IsOOBEComplete(nullptr)) {
    LOG(INFO) << "Scattering disabled since OOBE is enabled but not complete "
                 "yet";
  } else {
    is_scatter_enabled = true;
    LOG(INFO) << "Scattering is enabled";
  }

  if (is_scatter_enabled) {
    // This means the scattering policy is turned on.
    // Now check if we need to update the waiting period. The two cases
    // in which we'd need to update the waiting period are:
    // 1. First time in process or a scheduled check after a user-initiated one.
    //    (omaha_request_params_->waiting_period will be zero in this case).
    // 2. Admin has changed the scattering policy value.
    //    (new scattering value will be different from old one in this case).
    int64_t wait_period_in_secs = 0;
    if (omaha_request_params_->waiting_period().InSeconds() == 0) {
      // First case. Check if we have a suitable value to set for
      // the waiting period.
      if (prefs_->GetInt64(kPrefsWallClockScatteringWaitPeriod,
                           &wait_period_in_secs) &&
          wait_period_in_secs > 0 &&
          wait_period_in_secs <= scatter_factor_.InSeconds()) {
        // This means:
        // 1. There's a persisted value for the waiting period available.
        // 2. And that persisted value is still valid.
        // So, in this case, we should reuse the persisted value instead of
        // generating a new random value to improve the chances of a good
        // distribution for scattering.
        omaha_request_params_->set_waiting_period(
            base::Seconds(wait_period_in_secs));
        LOG(INFO) << "Using persisted wall-clock waiting period: "
                  << utils::FormatSecs(
                         omaha_request_params_->waiting_period().InSeconds());
      } else {
        // This means there's no persisted value for the waiting period
        // available or its value is invalid given the new scatter_factor value.
        // So, we should go ahead and regenerate a new value for the
        // waiting period.
        LOG(INFO) << "Persisted value not present or not valid ("
                  << utils::FormatSecs(wait_period_in_secs)
                  << ") for wall-clock waiting period.";
        GenerateNewWaitingPeriod();
      }
    } else if (scatter_factor_ != old_scatter_factor) {
      // This means there's already a waiting period value, but we detected
      // a change in the scattering policy value. So, we should regenerate the
      // waiting period to make sure it's within the bounds of the new scatter
      // factor value.
      GenerateNewWaitingPeriod();
    } else {
      // Neither the first time scattering is enabled nor the scattering value
      // changed. Nothing to do.
      LOG(INFO) << "Keeping current wall-clock waiting period: "
                << utils::FormatSecs(
                       omaha_request_params_->waiting_period().InSeconds());
    }

    // The invariant at this point is that omaha_request_params_->waiting_period
    // is non-zero no matter which path we took above.
    LOG_IF(ERROR, omaha_request_params_->waiting_period().InSeconds() == 0)
        << "Waiting Period should NOT be zero at this point!!!";

    // Since scattering is enabled, wall clock based wait will always be
    // enabled.
    omaha_request_params_->set_wall_clock_based_wait_enabled(true);

    // If we don't have any issues in accessing the file system to update
    // the update check count value, we'll turn that on as well.
    bool decrement_succeeded = DecrementUpdateCheckCount();
    omaha_request_params_->set_update_check_count_wait_enabled(
        decrement_succeeded);
  } else {
    // This means the scattering feature is turned off or disabled for
    // this particular update check. Make sure to disable
    // all the knobs and artifacts so that we don't invoke any scattering
    // related code.
    omaha_request_params_->set_wall_clock_based_wait_enabled(false);
    omaha_request_params_->set_update_check_count_wait_enabled(false);
    omaha_request_params_->set_waiting_period(base::Seconds(0));
    prefs_->Delete(kPrefsWallClockScatteringWaitPeriod);
    prefs_->Delete(kPrefsUpdateCheckCount);
    // Don't delete the UpdateFirstSeenAt file as we don't want manual checks
    // that result in no-updates (e.g. due to server side throttling) to
    // cause update starvation by having the client generate a new
    // UpdateFirstSeenAt for each scheduled check that follows a manual check.
  }
}

void UpdateAttempter::GenerateNewWaitingPeriod() {
  omaha_request_params_->set_waiting_period(
      base::Seconds(base::RandInt(1, scatter_factor_.InSeconds())));

  LOG(INFO) << "Generated new wall-clock waiting period: "
            << utils::FormatSecs(
                   omaha_request_params_->waiting_period().InSeconds());

  // Do a best-effort to persist this in all cases. Even if the persistence
  // fails, we'll still be able to scatter based on our in-memory value.
  // The persistence only helps in ensuring a good overall distribution
  // across multiple devices if they tend to reboot too often.
  SystemState::Get()->payload_state()->SetScatteringWaitPeriod(
      omaha_request_params_->waiting_period());
}

void UpdateAttempter::CalculateStagingParams(bool interactive) {
  bool oobe_complete = SystemState::Get()->hardware()->IsOOBEEnabled() &&
                       SystemState::Get()->hardware()->IsOOBEComplete(nullptr);
  auto device_policy = SystemState::Get()->device_policy();
  StagingCase staging_case = StagingCase::kOff;
  if (device_policy && !interactive && oobe_complete) {
    staging_wait_time_ = omaha_request_params_->waiting_period();
    staging_case = CalculateStagingCase(device_policy, &staging_wait_time_,
                                        &staging_schedule_);
  }
  switch (staging_case) {
    case StagingCase::kOff:
      // Staging is off, get rid of persisted value.
      prefs_->Delete(kPrefsWallClockStagingWaitPeriod);
      // Set |staging_wait_time_| to its default value so scattering can still
      // be turned on
      staging_wait_time_ = TimeDelta();
      break;
    // Let the cases fall through since they just add, and never remove, steps
    // to turning staging on.
    case StagingCase::kNoSavedValue:
      prefs_->SetInt64(kPrefsWallClockStagingWaitPeriod,
                       staging_wait_time_.InDays());
      [[fallthrough]];
    case StagingCase::kSetStagingFromPref:
      omaha_request_params_->set_waiting_period(staging_wait_time_);
      [[fallthrough]];
    case StagingCase::kNoAction:
      // Staging is on, enable wallclock based wait so that its values get used.
      omaha_request_params_->set_wall_clock_based_wait_enabled(true);
      // Use UpdateCheckCount if possible to prevent devices updating all at
      // once.
      omaha_request_params_->set_update_check_count_wait_enabled(
          DecrementUpdateCheckCount());
      // Scattering should not be turned on if staging is on, delete the
      // existing scattering configuration.
      prefs_->Delete(kPrefsWallClockScatteringWaitPeriod);
      scatter_factor_ = TimeDelta();
  }
}

bool UpdateAttempter::ResetDlcPrefs(const string& dlc_id) {
  vector<string> failures;
  for (auto& sub_key :
       {kPrefsPingActive, kPrefsPingLastActive, kPrefsPingLastRollcall}) {
    auto key = prefs_->CreateSubKey({kDlcPrefsSubDir, dlc_id, sub_key});
    if (!prefs_->Delete(key)) {
      failures.emplace_back(sub_key);
    }
  }
  if (failures.size() != 0) {
    PLOG(ERROR) << "Failed to delete prefs (" << base::JoinString(failures, ",")
                << " for DLC (" << dlc_id << ").";
  }

  return failures.size() == 0;
}

void UpdateAttempter::SetPref(const string& pref_key,
                              const string& pref_value,
                              const string& payload_id) {
  string dlc_id;
  string key = pref_key;
  if (omaha_request_params_->IsMiniOSAppId(payload_id)) {
    key = prefs_->CreateSubKey({kMiniOSPrefsSubDir, pref_key});
  } else if (omaha_request_params_->GetDlcId(payload_id, &dlc_id)) {
    key = prefs_->CreateSubKey({kDlcPrefsSubDir, dlc_id, pref_key});
  }
  prefs_->SetString(key, pref_value);
}

bool UpdateAttempter::SetDlcActiveValue(bool is_active, const string& dlc_id) {
  if (dlc_id.empty()) {
    LOG(ERROR) << "Empty DLC ID passed.";
    return false;
  }
  LOG(INFO) << "Set DLC (" << dlc_id << ") to "
            << (is_active ? "Active" : "Inactive");
  if (is_active) {
    auto ping_active_key =
        prefs_->CreateSubKey({kDlcPrefsSubDir, dlc_id, kPrefsPingActive});
    if (!prefs_->SetInt64(ping_active_key, kPingActiveValue)) {
      LOG(ERROR) << "Failed to set the value of ping metadata '"
                 << kPrefsPingActive << "'.";
      return false;
    }
  } else {
    return ResetDlcPrefs(dlc_id);
  }
  return true;
}

int64_t UpdateAttempter::GetPingMetadata(const string& metadata_key) const {
  // The first time a ping is sent, the metadata files containing the values
  // sent back by the server still don't exist. A value of -1 is used to
  // indicate this.
  if (!SystemState::Get()->prefs()->Exists(metadata_key)) {
    return kPingNeverPinged;
  }

  int64_t value;
  if (SystemState::Get()->prefs()->GetInt64(metadata_key, &value)) {
    return value;
  }

  // Return -2 when the file exists and there is a problem reading from it, or
  // the value cannot be converted to an integer.
  return kPingUnknownValue;
}

void UpdateAttempter::CalculateDlcParams() {
  // Set the |dlc_ids_| only for an update. This is required to get the
  // currently installed DLC(s).
  if (IsUpdating() &&
      !SystemState::Get()->dlcservice()->GetDlcsToUpdate(&dlc_ids_)) {
    LOG(INFO) << "Failed to retrieve DLC module IDs from dlcservice. Check the "
                 "state of dlcservice, will not update DLC modules.";
  }
  map<string, OmahaRequestParams::AppParams> dlc_apps_params;
  for (const auto& dlc_id : dlc_ids_) {
    const auto& manifest = SystemState::Get()->dlc_utils()->GetDlcManifest(
        dlc_id, base::FilePath(imageloader::kDlcManifestRootpath));
    if (!manifest) {
      LOG(ERROR) << "Unable to load the manifest for DLC '" << dlc_id
                 << "', treat it as a non-critical DLC.";
    }
    OmahaRequestParams::AppParams dlc_params{
        .active_counting_type = OmahaRequestParams::kDateBased,
        .critical_update = manifest && manifest->critical_update(),
        .name = dlc_id,
        .send_ping = false};
    if (!IsUpdating()) {
      // In some cases, |SetDlcActiveValue| might fail to reset the DLC prefs
      // when a DLC is uninstalled. To avoid having stale values from that
      // scenario, we reset the metadata values on a new install request.
      // Ignore failure to delete stale prefs.
      ResetDlcPrefs(dlc_id);
      SetDlcActiveValue(true, dlc_id);
    } else {
      // Only send the ping when the request is to update DLCs. When installing
      // DLCs, we don't want to send the ping yet, since the DLCs might fail to
      // install or might not really be active yet.
      dlc_params.ping_active = kPingActiveValue;
      auto ping_active_key =
          prefs_->CreateSubKey({kDlcPrefsSubDir, dlc_id, kPrefsPingActive});
      if (!prefs_->GetInt64(ping_active_key, &dlc_params.ping_active) ||
          dlc_params.ping_active != kPingActiveValue) {
        dlc_params.ping_active = kPingInactiveValue;
      }
      auto ping_last_active_key =
          prefs_->CreateSubKey({kDlcPrefsSubDir, dlc_id, kPrefsPingLastActive});
      dlc_params.ping_date_last_active = GetPingMetadata(ping_last_active_key);

      auto ping_last_rollcall_key = prefs_->CreateSubKey(
          {kDlcPrefsSubDir, dlc_id, kPrefsPingLastRollcall});
      dlc_params.ping_date_last_rollcall =
          GetPingMetadata(ping_last_rollcall_key);

      auto dlc_fp_key =
          prefs_->CreateSubKey({kDlcPrefsSubDir, dlc_id, kPrefsLastFp});
      prefs_->GetString(dlc_fp_key, &dlc_params.last_fp);

      dlc_params.send_ping = true;
    }
    dlc_apps_params[omaha_request_params_->GetDlcAppId(dlc_id)] = dlc_params;
  }
  omaha_request_params_->set_dlc_apps_params(dlc_apps_params);
  omaha_request_params_->set_is_install(!IsUpdating());
}

void UpdateAttempter::BuildUpdateActions(const UpdateCheckParams& params) {
  CHECK(!processor_->IsRunning());
  processor_->set_delegate(this);

  bool interactive = params.interactive;

  // The session ID needs to be kept throughout the update flow. The value
  // of the session ID will reset/update only when it is a new update flow.
  session_id_ = base::Uuid::GenerateRandomV4().AsLowercaseString();

  // Actions:
  auto update_check_fetcher = std::make_unique<LibcurlHttpFetcher>(
      GetProxyResolver(), SystemState::Get()->hardware());
  update_check_fetcher->set_server_to_check(ServerToCheck::kUpdate);
  // Try harder to connect to the network, esp when not interactive.
  // See comment in libcurl_http_fetcher.cc.
  update_check_fetcher->set_no_network_max_retries(interactive ? 1 : 3);
  update_check_fetcher->set_is_update_check(true);
  auto update_check_action = std::make_unique<OmahaRequestAction>(
      nullptr, std::move(update_check_fetcher), false, session_id_);

  // When `skip_applying_` is requested, the only actions required to process is
  // querying Omaha and parsing the response to get the new version/etc.
  if (skip_applying_) {
    SetOutPipe(update_check_action.get());
    processor_->EnqueueAction(std::move(update_check_action));
    return;
  }

  auto response_handler_action = std::make_unique<OmahaResponseHandlerAction>();
  auto update_boot_flags_action = std::make_unique<UpdateBootFlagsAction>(
      SystemState::Get()->boot_control(), SystemState::Get()->hardware());
  auto download_started_action = std::make_unique<OmahaRequestAction>(
      new OmahaEvent(OmahaEvent::kTypeUpdateDownloadStarted),
      std::make_unique<LibcurlHttpFetcher>(GetProxyResolver(),
                                           SystemState::Get()->hardware()),
      false, session_id_);

  auto download_fetcher = std::make_unique<LibcurlHttpFetcher>(
      GetProxyResolver(), SystemState::Get()->hardware());
  download_fetcher->set_server_to_check(ServerToCheck::kDownload);
  if (interactive) {
    download_fetcher->set_max_retry_count(kDownloadMaxRetryCountInteractive);
  }
  download_fetcher->SetHeader(kXGoogleUpdateSessionId, session_id_);
  auto download_action = std::make_unique<DownloadActionChromeos>(
      std::move(download_fetcher), interactive);
  download_action->set_delegate(this);

  auto download_finished_action = std::make_unique<OmahaRequestAction>(
      new OmahaEvent(OmahaEvent::kTypeUpdateDownloadFinished),
      std::make_unique<LibcurlHttpFetcher>(GetProxyResolver(),
                                           SystemState::Get()->hardware()),
      false, session_id_);
  auto filesystem_verifier_action = std::make_unique<FilesystemVerifierAction>(
      SystemState::Get()->boot_control()->GetDynamicPartitionControl());
  auto update_complete_action = std::make_unique<OmahaRequestAction>(
      new OmahaEvent(OmahaEvent::kTypeUpdateComplete),
      std::make_unique<LibcurlHttpFetcher>(GetProxyResolver(),
                                           SystemState::Get()->hardware()),
      false, session_id_);

  auto postinstall_runner_action = std::make_unique<PostinstallRunnerAction>(
      SystemState::Get()->boot_control(), SystemState::Get()->hardware(),
      params.force_fw_update);
  postinstall_runner_action->set_delegate(this);

  // Bond them together. We have to use the leaf-types when calling
  // BondActions().
  BondActions(update_check_action.get(), response_handler_action.get());
  BondActions(response_handler_action.get(), download_action.get());
  BondActions(download_action.get(), filesystem_verifier_action.get());
  BondActions(filesystem_verifier_action.get(),
              postinstall_runner_action.get());

  processor_->EnqueueAction(std::move(update_check_action));
  processor_->EnqueueAction(std::move(response_handler_action));
  processor_->EnqueueAction(std::move(update_boot_flags_action));
  processor_->EnqueueAction(std::move(download_started_action));
  processor_->EnqueueAction(std::move(download_action));
  processor_->EnqueueAction(std::move(download_finished_action));
  processor_->EnqueueAction(std::move(filesystem_verifier_action));
  processor_->EnqueueAction(std::move(postinstall_runner_action));
  processor_->EnqueueAction(std::move(update_complete_action));
}

bool UpdateAttempter::Rollback(bool powerwash) {
  pm_ = ProcessMode::UPDATE;
  if (!CanRollback()) {
    return false;
  }

  // Extra check for enterprise-enrolled devices since they don't support
  // powerwash.
  if (powerwash) {
    // Enterprise-enrolled devices have an empty owner in their device policy.
    string owner;
    RefreshDevicePolicy();
    const policy::DevicePolicy* device_policy =
        SystemState::Get()->device_policy();
    if (device_policy && (!device_policy->GetOwner(&owner) || owner.empty())) {
      LOG(ERROR) << "Enterprise device detected. "
                 << "Cannot perform a powerwash for enterprise devices.";
      return false;
    }
  }

  processor_->set_delegate(this);

  // Initialize the default request params.
  if (!omaha_request_params_->Init("", "", {.interactive = true})) {
    LOG(ERROR) << "Unable to initialize Omaha request params.";
    return false;
  }

  LOG(INFO) << "Setting rollback options.";
  install_plan_.reset(new InstallPlan());
  install_plan_->target_slot = GetRollbackSlot();
  install_plan_->source_slot =
      SystemState::Get()->boot_control()->GetCurrentSlot();

  TEST_AND_RETURN_FALSE(install_plan_->LoadPartitionsFromSlots(
      SystemState::Get()->boot_control()));
  install_plan_->powerwash_required = powerwash;

  install_plan_->Dump();

  auto install_plan_action =
      std::make_unique<InstallPlanAction>(*install_plan_);
  auto postinstall_runner_action = std::make_unique<PostinstallRunnerAction>(
      SystemState::Get()->boot_control(), SystemState::Get()->hardware());
  postinstall_runner_action->set_delegate(this);
  BondActions(install_plan_action.get(), postinstall_runner_action.get());
  processor_->EnqueueAction(std::move(install_plan_action));
  processor_->EnqueueAction(std::move(postinstall_runner_action));

  // Update the payload state for Rollback.
  SystemState::Get()->payload_state()->Rollback();

  SetStatusAndNotify(UpdateStatus::ATTEMPTING_ROLLBACK);

  ScheduleProcessingStart();
  return true;
}

bool UpdateAttempter::CanRollback() const {
  // We can only rollback if the update_engine isn't busy and we have a valid
  // rollback partition.
  return (status_ == UpdateStatus::IDLE &&
          GetRollbackSlot() != BootControlInterface::kInvalidSlot);
}

BootControlInterface::Slot UpdateAttempter::GetRollbackSlot() const {
  LOG(INFO) << "UpdateAttempter::GetRollbackSlot";
  const unsigned int num_slots =
      SystemState::Get()->boot_control()->GetNumSlots();
  const BootControlInterface::Slot current_slot =
      SystemState::Get()->boot_control()->GetCurrentSlot();

  LOG(INFO) << "  Installed slots: " << num_slots;
  LOG(INFO) << "  Booted from slot: "
            << BootControlInterface::SlotName(current_slot);

  if (current_slot == BootControlInterface::kInvalidSlot || num_slots < 2) {
    LOG(INFO) << "Device is not updateable.";
    return BootControlInterface::kInvalidSlot;
  }

  vector<BootControlInterface::Slot> bootable_slots;
  for (BootControlInterface::Slot slot = 0; slot < num_slots; slot++) {
    if (slot != current_slot &&
        SystemState::Get()->boot_control()->IsSlotBootable(slot)) {
      LOG(INFO) << "Found bootable slot "
                << BootControlInterface::SlotName(slot);
      return slot;
    }
  }
  LOG(INFO) << "No other bootable slot found.";
  return BootControlInterface::kInvalidSlot;
}

bool UpdateAttempter::CheckForUpdate(
    const update_engine::UpdateParams& update_params) {
  if (status_ != UpdateStatus::IDLE &&
      status_ != UpdateStatus::UPDATED_NEED_REBOOT) {
    LOG(INFO) << "Refusing to do an update as there is an "
              << ConvertToString(pm_) << " already in progress.";
    return false;
  }

  const auto& update_flags = update_params.update_flags();
  bool interactive = !update_flags.non_interactive();
  pm_ = ProcessMode::UPDATE;
  if (update_params.skip_applying()) {
    skip_applying_ = true;
    LOG(INFO) << "Update check is only going to query server for update, will "
              << "not be applying any updates.";
  }

  LOG(INFO) << "Forced update check requested.";
  forced_app_version_.clear();
  forced_omaha_url_.clear();

  const auto& app_version = update_params.app_version();
  const auto& omaha_url = update_params.omaha_url();

  // Certain conditions must be met to allow setting custom version and update
  // server URLs. However, kScheduledAUTestURLRequest and kAUTestURLRequest are
  // always allowed regardless of device state.
  if (IsAnyUpdateSourceAllowed()) {
    forced_app_version_ = app_version;
    forced_omaha_url_ = omaha_url;
  }
  if (omaha_url == kScheduledAUTestURLRequest) {
    forced_omaha_url_ = constants::kOmahaDefaultAUTestURL;
    // Pretend that it's not user-initiated even though it is,
    // so as to test scattering logic, etc. which get kicked off
    // only in scheduled update checks.
    interactive = false;
  } else if (omaha_url == kAUTestURLRequest) {
    forced_omaha_url_ = constants::kOmahaDefaultAUTestURL;
  }

  if (interactive) {
    // Use the passed-in update attempt flags for this update attempt instead
    // of the previously set ones.
    current_update_flags_ = update_flags;
    // Note: The caching for non-interactive update checks happens in
    // |OnUpdateScheduled()|.
  }

  // |forced_update_pending_callback_| should always be set, but even in the
  // case that it is not, we still return true indicating success because the
  // scheduled periodic check will pick up these changes.
  if (forced_update_pending_callback_.get()) {
    // Always call |ScheduleUpdates()| before forcing an update. This is because
    // we need an update to be scheduled for the
    // |forced_update_pending_callback_| to have an effect. Here we don't need
    // to care about the return value from |ScheduleUpdate()|.
    ScheduleUpdates({
        .force_fw_update = update_params.force_fw_update(),
    });
    forced_update_pending_callback_->Run(true, interactive);
  }
  return true;
}

bool UpdateAttempter::ApplyDeferredUpdate(bool shutdown) {
  if (status_ != UpdateStatus::UPDATED_BUT_DEFERRED) {
    LOG(ERROR) << "Cannot apply deferred update when there isn't one "
                  "deferred.";
    return false;
  }

  LOG(INFO) << "Applying deferred update.";
  install_plan_.reset(new InstallPlan());
  auto* boot_control = SystemState::Get()->boot_control();

  install_plan_->run_post_install = true;

  if (shutdown) {
    install_plan_->defer_update_action = DeferUpdateAction::kApplyAndShutdown;
  } else {
    install_plan_->defer_update_action = DeferUpdateAction::kApplyAndReboot;
  }

  // Since CrOS is A/B, it's okay to get the first inactive slot.
  install_plan_->source_slot = boot_control->GetCurrentSlot();
  install_plan_->target_slot = boot_control->GetFirstInactiveSlot();

  install_plan_->partitions.push_back({
      .name = "root",
      .source_size = 1,
      .target_size = 1,
      .run_postinstall = true,
      // TODO(kimjae): Store + override to handle non default script usage.
      .postinstall_path = kPostinstallDefaultScript,
  });
  if (!install_plan_->LoadPartitionsFromSlots(boot_control)) {
    LOG(ERROR) << "Failed to setup partitions for applying deferred update.";
    return false;
  }

  install_plan_->Dump();

  auto install_plan_action =
      std::make_unique<InstallPlanAction>(*install_plan_);
  auto postinstall_runner_action = std::make_unique<PostinstallRunnerAction>(
      boot_control, SystemState::Get()->hardware());
  postinstall_runner_action->set_delegate(this);
  BondActions(install_plan_action.get(), postinstall_runner_action.get());
  processor_->EnqueueAction(std::move(install_plan_action));
  processor_->EnqueueAction(std::move(postinstall_runner_action));
  processor_->set_delegate(this);

  ScheduleProcessingStart();
  return true;
}

bool UpdateAttempter::CheckForInstall(const vector<string>& dlc_ids,
                                      const string& omaha_url,
                                      bool scaled,
                                      bool force_ota,
                                      bool migration) {
  if (status_ != UpdateStatus::IDLE) {
    LOG(INFO) << "Refusing to do an install as there is an "
              << ConvertToString(pm_) << " already in progress.";
    return false;
  }

  dlc_ids_ = dlc_ids;
  pm_ = ProcessMode::INSTALL;
  if (migration) {
    pm_ = ProcessMode::MIGRATE;
    dlc_ids_ = {kMigrationDlcId};
  } else if (scaled) {
    pm_ = ProcessMode::SCALED_INSTALL;
    // `force_ota` lower precedence than `scaled`.
  } else if (force_ota) {
    pm_ = ProcessMode::FORCE_OTA_INSTALL;
  }

  if (pm_ != ProcessMode::INSTALL && dlc_ids_.size() != 1) {
    LOG(ERROR) << "Can't install more than one DLC at a time.";
    return false;
  }

  forced_omaha_url_.clear();

  // Certain conditions must be met to allow setting custom version and update
  // server URLs. However, kScheduledAUTestURLRequest and kAUTestURLRequest are
  // always allowed regardless of device state.
  if (IsAnyUpdateSourceAllowed()) {
    forced_omaha_url_ = omaha_url;
  }

  if (omaha_url == kScheduledAUTestURLRequest ||
      omaha_url == kAUTestURLRequest) {
    forced_omaha_url_ = constants::kOmahaDefaultAUTestURL;
  }

  // |forced_update_pending_callback_| should always be set, but even in the
  // case that it is not, we still return true indicating success because the
  // scheduled periodic check will pick up these changes.
  if (forced_update_pending_callback_.get()) {
    // Always call |ScheduleUpdates()| before forcing an update. This is because
    // we need an update to be scheduled for the
    // |forced_update_pending_callback_| to have an effect. Here we don't need
    // to care about the return value from |ScheduleUpdate()|.
    ScheduleUpdates();
    forced_update_pending_callback_->Run(true, true);
  }
  return true;
}

bool UpdateAttempter::RebootIfNeeded() {
  if (SystemState::Get()->power_manager()->RequestReboot()) {
    return true;
  }

  return RebootDirectly();
}

bool UpdateAttempter::ShutdownIfNeeded() {
  if (SystemState::Get()->power_manager()->RequestShutdown()) {
    return true;
  }

  return ShutdownDirectly();
}

void UpdateAttempter::WriteUpdateCompletedMarker() {
  string boot_id;
  if (!utils::GetBootId(&boot_id)) {
    return;
  }
  prefs_->SetString(kPrefsUpdateCompletedOnBootId, boot_id);

  int64_t value = SystemState::Get()->clock()->GetBootTime().ToInternalValue();
  prefs_->SetInt64(kPrefsUpdateCompletedBootTime, value);
}

bool UpdateAttempter::RebootDirectly() {
  vector<string> command = {"/sbin/shutdown", "-r", "now"};
  int rc = 0;
  Subprocess::SynchronousExec(command, &rc, nullptr, nullptr);
  return rc == 0;
}

bool UpdateAttempter::ShutdownDirectly() {
  vector<string> command = {"/sbin/shutdown", "-P", "now"};
  int rc = 0;
  Subprocess::SynchronousExec(command, &rc, nullptr, nullptr);
  return rc == 0;
}

void UpdateAttempter::OnUpdateScheduled(EvalStatus status) {
  const UpdateCheckParams& params = policy_data_->update_check_params;
  waiting_for_scheduled_check_ = false;

  if (status == EvalStatus::kSucceeded) {
    if (!params.updates_enabled) {
      LOG(WARNING) << "Updates permanently disabled.";
      // Signal disabled status, then switch right back to idle. This is
      // necessary for ensuring that observers waiting for a signal change will
      // actually notice one on subsequent calls. Note that we don't need to
      // re-schedule a check in this case as updates are permanently disabled;
      // further (forced) checks may still initiate a scheduling call.
      SetStatusAndNotify(UpdateStatus::DISABLED);
      ResetUpdateStatus();
      return;
    }

    LOG(INFO) << "Running " << (params.interactive ? "interactive" : "periodic")
              << " " << ConvertToString(pm_);

    if (!params.interactive) {
      // Cache the update attempt flags that will be used by this update attempt
      // so that they can't be changed mid-way through.
      current_update_flags_ = update_flags_;
    }

    switch (pm_) {
      case ProcessMode::UPDATE:
      case ProcessMode::INSTALL:
        Update(params);
        break;
      case ProcessMode::SCALED_INSTALL:
      case ProcessMode::FORCE_OTA_INSTALL:
      case ProcessMode::MIGRATE:
        Install();
        break;
    }
    // Always clear the forced app_version and omaha_url after an update attempt
    // so the next update uses the defaults.
    forced_app_version_.clear();
    forced_omaha_url_.clear();
  } else {
    LOG(WARNING)
        << "Update check scheduling failed (possibly timed out); retrying.";
    ScheduleUpdates();
  }

  // This check ensures that future update checks will be or are already
  // scheduled. The check should never fail. A check failure means that there's
  // a bug that will most likely prevent further automatic update checks. It
  // seems better to crash in such cases and restart the update_engine daemon
  // into, hopefully, a known good state.
  CHECK(IsBusyOrUpdateScheduled());
}

void UpdateAttempter::UpdateLastCheckedTime() {
  last_checked_time_ =
      SystemState::Get()->clock()->GetWallclockTime().ToTimeT();
}

void UpdateAttempter::UpdateRollbackHappened() {
  DCHECK(SystemState::Get()->payload_state());
  DCHECK(policy_provider_);
  if (SystemState::Get()->payload_state()->GetRollbackHappened() &&
      (policy_provider_->device_policy_is_loaded() ||
       policy_provider_->IsConsumerDevice())) {
    // Rollback happened, but we already went through OOBE and policy is
    // present or it's a consumer device.
    SystemState::Get()->payload_state()->SetRollbackHappened(false);
  }
}

void UpdateAttempter::ProcessingDoneInternal(const ActionProcessor* processor,
                                             ErrorCode code) {
  // Reset cpu shares back to normal.
  cpu_limiter_.StopLimiter();

  ResetInteractivityFlags();

  if (status_ == UpdateStatus::REPORTING_ERROR_EVENT) {
    LOG(INFO) << "Error event sent.";

    // Inform scheduler of new status.
    ResetUpdateStatus();
    ScheduleUpdates();

    if (!fake_update_success_) {
      return;
    }
    LOG(INFO) << "Booted from FW B and tried to install new firmware, "
                 "so requesting reboot from user.";
  }

  attempt_error_code_ = utils::GetBaseErrorCode(code);

  if (skip_applying_) {
    LOG(INFO) << "Skip applying complete, check status.";
    ResetUpdateStatus();
    ScheduleUpdates();
    return;
  }

  if (code != ErrorCode::kSuccess) {
    if (ScheduleErrorEventAction()) {
      return;
    }
    LOG(INFO) << "No update.";
    ResetUpdateStatus();
    ScheduleUpdates();
    return;
  }

  prefs_->SetInt64(kPrefsDeltaUpdateFailures, 0);
  prefs_->SetString(kPrefsPreviousVersion,
                    omaha_request_params_->app_version());
  DeltaPerformer::ResetUpdateProgress(prefs_, false);

  SystemState::Get()->payload_state()->UpdateSucceeded();

  // Since we're done with scattering fully at this point, this is the
  // safest point delete the state files, as we're sure that the status is
  // set to reboot (which means no more updates will be applied until reboot)
  // This deletion is required for correctness as we want the next update
  // check to re-create a new random number for the update check count.
  // Similarly, we also delete the wall-clock-wait period that was persisted
  // so that we start with a new random value for the next update check
  // after reboot so that the same device is not favored or punished in any
  // way.
  prefs_->Delete(kPrefsUpdateCheckCount);
  SystemState::Get()->payload_state()->SetScatteringWaitPeriod(TimeDelta());
  SystemState::Get()->payload_state()->SetStagingWaitPeriod(TimeDelta());
  prefs_->Delete(kPrefsUpdateFirstSeenAt);

  // Note: below this comment should only be on |ErrorCode::kSuccess|.
  switch (pm_) {
    case ProcessMode::UPDATE:
      ProcessingDoneUpdate(processor, code);
      break;
    case ProcessMode::INSTALL:
    case ProcessMode::SCALED_INSTALL:
    case ProcessMode::FORCE_OTA_INSTALL:
      ProcessingDoneInstall(processor, code);
      break;
    case ProcessMode::MIGRATE:
      ProcessingDoneMigrate(processor, code);
      break;
  }
}

vector<string> UpdateAttempter::GetSuccessfulDlcIds() {
  vector<string> dlc_ids;
  for (const auto& pr : omaha_request_params_->dlc_apps_params()) {
    if (pr.second.updated) {
      dlc_ids.push_back(pr.second.name);
    }
  }
  return dlc_ids;
}

void UpdateAttempter::ProcessingDoneInstall(const ActionProcessor* processor,
                                            ErrorCode code) {
  if (!SystemState::Get()->dlcservice()->InstallCompleted(
          GetSuccessfulDlcIds())) {
    LOG(WARNING) << "dlcservice didn't successfully handle install completion.";
  }
  SetStatusAndNotify(UpdateStatus::IDLE);
  ScheduleUpdates();
  LOG(INFO) << "DLC successfully installed, no reboot needed.";
}

void UpdateAttempter::ProcessingDoneMigrate(const ActionProcessor* processor,
                                            ErrorCode code) {
  // TODO(b/356338530): Create a `PartitionMigrateAction`.
  // Partition migration.
  // Get partition number.
  BootControlInterface* boot_control = SystemState::Get()->boot_control();
  const auto& boot_device = boot_control->GetBootDevicePath();
  if (boot_device.empty()) {
    LOG(ERROR) << "Unable to get the boot device.";
    return;
  }
  const auto& last_slot =
      boot_control->GetHighestOffsetSlot(kPartitionNameRoot);
  const auto& last_root =
      boot_control->GetPartitionNumber(kPartitionNameRoot, last_slot);

  // Get partition layout.
  const auto& manifest = SystemState::Get()->dlc_utils()->GetDlcManifest(
      kMigrationDlcId, base::FilePath(imageloader::kDlcManifestRootpath));
  if (!manifest) {
    LOG(ERROR) << "Unable to load the manifest for migration DLC.";
    return;
  }
  std::string partitions_json;
  for (std::string_view attr : manifest->attributes()) {
    if (attr.starts_with(kPartitionsAttributePrefix) &&
        base::Base64Decode(attr.substr(kPartitionsAttributePrefix.size()),
                           &partitions_json)) {
      break;
    }
  }
  if (partitions_json.empty()) {
    LOG(ERROR) << "Failed to get partitions layout.";
    return;
  }

  if (!installer::MigratePartition(boot_device, last_root, partitions_json,
                                   /*revert=*/false)) {
    LOG(ERROR) << "Failed to update partitions.";
    return;
  }

  // Set boot priority.
  if (!boot_control->SetActiveBootPartition(kPartitionNumberBootA,
                                            kPartitionNameBootA)) {
    LOG(ERROR) << "Failed to set the boot priority on " << kPartitionNameBootA
               << ", restoring partitions.";
    installer::MigratePartition(boot_device, last_root, partitions_json,
                                /*revert=*/true);
    return;
  }

  WriteUpdateCompletedMarker();
  prefs_->SetString(kPrefsUpdateCompletedIsMigration, "");
  SetStatusAndNotify(UpdateStatus::UPDATED_NEED_REBOOT);
  LOG(INFO) << "Migration installed.";
}

void UpdateAttempter::ProcessingDoneUpdate(const ActionProcessor* processor,
                                           ErrorCode code) {
  WriteUpdateCompletedMarker();

  if (!SystemState::Get()->dlcservice()->UpdateCompleted(
          GetSuccessfulDlcIds())) {
    LOG(WARNING) << "dlcservice didn't successfully handle update completion.";
  }

  if (install_plan_) {
    switch (install_plan_->defer_update_action) {
      case DeferUpdateAction::kOff:
        SetStatusAndNotify(UpdateStatus::UPDATED_NEED_REBOOT);
        ScheduleUpdates();
        LOG(INFO) << "Update successfully applied, waiting to reboot.";
        break;
      case DeferUpdateAction::kHold:
        prefs_->SetString(kPrefsDeferredUpdateCompleted, "");
        SetStatusAndNotify(UpdateStatus::UPDATED_BUT_DEFERRED);
        ScheduleUpdates();
        LOG(INFO) << "Deferred update hold action was successful.";
        return;
      case DeferUpdateAction::kApplyAndReboot:
        SetStatusAndNotify(UpdateStatus::UPDATED_BUT_DEFERRED);
        LOG(INFO) << "Deferred update apply action was successful, "
                     "proceeding with reboot.";
        if (!ResetStatus()) {
          LOG(WARNING) << "Failed to reset status.";
        }
        RebootIfNeeded();
        return;
      case DeferUpdateAction::kApplyAndShutdown:
        SetStatusAndNotify(UpdateStatus::UPDATED_BUT_DEFERRED);
        LOG(INFO) << "Deferred update apply action was successful, "
                     "proceeding with shutdown.";
        if (!ResetStatus()) {
          LOG(WARNING) << "Failed to reset status.";
        }
        ShutdownIfNeeded();
        return;
    }
  } else {
    SetStatusAndNotify(UpdateStatus::UPDATED_NEED_REBOOT);
    ScheduleUpdates();
    LOG(INFO) << "Update successfully applied, waiting to reboot.";
  }

  // |install_plan_| is null during rollback operations, and the stats don't
  // make much sense then anyway.
  if (install_plan_) {
    int64_t num_consecutive_updates = 0;
    SystemState::Get()->prefs()->GetInt64(kPrefsConsecutiveUpdateCount,
                                          &num_consecutive_updates);
    // Increment pref after every update.
    SystemState::Get()->prefs()->SetInt64(kPrefsConsecutiveUpdateCount,
                                          ++num_consecutive_updates);
    // TODO(kimjae): Seperate out apps into categories (OS, DLC, etc).
    // Generate an unique payload identifier.
    string target_version_uid;
    for (const auto& payload : install_plan_->payloads) {
      target_version_uid += brillo::data_encoding::Base64Encode(payload.hash) +
                            ":" + payload.metadata_signature + ":";
      // Set fingerprint value for updates only.
      SetPref(kPrefsLastFp, payload.fp, payload.app_id);
    }

    // If we just downloaded a rollback image, we should preserve this fact
    // over the following powerwash.
    if (install_plan_->is_rollback) {
      SystemState::Get()->payload_state()->SetRollbackHappened(true);
      SystemState::Get()->metrics_reporter()->ReportEnterpriseRollbackMetrics(
          metrics::kMetricEnterpriseRollbackSuccess, install_plan_->version);
    }

    // Expect to reboot into the new version to send the proper metric during
    // next boot.
    SystemState::Get()->payload_state()->ExpectRebootInNewVersion(
        target_version_uid);
  } else {
    // If we just finished a rollback, then we expect to have no Omaha
    // response. Otherwise, it's an error.
    if (SystemState::Get()->payload_state()->GetRollbackVersion().empty()) {
      LOG(ERROR) << "Can't send metrics because there was no Omaha response";
    }
  }
}

// Delegate methods:
void UpdateAttempter::ProcessingDone(const ActionProcessor* processor,
                                     ErrorCode code) {
  LOG(INFO) << "Processing Done.";
  ProcessingDoneInternal(processor, code);

  // Note: do cleanups here for any variables that need to be reset after a
  // failure, error, update, or install.
  pm_ = ProcessMode::UPDATE;
  skip_applying_ = false;
  // Scheduling a check for and subscribing to the enterprise update
  // invalidation signals at the very end of update cycles.
  // That allows to invalidate updates in case if the update engine receives
  // an enterprise invalidation signal after an update cycle completes.
  // Scheduling the check here also covers the case when the signal gets
  // received during an in-progress update.
  // More details can be found in the feature tracker b/275530794.
  ScheduleEnterpriseUpdateInvalidationCheck();
}

void UpdateAttempter::ProcessingStopped(const ActionProcessor* processor) {
  // Reset cpu shares back to normal.
  cpu_limiter_.StopLimiter();
  download_progress_ = 0.0;

  ResetInteractivityFlags();

  ResetUpdateStatus();
  ScheduleUpdates();
  error_event_.reset(nullptr);
}

// Called whenever an action has finished processing, either successfully
// or otherwise.
void UpdateAttempter::ActionCompleted(ActionProcessor* processor,
                                      AbstractAction* action,
                                      ErrorCode code) {
  // Reset download progress regardless of whether or not the download
  // action succeeded. Also, get the response code from HTTP request
  // actions (update download as well as the initial update check
  // actions).
  const string type = action->Type();
  if (type == DownloadActionChromeos::StaticType()) {
    download_progress_ = 0.0;
    DownloadActionChromeos* download_action =
        static_cast<DownloadActionChromeos*>(action);
    http_response_code_ = download_action->GetHTTPResponseCode();
  } else if (type == OmahaRequestAction::StaticType()) {
    OmahaRequestAction* omaha_request_action =
        static_cast<OmahaRequestAction*>(action);
    // If the request is not an event, then it's the update-check.
    if (!omaha_request_action->IsEvent()) {
      http_response_code_ = omaha_request_action->GetHTTPResponseCode();

      // Record the number of consecutive failed update checks.
      if (http_response_code_ == kHttpResponseInternalServerError ||
          http_response_code_ == kHttpResponseServiceUnavailable) {
        consecutive_failed_update_checks_++;
      } else {
        consecutive_failed_update_checks_ = 0;
      }

      const OmahaResponse& omaha_response =
          omaha_request_action->GetOutputObject();
      // Store the server-dictated poll interval, if any.
      server_dictated_poll_interval_ =
          std::max(0, omaha_response.poll_interval);

      // This update is ignored by omaha request action because update over
      // cellular or metered connection is not allowed. Needs to ask for user's
      // permissions to update.
      if (code == ErrorCode::kOmahaUpdateIgnoredOverCellular ||
          code == ErrorCode::kOmahaUpdateIgnoredOverMetered) {
        new_version_ = omaha_response.version;
        new_payload_size_ = 0;
        for (const auto& package : omaha_response.packages) {
          new_payload_size_ += package.size;
        }
        SetStatusAndNotify(UpdateStatus::NEED_PERMISSION_TO_UPDATE);
      }

      // Although `OmahaResponseHandlerAction` will update the new version,
      // need to set here explicitly when skipping application of updates as
      // there are no followup actions.
      if (skip_applying_) {
        // Only update version if there were updates to go into from Omaha.
        if (omaha_response.update_exists) {
          new_version_ = omaha_response.version;
        }
      }
    }
  } else if (type == OmahaResponseHandlerAction::StaticType()) {
    // Depending on the returned error code, note that an update is available.
    if (code == ErrorCode::kOmahaUpdateDeferredPerPolicy ||
        code == ErrorCode::kSuccess) {
      // Note that the status will be updated to DOWNLOADING when some bytes
      // get actually downloaded from the server and the BytesReceived
      // callback is invoked. This avoids notifying the user that a download
      // has started in cases when the server and the client are unable to
      // initiate the download.
      auto omaha_response_handler_action =
          static_cast<OmahaResponseHandlerAction*>(action);
      install_plan_.reset(
          new InstallPlan(omaha_response_handler_action->install_plan()));
      UpdateLastCheckedTime();
      new_version_ = install_plan_->version;
      new_payload_size_ = 0;
      for (const auto& payload : install_plan_->payloads) {
        new_payload_size_ += payload.size;
      }
      cpu_limiter_.StartLimiter();
      SetStatusAndNotify(UpdateStatus::UPDATE_AVAILABLE);
    }
  } else if (type == InstallAction::StaticType()) {
    // TODO(b/236008158): Report metrics here.
    if (code == ErrorCode::kSuccess) {
      LOG(INFO) << "InstallAction succeeded.";
    } else {
      LOG(INFO) << "InstallAction failed.";
    }
  }

  // General failure cases.
  if (code != ErrorCode::kSuccess) {
    // Best effort to invalidate the previous update by resetting the active
    // boot slot and update complete markers. Status will go back to 'IDLE'.
    if (code == ErrorCode::kInvalidateLastUpdate) {
      InvalidateUpdate();
      return;
    }

    // If the current state is at or past the download phase, count the failure
    // in case a switch to full update becomes necessary. Ignore network
    // transfer timeouts and failures.
    if (code != ErrorCode::kDownloadTransferError) {
      switch (status_) {
        case UpdateStatus::IDLE:
        case UpdateStatus::CHECKING_FOR_UPDATE:
        case UpdateStatus::UPDATE_AVAILABLE:
        case UpdateStatus::NEED_PERMISSION_TO_UPDATE:
          // Errored out before partition marked unbootable.
          break;
        case UpdateStatus::DOWNLOADING:
        case UpdateStatus::VERIFYING:
        case UpdateStatus::FINALIZING:
        case UpdateStatus::UPDATED_NEED_REBOOT:
        case UpdateStatus::REPORTING_ERROR_EVENT:
        case UpdateStatus::ATTEMPTING_ROLLBACK:
        case UpdateStatus::DISABLED:
        case UpdateStatus::CLEANUP_PREVIOUS_UPDATE:
        case UpdateStatus::UPDATED_BUT_DEFERRED:
          MarkDeltaUpdateFailure();
          // Errored out after partition was marked unbootable.
          int64_t num_consecutive_updates = 0;
          SystemState::Get()->prefs()->GetInt64(kPrefsConsecutiveUpdateCount,
                                                &num_consecutive_updates);
          if (num_consecutive_updates >= 1) {
            // There has already been at least 1 update, so this is a
            // consecutive update that failed. Send Metric.
            SystemState::Get()
                ->metrics_reporter()
                ->ReportFailedConsecutiveUpdate();
          }
          break;
      }
    }
    if (code != ErrorCode::kNoUpdate) {
      // On failure, schedule an error event to be sent to Omaha.
      CreatePendingErrorEvent(action, code);
    }
    return;
  }
  // Find out which action completed (successfully).
  if (type == DownloadActionChromeos::StaticType()) {
    SetStatusAndNotify(UpdateStatus::FINALIZING);
  } else if (type == FilesystemVerifierAction::StaticType()) {
    // Log the system properties before the postinst and after the file system
    // is verified. It used to be done in the postinst itself. But postinst
    // cannot do this anymore. On the other hand, these logs are frequently
    // looked at and it is preferable not to scatter them in random location in
    // the log and rather log it right before the postinst. The reason not do
    // this in the |PostinstallRunnerAction| is to prevent dependency from
    // libpayload_consumer to libupdate_engine.
    LogImageProperties();
  }
}

void UpdateAttempter::ProgressUpdate(uint64_t bytes_received, uint64_t total) {
  double progress = 0;
  if (total) {
    progress = static_cast<double>(bytes_received) / static_cast<double>(total);
  }
  if (status_ != UpdateStatus::DOWNLOADING || bytes_received == total) {
    download_progress_ = progress;
    SetStatusAndNotify(UpdateStatus::DOWNLOADING);
  } else {
    ProgressUpdate(progress);
  }
}

void UpdateAttempter::BytesReceived(uint64_t bytes_progressed,
                                    uint64_t bytes_received,
                                    uint64_t total) {
  // The PayloadState keeps track of how many bytes were actually downloaded
  // from a given URL for the URL skipping logic.
  SystemState::Get()->payload_state()->DownloadProgress(bytes_progressed);
  ProgressUpdate(bytes_received, total);
}

void UpdateAttempter::BytesReceived(uint64_t bytes_received, uint64_t total) {
  ProgressUpdate(bytes_received, total);
}

void UpdateAttempter::ResetUpdateStatus() {
  // If `GetBootTimeAtUpdate` is true, then the update complete markers exist
  // and there is an update in the inactive partition waiting to be applied.
  if (GetBootTimeAtUpdate(nullptr)) {
    LOG(INFO)
        << "Cancelling current update but going back to need reboot as there "
           "is an update in the inactive partition that can be applied.";
    if (prefs_->Exists(kPrefsDeferredUpdateCompleted)) {
      SetStatusAndNotify(UpdateStatus::UPDATED_BUT_DEFERRED);
    } else {
      SetStatusAndNotify(UpdateStatus::UPDATED_NEED_REBOOT);
    }
    return;
  }
  // One full update never completed or there no longer an inactive partition
  // from a previous update with a higher boot priority to reboot to. No choice
  // but to go back to idle.
  SetStatusAndNotify(UpdateStatus::IDLE);
}

bool UpdateAttempter::ResetUpdatePrefs() {
  auto* prefs = SystemState::Get()->prefs();
  bool ret_value = prefs->Delete(kPrefsUpdateCompletedOnBootId);
  ret_value = prefs->Delete(kPrefsUpdateCompletedBootTime) && ret_value;
  ret_value = prefs->Delete(kPrefsLastFp, {kDlcPrefsSubDir}) && ret_value;
  ret_value = prefs->Delete(kPrefsPreviousVersion) && ret_value;
  ret_value = prefs->Delete(kPrefsDeferredUpdateCompleted) && ret_value;
  ret_value = prefs->Delete(kPrefsUpdateCompletedIsMigration) && ret_value;
  return ret_value;
}

bool UpdateAttempter::InvalidateUpdate() {
  if (!GetBootTimeAtUpdate(nullptr)) {
    LOG(INFO) << "No previous update available to invalidate.";
    return true;
  }

  LOG(INFO) << "Invalidating previous update.";
  bool success = true;
  if (!ResetBootSlot()) {
    LOG(WARNING) << "Could not reset boot slot to active partition. "
                    "Continuing anyway.";
    success = false;
  }
  if (!ResetUpdatePrefs()) {
    LOG(WARNING)
        << "Could not delete update completed markers. Continuing anyway.";
    success = false;
  }

  LOG(INFO) << "Clearing powerwash and rollback flags, if any.";
  const std::optional<bool> is_powerwash_scheduled_by_update_engine =
      SystemState::Get()->hardware()->IsPowerwashScheduledByUpdateEngine();
  if (!is_powerwash_scheduled_by_update_engine) {
    LOG(INFO) << "Powerwash is not scheduled, continuing.";
  } else if (!is_powerwash_scheduled_by_update_engine.value()) {
    LOG(INFO) << "Not cancelling powerwash. Either not initiated by update "
                 "engine or there was a parsing error.";
  } else {
    LOG(INFO) << "Cancelling powerwash that was initiated by update engine.";
    if (!SystemState::Get()->hardware()->CancelPowerwash()) {
      LOG(WARNING) << "Failed to cancel powerwash. Continuing anyway.";
      success = false;
    }
  }
  SystemState::Get()->payload_state()->SetRollbackHappened(false);

  LOG(INFO) << "Invalidating firmware update.";
  if (!SystemState::Get()->hardware()->ResetFWTryNextSlot()) {
    LOG(WARNING) << "Could not reset firmware slot. Continuing anyway.";
    success = false;
  }

  SystemState::Get()->metrics_reporter()->ReportInvalidatedUpdate(success);

  return success;
}

void UpdateAttempter::DownloadComplete() {
  SystemState::Get()->payload_state()->DownloadComplete();
}

void UpdateAttempter::ProgressUpdate(double progress) {
  // Self throttle based on progress. Also send notifications if progress is
  // too slow.
  if (progress == 1.0 ||
      progress - download_progress_ >= kBroadcastThresholdProgress ||
      TimeTicks::Now() - last_notify_time_ >= kBroadcastThreshold) {
    download_progress_ = progress;
    BroadcastStatus();
  }
}

void UpdateAttempter::ResetInteractivityFlags() {
  // Reset the state that's only valid for a single update pass.
  current_update_flags_.Clear();

  if (forced_update_pending_callback_.get()) {
    // Clear prior interactive requests once the processor is done.
    forced_update_pending_callback_->Run(false, false);
  }
}

bool UpdateAttempter::ResetBootSlot() {
  bool success = true;
  // Update the boot flags so the current slot has higher priority.
  BootControlInterface* boot_control = SystemState::Get()->boot_control();
  if (!boot_control->SetActiveBootSlot(boot_control->GetCurrentSlot())) {
    LOG(WARNING) << "Unable to set the current slot as active.";
    success = false;
  }

  // Mark the current slot as successful again, since marking it as active
  // may reset the successful bit. We ignore the result of whether marking
  // the current slot as successful worked. This call must be synchronous as
  // concurrent calls into `cgpt` can cause corrupt GPT headers.
  if (!boot_control->MarkBootSuccessful()) {
    LOG(WARNING) << "Unable to mark the current slot as successfully booted.";
    success = false;
  }
  return success;
}

bool UpdateAttempter::ResetStatus() {
  LOG(INFO) << "Attempting to reset state from "
            << UpdateStatusToString(status_) << " to UpdateStatus::IDLE";

  switch (status_) {
    case UpdateStatus::IDLE:
      // no-op.
      return true;

    case UpdateStatus::UPDATED_NEED_REBOOT: {
      bool ret_value = true;
      status_ = UpdateStatus::IDLE;
      // Send metrics before resetting.
      ReportConsecutiveUpdateMetric();
      // Remove the reboot marker so that if the machine is rebooted
      // after resetting to idle state, it doesn't go back to
      // UpdateStatus::UPDATED_NEED_REBOOT state.
      ret_value = ResetUpdatePrefs() && ret_value;
      ret_value = prefs_->Delete(kPrefsConsecutiveUpdateCount) && ret_value;

      ret_value = ResetBootSlot() && ret_value;

      // Notify the PayloadState that the successful payload was canceled.
      SystemState::Get()->payload_state()->ResetUpdateStatus();

      // The previous version is used to report back to omaha after reboot that
      // we actually rebooted into the new version from this "prev-version". We
      // need to clear out this value now to prevent it being sent on the next
      // updatecheck request.
      ret_value = prefs_->SetString(kPrefsPreviousVersion, "") && ret_value;

      LOG(INFO) << "Reset status " << (ret_value ? "successful" : "failed");
      return ret_value;
    }
    case UpdateStatus::UPDATED_BUT_DEFERRED: {
      bool ret_value = true;
      status_ = UpdateStatus::IDLE;
      ret_value = ResetUpdatePrefs() && ret_value;

      // Notify the PayloadState that the successful payload was canceled.
      SystemState::Get()->payload_state()->ResetUpdateStatus();

      // The previous version is used to report back to omaha after reboot that
      // we actually rebooted into the new version from this "prev-version". We
      // need to clear out this value now to prevent it being sent on the next
      // updatecheck request.
      ret_value = prefs_->SetString(kPrefsPreviousVersion, "") && ret_value;

      LOG(INFO) << "Reset status " << (ret_value ? "successful" : "failed");
      return ret_value;
    }
    default:
      LOG(ERROR) << "Reset not allowed in this state.";
      return false;
  }
}

bool UpdateAttempter::GetStatus(UpdateEngineStatus* out_status) {
  out_status->last_checked_time = last_checked_time_;
  out_status->status = status_;
  out_status->current_version = omaha_request_params_->app_version();
  out_status->progress = download_progress_;
  out_status->new_size_bytes = new_payload_size_;
  out_status->new_version = new_version_;
  out_status->is_enterprise_rollback =
      install_plan_ && install_plan_->is_rollback;
  out_status->is_install =
      (pm_ == ProcessMode::INSTALL || pm_ == ProcessMode::SCALED_INSTALL ||
       pm_ == ProcessMode::FORCE_OTA_INSTALL || pm_ == ProcessMode::MIGRATE);
  out_status->update_urgency_internal =
      install_plan_ ? install_plan_->update_urgency
                    : update_engine::UpdateUrgencyInternal::REGULAR;

  string str_eol_date;
  if (SystemState::Get()->prefs()->Exists(kPrefsOmahaEolDate) &&
      !SystemState::Get()->prefs()->GetString(kPrefsOmahaEolDate,
                                              &str_eol_date)) {
    LOG(ERROR) << "Failed to retrieve kPrefsOmahaEolDate pref.";
  }
  out_status->eol_date = StringToDate(str_eol_date);

  string str_extended_date;
  if (SystemState::Get()->prefs()->Exists(kPrefsOmahaExtendedDate) &&
      !SystemState::Get()->prefs()->GetString(kPrefsOmahaExtendedDate,
                                              &str_extended_date)) {
    LOG(ERROR) << "Failed to retrieve kPrefsOmahaExtendedDate pref.";
  }
  out_status->extended_date = StringToDate(str_extended_date);

  out_status->extended_opt_in_required = false;
  if (SystemState::Get()->prefs()->Exists(kPrefsOmahaExtendedOptInRequired) &&
      !SystemState::Get()->prefs()->GetBoolean(
          kPrefsOmahaExtendedOptInRequired,
          &out_status->extended_opt_in_required)) {
    LOG(ERROR) << "Failed to retrieve kPrefsOmahaExtendedOptInRequired pref.";
  }

  // A powerwash will take place either if the install plan says it is required
  // or if an enterprise rollback is happening.
  out_status->will_powerwash_after_reboot =
      install_plan_ &&
      (install_plan_->powerwash_required || install_plan_->is_rollback);

  out_status->last_attempt_error = static_cast<int32_t>(GetLastUpdateError());

  FeatureInternalList features;
  for (const auto& feature : {update_engine::kFeatureRepeatedUpdates,
                              update_engine::kFeatureConsumerAutoUpdate}) {
    bool enabled;
    if (IsFeatureEnabled(feature, &enabled)) {
      features.push_back({
          .name = feature,
          .enabled = enabled,
      });
    } else {
      LOG(ERROR) << "Failed to read feature (" << feature << ").";
    }
  }
  out_status->features = std::move(features);
  out_status->is_interactive = omaha_request_params_->interactive();
  out_status->will_defer_update =
      install_plan_ &&
      install_plan_->defer_update_action == DeferUpdateAction::kHold;

  return true;
}

void UpdateAttempter::SetStatusAndNotify(UpdateStatus status) {
  status_ = status;
  BroadcastStatus();
}

ErrorCode UpdateAttempter::GetLastUpdateError() {
  switch (attempt_error_code_) {
    case ErrorCode::kSuccess:
    case ErrorCode::kNoUpdate:
    case ErrorCode::kInvalidateLastUpdate:
    case ErrorCode::kOmahaErrorInHTTPResponse:
    case ErrorCode::kUpdateIgnoredRollbackVersion:
      return attempt_error_code_;
    case ErrorCode::kInternalLibCurlError:
    case ErrorCode::kUnresolvedHostError:
    case ErrorCode::kDownloadTransferError:
      // Server or network error.
      return ErrorCode::kDownloadTransferError;
    case ErrorCode::kDownloadCancelledPerPolicy:
    case ErrorCode::kOmahaUpdateIgnoredPerPolicy:
      // Policy is blocking the update completely.
      return ErrorCode::kOmahaUpdateIgnoredPerPolicy;
    default:
      return ErrorCode::kError;
  }
}

void UpdateAttempter::BroadcastStatus() {
  UpdateEngineStatus broadcast_status;
  // Use common method for generating the current status.
  GetStatus(&broadcast_status);

  for (const auto& observer : service_observers_) {
    observer->SendStatusUpdate(broadcast_status);
  }
  last_notify_time_ = TimeTicks::Now();
}

uint32_t UpdateAttempter::GetErrorCodeFlags() {
  uint32_t flags = 0;

  if (!SystemState::Get()->hardware()->IsNormalBootMode()) {
    flags |= static_cast<uint32_t>(ErrorCode::kDevModeFlag);
  }

  if (install_plan_ && install_plan_->is_resume) {
    flags |= static_cast<uint32_t>(ErrorCode::kResumedFlag);
  }

  if (!SystemState::Get()->hardware()->IsOfficialBuild()) {
    flags |= static_cast<uint32_t>(ErrorCode::kTestImageFlag);
  }

  if (!omaha_request_params_->IsUpdateUrlOfficial()) {
    flags |= static_cast<uint32_t>(ErrorCode::kTestOmahaUrlFlag);
  }

  return flags;
}

bool UpdateAttempter::ShouldCancel(ErrorCode* cancel_reason) {
  // Check if the channel we're attempting to update to is the same as the
  // target channel currently chosen by the user.
  OmahaRequestParams* params = SystemState::Get()->request_params();
  if (params->download_channel() != params->target_channel()) {
    LOG(ERROR) << "Aborting download as target channel: "
               << params->target_channel()
               << " is different from the download channel: "
               << params->download_channel();
    *cancel_reason = ErrorCode::kUpdateCanceledByChannelChange;
    return true;
  }

  // Check if updates are disabled by the enterprise policy. Cancel the download
  // if disabled.
  if (SystemState::Get()->update_manager()->PolicyRequest(
          std::make_unique<EnterpriseUpdateDisabledPolicyImpl>(), nullptr) ==
      EvalStatus::kSucceeded) {
    LOG(ERROR) << "Cancelling download as updates have been disabled by "
                  "enterprise policy";
    *cancel_reason = ErrorCode::kDownloadCancelledPerPolicy;
    return true;
  }

  return false;
}

void UpdateAttempter::CreatePendingErrorEvent(AbstractAction* action,
                                              ErrorCode code) {
  if (error_event_.get() || status_ == UpdateStatus::REPORTING_ERROR_EVENT) {
    // This shouldn't really happen.
    LOG(WARNING) << "There's already an existing pending error event.";
    return;
  }

  // Classify the code to generate the appropriate result so that
  // the Borgmon charts show up the results correctly.
  // Do this before calling GetErrorCodeForAction which could potentially
  // augment the bit representation of code and thus cause no matches for
  // the switch cases below.
  OmahaEvent::Result event_result;
  switch (code) {
    case ErrorCode::kOmahaUpdateIgnoredPerPolicy:
    case ErrorCode::kUpdateIgnoredRollbackVersion:
    case ErrorCode::kOmahaUpdateDeferredPerPolicy:
    case ErrorCode::kOmahaUpdateDeferredForBackoff:
      event_result = OmahaEvent::kResultUpdateDeferred;
      break;
    default:
      event_result = OmahaEvent::kResultError;
      break;
  }

  code = GetErrorCodeForAction(action, code);
  fake_update_success_ = code == ErrorCode::kPostinstallBootedFromFirmwareB;

  // Compute the final error code with all the bit flags to be sent to Omaha.
  code =
      static_cast<ErrorCode>(static_cast<uint32_t>(code) | GetErrorCodeFlags());
  error_event_.reset(
      new OmahaEvent(OmahaEvent::kTypeUpdateComplete, event_result, code));
}

bool UpdateAttempter::ScheduleErrorEventAction() {
  if (error_event_.get() == nullptr) {
    return false;
  }

  LOG(ERROR) << "Update failed.";
  SystemState::Get()->payload_state()->UpdateFailed(error_event_->error_code);

  // Send metrics if it was a rollback.
  if (install_plan_ && install_plan_->is_rollback) {
    // Powerwash is not imminent because the Enterprise Rollback update failed,
    // report the failure immediately.
    rollback_metrics_->ReportEventNow(
        oobe_config::EnterpriseRollbackMetricsHandler::CreateEventData(
            EnterpriseRollbackEvent::ROLLBACK_UPDATE_FAILURE));
    // TODO(b/301924474): Clean old UMA metric.
    SystemState::Get()->metrics_reporter()->ReportEnterpriseRollbackMetrics(
        metrics::kMetricEnterpriseRollbackFailure, install_plan_->version);
  }

  if (install_plan_ && (install_plan_->defer_update_action ==
                            DeferUpdateAction::kApplyAndReboot ||
                        install_plan_->defer_update_action ==
                            DeferUpdateAction::kApplyAndShutdown)) {
    // TODO(kimjae): Report deferred update apply action failure metric.
  }

  // Send it to Omaha.
  LOG(INFO) << "Reporting the error event";
  auto error_event_action = std::make_unique<OmahaRequestAction>(
      error_event_.release(),  // Pass ownership.
      std::make_unique<LibcurlHttpFetcher>(GetProxyResolver(),
                                           SystemState::Get()->hardware()),
      false, session_id_);
  processor_->EnqueueAction(std::move(error_event_action));
  SetStatusAndNotify(UpdateStatus::REPORTING_ERROR_EVENT);
  processor_->StartProcessing();
  return true;
}

void UpdateAttempter::ScheduleProcessingStart() {
  LOG(INFO) << "Scheduling an action processor start.";
  MessageLoop::current()->PostTask(FROM_HERE,
                                   base::BindOnce(
                                       [](ActionProcessor* processor) {
                                         if (!processor->IsRunning()) {
                                           processor->StartProcessing();
                                         }
                                       },
                                       base::Unretained(processor_.get())));
}

void UpdateAttempter::DisableDeltaUpdateIfNeeded() {
  int64_t delta_failures;
  if (omaha_request_params_->delta_okay() &&
      prefs_->GetInt64(kPrefsDeltaUpdateFailures, &delta_failures) &&
      delta_failures >= kMaxDeltaUpdateFailures) {
    LOG(WARNING) << "Too many delta update failures, forcing full update.";
    omaha_request_params_->set_delta_okay(false);
  }
}

void UpdateAttempter::DetermineExtendedUpdateValue() {
  const policy::DevicePolicy* device_policy =
      SystemState::Get()->device_policy();
  if (!device_policy) {
    return;
  }
  // Always default `extended_okay` to false in case retrieval fails.
  bool extend_okay =
      device_policy->GetDeviceExtendedAutoUpdateEnabled().value_or(false);
  omaha_request_params_->set_extended_okay(extend_okay);
}

void UpdateAttempter::MarkDeltaUpdateFailure() {
  // Don't try to resume a failed delta update.
  DeltaPerformer::ResetUpdateProgress(prefs_, false);
  int64_t delta_failures;
  if (!prefs_->GetInt64(kPrefsDeltaUpdateFailures, &delta_failures) ||
      delta_failures < 0) {
    delta_failures = 0;
  }
  prefs_->SetInt64(kPrefsDeltaUpdateFailures, ++delta_failures);
}

void UpdateAttempter::PingOmaha() {
  if (!processor_->IsRunning()) {
    ResetInteractivityFlags();

    auto ping_action = std::make_unique<OmahaRequestAction>(
        nullptr,
        std::make_unique<LibcurlHttpFetcher>(GetProxyResolver(),
                                             SystemState::Get()->hardware()),
        true, "" /* session_id */);
    processor_->set_delegate(nullptr);
    processor_->EnqueueAction(std::move(ping_action));
    // Call StartProcessing() synchronously here to avoid any race conditions
    // caused by multiple outstanding ping Omaha requests.  If we call
    // StartProcessing() asynchronously, the device can be suspended before we
    // get a chance to callback to StartProcessing().  When the device resumes
    // (assuming the device sleeps longer than the next update check period),
    // StartProcessing() is called back and at the same time, the next update
    // check is fired which eventually invokes StartProcessing().  A crash
    // can occur because StartProcessing() checks to make sure that the
    // processor is idle which it isn't due to the two concurrent ping Omaha
    // requests.
    processor_->StartProcessing();
  } else {
    LOG(WARNING) << "Action processor running, Omaha ping suppressed.";
  }

  // Update the last check time here; it may be re-updated when an Omaha
  // response is received, but this will prevent us from repeatedly scheduling
  // checks in the case where a response is not received.
  UpdateLastCheckedTime();

  // Update the status which will schedule the next update check
  if (prefs_->Exists(kPrefsDeferredUpdateCompleted)) {
    SetStatusAndNotify(UpdateStatus::UPDATED_BUT_DEFERRED);
  } else {
    SetStatusAndNotify(UpdateStatus::UPDATED_NEED_REBOOT);
  }
  ScheduleUpdates();
}

bool UpdateAttempter::DecrementUpdateCheckCount() {
  int64_t update_check_count_value;

  if (!prefs_->Exists(kPrefsUpdateCheckCount)) {
    // This file does not exist. This means we haven't started our update
    // check count down yet, so nothing more to do. This file will be created
    // later when we first satisfy the wall-clock-based-wait period.
    LOG(INFO) << "No existing update check count. That's normal.";
    return true;
  }

  if (prefs_->GetInt64(kPrefsUpdateCheckCount, &update_check_count_value)) {
    // Only if we're able to read a proper integer value, then go ahead
    // and decrement and write back the result in the same file, if needed.
    LOG(INFO) << "Update check count = " << update_check_count_value;

    if (update_check_count_value == 0) {
      // It could be 0, if, for some reason, the file didn't get deleted
      // when we set our status to waiting for reboot. so we just leave it
      // as is so that we can prevent another update_check wait for this client.
      LOG(INFO) << "Not decrementing update check count as it's already 0.";
      return true;
    }

    if (update_check_count_value > 0) {
      update_check_count_value--;
    } else {
      update_check_count_value = 0;
    }

    // Write out the new value of update_check_count_value.
    if (prefs_->SetInt64(kPrefsUpdateCheckCount, update_check_count_value)) {
      // We successfully wrote out the new value, so enable the
      // update check based wait.
      LOG(INFO) << "New update check count = " << update_check_count_value;
      return true;
    }
  }

  LOG(INFO) << "Deleting update check count state due to read/write errors.";

  // We cannot read/write to the file, so disable the update check based wait
  // so that we don't get stuck in this OS version by any chance (which could
  // happen if there's some bug that causes to read/write incorrectly).
  // Also attempt to delete the file to do our best effort to cleanup.
  prefs_->Delete(kPrefsUpdateCheckCount);
  return false;
}

void UpdateAttempter::UpdateEngineStarted() {
  // If we just booted into a new update, keep the previous OS version
  // in case we rebooted because of a crash of the old version, so we
  // can do a proper crash report with correct information.
  // This must be done before calling
  // SystemState::Get()->payload_state()->UpdateEngineStarted() since it will
  // delete SystemUpdated marker file.
  if (SystemState::Get()->system_rebooted() &&
      prefs_->Exists(kPrefsSystemUpdatedMarker)) {
    if (!prefs_->GetString(kPrefsPreviousVersion, &prev_version_)) {
      // If we fail to get the version string, make sure it stays empty.
      prev_version_.clear();
    }
  }

  MoveToPrefs({kPrefsLastRollCallPingDay, kPrefsLastActivePingDay});

  SystemState::Get()->payload_state()->UpdateEngineStarted();
  StartP2PAtStartup();
}

void UpdateAttempter::MoveToPrefs(const vector<string>& keys) {
  auto* powerwash_safe_prefs = SystemState::Get()->powerwash_safe_prefs();
  for (const auto& key : keys) {
    // Do not overwrite existing pref key with powerwash prefs.
    if (!prefs_->Exists(key) && powerwash_safe_prefs->Exists(key)) {
      string value;
      if (!powerwash_safe_prefs->GetString(key, &value) ||
          !prefs_->SetString(key, value)) {
        PLOG(ERROR) << "Unable to add powerwash safe key " << key
                    << " to prefs. Powerwash safe key will be deleted.";
      }
    }
    // Delete keys regardless of operation success to preserve privacy.
    powerwash_safe_prefs->Delete(key);
  }
}

bool UpdateAttempter::StartP2PAtStartup() {
  if (!SystemState::Get()->p2p_manager()->IsP2PEnabled()) {
    LOG(INFO) << "Not starting p2p at startup since it's not enabled.";
    return false;
  }

  if (SystemState::Get()->p2p_manager()->CountSharedFiles() < 1) {
    LOG(INFO) << "Not starting p2p at startup since our application "
              << "is not sharing any files.";
    return false;
  }

  return StartP2PAndPerformHousekeeping();
}

bool UpdateAttempter::StartP2PAndPerformHousekeeping() {
  if (!SystemState::Get()->p2p_manager()->IsP2PEnabled()) {
    LOG(INFO) << "Not starting p2p since it's not enabled.";
    return false;
  }

  LOG(INFO) << "Ensuring that p2p is running.";
  if (!SystemState::Get()->p2p_manager()->EnsureP2PRunning()) {
    LOG(ERROR) << "Error starting p2p.";
    return false;
  }

  LOG(INFO) << "Performing p2p housekeeping.";
  if (!SystemState::Get()->p2p_manager()->PerformHousekeeping()) {
    LOG(ERROR) << "Error performing housekeeping for p2p.";
    return false;
  }

  LOG(INFO) << "Done performing p2p housekeeping.";
  return true;
}

bool UpdateAttempter::GetBootTimeAtUpdate(Time* out_boot_time) {
  // In case of an update_engine restart without a reboot, we stored the boot_id
  // when the update was completed by setting a pref, so we can check whether
  // the last update was on this boot or a previous one.
  string boot_id;
  TEST_AND_RETURN_FALSE(utils::GetBootId(&boot_id));

  // Reboots are allowed when updates get deferred, since they are actually
  // applied just not active. Hence the check on `kPrefsDeferredUpdate`.
  string update_completed_on_boot_id;
  if (!prefs_->Exists(kPrefsDeferredUpdateCompleted) &&
      (!prefs_->Exists(kPrefsUpdateCompletedOnBootId) ||
       !prefs_->GetString(kPrefsUpdateCompletedOnBootId,
                          &update_completed_on_boot_id) ||
       update_completed_on_boot_id != boot_id)) {
    return false;
  }

  // Short-circuit avoiding the read in case out_boot_time is nullptr.
  if (out_boot_time) {
    int64_t boot_time = 0;
    // Since the kPrefsUpdateCompletedOnBootId was correctly set, this pref
    // should not fail.
    TEST_AND_RETURN_FALSE(
        prefs_->GetInt64(kPrefsUpdateCompletedBootTime, &boot_time));
    *out_boot_time = Time::FromInternalValue(boot_time);
  }
  return true;
}

bool UpdateAttempter::IsBusyOrUpdateScheduled() {
  return ((status_ != UpdateStatus::IDLE &&
           status_ != UpdateStatus::UPDATED_NEED_REBOOT) ||
          waiting_for_scheduled_check_ || IsMigration());
}

bool UpdateAttempter::IsAnyUpdateSourceAllowed() const {
  // We allow updates from any source if either of these are true:
  //  * The device is running an unofficial (dev/test) image.
  //  * The debugd dev features are accessible (i.e. in devmode with no owner).
  // This protects users running a base image, while still allowing a specific
  // window (gated by the debug dev features) where `cros flash` is usable.
  if (!SystemState::Get()->hardware()->IsOfficialBuild()) {
    LOG(INFO) << "Non-official build; allowing any update source.";
    return true;
  }

  if (SystemState::Get()->hardware()->AreDevFeaturesEnabled()) {
    LOG(INFO) << "Developer features enabled; allowing custom update sources.";
    return true;
  }

  LOG(INFO)
      << "Developer features disabled; disallowing custom update sources.";
  return false;
}

bool UpdateAttempter::IsRepeatedUpdatesEnabled() {
  auto* prefs = SystemState::Get()->prefs();

  // Limit the number of repeated updates allowed as a safeguard on client.
  // Whether consecutive update feature is allowed or not.
  // Refer to b/201737820.
  int64_t consecutive_updates = 0;
  prefs->GetInt64(kPrefsConsecutiveUpdateCount, &consecutive_updates);
  if (consecutive_updates >= kConsecutiveUpdateLimit) {
    LOG(WARNING) << "Not allowing repeated updates as limit reached.";
    return false;
  }

  bool allow_repeated_updates;
  if (!SystemState::Get()->prefs()->GetBoolean(kPrefsAllowRepeatedUpdates,
                                               &allow_repeated_updates)) {
    // Defaulting to true.
    return true;
  }

  return allow_repeated_updates;
}

bool UpdateAttempter::ToggleFeature(const std::string& feature, bool enable) {
  bool ret = false;
  if (feature == update_engine::kFeatureRepeatedUpdates) {
    ret = utils::ToggleFeature(kPrefsAllowRepeatedUpdates, enable);
  } else if (feature == update_engine::kFeatureConsumerAutoUpdate) {
    // Pref will hold "disable" of consumer auto update.
    // So `not` the incoming `enable` to express this.
    ret = utils::ToggleFeature(kPrefsConsumerAutoUpdateDisabled, !enable);
  } else {
    LOG(WARNING) << "Feature (" << feature << ") is not supported.";
    ret = false;
  }
  // Always broadcast out in case callers cache the values of a feature.
  BroadcastStatus();
  return ret;
}

bool UpdateAttempter::IsFeatureEnabled(const std::string& feature,
                                       bool* out_enabled) const {
  if (feature == update_engine::kFeatureRepeatedUpdates) {
    return utils::IsFeatureEnabled(kPrefsAllowRepeatedUpdates, out_enabled);
  }
  if (feature == update_engine::kFeatureConsumerAutoUpdate) {
    bool consumer_auto_update_disabled = false;
    if (!utils::IsFeatureEnabled(kPrefsConsumerAutoUpdateDisabled,
                                 &consumer_auto_update_disabled)) {
      return false;
    }
    *out_enabled = !consumer_auto_update_disabled;
    return true;
  }
  LOG(WARNING) << "Feature (" << feature << ") is not supported.";
  return false;
}

void UpdateAttempter::RootfsIntegrityCheck() const {
  int error_counter = kErrorCounterZeroValue;
  auto* boot_control = SystemState::Get()->boot_control();
  if (!boot_control->GetErrorCounter(boot_control->GetCurrentSlot(),
                                     &error_counter)) {
    LOG(ERROR)
        << "Failed to get error counter, skipping rootfs integrity check.";
    return;
  }

  // Don't need to integrity check unless kernel has non-zero error counter.
  if (error_counter == kErrorCounterZeroValue) {
    LOG(INFO)
        << "Error counter is zero value, skipping rootfs integrity check.";
    return;
  }

  if (!SystemState::Get()->hardware()->IsRootfsVerificationEnabled()) {
    LOG(INFO)
        << "Rootfs verification is disable, skipping rootfs integrity check.";
    return;
  }

  if (Subprocess::Get().Exec(
          {"/bin/dd", "if=/dev/dm-0", "of=/dev/null", "bs=1MiB"},
          base::BindOnce(&UpdateAttempter::OnRootfsIntegrityCheck,
                         weak_ptr_factory_.GetWeakPtr())) == 0) {
    LOG(ERROR) << "Failed to launch rootfs integrity check process.";
    return;
  }
}

void UpdateAttempter::OnRootfsIntegrityCheck(int ret_code,
                                             const std::string& output) const {
  if (ret_code != 0) {
    LOG(ERROR) << "Rootfs integrity check failed with return code=" << ret_code
               << " will not reset error counter.";
    return;
  }

  LOG(INFO) << "Rootfs integrity check succeeded, resetting error counter.";

  auto* boot_control = SystemState::Get()->boot_control();
  if (!boot_control->SetErrorCounter(boot_control->GetCurrentSlot(),
                                     kErrorCounterZeroValue)) {
    LOG(ERROR) << "Failed to set error counter back to "
               << kErrorCounterZeroValue;
    return;
  }
}

bool UpdateAttempter::IsMigration() const {
  return status_ == UpdateStatus::UPDATED_NEED_REBOOT &&
         prefs_->Exists(kPrefsUpdateCompletedIsMigration);
}

}  // namespace chromeos_update_engine
