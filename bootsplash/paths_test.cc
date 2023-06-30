// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bootsplash/paths.h"

#include <string>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <gtest/gtest.h>

using base::FilePath;

namespace {

constexpr char kBootSplashAssetsDirSimonDisabledLowRes[] =
    "/usr/share/chromeos-assets/images_100_percent/";
constexpr char kBootSplashAssetsDirSimonEnabledLowRes[] =
    "/usr/share/chromeos-assets/animated_splash_screen/splash_100_percent/";
constexpr char kBootSplashAssetsDirSimonDisabledHiRes[] =
    "/usr/share/chromeos-assets/images_200_percent/";
constexpr char kBootSplashAssetsDirSimonEnabledHiRes[] =
    "/usr/share/chromeos-assets/animated_splash_screen/splash_200_percent/";

class PathsTest : public ::testing::Test {
 protected:
  const FilePath& test_dir() const { return scoped_temp_dir_.GetPath(); }
  const FilePath& frecon_hi_res_path() const { return frecon_hi_res_path_; }

 private:
  void SetUp() override {
    ASSERT_TRUE(scoped_temp_dir_.CreateUniqueTempDir());
    paths::SetPrefixForTesting(test_dir());

    // Create hi_res file to that's initially "0", but can be overwritten.
    frecon_hi_res_path_ = paths::Get(paths::kFreconHiRes);
    ASSERT_TRUE(base::CreateDirectory(frecon_hi_res_path_.DirName()));
    ASSERT_TRUE(base::WriteFile(frecon_hi_res_path_, "0"));
  }

  base::ScopedTempDir scoped_temp_dir_;
  base::FilePath frecon_hi_res_path_;
};

TEST_F(PathsTest, Get) {
  paths::SetPrefixForTesting(base::FilePath(""));
  EXPECT_EQ("/run/foo", paths::Get("/run/foo").value());
}

TEST_F(PathsTest, SetPrefixForTesting) {
  paths::SetPrefixForTesting(base::FilePath("/tmp"));
  EXPECT_EQ("/tmp/run/foo", paths::Get("/run/foo").value());
  paths::SetPrefixForTesting(base::FilePath());
  EXPECT_EQ("/run/foo", paths::Get("/run/foo").value());
}

TEST_F(PathsTest, GetBootSplashAssetsDirSimonDisabledLowRes) {
  /* Indicate the device is not hi-res. */
  ASSERT_TRUE(base::WriteFile(frecon_hi_res_path(), "0"));

  /* paths:: will return the assets path with the testing prefix, so include
   * the testing prefix in the expected output. */
  std::string expected_path =
      test_dir().value() + kBootSplashAssetsDirSimonDisabledLowRes;

  /* Validate we get the non-simon non-hi-res path to the boot splash assets. */
  EXPECT_EQ(expected_path, paths::GetBootSplashAssetsDir(false).value());
}

TEST_F(PathsTest, GetBootSplashAssetsDirSimonEnabledLowRes) {
  /* Indicate the device is not hi-res. */
  ASSERT_TRUE(base::WriteFile(frecon_hi_res_path(), "0"));

  /* paths:: will return the assets path with the testing prefix, so include
   * the testing prefix in the expected output. */
  std::string expected_path =
      test_dir().value() + kBootSplashAssetsDirSimonEnabledLowRes;

  /* Validate we get the non-simon non-hi-res path to the boot splash assets. */
  EXPECT_EQ(expected_path, paths::GetBootSplashAssetsDir(true).value());
}

TEST_F(PathsTest, GetBootSplashAssetsDirSimonDisabledHiRes) {
  /* Indicate the device is not hi-res. */
  ASSERT_TRUE(base::WriteFile(frecon_hi_res_path(), "1"));

  /* paths:: will return the assets path with the testing prefix, so include
   * the testing prefix in the expected output. */
  std::string expected_path =
      test_dir().value() + kBootSplashAssetsDirSimonDisabledHiRes;

  /* Validate we get the non-simon non-hi-res path to the boot splash assets. */
  EXPECT_EQ(expected_path, paths::GetBootSplashAssetsDir(false).value());
}

TEST_F(PathsTest, GetBootSplashAssetsDirSimonEnabledHiRes) {
  /* Indicate the device is not hi-res. */
  ASSERT_TRUE(base::WriteFile(frecon_hi_res_path(), "1"));

  /* paths:: will return the assets path with the testing prefix, so include
   * the testing prefix in the expected output. */
  std::string expected_path =
      test_dir().value() + kBootSplashAssetsDirSimonEnabledHiRes;

  /* Validate we get the non-simon non-hi-res path to the boot splash assets. */
  EXPECT_EQ(expected_path, paths::GetBootSplashAssetsDir(true).value());
}

}  // namespace
