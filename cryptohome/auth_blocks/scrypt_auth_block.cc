// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_blocks/scrypt_auth_block.h"

#include <memory>
#include <utility>
#include <variant>

#include <base/logging.h>
#include <libhwsec-foundation/crypto/aes.h>
#include <libhwsec-foundation/crypto/libscrypt_compat.h>
#include <libhwsec-foundation/crypto/scrypt.h>
#include <libhwsec-foundation/crypto/secure_blob_util.h>

#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/error/location_utils.h"
#include "cryptohome/flatbuffer_schemas/auth_block_state.h"

using ::cryptohome::error::CryptohomeCryptoError;
using ::cryptohome::error::ErrorAction;
using ::cryptohome::error::ErrorActionSet;
using ::hwsec_foundation::CreateSecureRandomBlob;
using ::hwsec_foundation::kAesBlockSize;
using ::hwsec_foundation::kDefaultAesKeySize;
using ::hwsec_foundation::kDefaultScryptParams;
using ::hwsec_foundation::kLibScryptDerivedKeySize;
using ::hwsec_foundation::kLibScryptSaltSize;
using ::hwsec_foundation::Scrypt;
using ::hwsec_foundation::status::MakeStatus;
using ::hwsec_foundation::status::OkStatus;

namespace cryptohome {

ScryptAuthBlock::ScryptAuthBlock() : SyncAuthBlock(kScryptBacked) {}

ScryptAuthBlock::ScryptAuthBlock(DerivationType derivation_type)
    : SyncAuthBlock(derivation_type) {}

CryptoStatus CreateScryptHelper(const brillo::SecureBlob& input_key,
                                brillo::SecureBlob* out_salt,
                                brillo::SecureBlob* out_derived_key) {
  *out_salt = CreateSecureRandomBlob(kLibScryptSaltSize);

  out_derived_key->resize(kLibScryptDerivedKeySize);
  if (!Scrypt(input_key, *out_salt, kDefaultScryptParams.n_factor,
              kDefaultScryptParams.r_factor, kDefaultScryptParams.p_factor,
              out_derived_key)) {
    LOG(ERROR) << "Scrypt for derived key creation failed.";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocScryptAuthBlockScryptFailedDerivedKeyInCreate),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        CryptoError::CE_SCRYPT_CRYPTO);
  }
  return OkStatus<CryptohomeCryptoError>();
}

CryptoStatus ScryptAuthBlock::Create(const AuthInput& auth_input,
                                     AuthBlockState* auth_block_state,
                                     KeyBlobs* key_blobs) {
  const brillo::SecureBlob input_key = auth_input.user_input.value();

  brillo::SecureBlob salt, derived_key;
  CryptoStatus error = CreateScryptHelper(input_key, &salt, &derived_key);
  if (!error.ok()) {
    return MakeStatus<CryptohomeCryptoError>(
               CRYPTOHOME_ERR_LOC(kLocScryptAuthBlockInputKeyFailedInCreate),
               ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}))
        .Wrap(std::move(error));
  }

  brillo::SecureBlob chaps_salt, derived_scrypt_chaps_key;
  error = CreateScryptHelper(input_key, &chaps_salt, &derived_scrypt_chaps_key);
  if (!error.ok()) {
    return MakeStatus<CryptohomeCryptoError>(
               CRYPTOHOME_ERR_LOC(kLocScryptAuthBlockChapsKeyFailedInCreate),
               ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}))
        .Wrap(std::move(error));
  }

  brillo::SecureBlob reset_seed_salt, derived_scrypt_reset_seed_key;
  error = CreateScryptHelper(input_key, &reset_seed_salt,
                             &derived_scrypt_reset_seed_key);
  if (!error.ok()) {
    return MakeStatus<CryptohomeCryptoError>(
               CRYPTOHOME_ERR_LOC(kLocScryptAuthBlockResetKeyFailedInCreate),
               ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}))
        .Wrap(std::move(error));
  }

  ScryptAuthBlockState scrypt_state{
      .salt = std::move(salt),
      .chaps_salt = std::move(chaps_salt),
      .reset_seed_salt = std::move(reset_seed_salt),
      .work_factor = kDefaultScryptParams.n_factor,
      .block_size = kDefaultScryptParams.r_factor,
      .parallel_factor = kDefaultScryptParams.p_factor,
  };

  key_blobs->vkk_key = std::move(derived_key);
  key_blobs->scrypt_chaps_key = std::move(derived_scrypt_chaps_key);
  key_blobs->scrypt_reset_seed_key = std::move(derived_scrypt_reset_seed_key);

  *auth_block_state = AuthBlockState{.state = std::move(scrypt_state)};
  return OkStatus<CryptohomeCryptoError>();
}

CryptoStatus ScryptAuthBlock::Derive(const AuthInput& auth_input,
                                     const AuthBlockState& auth_state,
                                     KeyBlobs* key_blobs) {
  const ScryptAuthBlockState* state;
  if (!(state = std::get_if<ScryptAuthBlockState>(&auth_state.state))) {
    LOG(ERROR) << "Invalid AuthBlockState";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocScryptAuthBlockInvalidBlockStateInDerive),
        ErrorActionSet(
            {ErrorAction::kDevCheckUnexpectedState, ErrorAction::kAuth}),
        CryptoError::CE_OTHER_CRYPTO);
  }

  if (!state->salt.has_value()) {
    LOG(ERROR) << "Invalid ScryptAuthBlockState: missing salt";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocScryptAuthBlockNoSaltInDerive),
        ErrorActionSet({ErrorAction::kAuth, ErrorAction::kReboot,
                        ErrorAction::kDeleteVault}),
        CryptoError::CE_OTHER_CRYPTO);
  }

  if (!state->work_factor.has_value() || !state->block_size.has_value() ||
      !state->parallel_factor.has_value()) {
    LOG(ERROR) << "Invalid ScryptAuthBlockState: missing N, R, P factors";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocScryptAuthBlockNofactorsInDerive),
        ErrorActionSet({ErrorAction::kAuth, ErrorAction::kReboot,
                        ErrorAction::kDeleteVault}),
        CryptoError::CE_OTHER_CRYPTO);
  }

  const brillo::SecureBlob input_key = auth_input.user_input.value();

  brillo::SecureBlob derived_key(kLibScryptDerivedKeySize);

  if (!Scrypt(input_key, state->salt.value(), state->work_factor.value(),
              state->block_size.value(), state->parallel_factor.value(),
              &derived_key)) {
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocScryptAuthBlockScryptFailedInDeriveFromSalt),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        CryptoError::CE_SCRYPT_CRYPTO);
  }
  key_blobs->vkk_key = std::move(derived_key);

  if (state->chaps_salt.has_value()) {
    brillo::SecureBlob derived_scrypt_chaps_key(kLibScryptDerivedKeySize);
    if (!Scrypt(input_key, state->chaps_salt.value(),
                state->work_factor.value(), state->block_size.value(),
                state->parallel_factor.value(), &derived_scrypt_chaps_key)) {
      return MakeStatus<CryptohomeCryptoError>(
          CRYPTOHOME_ERR_LOC(
              kLocScryptAuthBlockScryptFailedInDeriveFromChapsSalt),
          ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
          CryptoError::CE_SCRYPT_CRYPTO);
    }
    key_blobs->scrypt_chaps_key = std::move(derived_scrypt_chaps_key);
  }

  if (state->reset_seed_salt.has_value()) {
    brillo::SecureBlob derived_scrypt_reset_seed_key(kLibScryptDerivedKeySize);
    if (!Scrypt(input_key, state->reset_seed_salt.value(),
                state->work_factor.value(), state->block_size.value(),
                state->parallel_factor.value(),
                &derived_scrypt_reset_seed_key)) {
      return MakeStatus<CryptohomeCryptoError>(
          CRYPTOHOME_ERR_LOC(
              kLocScryptAuthBlockScryptFailedInDeriveFromResetSeedSalt),
          ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
          CryptoError::CE_SCRYPT_CRYPTO);
    }
    key_blobs->scrypt_reset_seed_key = std::move(derived_scrypt_reset_seed_key);
  }

  return OkStatus<CryptohomeCryptoError>();
}

}  // namespace cryptohome
