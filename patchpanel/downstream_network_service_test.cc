// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/downstream_network_service.h"

#include <optional>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "patchpanel/fake_system.h"
#include "patchpanel/system.h"

using testing::Return;

namespace patchpanel {

TEST(DownstreamNetworkServiceTest, CalculateDownstreamCurHopLimit) {
  FakeSystem system;

  // Successful case.
  EXPECT_CALL(system, SysNetGet(System::SysNet::kIPv6HopLimit, "wwan0"))
      .WillOnce(Return("64"));
  EXPECT_EQ(DownstreamNetworkService::CalculateDownstreamCurHopLimit(
                reinterpret_cast<System*>(&system), "wwan0"),
            63);

  // Failure case.
  EXPECT_CALL(system, SysNetGet(System::SysNet::kIPv6HopLimit, "wwan1"))
      .WillOnce(Return(""));
  EXPECT_EQ(DownstreamNetworkService::CalculateDownstreamCurHopLimit(
                reinterpret_cast<System*>(&system), "wwan1"),
            std::nullopt);
}
}  // namespace patchpanel
