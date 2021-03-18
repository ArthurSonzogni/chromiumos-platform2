// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/utils/fake_vpd_utils.h"

#include <map>
#include <string>
#include <vector>

#include <base/check.h>
#include <base/process/launch.h>

namespace rmad {

bool FakeVpdUtils::SetRoVpd(const std::string& key, const std::string& value) {
  ro_map_[key] = value;
  return true;
}

bool FakeVpdUtils::GetRoVpd(const std::string& key, std::string* value) const {
  auto it = ro_map_.find(key);
  if (it == ro_map_.end()) {
    *value = "";
    return false;
  }
  *value = it->second;
  return true;
}

bool FakeVpdUtils::SetRwVpd(const std::string& key, const std::string& value) {
  rw_map_[key] = value;
  return true;
}

bool FakeVpdUtils::GetRwVpd(const std::string& key, std::string* value) const {
  auto it = rw_map_.find(key);
  if (it == rw_map_.end()) {
    *value = "";
    return false;
  }
  *value = it->second;
  return true;
}

}  // namespace rmad
