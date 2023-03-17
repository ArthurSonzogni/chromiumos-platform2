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

constexpr char kFlashromWriteProtectEnabledOutput[] =
    R"(WP: write protect is enabled.)";
constexpr char kFlashromWriteProtectDisabledOutput[] =
    R"(WP: write protect is disabled.)";

constexpr char kFutilityWriteProtectEnabledOutput[] = R"(WP status: enabled.)";
constexpr char kFutilityWriteProtectDisabledOutput[] = R"(WP status: disabled)";
constexpr char kFutilityWriteProtectMisconfiguredOutput[] =
    R"(WP status: misconfigured (srp = 1, start = 0000000000, length = 0000000000))";

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
      .WillOnce(DoAll(SetArgPointee<1>(kFutilityWriteProtectEnabledOutput),
                      Return(true)));
  auto flashrom_utils =
      std::make_unique<FlashromUtilsImpl>(std::move(mock_cmd_utils));

  bool enabled;
  EXPECT_TRUE(flashrom_utils->GetApWriteProtectionStatus(&enabled));
  EXPECT_TRUE(enabled);
}

TEST_F(FlashromUtilsTest, GetApWriteProtectionStatus_Disabled) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(kFutilityWriteProtectDisabledOutput),
                      Return(true)));
  auto flashrom_utils =
      std::make_unique<FlashromUtilsImpl>(std::move(mock_cmd_utils));

  bool enabled;
  EXPECT_TRUE(flashrom_utils->GetApWriteProtectionStatus(&enabled));
  EXPECT_FALSE(enabled);
}

TEST_F(FlashromUtilsTest, GetApWriteProtectionStatus_Misconfigured) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(kFutilityWriteProtectMisconfiguredOutput),
                Return(true)));
  auto flashrom_utils =
      std::make_unique<FlashromUtilsImpl>(std::move(mock_cmd_utils));

  bool enabled;
  EXPECT_TRUE(flashrom_utils->GetApWriteProtectionStatus(&enabled));
  EXPECT_TRUE(enabled);
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
      .WillOnce(DoAll(SetArgPointee<1>(kFlashromWriteProtectEnabledOutput),
                      Return(true)));
  auto flashrom_utils =
      std::make_unique<FlashromUtilsImpl>(std::move(mock_cmd_utils));

  bool enabled;
  EXPECT_TRUE(flashrom_utils->GetEcWriteProtectionStatus(&enabled));
  EXPECT_TRUE(enabled);
}

TEST_F(FlashromUtilsTest, GetEcWriteProtectionStatus_Disabled) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(kFlashromWriteProtectDisabledOutput),
                      Return(true)));
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

TEST_F(FlashromUtilsTest, EnableApSoftwareWriteProtection_Success) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  {
    InSequence seq;
    // Futility set AP WP range.
    EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _)).WillOnce(Return(true));
  }
  auto flashrom_utils =
      std::make_unique<FlashromUtilsImpl>(std::move(mock_cmd_utils));

  EXPECT_TRUE(flashrom_utils->EnableApSoftwareWriteProtection());
}

TEST_F(FlashromUtilsTest, EnableApSoftwareWriteProtection_EnableApWpFail) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  {
    InSequence seq;
    // Futtility set AP WP range.
    EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _)).WillOnce(Return(false));
  }
  auto flashrom_utils =
      std::make_unique<FlashromUtilsImpl>(std::move(mock_cmd_utils));

  EXPECT_FALSE(flashrom_utils->EnableApSoftwareWriteProtection());
}

}  // namespace rmad
