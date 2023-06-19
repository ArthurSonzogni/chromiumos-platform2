// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/vmm_swap_metrics.h"

#include <memory>
#include <utility>

#include <base/functional/bind.h>
#include <base/memory/page_size.h>
#include <base/test/task_environment.h>
#include <base/time/time.h>
#include <base/timer/mock_timer.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <metrics/metrics_library_mock.h>
#include <vm_applications/apps.pb.h>

#include "base/types/expected.h"
#include "vm_tools/common/vm_id.h"

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::Return;

namespace vm_tools::concierge {

namespace {
static constexpr char kMetricsArcvmStateName[] = "Memory.VmmSwap.ARCVM.State";
static constexpr char kMetricsArcvmInactiveBeforeEnableDurationName[] =
    "Memory.VmmSwap.ARCVM.InactiveBeforeEnableDuration";
static constexpr char kMetricsArcvmActiveAfterEnableDurationName[] =
    "Memory.VmmSwap.ARCVM.ActiveAfterEnableDuration";
static constexpr char kMetricsArcvmInactiveNoEnableDurationName[] =
    "Memory.VmmSwap.ARCVM.InactiveNoEnableDuration";
static constexpr char kMetricsArcvmMinPagesInFileName[] =
    "Memory.VmmSwap.ARCVM.MinPagesInFile";
static constexpr char kMetricsArcvmAvgPagesInFileName[] =
    "Memory.VmmSwap.ARCVM.AvgPagesInFile";
static constexpr char kMetricsArcvmPageAverageDurationInFileName[] =
    "Memory.VmmSwap.ARCVM.PageAverageDurationInFile";

class FakeFetchVmmSwapStatus final {
 public:
  VmmSwapMetrics::FetchVmmSwapStatus Create() {
    return base::BindRepeating(&FakeFetchVmmSwapStatus::Callback,
                               base::Unretained(this));
  }

  bool result_ = false;
  SwapStatus status_;

 private:
  base::expected<SwapStatus, std::string> Callback() {
    if (result_) {
      return status_;
    }
    return base::unexpected("fetch error");
  }
};

class VmmSwapMetricsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    metrics_ = std::make_unique<MetricsLibraryMock>();
    heartbeat_timer_ = std::make_unique<base::MockRepeatingTimer>();

    ON_CALL(*metrics_, SendEnumToUMA(_, _, _)).WillByDefault(Return(true));
    // Ignore uninterested metrics.
    EXPECT_CALL(*metrics_, SendToUMA(_, _, _, _, _)).Times(AnyNumber());
  }

  raw_ref<MetricsLibraryInterface> GetMetricsRef() {
    return raw_ref<MetricsLibraryInterface>::from_ptr(metrics_.get());
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
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

TEST_F(VmmSwapMetricsTest, MetricsNameContainsARCVM) {
  base::MockRepeatingTimer* heartbeat_timer = heartbeat_timer_.get();
  VmmSwapMetrics metrics = VmmSwapMetrics(VmId::Type::ARCVM, GetMetricsRef(),
                                          std::move(heartbeat_timer_));

  metrics.OnSwappableIdleEnabled();

  // The vm_name `ARCVM` is registered to the manifest file in Chromium
  // "tools/metrics/histograms/metadata/memory/histograms.xml" as VmmSwapVmName.
  EXPECT_CALL(*metrics_, SendEnumToUMA("Memory.VmmSwap.ARCVM.State", _, _));
  heartbeat_timer->Fire();
}

TEST_F(VmmSwapMetricsTest, ReportDurationsEnabled) {
  VmmSwapMetrics metrics = VmmSwapMetrics(VmId::Type::ARCVM, GetMetricsRef(),
                                          std::move(heartbeat_timer_));
  base::Time now = base::Time::Now();

  metrics.OnSwappableIdleEnabled(now - base::Days(1));
  metrics.OnSwappableIdleEnabled(now - base::Hours(10));
  metrics.OnVmmSwapEnabled(now - base::Hours(10));

  EXPECT_CALL(
      *metrics_,
      SendToUMA(kMetricsArcvmInactiveBeforeEnableDurationName, 14, _, _, _))
      .Times(1);
  EXPECT_CALL(*metrics_, SendToUMA(kMetricsArcvmActiveAfterEnableDurationName,
                                   10, _, _, _))
      .Times(1);
  EXPECT_CALL(*metrics_,
              SendToUMA(kMetricsArcvmInactiveNoEnableDurationName, _, _, _, _))
      .Times(0);
  metrics.OnVmmSwapDisabled(now);
}

TEST_F(VmmSwapMetricsTest, ReportDurationsEnabledOnDestroy) {
  VmmSwapMetrics metrics = VmmSwapMetrics(VmId::Type::ARCVM, GetMetricsRef(),
                                          std::move(heartbeat_timer_));
  base::Time now = base::Time::Now();

  metrics.OnSwappableIdleEnabled(now - base::Days(1));
  metrics.OnSwappableIdleEnabled(now - base::Hours(10));
  metrics.OnVmmSwapEnabled(now - base::Hours(10));

  EXPECT_CALL(
      *metrics_,
      SendToUMA(kMetricsArcvmInactiveBeforeEnableDurationName, 14, _, _, _))
      .Times(1);
  EXPECT_CALL(*metrics_, SendToUMA(kMetricsArcvmActiveAfterEnableDurationName,
                                   10, _, _, _))
      .Times(1);
  EXPECT_CALL(*metrics_,
              SendToUMA(kMetricsArcvmInactiveNoEnableDurationName, _, _, _, _))
      .Times(0);
  metrics.OnDestroy(now);
}

TEST_F(VmmSwapMetricsTest, ReportDurationsForceEnabled) {
  VmmSwapMetrics metrics = VmmSwapMetrics(VmId::Type::ARCVM, GetMetricsRef(),
                                          std::move(heartbeat_timer_));
  base::Time now = base::Time::Now();

  EXPECT_CALL(
      *metrics_,
      SendToUMA(kMetricsArcvmInactiveBeforeEnableDurationName, _, _, _, _))
      .Times(0);
  EXPECT_CALL(*metrics_,
              SendToUMA(kMetricsArcvmActiveAfterEnableDurationName, _, _, _, _))
      .Times(0);
  EXPECT_CALL(*metrics_,
              SendToUMA(kMetricsArcvmInactiveNoEnableDurationName, _, _, _, _))
      .Times(0);

  // OnVmmSwapEnabled without OnSwappableIdleEnabled
  metrics.OnVmmSwapEnabled(now - base::Days(1));
  metrics.OnVmmSwapDisabled(now - base::Hours(10));
  metrics.OnVmmSwapDisabled(now);
}

TEST_F(VmmSwapMetricsTest, ReportDurationsDisabled) {
  VmmSwapMetrics metrics = VmmSwapMetrics(VmId::Type::ARCVM, GetMetricsRef(),
                                          std::move(heartbeat_timer_));
  base::Time now = base::Time::Now();

  metrics.OnSwappableIdleEnabled(now - base::Days(1));
  metrics.OnSwappableIdleEnabled(now - base::Hours(1));

  EXPECT_CALL(
      *metrics_,
      SendToUMA(kMetricsArcvmInactiveBeforeEnableDurationName, _, _, _, _))
      .Times(0);
  EXPECT_CALL(*metrics_,
              SendToUMA(kMetricsArcvmActiveAfterEnableDurationName, _, _, _, _))
      .Times(0);
  EXPECT_CALL(*metrics_,
              SendToUMA(kMetricsArcvmInactiveNoEnableDurationName, 24, _, _, _))
      .Times(1);
  metrics.OnVmmSwapDisabled(now);
}

TEST_F(VmmSwapMetricsTest, ReportDurationsDisabledClearEnabledLog) {
  VmmSwapMetrics metrics = VmmSwapMetrics(VmId::Type::ARCVM, GetMetricsRef(),
                                          std::move(heartbeat_timer_));
  base::Time now = base::Time::Now();

  metrics.OnSwappableIdleEnabled(now - base::Days(1));
  metrics.OnVmmSwapEnabled(now - base::Hours(15));
  metrics.OnVmmSwapDisabled(now - base::Hours(10));

  EXPECT_CALL(
      *metrics_,
      SendToUMA(kMetricsArcvmInactiveBeforeEnableDurationName, _, _, _, _))
      .Times(0);
  EXPECT_CALL(*metrics_,
              SendToUMA(kMetricsArcvmActiveAfterEnableDurationName, _, _, _, _))
      .Times(0);
  EXPECT_CALL(*metrics_,
              SendToUMA(kMetricsArcvmInactiveNoEnableDurationName, 10, _, _, _))
      .Times(1);
  metrics.OnVmmSwapDisabled(now);
}

TEST_F(VmmSwapMetricsTest, SendDurationToUMA) {
  VmmSwapMetrics metrics = VmmSwapMetrics(VmId::Type::ARCVM, GetMetricsRef(),
                                          std::move(heartbeat_timer_));
  base::Time now = base::Time::Now();
  const int min_duration = 1;
  const int max_duration = 24 * 28;  // 28days in hours
  const int buckets = 50;

  // Use kMetricsArcvmInactiveNoEnableDurationName for test
  metrics.OnSwappableIdleEnabled(now -
                                 (base::Hours(24) + base::Microseconds(1)));
  EXPECT_CALL(*metrics_, SendToUMA(kMetricsArcvmInactiveNoEnableDurationName,
                                   24, min_duration, max_duration, buckets))
      .Times(1);
  metrics.OnVmmSwapDisabled(now);
  metrics.OnSwappableIdleDisabled();

  metrics.OnSwappableIdleEnabled(now - base::Hours(24));
  EXPECT_CALL(*metrics_, SendToUMA(kMetricsArcvmInactiveNoEnableDurationName,
                                   24, min_duration, max_duration, buckets))
      .Times(1);
  metrics.OnVmmSwapDisabled(now);
  metrics.OnSwappableIdleDisabled();

  metrics.OnSwappableIdleEnabled(now -
                                 (base::Hours(24) - base::Microseconds(1)));
  EXPECT_CALL(*metrics_, SendToUMA(kMetricsArcvmInactiveNoEnableDurationName,
                                   23, min_duration, max_duration, buckets))
      .Times(1);
  metrics.OnVmmSwapDisabled(now);
  metrics.OnSwappableIdleDisabled();

  // Zero duration
  metrics.OnSwappableIdleEnabled(now);
  EXPECT_CALL(*metrics_, SendToUMA(kMetricsArcvmInactiveNoEnableDurationName, 0,
                                   min_duration, max_duration, buckets))
      .Times(1);
  metrics.OnVmmSwapDisabled(now);
  metrics.OnSwappableIdleDisabled();

  // Negative duration.
  metrics.OnSwappableIdleEnabled(now +
                                 (base::Hours(24) - base::Microseconds(1)));
  EXPECT_CALL(*metrics_,
              SendToUMA(kMetricsArcvmInactiveNoEnableDurationName, _, _, _, _))
      .Times(0);
  metrics.OnVmmSwapDisabled(now);
  metrics.OnSwappableIdleDisabled();
}

TEST_F(VmmSwapMetricsTest, ReportPagesInFile) {
  base::MockRepeatingTimer* heartbeat_timer = heartbeat_timer_.get();
  VmmSwapMetrics metrics = VmmSwapMetrics(VmId::Type::ARCVM, GetMetricsRef(),
                                          std::move(heartbeat_timer_));
  FakeFetchVmmSwapStatus loader;
  loader.result_ = true;
  loader.status_.state = SwapState::ACTIVE;
  metrics.SetFetchVmmSwapStatusFunction(loader.Create());
  metrics.OnSwappableIdleEnabled();
  metrics.OnVmmSwapEnabled();
  metrics.OnPreVmmSwapOut(100);

  loader.status_.metrics.swap_pages = 26;
  heartbeat_timer->Fire();
  loader.status_.metrics.swap_pages = 51;
  heartbeat_timer->Fire();

  const int min_pages = (50LL * 1024 * 1024) / 4096;        // 50MiB
  const int max_pages = (2LL * 1024 * 1024 * 1024) / 4096;  // 2GiB
  const int buckets = 50;
  const int min_pages_4KiB = 26 * base::GetPageSize() / 4096;
  const int avg_pages_4KiB = 59 * base::GetPageSize() / 4096;
  EXPECT_CALL(*metrics_,
              SendToUMA(kMetricsArcvmMinPagesInFileName, min_pages_4KiB,
                        min_pages, max_pages, buckets))
      .Times(1);
  EXPECT_CALL(*metrics_,
              SendToUMA(kMetricsArcvmAvgPagesInFileName, avg_pages_4KiB,
                        min_pages, max_pages, buckets))
      .Times(1);
  metrics.OnVmmSwapDisabled();
}

TEST_F(VmmSwapMetricsTest, ReportPagesInFileOnDestroy) {
  base::MockRepeatingTimer* heartbeat_timer = heartbeat_timer_.get();
  VmmSwapMetrics metrics = VmmSwapMetrics(VmId::Type::ARCVM, GetMetricsRef(),
                                          std::move(heartbeat_timer_));
  FakeFetchVmmSwapStatus loader;
  loader.result_ = true;
  loader.status_.state = SwapState::ACTIVE;
  metrics.SetFetchVmmSwapStatusFunction(loader.Create());
  metrics.OnSwappableIdleEnabled();
  metrics.OnVmmSwapEnabled();
  metrics.OnPreVmmSwapOut(100);

  loader.status_.metrics.swap_pages = 100;
  heartbeat_timer->Fire();

  const int pages_4KiB = 100 * base::GetPageSize() / (4 << 10);
  EXPECT_CALL(*metrics_,
              SendToUMA(kMetricsArcvmMinPagesInFileName, pages_4KiB, _, _, _))
      .Times(1);
  EXPECT_CALL(*metrics_,
              SendToUMA(kMetricsArcvmAvgPagesInFileName, pages_4KiB, _, _, _))
      .Times(1);
  metrics.OnDestroy();
}

TEST_F(VmmSwapMetricsTest, ReportPagesInFileOnDestroyAfterDisabled) {
  base::MockRepeatingTimer* heartbeat_timer = heartbeat_timer_.get();
  VmmSwapMetrics metrics = VmmSwapMetrics(VmId::Type::ARCVM, GetMetricsRef(),
                                          std::move(heartbeat_timer_));
  FakeFetchVmmSwapStatus loader;
  loader.result_ = true;
  loader.status_.state = SwapState::ACTIVE;
  metrics.SetFetchVmmSwapStatusFunction(loader.Create());
  metrics.OnSwappableIdleEnabled();
  metrics.OnVmmSwapEnabled();
  metrics.OnPreVmmSwapOut(100);

  loader.status_.metrics.swap_pages = 100;
  heartbeat_timer->Fire();
  metrics.OnVmmSwapDisabled();

  EXPECT_CALL(*metrics_, SendToUMA(kMetricsArcvmMinPagesInFileName, _, _, _, _))
      .Times(0);
  EXPECT_CALL(*metrics_, SendToUMA(kMetricsArcvmAvgPagesInFileName, _, _, _, _))
      .Times(0);
  metrics.OnDestroy();
}

TEST_F(VmmSwapMetricsTest, ReportPagesInFileWithoutLoader) {
  base::MockRepeatingTimer* heartbeat_timer = heartbeat_timer_.get();
  VmmSwapMetrics metrics = VmmSwapMetrics(VmId::Type::ARCVM, GetMetricsRef(),
                                          std::move(heartbeat_timer_));

  metrics.OnSwappableIdleEnabled();
  metrics.OnVmmSwapEnabled();
  heartbeat_timer->Fire();

  EXPECT_CALL(*metrics_, SendToUMA(kMetricsArcvmMinPagesInFileName, _, _, _, _))
      .Times(0);
  EXPECT_CALL(*metrics_, SendToUMA(kMetricsArcvmAvgPagesInFileName, _, _, _, _))
      .Times(0);
  metrics.OnVmmSwapDisabled();
}

TEST_F(VmmSwapMetricsTest, ReportPagesInFileNotActive) {
  base::MockRepeatingTimer* heartbeat_timer = heartbeat_timer_.get();
  VmmSwapMetrics metrics = VmmSwapMetrics(VmId::Type::ARCVM, GetMetricsRef(),
                                          std::move(heartbeat_timer_));
  FakeFetchVmmSwapStatus loader;
  loader.result_ = true;
  loader.status_.state = SwapState::READY;
  loader.status_.metrics.swap_pages = 100;
  metrics.SetFetchVmmSwapStatusFunction(loader.Create());
  metrics.OnSwappableIdleEnabled();
  metrics.OnVmmSwapEnabled();
  heartbeat_timer->Fire();

  EXPECT_CALL(*metrics_, SendToUMA(kMetricsArcvmMinPagesInFileName, _, _, _, _))
      .Times(0);
  EXPECT_CALL(*metrics_, SendToUMA(kMetricsArcvmAvgPagesInFileName, _, _, _, _))
      .Times(0);
  metrics.OnVmmSwapDisabled();
}

TEST_F(VmmSwapMetricsTest, ReportPagesInFileFailToLoadStatus) {
  base::MockRepeatingTimer* heartbeat_timer = heartbeat_timer_.get();
  VmmSwapMetrics metrics = VmmSwapMetrics(VmId::Type::ARCVM, GetMetricsRef(),
                                          std::move(heartbeat_timer_));
  FakeFetchVmmSwapStatus loader;
  loader.result_ = false;
  metrics.SetFetchVmmSwapStatusFunction(loader.Create());
  metrics.OnSwappableIdleEnabled();
  metrics.OnVmmSwapEnabled();
  heartbeat_timer->Fire();

  EXPECT_CALL(*metrics_, SendToUMA(kMetricsArcvmMinPagesInFileName, _, _, _, _))
      .Times(0);
  EXPECT_CALL(*metrics_, SendToUMA(kMetricsArcvmAvgPagesInFileName, _, _, _, _))
      .Times(0);
  metrics.OnVmmSwapDisabled();
}

TEST_F(VmmSwapMetricsTest, ReportPageDurationInFile) {
  base::MockRepeatingTimer* heartbeat_timer = heartbeat_timer_.get();
  VmmSwapMetrics metrics = VmmSwapMetrics(VmId::Type::ARCVM, GetMetricsRef(),
                                          std::move(heartbeat_timer_));
  FakeFetchVmmSwapStatus loader;
  loader.result_ = true;
  loader.status_.state = SwapState::ACTIVE;
  metrics.SetFetchVmmSwapStatusFunction(loader.Create());
  metrics.OnSwappableIdleEnabled();
  metrics.OnVmmSwapEnabled();
  metrics.OnPreVmmSwapOut(1000);

  // 200 pages : 200 seconds
  loader.status_.metrics.swap_pages = 800;
  task_environment_.FastForwardBy(base::Seconds(200));
  heartbeat_timer->Fire();
  // 400 pages : 400 seconds
  loader.status_.metrics.swap_pages = 400;
  task_environment_.FastForwardBy(base::Seconds(200));
  heartbeat_timer->Fire();
  // 800 pages : 200 seconds + 400 pages: total 800 seconds)
  task_environment_.FastForwardBy(base::Seconds(200));
  metrics.OnPreVmmSwapOut(1200);

  const int min_seconds = 10 * 60;         // 10 minutes
  const int max_seconds = 28 * 24 * 3600;  // 28 days
  const int buckets = 50;

  EXPECT_CALL(*metrics_, SendToUMA(kMetricsArcvmPageAverageDurationInFileName,
                                   377, min_seconds, max_seconds, buckets))
      .Times(1);
  task_environment_.FastForwardBy(base::Seconds(200));
  metrics.OnVmmSwapDisabled();
}

TEST_F(VmmSwapMetricsTest, ReportPageDurationInFileOnDestroy) {
  base::MockRepeatingTimer* heartbeat_timer = heartbeat_timer_.get();
  VmmSwapMetrics metrics = VmmSwapMetrics(VmId::Type::ARCVM, GetMetricsRef(),
                                          std::move(heartbeat_timer_));
  FakeFetchVmmSwapStatus loader;
  loader.result_ = true;
  loader.status_.state = SwapState::ACTIVE;
  metrics.SetFetchVmmSwapStatusFunction(loader.Create());
  metrics.OnSwappableIdleEnabled();
  metrics.OnVmmSwapEnabled();
  metrics.OnPreVmmSwapOut(1000);

  // 200 pages : 200 seconds
  loader.status_.metrics.swap_pages = 800;
  task_environment_.FastForwardBy(base::Seconds(200));
  heartbeat_timer->Fire();
  // 400 pages : 400 seconds
  loader.status_.metrics.swap_pages = 400;
  task_environment_.FastForwardBy(base::Seconds(200));
  heartbeat_timer->Fire();
  // 800 pages : 200 seconds + 400 pages: total 800 seconds)
  task_environment_.FastForwardBy(base::Seconds(200));
  metrics.OnPreVmmSwapOut(1200);

  EXPECT_CALL(*metrics_, SendToUMA(kMetricsArcvmPageAverageDurationInFileName,
                                   377, _, _, _))
      .Times(1);
  task_environment_.FastForwardBy(base::Seconds(200));
  metrics.OnDestroy();
}

TEST_F(VmmSwapMetricsTest, ReportPageDurationInFileZero) {
  base::MockRepeatingTimer* heartbeat_timer = heartbeat_timer_.get();
  VmmSwapMetrics metrics = VmmSwapMetrics(VmId::Type::ARCVM, GetMetricsRef(),
                                          std::move(heartbeat_timer_));
  FakeFetchVmmSwapStatus loader;
  loader.result_ = true;
  loader.status_.state = SwapState::ACTIVE;
  metrics.SetFetchVmmSwapStatusFunction(loader.Create());
  metrics.OnSwappableIdleEnabled();
  metrics.OnVmmSwapEnabled();
  metrics.OnPreVmmSwapOut(0);

  // Increased pages in file without OnPreVmmSwapOut are ignored.
  loader.status_.metrics.swap_pages = 100;
  task_environment_.FastForwardBy(base::Seconds(200));
  heartbeat_timer->Fire();

  EXPECT_CALL(*metrics_,
              SendToUMA(kMetricsArcvmPageAverageDurationInFileName, 0, _, _, _))
      .Times(1);
  task_environment_.FastForwardBy(base::Seconds(200));
  metrics.OnVmmSwapDisabled();
}

TEST_F(VmmSwapMetricsTest, ReportPageDurationInFileBeforeHeartbeat) {
  base::Time now = base::Time::Now();
  VmmSwapMetrics metrics = VmmSwapMetrics(VmId::Type::ARCVM, GetMetricsRef(),
                                          std::move(heartbeat_timer_));
  FakeFetchVmmSwapStatus loader;
  loader.result_ = true;
  loader.status_.state = SwapState::ACTIVE;
  metrics.SetFetchVmmSwapStatusFunction(loader.Create());
  metrics.OnSwappableIdleEnabled();
  metrics.OnVmmSwapEnabled();
  metrics.OnPreVmmSwapOut(1000, now - base::Seconds(1000));

  EXPECT_CALL(*metrics_, SendToUMA(kMetricsArcvmPageAverageDurationInFileName,
                                   1000, _, _, _))
      .Times(1);
  metrics.OnVmmSwapDisabled(now);
}

}  // namespace vm_tools::concierge
