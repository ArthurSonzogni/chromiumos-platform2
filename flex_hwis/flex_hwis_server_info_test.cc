// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flex_hwis/flex_hwis_server_info.h"

#include "base/files/file_path.h"
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <gtest/gtest.h>

namespace flex_hwis {

class FlexHwisServerInfoTest : public ::testing::Test {
 protected:
  void SetUp() override {
    CHECK(test_dir_.CreateUniqueTempDir());
    test_path_ = test_dir_.GetPath();
    lsb_path = test_path_.Append("lsb-release");
  }

  void CreateLsbReleaseFile(const std::string& image_info) {
    CHECK(base::WriteFile(lsb_path, image_info));
  }

  ServerInfo server_info_;
  base::ScopedTempDir test_dir_;
  base::FilePath lsb_path;
  base::FilePath test_path_;
};

TEST_F(FlexHwisServerInfoTest, IsTestImage) {
  CreateLsbReleaseFile("CHROMEOS_RELEASE_TRACK=testimage-channel");
  EXPECT_EQ(server_info_.IsTestImage(lsb_path), TestImageResult::TestImage);
}

TEST_F(FlexHwisServerInfoTest, NotTestImage) {
  CreateLsbReleaseFile("CHROMEOS_RELEASE_TRACK=image-channel");
  EXPECT_EQ(server_info_.IsTestImage(lsb_path), TestImageResult::NotTestImage);
}

TEST_F(FlexHwisServerInfoTest, ErrorParseReleaseTrack) {
  CreateLsbReleaseFile("");
  EXPECT_EQ(server_info_.IsTestImage(lsb_path), TestImageResult::Error);
}

}  // namespace flex_hwis
