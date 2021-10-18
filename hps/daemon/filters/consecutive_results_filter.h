// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HPS_DAEMON_FILTERS_CONSECUTIVE_RESULTS_FILTER_H_
#define HPS_DAEMON_FILTERS_CONSECUTIVE_RESULTS_FILTER_H_

#include <memory>

#include "base/callback.h"
#include "hps/daemon/filters/filter.h"

namespace hps {

// A filter that compares the inference result against a fixed threshold.
class ConsecutiveResultsFilter : public Filter {
 public:
  ConsecutiveResultsFilter(int threshold, int count, bool initial_state);
  ConsecutiveResultsFilter(const ConsecutiveResultsFilter&) = delete;
  ConsecutiveResultsFilter& operator=(const ConsecutiveResultsFilter&) = delete;
  ~ConsecutiveResultsFilter() override = default;

 private:
  // Metehods for Filter
  bool ProcessResultImpl(int result) override;

  const int threshold_;
  const int count_;
  int count_above_threshold_ = 0;
  int count_below_threshold_ = 0;
};

}  // namespace hps

#endif  // HPS_DAEMON_FILTERS_CONSECUTIVE_RESULTS_FILTER_H_
