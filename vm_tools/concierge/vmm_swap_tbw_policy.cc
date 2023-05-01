// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/vmm_swap_tbw_policy.h"

#include <algorithm>
#include <cstdint>
#include <utility>

#include <base/containers/ring_buffer.h>
#include <base/time/time.h>

namespace vm_tools::concierge {

VmmSwapTbwPolicy::VmmSwapTbwPolicy() {
  // Push a sentinel. VmmSwapTbwPolicy::Record() checks the latest entry by
  // `tbw_history_.MutableReadBuffer()` which fails if current index is 0.
  tbw_history_.SaveToBuffer(std::make_pair(base::Time(), 0));
}

void VmmSwapTbwPolicy::SetTargetTbwPerDay(uint64_t target_tbw_per_day) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  target_tbw_per_day_ = target_tbw_per_day;
}

void VmmSwapTbwPolicy::Record(uint64_t bytes_written, base::Time time) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto latest_entry =
      tbw_history_.MutableReadBuffer(tbw_history_.BufferSize() - 1);
  if ((time - latest_entry->first) > base::Hours(24)) {
    tbw_history_.SaveToBuffer(std::make_pair(time, bytes_written));
  } else {
    latest_entry->second += bytes_written;
  }
}

bool VmmSwapTbwPolicy::CanSwapOut(base::Time time) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  uint64_t tbw_28days = 0, tbw_7days = 0, tbw_1day = 0;
  for (auto iter = tbw_history_.Begin(); iter; ++iter) {
    if ((time - iter->first) < base::Days(28)) {
      tbw_28days += iter->second;
    }
    if ((time - iter->first) < base::Days(7)) {
      tbw_7days += iter->second;
    }
    if ((time - iter->first) < base::Days(1)) {
      tbw_1day += iter->second;
    }
  }

  // The targets for recent time ranges are eased using scale factor.
  // target_tbw_per_day_ * <num_days> * <scale_factor>
  uint64_t target_28days = target_tbw_per_day_ * 28 * 1;
  uint64_t target_7days = target_tbw_per_day_ * 7 * 2;
  uint64_t target_1day = target_tbw_per_day_ * 1 * 4;
  return tbw_28days < target_28days && tbw_7days < target_7days &&
         tbw_1day < target_1day;
}

}  // namespace vm_tools::concierge
