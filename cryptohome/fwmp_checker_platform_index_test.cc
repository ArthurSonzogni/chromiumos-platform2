// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/fwmp_checker_platform_index.h"

#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <tpm_manager/client/mock_tpm_manager_utility.h>

namespace cryptohome {

namespace {

constexpr uint32_t kFakeIndex = 0x123;
constexpr auto kValidAttributesForWrite = {
    tpm_manager::NVRAM_PLATFORM_READ, tpm_manager::NVRAM_READ_AUTHORIZATION,
    tpm_manager::NVRAM_PLATFORM_CREATE, tpm_manager::NVRAM_OWNER_WRITE};

std::vector<tpm_manager::NvramSpaceAttribute> RemoveAttribute(
    std::vector<tpm_manager::NvramSpaceAttribute> attributes,
    int remove_index) {
  attributes.erase(attributes.begin() + remove_index);
  return attributes;
}

}  // namespace

using ::testing::_;
using ::testing::DoAll;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::StrictMock;

class FwmpCheckerPlatformIndexTest : public ::testing::Test {
 public:
  FwmpCheckerPlatformIndexTest() = default;
  ~FwmpCheckerPlatformIndexTest() = default;

 protected:
  StrictMock<tpm_manager::MockTpmManagerUtility> mock_tpm_manager_utility_;
};

TEST_F(FwmpCheckerPlatformIndexTest, InitializeTpmManagerUtilityFail) {
  EXPECT_CALL(mock_tpm_manager_utility_, Initialize()).WillOnce(Return(false));
  FwmpCheckerPlatformIndex fwmp_checker(&mock_tpm_manager_utility_);
  EXPECT_FALSE(fwmp_checker.IsValidForWrite(kFakeIndex));
}

TEST_F(FwmpCheckerPlatformIndexTest, IsValidForWriteSuccess) {
  EXPECT_CALL(mock_tpm_manager_utility_, Initialize())
      .WillRepeatedly(Return(true));
  FwmpCheckerPlatformIndex fwmp_checker(&mock_tpm_manager_utility_);
  EXPECT_CALL(mock_tpm_manager_utility_, GetSpaceInfo(kFakeIndex, _, _, _, _))
      .WillOnce(
          DoAll(SetArgPointee<4>(kValidAttributesForWrite), Return(true)));
  EXPECT_TRUE(fwmp_checker.IsValidForWrite(kFakeIndex));

  for (int i = 0; i < kValidAttributesForWrite.size(); ++i) {
    EXPECT_CALL(mock_tpm_manager_utility_, GetSpaceInfo(kFakeIndex, _, _, _, _))
        .WillOnce(DoAll(
            SetArgPointee<4>(RemoveAttribute(kValidAttributesForWrite, i)),
            Return(true)));
    EXPECT_FALSE(fwmp_checker.IsValidForWrite(kFakeIndex));
  }
}

TEST_F(FwmpCheckerPlatformIndexTest, IsValidForWriteAnyMissingAttribute) {
  EXPECT_CALL(mock_tpm_manager_utility_, Initialize())
      .WillRepeatedly(Return(true));
  FwmpCheckerPlatformIndex fwmp_checker(&mock_tpm_manager_utility_);
  for (int i = 0; i < kValidAttributesForWrite.size(); ++i) {
    EXPECT_CALL(mock_tpm_manager_utility_, GetSpaceInfo(kFakeIndex, _, _, _, _))
        .WillOnce(DoAll(
            SetArgPointee<4>(RemoveAttribute(kValidAttributesForWrite, i)),
            Return(true)));
    EXPECT_FALSE(fwmp_checker.IsValidForWrite(kFakeIndex));
  }
}

TEST_F(FwmpCheckerPlatformIndexTest, IsValidForWriteHasWriteAuthorization) {
  EXPECT_CALL(mock_tpm_manager_utility_, Initialize())
      .WillRepeatedly(Return(true));
  FwmpCheckerPlatformIndex fwmp_checker(&mock_tpm_manager_utility_);
  std::vector<tpm_manager::NvramSpaceAttribute> attributes{
      kValidAttributesForWrite};
  attributes.push_back(tpm_manager::NVRAM_WRITE_AUTHORIZATION);
  EXPECT_CALL(mock_tpm_manager_utility_, GetSpaceInfo(kFakeIndex, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<4>(attributes), Return(true)));
  EXPECT_FALSE(fwmp_checker.IsValidForWrite(kFakeIndex));
}

}  // namespace cryptohome
