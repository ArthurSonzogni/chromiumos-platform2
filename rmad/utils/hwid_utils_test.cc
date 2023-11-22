// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <rmad/utils/hwid_utils_impl.h>

#include <gtest/gtest.h>

namespace rmad {

class HwidUtilsTest : public testing::Test {
 public:
  HwidUtilsTest() = default;
  ~HwidUtilsTest() override = default;
};

TEST_F(HwidUtilsTest, VerifyChecksumSuccess) {
  auto hwid_utils = std::make_unique<HwidUtilsImpl>();

  EXPECT_TRUE(hwid_utils->VerifyChecksum("MODEL-CODE A1B-C2D-E2J"));
}

TEST_F(HwidUtilsTest, VerifyChecksumInvalidLengthFail) {
  auto hwid_utils = std::make_unique<HwidUtilsImpl>();

  EXPECT_FALSE(hwid_utils->VerifyChecksum("HI"));
}

TEST_F(HwidUtilsTest, VerifyChecksumFail) {
  auto hwid_utils = std::make_unique<HwidUtilsImpl>();

  EXPECT_FALSE(hwid_utils->VerifyChecksum("MODEL-CODE A1B-C2D-E2K"));
}

TEST_F(HwidUtilsTest, VerifyHwidFormatSuccess) {
  auto hwid_utils = std::make_unique<HwidUtilsImpl>();

  EXPECT_TRUE(hwid_utils->VerifyHwidFormat("MODEL-CODE A1B-C2D-E2J",
                                           /*has_checksum=*/true));
}

TEST_F(HwidUtilsTest, VerifyHwidFormatFail) {
  auto hwid_utils = std::make_unique<HwidUtilsImpl>();

  EXPECT_FALSE(hwid_utils->VerifyHwidFormat("MODEL-CODE A1B-C2D-E",
                                            /*has_checksum=*/true));
}

TEST_F(HwidUtilsTest, VerifyHwidFormatNoChecksumSuccess) {
  auto hwid_utils = std::make_unique<HwidUtilsImpl>();

  EXPECT_TRUE(hwid_utils->VerifyHwidFormat("MODEL-CODE A1B-C2D-E",
                                           /*has_checksum=*/false));
}

TEST_F(HwidUtilsTest, VerifyHwidFormatNoChecksumFail) {
  auto hwid_utils = std::make_unique<HwidUtilsImpl>();

  EXPECT_FALSE(hwid_utils->VerifyHwidFormat("MODEL-CODE A1B-C2D-E2J",
                                            /*has_checksum=*/false));
}

TEST_F(HwidUtilsTest, VerifyHwidFormatModelBrandCodeOnlyFail) {
  auto hwid_utils = std::make_unique<HwidUtilsImpl>();

  std::string output;

  EXPECT_FALSE(
      hwid_utils->VerifyHwidFormat("MODEL-CODE", /*has_checksum=*/true));
}

TEST_F(HwidUtilsTest, VerifyHwidFormatTestHwidFail) {
  auto hwid_utils = std::make_unique<HwidUtilsImpl>();

  std::string output;

  EXPECT_FALSE(hwid_utils->VerifyHwidFormat("MODEL-CODE TEST 1126",
                                            /*has_checksum=*/true));
}

}  // namespace rmad
