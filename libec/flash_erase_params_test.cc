// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "libec/ec_command.h"
#include "libec/flash_erase_params.h"

namespace ec {
namespace {

TEST(FlashEraseParams, ValidateDefinitionsMatch) {
  EXPECT_EQ(sizeof(flash_erase::Params_v1), sizeof(ec_params_flash_erase_v1));
  EXPECT_EQ(sizeof(flash_erase::Params_v1().action),
            sizeof(ec_params_flash_erase_v1().cmd));
  EXPECT_EQ(sizeof(flash_erase::Params_v1().reserved),
            sizeof(ec_params_flash_erase_v1().reserved));
  EXPECT_EQ(sizeof(flash_erase::Params_v1().flag),
            sizeof(ec_params_flash_erase_v1().flag));
  EXPECT_EQ(sizeof(flash_erase::Params_v1().params),
            sizeof(ec_params_flash_erase_v1().params));
  EXPECT_EQ(sizeof(flash_erase::Params_v1().params.offset),
            sizeof(ec_params_flash_erase_v1().params.offset));
  EXPECT_EQ(sizeof(flash_erase::Params_v1().params.size),
            sizeof(ec_params_flash_erase_v1().params.size));
}

}  // namespace
}  // namespace ec
