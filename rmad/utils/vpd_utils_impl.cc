// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/utils/vpd_utils_impl.h"

#include <string>
#include <vector>

#include <base/process/launch.h>

namespace rmad {

const char kVpdCmdPath[] = "/usr/sbin/vpd";

bool VpdUtilsImpl::SetRoVpd(const std::string& key, const std::string& value) {
  std::vector<std::string> argv{kVpdCmdPath, "-i", "RO_VPD", "-s",
                                key + "=" + value};
  static std::string unused_output;
  return base::GetAppOutput(argv, &unused_output);
}

bool VpdUtilsImpl::GetRoVpd(const std::string& key, std::string* value) const {
  std::vector<std::string> argv{kVpdCmdPath, "-i", "RO_VPD", "-g", key};
  return base::GetAppOutput(argv, value);
}

bool VpdUtilsImpl::SetRwVpd(const std::string& key, const std::string& value) {
  std::vector<std::string> argv{kVpdCmdPath, "-i", "RW_VPD", "-s",
                                key + "=" + value};
  static std::string unused_output;
  return base::GetAppOutput(argv, &unused_output);
}

bool VpdUtilsImpl::GetRwVpd(const std::string& key, std::string* value) const {
  std::vector<std::string> argv{kVpdCmdPath, "-i", "RW_VPD", "-g", key};
  return base::GetAppOutput(argv, value);
}

}  // namespace rmad
