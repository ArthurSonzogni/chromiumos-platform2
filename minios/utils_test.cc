// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <gtest/gtest.h>

#include "minios/mock_process_manager.h"
#include "minios/utils.h"

using testing::_;

namespace minios {

class UtilTest : public ::testing::Test {
 public:
  void SetUp() override {
    ASSERT_TRUE(tmp_dir_.CreateUniqueTempDir());
    file_path_ = tmp_dir_.GetPath().Append("file");
    content_ = "line1\nline2\n" + std::string(100, 'a') + "\nb";
    ASSERT_TRUE(base::WriteFile(file_path_, content_));
    ASSERT_TRUE(
        CreateDirectory(tmp_dir_.GetPath().Append("sys/firmware/vpd/ro")));
  }

 protected:
  base::ScopedTempDir tmp_dir_;
  base::FilePath file_path_;
  std::string content_;
};

TEST_F(UtilTest, ReadFileContentOffsets) {
  auto [success, content] = ReadFileContentWithinRange(
      file_path_, /*start_offset=*/0, /*end_offset=*/1, /*num_cols=*/1);
  EXPECT_TRUE(success);
  EXPECT_EQ(content, "l\n");
}

TEST_F(UtilTest, ReadFileContentOffsets2) {
  auto [success, content] = ReadFileContentWithinRange(
      file_path_, /*start_offset=*/0, /*end_offset=*/1, /*num_cols=*/2);
  EXPECT_TRUE(success);
  EXPECT_EQ(content, "l");
}

TEST_F(UtilTest, ReadFileContentOffsets3) {
  auto [success, content] = ReadFileContentWithinRange(
      file_path_, /*start_offset=*/4, /*end_offset=*/6, /*num_cols=*/1);
  EXPECT_TRUE(success);
  EXPECT_EQ(content, "1\n");
}

TEST_F(UtilTest, ReadFileContentOffsets4) {
  auto [success, content] = ReadFileContentWithinRange(
      file_path_, /*start_offset=*/2, /*end_offset=*/7, /*num_cols=*/2);
  EXPECT_TRUE(success);
  EXPECT_EQ(content, "ne\n1\nl");
}

TEST_F(UtilTest, ReadFileContentMissingFile) {
  auto [success, content, bytes_read] =
      ReadFileContent(base::FilePath("/a/b/foobar"), 1, 1, 1);
  EXPECT_FALSE(success);
}

TEST_F(UtilTest, ReadFileContentWrappedTextCutoff) {
  auto [success, content, bytes_read] = ReadFileContent(
      file_path_, /*start_offset=*/0, /*num_lines=*/3, /*num_cols=*/4);
  EXPECT_TRUE(success);
  EXPECT_EQ(content, "line\n1\nline\n");
  EXPECT_LT(bytes_read, content.size());
}

TEST_F(UtilTest, ReadFileContentWrappedTextPerfectAlignmentColumns) {
  auto [success, content, bytes_read] = ReadFileContent(
      file_path_, /*start_offset=*/0, /*num_lines=*/5, /*num_cols=*/5);
  EXPECT_TRUE(success);
  // There should be no double newlining.
  EXPECT_EQ(content, "line1\nline2\naaaaa\naaaaa\naaaaa\n");
  EXPECT_LT(bytes_read, content.size());
}

TEST_F(UtilTest, ReadFileContentWrappedTextExceedsColumnLimit) {
  auto [success, content, bytes_read] = ReadFileContent(
      file_path_, /*start_offset=*/0, /*num_lines=*/5, /*num_cols=*/6);
  EXPECT_TRUE(success);
  EXPECT_EQ(content, "line1\nline2\naaaaaa\naaaaaa\naaaaaa\n");
  EXPECT_LT(bytes_read, content.size());
}

TEST_F(UtilTest, ReadFileContentZeroLimits) {
  auto [success, content, bytes_read] = ReadFileContent(
      file_path_, /*start_offset=*/0, /*num_lines=*/0, /*num_cols=*/0);
  EXPECT_TRUE(success);
  EXPECT_EQ(content, "");
  EXPECT_EQ(bytes_read, 0);
}

TEST_F(UtilTest, ReadFileContent) {
  auto [success, content, bytes_read] =
      ReadFileContent(file_path_, /*start_offset=*/0, /*num_lines=*/4,
                      /*num_cols=*/200);
  EXPECT_TRUE(success);
  EXPECT_EQ(content, content_);
  EXPECT_EQ(bytes_read, content_.size());
}

TEST_F(UtilTest, ReadFileContentStartOffset) {
  auto [success, content, bytes_read] =
      ReadFileContent(file_path_, /*start_offset=*/12, /*num_lines=*/3,
                      /*num_cols=*/1);
  EXPECT_TRUE(success);
  EXPECT_EQ(content, "a\na\na\n");
  EXPECT_EQ(bytes_read, 3);
}

TEST_F(UtilTest, GetKeyboardLayoutFailure) {
  MockProcessManager mock_process_manager_;
  EXPECT_CALL(mock_process_manager_, RunCommandWithOutput(_, _, _, _))
      .WillOnce(
          testing::DoAll(testing::SetArgPointee<2>(""), testing::Return(true)));
  EXPECT_EQ(GetKeyboardLayout(&mock_process_manager_), "us");

  // Badly formatted.
  EXPECT_CALL(mock_process_manager_, RunCommandWithOutput(_, _, _, _))
      .WillOnce(testing::DoAll(testing::SetArgPointee<2>("xkbeng:::"),
                               testing::Return(true)));
  EXPECT_EQ(GetKeyboardLayout(&mock_process_manager_), "us");

  // Failed.
  EXPECT_CALL(mock_process_manager_, RunCommandWithOutput(_, _, _, _))
      .WillOnce(testing::DoAll(testing::Return(false)));
  EXPECT_EQ(GetKeyboardLayout(&mock_process_manager_), "us");
}

TEST_F(UtilTest, GetKeyboardLayout) {
  MockProcessManager mock_process_manager_;
  EXPECT_CALL(mock_process_manager_, RunCommandWithOutput(_, _, _, _))
      .WillOnce(testing::DoAll(testing::SetArgPointee<2>("xkb:en::eng"),
                               testing::Return(true)));
  EXPECT_EQ(GetKeyboardLayout(&mock_process_manager_), "en");
}

}  // namespace minios
