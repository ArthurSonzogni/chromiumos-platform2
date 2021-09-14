// Copyright 2021 The Chromium OS Authors. All rights reserved.
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

  // The handle has no backing file descriptor.
  EXPECT_EQ(-1, GetFileDescriptor(handle));

  // Close the file handle: returns the file descriptor.
  base::ScopedFD fd = CloseFile(handle);
  EXPECT_FALSE(fd.is_valid());
  EXPECT_EQ(-1, fd.get());

  // GetFile should return 0 (the handle is not open).
  EXPECT_EQ(0, GetFile(handle));

  // Unknown handles cannot be found.
  EXPECT_EQ(0, GetFile(~1));

  // Unknown handles have no backing file descriptor.
  EXPECT_EQ(-1, GetFileDescriptor(~1));
  EXPECT_EQ(-1, CloseFile(~1).get());

  // Handle 0 is the invalid file handle value.
  EXPECT_EQ(0, GetFile(0));
  EXPECT_EQ(-1, GetFileDescriptor(0));
  EXPECT_EQ(-1, CloseFile(0).get());
}

TEST(FuseFileHandlesTest, FileHandlesFileDescriptor) {
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());

  // Create a base::ScopedFD.
  base::FilePath name;
  const auto path = dir.GetPath();
  base::ScopedFD sfd = base::CreateAndOpenFdForTemporaryFileInDir(path, &name);
  EXPECT_TRUE(sfd.is_valid());

  // Save its backing file descriptor value in |fd|.
  const int fd = sfd.get();
  EXPECT_NE(-1, fd) << "Invalid file descriptor";

  // Create a new file handle with the base::ScopedFD.
  uint64_t handle = OpenFile(std::move(sfd));
  EXPECT_NE(0, handle);

  // The base::ScopedFD is invalid after the std::move.
  EXPECT_FALSE(sfd.is_valid());
  EXPECT_EQ(-1, sfd.get());

  // And the file handle now owns the file descriptor.
  EXPECT_EQ(fd, GetFileDescriptor(handle));

  // Close the handle: returns the file descriptor.
  base::ScopedFD rfd = CloseFile(handle);
  EXPECT_TRUE(rfd.is_valid());
  EXPECT_EQ(fd, rfd.get());

  // GetFile should return 0 (the handle is not open).
  EXPECT_EQ(0, GetFile(handle));

  // Closed handles have no backing file descriptor.
  EXPECT_EQ(-1, GetFileDescriptor(handle));
  EXPECT_EQ(-1, CloseFile(handle).get());

  // Test done: clean up.
  rfd.reset();
  EXPECT_FALSE(rfd.is_valid());
  ASSERT_TRUE(dir.Delete());
}

}  // namespace fusebox
