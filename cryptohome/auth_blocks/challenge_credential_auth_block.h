// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_BLOCKS_CHALLENGE_CREDENTIAL_AUTH_BLOCK_H_
#define CRYPTOHOME_AUTH_BLOCKS_CHALLENGE_CREDENTIAL_AUTH_BLOCK_H_

#include "cryptohome/auth_blocks/auth_block_state.h"
#include "cryptohome/auth_blocks/libscrypt_compat_auth_block.h"

namespace cryptohome {

class ChallengeCredentialAuthBlock : public LibScryptCompatAuthBlock {
 public:
  ChallengeCredentialAuthBlock();
  ~ChallengeCredentialAuthBlock() = default;

  CryptoError Create(const AuthInput& user_input,
                     AuthBlockState* auth_block_state,
                     KeyBlobs* key_blobs) override;

  // This derives a high entropy secret from the input secret provided by the
  // challenge credential.
  CryptoError Derive(const AuthInput& user_input,
                     const AuthBlockState& state,
                     KeyBlobs* key_blobs) override;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_BLOCKS_CHALLENGE_CREDENTIAL_AUTH_BLOCK_H_
