// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/utils/floss_utils.h"

#include <vector>

#include <base/uuid.h>

#include <gtest/gtest.h>

namespace diagnostics::floss_utils {
namespace {

TEST(FlossUtilsTest, ParseUuidBytes) {
  std::vector<uint8_t> uuid_bytes = {0x74, 0xec, 0x21, 0x72, 0x0b, 0xad,
                                     0x4d, 0x01, 0x8f, 0x77, 0x99, 0x7b,
                                     0x2b, 0xe0, 0x72, 0x2a};
  EXPECT_EQ(ParseUuidBytes(uuid_bytes),
            base::Uuid::ParseLowercase("74ec2172-0bad-4d01-8f77-997b2be0722a"));
}

// Bluetooth base UUID format: (0000xxxx-0000-1000-8000-00805f9b34fb).
TEST(FlossUtilsTest, ParseUuidBytesBluetoothBase) {
  std::vector<uint8_t> uuid_bytes = {0x00, 0x00, 0x11, 0x0a, 0x00, 0x00,
                                     0x10, 0x00, 0x80, 0x00, 0x00, 0x80,
                                     0x5f, 0x9b, 0x34, 0xfb};
  EXPECT_EQ(ParseUuidBytes(uuid_bytes),
            base::Uuid::ParseLowercase("0000110a-0000-1000-8000-00805f9b34fb"));
}

TEST(FlossUtilsTest, ParseUuidBytesAllZero) {
  std::vector<uint8_t> uuid_bytes = {0, 0, 0, 0, 0, 0, 0, 0,
                                     0, 0, 0, 0, 0, 0, 0, 0};
  EXPECT_EQ(ParseUuidBytes(uuid_bytes),
            base::Uuid::ParseLowercase("00000000-0000-0000-0000-000000000000"));
}

TEST(FlossUtilsTest, ParseUuidBytesEmpty) {
  EXPECT_FALSE(ParseUuidBytes({}).is_valid());
}

TEST(FlossUtilsTest, ParseUuidBytesWrongBytesSize) {
  EXPECT_FALSE(ParseUuidBytes({0, 1, 2, 3}).is_valid());
}

}  // namespace
}  // namespace diagnostics::floss_utils
