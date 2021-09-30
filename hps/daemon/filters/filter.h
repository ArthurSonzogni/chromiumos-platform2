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
  Filter() = default;
  explicit Filter(bool initial_state);
  Filter(const Filter&) = delete;
  Filter& operator=(const Filter&) = delete;
  virtual ~Filter() = default;

  // Process an inference result from HPS. Will only be called when there is:
  // - a valid result from HPS
  // - a new inference has been performed.
  // Parameters:
  // - result: the most recent inference result from HPS
  // Returns:
  // - bool: That result of the filtered inference. Depending on the filter
  //   implementation this can be a cumulative result.
  bool ProcessResult(int result);

  // Returns the current inference result of the filter. This is the same as
  // the last result that was returned from ProcessResult.
  bool GetCurrentResult(void) const;

 protected:
  // Called from ProcessResult, derived filters should implement their filtering
  // logic in this method.
  virtual bool ProcessResultImpl(int result) = 0;

 private:
  bool current_result_ = false;
};

}  // namespace hps

#endif  // HPS_DAEMON_FILTERS_FILTER_H_
