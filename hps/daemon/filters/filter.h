// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HPS_DAEMON_FILTERS_FILTER_H_
#define HPS_DAEMON_FILTERS_FILTER_H_

#include <memory>

#include "base/callback.h"

namespace hps {
//
// Filter specifies an interface that can be specialized to provide advanced
// processing of HPS inferencing results.
//
class Filter {
 public:
  // The FilterResult indicates whether the ProcessResult or CurrentResult is
  // positive, negative or uncertain. Uncertain can happen when the result is
  // invalid or when the result is in certain range based on the implementation
  // of the Filter.
  enum class FilterResult {
    kUncertain,
    kPositive,
    kNegative,
  };

  Filter() = default;
  explicit Filter(FilterResult initial_state);
  Filter(const Filter&) = delete;
  Filter& operator=(const Filter&) = delete;
  virtual ~Filter() = default;

  // Process an inference result from HPS. Will only be called when there is:
  // - a new inference has been performed.
  // Parameters:
  // - result: the most recent inference result from HPS
  // - valid: whether this inference result is valid.
  // Returns:
  // - FilterResult: the result of the filtered inference. Depending on the
  // filter, implementation this can be a cumulative result.
  FilterResult ProcessResult(int result, bool valid);

  // Returns the current inference result of the filter. This is the same as
  // the last result that was returned from ProcessResult.
  FilterResult GetCurrentResult(void) const;

 protected:
  // Called from ProcessResult, derived filters should implement their filtering
  // logic in this method.
  virtual FilterResult ProcessResultImpl(int result, bool valid) = 0;

 private:
  FilterResult current_result_ = FilterResult::kUncertain;
};

}  // namespace hps

#endif  // HPS_DAEMON_FILTERS_FILTER_H_
