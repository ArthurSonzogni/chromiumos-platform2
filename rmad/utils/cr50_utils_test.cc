// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/utils/cr50_utils_impl.h"
#include "rmad/utils/fake_cr50_utils.h"

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

constexpr char kChallengeCodeResponse[] = R"(
Challenge:
 AAAAA BBBBB
 CCCCC DDDDD
)";
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

}  // namespace

namespace rmad {

class Cr50UtilsTest : public testing::Test {
 public:
  Cr50UtilsTest() = default;
  ~Cr50UtilsTest() override = default;
};

TEST_F(Cr50UtilsTest, GetRsuChallengeCode_Success) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(kChallengeCodeResponse), Return(true)));
  auto cr50_utils = std::make_unique<Cr50UtilsImpl>(std::move(mock_cmd_utils));

  std::string challenge_code;
  EXPECT_TRUE(cr50_utils->GetRsuChallengeCode(&challenge_code));
  EXPECT_EQ(challenge_code, "AAAAABBBBBCCCCCDDDDD");
}

TEST_F(Cr50UtilsTest, GetRsuChallengeCode_Fail) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _)).WillOnce(Return(false));
  auto cr50_utils = std::make_unique<Cr50UtilsImpl>(std::move(mock_cmd_utils));

  std::string challenge_code;
  EXPECT_FALSE(cr50_utils->GetRsuChallengeCode(&challenge_code));
}

TEST_F(Cr50UtilsTest, PerformRsu_Success) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _)).WillOnce(Return(true));
  auto cr50_utils = std::make_unique<Cr50UtilsImpl>(std::move(mock_cmd_utils));

  EXPECT_TRUE(cr50_utils->PerformRsu(""));
}

TEST_F(Cr50UtilsTest, PerformRsu_Fail) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _)).WillOnce(Return(false));
  auto cr50_utils = std::make_unique<Cr50UtilsImpl>(std::move(mock_cmd_utils));

  EXPECT_FALSE(cr50_utils->PerformRsu(""));
}

TEST_F(Cr50UtilsTest, IsFactoryModeEnabled_Enabled) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(kFactoryModeEnabledResponse), Return(true)));
  auto cr50_utils = std::make_unique<Cr50UtilsImpl>(std::move(mock_cmd_utils));

  EXPECT_TRUE(cr50_utils->IsFactoryModeEnabled());
}

TEST_F(Cr50UtilsTest, IsFactoryModeEnabled_Disabled) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(kFactoryModeDisabledResponse), Return(true)));
  auto cr50_utils = std::make_unique<Cr50UtilsImpl>(std::move(mock_cmd_utils));

  EXPECT_FALSE(cr50_utils->IsFactoryModeEnabled());
}

TEST_F(Cr50UtilsTest, IsFactoryModeEnabled_NoResponse) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _)).WillOnce(Return(false));
  auto cr50_utils = std::make_unique<Cr50UtilsImpl>(std::move(mock_cmd_utils));

  EXPECT_FALSE(cr50_utils->IsFactoryModeEnabled());
}

TEST_F(Cr50UtilsTest, EnableFactoryMode_Success) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  {
    InSequence seq;
    EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _))
        .WillOnce(DoAll(SetArgPointee<1>(kFactoryModeDisabledResponse),
                        Return(true)));
    EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _)).WillOnce(Return(true));
  }
  auto cr50_utils = std::make_unique<Cr50UtilsImpl>(std::move(mock_cmd_utils));

  EXPECT_TRUE(cr50_utils->EnableFactoryMode());
}

TEST_F(Cr50UtilsTest, EnableFactoryMode_Fail) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  {
    InSequence seq;
    EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _))
        .WillOnce(DoAll(SetArgPointee<1>(kFactoryModeDisabledResponse),
                        Return(true)));
    EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _)).WillOnce(Return(false));
  }
  auto cr50_utils = std::make_unique<Cr50UtilsImpl>(std::move(mock_cmd_utils));

  EXPECT_FALSE(cr50_utils->EnableFactoryMode());
}

TEST_F(Cr50UtilsTest, EnableFactoryMode_AlreadyEnabled) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(kFactoryModeEnabledResponse), Return(true)));
  auto cr50_utils = std::make_unique<Cr50UtilsImpl>(std::move(mock_cmd_utils));

  EXPECT_TRUE(cr50_utils->EnableFactoryMode());
}

namespace fake {

class FakeCr50UtilsTest : public testing::Test {
 public:
  FakeCr50UtilsTest() = default;
  ~FakeCr50UtilsTest() override = default;

  base::FilePath GetHwwpDisabledFilePath() const {
    return temp_dir_.GetPath().AppendASCII(kHwwpDisabledFilePath);
  }

  base::FilePath GetFactoryModeEnabledFilePath() const {
    return temp_dir_.GetPath().AppendASCII(kFactoryModeEnabledFilePath);
  }

  base::FilePath GetBlockCcdFilePath() const {
    return temp_dir_.GetPath().AppendASCII(kBlockCcdFilePath);
  }

  base::FilePath GetRebootRequestFilePath() const {
    return temp_dir_.GetPath().AppendASCII(kRebootRequestFilePath);
  }

 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    fake_cr50_utils_ = std::make_unique<FakeCr50Utils>(temp_dir_.GetPath());
  }

  base::ScopedTempDir temp_dir_;
  std::unique_ptr<FakeCr50Utils> fake_cr50_utils_;
};

TEST_F(FakeCr50UtilsTest, GetRsuChallengCode) {
  std::string challenge_code;
  ASSERT_TRUE(fake_cr50_utils_->GetRsuChallengeCode(&challenge_code));
  ASSERT_EQ(challenge_code, "ABCDEFG");
}

TEST_F(FakeCr50UtilsTest, PerformRsu_Success) {
  ASSERT_FALSE(base::PathExists(GetFactoryModeEnabledFilePath()));
  ASSERT_TRUE(fake_cr50_utils_->PerformRsu("AAAAAAAA"));
  ASSERT_TRUE(base::PathExists(GetFactoryModeEnabledFilePath()));
}

TEST_F(FakeCr50UtilsTest, PerformRsu_AlreadyEnabled) {
  brillo::TouchFile(GetFactoryModeEnabledFilePath());
  ASSERT_TRUE(base::PathExists(GetFactoryModeEnabledFilePath()));
  ASSERT_TRUE(fake_cr50_utils_->PerformRsu("AAAAAAAA"));
  ASSERT_TRUE(base::PathExists(GetFactoryModeEnabledFilePath()));
}

TEST_F(FakeCr50UtilsTest, PerformRsu_Fail) {
  ASSERT_FALSE(base::PathExists(GetFactoryModeEnabledFilePath()));
  ASSERT_FALSE(fake_cr50_utils_->PerformRsu("AAAAAAAB"));
  ASSERT_FALSE(base::PathExists(GetFactoryModeEnabledFilePath()));
}

TEST_F(FakeCr50UtilsTest, IsFactoryModeEnabled_Enabled) {
  brillo::TouchFile(GetFactoryModeEnabledFilePath());
  ASSERT_TRUE(fake_cr50_utils_->IsFactoryModeEnabled());
}

TEST_F(FakeCr50UtilsTest, IsFactoryModeEnabled_Disabled) {
  ASSERT_FALSE(fake_cr50_utils_->IsFactoryModeEnabled());
}

TEST_F(FakeCr50UtilsTest, EnableFactoryMode_Success) {
  brillo::TouchFile(GetHwwpDisabledFilePath());
  ASSERT_FALSE(base::PathExists(GetFactoryModeEnabledFilePath()));
  ASSERT_FALSE(base::PathExists(GetRebootRequestFilePath()));
  ASSERT_TRUE(fake_cr50_utils_->EnableFactoryMode());
  ASSERT_TRUE(base::PathExists(GetFactoryModeEnabledFilePath()));
  ASSERT_TRUE(base::PathExists(GetRebootRequestFilePath()));
}

TEST_F(FakeCr50UtilsTest, EnableFactoryMode_AlreadyEnabled) {
  brillo::TouchFile(GetFactoryModeEnabledFilePath());
  ASSERT_TRUE(base::PathExists(GetFactoryModeEnabledFilePath()));
  ASSERT_TRUE(fake_cr50_utils_->EnableFactoryMode());
  ASSERT_TRUE(base::PathExists(GetFactoryModeEnabledFilePath()));
}

TEST_F(FakeCr50UtilsTest, EnableFactoryMode_HwwpDisabled) {
  ASSERT_FALSE(fake_cr50_utils_->EnableFactoryMode());
  ASSERT_FALSE(base::PathExists(GetFactoryModeEnabledFilePath()));
}

TEST_F(FakeCr50UtilsTest, EnableFactoryMode_CcdBlocked) {
  brillo::TouchFile(GetHwwpDisabledFilePath());
  brillo::TouchFile(GetBlockCcdFilePath());
  ASSERT_FALSE(fake_cr50_utils_->EnableFactoryMode());
  ASSERT_FALSE(base::PathExists(GetFactoryModeEnabledFilePath()));
}

}  // namespace fake

}  // namespace rmad
