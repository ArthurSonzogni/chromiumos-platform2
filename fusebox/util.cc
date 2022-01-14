// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fusebox/util.h"

#include <fcntl.h>
#include <fuse_lowlevel.h>

#include <base/check.h>
#include <base/files/file.h>
#include <base/logging.h>
#include <base/posix/safe_strerror.h>
#include <base/strings/stringprintf.h>

int GetResponseErrno(dbus::MessageReader* reader, dbus::Response* response) {
  DCHECK(reader);

  if (!response) {
    LOG(ERROR) << "error: no server response";
    return ENODEV;
  }

  int32_t response_file_error;
  CHECK(reader->PopInt32(&response_file_error));

  if (response_file_error != 0) {
    int file_errno = FileErrorToErrno(response_file_error);
    auto file_error = static_cast<base::File::Error>(response_file_error);
    LOG(ERROR) << "error: " << base::safe_strerror(file_errno) << " ["
               << base::File::ErrorToString(file_error) << "]";
    return file_errno;
  }

  return 0;
}

int FileErrorToErrno(int error) {
  const auto file_error = static_cast<base::File::Error>(error);

  switch (file_error) {
    case base::File::Error::FILE_OK:
      return 0;
    case base::File::Error::FILE_ERROR_FAILED:
      return EFAULT;
    case base::File::Error::FILE_ERROR_IN_USE:
      return EBUSY;
    case base::File::Error::FILE_ERROR_EXISTS:
      return EEXIST;
    case base::File::Error::FILE_ERROR_NOT_FOUND:
      return ENOENT;
    case base::File::Error::FILE_ERROR_ACCESS_DENIED:
      return EACCES;
    case base::File::Error::FILE_ERROR_TOO_MANY_OPENED:
      return EMFILE;
    case base::File::Error::FILE_ERROR_NO_MEMORY:
      return ENOMEM;
    case base::File::Error::FILE_ERROR_NO_SPACE:
      return ENOSPC;
    case base::File::Error::FILE_ERROR_NOT_A_DIRECTORY:
      return ENOTDIR;
    case base::File::Error::FILE_ERROR_INVALID_OPERATION:
      return ENOTSUP;
    case base::File::Error::FILE_ERROR_SECURITY:
      return EACCES;
    case base::File::Error::FILE_ERROR_ABORT:
      return ENOTSUP;
    case base::File::Error::FILE_ERROR_NOT_A_FILE:
      return EINVAL;
    case base::File::Error::FILE_ERROR_NOT_EMPTY:
      return ENOTEMPTY;
    case base::File::Error::FILE_ERROR_INVALID_URL:
      return EINVAL;
    case base::File::Error::FILE_ERROR_IO:
      return EIO;
    default:
      return EFAULT;
  }
}

namespace {

struct FlagDef {
  const int flag;
  const char* name;
};

#define FLAG_DEF(f) \
  { f, #f }

const FlagDef kOpenFlags[] = {
    FLAG_DEF(O_APPEND),   FLAG_DEF(O_ASYNC),  FLAG_DEF(O_CLOEXEC),
    FLAG_DEF(O_CREAT),    FLAG_DEF(O_DIRECT), FLAG_DEF(O_DIRECTORY),
    FLAG_DEF(O_DSYNC),    FLAG_DEF(O_EXCL),   FLAG_DEF(O_LARGEFILE),
    FLAG_DEF(O_NOATIME),  FLAG_DEF(O_NOCTTY), FLAG_DEF(O_NOFOLLOW),
    FLAG_DEF(O_NONBLOCK), FLAG_DEF(O_PATH),   FLAG_DEF(O_SYNC),
    FLAG_DEF(O_TMPFILE),  FLAG_DEF(O_TRUNC),
};

const FlagDef kFuseToSetFlags[] = {
    FLAG_DEF(FUSE_SET_ATTR_MODE),      FLAG_DEF(FUSE_SET_ATTR_UID),
    FLAG_DEF(FUSE_SET_ATTR_GID),       FLAG_DEF(FUSE_SET_ATTR_SIZE),
    FLAG_DEF(FUSE_SET_ATTR_ATIME),     FLAG_DEF(FUSE_SET_ATTR_MTIME),
    FLAG_DEF(FUSE_SET_ATTR_ATIME_NOW), FLAG_DEF(FUSE_SET_ATTR_MTIME_NOW),
};

template <size_t N>
std::string FlagsToString(const FlagDef (&defs)[N], int flags) {
  std::string flags_string;

  if (!flags)
    return "0";

  for (const auto& d : defs) {
    if (flags & d.flag) {
      if (!flags_string.empty())
        flags_string.append("|");
      flags_string.append(d.name);
      flags &= ~d.flag;
    }
  }

  if (flags) {
    if (!flags_string.empty())
      flags_string.append("|");
    flags_string.append(base::StringPrintf("0x%x", flags));
  }

  return flags_string;
}

}  // namespace

std::string OpenFlagsToString(int flags) {
  std::string open_flags_string;

  switch (flags & O_ACCMODE) {
    case O_RDONLY:
      open_flags_string = "O_RDONLY";
      break;
    case O_WRONLY:
      open_flags_string = "O_WRONLY";
      break;
    case O_RDWR:
      open_flags_string = "O_RDWR";
      break;
    default:
      open_flags_string = "INVALID_OPEN_MODE";
      break;
  }

  flags &= ~O_ACCMODE;
  if (flags) {
    open_flags_string.append("|");
    open_flags_string.append(FlagsToString(kOpenFlags, flags));
  }

  return open_flags_string;
}

std::string ToSetFlagsToString(int flags) {
  return FlagsToString(kFuseToSetFlags, flags);
}
