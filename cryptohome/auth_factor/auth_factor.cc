// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_factor/auth_factor.h"

#include <memory>
#include <utility>

#include <base/logging.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>

#include "cryptohome/auth_blocks/auth_block_utility.h"
#include "cryptohome/auth_blocks/tpm_bound_to_pcr_auth_block.h"
#include "cryptohome/crypto_error.h"
#include "cryptohome/key_objects.h"
#include "cryptohome/scrypt_verifier.h"

namespace cryptohome {

namespace {

user_data_auth::CryptohomeErrorCode CryptoErrorToCryptohomeError(
    CryptoError crypto_error) {
  switch (crypto_error) {
    case CryptoError::CE_TPM_COMM_ERROR:
      return user_data_auth::CRYPTOHOME_ERROR_TPM_COMM_ERROR;
    case CryptoError::CE_TPM_DEFEND_LOCK:
      return user_data_auth::CRYPTOHOME_ERROR_TPM_DEFEND_LOCK;
    case CryptoError::CE_TPM_REBOOT:
      return user_data_auth::CRYPTOHOME_ERROR_TPM_NEEDS_REBOOT;
    default:
      return user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED;
  }
}

}  // namespace

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

user_data_auth::CryptohomeErrorCode AuthFactor::Authenticate(
    const AuthInput& auth_input,
    AuthBlockUtility* auth_block_utility,
    KeyBlobs& out_key_blobs) {
  CryptoError crypto_error = auth_block_utility->DeriveKeyBlobs(
      auth_input, auth_block_state_, out_key_blobs);
  if (crypto_error != CryptoError::CE_NONE) {
    user_data_auth::CryptohomeErrorCode error =
        CryptoErrorToCryptohomeError(crypto_error);
    LOG(ERROR) << "Auth factor authentication failed: error " << error
               << ", crypto error " << error;
    return error;
  }
  return user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
}

}  // namespace cryptohome
