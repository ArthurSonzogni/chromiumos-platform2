// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hps/daemon/filters/consecutive_results_filter.h"

namespace hps {

ConsecutiveResultsFilter::ConsecutiveResultsFilter(int threshold,
                                                   int count,
                                                   bool initial_state)
    : Filter(initial_state), threshold_(threshold), count_(count) {}

bool ConsecutiveResultsFilter::ProcessResultImpl(int result) {
  bool inference_result = (result > threshold_);

  if (inference_result) {
    count_below_threshold_ = 0;
    if (count_above_threshold_ < count_) {
      count_above_threshold_++;
    }
  } else {
    count_above_threshold_ = 0;
    if (count_below_threshold_ < count_) {
      count_below_threshold_++;
    }
  }

  bool current_filter_result = GetCurrentResult();

  if (current_filter_result) {
    if (count_below_threshold_ >= count_) {
      return false;
    }
  } else {
    if (count_above_threshold_ >= count_) {
      return true;
    }
  }

  return current_filter_result;
}

}  // namespace hps
