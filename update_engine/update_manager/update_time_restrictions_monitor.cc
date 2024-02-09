// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/update_manager/update_time_restrictions_monitor.h"

#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/time/time.h>

#include "update_engine/common/system_state.h"

using base::TimeDelta;
using brillo::MessageLoop;
using chromeos_update_engine::SystemState;

namespace chromeos_update_manager {

namespace {

const WeeklyTimeInterval* FindNextNearestInterval(
    const WeeklyTimeIntervalVector& intervals, const WeeklyTime& now) {
  const WeeklyTimeInterval* result_interval = nullptr;
  // As we are dealing with weekly time here, the maximum duration can be one
  // week.
  TimeDelta duration_till_next_interval = base::Days(7);
  for (const auto& interval : intervals) {
    if (interval.InRange(now)) {
      return &interval;
    }
    const TimeDelta current_duration = now.GetDurationTo(interval.start());
    if (current_duration < duration_till_next_interval) {
      result_interval = &interval;
      duration_till_next_interval = current_duration;
    }
  }
  return result_interval;
}

WeeklyTime Now() {
  return WeeklyTime::FromTime(SystemState::Get()->clock()->GetWallclockTime());
}

}  // namespace

UpdateTimeRestrictionsMonitor::UpdateTimeRestrictionsMonitor(
    DevicePolicyProvider* device_policy_provider, Delegate* delegate)
    : evaluation_context_(/* evaluation_timeout = */ TimeDelta::Max()),
      device_policy_provider_(device_policy_provider),
      delegate_(delegate),
      weak_ptr_factory_(this) {
  if (device_policy_provider_ != nullptr && delegate_ != nullptr)
    StartMonitoring();
}

UpdateTimeRestrictionsMonitor::~UpdateTimeRestrictionsMonitor() {
  StopMonitoring();
}

void UpdateTimeRestrictionsMonitor::StartMonitoring() {
  DCHECK(device_policy_provider_);
  const WeeklyTimeIntervalVector* new_intervals = evaluation_context_.GetValue(
      device_policy_provider_->var_disallowed_time_intervals());
  if (new_intervals && !new_intervals->empty())
    WaitForRestrictedIntervalStarts(*new_intervals);

  const bool is_registered = evaluation_context_.RunOnValueChangeOrTimeout(
      base::BindOnce(&UpdateTimeRestrictionsMonitor::OnIntervalsChanged,
                     base::Unretained(this)));
  DCHECK(is_registered);
}

void UpdateTimeRestrictionsMonitor::WaitForRestrictedIntervalStarts(
    const WeeklyTimeIntervalVector& restricted_time_intervals) {
  DCHECK(!restricted_time_intervals.empty());

  const WeeklyTimeInterval* current_interval =
      FindNextNearestInterval(restricted_time_intervals, Now());
  if (current_interval == nullptr) {
    LOG(WARNING) << "Could not find next nearest restricted interval.";
    return;
  }

  // If |current_interval| happens right now, set delay to zero.
  const TimeDelta duration_till_start =
      current_interval->InRange(Now())
          ? base::Microseconds(0)
          : Now().GetDurationTo(current_interval->start());
  LOG(INFO) << "Found restricted interval starting at "
            << (SystemState::Get()->clock()->GetWallclockTime() +
                duration_till_start);

  timeout_event_ = MessageLoop::current()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(
          &UpdateTimeRestrictionsMonitor::HandleRestrictedIntervalStarts,
          weak_ptr_factory_.GetWeakPtr()),
      duration_till_start);
}

void UpdateTimeRestrictionsMonitor::HandleRestrictedIntervalStarts() {
  timeout_event_ = MessageLoop::kTaskIdNull;
  if (delegate_)
    delegate_->OnRestrictedIntervalStarts();
}

void UpdateTimeRestrictionsMonitor::StopMonitoring() {
  MessageLoop::current()->CancelTask(timeout_event_);
  timeout_event_ = MessageLoop::kTaskIdNull;
}

void UpdateTimeRestrictionsMonitor::OnIntervalsChanged() {
  DCHECK(!evaluation_context_.is_expired());

  StopMonitoring();
  evaluation_context_.ResetEvaluation();
  StartMonitoring();
}

}  // namespace chromeos_update_manager
