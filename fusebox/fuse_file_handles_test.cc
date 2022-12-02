// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fusebox/fuse_file_handles.h"

#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <gtest/gtest.h>

namespace fusebox {

TEST(FuseFileHandlesTest, FileHandles) {
  // Create a new open file handle.
  uint64_t handle = OpenFile();
  EXPECT_NE(0, handle);

  // Find the open file handle.
  EXPECT_EQ(handle, GetFile(handle));

  // Close the file handle.
  CloseFile(handle);

  // Handle closed: API that need an open handle fail.
  EXPECT_EQ(0, GetFile(handle));

  // Unknown handles cannot be found.
  EXPECT_EQ(0, GetFile(~1));

  // Handle 0 is the invalid file handle value.
  EXPECT_EQ(0, GetFile(0));
}

TEST(FuseFileHandlesTest, FileHandlesFileData) {
  // Create a new open file handle.
  uint64_t handle = OpenFile();
  EXPECT_NE(0, handle);
  EXPECT_EQ(handle, GetFile(handle));

  // The handle can hold optional data.
  EXPECT_EQ(true, SetFileData(handle, 0, "something"));
  EXPECT_EQ("something", GetFileData(handle).path);
  EXPECT_EQ("", GetFileData(handle).type);

  // The data path could be a url.
  EXPECT_EQ(true, SetFileData(handle, 0, "file://foo/bar"));
  EXPECT_EQ("file://foo/bar", GetFileData(handle).path);
  EXPECT_EQ("", GetFileData(handle).type);

  // An optional type can be specified.
  EXPECT_EQ(true, SetFileData(handle, 0, "filesystem:url", "mtp"));
  EXPECT_EQ("filesystem:url", GetFileData(handle).path);
  EXPECT_EQ("mtp", GetFileData(handle).type);

  // Close the file handle.
  CloseFile(handle);

  // Closed handles have no optional data.
  EXPECT_EQ("", GetFileData(handle).path);
  EXPECT_EQ("", GetFileData(handle).type);

  // Unknown handles have no optional data.
  EXPECT_EQ(0, GetFile(~1));
  EXPECT_EQ("", GetFileData(~0).path);
  EXPECT_EQ("", GetFileData(~0).type);

  // Handle 0 is the invalid handle value.
  EXPECT_EQ(0, GetFile(0));
  EXPECT_EQ("", GetFileData(0).path);
  EXPECT_EQ("", GetFileData(0).type);
}

}  // namespace fusebox
