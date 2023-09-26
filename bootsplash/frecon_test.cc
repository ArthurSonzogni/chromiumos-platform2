// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/process/launch.h>
#include <gtest/gtest.h>

#include "bootsplash/frecon.h"
#include "bootsplash/paths.h"

using base::FilePath;

namespace {

class FreconTest : public ::testing::Test {
 protected:
  const FilePath& test_dir() const { return scoped_temp_dir_.GetPath(); }
  const FilePath& frecon_vt_path() const { return frecon_vt_path_; }

 private:
  void SetUp() override {
    ASSERT_TRUE(scoped_temp_dir_.CreateUniqueTempDir());
    paths::SetPrefixForTesting(test_dir());

    // Create an empty vt0 file to Write() to.
    frecon_vt_path_ = paths::Get(paths::kFreconVt);
    ASSERT_TRUE(base::CreateDirectory(frecon_vt_path_.DirName()));
    ASSERT_TRUE(base::WriteFile(frecon_vt_path_, ""));

    // Create an empty hi_res file.
    frecon_hi_res_path_ = paths::Get(paths::kFreconHiRes);
    ASSERT_TRUE(base::CreateDirectory(frecon_hi_res_path_.DirName()));
    ASSERT_TRUE(base::WriteFile(frecon_hi_res_path_, ""));

    // Create an empty boot splash assets directory.
    boot_splash_assets_dir_ = paths::GetBootSplashAssetsDir(false);
    ASSERT_TRUE(base::CreateDirectory(boot_splash_assets_dir_));
  }

  base::ScopedTempDir scoped_temp_dir_;
  base::FilePath frecon_vt_path_;
  base::FilePath frecon_hi_res_path_;
  base::FilePath boot_splash_assets_dir_;
};

// Test frecon process can be initialized and destroyed.
TEST_F(FreconTest, TestInitFrecon) {
  std::unique_ptr<bootsplash::Frecon> frecon_ =
      bootsplash::Frecon::Create(false);
}

// Test writing to frecon and to an output file.
TEST_F(FreconTest, Write) {
  std::unique_ptr<bootsplash::Frecon> frecon_ =
      bootsplash::Frecon::Create(false);

  // Record the previous file contents, which will be appended to.
  std::string prev_file_contents;
  ASSERT_TRUE(ReadFileToString(frecon_vt_path(), &prev_file_contents));

  // Write some new data to the file.
  frecon_->Write("some text");

  // Validate the new data was written.
  std::string curr_file_contents;
  ASSERT_TRUE(ReadFileToString(frecon_vt_path(), &curr_file_contents));
  EXPECT_EQ(curr_file_contents, prev_file_contents + "some text");
}

}  // namespace
