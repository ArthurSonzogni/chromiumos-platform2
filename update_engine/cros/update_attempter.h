//
// Copyright (C) 2012 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#ifndef UPDATE_ENGINE_CROS_UPDATE_ATTEMPTER_H_
#define UPDATE_ENGINE_CROS_UPDATE_ATTEMPTER_H_

#include <time.h>

#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <base/functional/bind.h>
#include <base/time/time.h>
#include <base/uuid.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include "update_engine/certificate_checker.h"
#include "update_engine/client_library/include/update_engine/update_status.h"
#include "update_engine/common/action_processor.h"
#include "update_engine/common/cpu_limiter.h"
#include "update_engine/common/daemon_state_interface.h"
#include "update_engine/common/download_action.h"
#include "update_engine/common/excluder_interface.h"
#include "update_engine/common/proxy_resolver.h"
#include "update_engine/common/service_observer_interface.h"
#include "update_engine/common/system_state.h"
#include "update_engine/cros/chrome_browser_proxy_resolver.h"
#include "update_engine/cros/install_action.h"
#include "update_engine/cros/omaha_request_builder_xml.h"
#include "update_engine/cros/omaha_request_params.h"
#include "update_engine/cros/omaha_response_handler_action.h"
#include "update_engine/payload_consumer/postinstall_runner_action.h"
#include "update_engine/proto_bindings/update_engine.pb.h"
#include "update_engine/update_manager/staging_utils.h"
#include "update_engine/update_manager/update_check_allowed_policy.h"
#include "update_engine/update_manager/update_manager.h"

namespace policy {
class PolicyProvider;
}

namespace chromeos_update_engine {

// The different types of top level operations that are processed through.
enum class ProcessMode {
  UPDATE,
  INSTALL,
  SCALED_INSTALL,
};

class UpdateAttempter : public ActionProcessorDelegate,
                        public DownloadActionDelegate,
                        public CertificateChecker::Observer,
                        public InstallActionDelegate,
                        public PostinstallRunnerAction::DelegateInterface,
                        public DaemonStateInterface {
 public:
  struct ScheduleUpdatesParams {
    bool force_fw_update{false};
  };

  using UpdateStatus = update_engine::UpdateStatus;
  static const int kMaxDeltaUpdateFailures;

  explicit UpdateAttempter(CertificateChecker* cert_checker);
  UpdateAttempter(const UpdateAttempter&) = delete;
  UpdateAttempter& operator=(const UpdateAttempter&) = delete;

  ~UpdateAttempter() override;

  // Further initialization to be done post construction.
  void Init();

  // Returns true if updating, otherwise (installing) false.
  virtual bool IsUpdating();

  // Initiates scheduling of update checks.
  // Returns true if update check is scheduled.
  virtual bool ScheduleUpdates(const ScheduleUpdatesParams& params = {
                                   .force_fw_update = false,
                               });

  // Checks for update and, if a newer version is available, attempts to update
  // the system.
  virtual void Update(const chromeos_update_manager::UpdateCheckParams& params);

  // Performs a scaled install of a DLC.
  virtual void Install();

  // ActionProcessorDelegate methods:
  void ProcessingDone(const ActionProcessor* processor,
                      ErrorCode code) override;
  void ProcessingStopped(const ActionProcessor* processor) override;
  void ActionCompleted(ActionProcessor* processor,
                       AbstractAction* action,
                       ErrorCode code) override;

  // PostinstallRunnerAction::DelegateInterface
  void ProgressUpdate(double progress) override;

  // Resets the active boot slot to be the current boot slot.
  bool ResetBootSlot();

  // Resets the current state to UPDATE_STATUS_IDLE.
  // Used by update_engine_client for restarting a new update without
  // having to reboot once the previous update has reached
  // UPDATE_STATUS_UPDATED_NEED_REBOOT state. This is used only
  // for testing purposes.
  virtual bool ResetStatus();

  // Resets the boot slot and update markers to invalidate a previously existing
  // update if one is available.
  bool InvalidateUpdate();

  // Returns the current status in the out param. Returns true on success.
  virtual bool GetStatus(update_engine::UpdateEngineStatus* out_status);

  // Sets the status to the given status and notifies a status update over dbus.
  void SetStatusAndNotify(UpdateStatus status);

  UpdateStatus status() const { return status_; }

  int http_response_code() const { return http_response_code_; }
  void set_http_response_code(int code) { http_response_code_ = code; }

  // Set flags that influence how updates and checks are performed.  These
  // influence all future checks and updates until changed or the device
  // reboots.
  void SetUpdateFlags(const update_engine::UpdateFlags& flags) {
    update_flags_ = flags;
  }

  // Returns the update attempt flags that are in place for the current update
  // attempt.  These are cached at the start of an update attempt so that they
  // remain constant throughout the process.
  virtual update_engine::UpdateFlags GetCurrentUpdateFlags() const {
    return current_update_flags_;
  }

  // This is the internal entry point for going through an
  // update. If the current status is idle invokes Update.
  // This is called by the DBus implementation.
  // This returns true if an update check was started, false if a check or an
  // update was already in progress.
  virtual bool CheckForUpdate(const update_engine::UpdateParams& update_params);

  // This is the internal entry point to apply a deferred update, will return
  // false if there wasn't a deferred update to apply or on failure. When
  // `shutdown` is set to true, shutdown instead of rebooting after applying the
  // update.
  virtual bool ApplyDeferredUpdate(bool shutdown);

  // This is the version of CheckForUpdate called by AttemptInstall API.
  virtual bool CheckForInstall(const std::vector<std::string>& dlc_ids,
                               const std::string& omaha_url,
                               bool scaled = false);

  // This is the internal entry point for going through a rollback. This will
  // attempt to run the postinstall on the non-active partition and set it as
  // the partition to boot from. If |powerwash| is True, perform a powerwash
  // as part of rollback. Returns True on success.
  bool Rollback(bool powerwash);

  // This is the internal entry point for checking if we can rollback.
  bool CanRollback() const;

  // This is the internal entry point for getting a rollback partition name,
  // if one exists. It returns the bootable rollback kernel device partition
  // name or empty string if none is available.
  BootControlInterface::Slot GetRollbackSlot() const;

  // Initiates a reboot if the current state is
  // UPDATED_NEED_REBOOT. Returns true on success, false otherwise.
  bool RebootIfNeeded();

  // Initiates a shutdown after applied the deferred update. Returns true on
  // success, false otherwise.
  bool ShutdownIfNeeded();

  // Sets the DLC as active or inactive. See chromeos/common_service.h
  virtual bool SetDlcActiveValue(bool is_active, const std::string& dlc_id);

  // Broadcasts the download/install progress.
  void ProgressUpdate(uint64_t bytes_received, uint64_t total);

  // DownloadActionDelegate methods:
  void BytesReceived(uint64_t bytes_progressed,
                     uint64_t bytes_received,
                     uint64_t total) override;

  // InstallActionDelegate methods:
  void BytesReceived(uint64_t bytes_received, uint64_t total) override;

  // Returns if an in-progress update should be cancelled.
  // Returns true if a download channel was changed or if updates are disabled
  // by the enterprise policy.
  bool ShouldCancel(ErrorCode* cancel_reason) override;

  // Resets the update status. If there is an a valid update complete marker,
  // resets to `UPDATED_NEED_REBOOT`. Otherwise goes back to `IDLE`.
  void ResetUpdateStatus();

  // Resets update prefs which are no longer up to date after the inactive
  // partition is marked unbootable. Update related prefs are re-written after
  // a successful update.
  bool ResetUpdatePrefs();

  void DownloadComplete() override;

  // Broadcasts the current status to all observers.
  void BroadcastStatus();

  ErrorCode GetAttemptErrorCode() const { return attempt_error_code_; }

  // Called at update_engine startup to do various house-keeping.
  void UpdateEngineStarted();

  // Returns the |Excluder| that is currently held onto.
  virtual ExcluderInterface* GetExcluder() const { return excluder_.get(); }

  // Reloads the device policy from libbrillo. Note: This method doesn't
  // cause a real-time policy fetch from the policy server. It just reloads the
  // latest value that libbrillo has cached. libbrillo fetches the policies
  // from the server asynchronously at its own frequency.
  virtual void RefreshDevicePolicy();

  // Stores in |out_boot_time| the boottime (CLOCK_BOOTTIME) recorded at the
  // time of the last successful update in the current boot. Returns false if
  // there wasn't a successful update in the current boot.
  virtual bool GetBootTimeAtUpdate(base::Time* out_boot_time);

  // Returns a version OS version that was being used before the last reboot,
  // and if that reboot happened to be into an update (current version).
  // This will return an empty string otherwise.
  const std::string& GetPrevVersion() const { return prev_version_; }

  // Returns the number of consecutive failed update checks.
  virtual unsigned int consecutive_failed_update_checks() const {
    return consecutive_failed_update_checks_;
  }

  // Returns the poll interval dictated by Omaha, if provided; zero otherwise.
  virtual unsigned int server_dictated_poll_interval() const {
    return server_dictated_poll_interval_;
  }

  // Sets a callback to be used when either a forced update request is received
  // (first argument set to true) or cleared by an update attempt (first
  // argument set to false). The callback further encodes whether the forced
  // check is an interactive one (second argument set to true). Takes ownership
  // of the callback object. A null value disables callback on these events.
  // Note that only one callback can be set, so effectively at most one client
  // can be notified.
  virtual void set_forced_update_pending_callback(
      base::RepeatingCallback<void(bool, bool)>* callback) {
    forced_update_pending_callback_.reset(callback);
  }

  // Returns true if we should allow updates from any source. In official builds
  // we want to restrict updates to known safe sources, but under certain
  // conditions it's useful to allow updating from anywhere (e.g. to allow
  // 'cros flash' to function properly).
  bool IsAnyUpdateSourceAllowed() const;

  // Returns whether repeated updates are enabled. Defaults to true if the pref
  // is unset or unable to be read.
  virtual bool IsRepeatedUpdatesEnabled();

  // Toggles a feature managed by update_engine and broadcasts the update_engine
  // status out to all observers.
  bool ToggleFeature(const std::string& feature, bool enable);

  // Returns the value of the feature managed by update_engine.
  bool IsFeatureEnabled(const std::string& feature, bool* out_enabled) const;

  // Triggers the rootfs scanning for integrity checking.
  virtual void RootfsIntegrityCheck() const;

  // |DaemonStateInterface| overrides.
  bool StartUpdater() override;
  void AddObserver(ServiceObserverInterface* observer) override {
    service_observers_.insert(observer);
  }
  void RemoveObserver(ServiceObserverInterface* observer) override {
    service_observers_.erase(observer);
  }
  const std::set<ServiceObserverInterface*>& service_observers() override {
    return service_observers_;
  }

  // Remove all the observers.
  void ClearObservers() { service_observers_.clear(); }

 private:
  // Friend declarations for testing purposes.
  friend class UpdateAttempterUnderTest;
  friend class UpdateAttempterTest;
  FRIEND_TEST(UpdateAttempterTest, ActionCompletedDownloadTest);
  FRIEND_TEST(UpdateAttempterTest, ActionCompletedErrorTest);
  FRIEND_TEST(UpdateAttempterTest, ActionCompletedOmahaRequestTest);
  FRIEND_TEST(UpdateAttempterTest, ActionCompletedSkipApplying);
  FRIEND_TEST(UpdateAttempterTest, ActionCompletedNewVersionSet);
  FRIEND_TEST(UpdateAttempterTest, BootTimeInUpdateMarkerFile);
  FRIEND_TEST(UpdateAttempterTest, BroadcastCompleteDownloadTest);
  FRIEND_TEST(UpdateAttempterTest, CalculateDlcParamsInstallTest);
  FRIEND_TEST(UpdateAttempterTest, CalculateDlcParamsNoPrefFilesTest);
  FRIEND_TEST(UpdateAttempterTest, CalculateDlcParamsNonParseableValuesTest);
  FRIEND_TEST(UpdateAttempterTest, CalculateDlcParamsValidValuesTest);
  FRIEND_TEST(UpdateAttempterTest, CalculateDlcParamsRemoveStaleMetadata);
  FRIEND_TEST(UpdateAttempterTest, ChangeToDownloadingOnReceivedBytesTest);
  FRIEND_TEST(UpdateAttempterTest, CheckForInstallNotIdleFails);
  FRIEND_TEST(UpdateAttempterTest, CheckForUpdateAUDlcTest);
  FRIEND_TEST(UpdateAttempterTest, CreatePendingErrorEventTest);
  FRIEND_TEST(UpdateAttempterTest, CreatePendingErrorEventResumedTest);
  FRIEND_TEST(UpdateAttempterTest, DisableDeltaUpdateIfNeededTest);
  FRIEND_TEST(UpdateAttempterTest, DownloadProgressAccumulationTest);
  FRIEND_TEST(UpdateAttempterTest, InstallSetsStatusIdle);
  FRIEND_TEST(UpdateAttempterTest, IsEnterpriseRollbackInGetStatusTrue);
  FRIEND_TEST(UpdateAttempterTest, IsEnterpriseRollbackInGetStatusFalse);
  FRIEND_TEST(UpdateAttempterTest,
              PowerwashInGetStatusTrueBecausePowerwashRequired);
  FRIEND_TEST(UpdateAttempterTest, PowerwashInGetStatusTrueBecauseRollback);
  FRIEND_TEST(UpdateAttempterTest, CriticalUpdateDefault);
  FRIEND_TEST(UpdateAttempterTest, CriticalUpdate);
  FRIEND_TEST(UpdateAttempterTest, MarkDeltaUpdateFailureTest);
  FRIEND_TEST(UpdateAttempterTest, PingOmahaTest);
  FRIEND_TEST(UpdateAttempterTest, ProcessingDoneNoUpdateReboot);
  FRIEND_TEST(UpdateAttempterTest, ProcessingDoneInstallError);
  FRIEND_TEST(UpdateAttempterTest, ProcessingDoneUpdateError);
  FRIEND_TEST(UpdateAttempterTest, ReportDailyMetrics);
  FRIEND_TEST(UpdateAttempterTest, RollbackNotAllowed);
  FRIEND_TEST(UpdateAttempterTest, RollbackAfterInstall);
  FRIEND_TEST(UpdateAttempterTest, RollbackAfterScaledInstall);
  FRIEND_TEST(UpdateAttempterTest, RollbackAllowed);
  FRIEND_TEST(UpdateAttempterTest, RollbackAllowedSetAndReset);
  FRIEND_TEST(UpdateAttempterTest, ChannelDowngradeNoRollback);
  FRIEND_TEST(UpdateAttempterTest, ChannelDowngradeRollback);
  FRIEND_TEST(UpdateAttempterTest, RollbackMetricsNotRollbackFailure);
  FRIEND_TEST(UpdateAttempterTest, RollbackMetricsNotRollbackSuccess);
  FRIEND_TEST(UpdateAttempterTest, RollbackMetricsRollbackFailure);
  FRIEND_TEST(UpdateAttempterTest, RollbackMetricsRollbackSuccess);
  FRIEND_TEST(UpdateAttempterTest, ScheduleErrorEventActionNoEventTest);
  FRIEND_TEST(UpdateAttempterTest, ScheduleErrorEventActionTest);
  FRIEND_TEST(UpdateAttempterTest, SessionIdTestEnforceEmptyStrPingOmaha);
  FRIEND_TEST(UpdateAttempterTest, SessionIdTestOnOmahaRequestActions);
  FRIEND_TEST(UpdateAttempterTest, SetRollbackHappenedNotRollback);
  FRIEND_TEST(UpdateAttempterTest, SetRollbackHappenedRollback);
  FRIEND_TEST(UpdateAttempterTest, TargetChannelHintSetAndReset);
  FRIEND_TEST(UpdateAttempterTest, TargetVersionPrefixSetAndReset);
  FRIEND_TEST(UpdateAttempterTest, UpdateAfterInstall);
  FRIEND_TEST(UpdateAttempterTest, UpdateAfterScaledInstall);
  FRIEND_TEST(UpdateAttempterTest, UpdateFlagsCachedAtUpdateStart);
  FRIEND_TEST(UpdateAttempterTest, UpdateDeferredByPolicyTest);
  FRIEND_TEST(UpdateAttempterTest, UpdateIsNotRunningWhenUpdateAvailable);
  FRIEND_TEST(UpdateAttempterTest, GetSuccessfulDlcIds);
  FRIEND_TEST(UpdateAttempterTest, QuickFixTokenWhenDeviceIsEnterpriseEnrolled);
  FRIEND_TEST(UpdateAttempterTest, MoveToPrefs);
  FRIEND_TEST(UpdateAttempterTest, FirstUpdateBeforeReboot);
  FRIEND_TEST(UpdateAttempterTest, InvalidateLastUpdate);
  FRIEND_TEST(UpdateAttempterTest, InvalidateLastPowerwashUpdate);
  FRIEND_TEST(UpdateAttempterTest, InvalidateLastUpdateNoPowerwashFile);
  FRIEND_TEST(UpdateAttempterTest, InvalidateLastUpdateExternalPowerwash);
  FRIEND_TEST(UpdateAttempterTest, ConsecutiveUpdateBeforeRebootSuccess);
  FRIEND_TEST(UpdateAttempterTest, ConsecutiveUpdateBeforeRebootLimited);
  FRIEND_TEST(UpdateAttempterTest, ConsecutiveUpdateFailureMetric);
  FRIEND_TEST(UpdateAttempterTest, ResetUpdatePrefs);
  FRIEND_TEST(UpdateAttempterTest, ProcessingDoneSkipApplying);
  FRIEND_TEST(UpdateAttempterTest, InstallZeroDlcTest);
  FRIEND_TEST(UpdateAttempterTest, InstallSingleDlcTest);
  FRIEND_TEST(UpdateAttempterTest, InstallMultiDlcTest);
  FRIEND_TEST(UpdateAttempterTest, AfterRestartUpdateInvalidationScheduled);
  FRIEND_TEST(UpdateAttempterTest,
              AfterRestartNoInvalidationScheduledIfNoUpdate);
  FRIEND_TEST(UpdateAttempterTest,
              AfterRestartNoInvalidationScheduledIfDeferredUpdate);
  FRIEND_TEST(UpdateAttempterTest, AfterRestartInvalidatesUpdate);
  FRIEND_TEST(UpdateAttempterTest, AfterRestartSubscribesInvalidatesUpdate);
  FRIEND_TEST(UpdateAttempterTest,
              AfterRestartSkipsUpdateInvalidationIfNonEnterprise);
  FRIEND_TEST(UpdateAttempterTest,
              AfterRestartSkipsUpdateInvalidationIfNotIdle);
  FRIEND_TEST(UpdateAttempterTest, AfterUpdateInvalidatesUpdate);
  FRIEND_TEST(UpdateAttempterTest, AfterUpdateInvalidatesUpdateMetrics);
  FRIEND_TEST(UpdateAttempterTest, AfterUpdateInvalidatesUpdateFailureMetrics);
  FRIEND_TEST(UpdateAttempterTest, AfterUpdateSubscribesInvalidatesUpdate);
  FRIEND_TEST(UpdateAttempterTest,
              AfterUpdateSkipsUpdateInvalidationIfNonEnterprise);
  FRIEND_TEST(UpdateAttempterTest,
              AfterUpdateSkipsInvalidationIfDeferredUpdates);
  FRIEND_TEST(UpdateAttempterTest, AfterUpdateSkipsUpdateInvalidationIfNonIdle);
  FRIEND_TEST(UpdateAttempterTest, AfterRepeatedUpdateInvalidatesUpdate);
  FRIEND_TEST(UpdateAttempterTest, AfterRepeatedInvalidatesUpdateOnError);
  // Returns the special flags to be added to ErrorCode values based on the
  // parameters used in the current update attempt.
  uint32_t GetErrorCodeFlags();

  // ActionProcessorDelegate methods |ProcessingDone()| internal helpers.
  void ProcessingDoneInternal(const ActionProcessor* processor, ErrorCode code);
  void ProcessingDoneUpdate(const ActionProcessor* processor, ErrorCode code);
  void ProcessingDoneInstall(const ActionProcessor* processor, ErrorCode code);

  // CertificateChecker::Observer method.
  // Report metrics about the certificate being checked.
  void CertificateChecked(ServerToCheck server_to_check,
                          CertificateCheckResult result) override;

  // Checks if it's more than 24 hours since daily metrics were last
  // reported and, if so, reports daily metrics. Returns |true| if
  // metrics were reported, |false| otherwise.
  bool CheckAndReportDailyMetrics();

  // Report the |ConsecutiveUpdateCount| metric after reboot.
  void ReportConsecutiveUpdateMetric();

  // Calculates and reports the age of the currently running OS. This
  // is defined as the age of the /etc/lsb-release file.
  void ReportOSAge();

  // Creates an error event object in |error_event_| to be included in an
  // OmahaRequestAction once the current action processor is done.
  void CreatePendingErrorEvent(AbstractAction* action, ErrorCode code);

  // If there's a pending error event allocated in |error_event_|, schedules an
  // OmahaRequestAction with that event in the current processor, clears the
  // pending event, updates the status and returns true. Returns false
  // otherwise.
  bool ScheduleErrorEventAction();

  // Schedules an event loop callback to start the action processor. This is
  // scheduled asynchronously to unblock the event loop.
  void ScheduleProcessingStart();

  // Checks if a full update is needed and forces it by updating the Omaha
  // request params.
  void DisableDeltaUpdateIfNeeded();

  // If this was a delta update attempt that failed, count it so that a full
  // update can be tried when needed.
  void MarkDeltaUpdateFailure();

  ProxyResolver* GetProxyResolver() {
    if (obeying_proxies_)
      return &chrome_proxy_resolver_;
    return &direct_proxy_resolver_;
  }

  // Sends a ping to Omaha.
  // This is used after an update has been applied and we're waiting for the
  // user to reboot.  This ping helps keep the number of actives count
  // accurate in case a user takes a long time to reboot the device after an
  // update has been applied.
  void PingOmaha();

  // Helper method of Update() to calculate the update-related parameters
  // from various sources and set the appropriate state. Please refer to
  // Update() method for the meaning of the parameters.
  bool CalculateUpdateParams(
      const chromeos_update_manager::UpdateCheckParams& params);

  // Calculates all the scattering related parameters (such as waiting period,
  // which type of scattering is enabled, etc.) and also updates/deletes
  // the corresponding prefs file used in scattering. Should be called
  // only after the device policy has been loaded and set in the system state.
  void CalculateScatteringParams(bool interactive);

  // Sets a random value for the waiting period to wait for before downloading
  // an update, if one available. This value will be upperbounded by the
  // scatter factor value specified from policy.
  void GenerateNewWaitingPeriod();

  // Helper method of Update() to construct the sequence of actions to
  // be performed for an update check. Please refer to
  // Update() method for the meaning of the parameters.
  void BuildUpdateActions(
      const chromeos_update_manager::UpdateCheckParams& params);

  // Decrements the count in the kUpdateCheckCountFilePath.
  // Returns True if successfully decremented, false otherwise.
  bool DecrementUpdateCheckCount();

  // Starts p2p and performs housekeeping. Returns true only if p2p is
  // running and housekeeping was done.
  bool StartP2PAndPerformHousekeeping();

  // Calculates whether peer-to-peer should be used. Sets the
  // |use_p2p_to_download_| and |use_p2p_to_share_| parameters
  // on the |omaha_request_params_| object.
  void CalculateP2PParams(bool interactive);

  // For each key, reads value from powerwash safe prefs and adds it to prefs
  // if key doesnt already exist. Then deletes the powerwash safe keys.
  void MoveToPrefs(const std::vector<std::string>& keys);

  // Starts P2P if it's enabled and there are files to actually share.
  // Called only at program startup. Returns true only if p2p was
  // started and housekeeping was performed.
  bool StartP2PAtStartup();

  // Writes to the processing completed marker. Does nothing if
  // |update_completed_marker_| is empty.
  void WriteUpdateCompletedMarker();

  // Reboots the system directly by calling /sbin/shutdown. Returns true on
  // success.
  bool RebootDirectly();

  // Shutdown the system directly by calling /sbin/shutdown. Returns true on
  // success.
  bool ShutdownDirectly();

  // Callback for the async update check allowed policy request. If |status| is
  // |EvalStatus::kSucceeded|, either runs or suppresses periodic update checks,
  // based on the content of |policy_data_|. Otherwise, retries the policy
  // request.
  void OnUpdateScheduled(chromeos_update_manager::EvalStatus status);

  // Updates the time an update was last attempted to the current time.
  void UpdateLastCheckedTime();

  // Checks whether we need to clear the rollback-happened preference after
  // policy is available again.
  void UpdateRollbackHappened();

  // Categorizes and returns the last update error.
  ErrorCode GetLastUpdateError();

  // Returns if an update is: running, applied and needs reboot, or scheduled.
  bool IsBusyOrUpdateScheduled();

  void CalculateStagingParams(bool interactive);

  // Resets interactivity and forced update flags.
  void ResetInteractivityFlags();

  // Resets all the DLC prefs.
  bool ResetDlcPrefs(const std::string& dlc_id);

  // Sets given pref key for DLC and platform.
  void SetPref(const std::string& pref_key,
               const std::string& pref_value,
               const std::string& payload_id);

  // Get the integer values from the DLC metadata for |kPrefsPingLastActive|
  // or |kPrefsPingLastRollcall|.
  // The value is equal to -2 when the value cannot be read or is not numeric.
  // The value is equal to -1 the first time it is being sent, which is
  // when the metadata file doesn't exist.
  int64_t GetPingMetadata(const std::string& metadata_key) const;

  // Calculates the update parameters for DLCs. Sets the |dlc_ids_|
  // parameter on the |omaha_request_params_| object.
  void CalculateDlcParams();

  // Returns the list of DLC IDs that were installed/updated, excluding the ones
  // which had "noupdate" in the Omaha response.
  std::vector<std::string> GetSuccessfulDlcIds();

  void OnRootfsIntegrityCheck(int ret_code, const std::string& output) const;

  // Schedules a policy request to check and subscribe to the enterprise signals
  // that indicate if we should invalidate a pending update.
  // Currently the only source of the signal is the
  // `EnterpriseUpdateDisabledPolicyImpl` policy.
  // Returns false if already scheduled or failed to schedule.
  bool ScheduleEnterpriseUpdateInvalidationCheck();

  // Used as a policy request callback for
  // `ScheduleEnterpriseUpdateInvalidationCheck`.
  // Invalidates pending updates on the enterprise invalidation signal and if
  // the update attempter is in the UpdateStatus::UPDATED_NEED_REBOOT state.
  // `status` values are expected to be semantically similar to
  // `EnterpriseUpdateDisabledPolicyImpl` policy.
  void OnEnterpriseUpdateInvalidationCheck(
      chromeos_update_manager::EvalStatus status);

  // Last status notification timestamp used for throttling. Use monotonic
  // TimeTicks to ensure that notifications are sent even if the system clock is
  // set back in the middle of an update.
  base::TimeTicks last_notify_time_;

  // Our two proxy resolvers
  DirectProxyResolver direct_proxy_resolver_;
  ChromeBrowserProxyResolver chrome_proxy_resolver_;

  std::unique_ptr<ActionProcessor> processor_;

  ActionProcessor aux_processor_;

  // Pointer to the certificate checker instance to use.
  CertificateChecker* cert_checker_;

  // The list of services observing changes in the updater.
  std::set<ServiceObserverInterface*> service_observers_;

  // The install plan.
  std::unique_ptr<InstallPlan> install_plan_;

  // Pointer to the preferences store interface. This is just a cached
  // copy of SystemState::Get()->prefs() because it's used in many methods and
  // is convenient this way.
  PrefsInterface* prefs_ = nullptr;

  // Pending error event, if any.
  std::unique_ptr<OmahaEvent> error_event_;

  // If we should request a reboot even tho we failed the update
  bool fake_update_success_ = false;

  // HTTP server response code from the last HTTP request action.
  int http_response_code_ = 0;

  // The attempt error code when the update attempt finished.
  ErrorCode attempt_error_code_ = ErrorCode::kSuccess;

  // CPU limiter during the update.
  CPULimiter cpu_limiter_;

  // For status:
  UpdateStatus status_{UpdateStatus::IDLE};
  double download_progress_ = 0.0;
  int64_t last_checked_time_ = 0;
  std::string prev_version_;
  std::string new_version_ = "0.0.0.0";
  uint64_t new_payload_size_ = 0;
  // Flags influencing all periodic update checks
  update_engine::UpdateFlags update_flags_;
  // Flags influencing the currently in-progress check (cached at the start of
  // the update check).
  update_engine::UpdateFlags current_update_flags_;

  // Common parameters for all Omaha requests.
  OmahaRequestParams* omaha_request_params_ = nullptr;

  // Number of consecutive manual update checks we've had where we obeyed
  // Chrome's proxy settings.
  int proxy_manual_checks_ = 0;

  // If true, this update cycle we are obeying proxies
  bool obeying_proxies_ = true;

  // Used for fetching information about the device policy.
  std::unique_ptr<policy::PolicyProvider> policy_provider_;

  // The current scatter factor as found in the policy setting.
  base::TimeDelta scatter_factor_;

  // The number of consecutive failed update checks. Needed for calculating the
  // next update check interval.
  unsigned int consecutive_failed_update_checks_ = 0;

  // The poll interval (in seconds) that was dictated by Omaha, if any; zero
  // otherwise. This is needed for calculating the update check interval.
  unsigned int server_dictated_poll_interval_ = 0;

  // Tracks whether we have scheduled update checks.
  bool waiting_for_scheduled_check_ = false;

  // Tracks if the enterprise update invalidation check is already scheduled by
  // `ScheduleEnterpriseUpdateInvalidationCheck`.
  // Needed so that we only have at most one scheduled check.
  bool enterprise_update_invalidation_check_scheduled_ = 0;

  // A callback to use when a forced update request is either received (true) or
  // cleared by an update attempt (false). The second argument indicates whether
  // this is an interactive update, and its value is significant iff the first
  // argument is true.
  std::unique_ptr<base::RepeatingCallback<void(bool, bool)>>
      forced_update_pending_callback_;

  // The |app_version| and |omaha_url| parameters received during the latest
  // forced update request. They are retrieved for use once the update is
  // actually scheduled.
  std::string forced_app_version_;
  std::string forced_omaha_url_;

  // A list of DLC module IDs.
  std::vector<std::string> dlc_ids_;

  // What type of operation is happening/scheduled.
  ProcessMode pm_{ProcessMode::UPDATE};

  // If this is not TimeDelta(), then that means staging is turned on.
  base::TimeDelta staging_wait_time_;
  chromeos_update_manager::StagingSchedule staging_schedule_;

  // This is the session ID used to track update flow to Omaha.
  std::string session_id_;

  // Interface for excluder.
  std::unique_ptr<ExcluderInterface> excluder_;

  // Data used by the update check allowed policies.
  std::shared_ptr<chromeos_update_manager::UpdateCheckAllowedPolicyData>
      policy_data_;

  // If the application of update should be skipped.
  bool skip_applying_ = false;

  base::WeakPtrFactory<UpdateAttempter> weak_ptr_factory_;
};

// Turns a generic ErrorCode::kError to a generic error code specific
// to |action| (e.g., ErrorCode::kFilesystemVerifierError). If |code| is
// not ErrorCode::kError, or the action is not matched, returns |code|
// unchanged.

ErrorCode GetErrorCodeForAction(AbstractAction* action, ErrorCode code);

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_CROS_UPDATE_ATTEMPTER_H_
