// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/lockbox-cache-manager/platform.h"

#include <base/check.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/process/launch.h>
#include <brillo/files/file_util.h>
#include <brillo/scoped_umask.h>

namespace cryptohome {
bool Platform::IsOwnedByRoot(const std::string& path) {
  struct stat st;
  const uid_t ROOT_UID = 0;
  const gid_t ROOT_GID = 0;
  if (stat(path.c_str(), &st) != 0) {
    LOG(ERROR) << "Cannot get ownership info for " << path;
    return false;
  }
  return st.st_uid == ROOT_UID && st.st_gid == ROOT_GID;
}
bool Platform::GetAppOutputAndError(const std::vector<std::string>& argv,
                                    std::string* output) {
  return base::GetAppOutputAndError(argv, output);
}
}  // namespace cryptohome
