// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include <base/files/file_enumerator.h>
#include <base/json/json_file_value_serializer.h>
#include <base/json/json_string_value_serializer.h>
#include <base/json/values_util.h>
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
const char kDefaultChargeHistoryDir[] =
    "/var/lib/power_manager/charge_history/";
const char kChargeEventsSubDir[] = "charge_events/";
const char kTimeFullOnACSubDir[] = "time_full_on_ac/";
const char kTimeOnACSubDir[] = "time_on_ac/";
// `kRententionDays`, `kChargeHistoryTimeBucketSize`, and `kMaxChargeEvents`
// require a privacy review to be changed.
const base::TimeDelta kRetentionDays = base::Days(30);
const base::TimeDelta kChargeHistoryTimeInterval = base::Minutes(15);
const int kMaxChargeEvents = 50;

// As a heuristic to improve the accuracy of Adaptive Charging, we require that
// there be 14 days tracked in ChargeHistory and that 50% of the time on AC has
// a full charge.
const int kHeuristicMinDaysHistory = 14;
const double kHeuristicMinFullOnACRatio = 0.5;

const int64_t kBatterySustainDisabled = -1;
const base::TimeDelta kDefaultAlarmInterval = base::Minutes(30);
const int64_t kDefaultHoldPercent = 80;
const double kDefaultMinProbability = 0.2;
const int kAdaptiveChargingTimeBucketMin = 15;

}  // namespace

ChargeHistory::ChargeHistory()
    : charge_history_dir_(kDefaultChargeHistoryDir), weak_ptr_factory_(this) {}

void ChargeHistory::Init(const system::PowerStatus& status) {
  ac_connect_time_ = base::Time();
  CHECK(base::CreateDirectory(charge_history_dir_));
  // Limit reading these files to just powerd and root.
  CHECK(base::SetPosixFilePermissions(charge_history_dir_, 0700));

  charge_events_dir_ = charge_history_dir_.Append(kChargeEventsSubDir);
  time_full_on_ac_dir_ = charge_history_dir_.Append(kTimeFullOnACSubDir);
  time_on_ac_dir_ = charge_history_dir_.Append(kTimeOnACSubDir);
  CHECK(base::CreateDirectory(charge_events_dir_));
  CHECK(base::CreateDirectory(time_full_on_ac_dir_));
  CHECK(base::CreateDirectory(time_on_ac_dir_));

  base::Time now = base::Time::Now();
  base::FileEnumerator events_dir(charge_events_dir_, false,
                                  base::FileEnumerator::FILES);
  for (base::FilePath path = events_dir.Next(); !path.empty();
       path = events_dir.Next()) {
    base::Time file_time;
    base::TimeDelta duration;
    if (!JSONFileNameToTime(path, &file_time)) {
      CHECK(base::DeleteFile(path));
    } else if (events_dir.GetInfo().GetSize() == 0) {
      // There should only be up to one empty charge event. If there's more than
      // one, only keep the newest one.
      if (ac_connect_time_ != base::Time()) {
        if (file_time > ac_connect_time_) {
          DeleteChargeFile(charge_events_dir_, ac_connect_time_);
          ac_connect_time_ = file_time;
        } else {
          CHECK(base::DeleteFile(path));
        }
      } else if (status.external_power ==
                 PowerSupplyProperties_ExternalPower_AC) {
        ac_connect_time_ = file_time;
      }
    } else if (!ReadTimeDeltaFromFile(path, &duration)) {
      CHECK(base::DeleteFile(path));
    } else if (file_time + duration < now - kRetentionDays) {
      CHECK(base::DeleteFile(path));
    } else {
      charge_events_[file_time] = duration;
    }
  }

  ReadChargeDaysFromFiles(time_full_on_ac_dir_, &time_full_on_ac_days_,
                          &duration_full_on_ac_);
  ReadChargeDaysFromFiles(time_on_ac_dir_, &time_on_ac_days_, &duration_on_ac_);

  // There are three cases to handle when creating a best guess for the
  // `full_charge_time_` (these values are for a heuristic):
  // - There isn't a charge event without a duration (plug in happened during a
  // prior instance of ChargeHistory), we go with now as `full_charge_time_`.
  // - The latest write to time_full_on_ac/ is not after the start of charge, or
  // there isn't a latest write to time_full_on_ac/. We treat the entire time
  // since starting charge as full, hence `full_charge_time_` is set to the
  // start of charge time.
  // - If the latest write to the last time_full_on_ac/ file is after the start
  // of the latest charge event, we go with that time, since we already recorded
  // the time between the start of charge and then.
  if (status.battery_state == PowerSupplyProperties_BatteryState_FULL &&
      status.external_power == PowerSupplyProperties_ExternalPower_AC) {
    base::File::Info full_on_ac_info;
    auto rit = time_full_on_ac_days_.rbegin();
    base::FilePath path;
    if (ac_connect_time_ == base::Time()) {
      full_charge_time_ = FloorTime(base::Time::Now());
    } else if (rit == time_full_on_ac_days_.rend() ||
               !TimeToJSONFileName(rit->first, &path) ||
               !base::GetFileInfo(path, &full_on_ac_info) ||
               full_on_ac_info.last_modified < ac_connect_time_) {
      full_charge_time_ = ac_connect_time_;
    } else {
      full_charge_time_ = FloorTime(full_on_ac_info.last_modified);
    }
  } else {
    full_charge_time_ = base::Time();
  }

  AddZeroDurationChargeDays(time_full_on_ac_dir_, &time_full_on_ac_days_);
  AddZeroDurationChargeDays(time_on_ac_dir_, &time_on_ac_days_);
  UpdateHistory(status);
  RemoveOldChargeEvents();
  retention_timer_.Start(
      FROM_HERE, base::Days(1),
      base::BindRepeating(&ChargeHistory::OnRetentionTimerFired,
                          weak_ptr_factory_.GetWeakPtr()));
  initialized_ = true;
}

void ChargeHistory::set_charge_history_dir_for_testing(
    const base::FilePath& dir) {
  charge_history_dir_ = dir;
}

void ChargeHistory::HandlePowerStatusUpdate(const system::PowerStatus& status) {
  if (!initialized_) {
    Init(status);
    return;
  }

  if (status.external_power == PowerSupplyProperties_ExternalPower_AC &&
      status.battery_state == PowerSupplyProperties_BatteryState_FULL &&
      full_charge_time_ == base::Time())
    full_charge_time_ = FloorTime(base::Time::Now());

  if (status.external_power == cached_external_power_)
    return;

  UpdateHistory(status);
}

base::TimeDelta ChargeHistory::GetTimeOnAC() {
  base::TimeDelta duration_on_ac = duration_on_ac_;

  if (ac_connect_time_ != base::Time() && ac_connect_time_ < base::Time::Now())
    duration_on_ac += base::Time::Now() - ac_connect_time_;

  return duration_on_ac.FloorToMultiple(kChargeHistoryTimeInterval);
}

base::TimeDelta ChargeHistory::GetTimeFullOnAC() {
  base::TimeDelta duration_full_on_ac = duration_full_on_ac_;
  if (full_charge_time_ != base::Time() &&
      full_charge_time_ < base::Time::Now())
    duration_full_on_ac += base::Time::Now() - full_charge_time_;

  return duration_full_on_ac.FloorToMultiple(kChargeHistoryTimeInterval);
}

int ChargeHistory::DaysOfHistory() {
  return time_on_ac_days_.size();
}

void ChargeHistory::OnEnterLowPowerState() {
  // Charge Events and Time on AC don't need to be recorded when entering a low
  // power state, which we may not return from, but Time Full on AC does, since
  // it relies on `full_charge_time_`, a variable stored only in memory.
  if (full_charge_time_ != base::Time()) {
    RecordDurations(time_full_on_ac_dir_, &time_full_on_ac_days_,
                    full_charge_time_, &duration_full_on_ac_);
    // Set `full_charge_time_` to now, so we don't double count if the low
    // power state returns.
    full_charge_time_ = FloorTime(base::Time::Now());
  }

  rewrite_timer_.Stop();
}

void ChargeHistory::OnExitLowPowerState() {
  ScheduleRewrites();
}

void ChargeHistory::UpdateHistory(const system::PowerStatus& status) {
  base::Time now = FloorTime(base::Time::Now());
  cached_external_power_ = status.external_power;

  // When AC is connected, we just create a new charge event with the current
  // time as its file name.
  if (cached_external_power_ == PowerSupplyProperties_ExternalPower_AC) {
    if (ac_connect_time_ != base::Time()) {
      LOG(ERROR) << "Last known state was AC Connected for AC Connect event";
      return;
    }

    ac_connect_time_ = now;

    // This will remove any existing charge event file with the same start time.
    CreateEmptyChargeEventFile(ac_connect_time_);

    // If there's an existing charge event for this timestamp, remove it.
    charge_events_.erase(ac_connect_time_);
    RemoveOldChargeEvents();
    return;
  }

  if (ac_connect_time_ == base::Time()) {
    LOG(ERROR) << "Latest charge event has a duration on AC unplug, which "
               << "means the plug-in event was missed.";
    return;
  }

  // On AC disconnect, write the charging duration to the latest charge event
  // file (the name of which will be the connection time), the time_on_ac files,
  // and the time_full_on_ac files (if we're fully charged).
  if (full_charge_time_ != base::Time())
    RecordDurations(time_full_on_ac_dir_, &time_full_on_ac_days_,
                    full_charge_time_, &duration_full_on_ac_);

  RecordDurations(time_on_ac_dir_, &time_on_ac_days_, ac_connect_time_,
                  &duration_on_ac_);
  full_charge_time_ = base::Time();

  base::TimeDelta duration = now - ac_connect_time_;
  WriteDurationToFile(charge_events_dir_, ac_connect_time_, duration);
  charge_events_[ac_connect_time_] = duration;
  ac_connect_time_ = base::Time();
}

void ChargeHistory::RecordDurations(const base::FilePath& dir,
                                    std::map<base::Time, base::TimeDelta>* days,
                                    const base::Time& start,
                                    base::TimeDelta* total_duration) {
  base::Time now = base::Time::Now();
  // Midnight for today (before start).
  base::Time date = start.UTCMidnight();
  while (date < now) {
    auto it = days->insert(std::make_pair(date, base::TimeDelta())).first;
    base::Time tomorrow = date + base::Days(1);
    base::Time start_for_day = start > it->first ? start : it->first;
    base::Time end_for_day = tomorrow > now ? now : tomorrow;
    base::TimeDelta duration = end_for_day - start_for_day;

    // Subtract the old duration for `date` then add back the updated duration
    // to `total_duration` (the total time for all days tracked in `days`) after
    // flooring the value and making sure it's not over 1 day.
    *total_duration -= it->second;
    it->second += duration;
    it->second = it->second.FloorToMultiple(kChargeHistoryTimeInterval);
    if (it->second > base::Days(1)) {
      LOG(WARNING) << "Time spent on AC: " << it->second << " for day " << date
                   << " was more than 1 day";
      it->second = base::Days(1);
    }

    *total_duration += it->second;
    WriteDurationToFile(dir, date, it->second);
    date += base::Days(1);
  }
}

void ChargeHistory::ReadChargeDaysFromFiles(
    const base::FilePath& dir,
    std::map<base::Time, base::TimeDelta>* days,
    base::TimeDelta* total_duration) {
  base::Time now = base::Time::Now();
  base::FileEnumerator dir_enum(dir, false, base::FileEnumerator::FILES);
  for (base::FilePath path = dir_enum.Next(); !path.empty();
       path = dir_enum.Next()) {
    base::Time file_time;
    base::TimeDelta duration;
    if (!JSONFileNameToTime(path, &file_time)) {
      CHECK(base::DeleteFile(path));
    } else if (!ReadTimeDeltaFromFile(path, &duration)) {
      CHECK(base::DeleteFile(path));
    } else if (file_time < now - kRetentionDays) {
      // Delete files that are older than our retention limit.
      CHECK(base::DeleteFile(path));
    } else {
      days->insert(std::make_pair(file_time, duration));
      *total_duration += duration;
    }
  }
}

void ChargeHistory::AddZeroDurationChargeDays(
    const base::FilePath& dir, std::map<base::Time, base::TimeDelta>* days) {
  auto rit = days->rbegin();
  base::Time todays_date = base::Time::Now().UTCMidnight();
  base::Time date =
      rit == days->rend() ? todays_date : rit->first + base::Days(1);

  for (; date <= todays_date; date += base::Days(1)) {
    days->insert(std::make_pair(date, base::TimeDelta()));
    WriteDurationToFile(dir, date, base::TimeDelta());
  }
}

void ChargeHistory::RemoveOldChargeDays(
    const base::FilePath& dir,
    std::map<base::Time, base::TimeDelta>* days,
    base::TimeDelta* total_duration) {
  for (auto rit = days->rbegin();
       rit != days->rend() && rit->first < base::Time::Now() - kRetentionDays;
       rit++) {
    *total_duration -= rit->second;
    days->erase(rit->first);

    base::FilePath path;
    if (!TimeToJSONFileName(rit->first, &path)) {
      continue;
    }

    CHECK(base::DeleteFile(dir.Append(path)));
  }
}

void ChargeHistory::CreateEmptyChargeEventFile(base::Time start) {
  base::FilePath path;
  if (!TimeToJSONFileName(FloorTime(start), &path))
    return;

  base::File file(charge_events_dir_.Append(path),
                  base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_READ |
                      base::File::FLAG_WRITE);
  DCHECK(file.IsValid());
}

void ChargeHistory::RemoveOldChargeEvents() {
  int max = kMaxChargeEvents;
  if (ac_connect_time_ != base::Time())
    --max;

  while (charge_events_.size() > max) {
    auto it = charge_events_.begin();
    DeleteChargeFile(charge_events_dir_, it->first);
    charge_events_.erase(it);
  }

  if (charge_events_.empty())
    return;

  auto it = charge_events_.begin();
  while (it != charge_events_.end() &&
         it->first + it->second < base::Time::Now() - kRetentionDays) {
    charge_events_.erase(it);
    it = charge_events_.begin();
  }
}

void ChargeHistory::OnRetentionTimerFired() {
  RemoveOldChargeEvents();
  RemoveOldChargeDays(time_full_on_ac_dir_, &time_full_on_ac_days_,
                      &duration_full_on_ac_);
  RemoveOldChargeDays(time_on_ac_dir_, &time_on_ac_days_, &duration_on_ac_);
}

void ChargeHistory::ScheduleRewrites() {
  base::TimeDelta delay = base::Time::Now().ToDeltaSinceWindowsEpoch();
  delay = delay.CeilToMultiple(kChargeHistoryTimeInterval) - delay;
  rewrite_timer_.Start(FROM_HERE, delay,
                       base::BindRepeating(&ChargeHistory::OnRewriteTimerFired,
                                           weak_ptr_factory_.GetWeakPtr()));
}

void ChargeHistory::OnRewriteTimerFired() {
  for (auto it = scheduled_rewrites_.begin(); it != scheduled_rewrites_.end();
       it++) {
    bool success = WriteTimeDeltaToFile(it->first, it->second);
    DCHECK(success);
  }
  scheduled_rewrites_.clear();
}

void ChargeHistory::WriteDurationToFile(const base::FilePath& dir,
                                        base::Time time,
                                        base::TimeDelta duration) {
  if (duration < base::TimeDelta()) {
    LOG(WARNING) << "Negative duration: " << duration.InMilliseconds()
                 << "ms set to be written to directory: " << dir
                 << " for time: " << time << ". Setting to 0";
    duration = base::TimeDelta();
  }

  base::FilePath path;
  if (!TimeToJSONFileName(time, &path)) {
    LOG(ERROR) << "Failed to convert time value: " << time << " to file name";
    return;
  }

  // Write the file now for data retention purposes, but schedule a write later
  // (that will replace the file) at a 15 minute aligned time for privacy
  // reasons.
  path = dir.Append(path);
  bool success = WriteTimeDeltaToFile(path, duration);
  DCHECK(success);
  if (success) {
    scheduled_rewrites_[path] = duration;
    ScheduleRewrites();
  }
}

// static
base::Time ChargeHistory::FloorTime(base::Time time) {
  base::TimeDelta conv = time.ToDeltaSinceWindowsEpoch().FloorToMultiple(
      kChargeHistoryTimeInterval);
  return base::Time::FromDeltaSinceWindowsEpoch(conv);
}

// static
bool ChargeHistory::ReadTimeDeltaFromFile(const base::FilePath& file,
                                          base::TimeDelta* delta) {
  // The TimeDelta values is stored in JSON format.
  JSONFileValueDeserializer deserializer(file);
  int error_code = 0;
  std::string error_msg;
  auto val = deserializer.Deserialize(&error_code, &error_msg);
  if (!val.get()) {
    LOG(ERROR) << "Failed to deserialize TimeDelta from " << file
               << " with error message " << error_msg << " and error code "
               << error_code;
    return false;
  }

  auto opt_delta = base::ValueToTimeDelta(*val);
  if (!opt_delta.has_value()) {
    LOG(ERROR) << "Failed to parse TimeDelta from file contents: " << file;
    return false;
  }

  *delta = opt_delta.value();
  return true;
}

// static
bool ChargeHistory::WriteTimeDeltaToFile(const base::FilePath& path,
                                         base::TimeDelta delta) {
  // Use the string instead of file serializer, since we use
  // ImportantFileWriter functionality to write the file safely.
  std::string json_string;
  JSONStringValueSerializer serializer(&json_string);
  base::Value val =
      base::TimeDeltaToValue(delta.FloorToMultiple(kChargeHistoryTimeInterval));
  if (!serializer.Serialize(val)) {
    LOG(ERROR) << "Failed to serialize TimeDelta: " << delta
               << " to a string. Deleting file: " << path
               << " that it would be written to";
    CHECK(base::DeleteFile(path));
    return false;
  }

  return base::ImportantFileWriter::WriteFileAtomically(path, json_string);
}

// static
bool ChargeHistory::JSONFileNameToTime(const base::FilePath& file,
                                       base::Time* time) {
  base::Value val = base::FilePathToValue(file.BaseName());
  absl::optional<base::Time> opt_time = base::ValueToTime(val);
  if (!opt_time.has_value()) {
    LOG(ERROR) << "Failed to parse timestamp from filename: " << file;
    return false;
  }

  *time = opt_time.value();
  return true;
}

// static
bool ChargeHistory::TimeToJSONFileName(base::Time time, base::FilePath* file) {
  base::Value val = base::TimeToValue(time);
  absl::optional<base::FilePath> opt_file = base::ValueToFilePath(val);
  if (!opt_file.has_value()) {
    LOG(ERROR) << "Failed to create filename from time: " << time;
    return false;
  }

  *file = opt_file.value();
  return true;
}

// static
// We don't schedule deletion of files since this will only update timestamps
// associated with the last modification to the directory. Since this is only
// one timestamp, and it will be overwritten later on as well, there is no
// privacy concern around this.
void ChargeHistory::DeleteChargeFile(const base::FilePath& dir,
                                     base::Time time) {
  base::FilePath path;
  if (!TimeToJSONFileName(FloorTime(time), &path))
    return;

  CHECK(base::DeleteFile(dir.Append(path)));
}

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
  // Make sure we're using the most up-to-date power status. If the system woke
  // from AC disconnect, this will make sure that IsRunning returns false, since
  // `recheck_alarm_` will be stopped. If a system doesn't support wake on AC
  // disconnect, the `recheck_alarm_` will wake the system, and will be
  // similarly stopped here.
  power_supply_->RefreshImmediately();
  charge_history_.OnEnterLowPowerState();

  // Don't run UpdateAdaptiveCharging, which will schedule an RTC wake from
  // sleep, if `recheck_alarm_` isn't already running.
  if (!IsRunning())
    return;

  // Set the charge policy synchronously to make sure this completes before
  // suspend.
  UpdateAdaptiveCharging(UserChargingEvent::Event::SUSPEND, false /* async */);
}

void AdaptiveChargingController::HandleFullResume() {
  charge_history_.OnExitLowPowerState();
}

void AdaptiveChargingController::HandleShutdown() {
  adaptive_charging_enabled_ = false;
  StopAdaptiveCharging();
  charge_history_.OnEnterLowPowerState();
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
  if (state_ != AdaptiveChargingState::ACTIVE || is_sustain_set_)
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
  charge_history_.HandlePowerStatusUpdate(status);

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
    if (state_ == AdaptiveChargingState::ACTIVE && is_sustain_set_) {
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
  if (adaptive_charging_enabled_) {
    base::TimeDelta time_full_on_ac = charge_history_.GetTimeFullOnAC();
    base::TimeDelta time_on_ac = charge_history_.GetTimeOnAC();
    double ratio =
        time_on_ac == base::TimeDelta() ? 0.0 : time_full_on_ac / time_on_ac;
    if (charge_history_.DaysOfHistory() < kHeuristicMinDaysHistory ||
        ratio < kHeuristicMinFullOnACRatio) {
      LOG(INFO) << "Adaptive Charging not started due to heuristic. "
                << charge_history_.DaysOfHistory()
                << " days of charge history and " << ratio
                << " time on AC with full charge over time on AC ratio.";
      state_ = AdaptiveChargingState::HEURISTIC_DISABLED;
      power_supply_->SetAdaptiveChargingHeuristicEnabled(false);
    } else {
      state_ = AdaptiveChargingState::ACTIVE;
      power_supply_->SetAdaptiveChargingHeuristicEnabled(true);
    }
  }

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
