// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_BLOCKS_SCRYPT_AUTH_BLOCK_H_
#define CRYPTOHOME_AUTH_BLOCKS_SCRYPT_AUTH_BLOCK_H_

#include "cryptohome/auth_blocks/auth_block.h"
#include "cryptohome/flatbuffer_schemas/auth_block_state.h"

namespace cryptohome {

// This auth block would generate the standard vkk_key that's
// similar to the other standard auth block.
class ScryptAuthBlock : public SyncAuthBlock {
 public:
  ScryptAuthBlock();
  ~ScryptAuthBlock() = default;

  // Derives a high entropy secret from the user's password with scrypt.
  // Returns a key for each field that must be wrapped by scrypt, such as the
  // wrapped_chaps_key, etc.
  CryptoStatus Create(const AuthInput& user_input,
                      AuthBlockState* auth_block_state,
                      KeyBlobs* key_blobs) override;

  // This uses Scrypt to derive high entropy keys from the user's password.
  CryptoStatus Derive(const AuthInput& auth_input,
                      const AuthBlockState& state,
                      KeyBlobs* key_blobs) override;

 protected:
  explicit ScryptAuthBlock(DerivationType);
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_BLOCKS_SCRYPT_AUTH_BLOCK_H_
