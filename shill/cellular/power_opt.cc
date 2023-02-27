// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/cellular/power_opt.h"

#include <base/time/time.h>
#include "shill/logging.h"
namespace shill {

PowerOpt::PowerOpt(Manager* manager) : manager_(manager) {
  current_opt_info_ = nullptr;
}

void PowerOpt::NotifyConnectionFailInvalidApn(const std::string& iccid) {
  if (opt_info_.count(iccid) == 0)
    return;
  PowerOptimizationInfo& info = opt_info_[iccid];
  if (!info.last_connect_fail_invalid_apn_time.is_null()) {
    info.no_service_invalid_apn_duration +=
        base::Time::Now() - info.last_connect_fail_invalid_apn_time;
    LOG(INFO) << __func__ << ": "
              << "no_service_invalid_apn_duration (minutes): "
              << info.no_service_invalid_apn_duration.InMinutes();
  }
  info.last_connect_fail_invalid_apn_time = base::Time::Now();

  if ((info.no_service_invalid_apn_duration >
           kNoServiceInvalidApnTimeThreshold &&
       info.time_since_last_online > kLastOnlineShortThreshold)) {
    PerformPowerOptimization(PowerEvent::kInvalidApn);
  }
}

void PowerOpt::NotifyRegistrationSuccess(const std::string& iccid) {
  if (opt_info_.count(iccid) == 0)
    return;
  opt_info_[iccid].no_service_invalid_apn_duration = base::Seconds(0);
  opt_info_[iccid].last_connect_fail_invalid_apn_time = base::Time();
  LOG(INFO) << __func__ << ": Reset invalid Apn related power opt info.";
}

void PowerOpt::UpdateDurationSinceLastOnline(const base::Time& last_online_time,
                                             bool is_user_request) {
  if (!current_opt_info_)
    return;
  current_opt_info_->time_since_last_online =
      base::Time::Now() - last_online_time;
  if (is_user_request &&
      current_opt_info_->time_since_last_online > kLastOnlineLongThreshold) {
    // |last_online|------------|now|----<grace period>---|trigger point|
    current_opt_info_->time_since_last_online =
        kLastOnlineLongThreshold - kUserRequestGracePeriod;
  }
  LOG(INFO) << "since_last_online (minutes): "
            << current_opt_info_->time_since_last_online.InMinutes();
  if (current_opt_info_->time_since_last_online > kLastOnlineLongThreshold) {
    PerformPowerOptimization(PowerEvent::kLongNotOnline);
  }
}

bool PowerOpt::UpdatePowerState(const std::string& iccid, PowerState state) {
  if (opt_info_.count(iccid) == 0)
    return false;
  current_opt_info_ = &opt_info_[iccid];
  if (state != opt_info_[iccid].power_state) {
    opt_info_[iccid].power_state = state;
    return true;
  }
  return false;
}

base::TimeDelta PowerOpt::GetTimeSinceLastOnline(const std::string& iccid) {
  if (opt_info_.count(iccid) > 0)
    return opt_info_[iccid].time_since_last_online;
  else
    return base::Seconds(0);
}

base::TimeDelta PowerOpt::GetInvalidApnDuration(const std::string& iccid) {
  if (opt_info_.count(iccid) > 0)
    return opt_info_[iccid].no_service_invalid_apn_duration;
  else
    return base::Seconds(0);
}

PowerOpt::PowerState PowerOpt::GetPowerState(const std::string& iccid) {
  if (opt_info_.count(iccid) > 0)
    return opt_info_[iccid].power_state;
  else
    return PowerState::kUnknown;
}

bool PowerOpt::RequestPowerStateChange(PowerOpt::PowerState power_state) {
  if (power_state == PowerState::kLow || power_state == PowerState::kOff) {
    LOG(INFO) << __func__ << ": disable cellular.";
    manager_->SetEnabledStateForTechnology(kTypeCellular, false, false,
                                           base::DoNothing());
  }
  return true;
}

bool PowerOpt::AddOptInfoForNewService(const std::string& iccid) {
  if (opt_info_.count(iccid) > 0)
    return false;
  // either |opt_info_| is empty or no match
  PowerOptimizationInfo info;
  info.power_state = PowerState::kOn;
  opt_info_[iccid] = info;
  LOG(INFO) << __func__ << ": power optimization info is added for iccid "
            << iccid << ". opt_info_.size() is " << opt_info_.size();
  return true;
}

PowerOpt::PowerState PowerOpt::PerformPowerOptimization(
    PowerOpt::PowerEvent event) {
  Metrics::CellularPowerOptimizationInfo metrics_info;
  PowerState target_power_state = PowerState::kUnknown;
  switch (event) {
    case PowerEvent::kInvalidApn:
      if (current_opt_info_->power_state == PowerState::kOn) {
        target_power_state = PowerState::kLow;
        metrics_info.reason = Metrics::CellularPowerOptimizationInfo::
            CellularPowerOptimizationReason::kNoServiceInvalidApn;
        metrics_info.new_power_state =
            Metrics::CellularPowerOptimizationInfo::PowerState::kLow;
        metrics_info.since_last_online_hours =
            current_opt_info_->time_since_last_online.InHours();
      }
      break;
    case PowerEvent::kNoRoamingAgreement:
    case PowerEvent::kServiceBlocked:
      break;
    case PowerEvent::kLongNotOnline:
      if (current_opt_info_->power_state == PowerState::kOn) {
        target_power_state = PowerState::kLow;
        metrics_info.reason = Metrics::CellularPowerOptimizationInfo::
            CellularPowerOptimizationReason::kNoServiceLongNotOnline;
        metrics_info.new_power_state =
            Metrics::CellularPowerOptimizationInfo::PowerState::kLow;
        metrics_info.since_last_online_hours =
            current_opt_info_->time_since_last_online.InHours();
      }
      break;
    case PowerEvent::kUnkown:
      break;
  }
  if (target_power_state != PowerState::kUnknown) {
    bool action_result = RequestPowerStateChange(target_power_state);
    if (action_result) {
      current_opt_info_->power_state = target_power_state;
      manager_->metrics()->NotifyCellularPowerOptimization(metrics_info);
    }
  }
  return current_opt_info_->power_state;
}

}  // namespace shill
