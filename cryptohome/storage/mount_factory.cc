// Copyright 2013 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/storage/mount_factory.h"

#include <memory>

#include "cryptohome/platform.h"
#include "cryptohome/storage/homedirs.h"
#include "cryptohome/storage/mount.h"
#include "cryptohome/storage/out_of_process_mount_helper.h"

namespace cryptohome {

MountFactory::MountFactory() {}
MountFactory::~MountFactory() {}

Mount* MountFactory::New(Platform* platform,
                         HomeDirs* homedirs,
                         bool legacy_mount,
                         bool bind_mount_downloads) {
  return new Mount(platform, homedirs,
                   std::make_unique<OutOfProcessMountHelper>(
                       legacy_mount, bind_mount_downloads, platform));
}
}  // namespace cryptohome
