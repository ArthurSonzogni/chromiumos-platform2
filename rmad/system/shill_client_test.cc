// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <dbus/shill/dbus-constants.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <shill/dbus-proxy-mocks.h>

#include "rmad/system/shill_client_impl.h"

using testing::_;
using testing::Eq;
using testing::Return;
using testing::StrictMock;

namespace rmad {

class ShillClientTest : public testing::Test {
 public:
  ShillClientTest() = default;
  ~ShillClientTest() override = default;
};

TEST_F(ShillClientTest, DisableCellular_Success) {
  auto mock_flimflam_manager_proxy =
      std::make_unique<StrictMock<org::chromium::flimflam::ManagerProxyMock>>();
  EXPECT_CALL(*mock_flimflam_manager_proxy,
              DisableTechnology(Eq(shill::kTypeCellular), _, _))
      .WillOnce(Return(true));

  auto shill_client =
      std::make_unique<ShillClientImpl>(std::move(mock_flimflam_manager_proxy));
  EXPECT_TRUE(shill_client->DisableCellular());
}

TEST_F(ShillClientTest, DisableCellular_NoResponse) {
  auto mock_flimflam_manager_proxy =
      std::make_unique<StrictMock<org::chromium::flimflam::ManagerProxyMock>>();
  EXPECT_CALL(*mock_flimflam_manager_proxy,
              DisableTechnology(Eq(shill::kTypeCellular), _, _))
      .WillOnce(Return(false));

  auto shill_client =
      std::make_unique<ShillClientImpl>(std::move(mock_flimflam_manager_proxy));
  EXPECT_FALSE(shill_client->DisableCellular());
}

}  // namespace rmad
