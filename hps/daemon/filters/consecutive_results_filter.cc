// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hps/daemon/filters/consecutive_results_filter.h"

namespace hps {

ConsecutiveResultsFilter::ConsecutiveResultsFilter(
    const FeatureConfig::ConsecutiveResultsFilterConfig& config)
    : config_(config) {
  if (config.count() > 0) {
    config_.set_positive_count_threshold(config.count());
    config_.set_negative_count_threshold(config.count());
    config_.set_uncertain_count_threshold(config.count());
  }
  if (config.threshold() != 0) {
    config_.set_positive_score_threshold(config.threshold());
    config_.set_negative_score_threshold(config.threshold());
  }
}

Filter::FilterResult ConsecutiveResultsFilter::ProcessResultImpl(int result,
                                                                 bool valid) {
  FilterResult inference_result = FilterResult::kUncertain;
  if (valid && result >= config_.positive_score_threshold()) {
    // If result is valid and above the positive threshold, then
    // inference_result
    // is positive.
    inference_result = FilterResult::kPositive;
  } else if (valid && result < config_.negative_score_threshold()) {
    // If result is valid and below the negative threshold, then
    // inference_result
    // is positive.
    inference_result = FilterResult::kNegative;
  }

  // If current inference_result is the same as consecutive_result_; then
  // increment the counter; otherwise restart the counter.
  if (inference_result == consecutive_result_) {
    consecutive_count_++;
  } else {
    consecutive_result_ = inference_result;
    consecutive_count_ = 1;
  }

  // Compare consecutive_count_ with each of the count_threshold.
  if (consecutive_result_ == FilterResult::kPositive &&
      consecutive_count_ >= config_.positive_count_threshold()) {
    return FilterResult::kPositive;
  } else if (consecutive_result_ == FilterResult::kNegative &&
             consecutive_count_ >= config_.negative_count_threshold()) {
    return FilterResult::kNegative;
  } else if (consecutive_result_ == FilterResult::kUncertain &&
             consecutive_count_ >= config_.uncertain_count_threshold()) {
    return FilterResult::kUncertain;
  }

  return GetCurrentResult();
}

}  // namespace hps
