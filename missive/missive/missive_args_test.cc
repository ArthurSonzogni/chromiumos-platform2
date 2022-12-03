// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/missive/missive_args.h"

#include <base/time/time.h>
#include <base/time/time_delta_from_string.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

using ::testing::Eq;

namespace reporting {
namespace {
TEST(MissiveArgsTest, DefaultValuesTest) {
  const MissiveArgs args("", "", "", "");
  ASSERT_THAT(
      args.enqueuing_record_tallier(),
      Eq(base::TimeDeltaFromString(MissiveArgs::kEnqueuingRecordTallierDefault)
             .value()));
  ASSERT_THAT(
      args.cpu_collector_interval(),
      Eq(base::TimeDeltaFromString(MissiveArgs::kCpuCollectorIntervalDefault)
             .value()));
  ASSERT_THAT(args.storage_collector_interval(),
              Eq(base::TimeDeltaFromString(
                     MissiveArgs::kStorageCollectorIntervalDefault)
                     .value()));
  ASSERT_THAT(
      args.memory_collector_interval(),
      Eq(base::TimeDeltaFromString(MissiveArgs::kMemoryCollectorIntervalDefault)
             .value()));
}

TEST(MissiveArgsTest, ExplicitValuesTest) {
  const MissiveArgs args("10ms", "20s", "30m", "40h");
  ASSERT_THAT(args.enqueuing_record_tallier(), Eq(base::Milliseconds(10)));
  ASSERT_THAT(args.cpu_collector_interval(), Eq(base::Seconds(20)));
  ASSERT_THAT(args.storage_collector_interval(), Eq(base::Minutes(30)));
  ASSERT_THAT(args.memory_collector_interval(), Eq(base::Hours(40)));
}

TEST(MissiveArgsTest, BadValuesTest) {
  const MissiveArgs args("AAAA", "BAD", "WRONG", "123");
  ASSERT_THAT(
      args.enqueuing_record_tallier(),
      Eq(base::TimeDeltaFromString(MissiveArgs::kEnqueuingRecordTallierDefault)
             .value()));
  ASSERT_THAT(
      args.cpu_collector_interval(),
      Eq(base::TimeDeltaFromString(MissiveArgs::kCpuCollectorIntervalDefault)
             .value()));
  ASSERT_THAT(args.storage_collector_interval(),
              Eq(base::TimeDeltaFromString(
                     MissiveArgs::kStorageCollectorIntervalDefault)
                     .value()));
  ASSERT_THAT(
      args.memory_collector_interval(),
      Eq(base::TimeDeltaFromString(MissiveArgs::kMemoryCollectorIntervalDefault)
             .value()));
}
}  // namespace
}  // namespace reporting
