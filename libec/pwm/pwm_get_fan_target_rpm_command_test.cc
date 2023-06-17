// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdlib>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "libec/pwm/pwm_get_fan_target_rpm_command.h"

namespace ec {
namespace {

using ::testing::Return;

// Mock the underlying EcCommand to test.
class PwmGetFanTargetRpmCommandTest : public testing::Test {
 public:
  class MockPwmGetFanTargetRpmCommand : public PwmGetFanTargetRpmCommand {
   public:
    explicit MockPwmGetFanTargetRpmCommand(uint8_t id)
        : PwmGetFanTargetRpmCommand(id) {}
    using PwmGetFanTargetRpmCommand::PwmGetFanTargetRpmCommand;
    MOCK_METHOD(uint16_t*, Resp, (), (const, override));
  };
};

TEST_F(PwmGetFanTargetRpmCommandTest, PwmGetFanTargetRpmCommand) {
  constexpr uint8_t id = 2;
  PwmGetFanTargetRpmCommand cmd{id};
  EXPECT_EQ(cmd.Command(), EC_CMD_READ_MEMMAP);
  EXPECT_GE(cmd.Version(), 0);
  EXPECT_GE(cmd.Req()->offset, EC_MEMMAP_FAN + 2 * id);
  EXPECT_GE(cmd.Req()->size, sizeof(uint16_t));
}

TEST_F(PwmGetFanTargetRpmCommandTest, Success) {
  constexpr uint8_t id = 2;
  MockPwmGetFanTargetRpmCommand mock_command{id};
  uint16_t expected_response = 100;

  EXPECT_CALL(mock_command, Resp).WillRepeatedly(Return(&expected_response));
  EXPECT_EQ(mock_command.Rpm(), 100);
}

}  // namespace
}  // namespace ec
