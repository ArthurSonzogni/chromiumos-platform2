// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_factor/auth_factor_map.h"

#include <string>
#include <utility>

#include "cryptohome/auth_factor/auth_factor.h"

namespace cryptohome {

void AuthFactorMap::Add(std::unique_ptr<AuthFactor> auth_factor) {
  std::string label = auth_factor->label();
  storage_[std::move(label)] = std::move(auth_factor);
}

void AuthFactorMap::Remove(const std::string& label) {
  storage_.erase(label);
}

AuthFactor* AuthFactorMap::Find(const std::string& label) {
  auto iter = storage_.find(label);
  if (iter == storage_.end()) {
    return nullptr;
  }
  return iter->second.get();
}

const AuthFactor* AuthFactorMap::Find(const std::string& label) const {
  auto iter = storage_.find(label);
  if (iter == storage_.end()) {
    return nullptr;
  }
  return iter->second.get();
}

}  // namespace cryptohome
