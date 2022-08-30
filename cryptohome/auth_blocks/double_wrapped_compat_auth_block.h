// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_BLOCKS_DOUBLE_WRAPPED_COMPAT_AUTH_BLOCK_H_
#define CRYPTOHOME_AUTH_BLOCKS_DOUBLE_WRAPPED_COMPAT_AUTH_BLOCK_H_

#include "cryptohome/auth_blocks/auth_block.h"

#include <memory>

#include <base/gtest_prod_util.h>

#include "cryptohome/auth_blocks/scrypt_auth_block.h"
#include "cryptohome/auth_blocks/tpm_not_bound_to_pcr_auth_block.h"
#include "cryptohome/flatbuffer_schemas/auth_block_state.h"

namespace cryptohome {

class CryptohomeKeysManager;

class DoubleWrappedCompatAuthBlock : public SyncAuthBlock {
 public:
  DoubleWrappedCompatAuthBlock(hwsec::CryptohomeFrontend* hwsec,
                               CryptohomeKeysManager* cryptohome_keys_manager);
  DoubleWrappedCompatAuthBlock(const DoubleWrappedCompatAuthBlock&) = delete;
  DoubleWrappedCompatAuthBlock& operator=(const DoubleWrappedCompatAuthBlock&) =
      delete;

  // This auth block represents legacy keysets left in an inconsistent state, so
  // calling Create() here is FATAL.
  CryptoStatus Create(const AuthInput& user_input,
                      AuthBlockState* auth_block_state,
                      KeyBlobs* key_blobs) override;

  // First tries to derive the keys with scrypt, and falls back to the TPM.
  CryptoStatus Derive(const AuthInput& auth_input,
                      const AuthBlockState& state,
                      KeyBlobs* key_blobs) override;

 private:
  TpmNotBoundToPcrAuthBlock tpm_auth_block_;
  ScryptAuthBlock scrypt_auth_block_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_BLOCKS_DOUBLE_WRAPPED_COMPAT_AUTH_BLOCK_H_
