// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MISSIVE_ANALYTICS_METRICS_TEST_UTIL_H_
#define MISSIVE_ANALYTICS_METRICS_TEST_UTIL_H_

#include <memory>
#include <utility>

#include <base/memory/scoped_refptr.h>
#include <base/task/sequenced_task_runner.h>
#include <metrics/metrics_library.h>
#include <metrics/metrics_library_mock.h>

#include "missive/analytics/metrics.h"

namespace reporting::analytics {

// Replaces the metrics library with a mock upon construction and restores it
// once the test terminates. Also resets the task runner that the metrics
// library instance runs on. Normally used as a member of a test class.
class Metrics::TestEnvironment {
 public:
  TestEnvironment();
  TestEnvironment(const TestEnvironment&) = delete;
  TestEnvironment& operator=(const TestEnvironment&) = delete;
  ~TestEnvironment();

  // Get the pointer to the mock metrics library pointer. Ownership of the
  // pointer is not transferred.
  static MetricsLibraryMock& GetMockMetricsLibrary();

 private:
  scoped_refptr<base::SequencedTaskRunner> original_task_runner_{
      std::move(Metrics::Get().metrics_task_runner_)};
  // The original metrics. We save it here to reduce chances of flakiness: It
  // was initialized in a thread-safe manner and it's safe to switch it back,
  // rather than instantiating another `MetricsLibrary` instance.
  std::unique_ptr<MetricsLibraryInterface> original_metrics_{
      std::move(Metrics::Get().metrics_)};
};
}  // namespace reporting::analytics
#endif  // MISSIVE_ANALYTICS_METRICS_TEST_UTIL_H_
