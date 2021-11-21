// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hps/daemon/filters/filter.h"

namespace hps {

Filter::Filter(Filter::FilterResult initial_state)
    : current_result_(initial_state) {}

Filter::FilterResult Filter::ProcessResult(int result, bool valid) {
  current_result_ = ProcessResultImpl(result, valid);
  return current_result_;
}

Filter::FilterResult Filter::GetCurrentResult(void) const {
  return current_result_;
}

}  // namespace hps
