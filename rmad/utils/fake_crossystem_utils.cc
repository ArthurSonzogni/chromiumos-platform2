// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/utils/fake_crossystem_utils.h"

namespace rmad {

bool FakeCrosSystemUtils::SetInt(const std::string& key, int value) {
  int_map_[key] = value;
  return true;
}

bool FakeCrosSystemUtils::GetInt(const std::string& key, int* value) const {
  auto it = int_map_.find(key);
  if (it == int_map_.end()) {
    return false;
  }
  *value = it->second;
  return true;
}

bool FakeCrosSystemUtils::SetString(const std::string& key,
                                    const std::string& value) {
  str_map_[key] = value;
  return true;
}

bool FakeCrosSystemUtils::GetString(const std::string& key,
                                    std::string* value) const {
  auto it = str_map_.find(key);
  if (it == str_map_.end()) {
    return false;
  }
  *value = it->second;
  return true;
}

}  // namespace rmad
