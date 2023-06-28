// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/utils/gsc_utils_impl.h"

#include <memory>
#include <string>
#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <brillo/file_utils.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "rmad/constants.h"
#include "rmad/utils/mock_cmd_utils.h"

using testing::_;
using testing::DoAll;
using testing::InSequence;
using testing::Return;
using testing::SetArgPointee;
using testing::StrictMock;

namespace {

constexpr char kChallengeCodeResponse[] =
    "CHALLENGE="
    "AAAAABBBBBCCCCCDDDDDEEEEEFFFFFGGGGGHHHHH"
    "1111122222333334444455555666667777788888\n";
constexpr char kFactoryModeEnabledResponse[] = R"(
State: Locked
---
---
Capabilities are modified.
)";
constexpr char kFactoryModeDisabledResponse[] = R"(
State: Locked
---
---
Capabilities are default.
)";
constexpr char kGetBoardIdResponse[] = R"(
BID_TYPE=5a5a4352
BID_TYPE_INV=a5a5bcad
BID_FLAGS=00007f80
BID_RLZ=ZZCR
)";
constexpr char kExpectedBoardIdType[] = "5a5a4352";
constexpr char kExpectedBoardIdFlags[] = "00007f80";

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
      .WillOnce(DoAll(SetArgPointee<1>(kChallengeCodeResponse), Return(true)));
  auto gsc_utils = std::make_unique<GscUtilsImpl>(std::move(mock_cmd_utils));

  std::string challenge_code;
  EXPECT_TRUE(gsc_utils->GetRsuChallengeCode(&challenge_code));
  EXPECT_EQ(challenge_code,
            "AAAAABBBBBCCCCCDDDDDEEEEEFFFFFGGGGGHHHHH"
            "1111122222333334444455555666667777788888");
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

TEST_F(GscUtilsTest, GetBoardIdType_Success) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(kGetBoardIdResponse), Return(true)));
  auto gsc_utils = std::make_unique<GscUtilsImpl>(std::move(mock_cmd_utils));

  std::string board_id_type;
  EXPECT_TRUE(gsc_utils->GetBoardIdType(&board_id_type));
  EXPECT_EQ(board_id_type, kExpectedBoardIdType);
}

TEST_F(GscUtilsTest, GetBoardIdType_Fail) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _)).WillOnce(Return(false));
  auto gsc_utils = std::make_unique<GscUtilsImpl>(std::move(mock_cmd_utils));

  std::string board_id_type;
  EXPECT_FALSE(gsc_utils->GetBoardIdType(&board_id_type));
}

TEST_F(GscUtilsTest, GetBoardIdFlags_Success) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(kGetBoardIdResponse), Return(true)));
  auto gsc_utils = std::make_unique<GscUtilsImpl>(std::move(mock_cmd_utils));

  std::string board_id_flags;
  EXPECT_TRUE(gsc_utils->GetBoardIdFlags(&board_id_flags));
  EXPECT_EQ(board_id_flags, kExpectedBoardIdFlags);
}

TEST_F(GscUtilsTest, GetBoardIdFlags_Fail) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _)).WillOnce(Return(false));
  auto gsc_utils = std::make_unique<GscUtilsImpl>(std::move(mock_cmd_utils));

  std::string board_id_flags;
  EXPECT_FALSE(gsc_utils->GetBoardIdFlags(&board_id_flags));
}

TEST_F(GscUtilsTest, SetBoardId_Success) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _)).WillOnce(Return(true));
  auto gsc_utils = std::make_unique<GscUtilsImpl>(std::move(mock_cmd_utils));

  EXPECT_TRUE(gsc_utils->SetBoardId(true));
}

TEST_F(GscUtilsTest, SetBoardId_Fail) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _)).WillOnce(Return(false));
  auto gsc_utils = std::make_unique<GscUtilsImpl>(std::move(mock_cmd_utils));

  EXPECT_FALSE(gsc_utils->SetBoardId(true));
}

TEST_F(GscUtilsTest, Reboot_Success) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _)).WillOnce(Return(true));
  auto gsc_utils = std::make_unique<GscUtilsImpl>(std::move(mock_cmd_utils));

  EXPECT_TRUE(gsc_utils->Reboot());
}
}  // namespace rmad
