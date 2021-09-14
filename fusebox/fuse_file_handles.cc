// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fusebox/fuse_file_handles.h"

#include <unordered_map>

#include <base/check.h>
#include <base/no_destructor.h>

namespace fusebox {

static auto& GetFileHandles() {
  static base::NoDestructor<std::unordered_map<uint64_t, int>> handles;
  return *handles;
}

uint64_t OpenFile(base::ScopedFD fd) {
  static uint64_t next = 0;
  uint64_t handle = ++next;
  CHECK(handle) << "file handles wrapped";
  GetFileHandles()[handle] = fd.release();
  return handle;
}

uint64_t GetFile(uint64_t handle) {
  const auto it = GetFileHandles().find(handle);
  if (it != GetFileHandles().end())
    return handle;  // handle is open
  return 0;
}

int GetFileDescriptor(uint64_t handle) {
  const auto it = GetFileHandles().find(handle);
  if (it != GetFileHandles().end())
    return it->second;  // handle is open
  return -1;
}

base::ScopedFD CloseFile(uint64_t handle) {
  const auto it = GetFileHandles().find(handle);
  if (it == GetFileHandles().end())
    return base::ScopedFD();  // handle is not open
  base::ScopedFD fd(it->second);
  GetFileHandles().erase(it);
  return fd;
}

}  // namespace fusebox
