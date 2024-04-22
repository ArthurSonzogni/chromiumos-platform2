// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net-base/technology.h"

#include <gtest/gtest.h>
#include <optional>

#include <base/logging.h>

namespace net_base {

TEST(TechnologyTest, ToString) {
  EXPECT_EQ(ToString(Technology::kCellular), "cellular");
  EXPECT_EQ(ToString(Technology::kEthernet), "ethernet");
  EXPECT_EQ(ToString(Technology::kVPN), "vpn");
  EXPECT_EQ(ToString(Technology::kWiFi), "wifi");
  EXPECT_EQ(ToString(Technology::kWiFiDirect), "wifi_direct");
}

TEST(TechnologyTest, FromString) {
  EXPECT_EQ(FromString("cellular"), Technology::kCellular);
  EXPECT_EQ(FromString("ethernet"), Technology::kEthernet);
  EXPECT_EQ(FromString("vpn"), Technology::kVPN);
  EXPECT_EQ(FromString("wifi"), Technology::kWiFi);
  EXPECT_EQ(FromString("wifi_direct"), Technology::kWiFiDirect);

  EXPECT_EQ(FromString("WIFI"), std::nullopt);
}

TEST(TechnologyTest, Logging) {
  LOG(INFO) << Technology::kWiFi;
}

}  // namespace net_base
