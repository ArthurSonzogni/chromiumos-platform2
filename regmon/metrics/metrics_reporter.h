// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REGMON_METRICS_METRICS_REPORTER_H_
#define REGMON_METRICS_METRICS_REPORTER_H_

namespace regmon::metrics {

class MetricsReporter {
 public:
  MetricsReporter(const MetricsReporter&) = delete;
  MetricsReporter& operator=(const MetricsReporter&) = delete;
  virtual ~MetricsReporter() = default;

  virtual bool ReportAnnotationViolation(int unique_id) = 0;

 protected:
  MetricsReporter() = default;
};

}  // namespace regmon::metrics

#endif  // REGMON_METRICS_METRICS_REPORTER_H_
