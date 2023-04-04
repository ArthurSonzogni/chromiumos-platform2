// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_blocks/double_wrapped_compat_auth_block.h"

#include <memory>
#include <utility>

#include <base/check.h>
#include <base/logging.h>

#include "cryptohome/auth_blocks/scrypt_auth_block.h"
#include "cryptohome/auth_blocks/sync_to_async_auth_block_adapter.h"
#include "cryptohome/auth_blocks/tpm_not_bound_to_pcr_auth_block.h"
#include "cryptohome/crypto.h"
#include "cryptohome/cryptohome_keys_manager.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/error/action.h"
#include "cryptohome/error/location_utils.h"
#include "cryptohome/error/locations.h"
#include "cryptohome/flatbuffer_schemas/auth_block_state.h"

using cryptohome::error::CryptohomeCryptoError;
using cryptohome::error::ErrorActionSet;
using cryptohome::error::PossibleAction;
using cryptohome::error::PrimaryAction;
using hwsec_foundation::status::MakeStatus;
using hwsec_foundation::status::OkStatus;
using hwsec_foundation::status::StatusChain;

namespace cryptohome {

CryptoStatus DoubleWrappedCompatAuthBlock::IsSupported(Crypto& crypto) {
  // Simply delegate to the encapsulated classes. Note that `ScryptAuthBlock`
  // has no `IsSupported()` method - it's always supported for us.
  CryptoStatus tpm_status = TpmNotBoundToPcrAuthBlock::IsSupported(crypto);
  if (!tpm_status.ok()) {
    return MakeStatus<CryptohomeCryptoError>(
               CRYPTOHOME_ERR_LOC(
                   kLocDoubleWrappedAuthBlockTpmBlockErrorInIsSupported))
        .Wrap(std::move(tpm_status));
  }
  return OkStatus<CryptohomeCryptoError>();
}

std::unique_ptr<AuthBlock> DoubleWrappedCompatAuthBlock::New(
    hwsec::CryptohomeFrontend& hwsec,
    CryptohomeKeysManager& cryptohome_keys_manager) {
  return std::make_unique<SyncToAsyncAuthBlockAdapter>(
      std::make_unique<DoubleWrappedCompatAuthBlock>(&hwsec,
                                                     &cryptohome_keys_manager));
}

DoubleWrappedCompatAuthBlock::DoubleWrappedCompatAuthBlock(
    hwsec::CryptohomeFrontend* hwsec,
    CryptohomeKeysManager* cryptohome_keys_manager)
    : SyncAuthBlock(kDoubleWrapped),
      tpm_auth_block_(hwsec, cryptohome_keys_manager) {}

CryptoStatus DoubleWrappedCompatAuthBlock::Create(
    const AuthInput& user_input,
    AuthBlockState* auth_block_state,
    KeyBlobs* key_blobs) {
  LOG(FATAL) << "Cannot create a keyset wrapped with both scrypt and TPM.";
  return MakeStatus<CryptohomeCryptoError>(
      CRYPTOHOME_ERR_LOC(kLocDoubleWrappedAuthBlockUnsupportedInCreate),
      ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
      CryptoError::CE_OTHER_CRYPTO);
}

CryptoStatus DoubleWrappedCompatAuthBlock::Derive(
    const AuthInput& auth_input,
    const AuthBlockState& state,
    KeyBlobs* key_blobs,
    std::optional<AuthBlock::SuggestedAction>* suggested_action) {
  const DoubleWrappedCompatAuthBlockState* auth_state;
  if (!(auth_state =
            std::get_if<DoubleWrappedCompatAuthBlockState>(&state.state))) {
    DLOG(FATAL) << "Invalid AuthBlockState";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocDoubleWrappedAuthBlockInvalidBlockStateInDerive),
        ErrorActionSet(
            {PossibleAction::kDevCheckUnexpectedState, PossibleAction::kAuth}),
        CryptoError::CE_OTHER_CRYPTO);
  }

  AuthBlockState scrypt_state = {.state = auth_state->scrypt_state};
  CryptoStatus error = scrypt_auth_block_.Derive(auth_input, scrypt_state,
                                                 key_blobs, suggested_action);
  if (error.ok()) {
    return OkStatus<CryptohomeCryptoError>();
  }

  AuthBlockState tpm_state = {.state = auth_state->tpm_state};
  error = tpm_auth_block_.Derive(auth_input, tpm_state, key_blobs,
                                 suggested_action);
  if (error.ok())
    return OkStatus<CryptohomeCryptoError>();
  return MakeStatus<CryptohomeCryptoError>(
             CRYPTOHOME_ERR_LOC(
                 kLocDoubleWrappedAuthBlockTpmDeriveFailedInDerive))
      .Wrap(std::move(error));
}

}  // namespace cryptohome
