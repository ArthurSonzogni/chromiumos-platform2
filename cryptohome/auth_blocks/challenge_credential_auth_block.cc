// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_blocks/challenge_credential_auth_block.h"

#include <utility>
#include <variant>

#include <base/logging.h>
#include <base/notreached.h>

#include "cryptohome/auth_blocks/scrypt_auth_block.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/error/location_utils.h"
#include "cryptohome/flatbuffer_schemas/auth_block_state.h"
#include "cryptohome/key_objects.h"

using cryptohome::error::CryptohomeCryptoError;
using cryptohome::error::ErrorAction;
using cryptohome::error::ErrorActionSet;
using hwsec_foundation::status::MakeStatus;
using hwsec_foundation::status::OkStatus;
using hwsec_foundation::status::StatusChain;

namespace cryptohome {

ChallengeCredentialAuthBlock::ChallengeCredentialAuthBlock()
    : ScryptAuthBlock(kSignatureChallengeProtected) {}

CryptoStatus ChallengeCredentialAuthBlock::Create(
    const AuthInput& user_input,
    AuthBlockState* auth_block_state,
    KeyBlobs* key_blobs) {
  AuthBlockState auth_state;
  CryptoStatus error =
      ScryptAuthBlock::Create(user_input, &auth_state, key_blobs);
  if (!error.ok()) {
    LOG(ERROR) << "scrypt derivation failed for challenge credential";
    return MakeStatus<CryptohomeCryptoError>(
               CRYPTOHOME_ERR_LOC(
                   kLocChalCredAuthBlockCreateScryptAuthBlockFailedInCreate))
        .Wrap(std::move(error));
  }
  if (auto* scrypt_state =
          std::get_if<ScryptAuthBlockState>(&auth_state.state)) {
    ChallengeCredentialAuthBlockState cc_state = {.scrypt_state =
                                                      std::move(*scrypt_state)};
    *auth_block_state = AuthBlockState{.state = std::move(cc_state)};
    return OkStatus<CryptohomeCryptoError>();

  } else {
    // This should never happen, but handling it anyway on the safe side.
    NOTREACHED() << "scrypt derivation failed for challenge credential";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocChalCredAuthBlockDerivationFailedInCreate),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        CryptoError::CE_OTHER_CRYPTO);
  }
}

CryptoStatus ChallengeCredentialAuthBlock::Derive(const AuthInput& user_input,
                                                  const AuthBlockState& state,
                                                  KeyBlobs* key_blobs) {
  const ChallengeCredentialAuthBlockState* cc_state =
      std::get_if<ChallengeCredentialAuthBlockState>(&state.state);
  if (cc_state == nullptr) {
    LOG(ERROR) << "Invalid state for challenge credential AuthBlock";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocChalCredAuthBlockInvalidBlockStateInDerive),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        CryptoError::CE_OTHER_FATAL);
  }

  AuthBlockState scrypt_state = {.state = cc_state->scrypt_state};
  CryptoStatus error =
      ScryptAuthBlock::Derive(user_input, scrypt_state, key_blobs);
  if (error.ok())
    return error;
  return MakeStatus<CryptohomeCryptoError>(
             CRYPTOHOME_ERR_LOC(
                 kLocChalCredAuthBlockScryptDeriveFailedInDerive))
      .Wrap(std::move(error));
}

}  // namespace cryptohome
