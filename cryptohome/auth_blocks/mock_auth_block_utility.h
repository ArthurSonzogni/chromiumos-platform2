// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_BLOCKS_MOCK_AUTH_BLOCK_UTILITY_H_
#define CRYPTOHOME_AUTH_BLOCKS_MOCK_AUTH_BLOCK_UTILITY_H_

#include "cryptohome/auth_blocks/auth_block_utility.h"

#include <memory>
#include <string>
#include <utility>

#include <brillo/secure_blob.h>
#include <gmock/gmock.h>

#include "cryptohome/auth_blocks/auth_block_state.h"
#include "cryptohome/auth_factor/auth_factor_type.h"
#include "cryptohome/credentials.h"
#include "cryptohome/crypto_error.h"
#include "cryptohome/key_objects.h"

namespace cryptohome {

class MockAuthBlockUtility : public AuthBlockUtility {
 public:
  MockAuthBlockUtility() = default;
  ~MockAuthBlockUtility() = default;

  MOCK_METHOD(CryptoError,
              CreateKeyBlobsWithAuthBlock,
              (AuthBlockType auth_block_type,
               const Credentials& credentials,
               const std::optional<brillo::SecureBlob>& reset_secret,
               AuthBlockState& out_state,
               KeyBlobs& out_key_blobs),
              (override));
  MOCK_METHOD(CryptoError,
              DeriveKeyBlobsWithAuthBlock,
              (AuthBlockType auth_block_type,
               const Credentials& credentials,
               const AuthBlockState& state,
               KeyBlobs& out_key_blobs),
              (override));
  MOCK_METHOD(AuthBlockType,
              GetAuthBlockTypeForCreation,
              (const Credentials& credentials),
              (const, override));
  MOCK_METHOD(AuthBlockType,
              GetAuthBlockTypeForDerivation,
              (const Credentials& credentials),
              (const, override));
  MOCK_METHOD(bool,
              GetAuthBlockStateFromVaultKeyset,
              (const Credentials& credentials, AuthBlockState& out_state),
              (override));
  MOCK_METHOD(void,
              AssignAuthBlockStateToVaultKeyset,
              (const AuthBlockState& state, VaultKeyset& vault_keyset),
              (override));
  MOCK_METHOD(CryptoError,
              CreateKeyBlobsWithAuthFactorType,
              (AuthFactorType auth_factor_type,
               const AuthInput& auth_input,
               AuthBlockState& out_auth_block_state,
               KeyBlobs& out_key_blobs),
              (override));
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_BLOCKS_MOCK_AUTH_BLOCK_UTILITY_H_
