//
// Copyright (C) 2014 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include "update_engine/update_manager/evaluation_context.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

#include <base/functional/bind.h>
#include <base/json/json_writer.h>
#include <base/location.h>
#include <base/logging.h>
#include <base/strings/string_util.h>
#include <base/values.h>

#include "update_engine/common/system_state.h"
#include "update_engine/common/utils.h"

using base::Time;
using base::TimeDelta;
using brillo::MessageLoop;
using chromeos_update_engine::SystemState;
using std::string;
using std::unique_ptr;

namespace {

// Returns whether |curr_time| surpassed |ref_time|; if not, also checks whether
// |ref_time| is sooner than the current value of |*reeval_time|, in which case
// the latter is updated to the former.
bool IsTimeGreaterThanHelper(Time ref_time, Time curr_time, Time* reeval_time) {
  if (curr_time > ref_time)
    return true;
  // Remember the nearest reference we've checked against in this evaluation.
  if (*reeval_time > ref_time)
    *reeval_time = ref_time;
  return false;
}

// If |expires| never happens (maximal value), returns the maximal interval;
// otherwise, returns the difference between |expires| and |curr|.
TimeDelta GetTimeout(Time curr, Time expires) {
  if (expires.is_max())
    return TimeDelta::Max();
  return expires - curr;
}

}  // namespace

namespace chromeos_update_manager {

EvaluationContext::EvaluationContext(TimeDelta evaluation_timeout,
                                     TimeDelta expiration_timeout)
    : evaluation_timeout_(evaluation_timeout),
      expiration_timeout_(expiration_timeout),
      weak_ptr_factory_(this) {
  ResetEvaluation();
  ResetExpiration();
}

EvaluationContext::~EvaluationContext() {
  RemoveObserversAndTimeout();
}

unique_ptr<base::OnceClosure> EvaluationContext::RemoveObserversAndTimeout() {
  for (auto& it : value_cache_) {
    if (it.first->GetMode() == kVariableModeAsync)
      it.first->RemoveObserver(this);
  }
  MessageLoop::current()->CancelTask(timeout_event_);
  timeout_event_ = MessageLoop::kTaskIdNull;

  return std::move(callback_);
}

TimeDelta EvaluationContext::RemainingTime(Time monotonic_deadline) const {
  if (monotonic_deadline.is_max())
    return TimeDelta::Max();
  TimeDelta remaining =
      monotonic_deadline - SystemState::Get()->clock()->GetMonotonicTime();
  return std::max(remaining, TimeDelta());
}

Time EvaluationContext::MonotonicDeadline(TimeDelta timeout) {
  return (timeout.is_max()
              ? Time::Max()
              : SystemState::Get()->clock()->GetMonotonicTime() + timeout);
}

void EvaluationContext::ValueChanged(BaseVariable* var) {
  DLOG(INFO) << "ValueChanged() called for variable " << var->GetName();
  OnValueChangedOrTimeout();
}

void EvaluationContext::OnTimeout() {
  DLOG(INFO) << "OnTimeout() called due to "
             << (timeout_marks_expiration_ ? "expiration" : "poll interval");
  timeout_event_ = MessageLoop::kTaskIdNull;
  is_expired_ = timeout_marks_expiration_;
  OnValueChangedOrTimeout();
}

void EvaluationContext::OnValueChangedOrTimeout() {
  // Copy the callback handle locally, allowing it to be reassigned.
  unique_ptr<base::OnceClosure> callback = RemoveObserversAndTimeout();

  if (callback.get())
    std::move(*callback).Run();
}

bool EvaluationContext::IsWallclockTimeGreaterThan(Time timestamp) {
  return IsTimeGreaterThanHelper(timestamp, evaluation_start_wallclock_,
                                 &reevaluation_time_wallclock_);
}

bool EvaluationContext::IsMonotonicTimeGreaterThan(Time timestamp) {
  return IsTimeGreaterThanHelper(timestamp, evaluation_start_monotonic_,
                                 &reevaluation_time_monotonic_);
}

void EvaluationContext::ResetEvaluation() {
  const auto* clock = SystemState::Get()->clock();
  evaluation_start_wallclock_ = clock->GetWallclockTime();
  evaluation_start_monotonic_ = clock->GetMonotonicTime();
  reevaluation_time_wallclock_ = Time::Max();
  reevaluation_time_monotonic_ = Time::Max();
  evaluation_monotonic_deadline_ = MonotonicDeadline(evaluation_timeout_);

  // Remove the cached values of non-const variables
  for (auto it = value_cache_.begin(); it != value_cache_.end();) {
    if (it->first->GetMode() == kVariableModeConst) {
      ++it;
    } else {
      it = value_cache_.erase(it);
    }
  }
}

void EvaluationContext::ResetExpiration() {
  expiration_monotonic_deadline_ = MonotonicDeadline(expiration_timeout_);
  is_expired_ = false;
}

bool EvaluationContext::RunOnValueChangeOrTimeout(base::OnceClosure callback) {
  // Check that the method was not called more than once.
  if (callback_.get()) {
    LOG(ERROR) << "RunOnValueChangeOrTimeout called more than once.";
    return false;
  }

  // Check that the context did not yet expire.
  if (is_expired()) {
    LOG(ERROR) << "RunOnValueChangeOrTimeout called on an expired context.";
    return false;
  }

  // Handle reevaluation due to a Is{Wallclock,Monotonic}TimeGreaterThan(). We
  // choose the smaller of the differences between evaluation start time and
  // reevaluation time among the wallclock and monotonic scales.
  TimeDelta timeout = std::min(
      GetTimeout(evaluation_start_wallclock_, reevaluation_time_wallclock_),
      GetTimeout(evaluation_start_monotonic_, reevaluation_time_monotonic_));

  // Handle reevaluation due to async or poll variables.
  bool waiting_for_value_change = false;
  for (auto& it : value_cache_) {
    switch (it.first->GetMode()) {
      case kVariableModeAsync:
        DLOG(INFO) << "Waiting for value on " << it.first->GetName();
        it.first->AddObserver(this);
        waiting_for_value_change = true;
        break;
      case kVariableModePoll:
        timeout = std::min(timeout, it.first->GetPollInterval());
        break;
      case kVariableModeConst:
        // Ignored.
        break;
    }
  }

  // Check if the re-evaluation is actually being scheduled. If there are no
  // events waited for, this function should return false.
  if (!waiting_for_value_change && timeout.is_max())
    return false;

  // Ensure that we take into account the expiration timeout.
  TimeDelta expiration = RemainingTime(expiration_monotonic_deadline_);
  timeout_marks_expiration_ = expiration < timeout;
  if (timeout_marks_expiration_)
    timeout = expiration;

  // Store the reevaluation callback.
  callback_.reset(new base::OnceClosure(std::move(callback)));

  // Schedule a timeout event, if one is set.
  if (!timeout.is_max()) {
    DLOG(INFO) << "Waiting for timeout in "
               << chromeos_update_engine::utils::FormatTimeDelta(timeout);
    timeout_event_ = MessageLoop::current()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&EvaluationContext::OnTimeout,
                       weak_ptr_factory_.GetWeakPtr()),
        timeout);
  }

  return true;
}

string EvaluationContext::DumpContext() const {
  base::Value::Dict variables;
  for (auto& it : value_cache_) {
    variables.Set(it.first->GetName(), it.second.ToString());
  }

  base::Value::Dict value;
  value.Set("variables", std::move(variables));
  value.Set(
      "evaluation_start_wallclock",
      chromeos_update_engine::utils::ToString(evaluation_start_wallclock_));
  value.Set(
      "evaluation_start_monotonic",
      chromeos_update_engine::utils::ToString(evaluation_start_monotonic_));

  string json_str;
  base::JSONWriter::WriteWithOptions(
      value, base::JSONWriter::OPTIONS_PRETTY_PRINT, &json_str);
  base::TrimWhitespaceASCII(json_str, base::TRIM_TRAILING, &json_str);

  return json_str;
}

}  // namespace chromeos_update_manager
