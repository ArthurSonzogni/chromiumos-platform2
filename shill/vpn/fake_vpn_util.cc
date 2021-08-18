// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/vpn/fake_vpn_util.h"

#include <base/files/file_util.h>

namespace shill {

bool FakeVPNUtil::WriteConfigFile(const base::FilePath& filename,
                                  const std::string& contents) const {
  return base::WriteFile(filename, contents);
}

base::ScopedTempDir FakeVPNUtil::CreateScopedTempDir(
    const base::FilePath& parent_path) const {
  base::ScopedTempDir temp_dir;
  if (!temp_dir.CreateUniqueTempDirUnderPath(parent_path)) {
    LOG(ERROR) << "Failed to create temp dir under path " << parent_path;
    return base::ScopedTempDir{};
  }
  return temp_dir;
}

}  // namespace shill
