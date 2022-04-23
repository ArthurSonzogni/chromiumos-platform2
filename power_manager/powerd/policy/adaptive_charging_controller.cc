// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <cmath>
#include <vector>

#include <base/logging.h>
#include <base/time/time.h>
#include <dbus/bus.h>
#include <dbus/message.h>

#include "chromeos/dbus/service_constants.h"
#include "power_manager/common/power_constants.h"
#include "power_manager/powerd/policy/adaptive_charging_controller.h"

namespace power_manager {
namespace policy {

namespace {
const int64_t kBatterySustainDisabled = -1;
const base::TimeDelta kDefaultAlarmInterval = base::Minutes(30);
const int64_t kDefaultHoldPercent = 80;
const double kDefaultMinProbability = 0.2;
const int kAdaptiveChargingTimeBucketMin = 15;
}  // namespace

AdaptiveChargingController::AdaptiveChargingController()
    : weak_ptr_factory_(this) {}

AdaptiveChargingController::~AdaptiveChargingController() {
  if (power_supply_)
    power_supply_->RemoveObserver(this);
}

void AdaptiveChargingController::Init(
    AdaptiveChargingController::Delegate* delegate,
    BacklightController* backlight_controller,
    system::InputWatcherInterface* input_watcher,
    system::PowerSupplyInterface* power_supply,
    system::DBusWrapperInterface* dbus_wrapper,
    PrefsInterface* prefs) {
  delegate_ = delegate;
  backlight_controller_ = backlight_controller;
  input_watcher_ = input_watcher;
  power_supply_ = power_supply;
  dbus_wrapper_ = dbus_wrapper;
  prefs_ = prefs;
  recheck_alarm_interval_ = kDefaultAlarmInterval;
  report_charge_time_ = false;
  hold_percent_ = kDefaultHoldPercent;
  hold_delta_percent_ = 0;
  display_percent_ = kDefaultHoldPercent;
  min_probability_ = kDefaultMinProbability;
  cached_external_power_ = PowerSupplyProperties_ExternalPower_DISCONNECTED;
  is_sustain_set_ = false;
  adaptive_charging_enabled_ = false;

  power_supply_->AddObserver(this);

  dbus_wrapper->ExportMethod(
      kChargeNowForAdaptiveChargingMethod,
      base::BindRepeating(&AdaptiveChargingController::HandleChargeNow,
                          weak_ptr_factory_.GetWeakPtr()));

  int64_t alarm_seconds;
  if (prefs_->GetInt64(kAdaptiveChargingAlarmSecPref, &alarm_seconds)) {
    CHECK_GT(alarm_seconds, 0);
    recheck_alarm_interval_ = base::Seconds(alarm_seconds);
  }

  prefs_->GetInt64(kAdaptiveChargingHoldPercentPref, &hold_percent_);
  prefs_->GetInt64(kAdaptiveChargingHoldDeltaPercentPref, &hold_delta_percent_);
  prefs_->GetDouble(kAdaptiveChargingMinProbabilityPref, &min_probability_);
  prefs_->GetBool(kAdaptiveChargingEnabledPref, &adaptive_charging_enabled_);
  CHECK(hold_percent_ < 100 && hold_percent_ > 0);
  CHECK(hold_delta_percent_ < 100 && hold_delta_percent_ >= 0);
  CHECK(min_probability_ >= 0 && min_probability_ <= 1.0);

  // Check if setting meaningless battery sustain values works. If the battery
  // sustain functionality is not supported on this system, we will still run ML
  // models for Adaptive Charging so we can track how well we would do if it is
  // enabled.
  adaptive_charging_supported_ = SetSustain(100, 100);
  if (!adaptive_charging_supported_) {
    adaptive_charging_enabled_ = false;
    state_ = AdaptiveChargingState::NOT_SUPPORTED;
  }

  state_ = adaptive_charging_enabled_ ? AdaptiveChargingState::INACTIVE
                                      : AdaptiveChargingState::USER_DISABLED;

  LOG(INFO) << "Adaptive Charging is "
            << (adaptive_charging_supported_ ? "supported" : "not supported")
            << " and " << (adaptive_charging_enabled_ ? "enabled" : "disabled")
            << ". Battery sustain range: ("
            << hold_percent_ - hold_delta_percent_ << ", " << hold_percent_
            << "), Minimum ML probability value: " << min_probability_;

  SetSustain(kBatterySustainDisabled, kBatterySustainDisabled);
  power_supply_->SetAdaptiveChargingSupported(adaptive_charging_supported_);
}

void AdaptiveChargingController::HandlePolicyChange(
    const PowerManagementPolicy& policy) {
  bool restart_adaptive = false;
  if (policy.has_adaptive_charging_hold_percent() &&
      policy.adaptive_charging_hold_percent() != hold_percent_) {
    hold_percent_ = policy.adaptive_charging_hold_percent();
    restart_adaptive = IsRunning();
  }

  if (policy.has_adaptive_charging_min_probability() &&
      policy.adaptive_charging_min_probability() != min_probability_) {
    min_probability_ = policy.adaptive_charging_min_probability();
    restart_adaptive = IsRunning();
  }

  if (policy.has_adaptive_charging_enabled() &&
      policy.adaptive_charging_enabled() != adaptive_charging_enabled_) {
    if (adaptive_charging_supported_) {
      adaptive_charging_enabled_ = policy.adaptive_charging_enabled();
      restart_adaptive = true;
      if (!adaptive_charging_enabled_)
        state_ = AdaptiveChargingState::USER_DISABLED;
    } else {
      LOG(ERROR) << "Policy Change attempted to enable Adaptive Charging "
                 << "without platform support.";
    }
  }

  if (!restart_adaptive)
    return;

  // Stop adaptive charging, then restart it with the new values.
  StopAdaptiveCharging();
  StartAdaptiveCharging(UserChargingEvent::Event::PERIODIC_LOG);
}

void AdaptiveChargingController::PrepareForSuspendAttempt() {
  // Set the charge policy synchronously to make sure this completes before
  // suspend.
  UpdateAdaptiveCharging(UserChargingEvent::Event::SUSPEND, false /* async */);
}

void AdaptiveChargingController::HandleShutdown() {
  adaptive_charging_enabled_ = false;
  StopAdaptiveCharging();
}

void AdaptiveChargingController::OnPredictionResponse(
    bool inference_done, const std::vector<double>& result) {
  if (!inference_done) {
    LOG(ERROR) << "Adaptive Charging ML Proxy failed to finish inference";
    StopAdaptiveCharging();
    return;
  }

  // This goes through the predictions, which are a value between (0.0, 1.0),
  // which indicate the probability of being unplugged at a certain hour.
  int hour = 0;
  for (int i = 1; i < result.size(); ++i) {
    // In the case of 2 probabilities being the max values, bias towards the
    // earlier probability.
    if (result[i] > result[hour])
      hour = i;
  }

  // If the max probability is less than `min_probability_` we treat that as the
  // model not having enough confidence in the prediction to delay charging.
  if (result[hour] < min_probability_) {
    StopAdaptiveCharging();
    target_full_charge_time_ = base::TimeTicks::Now();
    return;
  }

  // If the prediction isn't confident that the AC charger will remain plugged
  // in for the time left to finish charging, stop delaying and start charging.
  base::TimeDelta target_delay = base::Hours(hour);
  if (target_delay <= kFinishChargingDelay) {
    StopAdaptiveCharging();
    target_full_charge_time_ = base::TimeTicks::Now() + target_delay;
    return;
  }

  // Only continue running the `recheck_alarm_` if we plan to continue delaying
  // charge. The `recheck_alarm_` causes this code to be run again.
  StartRecheckAlarm(recheck_alarm_interval_);

  base::TimeTicks target_time = base::TimeTicks::Now() + target_delay;
  const system::PowerStatus status = power_supply_->GetPowerStatus();

  // If the last value in `result` was the largest probability and greater than
  // `min_probability_`, we don't set the `charge_alarm_` yet. It will be set
  // when this is no longer the case when this function is run again via the
  // `recheck_alarm_` or a suspend attempt.
  if (hour != (result.size() - 1)) {
    // Don't allow the time to start charging, which is
    // `target_full_charge_time_` - `kFinishChargingDelay`, to be pushed out as
    // long as `status.display_battery_percentage` is in the hold range or
    // above. This will happen when the prediction via `result` is different
    // from the last time this code ran. We do this because the prediction for
    // when charging will finish (with the delay time accounted for) is shown to
    // the user when the hold range is reached, and we don't want to subvert
    // their expectations.
    if (charge_alarm_->IsRunning() &&
        AtHoldPercent(status.display_battery_percentage) &&
        target_time >= target_full_charge_time_)
      return;

    StartChargeAlarm(target_delay - kFinishChargingDelay);
  } else {
    // Set the `target_full_charge_time_` to the Max() value, since we haven't
    // found a time that we'll start charging yet.
    target_full_charge_time_ = base::TimeTicks::Max();
  }

  // We still run the above code when Adaptive Charging isn't enabled to collect
  // metrics on how well the predictions perform.
  // TODO(b/222620437): If the Battery Sustainer was already set, don't set it
  // again as a workaround until all firmwares are updated.
  if (!adaptive_charging_enabled_ || is_sustain_set_)
    return;

  // Set the upper limit of battery sustain to the current charge if it's higher
  // than `hold_percent_`. The battery sustain feature will maintain a display
  // battery percentage range of (`sustain_percent` - `hold_delta_percent_`,
  // `sustain_percent`).
  int64_t sustain_percent = std::max(
      hold_percent_, static_cast<int64_t>(status.display_battery_percentage));
  if (!SetSustain(sustain_percent - hold_delta_percent_, sustain_percent)) {
    StopAdaptiveCharging();
    LOG(ERROR) << "Battery Sustain command failed. Stopping Adaptive Charging";
  }
  is_sustain_set_ = true;
  display_percent_ = sustain_percent;
}

void AdaptiveChargingController::OnPredictionFail(brillo::Error* error) {
  StopAdaptiveCharging();
  LOG(ERROR) << "Adaptive Charging ML Proxy failed call to "
             << "RequestAdaptiveChargingDecisionAsync with error: "
             << error->GetMessage();
}

void AdaptiveChargingController::OnPowerStatusUpdate() {
  const system::PowerStatus status = power_supply_->GetPowerStatus();
  PowerSupplyProperties::ExternalPower last_external_power =
      cached_external_power_;
  cached_external_power_ = status.external_power;

  if (status.external_power != last_external_power) {
    if (status.external_power == PowerSupplyProperties_ExternalPower_AC) {
      StartAdaptiveCharging(UserChargingEvent::Event::CHARGER_PLUGGED_IN);
    } else if (last_external_power == PowerSupplyProperties_ExternalPower_AC) {
      StopAdaptiveCharging();

      // Only generate metrics if Adaptive Charging started, and we're above
      // hold_percent_.
      if (started_ && AtHoldPercent(status.display_battery_percentage) &&
          status.external_power ==
              PowerSupplyProperties_ExternalPower_DISCONNECTED) {
        delegate_->GenerateAdaptiveChargingUnplugMetrics(
            state_, target_full_charge_time_, hold_percent_start_time_,
            hold_percent_end_time_, charge_finished_time_,
            status.display_battery_percentage);
      }

      // Clear timestamps after reporting metrics.
      target_full_charge_time_ = base::TimeTicks();
      hold_percent_start_time_ = base::TimeTicks();
      hold_percent_end_time_ = base::TimeTicks();
      charge_finished_time_ = base::TimeTicks();
      return;
    }
  }

  // Only collect information for metrics, etc. if plugged into a full powered
  // charge (denoted as PowerSupplyProperties_ExternalPower_AC) since that's the
  // only time that Adaptive Charging will be active.
  if (!started_ ||
      status.external_power != PowerSupplyProperties_ExternalPower_AC)
    return;

  if (AtHoldPercent(status.display_battery_percentage)) {
    if (adaptive_charging_enabled_ && is_sustain_set_) {
      power_supply_->SetAdaptiveCharging(target_full_charge_time_,
                                         display_percent_);
    }

    // Since we report metrics on how well the ML model does even if Adaptive
    // Charging isn't enabled, we still record this timestamp.
    if (hold_percent_start_time_ == base::TimeTicks())
      hold_percent_start_time_ = base::TimeTicks::Now();
  }

  if (status.battery_state == PowerSupplyProperties_BatteryState_FULL &&
      charge_finished_time_ == base::TimeTicks() && report_charge_time_)
    charge_finished_time_ = base::TimeTicks::Now();
}

void AdaptiveChargingController::HandleChargeNow(
    dbus::MethodCall* method_call,
    dbus::ExportedObject::ResponseSender response_sender) {
  if (state_ == AdaptiveChargingState::ACTIVE)
    state_ = AdaptiveChargingState::USER_CANCELED;

  StopAdaptiveCharging();
  power_supply_->RefreshImmediately();
  std::move(response_sender).Run(dbus::Response::FromMethodCall(method_call));
}

bool AdaptiveChargingController::SetSustain(int64_t lower, int64_t upper) {
  bool success = delegate_->SetBatterySustain(lower, upper);
  if (!success) {
    LOG(ERROR) << "Failed to set battery sustain values: " << lower << ", "
               << upper;
  }

  return success;
}

bool AdaptiveChargingController::StartAdaptiveCharging(
    const UserChargingEvent::Event::Reason& reason) {
  const system::PowerStatus status = power_supply_->GetPowerStatus();
  if (status.battery_state == PowerSupplyProperties_BatteryState_FULL) {
    started_ = false;
    return false;
  }

  started_ = true;
  report_charge_time_ = status.display_battery_percentage <= hold_percent_;
  if (adaptive_charging_enabled_)
    state_ = AdaptiveChargingState::ACTIVE;

  UpdateAdaptiveCharging(reason, true /* async */);
  return true;
}

void AdaptiveChargingController::UpdateAdaptiveCharging(
    const UserChargingEvent::Event::Reason& reason, bool async) {
  assist_ranker::RankerExample proto;

  // The features we need to set are:
  // TimeOfTheDay: int32, minutes that have passed for today.
  // DayOfWeek: int32, weekday (Sunday = 0, ...)
  // DayOfMonth: int32, day of the month
  // DeviceMode: int32, enum for device mode (eg TABLET_MODE)
  // BatteryPercentage: int32, display battery percentage (10% = 10)
  // IsCharging: int32, whether the AC charger is connected
  // ScreenBrightnessPercent: int32, display brightness percent
  // Reason: int32, enum for why we're running the model
  //
  // For more details (such as enum definitions), see
  // platform2/system_api/dbus/power_manager/user_charging_event.proto
  auto& features = *proto.mutable_features();

  const base::Time now = base::Time::Now();
  int minutes;
  base::Time::Exploded now_exploded;
  now.LocalExplode(&now_exploded);
  minutes = 60 * now_exploded.hour + now_exploded.minute;
  minutes -= minutes % kAdaptiveChargingTimeBucketMin;
  features["TimeOfTheDay"].set_int32_value(minutes);
  features["DayOfWeek"].set_int32_value(now_exploded.day_of_week);
  features["DayOfMonth"].set_int32_value(now_exploded.day_of_month);

  const LidState lid_state = input_watcher_->QueryLidState();
  int mode;
  if (lid_state == LidState::CLOSED)
    mode = UserChargingEvent::Features::CLOSED_LID_MODE;
  else if (input_watcher_->GetTabletMode() == TabletMode::ON)
    mode = UserChargingEvent::Features::TABLET_MODE;
  else if (lid_state == LidState::OPEN)
    mode = UserChargingEvent::Features::LAPTOP_MODE;
  else
    mode = UserChargingEvent::Features::UNKNOWN_MODE;

  features["DeviceMode"].set_int32_value(static_cast<int32_t>(mode));

  const system::PowerStatus status = power_supply_->GetPowerStatus();
  features["BatteryPercentage"].set_int32_value(
      static_cast<int32_t>(status.battery_percentage));
  features["IsCharging"].set_int32_value(
      status.external_power == PowerSupplyProperties_ExternalPower_AC ? 1 : 0);

  double screen_brightness;
  if (backlight_controller_ &&
      backlight_controller_->GetBrightnessPercent(&screen_brightness))
    features["ScreenBrightnessPercent"].set_int32_value(
        static_cast<int32_t>(screen_brightness));
  else
    features["ScreenBrightnessPercent"].set_int32_value(0);

  features["Reason"].set_int32_value(static_cast<int32_t>(reason));

  // This will call back into AdaptiveChargingController: when the DBus call to
  // the Adaptive Charging ml-service completes. Blocks if async is false.
  delegate_->GetAdaptiveChargingPrediction(proto, async);
}

void AdaptiveChargingController::StopAdaptiveCharging() {
  if (state_ == AdaptiveChargingState::ACTIVE) {
    state_ = AdaptiveChargingState::INACTIVE;
    hold_percent_end_time_ = base::TimeTicks::Now();
  }

  recheck_alarm_->Stop();
  charge_alarm_->Stop();
  SetSustain(kBatterySustainDisabled, kBatterySustainDisabled);
  is_sustain_set_ = false;
  power_supply_->ClearAdaptiveCharging();
}

bool AdaptiveChargingController::IsRunning() {
  return recheck_alarm_->IsRunning();
}

bool AdaptiveChargingController::AtHoldPercent(double display_battery_percent) {
  // We need to subtract 1 from this since the EC will start charging when the
  // battery percentage drops below `hold_percent_` - `hold_delta_percent_`.
  // This means that the battery charge can momentarily drop below the lower
  // end of the range we specified.
  return display_battery_percent >= hold_percent_ - hold_delta_percent_ - 1;
}

void AdaptiveChargingController::StartRecheckAlarm(base::TimeDelta delay) {
  recheck_alarm_->Start(
      FROM_HERE, delay,
      base::BindRepeating(&AdaptiveChargingController::OnRecheckAlarmFired,
                          base::Unretained(this)));
}

void AdaptiveChargingController::StartChargeAlarm(base::TimeDelta delay) {
  charge_alarm_->Start(
      FROM_HERE, delay,
      base::BindRepeating(&AdaptiveChargingController::StopAdaptiveCharging,
                          base::Unretained(this)));
  target_full_charge_time_ =
      base::TimeTicks::Now() + delay + kFinishChargingDelay;
}

void AdaptiveChargingController::OnRecheckAlarmFired() {
  UpdateAdaptiveCharging(UserChargingEvent::Event::PERIODIC_LOG,
                         true /* async */);
}

}  // namespace policy
}  // namespace power_manager
