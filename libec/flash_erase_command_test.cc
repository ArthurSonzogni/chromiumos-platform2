// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "libec/ec_command.h"
#include "libec/flash_erase_command.h"

namespace ec {
namespace {

TEST(FlashEraseCommand, FlashEraseCommand_v0) {
  auto cmd = FlashEraseCommand_v0::Create(1, 10);
  EXPECT_TRUE(cmd);
  EXPECT_EQ(cmd->Version(), 0);
  EXPECT_EQ(cmd->Command(), EC_CMD_FLASH_ERASE);
  EXPECT_EQ(cmd->Req()->offset, 1);
  EXPECT_EQ(cmd->Req()->size, 10);
}

TEST(FlashEraseCommand, FlashEraseCommand_v0_OffsetBoundaryCondition) {
  constexpr uint32_t kOffset = 4294967295;  // 2^32 - 1
  uint32_t erase_size = 1;
  EXPECT_TRUE(FlashEraseCommand_v0::Create(kOffset, erase_size));
  erase_size = 2;
  EXPECT_FALSE(FlashEraseCommand_v0::Create(kOffset, erase_size));
}

TEST(FlashEraseCommand, FlashEraseCommand_v1) {
  auto cmd = FlashEraseCommand_v1::Create(1, 10);
  EXPECT_TRUE(cmd);
  EXPECT_EQ(cmd->Version(), 1);
  EXPECT_EQ(cmd->Command(), EC_CMD_FLASH_ERASE);
  EXPECT_EQ(cmd->Req()->params.offset, 1);
  EXPECT_EQ(cmd->Req()->params.size, 10);
  EXPECT_EQ(cmd->Req()->action, FLASH_ERASE_SECTOR);
  EXPECT_EQ(cmd->options().poll_for_result_num_attempts, 20);
  EXPECT_EQ(cmd->options().poll_interval, base::Milliseconds(500));
}

TEST(FlashEraseCommand, FlashEraseCommand_v1_Large) {
  auto cmd = FlashEraseCommand_v1::Create(1, 16384);
  EXPECT_TRUE(cmd);
  EXPECT_EQ(cmd->Version(), 1);
  EXPECT_EQ(cmd->Command(), EC_CMD_FLASH_ERASE);
  EXPECT_EQ(cmd->Req()->action, FLASH_ERASE_SECTOR_ASYNC);
  EXPECT_EQ(cmd->options().poll_for_result_num_attempts, 20);
  EXPECT_EQ(cmd->options().poll_interval, base::Milliseconds(500));
}

TEST(FlashEraseCommand, FlashEraseCommand_v1_OffsetBoundaryCondition) {
  constexpr uint32_t kOffset = 4294967295;  // 2^32 - 1
  uint32_t erase_size = 1;
  EXPECT_TRUE(FlashEraseCommand_v1::Create(kOffset, erase_size));
  erase_size = 2;
  EXPECT_FALSE(FlashEraseCommand_v1::Create(kOffset, erase_size));
}

}  // namespace
}  // namespace ec
