// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/utils/callback_barrier.h"

#include <ostream>
#include <utility>

#include <base/check_op.h>

namespace diagnostics {

CallbackBarrier::CallbackBarrier(base::OnceClosure on_success,
                                 base::OnceClosure on_error)
    : tracker_(base::MakeRefCounted<CallbackBarrier::Tracker>(
          std::move(on_success), std::move(on_error))) {}

CallbackBarrier::~CallbackBarrier() = default;

CallbackBarrier::Tracker::Tracker(base::OnceClosure on_success,
                                  base::OnceClosure on_error)
    : on_success_(std::move(on_success)), on_error_(std::move(on_error)) {}

CallbackBarrier::Tracker::~Tracker() {
  if (num_uncalled_callback_ == 0) {
    std::move(on_success_).Run();
  } else {
    std::move(on_error_).Run();
  }
}

void CallbackBarrier::Tracker::IncreaseUncalledCallbackNum() {
  ++num_uncalled_callback_;
}

void CallbackBarrier::Tracker::DecreaseUncalledCallbackNum() {
  CHECK_GE(num_uncalled_callback_, 1)
      << "This should never be called when the counter is 0";
  --num_uncalled_callback_;
}

}  // namespace diagnostics
