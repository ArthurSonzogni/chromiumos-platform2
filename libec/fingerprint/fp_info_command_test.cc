// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libec/fingerprint/fp_info_command.h"

#include <bitset>

#include <gtest/gtest.h>

#include "libec/ec_command.h"

namespace ec {
namespace {

TEST(FpInfoCommand, FpInfoCommand) {
  auto cmd_v1 = FpInfoCommand(/*version=*/1);
  EXPECT_EQ(cmd_v1.Version(), 1);
  EXPECT_EQ(cmd_v1.GetVersion(), 1);
  EXPECT_EQ(cmd_v1.Command(), EC_CMD_FP_INFO);

  auto cmd_v2 = FpInfoCommand(/*version=*/2);
  EXPECT_EQ(cmd_v2.Version(), 2);
  EXPECT_EQ(cmd_v2.GetVersion(), 2);
  EXPECT_EQ(cmd_v2.Command(), EC_CMD_FP_INFO);
}

}  // namespace
}  // namespace ec
