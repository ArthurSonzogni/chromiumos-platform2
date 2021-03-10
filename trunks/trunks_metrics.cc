// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "trunks/trunks_metrics.h"

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

constexpr char kFirstTimeoutWritingCommand[] =
    "Platform.Trunks.FirstTimeoutWritingCommand";
constexpr char kFirstTimeoutWritingTime[] =
    "Platform.Trunks.FirstTimeoutWritingTime";

constexpr char kFirstTimeoutReadingCommand[] =
    "Platform.Trunks.FirstTimeoutReadingCommand";
constexpr char kFirstTimeoutReadingTime[] =
    "Platform.Trunks.FirstTimeoutReadingTime";

constexpr char kTpmErrorCode[] = "Platform.Trunks.TpmErrorCode";

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
      command_metrics = kFirstTimeoutReadingCommand;
      time_metrics = kFirstTimeoutReadingTime;
      break;
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

}  // namespace trunks
