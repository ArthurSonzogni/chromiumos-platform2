// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/utils/fake_crossystem_utils.h"

#include <memory>
#include <string>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <brillo/file_utils.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "rmad/constants.h"

namespace rmad {
namespace fake {

class FakeCrosSystemUtilsTest : public testing::Test {
 public:
  FakeCrosSystemUtilsTest() = default;
  ~FakeCrosSystemUtilsTest() override = default;

 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    fake_crossystem_utils_ =
        std::make_unique<FakeCrosSystemUtils>(temp_dir_.GetPath());
  }

  base::ScopedTempDir temp_dir_;
  std::unique_ptr<FakeCrosSystemUtils> fake_crossystem_utils_;
};

TEST_F(FakeCrosSystemUtilsTest, SetInt_Success_GetInt) {
  ASSERT_TRUE(fake_crossystem_utils_->SetInt("key", 1));
  int value;
  ASSERT_TRUE(fake_crossystem_utils_->GetInt("key", &value));
  ASSERT_EQ(value, 1);
}

TEST_F(FakeCrosSystemUtilsTest, SetInt_ReadOnly) {
  ASSERT_FALSE(fake_crossystem_utils_->SetInt("wpsw_cur", 1));
}

TEST_F(FakeCrosSystemUtilsTest, GetInt_NotSet) {
  int value;
  ASSERT_FALSE(fake_crossystem_utils_->GetInt("key", &value));
}

TEST_F(FakeCrosSystemUtilsTest, GetInt_Wpsw_FactoryModeEnabled) {
  const base::FilePath factory_mode_enabled_file_path =
      temp_dir_.GetPath().AppendASCII(kFactoryModeEnabledFilePath);
  brillo::TouchFile(factory_mode_enabled_file_path);

  int value;
  ASSERT_TRUE(fake_crossystem_utils_->GetInt("wpsw_cur", &value));
  ASSERT_EQ(value, 0);
}

TEST_F(FakeCrosSystemUtilsTest, GetInt_Wpsw_HwwpDisabled) {
  const base::FilePath hwwp_disabled_file_path =
      temp_dir_.GetPath().AppendASCII(kHwwpDisabledFilePath);
  brillo::TouchFile(hwwp_disabled_file_path);

  int value;
  ASSERT_TRUE(fake_crossystem_utils_->GetInt("wpsw_cur", &value));
  ASSERT_EQ(value, 0);
}

TEST_F(FakeCrosSystemUtilsTest, GetInt_Wpsw_FactoryModeDisabled_HwwpEnaabled) {
  int value;
  ASSERT_TRUE(fake_crossystem_utils_->GetInt("wpsw_cur", &value));
  ASSERT_EQ(value, 1);
}

TEST_F(FakeCrosSystemUtilsTest, SetString_Success_GetString) {
  ASSERT_TRUE(fake_crossystem_utils_->SetString("key", "value"));
  std::string value;
  ASSERT_TRUE(fake_crossystem_utils_->GetString("key", &value));
  ASSERT_EQ(value, "value");
}

TEST_F(FakeCrosSystemUtilsTest, SetString_ReadOnly) {
  ASSERT_FALSE(fake_crossystem_utils_->SetString("wpsw_cur", "value"));
}

TEST_F(FakeCrosSystemUtilsTest, GetString_NotSet) {
  std::string value;
  ASSERT_FALSE(fake_crossystem_utils_->GetString("key", &value));
}

TEST_F(FakeCrosSystemUtilsTest, GetString_Wpsw) {
  std::string value;
  ASSERT_FALSE(fake_crossystem_utils_->GetString("wpsw_cur", &value));
}

}  // namespace fake
}  // namespace rmad
