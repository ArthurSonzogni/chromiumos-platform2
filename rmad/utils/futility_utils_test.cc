// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/utils/futility_utils_impl.h"

#include <memory>
#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "rmad/utils/hwid_utils_impl.h"
#include "rmad/utils/mock_cmd_utils.h"

using testing::_;
using testing::DoAll;
using testing::InSequence;
using testing::Return;
using testing::SetArgPointee;
using testing::StrictMock;

namespace {

constexpr char kFutilityWriteProtectEnabledOutput[] = R"(WP status: enabled.)";
constexpr char kFutilityWriteProtectDisabledOutput[] = R"(WP status: disabled)";
constexpr char kFutilityWriteProtectMisconfiguredOutput[] =
    R"(WP status: misconfigured (srp = 1, start = 0000000000, length = 0000000000))";

}  // namespace

namespace rmad {

class FutilityUtilsTest : public testing::Test {
 public:
  FutilityUtilsTest() = default;
  ~FutilityUtilsTest() override = default;
};

TEST_F(FutilityUtilsTest, GetApWriteProtectionStatus_Enabled) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(kFutilityWriteProtectEnabledOutput),
                      Return(true)));
  auto futility_utils = std::make_unique<FutilityUtilsImpl>(
      std::move(mock_cmd_utils), std::make_unique<HwidUtilsImpl>());

  bool enabled;
  EXPECT_TRUE(futility_utils->GetApWriteProtectionStatus(&enabled));
  EXPECT_TRUE(enabled);
}

TEST_F(FutilityUtilsTest, GetApWriteProtectionStatus_Disabled) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(kFutilityWriteProtectDisabledOutput),
                      Return(true)));
  auto futility_utils = std::make_unique<FutilityUtilsImpl>(
      std::move(mock_cmd_utils), std::make_unique<HwidUtilsImpl>());

  bool enabled;
  EXPECT_TRUE(futility_utils->GetApWriteProtectionStatus(&enabled));
  EXPECT_FALSE(enabled);
}

TEST_F(FutilityUtilsTest, GetApWriteProtectionStatus_Misconfigured) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(kFutilityWriteProtectMisconfiguredOutput),
                Return(true)));
  auto futility_utils = std::make_unique<FutilityUtilsImpl>(
      std::move(mock_cmd_utils), std::make_unique<HwidUtilsImpl>());

  bool enabled;
  EXPECT_TRUE(futility_utils->GetApWriteProtectionStatus(&enabled));
  EXPECT_TRUE(enabled);
}

TEST_F(FutilityUtilsTest, GetApWriteProtectionStatus_Failed) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _)).WillOnce(Return(false));
  auto futility_utils = std::make_unique<FutilityUtilsImpl>(
      std::move(mock_cmd_utils), std::make_unique<HwidUtilsImpl>());

  bool enabled;
  EXPECT_FALSE(futility_utils->GetApWriteProtectionStatus(&enabled));
}

TEST_F(FutilityUtilsTest, EnableApSoftwareWriteProtection_Success) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  {
    InSequence seq;
    // Futility set AP WP range.
    EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _)).WillOnce(Return(true));
  }
  auto futility_utils = std::make_unique<FutilityUtilsImpl>(
      std::move(mock_cmd_utils), std::make_unique<HwidUtilsImpl>());

  EXPECT_TRUE(futility_utils->EnableApSoftwareWriteProtection());
}

TEST_F(FutilityUtilsTest, EnableApSoftwareWriteProtection_EnableApWpFail) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  {
    InSequence seq;
    // Futility set AP WP range.
    EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _)).WillOnce(Return(false));
  }
  auto futility_utils = std::make_unique<FutilityUtilsImpl>(
      std::move(mock_cmd_utils), std::make_unique<HwidUtilsImpl>());

  EXPECT_FALSE(futility_utils->EnableApSoftwareWriteProtection());
}

TEST_F(FutilityUtilsTest, SetHwidSuccess) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutputAndError(_, _)).WillOnce(Return(true));
  auto futility_utils = std::make_unique<FutilityUtilsImpl>(
      std::move(mock_cmd_utils), std::make_unique<HwidUtilsImpl>());

  EXPECT_TRUE(futility_utils->SetHwid("MODEL-CODE A1B-C2D-E2J"));
}

TEST_F(FutilityUtilsTest, SetHwidInvalidHwidFormatFail) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutputAndError(_, _)).Times(0);
  auto futility_utils = std::make_unique<FutilityUtilsImpl>(
      std::move(mock_cmd_utils), std::make_unique<HwidUtilsImpl>());

  EXPECT_FALSE(futility_utils->SetHwid("MODEL-CODE A1BC2DE2J"));
}

TEST_F(FutilityUtilsTest, SetHwidIncorrectChecksumFail) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutputAndError(_, _)).Times(0);
  auto futility_utils = std::make_unique<FutilityUtilsImpl>(
      std::move(mock_cmd_utils), std::make_unique<HwidUtilsImpl>());

  EXPECT_FALSE(futility_utils->SetHwid("MODEL-CODE A1B-C2D-E2K"));
}

TEST_F(FutilityUtilsTest, SetHwidGetOutputFail) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutputAndError(_, _)).WillOnce(Return(false));
  auto futility_utils = std::make_unique<FutilityUtilsImpl>(
      std::move(mock_cmd_utils), std::make_unique<HwidUtilsImpl>());

  EXPECT_FALSE(futility_utils->SetHwid("MODEL-CODE A1B-C2D-E2J"));
}

}  // namespace rmad
