// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_blocks/double_wrapped_compat_auth_block.h"

#include <memory>
#include <utility>

#include <base/check.h>
#include <base/logging.h>

#include "cryptohome/auth_blocks/auth_block_state.h"
#include "cryptohome/auth_blocks/libscrypt_compat_auth_block.h"
#include "cryptohome/auth_blocks/tpm_not_bound_to_pcr_auth_block.h"
#include "cryptohome/cryptohome_keys_manager.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/error/location_utils.h"
#include "cryptohome/tpm.h"

using cryptohome::error::CryptohomeCryptoError;
using cryptohome::error::ErrorAction;
using cryptohome::error::ErrorActionSet;
using hwsec_foundation::status::MakeStatus;
using hwsec_foundation::status::OkStatus;
using hwsec_foundation::status::StatusChain;

namespace cryptohome {

DoubleWrappedCompatAuthBlock::DoubleWrappedCompatAuthBlock(
    hwsec::CryptohomeFrontend* hwsec,
    CryptohomeKeysManager* cryptohome_keys_manager)
    : SyncAuthBlock(kDoubleWrapped),
      tpm_auth_block_(hwsec, cryptohome_keys_manager),
      lib_scrypt_compat_auth_block_() {}

CryptoStatus DoubleWrappedCompatAuthBlock::Create(
    const AuthInput& user_input,
    AuthBlockState* auth_block_state,
    KeyBlobs* key_blobs) {
  LOG(FATAL) << "Cannot create a keyset wrapped with both scrypt and TPM.";
  return MakeStatus<CryptohomeCryptoError>(
      CRYPTOHOME_ERR_LOC(kLocDoubleWrappedAuthBlockUnsupportedInCreate),
      ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
      CryptoError::CE_OTHER_CRYPTO);
}

CryptoStatus DoubleWrappedCompatAuthBlock::Derive(const AuthInput& auth_input,
                                                  const AuthBlockState& state,
                                                  KeyBlobs* key_blobs) {
  const DoubleWrappedCompatAuthBlockState* auth_state;
  if (!(auth_state =
            std::get_if<DoubleWrappedCompatAuthBlockState>(&state.state))) {
    DLOG(FATAL) << "Invalid AuthBlockState";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocDoubleWrappedAuthBlockInvalidBlockStateInDerive),
        ErrorActionSet(
            {ErrorAction::kDevCheckUnexpectedState, ErrorAction::kAuth}),
        CryptoError::CE_OTHER_CRYPTO);
  }

  AuthBlockState scrypt_state = {.state = auth_state->scrypt_state};
  CryptoStatus error =
      lib_scrypt_compat_auth_block_.Derive(auth_input, scrypt_state, key_blobs);
  if (error.ok()) {
    return OkStatus<CryptohomeCryptoError>();
  }

  AuthBlockState tpm_state = {.state = auth_state->tpm_state};
  error = tpm_auth_block_.Derive(auth_input, tpm_state, key_blobs);
  if (error.ok())
    return OkStatus<CryptohomeCryptoError>();
  return MakeStatus<CryptohomeCryptoError>(
             CRYPTOHOME_ERR_LOC(
                 kLocDoubleWrappedAuthBlockTpmDeriveFailedInDerive))
      .Wrap(std::move(error));
}

}  // namespace cryptohome
