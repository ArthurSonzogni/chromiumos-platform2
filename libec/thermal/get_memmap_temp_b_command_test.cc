// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <cstdlib>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "libec/thermal/get_memmap_temp_b_command.h"

namespace ec {
namespace {

using ::testing::Return;

// Mock the underlying EcCommand to test.
class GetMemmapTempBCommandTest : public testing::Test {
 public:
  class MockGetMemmapTempBCommand : public GetMemmapTempBCommand {
   public:
    explicit MockGetMemmapTempBCommand(uint8_t id)
        : GetMemmapTempBCommand(id) {}
    using GetMemmapTempBCommand::GetMemmapTempBCommand;
    MOCK_METHOD(uint8_t*, Resp, (), (const, override));
  };
};

TEST_F(GetMemmapTempBCommandTest, GetMemmapTempBCommand) {
  constexpr uint8_t id = 20;
  GetMemmapTempBCommand cmd{id};
  EXPECT_EQ(cmd.Command(), EC_CMD_READ_MEMMAP);
  EXPECT_GE(cmd.Version(), 0);
  EXPECT_GE(cmd.Req()->offset,
            EC_MEMMAP_TEMP_SENSOR_B + id - EC_TEMP_SENSOR_ENTRIES);
  EXPECT_GE(cmd.Req()->size, sizeof(uint8_t));
}

TEST_F(GetMemmapTempBCommandTest, Success) {
  MockGetMemmapTempBCommand mock_command{0};
  uint8_t expected_response = 100;

  EXPECT_CALL(mock_command, Resp).WillRepeatedly(Return(&expected_response));
  ASSERT_TRUE(mock_command.Temp().has_value());
  EXPECT_EQ(mock_command.Temp(), expected_response);
  EXPECT_EQ(sizeof(mock_command.Temp().value()), sizeof(uint8_t));
}

}  // namespace
}  // namespace ec
