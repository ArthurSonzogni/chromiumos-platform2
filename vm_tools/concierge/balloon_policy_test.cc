// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/balloon_policy.h"
#include "vm_tools/concierge/vm_util.h"

#include <string>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/json/json_reader.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace vm_tools {
namespace concierge {

// Test that having no limits still inflates the balloon to reduce excess free.
TEST(BalloonPolicyTest, LimitCacheNoLimit) {
  const int64_t host_lwm = 200 * MIB;
  const int64_t guest_lwm = 200 * MIB;
  const MemoryMargins margins = {.critical = 400 * MIB, .moderate = 2000 * MIB};
  const LimitCacheBalloonPolicy::Params params = {.reclaim_target_cache = 0,
                                                  .critical_target_cache = 0,
                                                  .moderate_target_cache = 0};
  LimitCacheBalloonPolicy policy(margins, host_lwm, guest_lwm, params, "test");

  // NB: Because there are no cache limits, target_free will always be
  // MaxFree().

  // Test that we don't inflate the balloon if it's just a little bit.
  {
    const BalloonStats stats = {.disk_caches = 0,
                                .free_memory = policy.MaxFree() + MIB};
    EXPECT_EQ(0, policy.ComputeBalloonDeltaImpl(0 /* host_free */, stats,
                                                0 /* host_available */, false,
                                                "test"));
  }

  // Test that we do inflate the balloon if it's a lot (more than MinFree()).
  {
    const BalloonStats stats = {
        .disk_caches = 0, .free_memory = policy.MaxFree() + policy.MinFree()};
    EXPECT_EQ(policy.MinFree(), policy.ComputeBalloonDeltaImpl(
                                    0 /* host_free */, stats,
                                    0 /* host_available */, false, "test"));
  }

  // Test that we deflate the balloon even if we just need a little bit.
  {
    const BalloonStats stats = {.disk_caches = 0,
                                .free_memory = policy.MaxFree() - MIB};
    EXPECT_EQ(-MIB, policy.ComputeBalloonDeltaImpl(0 /* host_free */, stats,
                                                   0 /* host_available */,
                                                   false, "test"));
  }
}

// Tests that moderate_target_cache works as expected.
TEST(BalloonPolicyTest, LimitCacheModerate) {
  // Values are roughly what a 4GB ARCVM would get (but rounded)
  const int64_t host_lwm = 200 * MIB;
  const int64_t guest_lwm = 200 * MIB;
  MemoryMargins margins = {.critical = 400 * MIB, .moderate = 2000 * MIB};
  const LimitCacheBalloonPolicy::Params params = {
      .reclaim_target_cache = 0,
      .critical_target_cache = 0,
      .moderate_target_cache = 200 * MIB};
  LimitCacheBalloonPolicy policy(margins, host_lwm, guest_lwm, params, "test");

  // limit_start is the host_available level below which we start limiting
  // guest memory.
  const uint64_t limit_start = margins.moderate + policy.MaxFree() - guest_lwm;

  // Test that we inflate the balloon a bit when we start getting a bit close
  // to the moderate margin.
  {
    BalloonStats stats = {.disk_caches = 1000 * MIB,
                          .free_memory = policy.MaxFree()};
    EXPECT_EQ(MIB, policy.ComputeBalloonDeltaImpl(
                       0 /* host_free */, stats,
                       limit_start - MIB /* host_available */, false, "test"));
  }

  // Test that when there is less cache left than the distance to target free,
  // we only inflate the balloon enough to reclaim that cache.
  {
    BalloonStats stats = {.disk_caches = 300 * MIB,
                          .free_memory = policy.MaxFree()};
    const int64_t cache_above_limit =
        stats.disk_caches - params.moderate_target_cache;
    EXPECT_EQ(cache_above_limit,
              policy.ComputeBalloonDeltaImpl(
                  0 /* host_free */, stats,
                  margins.moderate /* host_available */, false, "test"));
  }

  // Test that when we are way below the moderate margin, we still give the
  // guest MinFree() memory.
  {
    BalloonStats stats = {.disk_caches = 1000 * MIB,
                          .free_memory = policy.MaxFree()};
    const int64_t free_above_min = stats.free_memory - policy.MinFree();
    EXPECT_EQ(free_above_min, policy.ComputeBalloonDeltaImpl(
                                  0 /* host_free */, stats,
                                  0 /* host_available */, false, "test"));
  }
}

// Tests that critical_target_cache works as expected.
TEST(BalloonPolicyTest, LimitCacheCritical) {
  // Values are roughly what a 4GB ARCVM would get (but rounded)
  const int64_t host_lwm = 200 * MIB;
  const int64_t guest_lwm = 200 * MIB;
  MemoryMargins margins = {.critical = 400 * MIB, .moderate = 2000 * MIB};
  const LimitCacheBalloonPolicy::Params params = {
      .reclaim_target_cache = 0,
      .critical_target_cache = 100 * MIB,
      .moderate_target_cache = 0};
  LimitCacheBalloonPolicy policy(margins, host_lwm, guest_lwm, params, "test");

  // limit_start is the host_available level below which we start limiting
  // guest memory.
  const uint64_t limit_start = margins.critical + policy.MaxFree() - guest_lwm;

  // Test that we inflate the balloon a bit when we start getting a bit close
  // to the critical margin.
  {
    BalloonStats stats = {.disk_caches = 1000 * MIB,
                          .free_memory = policy.MaxFree()};
    EXPECT_EQ(MIB, policy.ComputeBalloonDeltaImpl(
                       0 /* host_free */, stats,
                       limit_start - MIB /* host_available */, false, "test"));
  }

  // Test that when there is less cache left than the distance to target free,
  // we only inflate the balloon enough to reclaim that cache.
  {
    BalloonStats stats = {.disk_caches = 150 * MIB,
                          .free_memory = policy.MaxFree()};
    const int64_t cache_above_limit =
        stats.disk_caches - params.critical_target_cache;
    EXPECT_EQ(cache_above_limit,
              policy.ComputeBalloonDeltaImpl(
                  0 /* host_free */, stats,
                  margins.critical /* host_available */, false, "test"));
  }

  // Test that when we are way below the critical margin, we still give the
  // guest MinFree() memory.
  {
    BalloonStats stats = {.disk_caches = 1000 * MIB,
                          .free_memory = policy.MaxFree()};
    const int64_t free_above_min = stats.free_memory - policy.MinFree();
    EXPECT_EQ(free_above_min, policy.ComputeBalloonDeltaImpl(
                                  0 /* host_free */, stats,
                                  0 /* host_available */, false, "test"));
  }
}

// Tests that reclaim_target_cache works as expected.
TEST(BalloonPolicyTest, LimitCacheReclaim) {
  // Values are roughly what a 4GB ARCVM would get (but rounded)
  const int64_t host_lwm = 200 * MIB;
  const int64_t guest_lwm = 200 * MIB;
  MemoryMargins margins = {.critical = 400 * MIB, .moderate = 2000 * MIB};
  const LimitCacheBalloonPolicy::Params params = {
      .reclaim_target_cache = 100 * MIB,
      .critical_target_cache = 0,
      .moderate_target_cache = 0};
  LimitCacheBalloonPolicy policy(margins, host_lwm, guest_lwm, params, "test");

  // limit_start is the host_free level below which we start limiting
  // guest memory.
  const uint64_t limit_start = host_lwm + policy.MaxFree() - guest_lwm;

  // Test that we inflate the balloon a bit when we start getting a bit close
  // to reclaiming in the host.
  {
    BalloonStats stats = {.disk_caches = 1000 * MIB,
                          .free_memory = policy.MaxFree()};
    EXPECT_EQ(MIB, policy.ComputeBalloonDeltaImpl(
                       limit_start - MIB /* host_free */, stats,
                       0 /* host_available */, false, "test"));
  }

  // Test that when there is less cache left than the distance to target free,
  // we only inflate the balloon enough to reclaim that cache.
  {
    BalloonStats stats = {.disk_caches = 150 * MIB,
                          .free_memory = policy.MaxFree()};
    const int64_t cache_above_limit =
        stats.disk_caches - params.reclaim_target_cache;
    EXPECT_EQ(cache_above_limit, policy.ComputeBalloonDeltaImpl(
                                     host_lwm /* host_free */, stats,
                                     0 /* host_available */, false, "test"));
  }

  // Test that when we are way past reclaiming in the host, we still give the
  // guest MinFree() memory.
  {
    BalloonStats stats = {.disk_caches = 1000 * MIB,
                          .free_memory = policy.MaxFree()};
    const int64_t free_above_min = stats.free_memory - policy.MinFree();
    EXPECT_EQ(free_above_min, policy.ComputeBalloonDeltaImpl(
                                  0 /* host_free */, stats,
                                  0 /* host_available */, false, "test"));
  }
}

// Tests that critical_target_cache and moderate_target_cache work together as
// expected.
TEST(BalloonPolicyTest, LimitCacheModerateAndCritical) {
  // Values are roughly what a 4GB ARCVM would get (but rounded)
  const int64_t host_lwm = 200 * MIB;
  const int64_t guest_lwm = 200 * MIB;
  MemoryMargins margins = {.critical = 400 * MIB, .moderate = 2000 * MIB};
  const LimitCacheBalloonPolicy::Params params = {
      .reclaim_target_cache = 0,
      .critical_target_cache = 100 * MIB,
      .moderate_target_cache = 200 * MIB};
  LimitCacheBalloonPolicy policy(margins, host_lwm, guest_lwm, params, "test");

  // Test that when we are limited by both moderate and critical available cache
  // limits, the smaller of the two is used.
  BalloonStats stats = {.disk_caches = 150 * MIB,
                        .free_memory = policy.MaxFree()};
  const int64_t cache_above_limit =
      stats.disk_caches - params.critical_target_cache;
  EXPECT_EQ(cache_above_limit,
            policy.ComputeBalloonDeltaImpl(
                0 /* host_free */, stats, margins.critical /* host_available */,
                false, "test"));
}

// Tests that the guest gets MinFree memory even if the host is very low.
TEST(BalloonPolicyTest, LimitCacheGuestFreeLow) {
  // Values are roughly what a 4GB ARCVM would get (but rounded)
  const int64_t host_lwm = 200 * MIB;
  const int64_t guest_lwm = 200 * MIB;
  MemoryMargins margins = {.critical = 400 * MIB, .moderate = 2000 * MIB};
  const LimitCacheBalloonPolicy::Params params = {
      .reclaim_target_cache = 0,
      .critical_target_cache = 100 * MIB,
      .moderate_target_cache = 200 * MIB};
  LimitCacheBalloonPolicy policy(margins, host_lwm, guest_lwm, params, "test");

  BalloonStats stats = {.disk_caches = 150 * MIB, .free_memory = 0};
  EXPECT_EQ(-policy.MinFree(), policy.ComputeBalloonDeltaImpl(
                                   0 /* host_free */, stats,
                                   0 /* host_available */, false, "test"));
}

}  // namespace concierge
}  // namespace vm_tools
