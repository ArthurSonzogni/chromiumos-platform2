// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/executor/utils/network_utils.h"

#include <gtest/gtest.h>

namespace diagnostics {
namespace {

TEST(NetworkUtilsTest, WirelessNameValidation) {
  EXPECT_TRUE(IsValidWirelessInterfaceName("wlan0"));
  EXPECT_TRUE(IsValidWirelessInterfaceName("mlan0"));
  EXPECT_TRUE(IsValidWirelessInterfaceName("wlan8"));
  EXPECT_TRUE(IsValidWirelessInterfaceName("mlan8"));
  EXPECT_TRUE(IsValidWirelessInterfaceName("wlan10"));
  EXPECT_FALSE(IsValidWirelessInterfaceName("xlan0"));
  EXPECT_FALSE(IsValidWirelessInterfaceName("Wlan0"));
  EXPECT_FALSE(IsValidWirelessInterfaceName("mlan-0"));
  EXPECT_FALSE(IsValidWirelessInterfaceName("wlanwaywaytolong0"));
  EXPECT_FALSE(IsValidWirelessInterfaceName("wln0"));
  EXPECT_FALSE(IsValidWirelessInterfaceName("man0"));
  EXPECT_FALSE(IsValidWirelessInterfaceName("wlan"));
}

}  // namespace
}  // namespace diagnostics
