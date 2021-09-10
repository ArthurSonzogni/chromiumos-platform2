// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "libec/flash_region_info_command.h"

namespace ec {
namespace {

using ::testing::Return;

TEST(FlashRegionInfoCommand, FlashRegionInfoCommand) {
  FlashRegionInfoCommand cmd(EC_FLASH_REGION_RO);
  EXPECT_EQ(cmd.Version(), 1);
  EXPECT_EQ(cmd.Command(), EC_CMD_FLASH_REGION_INFO);
  EXPECT_EQ(cmd.Req()->region, EC_FLASH_REGION_RO);
}

// Mock the underlying EcCommand to test.
class FlashRegionInfoCommandTest : public testing::Test {
 public:
  class MockFlashRegionInfoCommand : public FlashRegionInfoCommand {
   public:
    using FlashRegionInfoCommand::FlashRegionInfoCommand;
    MOCK_METHOD(const struct ec_response_flash_region_info*,
                Resp,
                (),
                (const, override));
  };
};

TEST_F(FlashRegionInfoCommandTest, Success) {
  MockFlashRegionInfoCommand mock_command(EC_FLASH_REGION_RO);
  struct ec_response_flash_region_info response = {.offset = 12, .size = 10};
  EXPECT_CALL(mock_command, Resp).WillRepeatedly(Return(&response));

  EXPECT_EQ(mock_command.GetSize(), 10);
  EXPECT_EQ(mock_command.GetOffset(), 12);
}

}  // namespace
}  // namespace ec
