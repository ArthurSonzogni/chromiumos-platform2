// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HPS_DAEMON_FILTERS_THRESHOLD_FILTER_H_
#define HPS_DAEMON_FILTERS_THRESHOLD_FILTER_H_

#include <memory>

#include "base/callback.h"
#include "hps/daemon/filters/filter.h"

namespace hps {

// A filter that compares the inference result against a fixed threshold.
class ThresholdFilter : public Filter {
 public:
  explicit ThresholdFilter(int threshold);
  ThresholdFilter(const ThresholdFilter&) = delete;
  ThresholdFilter& operator=(const ThresholdFilter&) = delete;
  virtual ~ThresholdFilter() = default;

 private:
  // Metehods for Filter
  bool ProcessResultImpl(int result) override;

  const int threshold_;
};

}  // namespace hps

#endif  // HPS_DAEMON_FILTERS_THRESHOLD_FILTER_H_
