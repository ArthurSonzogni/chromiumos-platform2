// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "debugd/src/drm_trace_tool.h"

#include <limits.h>
#include <memory>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/strings/stringprintf.h>
#include <gtest/gtest.h>

namespace debugd {

class DRMTraceToolTest : public testing::Test {
 protected:
  base::ScopedTempDir temp_dir_;
  std::unique_ptr<DRMTraceTool> drm_trace_tool_;

  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());

    // Create files that we expect to interact with in DRMTraceTool.
    ASSERT_TRUE(base::CreateDirectory(
        temp_dir_.GetPath().Append("sys/module/drm/parameters")));
    ASSERT_EQ(0, base::WriteFile(temp_dir_.GetPath().Append(
                                     "sys/module/drm/parameters/trace"),
                                 "", 0));

    ASSERT_TRUE(base::CreateDirectory(
        temp_dir_.GetPath().Append("sys/kernel/debug/tracing/instances/drm")));
    ASSERT_EQ(0,
              base::WriteFile(
                  temp_dir_.GetPath().Append(
                      "sys/kernel/debug/tracing/instances/drm/buffer_size_kb"),
                  "", 0));
    ASSERT_EQ(0, base::WriteFile(
                     temp_dir_.GetPath().Append(
                         "sys/kernel/debug/tracing/instances/drm/trace_marker"),
                     "", 0));

    // Initialize DRMTraceTool with a fake root path for testing.
    drm_trace_tool_ =
        std::unique_ptr<DRMTraceTool>(new DRMTraceTool(temp_dir_.GetPath()));
  }
};

TEST_F(DRMTraceToolTest, SetCategories) {
  brillo::ErrorPtr error;

  EXPECT_TRUE(drm_trace_tool_->SetCategories(&error, 0));
  EXPECT_EQ(error, nullptr);

  uint32_t all_flags = DRMTraceCategory_CORE | DRMTraceCategory_DRIVER |
                       DRMTraceCategory_KMS | DRMTraceCategory_PRIME |
                       DRMTraceCategory_ATOMIC | DRMTraceCategory_VBL |
                       DRMTraceCategory_STATE | DRMTraceCategory_LEASE |
                       DRMTraceCategory_DP | DRMTraceCategory_DRMRES;

  EXPECT_TRUE(drm_trace_tool_->SetCategories(&error, all_flags));
  EXPECT_EQ(error, nullptr);

  uint32_t too_large_flag = DRMTraceCategory_DRMRES << 1;
  EXPECT_FALSE(drm_trace_tool_->SetCategories(&error, too_large_flag));
  EXPECT_NE(error, nullptr);
}

TEST_F(DRMTraceToolTest, SetSize) {
  brillo::ErrorPtr error;

  EXPECT_TRUE(drm_trace_tool_->SetSize(&error, DRMTraceSize_DEFAULT));
  EXPECT_EQ(error, nullptr);

  EXPECT_TRUE(drm_trace_tool_->SetSize(&error, DRMTraceSize_DEBUG));
  EXPECT_EQ(error, nullptr);

  uint32_t invalid_enum = DRMTraceSize_DEBUG + 1;
  EXPECT_FALSE(drm_trace_tool_->SetSize(&error, invalid_enum));
  EXPECT_NE(error, nullptr);
}

TEST_F(DRMTraceToolTest, AnnotateLog) {
  brillo::ErrorPtr error;
  EXPECT_TRUE(drm_trace_tool_->AnnotateLog(&error, "elephant"));
  EXPECT_EQ(error, nullptr);

  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(
      temp_dir_.GetPath().Append(
          "sys/kernel/debug/tracing/instances/drm/trace_marker"),
      &contents));
  EXPECT_EQ(contents, "elephant");
}

TEST_F(DRMTraceToolTest, AnnotateLogInvalidCharacter) {
  brillo::ErrorPtr error;
  EXPECT_TRUE(drm_trace_tool_->AnnotateLog(&error, "bell\a"));
  EXPECT_EQ(error, nullptr);

  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(
      temp_dir_.GetPath().Append(
          "sys/kernel/debug/tracing/instances/drm/trace_marker"),
      &contents));
  EXPECT_EQ(contents, "bell_");
}

TEST_F(DRMTraceToolTest, AnnotateLogTooLarge) {
  brillo::ErrorPtr error;

  // Large buffer filled with 'c's.
  std::string large_log(1024 * 1024, 'c');

  EXPECT_FALSE(drm_trace_tool_->AnnotateLog(&error, large_log));
  EXPECT_NE(error, nullptr);
}

TEST_F(DRMTraceToolTest, WriteToNonExistentFile) {
  brillo::ErrorPtr error;

  EXPECT_FALSE(DRMTraceTool::WriteToFile(
      &error, base::FilePath("/probably/not/a/real/file"), "content"));
  EXPECT_NE(error, nullptr);
}

TEST_F(DRMTraceToolTest, WriteToReadOnlyFile) {
  brillo::ErrorPtr error;

  // Create a new file, and make it read-only
  base::FilePath path = temp_dir_.GetPath().Append("readonly-file");
  ASSERT_TRUE(base::WriteFile(path, "data"));
  base::SetPosixFilePermissions(
      path, base::FilePermissionBits::FILE_PERMISSION_READ_BY_USER);

  EXPECT_FALSE(DRMTraceTool::WriteToFile(&error, path, "content"));
  EXPECT_NE(error, nullptr);
}

TEST_F(DRMTraceToolTest, WriteToNonWritableFile) {
  brillo::ErrorPtr error;

  // Attempt to write to a directory.
  base::FilePath path = temp_dir_.GetPath().Append("directory");
  ASSERT_TRUE(base::CreateDirectory(path));

  EXPECT_FALSE(DRMTraceTool::WriteToFile(&error, path, "content"));
  EXPECT_NE(error, nullptr);
}

}  // namespace debugd
