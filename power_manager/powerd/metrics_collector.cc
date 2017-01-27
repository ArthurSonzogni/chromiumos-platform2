// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/metrics_collector.h"

#include <stdint.h>

#include <algorithm>
#include <cmath>

#include <base/logging.h>

#include "power_manager/common/metrics_constants.h"
#include "power_manager/common/metrics_sender.h"
#include "power_manager/common/prefs.h"
#include "power_manager/common/util.h"
#include "power_manager/powerd/policy/backlight_controller.h"

namespace power_manager {

using system::PowerStatus;

namespace metrics {
namespace {

// Generates the histogram name under which dark resume wake duration metrics
// are logged for the dark resume triggered by |wake_reason|.
std::string WakeReasonToHistogramName(const std::string& wake_reason) {
  return std::string("Power.DarkResumeWakeDurationMs.").append(wake_reason);
}

// Returns true if port |index| exists in |status| and has a connected dedicated
// source or dual-role device.
bool ChargingPortConnected(const PowerStatus& status, size_t index) {
  if (index >= status.ports.size())
    return false;

  const PowerStatus::Port::Connection conn = status.ports[index].connection;
  return conn == PowerStatus::Port::Connection::DEDICATED_SOURCE ||
         conn == PowerStatus::Port::Connection::DUAL_ROLE;
}

// Returns a value describing which power ports are connected.
ConnectedChargingPorts GetConnectedChargingPorts(const PowerStatus& status) {
  // TODO(derat): Add more values if we ship systems with more than two ports.
  if (status.ports.size() > 2u)
    return ConnectedChargingPorts::TOO_MANY_PORTS;

  const bool port1_connected = ChargingPortConnected(status, 0);
  const bool port2_connected = ChargingPortConnected(status, 1);
  if (port1_connected && port2_connected)
    return ConnectedChargingPorts::PORT1_PORT2;
  else if (port1_connected)
    return ConnectedChargingPorts::PORT1;
  else if (port2_connected)
    return ConnectedChargingPorts::PORT2;
  else
    return ConnectedChargingPorts::NONE;
}

}  // namespace

// static
std::string MetricsCollector::AppendPowerSourceToEnumName(
    const std::string& enum_name, PowerSource power_source) {
  return enum_name +
         (power_source == PowerSource::AC ? kAcSuffix : kBatterySuffix);
}

MetricsCollector::MetricsCollector() = default;

MetricsCollector::~MetricsCollector() = default;

void MetricsCollector::Init(
    PrefsInterface* prefs,
    policy::BacklightController* display_backlight_controller,
    policy::BacklightController* keyboard_backlight_controller,
    const PowerStatus& power_status) {
  prefs_ = prefs;
  display_backlight_controller_ = display_backlight_controller;
  keyboard_backlight_controller_ = keyboard_backlight_controller;
  last_power_status_ = power_status;

  if (display_backlight_controller_ || keyboard_backlight_controller_) {
    generate_backlight_metrics_timer_.Start(
        FROM_HERE,
        base::TimeDelta::FromMilliseconds(kBacklightLevelIntervalMs),
        this,
        &MetricsCollector::GenerateBacklightLevelMetrics);
  }
}

void MetricsCollector::HandleScreenDimmedChange(
    bool dimmed, base::TimeTicks last_user_activity_time) {
  if (dimmed) {
    base::TimeTicks now = clock_.GetCurrentTime();
    screen_dim_timestamp_ = now;
    last_idle_event_timestamp_ = now;
    last_idle_timedelta_ = now - last_user_activity_time;
  } else {
    screen_dim_timestamp_ = base::TimeTicks();
  }
}

void MetricsCollector::HandleScreenOffChange(
    bool off, base::TimeTicks last_user_activity_time) {
  if (off) {
    base::TimeTicks now = clock_.GetCurrentTime();
    screen_off_timestamp_ = now;
    last_idle_event_timestamp_ = now;
    last_idle_timedelta_ = now - last_user_activity_time;
  } else {
    screen_off_timestamp_ = base::TimeTicks();
  }
}

void MetricsCollector::HandleSessionStateChange(SessionState state) {
  if (state == session_state_)
    return;

  session_state_ = state;

  switch (state) {
    case SessionState::STARTED:
      session_start_time_ = clock_.GetCurrentTime();
      if (!last_power_status_.line_power_on)
        IncrementNumOfSessionsPerChargeMetric();
      if (last_power_status_.battery_is_present) {
        // Enum to avoid exponential histogram's varyingly-sized buckets.
        SendEnumMetricWithPowerSource(
            kBatteryRemainingAtStartOfSessionName,
            static_cast<int>(round(last_power_status_.battery_percentage)),
            kMaxPercent);
      }
      break;
    case SessionState::STOPPED: {
      if (last_power_status_.battery_is_present) {
        // Enum to avoid exponential histogram's varyingly-sized buckets.
        SendEnumMetricWithPowerSource(
            kBatteryRemainingAtEndOfSessionName,
            static_cast<int>(round(last_power_status_.battery_percentage)),
            kMaxPercent);
      }

      SendMetric(kLengthOfSessionName,
                 (clock_.GetCurrentTime() - session_start_time_).InSeconds(),
                 kLengthOfSessionMin,
                 kLengthOfSessionMax,
                 kDefaultBuckets);

      if (display_backlight_controller_) {
        SendMetric(kNumberOfAlsAdjustmentsPerSessionName,
                   display_backlight_controller_
                       ->GetNumAmbientLightSensorAdjustments(),
                   kNumberOfAlsAdjustmentsPerSessionMin,
                   kNumberOfAlsAdjustmentsPerSessionMax,
                   kDefaultBuckets);
        SendMetricWithPowerSource(
            kUserBrightnessAdjustmentsPerSessionName,
            display_backlight_controller_->GetNumUserAdjustments(),
            kUserBrightnessAdjustmentsPerSessionMin,
            kUserBrightnessAdjustmentsPerSessionMax,
            kDefaultBuckets);
      }
      break;
    }
  }
}

void MetricsCollector::HandlePowerStatusUpdate(const PowerStatus& status) {
  const bool previously_on_line_power = last_power_status_.line_power_on;
  const bool previously_using_unknown_type =
      previously_on_line_power &&
      system::GetPowerSupplyTypeMetric(last_power_status_.line_power_type) ==
          PowerSupplyType::OTHER;

  last_power_status_ = status;

  // Charge stats.
  if (status.line_power_on && !previously_on_line_power) {
    GenerateNumOfSessionsPerChargeMetric();
    if (status.battery_is_present) {
      // Enum to avoid exponential histogram's varyingly-sized buckets.
      SendEnumMetric(kBatteryRemainingWhenChargeStartsName,
                     static_cast<int>(round(status.battery_percentage)),
                     kMaxPercent);
      SendEnumMetric(kBatteryChargeHealthName,
                     static_cast<int>(round(100.0 * status.battery_charge_full /
                                            status.battery_charge_full_design)),
                     kBatteryChargeHealthMax);
    }
  } else if (!status.line_power_on && previously_on_line_power) {
    if (session_state_ == SessionState::STARTED)
      IncrementNumOfSessionsPerChargeMetric();
  }

  // Power supply details.
  if (status.line_power_on) {
    const PowerSupplyType type =
        system::GetPowerSupplyTypeMetric(status.line_power_type);
    if (type == PowerSupplyType::OTHER && !previously_using_unknown_type)
      LOG(WARNING) << "Unknown power supply type " << status.line_power_type;
    SendEnumMetric(kPowerSupplyTypeName,
                   static_cast<int>(type),
                   static_cast<int>(PowerSupplyType::MAX));

    // Sent as enums to avoid exponential histogram's exponentially-sized
    // buckets.
    SendEnumMetric(kPowerSupplyMaxVoltageName,
                   static_cast<int>(round(status.line_power_max_voltage)),
                   kPowerSupplyMaxVoltageMax);
    SendEnumMetric(kPowerSupplyMaxPowerName,
                   static_cast<int>(round(status.line_power_max_voltage *
                                          status.line_power_max_current)),
                   kPowerSupplyMaxPowerMax);
  }

  SendEnumMetric(kConnectedChargingPortsName,
                 static_cast<int>(GetConnectedChargingPorts(status)),
                 static_cast<int>(ConnectedChargingPorts::MAX));

  GenerateBatteryDischargeRateMetric();
  GenerateBatteryDischargeRateWhileSuspendedMetric();

  SendEnumMetric(kBatteryInfoSampleName,
                 static_cast<int>(BatteryInfoSampleResult::READ),
                 static_cast<int>(BatteryInfoSampleResult::MAX));
  // TODO(derat): Continue sending BAD in some situations? Remove this metric
  // entirely?
  SendEnumMetric(kBatteryInfoSampleName,
                 static_cast<int>(BatteryInfoSampleResult::GOOD),
                 static_cast<int>(BatteryInfoSampleResult::MAX));
}

void MetricsCollector::HandleShutdown(ShutdownReason reason) {
  SendEnumMetric(kShutdownReasonName,
                 static_cast<int>(reason),
                 static_cast<int>(kShutdownReasonMax));
}

void MetricsCollector::PrepareForSuspend() {
  battery_energy_before_suspend_ = last_power_status_.battery_energy;
  on_line_power_before_suspend_ = last_power_status_.line_power_on;
  time_before_suspend_ = clock_.GetCurrentWallTime();
}

void MetricsCollector::HandleResume(int num_suspend_attempts) {
  SendMetric(kSuspendAttemptsBeforeSuccessName,
             num_suspend_attempts,
             kSuspendAttemptsMin,
             kSuspendAttemptsMax,
             kSuspendAttemptsBuckets);
  // Report the discharge rate in response to the next
  // OnPowerStatusUpdate() call.
  report_battery_discharge_rate_while_suspended_ = true;
}

void MetricsCollector::HandleCanceledSuspendRequest(int num_suspend_attempts) {
  SendMetric(kSuspendAttemptsBeforeCancelName,
             num_suspend_attempts,
             kSuspendAttemptsMin,
             kSuspendAttemptsMax,
             kSuspendAttemptsBuckets);
}

void MetricsCollector::GenerateDarkResumeMetrics(
    const std::vector<policy::Suspender::DarkResumeInfo>& wake_durations,
    base::TimeDelta suspend_duration) {
  if (suspend_duration.InSeconds() <= 0)
    return;

  // We want to get metrics even if the system suspended for less than an hour
  // so we scale the number of wakes up.
  static const int kSecondsPerHour = 60 * 60;
  const int64_t wakeups_per_hour =
      wake_durations.size() * kSecondsPerHour / suspend_duration.InSeconds();
  SendMetric(kDarkResumeWakeupsPerHourName,
             wakeups_per_hour,
             kDarkResumeWakeupsPerHourMin,
             kDarkResumeWakeupsPerHourMax,
             kDefaultBuckets);

  for (const auto& pair : wake_durations) {
    // Send aggregated dark resume duration metric.
    SendMetric(kDarkResumeWakeDurationMsName,
               pair.second.InMilliseconds(),
               kDarkResumeWakeDurationMsMin,
               kDarkResumeWakeDurationMsMax,
               kDefaultBuckets);
    // Send wake reason-specific dark resume duration metric.
    SendMetric(WakeReasonToHistogramName(pair.first),
               pair.second.InMilliseconds(),
               kDarkResumeWakeDurationMsMin,
               kDarkResumeWakeDurationMsMax,
               kDefaultBuckets);
  }
}

void MetricsCollector::GenerateUserActivityMetrics() {
  if (last_idle_event_timestamp_.is_null())
    return;

  base::TimeTicks current_time = clock_.GetCurrentTime();
  base::TimeDelta event_delta = current_time - last_idle_event_timestamp_;
  base::TimeDelta total_delta = event_delta + last_idle_timedelta_;
  last_idle_event_timestamp_ = base::TimeTicks();

  SendMetricWithPowerSource(kIdleName,
                            total_delta.InMilliseconds(),
                            kIdleMin,
                            kIdleMax,
                            kDefaultBuckets);

  if (!screen_dim_timestamp_.is_null()) {
    base::TimeDelta dim_event_delta = current_time - screen_dim_timestamp_;
    SendMetricWithPowerSource(kIdleAfterDimName,
                              dim_event_delta.InMilliseconds(),
                              kIdleAfterDimMin,
                              kIdleAfterDimMax,
                              kDefaultBuckets);
    screen_dim_timestamp_ = base::TimeTicks();
  }
  if (!screen_off_timestamp_.is_null()) {
    base::TimeDelta screen_off_event_delta =
        current_time - screen_off_timestamp_;
    SendMetricWithPowerSource(kIdleAfterScreenOffName,
                              screen_off_event_delta.InMilliseconds(),
                              kIdleAfterScreenOffMin,
                              kIdleAfterScreenOffMax,
                              kDefaultBuckets);
    screen_off_timestamp_ = base::TimeTicks();
  }
}

void MetricsCollector::GenerateBacklightLevelMetrics() {
  if (!screen_dim_timestamp_.is_null() || !screen_off_timestamp_.is_null())
    return;

  double percent = 0.0;
  if (display_backlight_controller_ &&
      display_backlight_controller_->GetBrightnessPercent(&percent)) {
    // Enum to avoid exponential histogram's varyingly-sized buckets.
    SendEnumMetricWithPowerSource(
        kBacklightLevelName, lround(percent), kMaxPercent);
  }
  if (keyboard_backlight_controller_ &&
      keyboard_backlight_controller_->GetBrightnessPercent(&percent)) {
    // Enum to avoid exponential histogram's varyingly-sized buckets.
    SendEnumMetric(kKeyboardBacklightLevelName, lround(percent), kMaxPercent);
  }
}

void MetricsCollector::HandlePowerButtonEvent(ButtonState state) {
  switch (state) {
    case ButtonState::DOWN:
      // Just keep track of the time when the button was pressed.
      if (!last_power_button_down_timestamp_.is_null()) {
        LOG(ERROR) << "Got power-button-down event while button was already "
                   << "down";
      }
      last_power_button_down_timestamp_ = clock_.GetCurrentTime();
      break;
    case ButtonState::UP: {
      // Metrics are sent after the button is released.
      if (last_power_button_down_timestamp_.is_null()) {
        LOG(ERROR) << "Got power-button-up event while button was already up";
      } else {
        base::TimeDelta delta =
            clock_.GetCurrentTime() - last_power_button_down_timestamp_;
        last_power_button_down_timestamp_ = base::TimeTicks();
        SendMetric(kPowerButtonDownTimeName,
                   delta.InMilliseconds(),
                   kPowerButtonDownTimeMin,
                   kPowerButtonDownTimeMax,
                   kDefaultBuckets);
      }
      break;
    }
    case ButtonState::REPEAT:
      // Ignore repeat events if we get them.
      break;
  }
}

void MetricsCollector::SendPowerButtonAcknowledgmentDelayMetric(
    base::TimeDelta delay) {
  SendMetric(kPowerButtonAcknowledgmentDelayName,
             delay.InMilliseconds(),
             kPowerButtonAcknowledgmentDelayMin,
             kPowerButtonAcknowledgmentDelayMax,
             kDefaultBuckets);
}

bool MetricsCollector::SendMetricWithPowerSource(
    const std::string& name, int sample, int min, int max, int num_buckets) {
  const std::string full_name = AppendPowerSourceToEnumName(
      name,
      last_power_status_.line_power_on ? PowerSource::AC
                                       : PowerSource::BATTERY);
  return SendMetric(full_name, sample, min, max, num_buckets);
}

bool MetricsCollector::SendEnumMetricWithPowerSource(const std::string& name,
                                                     int sample,
                                                     int max) {
  const std::string full_name = AppendPowerSourceToEnumName(
      name,
      last_power_status_.line_power_on ? PowerSource::AC
                                       : PowerSource::BATTERY);
  return SendEnumMetric(full_name, sample, max);
}

void MetricsCollector::GenerateBatteryDischargeRateMetric() {
  // The battery discharge rate metric is relevant and collected only
  // when running on battery.
  if (!last_power_status_.battery_is_present ||
      last_power_status_.line_power_on)
    return;

  // Converts the discharge rate from W to mW.
  int rate =
      static_cast<int>(round(last_power_status_.battery_energy_rate * 1000));
  if (rate <= 0)
    return;

  // Ensures that the metric is not generated too frequently.
  if (!last_battery_discharge_rate_metric_timestamp_.is_null() &&
      (clock_.GetCurrentTime() - last_battery_discharge_rate_metric_timestamp_)
              .InSeconds() < kBatteryDischargeRateIntervalSec) {
    return;
  }

  if (SendMetric(kBatteryDischargeRateName,
                 rate,
                 kBatteryDischargeRateMin,
                 kBatteryDischargeRateMax,
                 kDefaultBuckets))
    last_battery_discharge_rate_metric_timestamp_ = clock_.GetCurrentTime();
}

void MetricsCollector::GenerateBatteryDischargeRateWhileSuspendedMetric() {
  // Do nothing unless this is the first time we're called after resuming.
  if (!report_battery_discharge_rate_while_suspended_)
    return;
  report_battery_discharge_rate_while_suspended_ = false;

  if (!last_power_status_.battery_is_present || on_line_power_before_suspend_ ||
      last_power_status_.line_power_on)
    return;

  base::TimeDelta elapsed_time =
      clock_.GetCurrentWallTime() - time_before_suspend_;
  if (elapsed_time.InSeconds() <
      kBatteryDischargeRateWhileSuspendedMinSuspendSec)
    return;

  double discharged_watt_hours =
      battery_energy_before_suspend_ - last_power_status_.battery_energy;
  double discharge_rate_watts =
      discharged_watt_hours / (elapsed_time.InSecondsF() / 3600);

  // Maybe the charger was connected while the system was suspended but
  // disconnected before it resumed.
  if (discharge_rate_watts < 0.0)
    return;

  SendMetric(kBatteryDischargeRateWhileSuspendedName,
             static_cast<int>(round(discharge_rate_watts * 1000)),
             kBatteryDischargeRateWhileSuspendedMin,
             kBatteryDischargeRateWhileSuspendedMax,
             kDefaultBuckets);
}

void MetricsCollector::IncrementNumOfSessionsPerChargeMetric() {
  int64_t num = 0;
  prefs_->GetInt64(kNumSessionsOnCurrentChargePref, &num);
  num = std::max(num, static_cast<int64_t>(0));
  prefs_->SetInt64(kNumSessionsOnCurrentChargePref, num + 1);
}

void MetricsCollector::GenerateNumOfSessionsPerChargeMetric() {
  int64_t sample = 0;
  prefs_->GetInt64(kNumSessionsOnCurrentChargePref, &sample);
  if (sample <= 0)
    return;

  sample = std::min(sample, static_cast<int64_t>(kNumOfSessionsPerChargeMax));
  prefs_->SetInt64(kNumSessionsOnCurrentChargePref, 0);
  SendMetric(kNumOfSessionsPerChargeName,
             sample,
             kNumOfSessionsPerChargeMin,
             kNumOfSessionsPerChargeMax,
             kDefaultBuckets);
}

}  // namespace metrics
}  // namespace power_manager
