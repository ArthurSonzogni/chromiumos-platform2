// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/utils/flashrom_utils_impl.h"
#include "rmad/utils/fake_flashrom_utils.h"

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

TEST_F(FlashromUtilsTest, EnableSoftwareWriteProtection_Success) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  {
    InSequence seq;
    // Flashrom read.
    EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _)).WillOnce(Return(true));
    // Parse fmap.
    EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _))
        .WillOnce(DoAll(SetArgPointee<1>(kFmapOutput), Return(true)));
    // Flashrom set WP range.
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

TEST_F(FlashromUtilsTest, EnableSoftwareWriteProtection_EnableFail) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  {
    InSequence seq;
    // Flashrom read.
    EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _)).WillOnce(Return(true));
    // Parse fmap.
    EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _))
        .WillOnce(DoAll(SetArgPointee<1>(kFmapOutput), Return(true)));
    // Flashrom set WP range.
    EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _)).WillOnce(Return(false));
  }
  auto flashrom_utils =
      std::make_unique<FlashromUtilsImpl>(std::move(mock_cmd_utils));

  EXPECT_FALSE(flashrom_utils->EnableSoftwareWriteProtection());
}

namespace fake {

class FakeFlashromUtilsTest : public testing::Test {
 public:
  FakeFlashromUtilsTest() = default;
  ~FakeFlashromUtilsTest() override = default;

 protected:
  void SetUp() override {
    fake_flashrom_utils_ = std::make_unique<FakeFlashromUtils>();
  }

  std::unique_ptr<FakeFlashromUtils> fake_flashrom_utils_;
};

TEST_F(FakeFlashromUtilsTest, EnableSoftwareWriteProtection_Success) {
  EXPECT_TRUE(fake_flashrom_utils_->EnableSoftwareWriteProtection());
}

TEST_F(FakeFlashromUtilsTest, DisableSoftwareWriteProtection_Success) {
  EXPECT_TRUE(fake_flashrom_utils_->DisableSoftwareWriteProtection());
}

}  // namespace fake

}  // namespace rmad
