// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/utils/cbi_utils_impl.h"
#include "rmad/utils/fake_cbi_utils.h"

#include <memory>
#include <string>
#include <utility>

#include <base/files/file_path.h>
#include <base/files/scoped_temp_dir.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "rmad/constants.h"
#include "rmad/utils/mock_cmd_utils.h"

using testing::_;
using testing::DoAll;
using testing::Return;
using testing::SetArgPointee;
using testing::StrictMock;

namespace {

constexpr char kGetIntSuccessOutput[] = R"(
As uint: 1234 (0x4d2)
As binary: d2 04
)";
constexpr char kGetStrSuccessOutput[] = "part_num";
constexpr char kRandomOutput[] = "*[)^";

}  // namespace

namespace rmad {

class CbiUtilsTest : public testing::Test {
 public:
  CbiUtilsTest() = default;
  ~CbiUtilsTest() override = default;
};

TEST_F(CbiUtilsTest, GetSku_Success) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(kGetIntSuccessOutput), Return(true)));
  auto cbi_utils = std::make_unique<CbiUtilsImpl>(std::move(mock_cmd_utils));

  uint64_t sku;
  EXPECT_TRUE(cbi_utils->GetSku(&sku));
  EXPECT_EQ(sku, 1234);
}

TEST_F(CbiUtilsTest, GetSku_Success_ParseFail) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(kRandomOutput), Return(true)));
  auto cbi_utils = std::make_unique<CbiUtilsImpl>(std::move(mock_cmd_utils));

  uint64_t sku;
  EXPECT_FALSE(cbi_utils->GetSku(&sku));
}

TEST_F(CbiUtilsTest, GetSku_Fail) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _)).WillOnce(Return(false));
  auto cbi_utils = std::make_unique<CbiUtilsImpl>(std::move(mock_cmd_utils));

  uint64_t sku;
  EXPECT_FALSE(cbi_utils->GetSku(&sku));
}

TEST_F(CbiUtilsTest, GetSku_Nullptr) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  auto cbi_utils = std::make_unique<CbiUtilsImpl>(std::move(mock_cmd_utils));

  EXPECT_DEATH(cbi_utils->GetSku(nullptr), "");
}

TEST_F(CbiUtilsTest, GetDramPartNum_Success) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(kGetStrSuccessOutput), Return(true)));
  auto cbi_utils = std::make_unique<CbiUtilsImpl>(std::move(mock_cmd_utils));

  std::string part_num;
  EXPECT_TRUE(cbi_utils->GetDramPartNum(&part_num));
  EXPECT_EQ(part_num, "part_num");
}

TEST_F(CbiUtilsTest, GetDramPartNum_Fail) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _)).WillOnce(Return(false));
  auto cbi_utils = std::make_unique<CbiUtilsImpl>(std::move(mock_cmd_utils));

  std::string part_num;
  EXPECT_FALSE(cbi_utils->GetDramPartNum(&part_num));
}

TEST_F(CbiUtilsTest, GetDramPartNum_Nullptr) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  auto cbi_utils = std::make_unique<CbiUtilsImpl>(std::move(mock_cmd_utils));

  EXPECT_DEATH(cbi_utils->GetDramPartNum(nullptr), "");
}

TEST_F(CbiUtilsTest, SetSku_Success) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _)).WillOnce(Return(true));
  auto cbi_utils = std::make_unique<CbiUtilsImpl>(std::move(mock_cmd_utils));

  EXPECT_TRUE(cbi_utils->SetSku(1));
}

TEST_F(CbiUtilsTest, SetSku_Fail) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _)).WillOnce(Return(false));
  auto cbi_utils = std::make_unique<CbiUtilsImpl>(std::move(mock_cmd_utils));

  EXPECT_FALSE(cbi_utils->SetSku(123));
}

TEST_F(CbiUtilsTest, SetDramPartNum_Success) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _)).WillOnce(Return(true));
  auto cbi_utils = std::make_unique<CbiUtilsImpl>(std::move(mock_cmd_utils));

  EXPECT_TRUE(cbi_utils->SetDramPartNum("part_num"));
}

TEST_F(CbiUtilsTest, SetDramPartNum_Fail) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _)).WillOnce(Return(false));
  auto cbi_utils = std::make_unique<CbiUtilsImpl>(std::move(mock_cmd_utils));

  EXPECT_FALSE(cbi_utils->SetDramPartNum("part_num"));
}

namespace fake {

class FakeCbiUtilsTest : public testing::Test {
 public:
  FakeCbiUtilsTest() = default;
  ~FakeCbiUtilsTest() override = default;

 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    fake_cbi_utils_ = std::make_unique<FakeCbiUtils>(temp_dir_.GetPath());
  }

  base::ScopedTempDir temp_dir_;
  std::unique_ptr<FakeCbiUtils> fake_cbi_utils_;
};

TEST_F(FakeCbiUtilsTest, SetSku_Success_GetSku_Success) {
  EXPECT_TRUE(fake_cbi_utils_->SetSku(1));
  uint64_t sku;
  EXPECT_TRUE(fake_cbi_utils_->GetSku(&sku));
  EXPECT_EQ(sku, 1);
}

TEST_F(FakeCbiUtilsTest, GetSku_Fail) {
  uint64_t sku;
  EXPECT_FALSE(fake_cbi_utils_->GetSku(&sku));
}

TEST_F(FakeCbiUtilsTest, GetSku_Nullptr) {
  EXPECT_DEATH(fake_cbi_utils_->GetSku(nullptr), "");
}

TEST_F(FakeCbiUtilsTest, SetDramPartNum_Success_GetDramPartNum_Success) {
  EXPECT_TRUE(fake_cbi_utils_->SetDramPartNum("fake_dram_part_num"));
  std::string dram_part_num;
  EXPECT_TRUE(fake_cbi_utils_->GetDramPartNum(&dram_part_num));
  EXPECT_EQ(dram_part_num, "fake_dram_part_num");
}

TEST_F(FakeCbiUtilsTest, GetDramPartNum_Fail) {
  std::string dram_part_num;
  EXPECT_FALSE(fake_cbi_utils_->GetDramPartNum(&dram_part_num));
}

TEST_F(FakeCbiUtilsTest, GetDramPartNum_Nullptr) {
  EXPECT_DEATH(fake_cbi_utils_->GetDramPartNum(nullptr), "");
}

}  // namespace fake

}  // namespace rmad
