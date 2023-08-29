// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/manager.h"

#include <gtest/gtest.h>

#include "patchpanel/fake_system.h"

namespace patchpanel {
namespace {

using testing::Return;

TEST(ManagerTest, CalculateDownstreamCurHopLimit) {
  FakeSystem system;

  // Successful case.
  EXPECT_CALL(system, SysNetGet(System::SysNet::kIPv6HopLimit, "wwan0"))
      .WillOnce(Return("64"));
  EXPECT_EQ(Manager::CalculateDownstreamCurHopLimit(
                reinterpret_cast<System*>(&system), "wwan0"),
            63);

  // Failure case.
  EXPECT_CALL(system, SysNetGet(System::SysNet::kIPv6HopLimit, "wwan1"))
      .WillOnce(Return(""));
  EXPECT_EQ(Manager::CalculateDownstreamCurHopLimit(
                reinterpret_cast<System*>(&system), "wwan1"),
            std::nullopt);
}

}  // namespace
}  // namespace patchpanel
