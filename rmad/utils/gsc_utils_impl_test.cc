// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/utils/gsc_utils_impl.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <brillo/file_utils.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "rmad/constants.h"
#include "rmad/utils/gsc_utils.h"
#include "rmad/utils/mock_cmd_utils.h"

using testing::_;
using testing::DoAll;
using testing::InSequence;
using testing::Return;
using testing::SetArgPointee;
using testing::StrictMock;

namespace {

// Constants for RSU
constexpr char kGetChallengeCodeResponse[] =
    "CHALLENGE="
    "AAAAABBBBBCCCCCDDDDDEEEEEFFFFFGGGGGHHHHH"
    "1111122222333334444455555666667777788888\n";
constexpr char kExpectedChallengeCode[] =
    "AAAAABBBBBCCCCCDDDDDEEEEEFFFFFGGGGGHHHHH"
    "1111122222333334444455555666667777788888";

// Constants for CCD info.
constexpr char kFactoryModeEnabledResponse[] = R"(
STATE=Locked
---
---
CCD_FLAG_FACTORY_MODE=Y
---
)";
constexpr char kFactoryModeDisabledResponse[] = R"(
STATE=Locked
---
---
CCD_FLAG_FACTORY_MODE=N
---
)";
constexpr char kInitialFactoryModeEnabledResponse[] = R"(
STATE=Locked
---
---
INITIAL_FACTORY_MODE=Y
---
)";
constexpr char kInitialFactoryModeDisabledResponse[] = R"(
STATE=Locked
---
---
INITIAL_FACTORY_MODE=N
---
)";

// Constants for board ID.
constexpr char kGetBoardIdResponse[] = R"(
BID_TYPE=5a5a4352
BID_TYPE_INV=a5a5bcad
BID_FLAGS=00007f80
BID_RLZ=ZZCR
)";
constexpr char kExpectedBoardIdType[] = "5a5a4352";
constexpr char kExpectedBoardIdFlags[] = "00007f80";

// Constants for factory config.
constexpr char kGetFactoryConfigResponse[] = R"(
raw value: 0000000000000012
other fields: don't care
)";
constexpr char kGetFactoryConfigErrorResponse[] = R"(
raw value: 000000000000001
other fields: don't care
)";
constexpr bool kExpectedIsChassisBranded = true;
constexpr int kExpectedHwComplianceVersion = 2;

// Constants for CHASSIS_OPEN status.
constexpr char kGetChassisOpenStatusResponseTrue[] = R"(
Chassis Open: true
)";
constexpr char kGetChassisOpenStatusResponseFalse[] = R"(
Chassis Open: false
)";
constexpr char kGetChassisOpenStatusResponseInvalid[] = R"(
Chassis Open: ITS_INVALID
)";

}  // namespace

namespace rmad {

class GscUtilsTest : public testing::Test {
 public:
  GscUtilsTest() = default;
  ~GscUtilsTest() override = default;
};

TEST_F(GscUtilsTest, GetRsuChallengeCode_Success) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(kGetChallengeCodeResponse), Return(true)));
  auto gsc_utils = std::make_unique<GscUtilsImpl>(std::move(mock_cmd_utils));

  std::string challenge_code;
  EXPECT_TRUE(gsc_utils->GetRsuChallengeCode(&challenge_code));
  EXPECT_EQ(challenge_code, kExpectedChallengeCode);
}

TEST_F(GscUtilsTest, GetRsuChallengeCode_Fail) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _)).WillOnce(Return(false));
  auto gsc_utils = std::make_unique<GscUtilsImpl>(std::move(mock_cmd_utils));

  std::string challenge_code;
  EXPECT_FALSE(gsc_utils->GetRsuChallengeCode(&challenge_code));
}

TEST_F(GscUtilsTest, PerformRsu_Success) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _)).WillOnce(Return(true));
  auto gsc_utils = std::make_unique<GscUtilsImpl>(std::move(mock_cmd_utils));

  EXPECT_TRUE(gsc_utils->PerformRsu(""));
}

TEST_F(GscUtilsTest, PerformRsu_Fail) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _)).WillOnce(Return(false));
  auto gsc_utils = std::make_unique<GscUtilsImpl>(std::move(mock_cmd_utils));

  EXPECT_FALSE(gsc_utils->PerformRsu(""));
}

TEST_F(GscUtilsTest, IsFactoryModeEnabled_Enabled) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(kFactoryModeEnabledResponse), Return(true)));
  auto gsc_utils = std::make_unique<GscUtilsImpl>(std::move(mock_cmd_utils));

  EXPECT_TRUE(gsc_utils->IsFactoryModeEnabled());
}

TEST_F(GscUtilsTest, IsFactoryModeEnabled_Disabled) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(kFactoryModeDisabledResponse), Return(true)));
  auto gsc_utils = std::make_unique<GscUtilsImpl>(std::move(mock_cmd_utils));

  EXPECT_FALSE(gsc_utils->IsFactoryModeEnabled());
}

TEST_F(GscUtilsTest, IsFactoryModeEnabled_NoResponse) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _)).WillOnce(Return(false));
  auto gsc_utils = std::make_unique<GscUtilsImpl>(std::move(mock_cmd_utils));

  EXPECT_FALSE(gsc_utils->IsFactoryModeEnabled());
}

TEST_F(GscUtilsTest, EnableFactoryMode_Success) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  {
    InSequence seq;
    EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _))
        .WillOnce(DoAll(SetArgPointee<1>(kFactoryModeDisabledResponse),
                        Return(true)));
    EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _)).WillOnce(Return(true));
  }
  auto gsc_utils = std::make_unique<GscUtilsImpl>(std::move(mock_cmd_utils));

  EXPECT_TRUE(gsc_utils->EnableFactoryMode());
}

TEST_F(GscUtilsTest, EnableFactoryMode_Fail) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  {
    InSequence seq;
    EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _))
        .WillOnce(DoAll(SetArgPointee<1>(kFactoryModeDisabledResponse),
                        Return(true)));
    EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _)).WillOnce(Return(false));
  }
  auto gsc_utils = std::make_unique<GscUtilsImpl>(std::move(mock_cmd_utils));

  EXPECT_FALSE(gsc_utils->EnableFactoryMode());
}

TEST_F(GscUtilsTest, EnableFactoryMode_AlreadyEnabled) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(kFactoryModeEnabledResponse), Return(true)));
  auto gsc_utils = std::make_unique<GscUtilsImpl>(std::move(mock_cmd_utils));

  EXPECT_TRUE(gsc_utils->EnableFactoryMode());
}

TEST_F(GscUtilsTest, DisableFactoryMode_Success) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  {
    InSequence seq;
    EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _))
        .WillOnce(
            DoAll(SetArgPointee<1>(kFactoryModeEnabledResponse), Return(true)));
    EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _)).WillOnce(Return(true));
  }
  auto gsc_utils = std::make_unique<GscUtilsImpl>(std::move(mock_cmd_utils));

  EXPECT_TRUE(gsc_utils->DisableFactoryMode());
}

TEST_F(GscUtilsTest, DisableFactoryMode_Fail) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  {
    InSequence seq;
    EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _))
        .WillOnce(
            DoAll(SetArgPointee<1>(kFactoryModeEnabledResponse), Return(true)));
    EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _)).WillOnce(Return(false));
  }
  auto gsc_utils = std::make_unique<GscUtilsImpl>(std::move(mock_cmd_utils));

  EXPECT_FALSE(gsc_utils->DisableFactoryMode());
}

TEST_F(GscUtilsTest, DisableFactoryMode_AlreadyDisabled) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(kFactoryModeDisabledResponse), Return(true)));
  auto gsc_utils = std::make_unique<GscUtilsImpl>(std::move(mock_cmd_utils));

  EXPECT_TRUE(gsc_utils->DisableFactoryMode());
}

TEST_F(GscUtilsTest, IsInitialFactoryModeEnabled_Enabled) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(kInitialFactoryModeEnabledResponse),
                      Return(true)));
  auto gsc_utils = std::make_unique<GscUtilsImpl>(std::move(mock_cmd_utils));

  EXPECT_TRUE(gsc_utils->IsInitialFactoryModeEnabled());
}

TEST_F(GscUtilsTest, IsInitialFactoryModeEnabled_Disabled) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(kInitialFactoryModeDisabledResponse),
                      Return(true)));
  auto gsc_utils = std::make_unique<GscUtilsImpl>(std::move(mock_cmd_utils));

  EXPECT_FALSE(gsc_utils->IsInitialFactoryModeEnabled());
}

TEST_F(GscUtilsTest, IsInitialFactoryModeEnabled_NoResponse) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _)).WillOnce(Return(false));
  auto gsc_utils = std::make_unique<GscUtilsImpl>(std::move(mock_cmd_utils));

  EXPECT_FALSE(gsc_utils->IsInitialFactoryModeEnabled());
}

TEST_F(GscUtilsTest, GetBoardIdType_Success) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(kGetBoardIdResponse), Return(true)));
  auto gsc_utils = std::make_unique<GscUtilsImpl>(std::move(mock_cmd_utils));

  auto board_id_type = gsc_utils->GetBoardIdType();
  EXPECT_TRUE(board_id_type.has_value());
  EXPECT_EQ(board_id_type.value(), kExpectedBoardIdType);
}

TEST_F(GscUtilsTest, GetBoardIdType_Fail) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _)).WillOnce(Return(false));
  auto gsc_utils = std::make_unique<GscUtilsImpl>(std::move(mock_cmd_utils));

  auto board_id_type = gsc_utils->GetBoardIdType();
  EXPECT_FALSE(board_id_type.has_value());
}

TEST_F(GscUtilsTest, GetBoardIdFlags_Success) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(kGetBoardIdResponse), Return(true)));
  auto gsc_utils = std::make_unique<GscUtilsImpl>(std::move(mock_cmd_utils));

  auto board_id_flags = gsc_utils->GetBoardIdFlags();
  EXPECT_TRUE(board_id_flags.has_value());
  EXPECT_EQ(board_id_flags.value(), kExpectedBoardIdFlags);
}

TEST_F(GscUtilsTest, GetBoardIdFlags_Fail) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _)).WillOnce(Return(false));
  auto gsc_utils = std::make_unique<GscUtilsImpl>(std::move(mock_cmd_utils));

  auto board_id_flags = gsc_utils->GetBoardIdFlags();
  EXPECT_FALSE(board_id_flags.has_value());
}

TEST_F(GscUtilsTest, SetBoardId_Success) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutputAndError(_, _)).WillOnce(Return(true));
  auto gsc_utils = std::make_unique<GscUtilsImpl>(std::move(mock_cmd_utils));

  EXPECT_TRUE(gsc_utils->SetBoardId(true));
}

TEST_F(GscUtilsTest, SetBoardId_Fail) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutputAndError(_, _)).WillOnce(Return(false));
  auto gsc_utils = std::make_unique<GscUtilsImpl>(std::move(mock_cmd_utils));

  EXPECT_FALSE(gsc_utils->SetBoardId(true));
}

TEST_F(GscUtilsTest, Reboot_Success) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _)).WillOnce(Return(true));
  auto gsc_utils = std::make_unique<GscUtilsImpl>(std::move(mock_cmd_utils));

  EXPECT_TRUE(gsc_utils->Reboot());
}

TEST_F(GscUtilsTest, GetFactoryConfig_Success) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(kGetFactoryConfigResponse), Return(true)));
  auto gsc_utils = std::make_unique<GscUtilsImpl>(std::move(mock_cmd_utils));

  auto factory_config = gsc_utils->GetFactoryConfig();
  EXPECT_TRUE(factory_config.has_value());
  EXPECT_EQ(factory_config->is_chassis_branded, kExpectedIsChassisBranded);
  EXPECT_EQ(factory_config->hw_compliance_version,
            kExpectedHwComplianceVersion);
}

TEST_F(GscUtilsTest, GetFactoryConfig_CommandFailed) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _)).WillOnce(Return(false));
  auto gsc_utils = std::make_unique<GscUtilsImpl>(std::move(mock_cmd_utils));

  auto factory_config = gsc_utils->GetFactoryConfig();
  EXPECT_FALSE(factory_config.has_value());
}

TEST_F(GscUtilsTest, GetFactoryConfig_ParseFailed) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(kGetFactoryConfigErrorResponse),
                      Return(true)));
  auto gsc_utils = std::make_unique<GscUtilsImpl>(std::move(mock_cmd_utils));

  auto factory_config = gsc_utils->GetFactoryConfig();
  EXPECT_FALSE(factory_config.has_value());
}

TEST_F(GscUtilsTest, SetFactoryConfig_Success) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _)).WillOnce(Return(true));
  auto gsc_utils = std::make_unique<GscUtilsImpl>(std::move(mock_cmd_utils));

  EXPECT_TRUE(gsc_utils->SetFactoryConfig(true, 1));
}

TEST_F(GscUtilsTest, SetFactoryConfig_Failed) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _)).WillOnce(Return(false));
  auto gsc_utils = std::make_unique<GscUtilsImpl>(std::move(mock_cmd_utils));

  EXPECT_FALSE(gsc_utils->SetFactoryConfig(true, 1));
}

TEST_F(GscUtilsTest, GetChassisOpenStatus_Success_True) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(kGetChassisOpenStatusResponseTrue),
                      Return(true)));
  auto gsc_utils = std::make_unique<GscUtilsImpl>(std::move(mock_cmd_utils));

  auto status = gsc_utils->GetChassisOpenStatus();
  EXPECT_TRUE(status.has_value());
  EXPECT_TRUE(status.value());
}

TEST_F(GscUtilsTest, GetChassisOpenStatus_Success_False) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(kGetChassisOpenStatusResponseFalse),
                      Return(true)));
  auto gsc_utils = std::make_unique<GscUtilsImpl>(std::move(mock_cmd_utils));

  auto status = gsc_utils->GetChassisOpenStatus();
  EXPECT_TRUE(status.has_value());
  EXPECT_FALSE(status.value());
}

TEST_F(GscUtilsTest, GetChassisOpenStatus_Failed) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(kGetChassisOpenStatusResponseTrue),
                      Return(false)));
  auto gsc_utils = std::make_unique<GscUtilsImpl>(std::move(mock_cmd_utils));

  auto status = gsc_utils->GetChassisOpenStatus();
  EXPECT_FALSE(status.has_value());
}

TEST_F(GscUtilsTest, GetChassisOpenStatus_Failed_Invalid) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(kGetChassisOpenStatusResponseInvalid),
                      Return(true)));
  auto gsc_utils = std::make_unique<GscUtilsImpl>(std::move(mock_cmd_utils));

  auto status = gsc_utils->GetChassisOpenStatus();
  EXPECT_FALSE(status.has_value());
}

TEST_F(GscUtilsTest, GetAddressingMode_Success) {
  // "3byte".
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutputAndError(_, _))
      .WillOnce(DoAll(SetArgPointee<1>("3byte"), Return(true)));
  auto gsc_utils = std::make_unique<GscUtilsImpl>(std::move(mock_cmd_utils));
  EXPECT_EQ(gsc_utils->GetAddressingMode(), SpiAddressingMode::k3Byte);

  // "4byte".
  mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutputAndError(_, _))
      .WillOnce(DoAll(SetArgPointee<1>("4byte"), Return(true)));
  gsc_utils = std::make_unique<GscUtilsImpl>(std::move(mock_cmd_utils));
  EXPECT_EQ(gsc_utils->GetAddressingMode(), SpiAddressingMode::k4Byte);

  // "not provisioned".
  mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutputAndError(_, _))
      .WillOnce(DoAll(SetArgPointee<1>("not provisioned"), Return(true)));
  gsc_utils = std::make_unique<GscUtilsImpl>(std::move(mock_cmd_utils));
  EXPECT_EQ(gsc_utils->GetAddressingMode(), SpiAddressingMode::kNotProvisioned);
}

TEST_F(GscUtilsTest, GetAddressingMode_Failed) {
  // "Invalid format".
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutputAndError(_, _))
      .WillOnce(DoAll(SetArgPointee<1>("invalid"), Return(true)));
  auto gsc_utils = std::make_unique<GscUtilsImpl>(std::move(mock_cmd_utils));
  auto result = gsc_utils->GetAddressingMode();
  EXPECT_EQ(result, SpiAddressingMode::kUnknown);

  // |cmd_utils| errors.
  mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutputAndError(_, _)).WillOnce(Return(false));
  gsc_utils = std::make_unique<GscUtilsImpl>(std::move(mock_cmd_utils));
  result = gsc_utils->GetAddressingMode();
  EXPECT_EQ(result, SpiAddressingMode::kUnknown);
}

TEST_F(GscUtilsTest, SetAddressingMode_Success) {
  // 0x0001000 -> "3byte".
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(
      *mock_cmd_utils,
      GetOutputAndError(
          testing::ElementsAreArray({"gsctool", "-a", "-C", "3byte"}), _))
      .WillOnce(Return(true));
  auto gsc_utils = std::make_unique<GscUtilsImpl>(std::move(mock_cmd_utils));
  EXPECT_TRUE(gsc_utils->SetAddressingMode(SpiAddressingMode::k3Byte));

  // 0x1000001 -> "4byte".
  mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(
      *mock_cmd_utils,
      GetOutputAndError(
          testing::ElementsAreArray({"gsctool", "-a", "-C", "4byte"}), _))
      .WillOnce(Return(true));
  gsc_utils = std::make_unique<GscUtilsImpl>(std::move(mock_cmd_utils));
  EXPECT_TRUE(gsc_utils->SetAddressingMode(SpiAddressingMode::k4Byte));
}

TEST_F(GscUtilsTest, GetAddressingModeByFlashSize) {
  auto gsc_utils = std::make_unique<GscUtilsImpl>();

  EXPECT_EQ(gsc_utils->GetAddressingModeByFlashSize(0x0001000),
            SpiAddressingMode::k3Byte);
  EXPECT_EQ(gsc_utils->GetAddressingModeByFlashSize(0x1000000),
            SpiAddressingMode::k3Byte);
  EXPECT_EQ(gsc_utils->GetAddressingModeByFlashSize(0x1000001),
            SpiAddressingMode::k4Byte);
}

TEST_F(GscUtilsTest, SetWpsr_Success) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutputAndError(_, _)).WillOnce(Return(true));
  auto gsc_utils = std::make_unique<GscUtilsImpl>(std::move(mock_cmd_utils));

  EXPECT_TRUE(gsc_utils->SetWpsr("0xA2 0x01 0x00 0x4A 0x00 0x01"));
}

TEST_F(GscUtilsTest, SetWpsr_Failed) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutputAndError(_, _)).WillOnce(Return(false));
  auto gsc_utils = std::make_unique<GscUtilsImpl>(std::move(mock_cmd_utils));

  EXPECT_FALSE(gsc_utils->SetWpsr("0xA2 0x01 0x00 0x4A 0x00 0x01"));
}

TEST_F(GscUtilsTest, IsApWpsrProvisioned_Success) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutputAndError(_, _))
      .WillOnce(
          DoAll(SetArgPointee<1>("expected values: 1: 99 & aa, 2: 00 & bb"),
                Return(true)));
  auto gsc_utils = std::make_unique<GscUtilsImpl>(std::move(mock_cmd_utils));

  auto result = gsc_utils->IsApWpsrProvisioned();
  EXPECT_TRUE(result.has_value());
  EXPECT_TRUE(result.value());
}

TEST_F(GscUtilsTest, IsApWpsrProvisioned_Unprovisioned) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutputAndError(_, _))
      .WillOnce(DoAll(SetArgPointee<1>("not provisioned"), Return(true)))
      .WillOnce(DoAll(SetArgPointee<1>("not provisioned\n"), Return(true)));

  auto gsc_utils = std::make_unique<GscUtilsImpl>(std::move(mock_cmd_utils));

  auto result = gsc_utils->IsApWpsrProvisioned();
  EXPECT_TRUE(result.has_value());
  EXPECT_FALSE(result.value());

  result = gsc_utils->IsApWpsrProvisioned();
  EXPECT_TRUE(result.has_value());
  EXPECT_FALSE(result.value());
}

TEST_F(GscUtilsTest, IsApWpsrProvisioned_Failed) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutputAndError(_, _)).WillOnce(Return(false));

  auto gsc_utils = std::make_unique<GscUtilsImpl>(std::move(mock_cmd_utils));

  auto result = gsc_utils->IsApWpsrProvisioned();
  EXPECT_FALSE(result.has_value());
}
}  // namespace rmad
