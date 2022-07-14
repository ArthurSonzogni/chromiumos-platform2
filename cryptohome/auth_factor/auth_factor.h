// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_FACTOR_AUTH_FACTOR_H_
#define CRYPTOHOME_AUTH_FACTOR_AUTH_FACTOR_H_

#include <memory>
#include <optional>
#include <string>

#include <libhwsec-foundation/status/status_chain_or.h>

#include "cryptohome/auth_blocks/auth_block.h"
#include "cryptohome/auth_blocks/auth_block_state.h"
#include "cryptohome/auth_blocks/auth_block_utility.h"
#include "cryptohome/auth_factor/auth_factor_metadata.h"
#include "cryptohome/auth_factor/auth_factor_type.h"
#include "cryptohome/credentials.h"
#include "cryptohome/error/cryptohome_crypto_error.h"
#include "cryptohome/error/cryptohome_error.h"
#include "cryptohome/storage/file_system_keyset.h"

namespace cryptohome {

// This is an interface designed to be implemented by the different
// authentication factors - password, pin, security keys, etc - so that
// they take handle multiple factors of the same type and know what to do with
// it.
class AuthFactor {
 public:
  AuthFactor(AuthFactorType type,
             const std::string& label,
             const AuthFactorMetadata& metadata,
             const AuthBlockState& auth_block_state);

  ~AuthFactor() = default;

  const AuthFactorType& type() const { return type_; }
  const std::string& label() const { return label_; }
  const AuthFactorMetadata& metadata() const { return metadata_; }
  const AuthBlockState& auth_block_state() const { return auth_block_state_; }

  // Authenticates and derives key blobs.
  // `auth_block_utility` is not owned and only needs to stay valid throughout
  // this call.
  CryptoStatus Authenticate(const AuthInput& auth_input,
                            AuthBlockUtility* auth_block_utility,
                            KeyBlobs& out_key_blobs,
                            AuthBlockType& out_auth_block_type);

  // Executes additional steps needed for auth factor removal before it's
  // deleted from disk.
  CryptoStatus PrepareForRemoval(AuthBlockUtility* auth_block_utility);

 private:
  // The auth factor public information.
  const AuthFactorType type_;
  const std::string label_;
  const AuthFactorMetadata metadata_;
  // Contains the data that the auth factor needs for deriving the secret.
  const AuthBlockState auth_block_state_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_FACTOR_AUTH_FACTOR_H_
