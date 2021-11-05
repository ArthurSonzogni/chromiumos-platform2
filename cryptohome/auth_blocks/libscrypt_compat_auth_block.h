// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_BLOCKS_LIBSCRYPT_COMPAT_AUTH_BLOCK_H_
#define CRYPTOHOME_AUTH_BLOCKS_LIBSCRYPT_COMPAT_AUTH_BLOCK_H_

#include "cryptohome/auth_blocks/auth_block.h"
#include "cryptohome/auth_blocks/auth_block_state.h"

namespace cryptohome {

// AuthBlocks generally output a metadata populated AuthBlockState in the
// Create() method, and consume the same AuthBlockState in the Derive() method.
// LibScryptCompat is a special case because it includes the metadata
// (including salt and scrypt parameters) at the beginning of the same buffer
// as the encrypted blob. Thus, Create() outputs an empty AuthBlockState and
// the KeyBlobs struct stores the scrypt derived keys and salts. When a
// VaultKeyset encrypts itself with LibScryptCompat, wrapped_keyset, along
// with wrapped_chaps_key and wrapped_reset_seed, is an encrypted buffer which
// happen to have embedded the metadata. Before Derive() is called, those
// encryption blobs are put into the AuthBlockState from a VaultKeyset so
// Derive() can parse the metadata from them to derive the same scrypt keys.
class LibScryptCompatAuthBlock : public SyncAuthBlock {
 public:
  LibScryptCompatAuthBlock();
  ~LibScryptCompatAuthBlock() = default;

  // Derives a high entropy secret from the user's password with scrypt.
  // Returns a key for each field that must be wrapped by scrypt, such as the
  // wrapped_chaps_key, etc.
  CryptoError Create(const AuthInput& user_input,
                     AuthBlockState* auth_block_state,

                     KeyBlobs* key_blobs) override;

  // This uses Scrypt to derive high entropy keys from the user's password.
  CryptoError Derive(const AuthInput& auth_input,
                     const AuthBlockState& state,
                     KeyBlobs* key_blobs) override;

 protected:
  explicit LibScryptCompatAuthBlock(DerivationType);
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_BLOCKS_LIBSCRYPT_COMPAT_AUTH_BLOCK_H_
