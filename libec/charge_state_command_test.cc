// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libec/charge_state_command.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace ec {
namespace {

using ::testing::Return;

TEST(ChargeStateGetParamCommand, ChargeStateGetParamCommand) {
  ChargeStateGetParamCommand cmd(CS_PARAM_CHG_CURRENT);
  EXPECT_EQ(cmd.Version(), 0);
  EXPECT_EQ(cmd.Command(), EC_CMD_CHARGE_STATE);
  EXPECT_EQ(cmd.Req()->cmd, CHARGE_STATE_CMD_GET_PARAM);
  EXPECT_EQ(cmd.Req()->get_param.param, CS_PARAM_CHG_CURRENT);
}

TEST(GetMinChargingVoltCommand, GetMinChargingVoltCommand) {
  GetMinChargingVoltCommand cmd;
  EXPECT_EQ(cmd.Version(), 0);
  EXPECT_EQ(cmd.Command(), EC_CMD_CHARGE_STATE);
  EXPECT_EQ(cmd.Req()->cmd, CHARGE_STATE_CMD_GET_PARAM);
  EXPECT_EQ(cmd.Req()->get_param.param, CS_PARAM_CHG_MIN_REQUIRED_MV);
}

// Mock the underlying EcCommand to test.
class ChargeStateGetParamCommandTest : public testing::Test {
 public:
  class MockChargeStateGetParamCommand : public ChargeStateGetParamCommand {
   public:
    using ChargeStateGetParamCommand::ChargeStateGetParamCommand;
    MOCK_METHOD(uint32_t, Result, (), (const, override));
    MOCK_METHOD(struct ec_response_charge_state*, Resp, (), (const, override));
  };
};

TEST_F(ChargeStateGetParamCommandTest, Success) {
  MockChargeStateGetParamCommand mock_command(CS_PARAM_CHG_VOLTAGE);

  struct ec_response_charge_state response = {.get_param = {.value = 13200}};

  EXPECT_CALL(mock_command, Resp).Times(2).WillRepeatedly(Return(&response));

  EXPECT_EQ(mock_command.Get(), 13200);
}

TEST_F(ChargeStateGetParamCommandTest, NullResponse) {
  MockChargeStateGetParamCommand mock_command(CS_PARAM_CHG_INPUT_CURRENT_STEP);

  EXPECT_CALL(mock_command, Resp).WillOnce(Return(nullptr));

  EXPECT_EQ(mock_command.Get(), std::nullopt);
}

// Mock the underlying EcCommand to test.
class GetMinChargingVoltCommandTest : public testing::Test {
 public:
  class MockGetMinChargingVoltCommand : public GetMinChargingVoltCommand {
   public:
    using GetMinChargingVoltCommand::GetMinChargingVoltCommand;
    MOCK_METHOD(struct ec_response_charge_state*, Resp, (), (const, override));
  };
};

TEST_F(GetMinChargingVoltCommandTest, Success) {
  MockGetMinChargingVoltCommand mock_command;

  struct ec_response_charge_state response = {.get_param = {.value = 15000}};

  EXPECT_CALL(mock_command, Resp).Times(2).WillRepeatedly(Return(&response));

  EXPECT_EQ(mock_command.Get(), 15.0);
}

TEST_F(GetMinChargingVoltCommandTest, NullResponse) {
  MockGetMinChargingVoltCommand mock_command;

  EXPECT_CALL(mock_command, Resp).WillOnce(Return(nullptr));

  EXPECT_EQ(mock_command.Get(), std::nullopt);
}

}  // namespace
}  // namespace ec
