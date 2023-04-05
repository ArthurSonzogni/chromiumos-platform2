// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LOGIN_MANAGER_LOGIN_METRICS_H_
#define LOGIN_MANAGER_LOGIN_METRICS_H_

#include <memory>
#include <string>

#include <base/files/file_path.h>
#include <base/time/time.h>
#include <metrics/metrics_library.h>

namespace login_manager {

class CumulativeUseTimeMetric;

class LoginMetrics {
 public:
  // Do we believe the session exited due to a login crash loop?
  // These values are persisted to metrics server. Entries (other than
  // NUM_VALUES) should not be renumbered and numeric values should never be
  // reused. If you update this enum, also update Chrome's enums.xml.
  enum class SessionExitType {
    NORMAL_EXIT = 0,
    LOGIN_CRASH_LOOP = 1,

    NUM_VALUES  // Keep last
  };
  enum AllowedUsersState { ANY_USER_ALLOWED = 0, ONLY_ALLOWLISTED = 1 };
  enum PolicyFileState {
    GOOD = 0,
    MALFORMED = 1,
    NOT_PRESENT = 2,
    NUM_STATES = 3
  };
  enum UserType {
    GUEST = 0,
    OWNER = 1,
    OTHER = 2,
    DEV_GUEST = 3,
    DEV_OWNER = 4,
    DEV_OTHER = 5,
    NUM_TYPES = 6
  };
  enum StateKeyGenerationStatus {
    STATE_KEY_STATUS_GENERATION_METHOD_IDENTIFIER_HASH = 0,
    STATE_KEY_STATUS_GENERATION_METHOD_HMAC_DEVICE_SECRET = 1,
    STATE_KEY_STATUS_MISSING_IDENTIFIERS = 2,
    STATE_KEY_STATUS_BAD_DEVICE_SECRET = 3,
    STATE_KEY_STATUS_HMAC_INIT_FAILURE = 4,
    STATE_KEY_STATUS_HMAC_SIGN_FAILURE = 5,
    STATE_KEY_STATUS_COUNT  // must be last.
  };
  enum InvalidDevicePolicyFilesStatus {
    ALL_VALID = 0,
    SOME_INVALID = 1,
    ALL_INVALID = 2,
    NUM_VALUES = 3
  };
  enum SwitchToFeatureFlagMappingStatus {
    SWITCHES_ABSENT = 0,
    SWITCHES_VALID = 1,
    SWITCHES_INVALID = 2,
    NUM_SWITCHES_STATUSES = 3,
  };
  // Current state of the browser process at the moment we decide to abort it.
  // Includes the standard Linux process states. Also includes an error bucket
  // so we can see if LivenessCheckerImpl::GetBrowserState() is failing. Used by
  // the "ChromeOS.Liveness.BrowserStateAtTimeout" UMA.
  // These values are persisted to logs. Entries should not be renumbered and
  // numeric values should never be reused. Update Chrome's enums.xml if new
  // values are added.
  enum class BrowserState {
    kRunning = 0,                   // State: R
    kSleeping = 1,                  // State: S
    kUninterruptibleWait = 2,       // State: D
    kZombie = 3,                    // State: Z
    kTracedOrStopped = 4,           // State: T
    kUnknown = 5,                   // Got a State character from status file
                                    // but it wasn't R, S, D, Z, or T
    kErrorGettingState = 6,         // Failed to read status file from /proc.
    kMaxValue = kErrorGettingState  // Must be equal to the largest value
  };
  enum class ArcContinueBootImpulseStatus {
    // These values are persisted to logs. Entries should not be renumbered and
    // numeric values should never be reused.
    kArcContinueBootImpulseStatusSuccess = 0,
    kArcContinueBootImpulseStatusFailed = 1,
    kArcContinueBootImpulseStatusTimedOut = 2,
    kMaxValue = kArcContinueBootImpulseStatusTimedOut
  };

  explicit LoginMetrics(const base::FilePath& per_boot_flag_dir);
  LoginMetrics(const LoginMetrics&) = delete;
  LoginMetrics& operator=(const LoginMetrics&) = delete;

  virtual ~LoginMetrics();

  // Sends metric reporting whether the mount namespace creation succeeded or
  // failed.
  virtual void SendNamespaceCreationResult(bool status);

  // Sends the type of user that logs in (guest, owner or other) and the mode
  // (developer or normal) to UMA by using the metrics library.
  virtual void SendLoginUserType(bool dev_mode, bool guest, bool owner);

  // Writes a histogram indicating the state key generation method used.
  virtual void SendStateKeyGenerationStatus(StateKeyGenerationStatus status);

  // Record a stat called |tag| via the bootstat library.
  virtual void RecordStats(const char* tag);

  // Return true if we have already recorded that Chrome has exec'd.
  virtual bool HasRecordedChromeExec();

  // Starts tracking cumulative ARC usage time. Should be called when ARC
  // container is started.
  virtual void StartTrackingArcUseTime();

  // Stops tracking cumulative ARC usage time. Should be called when ARC
  // container is stopped.
  virtual void StopTrackingArcUseTime();

  // Submits to UMA the result of invalid policy checks.
  virtual void SendInvalidPolicyFilesStatus(
      InvalidDevicePolicyFilesStatus result);

  // Submits to UMA whether or not the session exited due to a login crash loop.
  virtual void SendSessionExitType(SessionExitType session_exit_type);

  // Submits to UMA the browser shutdown time of normal exit.
  virtual void SendBrowserShutdownTime(base::TimeDelta browser_shutdown_time);

  // Submits to UMA the time to backup ARC bug report.
  virtual void SendArcBugReportBackupTime(
      base::TimeDelta arc_bug_report_backup_time);

  // Submits to UMA the status of the Arc Continue Boot time.
  virtual void SendArcContinueBootImpulseStatus(
      ArcContinueBootImpulseStatus status);

  // Submits to UMA the time to execute continue-arc-boot impulse.
  virtual void SendArcContinueBootImpulseTime(
      base::TimeDelta arc_continue_boot_impulse_time);

  // Submits a UMA sample indicating compatibility feature flag mapping status.
  virtual void SendSwitchToFeatureFlagMappingStatus(
      SwitchToFeatureFlagMappingStatus status);

  // Submits to UMA the time it took for a response to be received after a
  // liveness ping was sent.
  virtual void SendLivenessPingResponseTime(base::TimeDelta response_time);

  // Submits to UMA the liveness ping result.
  virtual void SendLivenessPingResult(bool success);

  // CrOS events are translated to an enum and reported to the generic
  // "Platform.CrOSEvent" enum histogram. The |event| string must be registered
  // in metrics/metrics_library.cc:kCrosEventNames.
  virtual void ReportCrosEvent(const std::string& event);

 private:
  friend class LoginMetricsTest;
  friend class UserTypeTest;

  // Returns code to send to the metrics library based on the type of user
  // (owner, guest or other) and the mode (normal or developer).
  static int LoginUserTypeCode(bool dev_mode, bool guest, bool owner);

  const base::FilePath per_boot_flag_file_;
  MetricsLibrary metrics_lib_;
  std::unique_ptr<CumulativeUseTimeMetric> arc_cumulative_use_time_;
};
}  // namespace login_manager

#endif  // LOGIN_MANAGER_LOGIN_METRICS_H_
