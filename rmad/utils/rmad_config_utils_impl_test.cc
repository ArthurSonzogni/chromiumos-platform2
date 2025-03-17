// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/utils/rmad_config_utils_impl.h"

#include <memory>
#include <string>
#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "rmad/constants.h"
#include "rmad/utils/mock_cros_config_utils.h"

using testing::_;
using testing::DoAll;
using testing::InSequence;
using testing::Return;
using testing::SetArgPointee;
using testing::StrictMock;

namespace rmad {

class RmadConfigUtilsTest : public testing::Test {
 public:
  RmadConfigUtilsTest() = default;
  ~RmadConfigUtilsTest() override = default;

  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    config_dir_path_ = temp_dir_.GetPath();
  }

 protected:
  base::ScopedTempDir temp_dir_;
  base::FilePath config_dir_path_;
};

TEST_F(RmadConfigUtilsTest, Initialize_Success) {
  auto mock_cros_config_utils =
      std::make_unique<StrictMock<MockCrosConfigUtils>>();
  EXPECT_CALL(*mock_cros_config_utils, GetModelName(_))
      .WillOnce(DoAll(SetArgPointee<0>("model_name"), Return(true)));

  const base::FilePath textproto_file_path =
      config_dir_path_.Append("model_name")
          .Append(kDefaultRmadConfigProtoFilePath);
  std::string textproto = R"(
  )";
  ASSERT_TRUE(base::CreateDirectory(textproto_file_path.DirName()));
  ASSERT_TRUE(base::WriteFile(textproto_file_path, textproto));

  RmadConfigUtilsImpl rmad_config_utils(config_dir_path_,
                                        std::move(mock_cros_config_utils));

  auto rmad_config = rmad_config_utils.GetConfig();
  EXPECT_TRUE(rmad_config.has_value());
}

TEST_F(RmadConfigUtilsTest, Initialize_GetModelName_Fail) {
  auto mock_cros_config_utils =
      std::make_unique<StrictMock<MockCrosConfigUtils>>();
  EXPECT_CALL(*mock_cros_config_utils, GetModelName(_)).WillOnce(Return(false));

  RmadConfigUtilsImpl rmad_config_utils(config_dir_path_,
                                        std::move(mock_cros_config_utils));
  auto rmad_config = rmad_config_utils.GetConfig();
  EXPECT_FALSE(rmad_config.has_value());
}

TEST_F(RmadConfigUtilsTest, Initialize_NoFile_Fail) {
  auto mock_cros_config_utils =
      std::make_unique<StrictMock<MockCrosConfigUtils>>();
  EXPECT_CALL(*mock_cros_config_utils, GetModelName(_))
      .WillOnce(DoAll(SetArgPointee<0>("model_name"), Return(true)));

  RmadConfigUtilsImpl rmad_config_utils(config_dir_path_,
                                        std::move(mock_cros_config_utils));
  auto rmad_config = rmad_config_utils.GetConfig();
  EXPECT_FALSE(rmad_config.has_value());
}

TEST_F(RmadConfigUtilsTest, Initialize_ReadFile_Fail) {
  auto mock_cros_config_utils =
      std::make_unique<StrictMock<MockCrosConfigUtils>>();
  EXPECT_CALL(*mock_cros_config_utils, GetModelName(_))
      .WillOnce(DoAll(SetArgPointee<0>("model_name"), Return(true)));

  const base::FilePath textproto_file_path =
      config_dir_path_.Append("model_name")
          .Append(kDefaultRmadConfigProtoFilePath);
  ASSERT_TRUE(base::CreateDirectory(textproto_file_path.DirName()));
  // Create a directory instead of a file.
  ASSERT_TRUE(base::CreateDirectory(textproto_file_path));

  RmadConfigUtilsImpl rmad_config_utils(config_dir_path_,
                                        std::move(mock_cros_config_utils));
  auto rmad_config = rmad_config_utils.GetConfig();
  EXPECT_FALSE(rmad_config.has_value());
}

TEST_F(RmadConfigUtilsTest, Initialize_ParseFile_Fail) {
  auto mock_cros_config_utils =
      std::make_unique<StrictMock<MockCrosConfigUtils>>();
  EXPECT_CALL(*mock_cros_config_utils, GetModelName(_))
      .WillOnce(DoAll(SetArgPointee<0>("model_name"), Return(true)));

  const base::FilePath textproto_file_path =
      config_dir_path_.Append("model_name")
          .Append(kDefaultRmadConfigProtoFilePath);
  std::string textproto = "invalid textproto";
  ASSERT_TRUE(base::CreateDirectory(textproto_file_path.DirName()));
  ASSERT_TRUE(base::WriteFile(textproto_file_path, textproto));

  RmadConfigUtilsImpl rmad_config_utils(config_dir_path_,
                                        std::move(mock_cros_config_utils));
  auto rmad_config = rmad_config_utils.GetConfig();
  EXPECT_FALSE(rmad_config.has_value());
}

}  // namespace rmad
