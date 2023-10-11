// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/utils/floss_utils.h"

#include <vector>

#include <gtest/gtest.h>

namespace diagnostics::floss_utils {
namespace {

TEST(FlossUtilsTest, ParseUuidBytes) {
  std::vector<uint8_t> uuid_bytes = {0x00, 0x00, 0x11, 0x0a, 0x00, 0x00,
                                     0x10, 0x00, 0x80, 0x00, 0x00, 0x80,
                                     0x5f, 0x9b, 0x34, 0xfb};
  EXPECT_EQ(ParseUuidBytes(uuid_bytes), "0000110a-0000-1000-8000-00805f9b34fb");
}

TEST(FlossUtilsTest, ParseUuidBytesAllZero) {
  std::vector<uint8_t> uuid_bytes = {0, 0, 0, 0, 0, 0, 0, 0,
                                     0, 0, 0, 0, 0, 0, 0, 0};
  EXPECT_EQ(ParseUuidBytes(uuid_bytes), "00000000-0000-0000-0000-000000000000");
}

TEST(FlossUtilsTest, ParseUuidBytesEmpty) {
  EXPECT_EQ(ParseUuidBytes({}), std::nullopt);
}

TEST(FlossUtilsTest, ParseUuidBytesWrongBytesSize) {
  EXPECT_EQ(ParseUuidBytes({0, 1, 2, 3}), std::nullopt);
}

}  // namespace
}  // namespace diagnostics::floss_utils
