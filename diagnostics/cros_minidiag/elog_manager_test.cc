// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <array>
#include <memory>

#include <gtest/gtest.h>

#include "diagnostics/cros_minidiag/elog_manager.h"

namespace cros_minidiag {

namespace {

const std::array<const char*, 6> kElogLines = {
    "1 | 2022-01-01 00:00:00 | Mock Type | Mock Data",
    "2 | 2022-01-01 00:00:01 | Mock Type | Mock Data",
    "3 | 2022-01-01 00:00:02 | Mock Type | Mock Data",
    "4 | 2022-01-01 00:00:03 | Mock Type | Mock Data",
    "5 | 2022-01-01 00:00:04 | Mock Type | Mock Data",
    "6 | 2022-01-01 00:00:05 | Mock Type | Mock Data",
};

}  // namespace

class ElogManagerTest : public testing::Test {
 protected:
  template <std::size_t N>
  void SetUpElog(const std::array<const char*, N>& raw_logs) {
    full_elog_.clear();
    for (const auto& line : raw_logs) {
      full_elog_.append(line);
      full_elog_.append("\n");
    }
    size_ = N;
  }

  std::string full_elog_;
  std::size_t size_;
};

TEST_F(ElogManagerTest, BasicLastLine) {
  SetUpElog(kElogLines);
  auto elog_manager = std::make_unique<ElogManager>(full_elog_, "");
  EXPECT_EQ(elog_manager->last_line(), kElogLines[size_ - 1]);
  EXPECT_EQ(elog_manager->GetEventNum(), size_);
}

TEST_F(ElogManagerTest, TestPreviousLastLine) {
  const int idx = 3;
  SetUpElog(kElogLines);
  ASSERT_TRUE(idx < size_);
  auto elog_manager =
      std::make_unique<ElogManager>(full_elog_, kElogLines[idx]);
  EXPECT_EQ(elog_manager->last_line(), kElogLines[size_ - 1]);
  EXPECT_EQ(elog_manager->GetEventNum(), size_ - idx - 1);
}

TEST_F(ElogManagerTest, BadPreviousLastLine) {
  SetUpElog(kElogLines);
  auto elog_manager = std::make_unique<ElogManager>(full_elog_, "XXX");
  EXPECT_EQ(elog_manager->last_line(), kElogLines[size_ - 1]);
  EXPECT_EQ(elog_manager->GetEventNum(), size_);
}

TEST(ElogEventTest, BasicEvent) {
  auto event = std::make_unique<ElogEvent>(kElogLines[0]);
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
