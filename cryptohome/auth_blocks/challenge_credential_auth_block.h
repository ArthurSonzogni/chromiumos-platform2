// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_BLOCKS_CHALLENGE_CREDENTIAL_AUTH_BLOCK_H_
#define CRYPTOHOME_AUTH_BLOCKS_CHALLENGE_CREDENTIAL_AUTH_BLOCK_H_

#include "cryptohome/auth_blocks/scrypt_auth_block.h"
#include "cryptohome/flatbuffer_schemas/auth_block_state.h"

namespace cryptohome {

class ChallengeCredentialAuthBlock : public ScryptAuthBlock {
 public:
  ChallengeCredentialAuthBlock();
  ~ChallengeCredentialAuthBlock() = default;

  CryptoStatus Create(const AuthInput& user_input,
                      AuthBlockState* auth_block_state,
                      KeyBlobs* key_blobs) override;

  // This derives a high entropy secret from the input secret provided by the
  // challenge credential.
  CryptoStatus Derive(const AuthInput& user_input,
                      const AuthBlockState& state,
                      KeyBlobs* key_blobs) override;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_BLOCKS_CHALLENGE_CREDENTIAL_AUTH_BLOCK_H_
