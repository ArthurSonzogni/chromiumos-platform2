// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/challenge_credential_auth_block.h"

#include <utility>

#include <absl/types/variant.h>
#include <base/logging.h>
#include <base/notreached.h>

#include "cryptohome/auth_block_state.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/key_objects.h"
#include "cryptohome/libscrypt_compat_auth_block.h"

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
  if (auto* scrypt_state =
          absl::get_if<LibScryptCompatAuthBlockState>(&auth_state->state)) {
    ChallengeCredentialAuthBlockState cc_state = {.scrypt_state =
                                                      std::move(*scrypt_state)};
    AuthBlockState final_state = {.state = std::move(cc_state)};
    return final_state;

  } else {
    // This should never happen, but handling it anyway on the safe side.
    NOTREACHED() << "scrypt derivation failed for challenge credential";
    return base::nullopt;
  }
}

bool ChallengeCredentialAuthBlock::Derive(const AuthInput& user_input,
                                          const AuthBlockState& state,
                                          KeyBlobs* key_blobs,
                                          CryptoError* error) {
  const ChallengeCredentialAuthBlockState* cc_state =
      absl::get_if<ChallengeCredentialAuthBlockState>(&state.state);
  if (cc_state == nullptr) {
    LOG(ERROR) << "Invalid state for challenge credential AuthBlock";
    *error = CryptoError::CE_OTHER_FATAL;
    return false;
  }

  AuthBlockState scrypt_state = {.state = cc_state->scrypt_state};
  return LibScryptCompatAuthBlock::Derive(user_input, scrypt_state, key_blobs,
                                          error);
}

}  // namespace cryptohome
