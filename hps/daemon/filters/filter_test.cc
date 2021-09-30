// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include "base/test/bind.h"
#include "gtest/gtest.h"
#include "hps/daemon/filters/consecutive_results_filter.h"
#include "hps/daemon/filters/filter.h"
#include "hps/daemon/filters/filter_factory.h"
#include "hps/daemon/filters/filter_watcher.h"
#include "hps/daemon/filters/threshold_filter.h"

namespace hps {

constexpr int kThreshold = 5;

TEST(ThresholdFilterTest, BasicTest) {
  ThresholdFilter filter(kThreshold);

  EXPECT_FALSE(filter.ProcessResult(kThreshold - 1));
  EXPECT_FALSE(filter.GetCurrentResult());
  EXPECT_TRUE(filter.ProcessResult(kThreshold + 1));
  EXPECT_TRUE(filter.GetCurrentResult());
}

TEST(FilterWatcherTest, BasicTest) {
  bool cb_result = false;
  base::RepeatingCallback<void(bool)> callback =
      base::BindLambdaForTesting([&](bool result) { cb_result = result; });

  auto filter = std::make_unique<ThresholdFilter>(kThreshold);
  FilterWatcher watcher(std::move(filter), callback);

  EXPECT_FALSE(watcher.ProcessResult(kThreshold - 1));
  EXPECT_FALSE(watcher.GetCurrentResult());
  EXPECT_TRUE(watcher.ProcessResult(kThreshold + 1));
  EXPECT_TRUE(watcher.GetCurrentResult());
  EXPECT_TRUE(cb_result);
  EXPECT_FALSE(watcher.ProcessResult(kThreshold - 1));
  EXPECT_FALSE(watcher.GetCurrentResult());
  EXPECT_FALSE(cb_result);
}

TEST(ConsecutiveResultsFilterTest, BasicTest) {
  constexpr int kCount = 5;
  ConsecutiveResultsFilter filter(kThreshold, kCount, false);

  EXPECT_FALSE(filter.GetCurrentResult());

  // First kCount-1 above threshold should not change filter state.
  for (int i = 0; i < kCount - 1; i++) {
    EXPECT_FALSE(filter.ProcessResult(kThreshold + 1));
    EXPECT_FALSE(filter.GetCurrentResult());
  }

  EXPECT_TRUE(filter.ProcessResult(kThreshold + 1));
  EXPECT_TRUE(filter.GetCurrentResult());

  // First kCount-1 below threshold should not change filter state.
  for (int i = 0; i < kCount - 1; i++) {
    EXPECT_TRUE(filter.ProcessResult(kThreshold));
    EXPECT_TRUE(filter.GetCurrentResult());
  }

  EXPECT_FALSE(filter.ProcessResult(kThreshold));
  EXPECT_FALSE(filter.GetCurrentResult());
}

TEST(ConsecutiveResultsFilterTest, ResetCountOnChange) {
  constexpr int kCount = 5;
  ConsecutiveResultsFilter filter(kThreshold, kCount, false);

  EXPECT_FALSE(filter.GetCurrentResult());

  // First kCount-1 above threshold should not change filter state.
  for (int i = 0; i < kCount - 1; i++) {
    EXPECT_FALSE(filter.ProcessResult(kThreshold + 1));
    EXPECT_FALSE(filter.GetCurrentResult());
  }
  // Sending one result below the threshold should reset the count
  EXPECT_FALSE(filter.ProcessResult(kThreshold));
  EXPECT_FALSE(filter.GetCurrentResult());

  // First kCount-1 above threshold should not change filter state.
  for (int i = 0; i < kCount - 1; i++) {
    EXPECT_FALSE(filter.ProcessResult(kThreshold + 1));
    EXPECT_FALSE(filter.GetCurrentResult());
  }

  EXPECT_TRUE(filter.ProcessResult(kThreshold + 1));
  EXPECT_TRUE(filter.GetCurrentResult());

  // Now test going from above threshold to below threshold.
  // First kCount below threshold should not change filter state.
  for (int i = 0; i < kCount - 1; i++) {
    EXPECT_TRUE(filter.ProcessResult(kThreshold));
    EXPECT_TRUE(filter.GetCurrentResult());
  }

  EXPECT_TRUE(filter.ProcessResult(kThreshold + 1));
  EXPECT_TRUE(filter.GetCurrentResult());

  // First kCount below threshold should not change filter state.
  for (int i = 0; i < kCount - 1; i++) {
    EXPECT_TRUE(filter.ProcessResult(kThreshold));
    EXPECT_TRUE(filter.GetCurrentResult());
  }
}

}  // namespace hps
