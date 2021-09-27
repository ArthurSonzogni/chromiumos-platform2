// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HPS_DAEMON_FILTERS_FILTER_WATCHER_H_
#define HPS_DAEMON_FILTERS_FILTER_WATCHER_H_

#include <memory>

#include "hps/daemon/filters/filter.h"
#include "hps/daemon/filters/status_callback.h"

namespace hps {

// FilterWatcher will invoke the StatusCallback whenever the composed filter
// changes state.
class FilterWatcher : public Filter {
 public:
  FilterWatcher(std::unique_ptr<Filter> wrapped_filter, StatusCallback signal);
  FilterWatcher(const FilterWatcher&) = delete;
  FilterWatcher& operator=(const FilterWatcher&) = delete;
  virtual ~FilterWatcher() = default;

 private:
  // Metehods for Filter
  bool ProcessResultImpl(int result) override;

  std::unique_ptr<Filter> wrapped_filter_;
  StatusCallback status_changed_callback_;
};

}  // namespace hps

#endif  // HPS_DAEMON_FILTERS_FILTER_WATCHER_H_
