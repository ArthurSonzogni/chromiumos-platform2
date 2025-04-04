// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_blocks/double_wrapped_compat_auth_block.h"

#include <memory>
#include <optional>
#include <utility>

#include <base/check.h>
#include <base/logging.h>

#include "cryptohome/auth_blocks/scrypt_auth_block.h"
#include "cryptohome/auth_blocks/tpm_not_bound_to_pcr_auth_block.h"
#include "cryptohome/crypto.h"
#include "cryptohome/cryptohome_keys_manager.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/error/action.h"
#include "cryptohome/error/locations.h"
#include "cryptohome/features.h"
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
    AsyncInitFeatures& features,
    const hwsec::CryptohomeFrontend& hwsec,
    CryptohomeKeysManager& cryptohome_keys_manager) {
  return std::make_unique<DoubleWrappedCompatAuthBlock>(
      features, hwsec, cryptohome_keys_manager);
}

DoubleWrappedCompatAuthBlock::DoubleWrappedCompatAuthBlock(
    AsyncInitFeatures& features,
    const hwsec::CryptohomeFrontend& hwsec,
    CryptohomeKeysManager& cryptohome_keys_manager)
    : NonPinweaverPasswordAuthBlock(kDoubleWrapped, features, hwsec),
      tpm_auth_block_(features, hwsec, cryptohome_keys_manager) {}

void DoubleWrappedCompatAuthBlock::Create(
    const AuthInput& user_input,
    const AuthFactorMetadata& auth_factor_metadata,
    CreateCallback callback) {
  LOG(FATAL) << "Cannot create a keyset wrapped with both scrypt and TPM.";
}

void DoubleWrappedCompatAuthBlock::DerivePassword(
    const AuthInput& user_input,
    const AuthFactorMetadata& auth_factor_metadata,
    const AuthBlockState& state,
    DeriveCallback callback) {
  const DoubleWrappedCompatAuthBlockState* auth_state;
  if (!(auth_state =
            std::get_if<DoubleWrappedCompatAuthBlockState>(&state.state))) {
    DLOG(FATAL) << "Invalid AuthBlockState";
    std::move(callback).Run(
        MakeStatus<CryptohomeCryptoError>(
            CRYPTOHOME_ERR_LOC(
                kLocDoubleWrappedAuthBlockInvalidBlockStateInDerive),
            ErrorActionSet({PossibleAction::kDevCheckUnexpectedState,
                            PossibleAction::kAuth}),
            CryptoError::CE_OTHER_CRYPTO),
        nullptr, std::nullopt);
    return;
  }

  AuthBlockState scrypt_state = {.state = auth_state->scrypt_state};
  scrypt_auth_block_.Derive(
      user_input, auth_factor_metadata, scrypt_state,
      base::BindOnce(&DoubleWrappedCompatAuthBlock::CreateDeriveAfterScrypt,
                     weak_factory_.GetWeakPtr(), std::move(callback),
                     user_input, auth_factor_metadata, std::move(state)));
}

void DoubleWrappedCompatAuthBlock::CreateDeriveAfterScrypt(
    DeriveCallback callback,
    const AuthInput& user_input,
    const AuthFactorMetadata& auth_factor_metadata,
    const AuthBlockState& state,
    CryptohomeStatus error,
    std::unique_ptr<KeyBlobs> key_blobs,
    std::optional<SuggestedAction> suggested_action) {
  if (error.ok()) {
    std::move(callback).Run(std::move(error), std::move(key_blobs),
                            std::move(suggested_action));
    return;
  }
  const DoubleWrappedCompatAuthBlockState* auth_state;
  if (!(auth_state =
            std::get_if<DoubleWrappedCompatAuthBlockState>(&state.state))) {
    DLOG(FATAL) << "Invalid AuthBlockState";
    std::move(callback).Run(
        MakeStatus<CryptohomeCryptoError>(
            CRYPTOHOME_ERR_LOC(
                kLocDoubleWrappedAuthBlockInvalidBlockStateInAfterScrypt),
            ErrorActionSet({PossibleAction::kDevCheckUnexpectedState,
                            PossibleAction::kAuth}),
            CryptoError::CE_OTHER_CRYPTO),
        nullptr, std::nullopt);
    return;
  }

  AuthBlockState tpm_state = {.state = auth_state->tpm_state};
  tpm_auth_block_.Derive(
      user_input, auth_factor_metadata, tpm_state,
      base::BindOnce(&DoubleWrappedCompatAuthBlock::CreateDeriveAfterTpm,
                     weak_factory_.GetWeakPtr(), std::move(callback)));
}

void DoubleWrappedCompatAuthBlock::CreateDeriveAfterTpm(
    DeriveCallback callback,
    CryptohomeStatus error,
    std::unique_ptr<KeyBlobs> key_blobs,
    std::optional<SuggestedAction> suggested_action) {
  if (error.ok()) {
    std::move(callback).Run(std::move(error), std::move(key_blobs),
                            std::move(suggested_action));
    return;
  }
  std::move(callback).Run(
      MakeStatus<error::CryptohomeError>(
          CRYPTOHOME_ERR_LOC(kLocDoubleWrappedAuthBlockTpmDeriveFailedInDerive))
          .Wrap(std::move(error)),
      nullptr, std::nullopt);
}

}  // namespace cryptohome
