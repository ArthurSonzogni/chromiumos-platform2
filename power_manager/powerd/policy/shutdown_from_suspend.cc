// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/policy/shutdown_from_suspend.h"

#include <utility>
#include <base/bind.h>
#include <base/check.h>
#include <base/logging.h>
#include <base/strings/stringprintf.h>

#include "power_manager/common/power_constants.h"
#include "power_manager/common/prefs.h"
#include "power_manager/common/util.h"
#include "power_manager/powerd/system/power_supply.h"
#include "power_manager/powerd/system/suspend_configurator.h"

namespace power_manager {
namespace policy {

ShutdownFromSuspend::ShutdownFromSuspend()
    : ShutdownFromSuspend(timers::SimpleAlarmTimer::Create()) {}
ShutdownFromSuspend::ShutdownFromSuspend(
    std::unique_ptr<timers::SimpleAlarmTimer> alarm_timer)
    : alarm_timer_(std::move(alarm_timer)) {}
ShutdownFromSuspend::~ShutdownFromSuspend() = default;

void ShutdownFromSuspend::Init(
    PrefsInterface* prefs,
    system::PowerSupplyInterface* power_supply,
    system::SuspendConfiguratorInterface* suspend_configurator) {
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
      prefs->GetInt64(kLowerPowerFromSuspendSecPref, &shutdown_after_sec) &&
      shutdown_after_sec > 0;

  // Hibernate only works if the system supports it, and
  // shutdown-after-x works.
  bool disable_hibernate = true;

  CHECK(prefs->GetBool(kDisableHibernatePref, &disable_hibernate))
      << "Failed to read pref " << kDisableHibernatePref;

  hibernate_enabled_ = global_enabled_ &&
                       suspend_configurator->IsHibernateAvailable() &&
                       !disable_hibernate;
  if (global_enabled_) {
    shutdown_delay_ = base::TimeDelta::FromSeconds(shutdown_after_sec);
    prefs->GetDouble(kLowBatteryShutdownPercentPref,
                     &low_battery_shutdown_percent_);
    LOG(INFO) << (hibernate_enabled_ ? "Hibernate" : "Shutdown")
              << " from suspend is configured to "
              << util::TimeDeltaToString(shutdown_delay_)
              << ". low_battery_shutdown_percent is "
              << low_battery_shutdown_percent_;
  } else {
    LOG(INFO) << "Shutdown/Hibernate from suspend is disabled";
  }
}

bool ShutdownFromSuspend::IsBatteryLow() {
  if (power_supply_->RefreshImmediately()) {
    const double percent = power_supply_->GetPowerStatus().battery_percentage;
    if (0 <= percent && percent <= low_battery_shutdown_percent_) {
      LOG(INFO) << "Battery percentage " << base::StringPrintf("%0.2f", percent)
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

bool ShutdownFromSuspend::ShouldHibernate() {
  if (!hibernate_enabled_) {
    return false;
  }

  if (ShutdownFromSuspend::IsBatteryLow()) {
    LOG(INFO) << "Hibernate due to low battery";
    return true;
  }

  if (!timer_fired_) {
    LOG(INFO) << "Don't hibernate, timer hasn't fired";
    return false;
  }

  return true;
}

bool ShutdownFromSuspend::ShouldShutdown() {
  if (timer_fired_) {
    LOG(INFO) << "Timer expired. Device should shut down";
    return true;
  }

  if (ShutdownFromSuspend::IsBatteryLow()) {
    LOG(INFO) << "Shut down due to low battery";
    return true;
  }

  return false;
}

ShutdownFromSuspend::Action ShutdownFromSuspend::PrepareForSuspendAttempt() {
  if (!global_enabled_)
    return ShutdownFromSuspend::Action::SUSPEND;

  if (in_dark_resume_ && ShutdownFromSuspend::ShouldHibernate()) {
    return ShutdownFromSuspend::Action::HIBERNATE;
  }

  // TODO(crbug.com/964510): If the timer is gonna expire in next few minutes,
  // shutdown.
  if (in_dark_resume_ && ShutdownFromSuspend::ShouldShutdown()) {
    if (!power_supply_->GetPowerStatus().line_power_on) {
      LOG(INFO) << "Shutting down.";
      return ShutdownFromSuspend::Action::SHUT_DOWN;
    }
    LOG(INFO) << "Not shutting down from resume as line power is connected.";
  }

  if (!alarm_timer_) {
    LOG(WARNING) << "System doesn't support CLOCK_REALTIME_ALARM";
    return ShutdownFromSuspend::Action::SUSPEND;
  }
  if (!alarm_timer_->IsRunning()) {
    alarm_timer_->Start(
        FROM_HERE, shutdown_delay_,
        base::Bind(&ShutdownFromSuspend::OnTimerWake, base::Unretained(this)));
  }

  return ShutdownFromSuspend::Action::SUSPEND;
}

void ShutdownFromSuspend::HandleDarkResume() {
  in_dark_resume_ = true;
}

void ShutdownFromSuspend::HandleFullResume() {
  in_dark_resume_ = false;
  if (alarm_timer_)
    alarm_timer_->Stop();
  else
    LOG(WARNING) << "System doesn't support CLOCK_REALTIME_ALARM.";
  timer_fired_ = false;
}

void ShutdownFromSuspend::OnTimerWake() {
  timer_fired_ = true;
}

}  // namespace policy
}  // namespace power_manager
