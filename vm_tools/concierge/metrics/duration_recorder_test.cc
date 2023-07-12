// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/metrics/duration_recorder.h"

#include <memory>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <metrics/metrics_library_mock.h>
#include <vm_applications/apps.pb.h>

using ::testing::_;
using ::testing::Return;

namespace vm_tools::concierge::metrics {

TEST(DurationRecorderTest, GetMetricsName) {
  for (int vm_type = static_cast<int>(apps::VmType_MIN);
       vm_type <= apps::VmType_MAX; vm_type++) {
    std::string vm_name = apps::VmType_Name(vm_type);
    EXPECT_EQ(internal::GetVirtualizationMetricsName(
                  static_cast<apps::VmType>(vm_type),
                  DurationRecorder::Event::kVmStart),
              "Virtualization." + vm_name + ".Start.Duration");
    EXPECT_EQ(internal::GetVirtualizationMetricsName(
                  static_cast<apps::VmType>(vm_type),
                  DurationRecorder::Event::kVmStop),
              "Virtualization." + vm_name + ".Stop.Duration");
  }
}

}  // namespace vm_tools::concierge::metrics
