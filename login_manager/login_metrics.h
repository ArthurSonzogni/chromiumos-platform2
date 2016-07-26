// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LOGIN_MANAGER_LOGIN_METRICS_H_
#define LOGIN_MANAGER_LOGIN_METRICS_H_

#include <base/files/file_path.h>
#include <base/macros.h>
#include <metrics/metrics_library.h>

namespace login_manager {

class CumulativeUseTimeMetric;

class LoginMetrics {
 public:
  enum AllowedUsersState {
    ANY_USER_ALLOWED = 0,
    ONLY_WHITELISTED_ALLOWED = 1
  };
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

  // Holds the state of several policy-related files on disk.
  // We leave an extra bit for future state-space expansion.
  // Treat as, essentially, a base-4 number that we encode in decimal before
  // sending to chrome as a metric.
  // Digits are in this order:
  // Key file state - policy file state - old prefs file state.
  //
  // Some codes of interest:
  // CODE | Key | Policy | Prefs
  // -----+-----+--------+-------
  //  0   |  G  |   G    |  G     (Healthy, long-running users)
  //  2   |  G  |   G    |  N     (Healthy, newer users)
  //  8   |  G  |   N    |  G     (http://crosbug.com/24361)
  //  42  |  N  |   N    |  N     (As-yet unowned devices)
  //
  // Also, codes in the 9-17 range indicate a horked owner key with other files
  // in various states.  3-5, 12-14, and 21-23 indicate broken policy files.
  struct PolicyFilesStatus {
   public:
    PolicyFilesStatus()
        : owner_key_file_state(NOT_PRESENT),
          policy_file_state(NOT_PRESENT),
          defunct_prefs_file_state(NOT_PRESENT) {
    }
    virtual ~PolicyFilesStatus() {}

    PolicyFileState owner_key_file_state;
    PolicyFileState policy_file_state;
    PolicyFileState defunct_prefs_file_state;
  };

  explicit LoginMetrics(const base::FilePath& per_boot_flag_dir);
  virtual ~LoginMetrics();

  // Sends metric reporting whether the Owner of this non-enrolled device has
  // chosen to allow arbitrary users to sign in or not.
  virtual void SendConsumerAllowsNewUsers(bool allowed);

  // Sends the type of user that logs in (guest, owner or other) and the mode
  // (developer or normal) to UMA by using the metrics library.
  virtual void SendLoginUserType(bool dev_mode, bool guest, bool owner);

  // Sends info about the state of the Owner key, device policy, and legacy
  // prefs file to UMA using the metrics library.
  // Returns true if stats are sent.
  virtual bool SendPolicyFilesStatus(const PolicyFilesStatus& status);

  // Writes a histogram indicating the state key generation method used.
  virtual void SendStateKeyGenerationStatus(
      StateKeyGenerationStatus status);

  // Record a stat called |tag| via the bootstat library.
  virtual void RecordStats(const char* tag);

  // Return true if we have already recorded that Chrome has exec'd.
  virtual bool HasRecordedChromeExec();

  // Starts tracking cumulative ARC usage time. Should be called when ARC
  // container is started.
  virtual void StartTrackingArcUseTime();

  // Stops tracking culumative ARC usage time. Should be called when ARC
  // container is stopped.
  virtual void StopTrackingArcUseTime();

 private:
  friend class LoginMetricsTest;
  friend class UserTypeTest;

  // Returns code to send to the metrics library based on the state of
  // several policy-related files on disk.
  // As each file has three possible states, treat as a base-3 number and
  // convert to decimal.
  static int PolicyFilesStatusCode(const PolicyFilesStatus& status);

  // Returns code to send to the metrics library based on the type of user
  // (owner, guest or other) and the mode (normal or developer).
  static int LoginUserTypeCode(bool dev_mode, bool guest, bool owner);

  const base::FilePath per_boot_flag_file_;
  MetricsLibrary metrics_lib_;
  std::unique_ptr<CumulativeUseTimeMetric> arc_cumulative_use_time_;

  DISALLOW_COPY_AND_ASSIGN(LoginMetrics);
};
}  // namespace login_manager

#endif  // LOGIN_MANAGER_LOGIN_METRICS_H_
