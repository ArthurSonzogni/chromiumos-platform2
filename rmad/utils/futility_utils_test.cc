// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/utils/futility_utils_impl.h"

#include <cstdint>
#include <memory>
#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
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

 protected:
  void SetUp() override { ASSERT_TRUE(temp_dir_.CreateUniqueTempDir()); }

  base::ScopedTempDir temp_dir_;
};

TEST_F(FutilityUtilsTest, GetApWriteProtectionStatus_Enabled) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(kFutilityWriteProtectEnabledOutput),
                      Return(true)));
  auto futility_utils = std::make_unique<FutilityUtilsImpl>(
      std::move(mock_cmd_utils), std::make_unique<HwidUtilsImpl>(),
      base::FilePath());

  auto enabled = futility_utils->GetApWriteProtectionStatus();
  EXPECT_TRUE(enabled.has_value());
  EXPECT_TRUE(enabled.value());
}

TEST_F(FutilityUtilsTest, GetApWriteProtectionStatus_Disabled) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(kFutilityWriteProtectDisabledOutput),
                      Return(true)));
  auto futility_utils = std::make_unique<FutilityUtilsImpl>(
      std::move(mock_cmd_utils), std::make_unique<HwidUtilsImpl>(),
      base::FilePath());

  auto enabled = futility_utils->GetApWriteProtectionStatus();
  EXPECT_TRUE(enabled.has_value());
  EXPECT_FALSE(enabled.value());
}

TEST_F(FutilityUtilsTest, GetApWriteProtectionStatus_Misconfigured) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(kFutilityWriteProtectMisconfiguredOutput),
                Return(true)));
  auto futility_utils = std::make_unique<FutilityUtilsImpl>(
      std::move(mock_cmd_utils), std::make_unique<HwidUtilsImpl>(),
      base::FilePath());

  auto enabled = futility_utils->GetApWriteProtectionStatus();
  EXPECT_TRUE(enabled.has_value());
  EXPECT_TRUE(enabled.value());
}

TEST_F(FutilityUtilsTest, GetApWriteProtectionStatus_Failed) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _)).WillOnce(Return(false));
  auto futility_utils = std::make_unique<FutilityUtilsImpl>(
      std::move(mock_cmd_utils), std::make_unique<HwidUtilsImpl>(),
      base::FilePath());

  auto enabled = futility_utils->GetApWriteProtectionStatus();
  EXPECT_FALSE(enabled.has_value());
}

TEST_F(FutilityUtilsTest, EnableApSoftwareWriteProtection_Success) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  {
    InSequence seq;
    // Futility set AP WP range.
    EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _)).WillOnce(Return(true));
  }
  auto futility_utils = std::make_unique<FutilityUtilsImpl>(
      std::move(mock_cmd_utils), std::make_unique<HwidUtilsImpl>(),
      base::FilePath());

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
      std::move(mock_cmd_utils), std::make_unique<HwidUtilsImpl>(),
      base::FilePath());

  EXPECT_FALSE(futility_utils->EnableApSoftwareWriteProtection());
}

TEST_F(FutilityUtilsTest, SetHwidSuccess) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutputAndError(_, _)).WillOnce(Return(true));
  auto futility_utils = std::make_unique<FutilityUtilsImpl>(
      std::move(mock_cmd_utils), std::make_unique<HwidUtilsImpl>(),
      base::FilePath());

  EXPECT_TRUE(futility_utils->SetHwid("MODEL-CODE A1B-C2D-E2J"));
}

TEST_F(FutilityUtilsTest, SetHwidInvalidHwidFormatFail) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutputAndError(_, _)).Times(0);
  auto futility_utils = std::make_unique<FutilityUtilsImpl>(
      std::move(mock_cmd_utils), std::make_unique<HwidUtilsImpl>(),
      base::FilePath());

  EXPECT_FALSE(futility_utils->SetHwid("MODEL-CODE A1BC2DE2J"));
}

TEST_F(FutilityUtilsTest, SetHwidIncorrectChecksumFail) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutputAndError(_, _)).Times(0);
  auto futility_utils = std::make_unique<FutilityUtilsImpl>(
      std::move(mock_cmd_utils), std::make_unique<HwidUtilsImpl>(),
      base::FilePath());

  EXPECT_FALSE(futility_utils->SetHwid("MODEL-CODE A1B-C2D-E2K"));
}

TEST_F(FutilityUtilsTest, SetHwidGetOutputFail) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutputAndError(_, _)).WillOnce(Return(false));
  auto futility_utils = std::make_unique<FutilityUtilsImpl>(
      std::move(mock_cmd_utils), std::make_unique<HwidUtilsImpl>(),
      base::FilePath());

  EXPECT_FALSE(futility_utils->SetHwid("MODEL-CODE A1B-C2D-E2J"));
}

TEST_F(FutilityUtilsTest, GetFlashSizeSuccess) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutputAndError(_, _))
      .WillOnce(
          DoAll(SetArgPointee<1>("Flash size: 0x00800000"), Return(true)));
  auto futility_utils = std::make_unique<FutilityUtilsImpl>(
      std::move(mock_cmd_utils), std::make_unique<HwidUtilsImpl>(),
      base::FilePath());

  auto size = futility_utils->GetFlashSize();
  const uint64_t expected = 0x800000;
  EXPECT_TRUE(size.has_value());
  EXPECT_EQ(expected, size.value());
}

TEST_F(FutilityUtilsTest, GetFlashSizeGetOutputAndErrorFail) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutputAndError(_, _)).WillOnce(Return(false));
  auto futility_utils = std::make_unique<FutilityUtilsImpl>(
      std::move(mock_cmd_utils), std::make_unique<HwidUtilsImpl>(),
      base::FilePath());

  auto size = futility_utils->GetFlashSize();
  EXPECT_FALSE(size.has_value());
}

TEST_F(FutilityUtilsTest, GetFlashSizeFailToParseFail) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutputAndError(_, _))
      .WillOnce(
          DoAll(SetArgPointee<1>("Flash size: 0xGGGGGGGG"), Return(true)));
  auto futility_utils = std::make_unique<FutilityUtilsImpl>(
      std::move(mock_cmd_utils), std::make_unique<HwidUtilsImpl>(),
      base::FilePath());

  auto size = futility_utils->GetFlashSize();
  EXPECT_FALSE(size.has_value());
}

TEST_F(FutilityUtilsTest, GetFlashInfoSuccess) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutputAndError(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(
                          "PR0: Warning: 0xFFFFFFFF-0xFFFFFFFF is read-only.\n"
                          "GPR0: Warning: 0xFFFFFFFF-0xFFFFFFFF is read-only.\n"
                          "At least some flash regions are write protected. "
                          "For write operations,\n"
                          "you should use a flash layout and include only "
                          "writable regions. See\n"
                          "manpage for more details.\n"
                          "Flash vendor: test vendor\n"
                          "Flash name: test flash name\n"
                          "Flash vid-pid: 0xFFFFFFFFFF\n"
                          "Flash size: 0xFFFFFFFF\n"
                          "Warning: Setting BIOS Control at 0xaa from 0xbb to "
                          "0xcc failed.\n"
                          "New value is 0xFF.\n"
                          "Expected WP SR configuration by FW image: (start = "
                          "0x11110000, length = 0xffff0000)"),
                      Return(true)));
  auto futility_utils = std::make_unique<FutilityUtilsImpl>(
      std::move(mock_cmd_utils), std::make_unique<HwidUtilsImpl>(),
      base::FilePath());

  auto flash_info = futility_utils->GetFlashInfo();
  const std::string expected_flash_name = "test flash name";
  const uint64_t expected_start = 0x11110000;
  const uint64_t expected_length = 0xffff0000;

  EXPECT_TRUE(flash_info.has_value());
  EXPECT_EQ(expected_flash_name, flash_info->flash_name);
  EXPECT_EQ(expected_start, flash_info->wpsr_start);
  EXPECT_EQ(expected_length, flash_info->wpsr_length);
}

TEST_F(FutilityUtilsTest, GetArmFlashInfoSuccess) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutputAndError(_, _))
      .WillOnce(DoAll(
          SetArgPointee<1>("Flash vendor: Programmer\n"
                           "Flash name: Opaque flash chip\n"
                           "Flash vid-pid: 0x000000000000\n"
                           "Flash size: 0x00000000\n"
                           "Expected WP SR configuration by FW image: (start = "
                           "0x00000000, length = 0xFFFFFFFF)"),
          Return(true)));
  auto futility_utils = std::make_unique<FutilityUtilsImpl>(
      std::move(mock_cmd_utils), std::make_unique<HwidUtilsImpl>(),
      temp_dir_.GetPath());

  auto partname_path = temp_dir_.GetPath().AppendASCII("partname");
  ASSERT_TRUE(base::WriteFile(partname_path, "test arm flash name \n"));

  auto flash_info = futility_utils->GetFlashInfo();
  const std::string expected_flash_name = "test arm flash name";
  const uint64_t expected_start = 0x00000000;
  const uint64_t expected_length = 0xFFFFFFFF;
  EXPECT_TRUE(flash_info.has_value());
  EXPECT_EQ(expected_flash_name, flash_info->flash_name);
  EXPECT_EQ(expected_start, flash_info->wpsr_start);
  EXPECT_EQ(expected_length, flash_info->wpsr_length);
}

TEST_F(FutilityUtilsTest, GetFlashInfoGetOutputAndErrorFail) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutputAndError(_, _)).WillOnce(Return(false));
  auto futility_utils = std::make_unique<FutilityUtilsImpl>(
      std::move(mock_cmd_utils), std::make_unique<HwidUtilsImpl>(),
      base::FilePath());

  auto flash_info = futility_utils->GetFlashInfo();
  EXPECT_FALSE(flash_info.has_value());
}

}  // namespace rmad
