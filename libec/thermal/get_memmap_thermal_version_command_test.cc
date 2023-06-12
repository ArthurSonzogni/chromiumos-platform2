// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <cstdlib>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "libec/thermal/get_memmap_thermal_version_command.h"

namespace ec {
namespace {

using ::testing::Return;

// Mock the underlying EcCommand to test.
class GetMemmapThermalVersionCommandTest : public testing::Test {
 public:
  class MockGetMemmapThermalVersionCommand
      : public GetMemmapThermalVersionCommand {
   public:
    MockGetMemmapThermalVersionCommand() = default;
    using GetMemmapThermalVersionCommand::GetMemmapThermalVersionCommand;
    MOCK_METHOD(uint8_t*, Resp, (), (const, override));
  };
};

TEST_F(GetMemmapThermalVersionCommandTest, GetMemmapThermalVersionCommand) {
  GetMemmapThermalVersionCommand cmd;
  EXPECT_EQ(cmd.Command(), EC_CMD_READ_MEMMAP);
  EXPECT_GE(cmd.Version(), 0);
  EXPECT_GE(cmd.Req()->offset, EC_MEMMAP_THERMAL_VERSION);
  EXPECT_GE(cmd.Req()->size, sizeof(uint8_t));
}

TEST_F(GetMemmapThermalVersionCommandTest, Success) {
  MockGetMemmapThermalVersionCommand mock_command;
  uint8_t expected_response = 100;

  EXPECT_CALL(mock_command, Resp).WillRepeatedly(Return(&expected_response));
  ASSERT_TRUE(mock_command.ThermalVersion().has_value());
  EXPECT_EQ(mock_command.ThermalVersion(), expected_response);
  EXPECT_EQ(sizeof(mock_command.ThermalVersion().value()), sizeof(uint8_t));
}

}  // namespace
}  // namespace ec
