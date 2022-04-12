// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_POLICY_ADAPTIVE_CHARGING_CONTROLLER_H_
#define POWER_MANAGER_POWERD_POLICY_ADAPTIVE_CHARGING_CONTROLLER_H_

#include <memory>
#include <utility>
#include <vector>

#include <base/macros.h>
#include <base/time/time.h>
#include <brillo/errors/error.h>
#include <brillo/timers/alarm_timer.h>

#include "ml/proto_bindings/ranker_example.pb.h"

#include "power_manager/common/metrics_constants.h"
#include "power_manager/common/power_constants.h"
#include "power_manager/common/prefs.h"
#include "power_manager/powerd/policy/backlight_controller.h"
#include "power_manager/powerd/system/input_watcher_interface.h"
#include "power_manager/powerd/system/power_supply.h"
#include "power_manager/powerd/system/power_supply_observer.h"
#include "power_manager/proto_bindings/policy.pb.h"
#include "power_manager/proto_bindings/user_charging_event.pb.h"

namespace power_manager {

namespace policy {

class AdaptiveChargingControllerInterface : public system::PowerSupplyObserver {
 public:
  using AdaptiveChargingState = metrics::AdaptiveChargingState;

  class Delegate {
   public:
    // Set the battery sustain state to `lower`, `upper`. `lower` is the charge
    // percent which will be the minimum charge for the battery before it starts
    // charging again. `upper` is the maximum charge. If the battery charge goes
    // over this, it will start to discharge by disabling the AC input current.
    // If `upper` == `lower` and 0 < `upper` < 100, `upper` will be maintained
    // after it is reached by disabling charging (AC will provide current, but
    // won't charge the battery). If both `lower` and `upper` are -1, charge
    // behavior is reverted to the default behavior.
    // Returns true upon success and false otherwise.
    virtual bool SetBatterySustain(int lower, int upper) = 0;

    // Get the prediction for the next X hours on whether the charger will be
    // connected. If a value in `result` is >= `min_probability_` and larger
    // than any other value in `result`, the charger is predicted to be
    // unplugged during that hour (except for the last value, which means longer
    // than the number of hours associated with the second to last value).
    // `proto` contains all of the features for the ML model, and `async`
    // indicates if this should not block. Calls `OnPredictionResponse` on
    // success and `OnPredictionFail` otherwise.
    virtual void GetAdaptiveChargingPrediction(
        const assist_ranker::RankerExample& proto, bool async) = 0;

    virtual void GenerateAdaptiveChargingUnplugMetrics(
        const AdaptiveChargingState state,
        const base::TimeTicks& target_time,
        const base::TimeTicks& hold_start_time,
        const base::TimeTicks& hold_end_time,
        const base::TimeTicks& charge_finished_time,
        double display_battery_percentage) = 0;
  };

  AdaptiveChargingControllerInterface() {}
  AdaptiveChargingControllerInterface(
      const AdaptiveChargingControllerInterface&) = delete;
  AdaptiveChargingControllerInterface& operator=(
      const AdaptiveChargingControllerInterface&) = delete;

  virtual ~AdaptiveChargingControllerInterface() {}

  // For handling setting changes from the UI settings page or Enterprise
  // policy.
  virtual void HandlePolicyChange(const PowerManagementPolicy& policy) = 0;

  // Runs the prediction before suspending to maximize the delay until we wake
  // in dark resume to re-evaluate charging delays.
  virtual void PrepareForSuspendAttempt() = 0;

  // Disables Adaptive Charging for shutdown (and hibernate).
  virtual void HandleShutdown() = 0;

  // Function to pass in the results from the Adaptive Charging ml-service.
  // Handles the logic on how to delay charging based on the prediction,
  // `result`.
  virtual void OnPredictionResponse(bool inference_done,
                                    const std::vector<double>& result) = 0;

  // Called upon failure from the Adaptive Charging ml-service.
  virtual void OnPredictionFail(brillo::Error* error) = 0;
};

class AdaptiveChargingController : public AdaptiveChargingControllerInterface {
 public:
  static constexpr base::TimeDelta kFinishChargingDelay = base::Hours(2);

  AdaptiveChargingController() = default;
  AdaptiveChargingController(const AdaptiveChargingController&) = delete;
  AdaptiveChargingController& operator=(const AdaptiveChargingController&) =
      delete;
  ~AdaptiveChargingController() override;

  void Init(Delegate* delegate,
            BacklightController* backlight_controller,
            system::InputWatcherInterface* input_watcher,
            system::PowerSupplyInterface* power_supply,
            PrefsInterface* prefs);

  void set_recheck_alarm_for_testing(
      std::unique_ptr<brillo::timers::SimpleAlarmTimer> alarm) {
    recheck_alarm_ = std::move(alarm);
  }

  void set_charge_alarm_for_testing(
      std::unique_ptr<brillo::timers::SimpleAlarmTimer> alarm) {
    charge_alarm_ = std::move(alarm);
  }

  void set_charge_delay_for_testing(base::TimeDelta delay) {
    StartChargeAlarm(delay);
  }

  base::TimeDelta get_charge_delay_for_testing() {
    return target_full_charge_time_ - base::TimeTicks::Now() -
           kFinishChargingDelay;
  }

  base::TimeTicks get_target_full_charge_time_for_testing() {
    return target_full_charge_time_;
  }

  void set_recheck_delay_for_testing(base::TimeDelta delay) {
    StartRecheckAlarm(delay);
  }

  // Overridden from AdaptiveChargingControllerInterface:
  void HandlePolicyChange(const PowerManagementPolicy& policy) override;
  void PrepareForSuspendAttempt() override;
  void HandleShutdown() override;
  void OnPredictionResponse(bool inference_done,
                            const std::vector<double>& result) override;
  void OnPredictionFail(brillo::Error* error) override;

  // Overridden from system::PowerSupplyObserver:
  void OnPowerStatusUpdate() override;

 private:
  // Sets battery sustain via the `Delegate::SetBatterySustain` callback.
  // Returns true on success and false otherwise.
  bool SetSustain(int64_t lower, int64_t upper);

  // Initiates Adaptive Charging logic, which fetches predictions from the
  // Adaptive Charging ml-service, and delays charging if
  // `adaptive_charging_enabled_` is true.
  void StartAdaptiveCharging(const UserChargingEvent::Event::Reason& reason);

  // Starts the prediction evaluation. Logic is finished via the
  // `OnPredictionResponse` callback.
  void UpdateAdaptiveCharging(const UserChargingEvent::Event::Reason& reason,
                              bool async);

  // Stops Adaptive Charging from delaying charge anymore. The `recheck_alarm_`
  // and `charge_alarm_` will no longer run unless `StartAdaptiveCharging` is
  // called.
  void StopAdaptiveCharging();

  // Indicates that the prediction code will periodically run for re-evaluating
  // charging delays.
  bool IsRunning();

  // We've reached a display battery percentage where the battery sustainer is
  // active, which in practice means >= `lower` - 1 (`lower` is the last `lower`
  // value passed to `SetSustain`). We subtract 1 since charge can momentarily
  // drop below `lower` with how the battery sustainer code works.
  bool AtHoldPercent(double display_battery_percent);

  // Schedule re-evaluation of the prediction code after `delay`.
  void StartRecheckAlarm(base::TimeDelta delay);

  // Schedule stopping Adaptive Charging, which disables the battery sustainer
  // and `recheck_alarm_` after `delay`.
  void StartChargeAlarm(base::TimeDelta delay);

  // Callback for the `recheck_alarm_`. Re-evaluates the prediction.
  void OnRecheckAlarmFired();

  Delegate* delegate_;  // non-owned

  system::PowerSupplyInterface* power_supply_;  // non-owned

  system::InputWatcherInterface* input_watcher_;  // non-owned

  policy::BacklightController* backlight_controller_;  // non-owned

  PrefsInterface* prefs_;  // non-owned

  PowerSupplyProperties::ExternalPower cached_external_power_;

  // For periodically rechecking charger unplug predictions. A SimpleAlarmTimer
  // is used since this will wake the system from suspend (in dark resume) to do
  // this as well.
  std::unique_ptr<brillo::timers::SimpleAlarmTimer> recheck_alarm_ =
      brillo::timers::SimpleAlarmTimer::Create();

  // For charging to full after sustaining `hold_percent_`. A SimpleAlarmTimer
  // is used since we need to wake up the system (in dark resume) to do this as
  // well.
  std::unique_ptr<brillo::timers::SimpleAlarmTimer> charge_alarm_ =
      brillo::timers::SimpleAlarmTimer::Create();

  // Current target for when we plan to fully charge the battery.
  base::TimeTicks target_full_charge_time_;

  // The time when we started delaying charge via the battery sustainer. Used
  // for reporting metrics.
  base::TimeTicks hold_percent_start_time_;

  // The time when we stopped delaying charge. Used for reporting metrics.
  base::TimeTicks hold_percent_end_time_;

  // The time when we reached fill charge. Used for reporting metrics.
  base::TimeTicks charge_finished_time_;

  // Interval for rechecking the prediction, and modifying whether charging is
  // delayed based on that prediction.
  base::TimeDelta recheck_alarm_interval_;

  // Tracks the specific state of Adaptive Charging for UMA reporting.
  AdaptiveChargingState state_;

  // Whether we should report the AdaptiveChargingTimeToFull metric, which
  // should only be done if charging started with the battery charge less than
  // `hold_percent_`.
  bool report_charge_time_;

  // The default upper percent for the battery sustainer. Not used if the
  // battery has a higher display battery percentage when the AC is connected.
  int64_t hold_percent_;

  // Used for setting the lower percent for the battery sustainer, with `upper`
  // - `hold_delta_percent_`. Used to work around "singing" capacitors, which
  // are on some systems. When there is no current going to or from the battery,
  // the system load from the AC power circuit can drop low enough that makes
  // the capacitors vibrate at an audible frequency. By always having the
  // battery charge or discharge (AC current is disabled in this case), we can
  // avoid the "singing" of these capacitors.
  int64_t hold_delta_percent_;

  // The battery percent to display while delaying charge. Will be
  // `hold_percent_` or the display battery percentage when battery sustainer
  // starts if it's higher than `hold_percent_`.
  int64_t display_percent_;

  // Minimum value for the prediction from the Adaptive Charging ml-service that
  // is interpreted as expecting the AC to be unplugged at a specific hour. The
  // service returns a vector of doubles in the range (0.0, 1.0). The largest
  // value in this vector must be larger than `min_probability_` for the
  // prediction to be used to delay charging.
  double min_probability_;

  // Whether the Battery Sustainer is currently set for Adaptive Charging.
  bool is_sustain_set_;

  // The following two booleans control how this class behaves via the following
  // table:
  // enabled | supported |
  // 1       | 1         | evaluate predictions and delay charging
  // 1       | 0         | scenario does not exist
  // 0       | 1         | evaluate predictions but do not delay charging
  // 0       | 0         | evaluate predictions but do not delay charging
  //
  // Whether Adaptive Charging will delay charging. Predictions are still
  // evaluated if this is false.
  bool adaptive_charging_enabled_;

  // Whether the system supports battery sustainer on the EC. Explicitly checked
  // for during `Init`. Adaptive Charging cannot be enabled unless this is true.
  bool adaptive_charging_supported_;
};

}  // namespace policy
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_POLICY_ADAPTIVE_CHARGING_CONTROLLER_H_
