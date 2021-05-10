// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/utils/fake_cbi_utils.h"

#include <map>
#include <string>
#include <vector>

#include <base/check.h>
#include <base/process/launch.h>
#include <base/strings/string_number_conversions.h>

namespace rmad {

bool FakeCbiUtils::SetCbi(int tag, const std::string& value, int set_flag) {
  cbi_map_[tag] = value;
  return true;
}

bool FakeCbiUtils::GetCbi(int tag, std::string* value, int get_flag) const {
  CHECK(value != nullptr);

  auto it = cbi_map_.find(tag);
  if (it == cbi_map_.end()) {
    *value = "";
    return false;
  }
  *value = it->second;
  return true;
}

bool FakeCbiUtils::SetCbi(int tag, uint64_t value, int size, int set_flag) {
  CHECK_GE(size, 1);
  CHECK_LE(size, 8);
  CHECK(size == 8 || 1 << (size * 8) > value);

  cbi_map_[tag] = base::NumberToString(value);
  return true;
}

bool FakeCbiUtils::GetCbi(int tag, uint64_t* value, int get_flag) const {
  CHECK(value != nullptr);

  auto it = cbi_map_.find(tag);
  if (it == cbi_map_.end()) {
    return false;
  }

  base::StringToUint64(it->second, value);
  return true;
}

}  // namespace rmad
