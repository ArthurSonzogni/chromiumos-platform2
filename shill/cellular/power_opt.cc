// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/cellular/power_opt.h"

#include <base/logging.h>
#include <base/time/time.h>

#include "shill/cellular/cellular_service_provider.h"
#include "shill/logging.h"
namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kCellular;
}  // namespace Logging

PowerOpt::PowerOpt(Manager* manager) : manager_(manager) {
  current_opt_info_ = nullptr;
}

void PowerOpt::Start() {
  SLOG(3) << __func__;
  power_opt_timer_.Start(
      FROM_HERE, kPowerStateCheckInterval,
      base::BindRepeating(&PowerOpt::PowerOptTask, weak_factory_.GetWeakPtr()));
}

void PowerOpt::Stop() {
  SLOG(3) << __func__;
  power_opt_timer_.Stop();
}

void PowerOpt::NotifyConnectionFailInvalidApn(const std::string& iccid) {
  if (opt_info_.count(iccid) == 0) {
    return;
  }
  PowerOptimizationInfo& info = opt_info_[iccid];
  if (!info.last_connect_fail_invalid_apn_time.is_null()) {
    info.no_service_invalid_apn_duration +=
        base::Time::Now() - info.last_connect_fail_invalid_apn_time;
    SLOG(2) << __func__ << ": " << "no_service_invalid_apn_duration (hours): "
            << info.no_service_invalid_apn_duration.InHours();
  }
  info.last_connect_fail_invalid_apn_time = base::Time::Now();

  if (info.no_service_invalid_apn_duration >
          kNoServiceInvalidApnTimeThreshold &&
      (base::Time::Now() - device_last_online_time_) >
          kLastOnlineShortThreshold) {
    PerformPowerOptimization(PowerEvent::kInvalidApn);
  }
}

void PowerOpt::NotifyRegistrationSuccess(const std::string& iccid) {
  if (opt_info_.count(iccid) == 0) {
    return;
  }
  opt_info_[iccid].no_service_invalid_apn_duration = base::Seconds(0);
  opt_info_[iccid].last_connect_fail_invalid_apn_time = base::Time();
}

void PowerOpt::UpdateManualConnectTime(const base::Time& connect_time) {
  if (!connect_time.is_null()) {
    user_connect_request_time_ = connect_time;
  }
}

void PowerOpt::UpdateDurationSinceLastOnline(
    const base::Time& last_online_time) {
  if (!current_opt_info_) {
    return;
  }

  if (!last_online_time.is_null()) {
    current_opt_info_->last_online_time = last_online_time;
    if (device_last_online_time_.is_null() ||
        last_online_time > device_last_online_time_) {
      device_last_online_time_ = last_online_time;
    }
  }

  // Time since user manually request cellular connection is less than
  // |kLastUserRequestThreshold|, keep modem power on.
  if (!user_connect_request_time_.is_null()) {
    base::TimeDelta since_last_user_connect_request =
        base::Time::Now() - user_connect_request_time_;
    if (since_last_user_connect_request < kLastUserRequestThreshold) {
      return;
    }
  }

  if (!device_last_online_time_.is_null()) {
    base::TimeDelta device_since_last_online =
        base::Time::Now() - device_last_online_time_;
    SLOG(2) << "Time since device was last online through cellular (days): "
            << device_since_last_online.InDays();
    if (device_since_last_online > kLastOnlineLongThreshold) {
      PerformPowerOptimization(PowerEvent::kLongNotOnline);
    }
  }
}

bool PowerOpt::UpdatePowerState(const std::string& iccid, PowerState state) {
  if (opt_info_.count(iccid) == 0) {
    return false;
  }
  current_opt_info_ = &opt_info_[iccid];
  if (state != opt_info_[iccid].power_state) {
    opt_info_[iccid].power_state = state;
    return true;
  }
  return false;
}

base::Time PowerOpt::GetLastOnlineTime(const std::string& iccid) {
  return opt_info_.count(iccid) > 0 ? opt_info_[iccid].last_online_time
                                    : base::Time();
}

base::TimeDelta PowerOpt::GetInvalidApnDuration(const std::string& iccid) {
  return opt_info_.count(iccid) > 0
             ? opt_info_[iccid].no_service_invalid_apn_duration
             : base::Seconds(0);
}

PowerOpt::PowerState PowerOpt::GetPowerState(const std::string& iccid) {
  if (opt_info_.count(iccid) > 0) {
    return opt_info_[iccid].power_state;
  } else {
    return PowerState::kUnknown;
  }
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
  if (opt_info_.count(iccid) > 0) {
    return false;
  }
  // either |opt_info_| is empty or no match
  PowerOptimizationInfo info;
  info.power_state = PowerState::kOn;
  opt_info_[iccid] = info;
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
            (base::Time::Now() - device_last_online_time_).InHours();
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
            (base::Time::Now() - device_last_online_time_).InHours();
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

void PowerOpt::PowerOptTask() {
  SLOG(3) << __func__;
  CheckLastOnline();
}

void PowerOpt::CheckLastOnline() {
  SLOG(3) << __func__;
  if (!manager_->cellular_service_provider()) {
    return;
  }
  base::Time last_online =
      manager_->cellular_service_provider()->FindLastOnline();
  if (!last_online.ToDeltaSinceWindowsEpoch().is_zero()) {
    UpdateDurationSinceLastOnline(last_online);
  }
}

}  // namespace shill
