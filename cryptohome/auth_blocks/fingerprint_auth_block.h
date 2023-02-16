// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_BLOCKS_FINGERPRINT_AUTH_BLOCK_H_
#define CRYPTOHOME_AUTH_BLOCKS_FINGERPRINT_AUTH_BLOCK_H_

#include "cryptohome/auth_blocks/auth_block.h"
#include "cryptohome/crypto.h"
#include "cryptohome/error/cryptohome_crypto_error.h"
#include "cryptohome/flatbuffer_schemas/auth_block_state.h"

namespace cryptohome {

class FingerprintAuthBlock : public AuthBlock {
 public:
  // Returns success if the AuthBlock is supported on the current hardware and
  // software environment.
  static CryptoStatus IsSupported(Crypto& crypto);

  FingerprintAuthBlock();
  FingerprintAuthBlock(const FingerprintAuthBlock&) = delete;
  FingerprintAuthBlock& operator=(const FingerprintAuthBlock&) = delete;

  void Create(const AuthInput& auth_input, CreateCallback callback) override;

  void Derive(const AuthInput& auth_input,
              const AuthBlockState& state,
              DeriveCallback callback) override;

  CryptohomeStatus PrepareForRemoval(const AuthBlockState& state) override;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_BLOCKS_FINGERPRINT_AUTH_BLOCK_H_
