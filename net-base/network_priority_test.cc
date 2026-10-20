// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net-base/network_priority.h"

#include <sstream>

#include <gtest/gtest.h>

namespace net_base {
namespace {

TEST(NetworkPriorityTest, DefaultAndEquality) {
  NetworkPriority p1;
  EXPECT_FALSE(p1.is_primary_logical);
  EXPECT_FALSE(p1.is_primary_physical);
  EXPECT_FALSE(p1.is_primary_for_dns);
  EXPECT_EQ(p1.ranking_order, NetworkPriority::kMaxRankingOrder);

  NetworkPriority p2 = p1;
  EXPECT_EQ(p1, p2);

  p2 = p1;
  p2.ranking_order = 0;
  EXPECT_NE(p1, p2);

  p2 = p1;
  p2.is_primary_logical = true;
  EXPECT_NE(p1, p2);

  p2 = p1;
  p2.is_primary_physical = true;
  EXPECT_NE(p1, p2);

  p2 = p1;
  p2.is_primary_for_dns = true;
  EXPECT_NE(p1, p2);
}

TEST(NetworkPriorityTest, HaveSameRoutingPriority) {
  NetworkPriority p1{
      .is_primary_logical = true,
      .is_primary_physical = true,
      .is_primary_for_dns = false,
      .ranking_order = 0,
  };
  NetworkPriority p2 = p1;
  EXPECT_TRUE(NetworkPriority::HaveSameRoutingPriority(p1, p2));

  // Changing is_primary_for_dns should not affect routing priority.
  p2.is_primary_for_dns = true;
  EXPECT_TRUE(NetworkPriority::HaveSameRoutingPriority(p1, p2));

  p2.ranking_order = 1;
  EXPECT_FALSE(NetworkPriority::HaveSameRoutingPriority(p1, p2));

  p2 = p1;
  p2.is_primary_logical = false;
  EXPECT_FALSE(NetworkPriority::HaveSameRoutingPriority(p1, p2));

  p2 = p1;
  p2.is_primary_physical = false;
  EXPECT_FALSE(NetworkPriority::HaveSameRoutingPriority(p1, p2));
}

TEST(NetworkPriorityTest, StreamOperator) {
  NetworkPriority p_default;
  std::ostringstream oss_default;
  oss_default << p_default;
  EXPECT_EQ(oss_default.str(), "{31}");

  NetworkPriority p_all{
      .is_primary_logical = true,
      .is_primary_physical = true,
      .is_primary_for_dns = true,
      .ranking_order = 0,
  };
  std::ostringstream oss_all;
  oss_all << p_all;
  EXPECT_EQ(oss_all.str(),
            "{0, primary_logical, primary_physical, primary_for_dns}");
}

}  // namespace
}  // namespace net_base
