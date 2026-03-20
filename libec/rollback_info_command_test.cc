// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libec/rollback_info_command.h"

#include <memory>
#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace ec {
namespace {

using ::testing::Return;

TEST(RollbackInfoCommand, RollbackInfoCommand) {
  RollbackInfoCommand cmd_v0(0);
  EXPECT_EQ(cmd_v0.Version(), 0);
  EXPECT_EQ(cmd_v0.GetVersion(), 0);
  EXPECT_EQ(cmd_v0.Command(), EC_CMD_ROLLBACK_INFO);

  RollbackInfoCommand cmd_v1(1);
  EXPECT_EQ(cmd_v1.Version(), 1);
  EXPECT_EQ(cmd_v1.GetVersion(), 1);
  EXPECT_EQ(cmd_v1.Command(), EC_CMD_ROLLBACK_INFO);
}

// Mock the underlying EcCommand to test.
class RollbackInfoCommandTest : public testing::Test {
 public:
  class MockRollbackInfoCommand_v0 : public RollbackInfoCommand_v0 {
   public:
    using RollbackInfoCommand_v0::RollbackInfoCommand_v0;
    MOCK_METHOD(const struct ec_response_rollback_info*,
                Resp,
                (),
                (const, override));
  };

  class MockRollbackInfoCommand_v1 : public RollbackInfoCommand_v1 {
   public:
    using RollbackInfoCommand_v1::RollbackInfoCommand_v1;
    MOCK_METHOD(const struct ec_response_rollback_info_v1*,
                Resp,
                (),
                (const, override));
  };
};

TEST_F(RollbackInfoCommandTest, Success_v0) {
  auto mock_v0 = std::make_unique<MockRollbackInfoCommand_v0>();
  struct ec_response_rollback_info response = {
      .id = 3,
      .rollback_min_version = 2,
      .rw_rollback_version = 1,
  };
  EXPECT_CALL(*mock_v0, Resp).WillRepeatedly(Return(&response));

  RollbackInfoCommand mock_command(0, std::move(mock_v0), nullptr);

  EXPECT_EQ(mock_command.ID(), 3);
  EXPECT_EQ(mock_command.MinVersion(), 2);
  EXPECT_EQ(mock_command.RWVersion(), 1);
  EXPECT_FALSE(mock_command.IsSecretInited().has_value());
}

TEST_F(RollbackInfoCommandTest, Success_v1) {
  auto mock_v1 = std::make_unique<MockRollbackInfoCommand_v1>();
  struct ec_response_rollback_info_v1 response = {
      .id = 4,
      .rollback_min_version = 3,
      .rw_rollback_version = 2,
      .is_secret_inited = 1,
  };
  EXPECT_CALL(*mock_v1, Resp).WillRepeatedly(Return(&response));

  RollbackInfoCommand mock_command(1, nullptr, std::move(mock_v1));

  EXPECT_EQ(mock_command.ID(), 4);
  EXPECT_EQ(mock_command.MinVersion(), 3);
  EXPECT_EQ(mock_command.RWVersion(), 2);
  ASSERT_TRUE(mock_command.IsSecretInited().has_value());
  EXPECT_TRUE(mock_command.IsSecretInited().value());
}

}  // namespace
}  // namespace ec
