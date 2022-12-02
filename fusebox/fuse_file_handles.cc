// Copyright 2021 The ChromiumOS Authors
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

uint64_t OpenFile() {
  static uint64_t next = 0;
  uint64_t handle = ++next;
  CHECK(handle) << "file handles wrapped";
  GetFileHandles()[handle] = HandleData();
  return handle;
}

uint64_t GetFile(uint64_t handle) {
  const auto it = GetFileHandles().find(handle);
  if (it == GetFileHandles().end())
    return 0;
  return handle;
}

HandleData GetFileData(uint64_t handle) {
  const auto it = GetFileHandles().find(handle);
  if (it == GetFileHandles().end())
    return {};
  return it->second;
}

bool SetFileData(uint64_t handle,
                 uint64_t server_side_fuse_handle,
                 std::string path,
                 std::string type) {
  const auto it = GetFileHandles().find(handle);
  if (it == GetFileHandles().end())
    return false;
  it->second.server_side_fuse_handle = server_side_fuse_handle;
  std::swap(it->second.path, path);
  std::swap(it->second.type, type);
  return true;
}

void CloseFile(uint64_t handle) {
  const auto it = GetFileHandles().find(handle);
  if (it == GetFileHandles().end())
    return;
  GetFileHandles().erase(it);
}

}  // namespace fusebox
