// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_factor/auth_factor.h"

#include <memory>
#include <utility>

#include <base/logging.h>

#include "cryptohome/auth_blocks/auth_block_utility.h"
#include "cryptohome/auth_blocks/tpm_bound_to_pcr_auth_block.h"
#include "cryptohome/crypto_error.h"
#include "cryptohome/key_objects.h"
#include "cryptohome/scrypt_verifier.h"

namespace cryptohome {

// static
std::unique_ptr<AuthFactor> AuthFactor::CreateNew(
    AuthFactorType type,
    const std::string& label,
    const AuthFactorMetadata& metadata,
    const AuthInput& auth_input,
    AuthBlockUtility* auth_block_utility,
    KeyBlobs& out_key_blobs) {
  AuthBlockState auth_block_state;
  CryptoError error = auth_block_utility->CreateKeyBlobsWithAuthFactorType(
      type, auth_input, auth_block_state, out_key_blobs);
  if (error != CryptoError::CE_NONE) {
    LOG(ERROR) << "Auth block creation failed for new auth factor";
    return nullptr;
  }
  return std::make_unique<AuthFactor>(type, label, metadata, auth_block_state);
}

AuthFactor::AuthFactor(AuthFactorType type,
                       const std::string& label,
                       const AuthFactorMetadata& metadata,
                       const AuthBlockState& auth_block_state)
    : type_(type),
      label_(label),
      metadata_(metadata),
      auth_block_state_(auth_block_state) {}

}  // namespace cryptohome
