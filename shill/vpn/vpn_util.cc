// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/vpn/vpn_util.h"

#include <sys/stat.h>
#include <sys/types.h>

#include <memory>

#include <base/files/file_util.h>

namespace shill {

class VPNUtilImpl : public VPNUtil {
 public:
  bool WriteConfigFile(const base::FilePath& filename,
                       const std::string& contents) const override;
  base::ScopedTempDir CreateScopedTempDir(
      const base::FilePath& parent_path) const override;
};

bool VPNUtilImpl::WriteConfigFile(const base::FilePath& filename,
                                  const std::string& contents) const {
  if (!base::WriteFile(filename, contents)) {
    LOG(ERROR) << "Failed to write config file";
    return false;
  }

  if (chmod(filename.value().c_str(), S_IRUSR | S_IRGRP) != 0) {
    PLOG(ERROR) << "Failed to make config file group-readable";
    return false;
  }

  if (chown(filename.value().c_str(), -1, kVPNGid) != 0) {
    PLOG(ERROR) << "Failed to change gid of config file";
    return false;
  }

  return true;
}

base::ScopedTempDir VPNUtilImpl::CreateScopedTempDir(
    const base::FilePath& parent_path) const {
  base::ScopedTempDir temp_dir;

  if (!temp_dir.CreateUniqueTempDirUnderPath(parent_path)) {
    PLOG(ERROR) << "Failed to create temp dir under path " << parent_path;
    return base::ScopedTempDir{};
  }

  if (chmod(temp_dir.GetPath().value().c_str(), S_IRWXU | S_IRWXG) != 0) {
    PLOG(ERROR) << "Failed to change the permission of temp dir";
    return base::ScopedTempDir{};
  }

  if (chown(temp_dir.GetPath().value().c_str(), -1, VPNUtil::kVPNGid) != 0) {
    PLOG(ERROR) << "Failed to change gid of temp dir";
    return base::ScopedTempDir{};
  }
  return temp_dir;
}

std::unique_ptr<VPNUtil> VPNUtil::New() {
  return std::make_unique<VPNUtilImpl>();
}

}  // namespace shill
