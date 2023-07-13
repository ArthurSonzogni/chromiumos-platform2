// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_POLICY_SHUTDOWN_FROM_SUSPEND_H_
#define POWER_MANAGER_POWERD_POLICY_SHUTDOWN_FROM_SUSPEND_H_

#include "power_manager/powerd/policy/shutdown_from_suspend_interface.h"
#include "power_manager/powerd/system/suspend_configurator.h"
#include "power_manager/powerd/system/wakeup_timer.h"
#include "power_manager/proto_bindings/policy.pb.h"

#include <memory>

#include <base/time/time.h>
#include <brillo/timers/alarm_timer.h>

namespace power_manager {

class PrefsInterface;

namespace system {

class PowerSupplyInterface;

}  // namespace system

namespace policy {

class ShutdownFromSuspend : public ShutdownFromSuspendInterface {
 public:
  ShutdownFromSuspend();
  ShutdownFromSuspend(const ShutdownFromSuspend&) = delete;
  ShutdownFromSuspend& operator=(const ShutdownFromSuspend&) = delete;
  ~ShutdownFromSuspend() override;

  void Init(PrefsInterface* prefs,
            system::PowerSupplyInterface* power_supply,
            system::SuspendConfiguratorInterface* suspend_configurator);

  bool enabled_for_testing() const { return global_enabled_; }
  bool hibernate_enabled_for_testing() const {
    return enabled_for_testing() &&
           suspend_configurator_->IsHibernateAvailable();
  }

  // ShutdownFromSuspendInterface implementation.
  Action PrepareForSuspendAttempt() override;
  void HandleDarkResume() override;
  void HandleFullResume() override;
  void HandlePolicyChange(const PowerManagementPolicy& policy) override;

 private:
  ShutdownFromSuspend(
      std::unique_ptr<power_manager::system::WakeupTimer> shutdown_timer,
      std::unique_ptr<power_manager::system::WakeupTimer> hibernate_timer);

  friend class ShutdownFromSuspendTest;

  // Invoked by |shutdown_alarm_timer_| after spending |shutdown_delay_| in
  // suspend.
  void OnShutdownTimerWake();
  // Invoked by |hibernate_alarm_timer_| after spending the hibernate timeout in
  // suspend. This timeout value is determined by calling
  // SuspendConfigurator::MinimumTimeInSuspendBeforeHibernate.
  void OnHibernateTimerWake();

  // Helper function to determine if the battery is below a certain threshold.
  bool IsBatteryLow();
  // Called to decide whether or not we should shutdown or hibernate right now.
  Action DetermineTargetState();
  // Start or RestartTimers based on the state of the system. For example, after
  // the hibernate timer fires we want to immediately reset it because even if
  // the system fails to hibernate we can try again later.
  void ConfigureTimers();

  // Is shutdown or hibernate after x enabled ?
  bool global_enabled_ = false;

  // Time in suspend after which the device wakes up to shut down.
  base::TimeDelta shutdown_delay_;
  // Configurable delay for the |hibernate_alarm_timer_|.
  base::TimeDelta hibernate_delay_;

  // Is the device in dark resume currently ?
  bool in_dark_resume_ = false;

  // Has the shutdown or hibernate alarm fired ? They will have different
  // timeouts where the hibernate timer will always be less than the shutdown
  // timer.
  bool shutdown_timer_fired_ = false;
  bool hibernate_timer_fired_ = false;

  // Number of hibernate attempts since dark resume was entered,
  int64_t hibernate_attempts_ = 0;

  // Timer to wake the system from suspend after |shutdown_delay_|.
  std::unique_ptr<power_manager::system::WakeupTimer> shutdown_alarm_timer_;
  std::unique_ptr<power_manager::system::WakeupTimer> hibernate_alarm_timer_;

  system::PowerSupplyInterface* power_supply_ = nullptr;  // weak
  system::SuspendConfiguratorInterface* suspend_configurator_ =
      nullptr;  // weak

  double low_battery_shutdown_percent_ = 0.0;
};

}  // namespace policy
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_POLICY_SHUTDOWN_FROM_SUSPEND_H_
