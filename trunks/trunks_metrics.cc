// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "trunks/trunks_metrics.h"

#include <algorithm>
#include <cstdint>
#include <string>

#include <base/check_op.h>
#include <base/logging.h>
#include <base/time/time.h>

#include "trunks/error_codes.h"

extern "C" {
#include <sys/sysinfo.h>
}

namespace trunks {

namespace {

constexpr base::TimeDelta kMinMetricsTimeout = base::Minutes(0);
constexpr base::TimeDelta kMaxMetricsTimeout = base::Minutes(5);
constexpr int kNumBuckets = 100;

constexpr char kFirstTimeoutWritingCommand[] =
    "Platform.Trunks.FirstTimeoutWritingCommand";
constexpr char kFirstTimeoutWritingTime[] =
    "Platform.Trunks.FirstTimeoutWritingTime";

constexpr char kRecoverableWriteErrorNo[] =
    "Platform.Trunks.RecoverableWriteErrorNo";
constexpr char kUnrecoverableWriteErrorNo[] =
    "Platform.Trunks.UnrecoverableWriteErrorNo";
constexpr char kTransitionedWriteErrorNo[] =
    "Platform.Trunks.TransitionedWriteErrorNo";

constexpr char kTpmErrorCode[] = "Platform.Trunks.TpmErrorCode";

// The total event time.
constexpr char kEventTime[] = "Platform.Trunks.EventTime.";

// The time we spend on the TPM that is directly related to the event.
constexpr char kEventRelatedTime[] = "Platform.Trunks.EventRelatedTime.";

// The time we spend on the TPM that is not directly related to the event.
constexpr char kEventIrrelatedTime[] = "Platform.Trunks.EventIrrelatedTime.";

}  // namespace

bool TrunksMetrics::ReportTpmHandleTimeoutCommandAndTime(int error_result,
                                                         TPM_CC command_code) {
  std::string command_metrics, time_metrics;
  switch (error_result) {
    case TRUNKS_RC_WRITE_ERROR:
      command_metrics = kFirstTimeoutWritingCommand;
      time_metrics = kFirstTimeoutWritingTime;
      break;
    case TRUNKS_RC_READ_ERROR:
      return true;
    default:
      LOG(INFO) << "Reporting unexpected error: " << error_result;
      return false;
  }

  metrics_library_.SendSparseToUMA(command_metrics,
                                   static_cast<int>(command_code));
  struct sysinfo info;
  if (sysinfo(&info) == 0) {
    constexpr int kMinUptimeInSeconds = 1;
    constexpr int kMaxUptimeInSeconds = 7 * 24 * 60 * 60;  // 1 week
    constexpr int kNumUptimeBuckets = 50;

    metrics_library_.SendToUMA(time_metrics, info.uptime, kMinUptimeInSeconds,
                               kMaxUptimeInSeconds, kNumUptimeBuckets);
  } else {
    PLOG(WARNING) << "Error getting system uptime";
  }
  return true;
}

void TrunksMetrics::ReportTpmErrorCode(TPM_RC error_code) {
  metrics_library_.SendSparseToUMA(kTpmErrorCode, static_cast<int>(error_code));
}

void TrunksMetrics::ReportWriteErrorNo(int prev, int next) {
  // Don't record any UMA if the state is good or just goes from good to bad.
  if (prev <= 0) {
    return;
  }

  static bool has_error_transitioned = false;
  if (next <= 0) {
    metrics_library_.SendSparseToUMA(kRecoverableWriteErrorNo, prev);
  } else if (prev == next) {
    // It is possible for the error to change, and the new error keeps
    // happending. In that case, it is not conclusive if the error is
    // unrecoverable until the next process cycle.
    if (has_error_transitioned) {
      return;
    }
    // Since the status gets stuck in a single error, the same call occurs for
    // every single TPM commands, need a call-once guard for this case.
    static bool call_once = [&]() {
      this->metrics_library_.SendSparseToUMA(kUnrecoverableWriteErrorNo, prev);
      return true;
    }();
    (void)(call_once);
  } else {
    metrics_library_.SendSparseToUMA(kTransitionedWriteErrorNo, prev);
    has_error_transitioned = true;
  }
}

void TrunksMetrics::StartEvent(const std::string& event, uint64_t sender) {
  events[event] = EventDetail{
      .sender = sender,
      .start_time = base::Time::Now(),
      .related_time = base::Seconds(0),
      .irrelated_time = base::Seconds(0),
  };
}

void TrunksMetrics::StopEvent(const std::string& event, uint64_t sender) {
  auto it = events.find(event);
  if (it == events.end()) {
    LOG(WARNING) << "Stop event(" << event << ") without starting it.";
    return;
  }

  // Total event time.
  metrics_library_.SendTimeToUMA(
      kEventTime + event, base::Time::Now() - it->second.start_time,
      kMinMetricsTimeout, kMaxMetricsTimeout, kNumBuckets);

  // Related event time.
  metrics_library_.SendTimeToUMA(kEventRelatedTime + event,
                                 it->second.related_time, kMinMetricsTimeout,
                                 kMaxMetricsTimeout, kNumBuckets);

  // Irrelated event time.
  metrics_library_.SendTimeToUMA(kEventIrrelatedTime + event,
                                 it->second.irrelated_time, kMinMetricsTimeout,
                                 kMaxMetricsTimeout, kNumBuckets);

  events.erase(it);
}

void TrunksMetrics::ReportCommandTime(uint64_t sender,
                                      base::TimeDelta duration) {
  for (auto& [event, detail] : events) {
    base::TimeDelta event_time = duration;

    // If the command starts before we starts the event, we should only record
    // the time within the event range.
    base::Time now = base::Time::Now();
    base::TimeDelta delta = now - detail.start_time;
    if (event_time > delta) {
      event_time = delta;
    }

    if (detail.sender == sender) {
      detail.related_time += event_time;
    } else {
      detail.irrelated_time += event_time;
    }
  }
}

}  // namespace trunks
