// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/util/file.h"

#include <base/files/file.h>
#include <base/files/scoped_temp_dir.h>
#include <base/test/test_file_util.h>

#include <gtest/gtest.h>

namespace reporting {
namespace {

TEST(FileTest, DeleteFileWarnIfFailed) {
  // This test briefly tests DeleteFileWarnIfFailed, as it mostly calls
  // DeleteFile(), which should be more extensively tested in base.
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  const auto dir_path = temp_dir.GetPath();
  ASSERT_TRUE(base::DirectoryExists(dir_path));

  base::FilePath file_path;
  ASSERT_TRUE(base::CreateTemporaryFileInDir(dir_path, &file_path));

  // Delete an existing file with no permission.
  // Difference from the counterpart in components: We don't care about Windows
  // and Fuchsia here.
  {
    // we modify the directory permission to prevent it from being deleted.
    base::FilePermissionRestorer restore_permissions_for(dir_path);
    // Get rid of the write permission from temp_dir
    ASSERT_TRUE(base::MakeFileUnwritable(dir_path));
    // Ensure no deletion permission
    ASSERT_FALSE(base::PathIsWritable(dir_path));
    ASSERT_TRUE(base::PathExists(file_path));
    ASSERT_FALSE(DeleteFileWarnIfFailed(file_path))
        << "Deletion of an existing file without permission should fail";
  }

  {
    // Delete with permission
    ASSERT_TRUE(base::PathIsWritable(dir_path));  // Ensure deletion permission
    ASSERT_TRUE(base::PathExists(file_path));
    ASSERT_TRUE(DeleteFileWarnIfFailed(file_path))
        << "Deletion of an existing file should succeed";
    ASSERT_FALSE(base::PathExists(file_path)) << "File failed to be deleted";
  }

  // Delete a non-existing file
  {
    ASSERT_FALSE(base::PathExists(file_path));
    ASSERT_TRUE(DeleteFileWarnIfFailed(file_path))
        << "Deletion of a nonexisting file should succeed";
  }
}

TEST(FileTest, DeleteFilesWarnIfFailed) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  const auto dir_path = temp_dir.GetPath();
  ASSERT_TRUE(base::DirectoryExists(dir_path));

  base::FilePath file_path;
  ASSERT_TRUE(base::CreateTemporaryFileInDir(dir_path, &file_path));

  // empty the directory
  base::FileEnumerator dir_enum(dir_path, /*recursive=*/false,
                                base::FileEnumerator::FILES, "*");
  ASSERT_TRUE(DeleteFilesWarnIfFailed(dir_enum))
      << "Failed to delete " << file_path.MaybeAsASCII();
  ASSERT_FALSE(base::PathExists(file_path))
      << "Deletion succeeds but " << file_path.MaybeAsASCII()
      << " still exists.";
}

}  // namespace
}  // namespace reporting
