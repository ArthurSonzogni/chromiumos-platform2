// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/analytics/metrics_test_util.h"

#include <memory>
#include <utility>

#include <base/task/sequenced_task_runner.h>
#include <gtest/gtest.h>
#include <metrics/metrics_library_mock.h>

#include "missive/analytics/metrics.h"

using ::testing::NotNull;

namespace reporting::analytics {

Metrics::TestEnvironment::TestEnvironment() {
  Metrics::Get().metrics_ = std::make_unique<MetricsLibraryMock>();
  Metrics::Get().metrics_task_runner_ =
      base::SequencedTaskRunner::GetCurrentDefault();

  // gtest is thread-safe; supposedly these assertions on the pointers should
  // establish a memory barrier while also making some assertions.
  // metrics_task_runner_ is thread-safe by itself and is not asserted here.
  EXPECT_THAT(Metrics::Get().metrics_, NotNull());
}

Metrics::TestEnvironment::~TestEnvironment() {
  Metrics::Get().metrics_ = std::move(original_metrics_);
  Metrics::Get().metrics_task_runner_ = std::move(original_task_runner_);

  // gtest is thread-safe; supposedly these assertions on the pointers should
  // establish a memory barrier while also making some assertions.
  // metrics_task_runner_ is thread-safe by itself and is not asserted here.
  EXPECT_THAT(Metrics::Get().metrics_, NotNull());
}

// static
MetricsLibraryMock& Metrics::TestEnvironment::GetMockMetricsLibrary() {
  return *static_cast<MetricsLibraryMock*>(Metrics::Get().metrics_.get());
}

}  // namespace reporting::analytics
