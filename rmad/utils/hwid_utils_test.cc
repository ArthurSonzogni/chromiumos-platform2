// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <rmad/utils/hwid_utils_impl.h>

#include <optional>

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

TEST_F(HwidUtilsTest, VerifyHwidFormatInvalidFirstPartFail) {
  auto hwid_utils = std::make_unique<HwidUtilsImpl>();

  std::string output;

  EXPECT_FALSE(hwid_utils->VerifyHwidFormat("MODEL-CODE-INVALID A1B-C2D-E2J",
                                            /*has_checksum=*/true));
}

TEST_F(HwidUtilsTest, DecomposeHwidSuccess) {
  auto hwid_utils = std::make_unique<HwidUtilsImpl>();

  HwidElements expected = {.model_name = "MODEL",
                           .brand_code = "CODE",
                           .encoded_components = "A1B-C2D-E",
                           .checksum = "2J"};

  const auto result = hwid_utils->DecomposeHwid("MODEL-CODE A1B-C2D-E2J");

  EXPECT_TRUE(result.has_value());
  EXPECT_EQ(result.value(), expected);
}

TEST_F(HwidUtilsTest, DecomposeHwidModelOnlySuccess) {
  auto hwid_utils = std::make_unique<HwidUtilsImpl>();

  HwidElements expected = {.model_name = "MODEL",
                           .brand_code = std::nullopt,
                           .encoded_components = "A1B-C2D-E",
                           .checksum = "2J"};

  const auto result = hwid_utils->DecomposeHwid("MODEL A1B-C2D-E2J");

  EXPECT_TRUE(result.has_value());
  EXPECT_EQ(result.value(), expected);
}

TEST_F(HwidUtilsTest, DecomposeTestHwidFail) {
  auto hwid_utils = std::make_unique<HwidUtilsImpl>();

  const auto result = hwid_utils->DecomposeHwid("MODEL TEST 1126");

  EXPECT_FALSE(result.has_value());
}

TEST_F(HwidUtilsTest, DecomposeHwidInvalidLengthFail) {
  auto hwid_utils = std::make_unique<HwidUtilsImpl>();

  const auto result = hwid_utils->DecomposeHwid("MODEL-CODE A1B-C2D-E");

  EXPECT_FALSE(result.has_value());
}

}  // namespace rmad
