// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/crc.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace cryptohome {
namespace {

using ::testing::Eq;

// Various spot check tests for a few different CRC8 values. This is not
// intended to be comprehensive.
TEST(Crc8Test, Empty) {
  // We don't actually use the stored value here, but C++ does not allow
  // zero-length arrays.
  const char kData[] = {0};
  EXPECT_THAT(Crc8(kData, 0), Eq(0));
}

TEST(Crc8Test, ZeroArray) {
  const char kData[] = {0, 0, 0, 0, 0, 0, 0, 0};
  EXPECT_THAT(Crc8(kData, sizeof(kData)), Eq(0));
}

TEST(Crc8Test, SomeBytes) {
  const char kData[] = {1, 2, 3, 4, 5, 6, 7, 8};
  EXPECT_THAT(Crc8(kData, sizeof(kData)), Eq(0x3e));
}

TEST(Crc8Test, MoreBytes) {
  const char kData[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
  EXPECT_THAT(Crc8(kData, sizeof(kData)), Eq(0xb0));
}

TEST(Crc8Test, AllOnes) {
  const unsigned char kData[] = {0xff, 0xff, 0xff, 0xff,
                                 0xff, 0xff, 0xff, 0xff};
  EXPECT_THAT(Crc8(kData, sizeof(kData)), Eq(0xd7));
}

}  // namespace
}  // namespace cryptohome
