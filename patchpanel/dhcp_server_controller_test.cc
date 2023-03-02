// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "patchpanel/dhcp_server_controller.h"

namespace patchpanel {

class DHCPServerControllerTest : public ::testing::Test {
 protected:
  void SetUp() override {}

  DHCPServerController dhcp_server_controller_{"wlan0"};
};

TEST_F(DHCPServerControllerTest, Start) {
  EXPECT_EQ(dhcp_server_controller_.Start(), true);
}

}  // namespace patchpanel
