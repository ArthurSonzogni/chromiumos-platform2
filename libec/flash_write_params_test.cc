// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include "libec/flash_write_params.h"

namespace ec {
namespace {

TEST(FLashWriteParams, HeaderSize) {
  EXPECT_EQ(sizeof(flash_write::Header::offset),
            sizeof(ec_params_flash_write::offset));
  EXPECT_EQ(sizeof(flash_write::Header::size),
            sizeof(ec_params_flash_write::size));
  EXPECT_EQ(sizeof(flash_write::Header), sizeof(ec_params_flash_write));
}

TEST(FlashWriteParams, ParamsSize) {
  EXPECT_EQ(sizeof(flash_write::Params), kMaxPacketSize);
}

}  // namespace
}  // namespace ec
