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
                       const std::string& contents) const;
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

std::unique_ptr<VPNUtil> VPNUtil::New() {
  return std::make_unique<VPNUtilImpl>();
}

}  // namespace shill
