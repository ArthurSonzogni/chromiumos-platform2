// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/file_io.h"

#include <fcntl.h>
#include <unistd.h>

#include <base/posix/eintr_wrapper.h>

namespace shill {

namespace {

static base::LazyInstance<FileIO> g_file_io =
    LAZY_INSTANCE_INITIALIZER;

}  // namespace

FileIO::FileIO() {}

FileIO::~FileIO() {}

// static
FileIO *FileIO::GetInstance() {
  return g_file_io.Pointer();
}

ssize_t FileIO::Write(int fd, const void *buf, size_t count) {
  return HANDLE_EINTR(write(fd, buf, count));
}

ssize_t FileIO::Read(int fd, void *buf, size_t count) {
  return HANDLE_EINTR(read(fd, buf, count));
}

int FileIO::Close(int fd) {
  return HANDLE_EINTR(close(fd));
}

int FileIO::SetFdNonBlocking(int fd) {
  const int flags = HANDLE_EINTR(fcntl(fd, F_GETFL)) | O_NONBLOCK;
  return HANDLE_EINTR(fcntl(fd, F_SETFL,  flags));
}

}  // namespace shill
