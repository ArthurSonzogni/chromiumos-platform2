// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TRUNKS_TRUNKS_METRICS_H_
#define TRUNKS_TRUNKS_METRICS_H_

#include <string>

#include <metrics/metrics_library.h>

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
                                            const std::string& command);

 private:
  MetricsLibrary metrics_library_;
};

}  // namespace trunks

#endif  // TRUNKS_TRUNKS_METRICS_H_
