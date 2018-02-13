// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "smbprovider/iterator/post_depth_first_iterator.h"

#include "smbprovider/proto.h"
#include "smbprovider/samba_interface.h"

namespace smbprovider {

PostDepthFirstIterator::PostDepthFirstIterator(const std::string& dir_path,
                                               SambaInterface* samba_interface)
    : DepthFirstIterator(dir_path, samba_interface) {}

int32_t PostDepthFirstIterator::OnPop(const DirectoryEntry& entry) {
  SetCurrent(entry);
  return 0;
}

}  // namespace smbprovider
