// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/update_manager/real_device_policy_provider.h"

#include <stdint.h>

#include <vector>

#include <base/location.h>
#include <base/logging.h>
#include <base/time/time.h>
#include <base/types/expected.h>
#include <oobe_config/metrics/enterprise_rollback_metrics_tracking.h>
#include <policy/device_policy.h>

#include "update_engine/common/connection_utils.h"
#include "update_engine/common/utils.h"
#include "update_engine/update_manager/generic_variables.h"

using base::TimeDelta;
using brillo::MessageLoop;
using chromeos_update_engine::ConnectionType;
using policy::DevicePolicy;
using std::set;
using std::string;
using std::vector;

namespace {

const TimeDelta kDevicePolicyRefreshRateTime = base::Minutes(60);

constexpr char kMarketSegmentUnknown[] = "unknown";
constexpr char kMarketSegmentEducation[] = "education";
constexpr char kMarketSegmentEnterprise[] = "enterprise";

}  // namespace

namespace chromeos_update_manager {

RealDevicePolicyProvider::~RealDevicePolicyProvider() {
  MessageLoop::current()->CancelTask(scheduled_refresh_);
}

bool RealDevicePolicyProvider::Init() {
  CHECK(policy_provider_ != nullptr);

  // On Init() we try to get the device policy and keep updating it.
  RefreshDevicePolicyAndReschedule();

  // We also listen for signals from the session manager to force a device
  // policy refresh.
  session_manager_proxy_->RegisterPropertyChangeCompleteSignalHandler(
      base::BindRepeating(
          &RealDevicePolicyProvider::OnPropertyChangedCompletedSignal,
          base::Unretained(this)),
      base::BindOnce(&RealDevicePolicyProvider::OnSignalConnected,
                     base::Unretained(this)));
  return true;
}

void RealDevicePolicyProvider::OnPropertyChangedCompletedSignal(
    const string& success) {
  if (success != "success") {
    LOG(WARNING) << "Received device policy updated signal with a failure.";
  }
  // We refresh the policy file even if the payload string is kSignalFailure.
  LOG(INFO) << "Reloading and re-scheduling device policy due to signal "
               "received.";
  MessageLoop::current()->CancelTask(scheduled_refresh_);
  scheduled_refresh_ = MessageLoop::kTaskIdNull;
  RefreshDevicePolicyAndReschedule();
}

void RealDevicePolicyProvider::OnSignalConnected(const string& interface_name,
                                                 const string& signal_name,
                                                 bool successful) {
  if (!successful) {
    LOG(WARNING) << "We couldn't connect to SessionManager signal for updates "
                    "on the device policy blob. We will reload the policy file "
                    "periodically.";
  }
  // We do a one-time refresh of the DevicePolicy just in case we missed a
  // signal between the first refresh and the time the signal handler was
  // actually connected.
  RefreshDevicePolicy();
}

void RealDevicePolicyProvider::RefreshDevicePolicyAndReschedule() {
  RefreshDevicePolicy();
  scheduled_refresh_ = MessageLoop::current()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(
          &RealDevicePolicyProvider::RefreshDevicePolicyAndReschedule,
          base::Unretained(this)),
      kDevicePolicyRefreshRateTime);
}

template <typename T>
void RealDevicePolicyProvider::UpdateVariable(
    AsyncCopyVariable<T>* var,
    // NOLINTNEXTLINE(readability/casting)
    bool (DevicePolicy::*getter)(T*) const) {
  T new_value;
  if (policy_provider_->device_policy_is_loaded() &&
      (policy_provider_->GetDevicePolicy().*getter)(&new_value)) {
    var->SetValue(new_value);
  } else {
    var->UnsetValue();
  }
}

template <typename T>
void RealDevicePolicyProvider::UpdateVariable(
    AsyncCopyVariable<T>* var,
    bool (RealDevicePolicyProvider::*getter)(T*) const) {
  T new_value;
  if (policy_provider_->device_policy_is_loaded() &&
      (this->*getter)(&new_value)) {
    var->SetValue(new_value);
  } else {
    var->UnsetValue();
  }
}

bool RealDevicePolicyProvider::ConvertRollbackToTargetVersion(
    RollbackToTargetVersion* rollback_to_target_version) const {
  int rollback_to_target_version_int;
  if (!policy_provider_->GetDevicePolicy().GetRollbackToTargetVersion(
          &rollback_to_target_version_int)) {
    if (policy_provider_->IsEnterpriseEnrolledDevice() &&
        policy_provider_->device_policy_is_loaded()) {
      // Device is managed but Rollback policy is not set, clean tracking for
      // any old Enterprise Rollback.
      if (!oobe_config::CleanOutdatedTracking(*rollback_metrics_)) {
        LOG(ERROR) << "Error cleaning up old Enterprise Rollback tracking.";
      }
    }
    return false;
  }
  if (rollback_to_target_version_int < 0 ||
      rollback_to_target_version_int >=
          static_cast<int>(RollbackToTargetVersion::kMaxValue)) {
    if (!oobe_config::CleanOutdatedTracking(*rollback_metrics_)) {
      LOG(ERROR) << "Error cleaning up old Enterprise Rollback tracking when "
                    "wrong policy value is provided.";
    }
    return false;
  }

  *rollback_to_target_version =
      static_cast<RollbackToTargetVersion>(rollback_to_target_version_int);

  // Track Enterprise Rollback Metrics if the policy is enabled and we are
  // preserving rollback data during powerwash. Clean old tracking otherwise.
  switch (*rollback_to_target_version) {
    case chromeos_update_manager::RollbackToTargetVersion::
        kRollbackAndRestoreIfPossible: {
      std::string target_version;
      if (!policy_provider_->GetDevicePolicy().GetTargetVersionPrefix(
              &target_version)) {
        LOG(ERROR) << "Failed to read target version policy.";
        break;
      }

      auto is_tracking_current_rollback =
          oobe_config::IsTrackingForRollbackTargetVersion(*rollback_metrics_,
                                                          target_version);
      if (!is_tracking_current_rollback.has_value()) {
        LOG(ERROR) << is_tracking_current_rollback.error();
        if (!oobe_config::CleanOutdatedTracking(*rollback_metrics_)) {
          LOG(ERROR) << "Error cleaning up old Enterprise Rollback tracking "
                        "when error obtaining current tracking.";
        }
      } else if (!is_tracking_current_rollback.value()) {
        if (!oobe_config::StartNewTracking(*rollback_metrics_,
                                           target_version)) {
          LOG(WARNING) << "Error starting new Enterprise Rollback tracking.";
        }
      }
      break;
    }
    default:
      if (!oobe_config::CleanOutdatedTracking(*rollback_metrics_)) {
        LOG(ERROR) << "Error cleaning up old Enterprise Rollback metrics.";
      }
      break;
  }

  return true;
}

bool RealDevicePolicyProvider::ConvertAllowedConnectionTypesForUpdate(
    set<ConnectionType>* allowed_types) const {
  set<string> allowed_types_str;
  if (!policy_provider_->GetDevicePolicy().GetAllowedConnectionTypesForUpdate(
          &allowed_types_str)) {
    return false;
  }
  allowed_types->clear();
  for (auto& type_str : allowed_types_str) {
    ConnectionType type =
        chromeos_update_engine::connection_utils::ParseConnectionType(type_str);
    if (type != ConnectionType::kUnknown) {
      allowed_types->insert(type);
    } else {
      LOG(WARNING) << "Policy includes unknown connection type: " << type_str;
    }
  }
  return true;
}

bool RealDevicePolicyProvider::ConvertScatterFactor(
    TimeDelta* scatter_factor) const {
  int64_t scatter_factor_in_seconds;
  if (!policy_provider_->GetDevicePolicy().GetScatterFactorInSeconds(
          &scatter_factor_in_seconds)) {
    return false;
  }
  if (scatter_factor_in_seconds < 0) {
    LOG(WARNING) << "Ignoring negative scatter factor: "
                 << scatter_factor_in_seconds;
    return false;
  }
  *scatter_factor = base::Seconds(scatter_factor_in_seconds);
  return true;
}

bool RealDevicePolicyProvider::ConvertDisallowedTimeIntervals(
    WeeklyTimeIntervalVector* disallowed_intervals_out) const {
  vector<DevicePolicy::WeeklyTimeInterval> parsed_intervals;
  if (!policy_provider_->GetDevicePolicy().GetDisallowedTimeIntervals(
          &parsed_intervals)) {
    return false;
  }

  disallowed_intervals_out->clear();
  for (const auto& interval : parsed_intervals) {
    disallowed_intervals_out->emplace_back(
        WeeklyTime(interval.start_day_of_week, interval.start_time),
        WeeklyTime(interval.end_day_of_week, interval.end_time));
  }
  return true;
}

bool RealDevicePolicyProvider::ConvertHasOwner(bool* has_owner) const {
  string owner;
  if (!policy_provider_->GetDevicePolicy().GetOwner(&owner)) {
    return false;
  }
  *has_owner = !owner.empty();
  return true;
}

bool RealDevicePolicyProvider::ConvertChannelDowngradeBehavior(
    ChannelDowngradeBehavior* channel_downgrade_behavior) const {
  int behavior;
  if (!policy_provider_->GetDevicePolicy().GetChannelDowngradeBehavior(
          &behavior)) {
    return false;
  }
  if (behavior < static_cast<int>(ChannelDowngradeBehavior::kFirstValue) ||
      behavior > static_cast<int>(ChannelDowngradeBehavior::kLastValue)) {
    return false;
  }
  *channel_downgrade_behavior = static_cast<ChannelDowngradeBehavior>(behavior);
  return true;
}

bool RealDevicePolicyProvider::ConvertDeviceMarketSegment(
    string* market_segment) const {
  DevicePolicy::DeviceMarketSegment device_market_segment;
  if (!policy_provider_->GetDevicePolicy().GetDeviceMarketSegment(
          &device_market_segment)) {
    return false;
  }

  switch (device_market_segment) {
    case DevicePolicy::DeviceMarketSegment::kEducation:
      *market_segment = kMarketSegmentEducation;
      break;
    case DevicePolicy::DeviceMarketSegment::kEnterprise:
      *market_segment = kMarketSegmentEnterprise;
      break;
    case DevicePolicy::DeviceMarketSegment::kUnknown:
    default:
      *market_segment = kMarketSegmentUnknown;
  }
  return true;
}

void RealDevicePolicyProvider::RefreshDevicePolicy() {
  if (!policy_provider_->Reload()) {
    LOG(INFO) << "No device policies/settings present.";
  }

  var_device_policy_is_loaded_.SetValue(
      policy_provider_->device_policy_is_loaded());
  var_is_enterprise_enrolled_.SetValue(
      policy_provider_->IsEnterpriseEnrolledDevice());

  UpdateVariable(&var_release_channel_, &DevicePolicy::GetReleaseChannel);
  UpdateVariable(&var_release_channel_delegated_,
                 &DevicePolicy::GetReleaseChannelDelegated);
  UpdateVariable(&var_release_lts_tag_, &DevicePolicy::GetReleaseLtsTag);
  UpdateVariable(&var_update_disabled_, &DevicePolicy::GetUpdateDisabled);
  UpdateVariable(&var_target_version_prefix_,
                 &DevicePolicy::GetTargetVersionPrefix);
  UpdateVariable(&var_rollback_to_target_version_,
                 &RealDevicePolicyProvider::ConvertRollbackToTargetVersion);
  UpdateVariable(&var_rollback_allowed_milestones_,
                 &DevicePolicy::GetRollbackAllowedMilestones);
  UpdateVariable(&var_scatter_factor_,
                 &RealDevicePolicyProvider::ConvertScatterFactor);
  UpdateVariable(
      &var_allowed_connection_types_for_update_,
      &RealDevicePolicyProvider::ConvertAllowedConnectionTypesForUpdate);
  UpdateVariable(&var_has_owner_, &RealDevicePolicyProvider::ConvertHasOwner);
  UpdateVariable(&var_http_downloads_enabled_,
                 &DevicePolicy::GetHttpDownloadsEnabled);
  UpdateVariable(&var_au_p2p_enabled_, &DevicePolicy::GetAuP2PEnabled);
  UpdateVariable(&var_allow_kiosk_app_control_chrome_version_,
                 &DevicePolicy::GetAllowKioskAppControlChromeVersion);
  UpdateVariable(&var_disallowed_time_intervals_,
                 &RealDevicePolicyProvider::ConvertDisallowedTimeIntervals);
  UpdateVariable(&var_channel_downgrade_behavior_,
                 &RealDevicePolicyProvider::ConvertChannelDowngradeBehavior);
  UpdateVariable(&var_device_minimum_version_,
                 &DevicePolicy::GetHighestDeviceMinimumVersion);
  UpdateVariable(&var_quick_fix_build_token_,
                 &DevicePolicy::GetDeviceQuickFixBuildToken);
  UpdateVariable(&var_market_segment_,
                 &RealDevicePolicyProvider::ConvertDeviceMarketSegment);
}

}  // namespace chromeos_update_manager
