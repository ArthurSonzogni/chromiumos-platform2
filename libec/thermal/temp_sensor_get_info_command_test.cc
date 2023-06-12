// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <cstdlib>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "libec/thermal/temp_sensor_get_info_command.h"

namespace ec {
namespace {

using ::testing::Return;

// Mock the underlying EcCommand to test.
class TempSensorGetInfoCommandTest : public testing::Test {
 public:
  class MockTempSensorGetInfoCommand : public TempSensorGetInfoCommand {
   public:
    explicit MockTempSensorGetInfoCommand(uint8_t id)
        : TempSensorGetInfoCommand(id) {}
    using TempSensorGetInfoCommand::TempSensorGetInfoCommand;
    MOCK_METHOD(struct ec_response_temp_sensor_get_info*,
                Resp,
                (),
                (const, override));
  };
};

TEST_F(TempSensorGetInfoCommandTest, TempSensorGetInfoCommand) {
  TempSensorGetInfoCommand cmd{0};
  EXPECT_EQ(cmd.Command(), EC_CMD_TEMP_SENSOR_GET_INFO);
  EXPECT_GE(cmd.Version(), 0);
  EXPECT_EQ(cmd.Req()->id, 0);
}

TEST_F(TempSensorGetInfoCommandTest, Success) {
  MockTempSensorGetInfoCommand mock_command{0};
  struct ec_response_temp_sensor_get_info response = {
      .sensor_name = "sensor_name", .sensor_type = 0};

  EXPECT_CALL(mock_command, Resp).WillRepeatedly(Return(&response));
  EXPECT_EQ(mock_command.SensorName(), "sensor_name");
  EXPECT_EQ(mock_command.SensorType(), 0);
}

}  // namespace
}  // namespace ec
