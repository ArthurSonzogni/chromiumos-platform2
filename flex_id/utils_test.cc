// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flex_id/utils.h"

#include <optional>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <brillo/files/file_util.h>
#include <gtest/gtest.h>

namespace flex_id {

namespace {

constexpr char kExampleFileContents[] = "file contents \n";
constexpr char kExpectedReadOutput[] = "file contents";
constexpr char kTestFileName[] = "test_file";

}  // namespace

class FlexUtilsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    CHECK(test_dir_.CreateUniqueTempDir());
    test_path_ = test_dir_.GetPath();
  }

  void CreateTestFile(const std::string& file_contents) {
    CHECK(base::WriteFile(test_path_.Append(kTestFileName), file_contents));
  }

  base::ScopedTempDir test_dir_;
  base::FilePath test_path_;
};

TEST_F(FlexUtilsTest, ReadAndTrimFile) {
  CreateTestFile(kExampleFileContents);
  const auto trimmed_file = ReadAndTrimFile(test_path_.Append(kTestFileName));
  EXPECT_EQ(trimmed_file.value(), kExpectedReadOutput);
}

}  // namespace flex_id
