// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <sys/types.h>

#include <string>
#include <vector>

#include <base/files/file_util.h>

#include "init/startup/fake_platform_impl.h"

namespace startup {

FakePlatform::FakePlatform() : Platform() {}

void FakePlatform::SetStatResultForPath(const base::FilePath& path,
                                        const struct stat& st) {
  result_map_[path.value()] = st;
}

void FakePlatform::SetMountResultForPath(const base::FilePath& path,
                                         const std::string& output) {
  mount_result_map_[path.value()] = output;
}

void FakePlatform::SetIoctlReturnValue(int ret) {
  ioctl_ret_ = ret;
}

bool FakePlatform::Stat(const base::FilePath& path, struct stat* st) {
  std::unordered_map<std::string, struct stat>::iterator it;
  it = result_map_.find(path.value());
  if (st == nullptr || it == result_map_.end()) {
    return false;
  }

  *st = it->second;
  return true;
}

bool FakePlatform::Mount(const base::FilePath& src,
                         const base::FilePath& dst,
                         const std::string& type,
                         unsigned long flags,  // NOLINT(runtime/int)
                         const std::string& data) {
  std::unordered_map<std::string, std::string>::iterator it;
  it = mount_result_map_.find(dst.value());
  if (it == mount_result_map_.end()) {
    return false;
  }

  return src.value().compare(it->second) == 0;
}

bool FakePlatform::Mount(const std::string& src,
                         const base::FilePath& dst,
                         const std::string& type,
                         unsigned long flags,  // NOLINT(runtime/int)
                         const std::string& data) {
  std::unordered_map<std::string, std::string>::iterator it;
  it = mount_result_map_.find(dst.value());
  if (it == mount_result_map_.end()) {
    return false;
  }

  return src.compare(it->second) == 0;
}

bool FakePlatform::Umount(const base::FilePath& path) {
  umount_vector_.push_back(path.value());
  return true;
}

base::ScopedFD FakePlatform::Open(const base::FilePath& pathname, int flags) {
  return base::ScopedFD(open_ret_);
}

// NOLINTNEXTLINE(runtime/int)
int FakePlatform::Ioctl(int fd, unsigned long request, int* arg1) {
  return ioctl_ret_;
}

}  // namespace startup
