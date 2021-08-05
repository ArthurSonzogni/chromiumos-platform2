// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "vm_tools/concierge/balloon_policy.h"

#include <algorithm>
#include <assert.h>

#include <base/check.h>
#include <base/logging.h>

namespace vm_tools {
namespace concierge {

BalanceAvailableBalloonPolicy::BalanceAvailableBalloonPolicy(
    int64_t critical_host_available,
    int64_t guest_available_bias,
    const std::string& vm)
    : critical_host_available_(critical_host_available),
      guest_available_bias_(guest_available_bias),
      max_balloon_actual_(0),
      critical_guest_available_(400 * MIB) {
  LOG(INFO) << "BalloonInit: { "
            << "\"type\": \"BalanceAvailableBalloonPolicy\","
            << "\"vm\": \"" << vm << "\","
            << "\"critical_margin\": " << critical_host_available << ","
            << "\"bias\": " << guest_available_bias << " }";
}

int64_t BalanceAvailableBalloonPolicy::ComputeBalloonDelta(
    const BalloonStats& stats,
    uint64_t host_available,
    bool game_mode,
    const std::string& vm) {
  // returns delta size of balloon
  constexpr int64_t MAX_CRITICAL_DELTA = 10 * MIB;

  const int64_t balloon_actual = stats.balloon_actual;
  const int64_t guest_free = stats.free_memory;
  const int64_t guest_cached = stats.disk_caches;
  const int64_t guest_total = stats.total_memory;

  // NB: max_balloon_actual_ should start at a resonably high value, but we
  // don't know how much memory the guest has until we get some BalloonStats, so
  // update it here instead of the constructor.
  if (max_balloon_actual_ == 0) {
    max_balloon_actual_ = (guest_total * 3) / 4;
  }
  max_balloon_actual_ = std::max(max_balloon_actual_, balloon_actual);

  const int64_t guest_available = guest_free + guest_cached;
  const int64_t balloon_full_percent =
      max_balloon_actual_ > 0 ? balloon_actual * 100 / max_balloon_actual_ : 0;

  if (guest_available < critical_guest_available_ &&
      balloon_full_percent < 95) {
    if (prev_guest_available_ < critical_guest_available_ &&
        prev_balloon_full_percent_ < 95) {
      critical_guest_available_ = prev_guest_available_;
    }
  }

  const int64_t bias = guest_available_bias_ * balloon_full_percent / 100;
  const int64_t guest_above_critical =
      guest_available - critical_guest_available_ - bias;
  const int64_t host_above_critical = host_available - critical_host_available_;
  const int64_t balloon_delta = guest_above_critical - host_above_critical;

  // To avoid killing apps accidentally, cap the delta here by leaving the space
  // MAX_CRITICAL_DELTA;
  // We can remove this if clause
  // TODO(hikalium): Consider changing 2nd argument of clamp to
  // guest_above_critical + MAX_CRITICAL_DELTA
  const int64_t balloon_delta_capped = std::clamp(
      balloon_delta, -(host_above_critical + MAX_CRITICAL_DELTA),
      guest_available - critical_guest_available_ + MAX_CRITICAL_DELTA);

  prev_guest_available_ = guest_available;
  prev_balloon_full_percent_ = balloon_full_percent;

  const int64_t balloon_delta_abs =
      std::abs(balloon_delta);  // should be balloon_delta_capped???
  // Only return a value if target would change available above critical
  // by more than 1%, or we are within 1 MB of critical in host or guest.
  // Division by guest_above_critical and host_above_critical here are
  // safe since they will not be evaluated on that condition.
  if (guest_above_critical < 1 * MIB || host_above_critical < 1 * MIB ||
      balloon_delta_abs * 100 / guest_above_critical > 1 ||
      balloon_delta_abs * 100 / host_above_critical > 1) {
    // Finally, make sure the balloon delta won't cause a negative size.
    const int64_t delta = std::max(balloon_delta_capped, -balloon_actual);
    LOG(INFO) << "BalloonTrace: { "
              << "\"vm\": \"" << vm << "\", "
              << "\"game_mode\": " << (game_mode ? "true" : "false") << ", "
              << "\"balloon\": " << balloon_actual << ", "
              << "\"guest_cached\": " << guest_cached << ", "
              << "\"guest_free\": " << guest_free << ", "
              << "\"host_available\": " << host_available << ", "
              << "\"delta\": " << delta << " }";
    return delta;
  }

  return 0;
}

}  // namespace concierge
}  // namespace vm_tools
