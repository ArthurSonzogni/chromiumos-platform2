// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include "hps/daemon/filters/filter_watcher.h"

namespace hps {

FilterWatcher::FilterWatcher(std::unique_ptr<Filter> wrapped_filter,
                             StatusCallback signal)
    : wrapped_filter_(std::move(wrapped_filter)),
      status_changed_callback_(std::move(signal)) {}

bool FilterWatcher::ProcessResultImpl(int result) {
  bool previous_filter_result = wrapped_filter_->GetCurrentResult();
  int filter_result = wrapped_filter_->ProcessResult(result);

  // TODO(slangley): We might want to fire the callback the first time through
  // so clients can establish current state.
  if (filter_result != previous_filter_result) {
    status_changed_callback_.Run(filter_result);
  }

  return filter_result;
}

}  // namespace hps
