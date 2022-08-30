// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <string>

#include <base/files/file_path.h>
#include <base/files/file_util.h>

#include "init/startup/platform_impl.h"

namespace startup {

bool Platform::Stat(const base::FilePath& path, struct stat* st) {
  return stat(path.value().c_str(), st) == 0;
}

bool Platform::Mount(const base::FilePath& src,
                     const base::FilePath& dst,
                     const std::string& type,
                     const unsigned long flags,  // NOLINT(runtime/int)
                     const std::string& data) {
  return mount(src.value().c_str(), dst.value().c_str(), type.c_str(), flags,
               data.c_str()) == 0;
}

bool Platform::Mount(const std::string& src,
                     const base::FilePath& dst,
                     const std::string& type,
                     const unsigned long flags,  // NOLINT(runtime/int)
                     const std::string& data) {
  return mount(src.c_str(), dst.value().c_str(), type.c_str(), flags,
               data.c_str()) == 0;
}

bool Platform::Umount(const base::FilePath& path) {
  return !umount(path.value().c_str());
}

base::ScopedFD Platform::Open(const base::FilePath& pathname, int flags) {
  return base::ScopedFD(HANDLE_EINTR(open(pathname.value().c_str(), flags)));
}

// NOLINTNEXTLINE(runtime/int)
int Platform::Ioctl(int fd, unsigned long request, int* arg1) {
  return ioctl(fd, request, arg1);
}

}  // namespace startup
