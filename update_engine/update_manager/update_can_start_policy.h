//
// Copyright (C) 2014 The Android Open Source Project
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

#ifndef UPDATE_ENGINE_UPDATE_MANAGER_POLICY_H_
#define UPDATE_ENGINE_UPDATE_MANAGER_POLICY_H_

#include <string>
#include <tuple>
#include <vector>

#include "update_engine/common/error_code.h"
#include "update_engine/update_manager/policy_interface.h"

namespace chromeos_update_manager {

// Input arguments to UpdateCanStart.
//
// A snapshot of the state of the current update process. This includes
// everything that a policy might need and that occurred since the first time
// the current payload was first seen and attempted (consecutively).
struct UpdateState {
  // Information pertaining to the current update payload and/or check.
  //
  // Whether the current update check is an interactive one. The caller should
  // feed the value returned by the preceding call to UpdateCheckAllowed().
  bool interactive;
  // Whether it is a delta payload.
  bool is_delta_payload;
  // Wallclock time when payload was first (consecutively) offered by Omaha.
  base::Time first_seen;
  // Number of consecutive update checks returning the current update.
  int num_checks;
  // Number of update payload failures and the wallclock time when it was last
  // updated by the updater. These should both be nullified whenever a new
  // update is seen; they are updated at the policy's descretion (via
  // UpdateDownloadParams.do_increment_failures) once all of the usable download
  // URLs for the payload have been used without success. They should be
  // persisted across reboots.
  int num_failures;
  base::Time failures_last_updated;

  // Information pertaining to downloading and applying of the current update.
  //
  // An array of download URLs provided by Omaha.
  std::vector<std::string> download_urls;
  // Max number of errors allowed per download URL.
  int download_errors_max;
  // The index of the URL to download from, as determined in the previous call
  // to the policy. For a newly seen payload, this should be -1.
  int last_download_url_idx;
  // The number of successive download errors pertaining to this last URL, as
  // determined in the previous call to the policy. For a newly seen payload,
  // this should be zero.
  int last_download_url_num_errors;
  // An array of errors that occurred while trying to download this update since
  // the previous call to this policy has returned, or since this payload was
  // first seen, or since the updater process has started (whichever is later).
  // Includes the URL index attempted, the error code, and the wallclock-based
  // timestamp when it occurred.
  std::vector<std::tuple<int, chromeos_update_engine::ErrorCode, base::Time>>
      download_errors;
  // Whether Omaha forbids use of P2P for downloading and/or sharing.
  bool p2p_downloading_disabled;
  bool p2p_sharing_disabled;
  // The number of P2P download attempts and wallclock-based time when P2P
  // download was first attempted.
  int p2p_num_attempts;
  base::Time p2p_first_attempted;

  // Information pertaining to update backoff mechanism.
  //
  // The currently known (persisted) wallclock-based backoff expiration time;
  // zero if none.
  base::Time backoff_expiry;
  // Whether backoff is disabled by Omaha.
  bool is_backoff_disabled;

  // Information pertaining to update scattering.
  //
  // The currently known (persisted) scattering wallclock-based wait period and
  // update check threshold; zero if none.
  base::TimeDelta scatter_wait_period;
  int scatter_check_threshold;
  // Maximum wait period allowed for this update, as determined by Omaha.
  base::TimeDelta scatter_wait_period_max;
  // Minimum/maximum check threshold values.
  // TODO(garnold) These appear to not be related to the current update and so
  // should probably be obtained as variables via UpdaterProvider.
  int scatter_check_threshold_min;
  int scatter_check_threshold_max;
};

// Results regarding the downloading and applying of an update, as determined by
// UpdateCanStart.
//
// An enumerator for the reasons of not allowing an update to start.
enum class UpdateCannotStartReason {
  kUndefined,
  kCheckDue,
  kScattering,
  kBackoff,
  kCannotDownload,
};

struct UpdateDownloadParams {
  // Whether the update attempt is allowed to proceed.
  bool update_can_start;
  // If update cannot proceed, a reason code for why it cannot do so.
  UpdateCannotStartReason cannot_start_reason;

  // Download related attributes. The update engine uses them to choose the
  // means for downloading and applying an update.
  //
  // The index of the download URL to use (-1 means no suitable URL was found)
  // and whether it can be used. Even if there's no URL or its use is not
  // allowed (backoff, scattering) there may still be other means for download
  // (like P2P).  The URL index needs to be persisted and handed back to the
  // policy on the next time it is called.
  int download_url_idx;
  bool download_url_allowed;
  // The number of download errors associated with this download URL. This value
  // needs to be persisted and handed back to the policy on the next time it is
  // called.
  int download_url_num_errors;
  // Whether P2P download and sharing are allowed.
  bool p2p_downloading_allowed;
  bool p2p_sharing_allowed;

  // Other values that need to be persisted and handed to the policy as need on
  // the next call.
  //
  // Whether an update failure has been identified by the policy. The client
  // should increment and persist its update failure count, and record the time
  // when this was done; it needs to hand these values back to the policy
  // (UpdateState.{num_failures,failures_last_updated}) on the next time it is
  // called.
  bool do_increment_failures;
  // The current backof expiry.
  base::Time backoff_expiry;
  // The scattering wait period and check threshold.
  base::TimeDelta scatter_wait_period;
  int scatter_check_threshold;
};

class UpdateCanStartPolicyData : public PolicyDataInterface {
 public:
  UpdateState update_state;

  UpdateDownloadParams result;
};

// The Policy class is an interface to the ensemble of policy requests that the
// client can make. A derived class includes the policy implementations of
// these.
//
// When compile-time selection of the policy is required due to missing or extra
// parts in a given platform, a different Policy subclass can be used.
class UpdateCanStartPolicy : public PolicyInterface {
 public:
  UpdateCanStartPolicy() = default;
  virtual ~UpdateCanStartPolicy() = default;

  UpdateCanStartPolicy(const UpdateCanStartPolicy&) = delete;
  UpdateCanStartPolicy& operator=(const UpdateCanStartPolicy&) = delete;

  // Returns EvalStatus::kSucceeded if either an update can start being
  // processed, or the attempt needs to be aborted. In cases where the update
  // needs to wait for some condition to be satisfied, but none of the values
  // that need to be persisted has changed, returns
  // EvalStatus::kAskMeAgainLater. Arguments include an |update_state| that
  // encapsulates data pertaining to the current ongoing update process.
  EvalStatus Evaluate(EvaluationContext* ec,
                      State* state,
                      std::string* error,
                      PolicyDataInterface* data) const override;

  EvalStatus EvaluateDefault(EvaluationContext* ec,
                             State* state,
                             std::string* error,
                             PolicyDataInterface* data) const override;

 protected:
  std::string PolicyName() const override { return "UpdateCanStartPolicy"; }
};

// Output information from UpdateBackoffAndDownloadUrl.
struct UpdateBackoffAndDownloadUrlResult {
  // Whether the failed attempt count (maintained by the caller) needs to be
  // incremented.
  bool do_increment_failures;
  // The current backoff expiry. Null if backoff is not in effect.
  base::Time backoff_expiry;
  // The new URL index to use and number of download errors associated with it.
  // Significant iff |do_increment_failures| is false and |backoff_expiry| is
  // null. Negative value means no usable URL was found.
  int url_idx;
  int url_num_errors;
};

// Parameters for update scattering, as returned by UpdateScattering.
struct UpdateScatteringResult {
  bool is_scattering;
  base::TimeDelta wait_period;
  int check_threshold;
};

// A private policy for determining backoff and the download URL to use.
// Within |update_state|, |backoff_expiry| and |is_backoff_disabled| are used
// for determining whether backoff is still in effect; if not,
// |download_errors| is scanned past |failures_last_updated|, and a new
// download URL from |download_urls| is found and written to |result->url_idx|
// (-1 means no usable URL exists); |download_errors_max| determines the
// maximum number of attempts per URL, according to the Omaha response. If an
// update failure is identified then |result->do_increment_failures| is set to
// true; if backoff is enabled, a new backoff period is computed (from the
// time of failure) based on |num_failures|. Otherwise, backoff expiry is
// nullified, indicating that no backoff is in effect.
//
// If backing off but the previous backoff expiry is unchanged, returns
// |EvalStatus::kAskMeAgainLater|. Otherwise:
//
// * If backing off with a new expiry time, then |result->backoff_expiry| is
//   set to this time.
//
// * Else, |result->backoff_expiry| is set to null, indicating that no backoff
//   is in effect.
//
// In any of these cases, returns |EvalStatus::kSucceeded|. If an error
// occurred, returns |EvalStatus::kFailed|.
EvalStatus UpdateBackoffAndDownloadUrl(
    EvaluationContext* ec,
    State* state,
    std::string* error,
    UpdateBackoffAndDownloadUrlResult* result,
    const UpdateState& update_state);

// A private policy for checking whether scattering is due. Writes in |result|
// the decision as to whether or not to scatter; a wallclock-based scatter
// wait period, which ranges from zero (do not wait) and no greater than the
// current scatter factor provided by the device policy (if available) or the
// maximum wait period determined by Omaha; and an update check-based
// threshold between zero (no threshold) and the maximum number determined by
// the update engine. Within |update_state|, |scatter_wait_period| should
// contain the last scattering period returned by this function, or zero if no
// wait period is known; |scatter_check_threshold| is the last update check
// threshold, or zero if no such threshold is known. If not scattering, or if
// any of the scattering values has changed, returns |EvalStatus::kSucceeded|;
// otherwise, |EvalStatus::kAskMeAgainLater|.
EvalStatus UpdateScattering(EvaluationContext* ec,
                            State* state,
                            std::string* error,
                            UpdateScatteringResult* result,
                            const UpdateState& update_state);

}  // namespace chromeos_update_manager

#endif  // UPDATE_ENGINE_UPDATE_MANAGER_POLICY_H_
