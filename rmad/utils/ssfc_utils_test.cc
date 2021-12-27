// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/utils/fake_ssfc_utils.h"
#include "rmad/utils/ssfc_utils_impl.h"

#include <memory>
#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>

#include "rmad/utils/mock_cmd_utils.h"

using testing::_;
using testing::DoAll;
using testing::NiceMock;
using testing::Return;
using testing::SetArgPointee;

namespace {

constexpr char kTestModelName[] = "TestModelName";
constexpr char kSsfcScriptPathPostfix[] = "_ssfc.sh";
constexpr char kTestSsfcOutput[] = "0x1234";
constexpr uint32_t kTestSsfc = 0x1234;

}  // namespace

namespace rmad {

class SsfcUtilsImplTest : public testing::Test {
 public:
  SsfcUtilsImplTest() {}

  std::unique_ptr<SsfcUtilsImpl> CreateSsfcUtils(
      bool mock_return_value, const std::string& mock_cmd_output) {
    auto cmd_utils = std::make_unique<NiceMock<MockCmdUtils>>();
    std::vector<std::string> argv = {file_path_.MaybeAsASCII()};
    if (mock_return_value) {
      ON_CALL(*cmd_utils, GetOutput(argv, _))
          .WillByDefault(
              DoAll(testing::SetArgPointee<1>(mock_cmd_output), Return(true)));
    } else {
      ON_CALL(*cmd_utils, GetOutput(argv, _)).WillByDefault(Return(false));
    }

    return std::make_unique<SsfcUtilsImpl>(std::move(cmd_utils),
                                           temp_dir_.GetPath().MaybeAsASCII());
  }

  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    file_path_ = temp_dir_.GetPath().AppendASCII(std::string(kTestModelName) +
                                                 kSsfcScriptPathPostfix);
    base::WriteFile(file_path_, "", 0);
    ASSERT_TRUE(base::PathExists(file_path_));
  }

  base::ScopedTempDir temp_dir_;
  base::FilePath file_path_;
};

TEST_F(SsfcUtilsImplTest, GetSSFC_Success) {
  auto ssfc_utils = CreateSsfcUtils(true, kTestSsfcOutput);

  bool need_to_update = false;
  uint32_t ssfc = 0;
  EXPECT_TRUE(ssfc_utils->GetSSFC(kTestModelName, &need_to_update, &ssfc));
  EXPECT_TRUE(need_to_update);
  EXPECT_EQ(ssfc, kTestSsfc);
}

TEST_F(SsfcUtilsImplTest, GetSSFC_NoScriptFileSuccess) {
  auto ssfc_utils = CreateSsfcUtils(true, kTestSsfcOutput);
  base::DeleteFile(file_path_);

  bool need_to_update = false;
  uint32_t ssfc = 0;
  EXPECT_TRUE(ssfc_utils->GetSSFC(kTestModelName, &need_to_update, &ssfc));
  EXPECT_FALSE(need_to_update);
  EXPECT_EQ(ssfc, 0);
}

TEST_F(SsfcUtilsImplTest, GetSSFC_FailedToExecScriptFailed) {
  auto ssfc_utils = CreateSsfcUtils(false, kTestSsfcOutput);

  bool need_to_update = false;
  uint32_t ssfc = 0;
  EXPECT_FALSE(ssfc_utils->GetSSFC(kTestModelName, &need_to_update, &ssfc));
  EXPECT_FALSE(need_to_update);
  EXPECT_EQ(ssfc, 0);
}

TEST_F(SsfcUtilsImplTest, GetSSFC_InvalidScriptOutputFailed) {
  auto ssfc_utils = CreateSsfcUtils(true, "InvalidString");

  bool need_to_update = false;
  uint32_t ssfc = 0;
  EXPECT_FALSE(ssfc_utils->GetSSFC(kTestModelName, &need_to_update, &ssfc));
  EXPECT_FALSE(need_to_update);
  EXPECT_EQ(ssfc, 0);
}

namespace fake {

class FakeSsfcUtilsImplTest : public testing::Test {
 public:
  FakeSsfcUtilsImplTest() = default;
  ~FakeSsfcUtilsImplTest() override = default;
};

TEST_F(FakeSsfcUtilsImplTest, GetSSFC_Failed) {
  auto fake_ssfc_utils = std::make_unique<FakeSsfcUtils>();

  bool need_to_update = false;
  uint32_t ssfc = 0;
  EXPECT_TRUE(fake_ssfc_utils->GetSSFC(kTestModelName, &need_to_update, &ssfc));
  EXPECT_FALSE(need_to_update);
  EXPECT_EQ(ssfc, 0);
}

}  // namespace fake

}  // namespace rmad
