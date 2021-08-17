// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <string>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <gtest/gtest.h>
#include <rootdev/rootdev.h>
#include "init/utils.h"

TEST(GetRootDevice, NoStripPartition) {
  base::FilePath root_dev;
  char dev_path[PATH_MAX];
  int ret = rootdev(dev_path, sizeof(dev_path), true, false);
  EXPECT_EQ(!ret, utils::GetRootDevice(&root_dev, false));
  EXPECT_EQ(dev_path, root_dev.value());
}

TEST(ReadFileToInt, IntContents) {
  base::ScopedTempDir temp_dir_;
  ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
  base::FilePath file = temp_dir_.GetPath().Append("file");
  ASSERT_TRUE(base::WriteFile(file, "1"));
  int output;
  EXPECT_EQ(utils::ReadFileToInt(file, &output), true);
  EXPECT_EQ(output, 1);
}

TEST(ReadFileToInt, StringContents) {
  base::ScopedTempDir temp_dir_;
  ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
  base::FilePath file = temp_dir_.GetPath().Append("file");
  ASSERT_TRUE(base::WriteFile(file, "Not an int"));
  int output;
  EXPECT_EQ(utils::ReadFileToInt(file, &output), false);
}
