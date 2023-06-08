// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/vmm_swap_metrics.h"

#include <memory>
#include <utility>

#include <base/timer/mock_timer.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <metrics/metrics_library_mock.h>
#include <vm_applications/apps.pb.h>

#include "vm_tools/common/vm_id.h"

using ::testing::_;
using ::testing::Return;

namespace vm_tools::concierge {

namespace {
static constexpr char kMetricsArcvmStateName[] = "Memory.VmmSwap.ArcVm.State";

class VmmSwapMetricsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    metrics_ = std::make_unique<MetricsLibraryMock>();
    heartbeat_timer_ = std::make_unique<base::MockRepeatingTimer>();

    ON_CALL(*metrics_, SendEnumToUMA(_, _, _)).WillByDefault(Return(true));
  }

  raw_ref<MetricsLibraryInterface> GetMetricsRef() {
    return raw_ref<MetricsLibraryInterface>::from_ptr(metrics_.get());
  }

  std::unique_ptr<MetricsLibraryMock> metrics_;
  std::unique_ptr<base::MockRepeatingTimer> heartbeat_timer_;
};
}  // namespace

TEST_F(VmmSwapMetricsTest, OnSwappableIdleEnabledStartHeartbeat) {
  base::MockRepeatingTimer* heartbeat_timer = heartbeat_timer_.get();
  VmmSwapMetrics metrics = VmmSwapMetrics(VmId::Type::ARCVM, GetMetricsRef(),
                                          std::move(heartbeat_timer_));

  metrics.OnSwappableIdleEnabled();

  EXPECT_TRUE(heartbeat_timer->IsRunning());
}

TEST_F(VmmSwapMetricsTest, OnSwappableIdleDisabledStopHeartbeat) {
  base::MockRepeatingTimer* heartbeat_timer = heartbeat_timer_.get();
  VmmSwapMetrics metrics = VmmSwapMetrics(VmId::Type::ARCVM, GetMetricsRef(),
                                          std::move(heartbeat_timer_));

  metrics.OnSwappableIdleEnabled();
  metrics.OnSwappableIdleDisabled();

  EXPECT_FALSE(heartbeat_timer->IsRunning());
}

TEST_F(VmmSwapMetricsTest, HeartbeatWithoutEnabled) {
  base::MockRepeatingTimer* heartbeat_timer = heartbeat_timer_.get();
  VmmSwapMetrics metrics = VmmSwapMetrics(VmId::Type::ARCVM, GetMetricsRef(),
                                          std::move(heartbeat_timer_));

  metrics.OnSwappableIdleEnabled();

  EXPECT_CALL(
      *metrics_,
      SendEnumToUMA(kMetricsArcvmStateName,
                    static_cast<int>(VmmSwapMetrics::State::kDisabled),
                    static_cast<int>(VmmSwapMetrics::State::kMaxValue) + 1));
  heartbeat_timer->Fire();
}

TEST_F(VmmSwapMetricsTest, HeartbeatFailToSend) {
  base::MockRepeatingTimer* heartbeat_timer = heartbeat_timer_.get();
  VmmSwapMetrics metrics = VmmSwapMetrics(VmId::Type::ARCVM, GetMetricsRef(),
                                          std::move(heartbeat_timer_));

  metrics.OnSwappableIdleEnabled();

  EXPECT_CALL(
      *metrics_,
      SendEnumToUMA(kMetricsArcvmStateName,
                    static_cast<int>(VmmSwapMetrics::State::kDisabled),
                    static_cast<int>(VmmSwapMetrics::State::kMaxValue) + 1))
      .WillOnce(Return(false));
  EXPECT_NO_FATAL_FAILURE(heartbeat_timer->Fire());
}

TEST_F(VmmSwapMetricsTest, HeartbeatAfterEnabled) {
  base::MockRepeatingTimer* heartbeat_timer = heartbeat_timer_.get();
  VmmSwapMetrics metrics = VmmSwapMetrics(VmId::Type::ARCVM, GetMetricsRef(),
                                          std::move(heartbeat_timer_));

  metrics.OnSwappableIdleEnabled();
  metrics.OnVmmSwapEnabled();

  EXPECT_CALL(
      *metrics_,
      SendEnumToUMA(kMetricsArcvmStateName,
                    static_cast<int>(VmmSwapMetrics::State::kEnabled),
                    static_cast<int>(VmmSwapMetrics::State::kMaxValue) + 1));
  heartbeat_timer->Fire();
}

TEST_F(VmmSwapMetricsTest, HeartbeatAfterDisabled) {
  base::MockRepeatingTimer* heartbeat_timer = heartbeat_timer_.get();
  VmmSwapMetrics metrics = VmmSwapMetrics(VmId::Type::ARCVM, GetMetricsRef(),
                                          std::move(heartbeat_timer_));

  metrics.OnSwappableIdleEnabled();
  metrics.OnVmmSwapEnabled();
  metrics.OnVmmSwapDisabled();

  EXPECT_CALL(
      *metrics_,
      SendEnumToUMA(kMetricsArcvmStateName,
                    static_cast<int>(VmmSwapMetrics::State::kDisabled),
                    static_cast<int>(VmmSwapMetrics::State::kMaxValue) + 1));
  heartbeat_timer->Fire();
}

TEST_F(VmmSwapMetricsTest, HeartbeatMultiple) {
  base::MockRepeatingTimer* heartbeat_timer = heartbeat_timer_.get();
  VmmSwapMetrics metrics = VmmSwapMetrics(VmId::Type::ARCVM, GetMetricsRef(),
                                          std::move(heartbeat_timer_));

  metrics.OnSwappableIdleEnabled();
  metrics.OnVmmSwapEnabled();

  EXPECT_CALL(
      *metrics_,
      SendEnumToUMA(kMetricsArcvmStateName,
                    static_cast<int>(VmmSwapMetrics::State::kEnabled),
                    static_cast<int>(VmmSwapMetrics::State::kMaxValue) + 1))
      .Times(3);
  heartbeat_timer->Fire();
  heartbeat_timer->Fire();
  heartbeat_timer->Fire();
  metrics.OnVmmSwapDisabled();
  EXPECT_CALL(
      *metrics_,
      SendEnumToUMA(kMetricsArcvmStateName,
                    static_cast<int>(VmmSwapMetrics::State::kDisabled),
                    static_cast<int>(VmmSwapMetrics::State::kMaxValue) + 1))
      .Times(3);
  heartbeat_timer->Fire();
  heartbeat_timer->Fire();
  heartbeat_timer->Fire();
}

TEST_F(VmmSwapMetricsTest, UnsupportedVm) {
  base::MockRepeatingTimer* heartbeat_timer = heartbeat_timer_.get();
  VmmSwapMetrics metrics = VmmSwapMetrics(VmId::Type::UNKNOWN, GetMetricsRef(),
                                          std::move(heartbeat_timer_));

  metrics.OnSwappableIdleEnabled();

  EXPECT_CALL(*metrics_, SendEnumToUMA("Memory.VmmSwap.Unknown.State", _, _));
  heartbeat_timer->Fire();
}

}  // namespace vm_tools::concierge
