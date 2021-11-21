// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>
#include <vector>

#include "base/test/bind.h"
#include "gtest/gtest.h"
#include "hps/daemon/filters/average_filter.h"
#include "hps/daemon/filters/consecutive_results_filter.h"
#include "hps/daemon/filters/filter.h"
#include "hps/daemon/filters/filter_factory.h"
#include "hps/daemon/filters/filter_watcher.h"
#include "hps/daemon/filters/threshold_filter.h"
#include "hps/proto_bindings/hps_service.pb.h"

namespace hps {

using FilterResult = Filter::FilterResult;
constexpr int kThreshold = 5;

TEST(HpsFilterTest, ThresholdFilterTest) {
  ThresholdFilter filter(kThreshold);

  EXPECT_EQ(filter.ProcessResult(kThreshold - 1, true),
            FilterResult::kNegative);
  EXPECT_EQ(filter.ProcessResult(kThreshold + 1, true),
            FilterResult::kPositive);
  EXPECT_EQ(filter.ProcessResult(kThreshold + 1, false),
            FilterResult::kUncertain);
}

TEST(HpsFilterTest, FilterWatcherTest) {
  bool cb_result = false;
  base::RepeatingCallback<void(bool)> callback =
      base::BindLambdaForTesting([&](bool result) { cb_result = result; });

  auto filter = std::make_unique<ThresholdFilter>(kThreshold);
  FilterWatcher watcher(std::move(filter), callback);

  EXPECT_EQ(watcher.ProcessResult(kThreshold - 1, true),
            FilterResult::kNegative);
  EXPECT_EQ(watcher.ProcessResult(kThreshold + 1, true),
            FilterResult::kPositive);
  EXPECT_TRUE(cb_result);
  EXPECT_EQ(watcher.ProcessResult(kThreshold - 1, true),
            FilterResult::kNegative);
  EXPECT_FALSE(cb_result);
  EXPECT_EQ(watcher.ProcessResult(kThreshold - 1, false),
            FilterResult::kUncertain);
  EXPECT_FALSE(cb_result);
}

TEST(HpsFilterTest, ConsecutiveResultsFilterTest) {
  FeatureConfig::ConsecutiveResultsFilterConfig config;

  config.set_positive_score_threshold(10);
  config.set_negative_score_threshold(4);

  config.set_positive_count_threshold(1);
  config.set_negative_count_threshold(2);
  config.set_uncertain_count_threshold(3);

  ConsecutiveResultsFilter filter(config);

  const int positive_score = 10;
  const int negative_score = 3;
  const int uncertain_score = 5;

  // Only need one positive value to change the state.
  EXPECT_EQ(filter.ProcessResult(positive_score, true),
            FilterResult::kPositive);

  // One negative value will not change the state.
  EXPECT_EQ(filter.ProcessResult(negative_score, true),
            FilterResult::kPositive);
  // Two negative values will change the state.
  EXPECT_EQ(filter.ProcessResult(negative_score, true),
            FilterResult::kNegative);

  // One uncertain value will not change the state.
  EXPECT_EQ(filter.ProcessResult(uncertain_score, true),
            FilterResult::kNegative);
  // Two uncertain values will not change the state.
  EXPECT_EQ(filter.ProcessResult(uncertain_score, true),
            FilterResult::kNegative);
  // Three uncertain values will change the state.
  EXPECT_EQ(filter.ProcessResult(uncertain_score, true),
            FilterResult::kUncertain);

  // Only need one positive value to change the state.
  EXPECT_EQ(filter.ProcessResult(positive_score, true),
            FilterResult::kPositive);

  // Alternating between negative_scores and uncertain_scores without reaching
  // count_threshold will not change the state.
  const std::vector<int> scores = {negative_score,  uncertain_score,
                                   uncertain_score, negative_score,
                                   uncertain_score, uncertain_score};
  for (const int score : scores) {
    EXPECT_EQ(filter.ProcessResult(score, true), FilterResult::kPositive);
  }

  // This resets the internal consecutive_count_.
  EXPECT_EQ(filter.ProcessResult(positive_score, true),
            FilterResult::kPositive);

  // One uncertain value will not change the state.
  EXPECT_EQ(filter.ProcessResult(uncertain_score, true),
            FilterResult::kPositive);
  // Invalid value is treated as uncertain.
  EXPECT_EQ(filter.ProcessResult(negative_score, false),
            FilterResult::kPositive);
  // Invalid value is treated as uncertain and three uncertain changes the state
  EXPECT_EQ(filter.ProcessResult(negative_score, false),
            FilterResult::kUncertain);
}

TEST(HpsFilterTest, ConsecutiveResultsFilterCompatibleTest) {
  FeatureConfig::ConsecutiveResultsFilterConfig config;

  config.set_threshold(10);
  config.set_count(2);

  ConsecutiveResultsFilter filter(config);

  const int positive_score = 10;
  const int negative_score = 9;

  // One positive value will not change the state.
  EXPECT_EQ(filter.ProcessResult(positive_score, true),
            FilterResult::kUncertain);
  // Two positive value will change the state.
  EXPECT_EQ(filter.ProcessResult(positive_score, true),
            FilterResult::kPositive);

  // One negative value will not change the state.
  EXPECT_EQ(filter.ProcessResult(negative_score, true),
            FilterResult::kPositive);
  // Two negative values will change the state.
  EXPECT_EQ(filter.ProcessResult(negative_score, true),
            FilterResult::kNegative);

  // One uncertain value will not change the state.
  EXPECT_EQ(filter.ProcessResult(negative_score, false),
            FilterResult::kNegative);
  // Two uncertain values will change the state.
  EXPECT_EQ(filter.ProcessResult(negative_score, false),
            FilterResult::kUncertain);
}

TEST(HpsFilterTest, AverageFilter) {
  FeatureConfig::AverageFilterConfig config;
  config.set_average_window_size(3);
  config.set_positive_score_threshold(10);
  config.set_negative_score_threshold(4);
  config.set_default_uncertain_score(8);

  AverageFilter filter(config);

  // Average is 10;
  EXPECT_EQ(filter.ProcessResult(10, true), FilterResult::kPositive);
  // Average is 5;
  EXPECT_EQ(filter.ProcessResult(0, true), FilterResult::kUncertain);
  // Average is 3;
  EXPECT_EQ(filter.ProcessResult(0, true), FilterResult::kNegative);
  // Average is 5;
  EXPECT_EQ(filter.ProcessResult(15, true), FilterResult::kUncertain);
  // Average is 7;
  EXPECT_EQ(filter.ProcessResult(6, true), FilterResult::kUncertain);
  // Average is 10;
  EXPECT_EQ(filter.ProcessResult(9, true), FilterResult::kPositive);
  // Average is 7, not 12; since default_uncertain_score is used if invalid.
  EXPECT_EQ(filter.ProcessResult(21, false), FilterResult::kUncertain);
}

}  // namespace hps
