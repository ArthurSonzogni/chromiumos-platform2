// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/mm/balloon_metrics.h"

#include <memory>

#include <base/test/task_environment.h>
#include <base/time/time.h>
#include <gtest/gtest.h>
#include <metrics/metrics_library_mock.h>

namespace vm_tools::concierge::mm {
namespace {

using testing::_;

TEST(BalloonMetricsTest, SizeOnShutdown) {
  base::test::TaskEnvironment task_environment{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};

  auto metrics_library = std::make_unique<MetricsLibraryMock>();
  {
    auto balloon_metrics = std::make_unique<BalloonMetrics>(
        apps::VmType::ARCVM,
        raw_ref<MetricsLibraryInterface>::from_ptr(metrics_library.get()));

    EXPECT_CALL(*metrics_library,
                SendTimeToUMA("Memory.VMMMS.ARCVM.ResizeInterval",
                              base::Seconds(10), _, _, _))
        .Times(1);
    EXPECT_CALL(*metrics_library,
                SendToUMA("Memory.VMMMS.ARCVM.Inflate", 256, _, _, _))
        .Times(1);
    task_environment.FastForwardBy(base::Seconds(10));
    balloon_metrics->OnResize({true, MiB(256), MiB(256)});

    // We expect 6 size samples if we destruct 60 minutes later.
    task_environment.FastForwardBy(base::Minutes(60));
    EXPECT_CALL(
        *metrics_library,
        SendRepeatedToUMA("Memory.VMMMS.ARCVM.Size10Minutes", 256, _, _, _, 6));
  }
}

}  // namespace
}  // namespace vm_tools::concierge::mm
