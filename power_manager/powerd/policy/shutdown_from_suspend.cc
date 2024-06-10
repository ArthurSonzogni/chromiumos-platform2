// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/policy/shutdown_from_suspend.h"

#include <utility>
#include <base/check.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/strings/stringprintf.h>

#include "power_manager/common/power_constants.h"
#include "power_manager/common/prefs.h"
#include "power_manager/common/tracing.h"
#include "power_manager/common/util.h"
#include "power_manager/powerd/system/power_supply.h"
#include "power_manager/powerd/system/wakeup_timer.h"

namespace power_manager::policy {

using power_manager::system::RealWakeupTimer;
using power_manager::system::WakeupTimer;

ShutdownFromSuspend::ShutdownFromSuspend()
    : ShutdownFromSuspend(RealWakeupTimer::Create(CLOCK_BOOTTIME_ALARM)) {}

ShutdownFromSuspend::ShutdownFromSuspend(
    std::unique_ptr<WakeupTimer> shutdown_alarm_timer)
    : shutdown_alarm_timer_(std::move(shutdown_alarm_timer)) {}

ShutdownFromSuspend::~ShutdownFromSuspend() = default;

void ShutdownFromSuspend::Init(PrefsInterface* prefs,
                               system::PowerSupplyInterface* power_supply) {
  DCHECK(prefs);
  DCHECK(power_supply);

  power_supply_ = power_supply;

  // Shutdown after X can only work if dark resume is enabled.
  bool dark_resume_disable =
      prefs->GetBool(kDisableDarkResumePref, &dark_resume_disable) &&
      dark_resume_disable;

  int64_t shutdown_after_sec = 0;
  global_enabled_ =
      !dark_resume_disable &&
      prefs->GetInt64(kShutdownFromSuspendSecPref, &shutdown_after_sec) &&
      shutdown_after_sec > 0;

  if (global_enabled_) {
    shutdown_delay_ = base::Seconds(shutdown_after_sec);
    prefs->GetDouble(kLowBatteryShutdownPercentPref,
                     &low_battery_shutdown_percent_);
    LOG(INFO) << "Shutdown from suspend is configured to "
              << util::TimeDeltaToString(shutdown_delay_)
              << ". low_battery_shutdown_percent is "
              << low_battery_shutdown_percent_;
  } else {
    LOG(INFO) << "Shutdown from suspend is disabled";
  }
}

bool ShutdownFromSuspend::IsBatteryLow() {
  if (power_supply_->RefreshImmediately()) {
    system::PowerStatus status = power_supply_->GetPowerStatus();
    if (status.battery_below_shutdown_threshold) {
      LOG(INFO) << "Battery percentage "
                << base::StringPrintf("%0.2f", status.battery_percentage)
                << "% <= low_battery_shutdown_percent ("
                << base::StringPrintf("%0.2f", low_battery_shutdown_percent_)
                << "%).";
      return true;
    }
  } else {
    LOG(ERROR) << "Failed to refresh battery status";
  }

  return false;
}

ShutdownFromSuspend::Action ShutdownFromSuspend::DetermineTargetState() {
  const system::PowerStatus power_status = power_supply_->GetPowerStatus();
  if (power_status.line_power_on)
    return Action::SUSPEND;

  if (shutdown_timer_fired_) {
    // Shutdown after x (if not on line power).
    LOG(INFO) << "Shutdown timer expired. The system will shutdown";
    return Action::SHUT_DOWN;
  }

  if (ShutdownFromSuspend::IsBatteryLow()) {
    // If the battery is low we always will shutdown.
    LOG(INFO) << "Shutdown due to low battery";
    return Action::SHUT_DOWN;
  }

  // By default we will suspend.
  return Action::SUSPEND;
}

void ShutdownFromSuspend::ConfigureShutdownTimer() {
  if (!shutdown_alarm_timer_) {
    LOG(WARNING) << "System doesn't support CLOCK_REALTIME_ALARM";
    return;
  }

  if (!shutdown_alarm_timer_->IsRunning()) {
    shutdown_alarm_timer_->Start(
        shutdown_delay_,
        base::BindRepeating(&ShutdownFromSuspend::OnShutdownTimerWake,
                            base::Unretained(this)));
    shutdown_timer_fired_ = false;
  }
}

ShutdownFromSuspend::Action ShutdownFromSuspend::PrepareForSuspendAttempt() {
  if (!global_enabled_)
    return ShutdownFromSuspend::Action::SUSPEND;

  ShutdownFromSuspend::Action action = ShutdownFromSuspend::Action::SUSPEND;

  if (in_dark_resume_) {
    action = DetermineTargetState();
  }

  ConfigureShutdownTimer();
  return action;
}

void ShutdownFromSuspend::HandleDarkResume() {
  in_dark_resume_ = true;
}

void ShutdownFromSuspend::HandleFullResume() {
  in_dark_resume_ = false;

  if (shutdown_alarm_timer_)
    shutdown_alarm_timer_->Stop();

  LOG_IF(WARNING, !shutdown_alarm_timer_)
      << "System doesn't support CLOCK_REALTIME_ALARM.";
  shutdown_timer_fired_ = false;
}

void ShutdownFromSuspend::OnShutdownTimerWake() {
  TRACE_EVENT("power", "ShutdownFromSuspend::OnShutdownTimerWake");
  shutdown_timer_fired_ = true;
}

}  // namespace power_manager::policy
