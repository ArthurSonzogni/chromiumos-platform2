// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <gtest/gtest.h>

#include "diagnostics/cros_minidiag/elog_manager.h"

namespace cros_minidiag {

namespace {

constexpr const char kMockElogList[] =
    "1 | 2022-01-01 00:00:00 | Mock Type | Mock Data\n"
    "2 | 2022-01-01 00:00:01 | Mock Type | Mock Data\n"
    "3 | 2022-01-01 00:00:02 | Mock Type | Mock Data\n"
    "4 | 2022-01-01 00:00:03 | Mock Type | Mock Data\n"
    "5 | 2022-01-01 00:00:04 | Mock Type | Mock Data\n"
    "6 | 2022-01-01 00:00:05 | Mock Type | Mock Data\n";

constexpr const char kMockEvent[] =
    "6 | 2022-01-01 00:00:05 | Mock Type | Mock Data";

}  // namespace

TEST(ElogManager, BasicLastLine) {
  auto elog_manager = std::make_unique<ElogManager>(kMockElogList);
  EXPECT_EQ(elog_manager->last_line(), kMockEvent);
}

TEST(ElogEventTest, BasicEvent) {
  auto event = std::make_unique<ElogEvent>(kMockEvent);
  EXPECT_EQ(event->GetType(), "Mock Type");
}

TEST(ElogEventTest, BadEventEmpty) {
  auto event = std::make_unique<ElogEvent>("");
  EXPECT_EQ(event->GetType(), "");
}

TEST(ElogEventTest, BadEventColumnTooFew) {
  auto event = std::make_unique<ElogEvent>("6 | 2022-01-01");
  EXPECT_EQ(event->GetType(), "");
}

}  // namespace cros_minidiag
