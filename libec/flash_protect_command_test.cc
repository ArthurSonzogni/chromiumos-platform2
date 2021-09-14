// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/stl_util.h>
#include <gtest/gtest.h>

#include "libec/ec_command.h"
#include "libec/flash_protect_command.h"

namespace ec {
namespace {

TEST(FlashProtectCommand, FlashProtectCommand) {
  flash_protect::Flags flags =
      flash_protect::Flags::kRollbackAtBoot | flash_protect::Flags::kRoAtBoot;
  flash_protect::Flags mask = flash_protect::Flags::kNone;
  FlashProtectCommand cmd(flags, mask);
  EXPECT_EQ(cmd.Version(), EC_VER_FLASH_PROTECT);
  EXPECT_EQ(cmd.Command(), EC_CMD_FLASH_PROTECT);
  EXPECT_EQ(cmd.Req()->flags, base::to_underlying(flags));
  EXPECT_EQ(cmd.Req()->mask, base::to_underlying(mask));
}

TEST(FlashProtectCommand, ParseFlags) {
  std::string result;

  // test each flag string individually
  flash_protect::Flags flags = flash_protect::Flags::kNone;
  result = FlashProtectCommand::ParseFlags(flags);
  EXPECT_EQ(result, "");

  flags = flash_protect::Flags::kRoAtBoot;
  result = FlashProtectCommand::ParseFlags(flags);
  EXPECT_EQ(result, "RO_AT_BOOT  ");

  flags = flash_protect::Flags::kRoNow;
  result = FlashProtectCommand::ParseFlags(flags);
  EXPECT_EQ(result, "RO_NOW  ");

  flags = flash_protect::Flags::kAllNow;
  result = FlashProtectCommand::ParseFlags(flags);
  EXPECT_EQ(result, "ALL_NOW  ");

  flags = flash_protect::Flags::kGpioAsserted;
  result = FlashProtectCommand::ParseFlags(flags);
  EXPECT_EQ(result, "GPIO_ASSERTED  ");

  flags = flash_protect::Flags::kErrorStuck;
  result = FlashProtectCommand::ParseFlags(flags);
  EXPECT_EQ(result, "ERROR_STUCK  ");

  flags = flash_protect::Flags::kErrorInconsistent;
  result = FlashProtectCommand::ParseFlags(flags);
  EXPECT_EQ(result, "ERROR_INCONSISTENT  ");

  flags = flash_protect::Flags::kAllAtBoot;
  result = FlashProtectCommand::ParseFlags(flags);
  EXPECT_EQ(result, "ALL_AT_BOOT  ");

  flags = flash_protect::Flags::kRwAtBoot;
  result = FlashProtectCommand::ParseFlags(flags);
  EXPECT_EQ(result, "RW_AT_BOOT  ");

  flags = flash_protect::Flags::kRwNow;
  result = FlashProtectCommand::ParseFlags(flags);
  EXPECT_EQ(result, "RW_NOW  ");

  flags = flash_protect::Flags::kRollbackAtBoot;
  result = FlashProtectCommand::ParseFlags(flags);
  EXPECT_EQ(result, "ROLLBACK_AT_BOOT  ");

  flags = flash_protect::Flags::kRollbackNow;
  result = FlashProtectCommand::ParseFlags(flags);
  EXPECT_EQ(result, "ROLLBACK_NOW  ");

  // test a combination of flags
  flags = flash_protect::Flags::kRoAtBoot | flash_protect::Flags::kRoNow |
          flash_protect::Flags::kGpioAsserted;
  result = FlashProtectCommand::ParseFlags(flags);
  EXPECT_EQ(result, "RO_AT_BOOT  RO_NOW  GPIO_ASSERTED  ");
}

TEST(FlashProtectCommand, Enum) {
  EXPECT_EQ(base::to_underlying(flash_protect::Flags::kNone), 0);
  EXPECT_EQ(base::to_underlying(flash_protect::Flags::kRoAtBoot), 1);
  EXPECT_EQ(base::to_underlying(flash_protect::Flags::kRoNow), 2);
  EXPECT_EQ(base::to_underlying(flash_protect::Flags::kAllNow), 4);
  EXPECT_EQ(base::to_underlying(flash_protect::Flags::kGpioAsserted), 8);
  EXPECT_EQ(base::to_underlying(flash_protect::Flags::kErrorStuck), 16);
  EXPECT_EQ(base::to_underlying(flash_protect::Flags::kErrorInconsistent), 32);
  EXPECT_EQ(base::to_underlying(flash_protect::Flags::kAllAtBoot), 64);
  EXPECT_EQ(base::to_underlying(flash_protect::Flags::kRwAtBoot), 128);
  EXPECT_EQ(base::to_underlying(flash_protect::Flags::kRwNow), 256);
  EXPECT_EQ(base::to_underlying(flash_protect::Flags::kRollbackAtBoot), 512);
  EXPECT_EQ(base::to_underlying(flash_protect::Flags::kRollbackNow), 1024);
}

TEST(FlashProtect, OverloadedStreamOperator) {
  std::stringstream stream;
  stream << flash_protect::Flags::kRoAtBoot;
  EXPECT_EQ(stream.str(), "1");
}

}  // namespace
}  // namespace ec
