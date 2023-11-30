// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SWAP_MANAGEMENT_ZRAM_IDLE_H_
#define SWAP_MANAGEMENT_ZRAM_IDLE_H_

#include <absl/status/status.h>
#include <base/time/time.h>

namespace swap_management {

absl::Status MarkIdle(uint32_t age_seconds);
std::optional<uint64_t> GetCurrentIdleTimeSec(uint64_t min_sec,
                                              uint64_t max_sec);

}  // namespace swap_management

#endif  // SWAP_MANAGEMENT_ZRAM_IDLE_H_
