// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libec/flash_info_params.h"

#include <gtest/gtest.h>

#include "libec/ec_command.h"

namespace ec {
namespace {

TEST(FlashInfoParams, ValidateSize) {
  EXPECT_EQ(flash_info::Params_v2().banks.size(), 66);
}

TEST(FlashInfoParams, FlashBankEqual) {
  struct ec_flash_bank expected_bank0 = {.count = 1,
                                         .size_exp = 2,
                                         .write_size_exp = 3,
                                         .erase_size_exp = 4,
                                         .protect_size_exp = 5};
  struct ec_flash_bank expected_bank1 = {.count = 1,
                                         .size_exp = 2,
                                         .write_size_exp = 3,
                                         .erase_size_exp = 4,
                                         .protect_size_exp = 5};
  EXPECT_EQ(expected_bank0, expected_bank1);
}

TEST(FlashInfoParams, FlashBankNotEqual) {
  struct ec_flash_bank expected_bank0 = {.count = 1,
                                         .size_exp = 2,
                                         .write_size_exp = 3,
                                         .erase_size_exp = 4,
                                         .protect_size_exp = 5};
  struct ec_flash_bank expected_bank1 = {.count = 0,
                                         .size_exp = 2,
                                         .write_size_exp = 3,
                                         .erase_size_exp = 4,
                                         .protect_size_exp = 5};
  EXPECT_FALSE(expected_bank0 == expected_bank1);
}

TEST(FlashInfoParams, HeaderSize) {
  EXPECT_EQ(sizeof(flash_info::Header::flash_size),
            sizeof(ec_response_flash_info_2::flash_size));
  EXPECT_EQ(sizeof(flash_info::Header::flags),
            sizeof(ec_response_flash_info_2::flags));
  EXPECT_EQ(sizeof(flash_info::Header::write_ideal_size),
            sizeof(ec_response_flash_info_2::write_ideal_size));
  EXPECT_EQ(sizeof(flash_info::Header::num_banks_total),
            sizeof(ec_response_flash_info_2::num_banks_total));
  EXPECT_EQ(sizeof(flash_info::Header::num_banks_desc),
            sizeof(ec_response_flash_info_2::num_banks_desc));
  EXPECT_EQ(sizeof(flash_info::Header), sizeof(ec_response_flash_info_2));
}

TEST(FlashInfoParams, ParamsSize) {
  EXPECT_EQ(sizeof(flash_info::Params_v2), kMaxPacketSize);
}

}  // namespace
}  // namespace ec
