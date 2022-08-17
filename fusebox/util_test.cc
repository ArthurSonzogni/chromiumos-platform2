// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fusebox/util.h"

#include <fcntl.h>
#include <fuse_lowlevel.h>
#include <memory>

#include <brillo/dbus/data_serialization.h>
#include <gtest/gtest.h>

using brillo::dbus_utils::AppendValueToWriter;

namespace fusebox {

TEST(UtilTest, GetResponseErrnoNoServerResponse) {
  dbus::Response* response = nullptr;  // The server did not respond.

  dbus::MessageReader reader(response);
  EXPECT_EQ(EHOSTUNREACH, GetResponseErrno(&reader, response));
}

TEST(UtilTest, GetResponseErrnoBusyError) {
  std::unique_ptr<dbus::Response> response = dbus::Response::CreateEmpty();

  dbus::MessageWriter writer(response.get());
  AppendValueToWriter(&writer, EBUSY);

  dbus::MessageReader reader(response.get());
  EXPECT_EQ(EBUSY, GetResponseErrno(&reader, response.get()));
}

TEST(UtilTest, GetResponseErrnoNoError) {
  std::unique_ptr<dbus::Response> response = dbus::Response::CreateEmpty();

  dbus::MessageWriter writer(response.get());
  AppendValueToWriter(&writer, 0);

  dbus::MessageReader reader(response.get());
  EXPECT_EQ(0, GetResponseErrno(&reader, response.get()));
}

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
  EXPECT_EQ("INVALID_O_ACCMODE_FLAG", flags);
}

TEST(UtilTest, ToSetFlagsToString) {
  std::string flags = ToSetFlagsToString(0);
  EXPECT_EQ("0", flags);

  flags = ToSetFlagsToString(FUSE_SET_ATTR_SIZE);
  EXPECT_EQ("FUSE_SET_ATTR_SIZE", flags);

  flags = ToSetFlagsToString(FUSE_SET_ATTR_UID | FUSE_SET_ATTR_GID);
  EXPECT_EQ("FUSE_SET_ATTR_UID|FUSE_SET_ATTR_GID", flags);

  flags = ToSetFlagsToString(FUSE_SET_ATTR_ATIME | 0x110000);
  EXPECT_EQ("FUSE_SET_ATTR_ATIME|0x110000", flags);

  flags = ToSetFlagsToString(FUSE_SET_ATTR_ATIME | FUSE_SET_ATTR_ATIME_NOW);
  EXPECT_EQ("FUSE_SET_ATTR_ATIME|FUSE_SET_ATTR_ATIME_NOW", flags);

  flags = ToSetFlagsToString(FUSE_SET_ATTR_MTIME | 0x120000);
  EXPECT_EQ("FUSE_SET_ATTR_MTIME|0x120000", flags);

  flags = ToSetFlagsToString(FUSE_SET_ATTR_MTIME | FUSE_SET_ATTR_MTIME_NOW);
  EXPECT_EQ("FUSE_SET_ATTR_MTIME|FUSE_SET_ATTR_MTIME_NOW", flags);
}

}  // namespace fusebox
