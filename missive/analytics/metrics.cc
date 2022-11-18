// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/analytics/metrics.h"

#include <string>

#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/memory/scoped_refptr.h>
#include <base/no_destructor.h>
#include <base/task/sequenced_task_runner.h>
#include <metrics/metrics_library.h>

namespace reporting::analytics {

Metrics::Metrics() = default;

// static
Metrics& Metrics::Get() {
  static base::NoDestructor<Metrics> metrics;
  return *metrics;
}

template <typename FuncType, typename... ArgTypes>
bool Metrics::PostUMATask(FuncType send_to_uma_func, ArgTypes... args) {
  DCHECK(metrics_task_runner_);
  return metrics_task_runner_->PostTask(
      FROM_HERE,
      // Wrap send_to_uma_func with a void-return type lambda.
      base::BindOnce(
          [](FuncType send_to_uma_func, MetricsLibraryInterface* metrics,
             ArgTypes... args) -> void {
            DCHECK(metrics);
            bool success = (metrics->*send_to_uma_func)(args...);
            LOG_IF(WARNING, !success) << "Send to UMA failed.";
          },
          // Safe to use `metrics_.get()` because `metrics_` never gets
          // destructed. For tasks posted in tests, the tasks must be always
          // completed before the test terminates.
          send_to_uma_func, base::Unretained(metrics_.get()), args...));
}

bool Metrics::SendPercentageToUMA(const std::string& name, int sample) {
  return PostUMATask(&MetricsLibraryInterface::SendPercentageToUMA, name,
                     sample);
}

bool Metrics::SendLinearToUMA(const std::string& name, int sample, int max) {
  return PostUMATask(&MetricsLibraryInterface::SendLinearToUMA, name, sample,
                     max);
}

bool Metrics::SendToUMA(
    const std::string& name, int sample, int min, int max, int nbuckets) {
  return PostUMATask(&MetricsLibraryInterface::SendToUMA, name, sample, min,
                     max, nbuckets);
}

}  // namespace reporting::analytics
