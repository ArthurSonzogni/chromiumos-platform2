// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/policy/shutdown_from_suspend.h"

#include <base/bind.h>

#include "power_manager/common/power_constants.h"
#include "power_manager/common/prefs.h"
#include "power_manager/common/util.h"
#include "power_manager/powerd/system/power_supply.h"

namespace power_manager {
namespace policy {

ShutdownFromSuspend::ShutdownFromSuspend() = default;
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
  enabled_ =
      !dark_resume_disable &&
      prefs->GetInt64(kShutdownFromSuspendSecPref, &shutdown_after_sec) &&
      shutdown_after_sec > 0;

  if (enabled_) {
    shutdown_delay_ = base::TimeDelta::FromSeconds(shutdown_after_sec);
    LOG(INFO) << "Shutdown from suspend is configured to "
              << util::TimeDeltaToString(shutdown_delay_);
  } else {
    LOG(INFO) << "Shutdown from suspend is disabled";
  }
}

ShutdownFromSuspend::Action ShutdownFromSuspend::PrepareForSuspendAttempt() {
  if (!enabled_)
    return ShutdownFromSuspend::Action::SUSPEND;

  // TODO(crbug.com/964510): If the timer is gonna expire in next few minutes,
  // shutdown.
  if (in_dark_resume_ && timer_fired_) {
    if (!power_supply_->GetPowerStatus().line_power_on)
      return ShutdownFromSuspend::Action::SHUT_DOWN;

    LOG(INFO) << "Not shutting down even after "
              << util::TimeDeltaToString(shutdown_delay_)
              << " in suspend as line power is connected";
  }

  if (!alarm_timer_.IsRunning()) {
    alarm_timer_.Start(
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
  alarm_timer_.Stop();
  timer_fired_ = false;
}

void ShutdownFromSuspend::OnTimerWake() {
  timer_fired_ = true;
}

}  // namespace policy
}  // namespace power_manager
