// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/challenge_credential_auth_block.h"

#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/key_objects.h"
#include "cryptohome/libscrypt_compat_auth_block.h"

#include <base/logging.h>

namespace cryptohome {

ChallengeCredentialAuthBlock::ChallengeCredentialAuthBlock()
    : LibScryptCompatAuthBlock(kSignatureChallengeProtected) {}

base::Optional<AuthBlockState> ChallengeCredentialAuthBlock::Create(
    const AuthInput& user_input, KeyBlobs* key_blobs, CryptoError* error) {
  auto auth_state =
      LibScryptCompatAuthBlock::Create(user_input, key_blobs, error);
  if (auth_state == base::nullopt) {
    LOG(ERROR) << "scrypt derivation failed for challenge credential";
    return base::nullopt;
  }
  AuthBlockState final_state;
  *(final_state.mutable_challenge_credential_state()->mutable_scrypt_state()) =
      auth_state->libscrypt_compat_state();
  return final_state;
}

bool ChallengeCredentialAuthBlock::Derive(const AuthInput& user_input,
                                          const AuthBlockState& state,
                                          KeyBlobs* key_blobs,
                                          CryptoError* error) {
  if (!state.has_challenge_credential_state()) {
    LOG(ERROR) << "Invalid state for challenge credential AuthBlock";
    *error = CryptoError::CE_OTHER_FATAL;
    return false;
  }

  AuthBlockState scrypt_state;
  *(scrypt_state.mutable_libscrypt_compat_state()) =
      state.challenge_credential_state().scrypt_state();
  return LibScryptCompatAuthBlock::Derive(user_input, scrypt_state, key_blobs,
                                          error);
}

}  // namespace cryptohome
