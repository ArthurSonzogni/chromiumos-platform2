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
  // must match chrome browser
  // tools/metrics/histograms/metadata/virtualization/histograms.xml
  EXPECT_EQ(internal::GetVirtualizationMetricsName(
                apps::VmType::TERMINA, DurationRecorder::Event::kVmStart),
            "Virtualization.TERMINA.StartDuration");
  EXPECT_EQ(internal::GetVirtualizationMetricsName(
                apps::VmType::TERMINA, DurationRecorder::Event::kVmStop),
            "Virtualization.TERMINA.StopDuration");

  EXPECT_EQ(internal::GetVirtualizationMetricsName(
                apps::VmType::PLUGIN_VM, DurationRecorder::Event::kVmStart),
            "Virtualization.PLUGIN_VM.StartDuration");

  EXPECT_EQ(internal::GetVirtualizationMetricsName(
                apps::VmType::BOREALIS, DurationRecorder::Event::kVmStart),
            "Virtualization.BOREALIS.StartDuration");

  EXPECT_EQ(internal::GetVirtualizationMetricsName(
                apps::VmType::BRUSCHETTA, DurationRecorder::Event::kVmStart),
            "Virtualization.BRUSCHETTA.StartDuration");

  EXPECT_EQ(internal::GetVirtualizationMetricsName(
                apps::VmType::ARCVM, DurationRecorder::Event::kVmStart),
            "Virtualization.ARCVM.StartDuration");
}

}  // namespace vm_tools::concierge::metrics
