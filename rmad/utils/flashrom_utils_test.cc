// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/utils/flashrom_utils_impl.h"

#include <memory>
#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "rmad/utils/mock_cmd_utils.h"

using testing::_;
using testing::DoAll;
using testing::InSequence;
using testing::Return;
using testing::SetArgPointee;
using testing::StrictMock;

namespace {

constexpr char kWriteProtectEnabledOutput[] =
    R"(WP: write protect is enabled.)";
constexpr char kWriteProtectDisabledOutput[] =
    R"(WP: write protect is disabled.)";
constexpr char kFmapOutput[] =
    R"(area_offset="0x10" area_size="0x20" area_name="WP_RO")";
constexpr char kFmapErrorOutput[] =
    R"(area_offset="0x10" area_size="0x20" area_name="RO")";

}  // namespace

namespace rmad {

class FlashromUtilsTest : public testing::Test {
 public:
  FlashromUtilsTest() = default;
  ~FlashromUtilsTest() override = default;
};

TEST_F(FlashromUtilsTest, GetApWriteProtectionStatus_Enabled) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(kWriteProtectEnabledOutput), Return(true)));
  auto flashrom_utils =
      std::make_unique<FlashromUtilsImpl>(std::move(mock_cmd_utils));

  bool enabled;
  EXPECT_TRUE(flashrom_utils->GetApWriteProtectionStatus(&enabled));
  EXPECT_TRUE(enabled);
}

TEST_F(FlashromUtilsTest, GetApWriteProtectionStatus_Disabled) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(kWriteProtectDisabledOutput), Return(true)));
  auto flashrom_utils =
      std::make_unique<FlashromUtilsImpl>(std::move(mock_cmd_utils));

  bool enabled;
  EXPECT_TRUE(flashrom_utils->GetApWriteProtectionStatus(&enabled));
  EXPECT_FALSE(enabled);
}

TEST_F(FlashromUtilsTest, GetApWriteProtectionStatus_Failed) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _)).WillOnce(Return(false));
  auto flashrom_utils =
      std::make_unique<FlashromUtilsImpl>(std::move(mock_cmd_utils));

  bool enabled;
  EXPECT_FALSE(flashrom_utils->GetApWriteProtectionStatus(&enabled));
}

TEST_F(FlashromUtilsTest, GetEcWriteProtectionStatus_Enabled) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(kWriteProtectEnabledOutput), Return(true)));
  auto flashrom_utils =
      std::make_unique<FlashromUtilsImpl>(std::move(mock_cmd_utils));

  bool enabled;
  EXPECT_TRUE(flashrom_utils->GetEcWriteProtectionStatus(&enabled));
  EXPECT_TRUE(enabled);
}

TEST_F(FlashromUtilsTest, GetEcWriteProtectionStatus_Disabled) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(kWriteProtectDisabledOutput), Return(true)));
  auto flashrom_utils =
      std::make_unique<FlashromUtilsImpl>(std::move(mock_cmd_utils));

  bool enabled;
  EXPECT_TRUE(flashrom_utils->GetEcWriteProtectionStatus(&enabled));
  EXPECT_FALSE(enabled);
}

TEST_F(FlashromUtilsTest, GetEcWriteProtectionStatus_Failed) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _)).WillOnce(Return(false));
  auto flashrom_utils =
      std::make_unique<FlashromUtilsImpl>(std::move(mock_cmd_utils));

  bool enabled;
  EXPECT_FALSE(flashrom_utils->GetEcWriteProtectionStatus(&enabled));
}

TEST_F(FlashromUtilsTest, EnableSoftwareWriteProtection_Success) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  {
    InSequence seq;
    // Flashrom read.
    EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _)).WillOnce(Return(true));
    // Parse fmap.
    EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _))
        .WillOnce(DoAll(SetArgPointee<1>(kFmapOutput), Return(true)));
    // Flashrom set AP WP range.
    EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _)).WillOnce(Return(true));
    // Flashrom set EC WP range.
    EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _)).WillOnce(Return(true));
  }
  auto flashrom_utils =
      std::make_unique<FlashromUtilsImpl>(std::move(mock_cmd_utils));

  EXPECT_TRUE(flashrom_utils->EnableSoftwareWriteProtection());
}

TEST_F(FlashromUtilsTest, EnableSoftwareWriteProtection_ReadFail) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  {
    InSequence seq;
    // Flashrom read.
    EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _)).WillOnce(Return(false));
  }
  auto flashrom_utils =
      std::make_unique<FlashromUtilsImpl>(std::move(mock_cmd_utils));

  EXPECT_FALSE(flashrom_utils->EnableSoftwareWriteProtection());
}

TEST_F(FlashromUtilsTest, EnableSoftwareWriteProtection_FmapCmdFail) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  {
    InSequence seq;
    // Flashrom read.
    EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _)).WillOnce(Return(true));
    // Parse fmap.
    EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _)).WillOnce(Return(false));
  }
  auto flashrom_utils =
      std::make_unique<FlashromUtilsImpl>(std::move(mock_cmd_utils));

  EXPECT_FALSE(flashrom_utils->EnableSoftwareWriteProtection());
}

TEST_F(FlashromUtilsTest, EnableSoftwareWriteProtection_FmapParseFail) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  {
    InSequence seq;
    // Flashrom read.
    EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _)).WillOnce(Return(true));
    // Parse fmap.
    EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _))
        .WillOnce(DoAll(SetArgPointee<1>(kFmapErrorOutput), Return(true)));
  }
  auto flashrom_utils =
      std::make_unique<FlashromUtilsImpl>(std::move(mock_cmd_utils));

  EXPECT_FALSE(flashrom_utils->EnableSoftwareWriteProtection());
}

TEST_F(FlashromUtilsTest, EnableSoftwareWriteProtection_EnableApWpFail) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  {
    InSequence seq;
    // Flashrom read.
    EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _)).WillOnce(Return(true));
    // Parse fmap.
    EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _))
        .WillOnce(DoAll(SetArgPointee<1>(kFmapOutput), Return(true)));
    // Flashrom set AP WP range.
    EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _)).WillOnce(Return(false));
  }
  auto flashrom_utils =
      std::make_unique<FlashromUtilsImpl>(std::move(mock_cmd_utils));

  EXPECT_FALSE(flashrom_utils->EnableSoftwareWriteProtection());
}

TEST_F(FlashromUtilsTest, EnableSoftwareWriteProtection_EnableEcWpFail) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  {
    InSequence seq;
    // Flashrom read.
    EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _)).WillOnce(Return(true));
    // Parse fmap.
    EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _))
        .WillOnce(DoAll(SetArgPointee<1>(kFmapOutput), Return(true)));
    // Flashrom set AP WP range.
    EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _)).WillOnce(Return(true));
    // Flashrom set EC WP range.
    EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _)).WillOnce(Return(false));
  }
  auto flashrom_utils =
      std::make_unique<FlashromUtilsImpl>(std::move(mock_cmd_utils));

  EXPECT_FALSE(flashrom_utils->EnableSoftwareWriteProtection());
}

}  // namespace rmad
