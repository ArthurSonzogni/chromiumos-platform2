// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/mount_stack.h"

#include <algorithm>
#include <base/logging.h>

MountStack::MountStack() { }
MountStack::~MountStack() {
  if (!mounts_.empty()) {
    LOG(ERROR) << "MountStack destroyed with " << mounts_.size() << "mounts.";
    std::vector<std::string>::iterator it;
    for (it = mounts_.begin(); it != mounts_.end(); ++it)
      LOG(ERROR) << "  " << *it;
  }
}

void MountStack::Push(const std::string& path) {
  mounts_.push_back(path);
}

bool MountStack::Pop(std::string* path) {
  if (mounts_.empty())
    return false;
  *path = mounts_.back();
  mounts_.pop_back();
  return true;
}

bool MountStack::Contains(const std::string& path) const {
  return std::find(mounts_.begin(), mounts_.end(), path) != mounts_.end();
}
