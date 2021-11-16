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
  // Initializes an object for recording time metrics and reporting them to UMA.
  // Records a point in time upon instantiation to keep track of time. Receives
  // the histogram suffix as an input, which is appended to the constant prefix.
  explicit Stopwatch(const std::string&);

  // Calculates the time delta between when the object was instantiated and when
  // the destructor is called and reports the duration to UMA.
  ~Stopwatch();

 private:
  base::TimeTicks start_;
  std::string histogram_name_;
  std::unique_ptr<MetricsLibraryInterface> metrics_library_;
};

}  // namespace debugd

#endif  // DEBUGD_SRC_METRICS_H_
