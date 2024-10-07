// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dbus_perfetto_producer/util.h"

#include <unistd.h>

namespace dbus_perfetto_producer {

bool WriteInt(int fd, uint64_t num) {
  return (write(fd, &num, sizeof(uint64_t)) > 0);
}

bool WriteBuf(int fd, const char* name) {
  size_t len = name ? strlen(name) + 1 : 0;
  return (write(fd, &len, sizeof(size_t)) > 0 && write(fd, name, len) >= 0);
}

bool ReadInt(int fd, uint64_t& ptr) {
  return (read(fd, &ptr, sizeof(uint64_t)) > 0);
}

bool ReadBuf(int fd, std::string& buf) {
  size_t size;
  if (read(fd, &size, sizeof(size_t)) <= 0) {
    return false;
  }

  if (size > 0) {
    constexpr ssize_t BUF_SIZE = 1024;
    char buf_tmp[BUF_SIZE];
    if (read(fd, buf_tmp, size) <= 0) {
      return false;
    }
    buf = std::string(buf_tmp);
  } else {
    buf = "";
  }

  return true;
}

}  // namespace dbus_perfetto_producer
