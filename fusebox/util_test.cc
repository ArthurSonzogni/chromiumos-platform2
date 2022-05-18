// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fusebox/util.h"

#include <fcntl.h>
#include <fuse_lowlevel.h>
#include <memory>

#include <base/files/file.h>
#include <brillo/dbus/data_serialization.h>
#include <gtest/gtest.h>

using brillo::dbus_utils::AppendValueToWriter;

namespace fusebox {

TEST(UtilTest, GetResponseErrnoNoServerResponse) {
  dbus::Response* response = nullptr;  // The server did not respond.

  dbus::MessageReader reader(response);
  EXPECT_EQ(ENODEV, GetResponseErrno(&reader, response));
}

TEST(UtilTest, GetResponseErrnoBusyError) {
  std::unique_ptr<dbus::Response> response = dbus::Response::CreateEmpty();

  dbus::MessageWriter writer(response.get());
  int busy_error = static_cast<int>(base::File::Error::FILE_ERROR_IN_USE);
  AppendValueToWriter(&writer, busy_error);
  ASSERT_NE(busy_error, 0);

  dbus::MessageReader reader(response.get());
  EXPECT_EQ(EBUSY, GetResponseErrno(&reader, response.get()));
}

TEST(UtilTest, GetResponseErrnoNoError) {
  std::unique_ptr<dbus::Response> response = dbus::Response::CreateEmpty();

  dbus::MessageWriter writer(response.get());
  int no_error = static_cast<int>(base::File::Error::FILE_OK);
  AppendValueToWriter(&writer, no_error);
  ASSERT_EQ(no_error, 0);

  dbus::MessageReader reader(response.get());
  EXPECT_EQ(0, GetResponseErrno(&reader, response.get()));
}

TEST(UtilTest, GetResponseErrnoPosixErrno) {
  std::unique_ptr<dbus::Response> response = dbus::Response::CreateEmpty();

  dbus::MessageWriter writer(response.get());
  int posix_io_error = EIO;
  AppendValueToWriter(&writer, posix_io_error);
  ASSERT_GT(posix_io_error, 0);

  dbus::MessageReader reader(response.get());
  EXPECT_EQ(EIO, GetResponseErrno(&reader, response.get()));
}

TEST(UtilTest, FileErrorToErrno) {
  int ok = static_cast<int>(base::File::Error::FILE_OK);
  EXPECT_EQ(0, FileErrorToErrno(ok));

  int not_found = static_cast<int>(base::File::Error::FILE_ERROR_NOT_FOUND);
  EXPECT_EQ(ENOENT, FileErrorToErrno(not_found));

  int security = static_cast<int>(base::File::Error::FILE_ERROR_SECURITY);
  EXPECT_EQ(EACCES, FileErrorToErrno(security));

  int io = static_cast<int>(base::File::Error::FILE_ERROR_IO);
  EXPECT_EQ(EIO, FileErrorToErrno(io));
}

TEST(UtilTest, ResponseErrorToErrno) {
  int posix_ok = 0;
  EXPECT_EQ(0, ResponseErrorToErrno(posix_ok));

  int file_ok = static_cast<int>(base::File::Error::FILE_OK);
  EXPECT_EQ(0, ResponseErrorToErrno(file_ok));

  int posix_error = ENOMEM;
  EXPECT_EQ(ENOMEM, ResponseErrorToErrno(posix_error));

  int file_error = static_cast<int>(base::File::Error::FILE_ERROR_IO);
  EXPECT_EQ(EIO, ResponseErrorToErrno(file_error));
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
