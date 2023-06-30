// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bootsplash/utils.h"

#include <string>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/strings/stringprintf.h>
#include <gtest/gtest.h>

#include "bootsplash/paths.h"

using base::FilePath;

namespace {

const int kBootSplashFrameMaxNumber = 5;

class UtilsTest : public ::testing::Test {
 protected:
  const FilePath& test_dir() const { return scoped_temp_dir_.GetPath(); }
  const FilePath& frecon_hi_res_path() const { return frecon_hi_res_path_; }

 private:
  void SetUp() override {
    ASSERT_TRUE(scoped_temp_dir_.CreateUniqueTempDir());
    paths::SetPrefixForTesting(test_dir());

    frecon_hi_res_path_ = paths::Get(paths::kFreconHiRes);
    ASSERT_TRUE(base::CreateDirectory(frecon_hi_res_path_.DirName()));

    boot_splash_frames_path_ = paths::GetBootSplashAssetsDir(false);
    ASSERT_TRUE(base::CreateDirectory(boot_splash_frames_path_));
    AddBootSplashFrameFiles();
  }

  base::ScopedTempDir scoped_temp_dir_;
  base::FilePath frecon_hi_res_path_;
};

TEST_F(UtilsTest, IsHiResDisplayFalse) {
  ASSERT_TRUE(base::WriteFile(frecon_hi_res_path(), "0"));
  ASSERT_FALSE(utils::IsHiResDisplay());
}

TEST_F(UtilsTest, IsHiResDisplayTrue) {
  ASSERT_TRUE(base::WriteFile(frecon_hi_res_path(), "1"));
  ASSERT_TRUE(utils::IsHiResDisplay());
}

}  // namespace
