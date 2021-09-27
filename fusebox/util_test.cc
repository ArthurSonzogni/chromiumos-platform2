// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fusebox/util.h"

#include <fcntl.h>
#include <fuse_lowlevel.h>

#include <gtest/gtest.h>

namespace fusebox {

TEST(UtilTest, OpenFlagsToString) {
  std::string flags = OpenFlagsToString(O_RDONLY);
  EXPECT_EQ("O_RDONLY", flags);

  flags = OpenFlagsToString(O_WRONLY | O_ASYNC);
  EXPECT_EQ("O_WRONLY|O_ASYNC", flags);

  flags = OpenFlagsToString(O_RDWR | O_CREAT | O_EXCL | O_TRUNC);
  EXPECT_EQ("O_RDWR|O_CREAT|O_EXCL|O_TRUNC", flags);

  flags = OpenFlagsToString(O_RDWR | 0x78000000);
  EXPECT_EQ("O_RDWR|0x78000000", flags);

  flags = OpenFlagsToString(O_RDONLY | O_WRONLY | O_RDWR);
  EXPECT_EQ("INVALID_OPEN_MODE", flags);
}

TEST(UtilTest, ToSetFlagsToString) {
  std::string flags = ToSetFlagsToString(0);
  EXPECT_EQ("0", flags);

  flags = ToSetFlagsToString(FUSE_SET_ATTR_SIZE);
  EXPECT_EQ("FUSE_SET_ATTR_SIZE", flags);

  flags = ToSetFlagsToString(FUSE_SET_ATTR_UID | FUSE_SET_ATTR_GID);
  EXPECT_EQ("FUSE_SET_ATTR_UID|FUSE_SET_ATTR_GID", flags);

  flags = ToSetFlagsToString(FUSE_SET_ATTR_ATIME | 0x120000);
  EXPECT_EQ("FUSE_SET_ATTR_ATIME|0x120000", flags);
}

}  // namespace fusebox
