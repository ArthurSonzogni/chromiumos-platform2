// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "init/crossystem_fake.h"

bool CrosSystemFake::GetInt(const std::string& name, int* value_out) const {
  const auto it = int_map_.find(name);
  if (it == int_map_.end())
    return false;
  *value_out = it->second;
  return true;
}

bool CrosSystemFake::SetInt(const std::string& name, int value) {
  int_map_[name] = value;
  return true;
}

bool CrosSystemFake::GetString(const std::string& name,
                               std::string* value_out) const {
  const auto it = string_map_.find(name);
  if (it == string_map_.end())
    return false;
  *value_out = it->second;
  return true;
}

bool CrosSystemFake::SetString(const std::string& name,
                               const std::string& value) {
  string_map_[name] = value;
  return true;
}
