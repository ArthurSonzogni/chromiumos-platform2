// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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
                     const unsigned long flags,
                     const std::string& data) {
  return mount(src.value().c_str(), dst.value().c_str(), type.c_str(), flags,
               data.c_str()) == 0;
}

bool Platform::Mount(const std::string& src,
                     const base::FilePath& dst,
                     const std::string& type,
                     const unsigned long flags,
                     const std::string& data) {
  return mount(src.c_str(), dst.value().c_str(), type.c_str(), flags,
               data.c_str()) == 0;
}

bool Platform::Umount(const base::FilePath& path) {
  return !umount(path.value().c_str());
}

}  // namespace startup
