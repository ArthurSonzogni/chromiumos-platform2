// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/utils/cros_config_properties.h"

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <gtest/gtest.h>

namespace rmad {

class CrosConfigPropertiesTest : public testing::Test {
 public:
  CrosConfigPropertiesTest() {}
  base::FilePath GetRootPath() const { return temp_dir_.GetPath(); }

 protected:
  void SetUp() override { ASSERT_TRUE(temp_dir_.CreateUniqueTempDir()); }

  base::ScopedTempDir temp_dir_;
};

TEST_F(CrosConfigPropertiesTest, HasTouchscreen) {
  const base::FilePath property_dir_path =
      GetRootPath().Append(kCrosHardwarePropertiesPath);
  const base::FilePath property_file_path =
      property_dir_path.Append(kCrosHardwarePropertiesHasTouchscreenKey);
  EXPECT_TRUE(base::CreateDirectory(property_dir_path));
  EXPECT_TRUE(base::WriteFile(property_file_path, "true"));

  EXPECT_EQ("Touchscreen:Yes", GetHasTouchscreenDescription(GetRootPath()));
}

TEST_F(CrosConfigPropertiesTest, NoTouchscreen) {
  const base::FilePath property_dir_path =
      GetRootPath().Append(kCrosHardwarePropertiesPath);
  const base::FilePath property_file_path =
      property_dir_path.Append(kCrosHardwarePropertiesHasTouchscreenKey);
  EXPECT_TRUE(base::CreateDirectory(property_dir_path));
  EXPECT_TRUE(base::WriteFile(property_file_path, "false"));

  EXPECT_EQ("Touchscreen:No", GetHasTouchscreenDescription(GetRootPath()));
}

TEST_F(CrosConfigPropertiesTest, UnknownTouchscreen) {
  EXPECT_EQ("Touchscreen:No", GetHasTouchscreenDescription(GetRootPath()));
}

}  // namespace rmad
