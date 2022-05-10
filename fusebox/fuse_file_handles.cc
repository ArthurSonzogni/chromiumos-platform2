// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fusebox/fuse_file_handles.h"

#include <ostream>
#include <unordered_map>
#include <utility>

#include <base/check.h>
#include <base/no_destructor.h>

namespace fusebox {

static auto& GetFileHandles() {
  static base::NoDestructor<std::unordered_map<uint64_t, HandleData>> handles;
  return *handles;
}

uint64_t OpenFile(base::ScopedFD fd) {
  static uint64_t next = 0;
  uint64_t handle = ++next;
  CHECK(handle) << "file handles wrapped";
  GetFileHandles()[handle].fd = fd.release();
  return handle;
}

uint64_t GetFile(uint64_t handle) {
  const auto it = GetFileHandles().find(handle);
  if (it == GetFileHandles().end())
    return 0;
  return handle;
}

int GetFileDescriptor(uint64_t handle) {
  const auto it = GetFileHandles().find(handle);
  if (it == GetFileHandles().end())
    return -1;
  return it->second.fd;
}

int SetFileDescriptor(uint64_t handle, int fd) {
  const auto it = GetFileHandles().find(handle);
  if (it == GetFileHandles().end())
    return -1;
  std::swap(it->second.fd, fd);
  return fd;
}

HandleData GetFileData(uint64_t handle) {
  const auto it = GetFileHandles().find(handle);
  if (it == GetFileHandles().end())
    return {};
  return it->second;
}

bool SetFileData(uint64_t handle, std::string path, std::string type) {
  const auto it = GetFileHandles().find(handle);
  if (it == GetFileHandles().end())
    return false;
  std::swap(it->second.path, path);
  std::swap(it->second.type, type);
  return true;
}

base::ScopedFD CloseFile(uint64_t handle) {
  const auto it = GetFileHandles().find(handle);
  if (it == GetFileHandles().end())
    return {};
  base::ScopedFD fd(it->second.fd);
  GetFileHandles().erase(it);
  return fd;
}

}  // namespace fusebox
