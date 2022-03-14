// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/error/cryptohome_error.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/strings/string_util.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>

namespace cryptohome {

namespace error {

CryptohomeError::CryptohomeError(
    ErrorLocation loc,
    std::set<Action> actions,
    std::optional<user_data_auth::CryptohomeErrorCode> ec)
    : loc_(loc), actions_(actions), ec_(ec) {}

std::string CryptohomeError::ToString() const {
  std::stringstream ss;
  ss << "Loc: " << loc_ << " Actions: (";
  std::vector<std::string> actions_str;
  for (const auto& action : actions_) {
    actions_str.push_back(std::to_string(static_cast<int>(action)));
  }
  ss << base::JoinString(actions_str, ", ") << ")";
  return ss.str();
}

}  // namespace error

}  // namespace cryptohome
