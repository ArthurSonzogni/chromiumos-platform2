// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_FACTOR_AUTH_FACTOR_H_
#define CRYPTOHOME_AUTH_FACTOR_AUTH_FACTOR_H_

#include <memory>
#include <optional>
#include <string>

#include <cryptohome/proto_bindings/rpc.pb.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>

#include "cryptohome/auth_blocks/auth_block.h"
#include "cryptohome/auth_blocks/auth_block_state.h"
#include "cryptohome/auth_factor/auth_factor_metadata.h"
#include "cryptohome/auth_factor/auth_factor_type.h"
#include "cryptohome/credentials.h"
#include "cryptohome/storage/file_system_keyset.h"

namespace cryptohome {

// This is an interface designed to be implemented by the different
// authentication factors - password, pin, security keys, etc - so that
// they take handle multiple factors of the same type and know what to do with
// it.
class AuthFactor {
 public:
  // Only for testing currently.
  AuthFactor(AuthFactorType type,
             const std::string& label,
             const AuthFactorMetadata& metadata,
             const AuthBlockState& auth_block_state);

  ~AuthFactor() = default;

  const std::optional<AuthFactorType>& type() const { return type_; }
  const std::optional<std::string>& label() const { return label_; }
  const std::optional<AuthFactorMetadata>& metadata() const {
    return metadata_;
  }
  const std::optional<AuthBlockState>& auth_block_state() const {
    return auth_block_state_;
  }

 private:
  // The auth factor public information. TODO(b:208351356): Make these
  // non-optional by implementing vault keyset conversion into these fields.
  const std::optional<AuthFactorType> type_;
  const std::optional<std::string> label_;
  const std::optional<AuthFactorMetadata> metadata_;
  // Contains the data that the auth factor needs for deriving the secret.
  std::optional<AuthBlockState> auth_block_state_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_FACTOR_AUTH_FACTOR_H_
