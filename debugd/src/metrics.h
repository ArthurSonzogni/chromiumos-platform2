// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEBUGD_SRC_METRICS_H_
#define DEBUGD_SRC_METRICS_H_

#include <memory>
#include <string>

#include <base/time/time.h>
#include <metrics/metrics_library.h>

namespace debugd {

class Stopwatch {
 public:
  // Initializes an object for recording time metrics and optional reporting to
  // UMA. Records a point in time upon instantiation to keep track of time
  // passed. Receives the metric suffix as an input, which is appended to the
  // constant prefix to create the metric name used for logging and UMA
  // reporting.
  Stopwatch(const std::string& metric_name,
            const bool local_logging,
            const bool report_to_uma);

  // Calculates the time delta between when the object was instantiated and when
  // the destructor is called and reports the duration to UMA.
  ~Stopwatch();

  // Similar to an actual stopwatch, the Lap function takes a snapshot of the
  // elapsed time at the moment the function is called. If local_logging_ is
  // active, this function logs the time, alongside the name of the lap received
  // as an argument. This is never reported to UMA.
  void Lap(const std::string&);

 private:
  base::TimeTicks start_;
  std::string metric_name_;
  // Dictates whether the lap times are logged locally.
  const bool local_logging_;
  // Dictates whether the stopwatch elapsed time is reported to UMA.
  const bool report_to_uma_;
  std::unique_ptr<MetricsLibraryInterface> metrics_library_;
};

}  // namespace debugd

#endif  // DEBUGD_SRC_METRICS_H_
