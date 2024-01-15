// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TRUNKS_TRUNKS_METRICS_H_
#define TRUNKS_TRUNKS_METRICS_H_

#include <cstdint>
#include <map>
#include <string>

#include <base/time/time.h>
#include <metrics/metrics_library.h>

#include "trunks/tpm_generated.h"

namespace trunks {

// This class provides wrapping functions for callers to report UMAs of
// `trunks`.
// TODO(chingkang): Add unittest for this.
class TrunksMetrics {
 public:
  TrunksMetrics() = default;
  ~TrunksMetrics() = default;

  // Not copyable or movable.
  TrunksMetrics(const TrunksMetrics&) = delete;
  TrunksMetrics& operator=(const TrunksMetrics&) = delete;
  TrunksMetrics(TrunksMetrics&&) = delete;
  TrunksMetrics& operator=(TrunksMetrics&&) = delete;

  // This function reports the command code and the time of the first writing
  // or reading timeout. So it should only be called once.
  bool ReportTpmHandleTimeoutCommandAndTime(int error_result,
                                            TPM_CC command_code);

  // This function reports the TPM command error code.
  void ReportTpmErrorCode(TPM_RC error_code);

  void ReportWriteErrorNo(int prev, int next);

  // These event related functions can only be called on the same thread.
  void StartEvent(const std::string& event, uint64_t sender);
  void StopEvent(const std::string& event, uint64_t sender);
  void ReportCommandTime(uint64_t sender, base::TimeDelta duration);

 private:
  struct EventDetail {
    uint64_t sender;
    base::Time start_time;
    base::TimeDelta related_time;
    base::TimeDelta irrelated_time;
  };

  std::map<std::string, EventDetail> events;
  MetricsLibrary metrics_library_;
};

}  // namespace trunks

#endif  // TRUNKS_TRUNKS_METRICS_H_
