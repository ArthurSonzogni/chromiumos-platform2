// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_blocks/challenge_credential_auth_block.h"

#include <utility>

#include <absl/types/variant.h>
#include <base/logging.h>
#include <base/notreached.h>

#include "cryptohome/auth_blocks/auth_block_state.h"
#include "cryptohome/auth_blocks/libscrypt_compat_auth_block.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/key_objects.h"

namespace cryptohome {

ChallengeCredentialAuthBlock::ChallengeCredentialAuthBlock()
    : LibScryptCompatAuthBlock(kSignatureChallengeProtected) {}

CryptoError ChallengeCredentialAuthBlock::Create(
    const AuthInput& user_input,
    AuthBlockState* auth_block_state,
    KeyBlobs* key_blobs) {
  AuthBlockState auth_state;
  CryptoError error =
      LibScryptCompatAuthBlock::Create(user_input, &auth_state, key_blobs);
  if (error != CryptoError::CE_NONE) {
    LOG(ERROR) << "scrypt derivation failed for challenge credential";
    return error;
  }
  if (auto* scrypt_state =
          absl::get_if<LibScryptCompatAuthBlockState>(&auth_state.state)) {
    ChallengeCredentialAuthBlockState cc_state = {.scrypt_state =
                                                      std::move(*scrypt_state)};
    *auth_block_state = AuthBlockState{.state = std::move(cc_state)};
    return CryptoError::CE_NONE;

  } else {
    // This should never happen, but handling it anyway on the safe side.
    NOTREACHED() << "scrypt derivation failed for challenge credential";
    return CryptoError::CE_OTHER_CRYPTO;
  }
}

CryptoError ChallengeCredentialAuthBlock::Derive(const AuthInput& user_input,
                                                 const AuthBlockState& state,
                                                 KeyBlobs* key_blobs) {
  const ChallengeCredentialAuthBlockState* cc_state =
      absl::get_if<ChallengeCredentialAuthBlockState>(&state.state);
  if (cc_state == nullptr) {
    LOG(ERROR) << "Invalid state for challenge credential AuthBlock";
    return CryptoError::CE_OTHER_FATAL;
  }

  AuthBlockState scrypt_state = {.state = cc_state->scrypt_state};
  return LibScryptCompatAuthBlock::Derive(user_input, scrypt_state, key_blobs);
}

}  // namespace cryptohome
