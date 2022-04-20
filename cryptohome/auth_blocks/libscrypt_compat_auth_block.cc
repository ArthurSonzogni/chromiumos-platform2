// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_blocks/libscrypt_compat_auth_block.h"

#include <memory>
#include <utility>
#include <variant>

#include <base/logging.h>
#include <libhwsec-foundation/crypto/libscrypt_compat.h>
#include <libhwsec-foundation/crypto/scrypt.h>
#include <libhwsec-foundation/crypto/secure_blob_util.h>

#include "cryptohome/auth_blocks/auth_block_state.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/error/location_utils.h"
#include "cryptohome/key_objects.h"

using ::cryptohome::error::CryptohomeCryptoError;
using ::cryptohome::error::ErrorAction;
using ::cryptohome::error::ErrorActionSet;
using ::hwsec_foundation::CreateSecureRandomBlob;
using ::hwsec_foundation::kDefaultScryptParams;
using ::hwsec_foundation::kLibScryptDerivedKeySize;
using ::hwsec_foundation::kLibScryptSaltSize;
using ::hwsec_foundation::LibScryptCompat;
using ::hwsec_foundation::Scrypt;
using ::hwsec_foundation::status::MakeStatus;
using ::hwsec_foundation::status::OkStatus;
using ::hwsec_foundation::status::StatusChain;

namespace cryptohome {

namespace {

CryptoStatus CreateScryptHelper(const brillo::SecureBlob& input_key,
                                brillo::SecureBlob* salt,
                                brillo::SecureBlob* derived_key) {
  // Because of the implementation peculiarity of libscrypt, the salt MUST be
  // unique for each key, and the same key can never be repurposed.
  *salt = CreateSecureRandomBlob(kLibScryptSaltSize);

  derived_key->resize(kLibScryptDerivedKeySize);
  if (!Scrypt(input_key, *salt, kDefaultScryptParams.n_factor,
              kDefaultScryptParams.r_factor, kDefaultScryptParams.p_factor,
              derived_key)) {
    LOG(ERROR) << "scrypt failed";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocScryptCompatAuthBlockScryptFailedInCreateHelper),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        CryptoError::CE_SCRYPT_CRYPTO);
  }

  return OkStatus<CryptohomeCryptoError>();
}

CryptoStatus ParseHeaderAndDerive(const brillo::SecureBlob& wrapped_blob,
                                  const brillo::SecureBlob& input_key,
                                  brillo::SecureBlob* derived_key) {
  hwsec_foundation::ScryptParameters params;
  brillo::SecureBlob salt;
  if (!LibScryptCompat::ParseHeader(wrapped_blob, &params, &salt)) {
    LOG(ERROR) << "Failed to parse header.";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocScryptCompatAuthBlockParseFailedInParseHeader),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState,
                        ErrorAction::kAuth, ErrorAction::kDeleteVault}),
        CryptoError::CE_SCRYPT_CRYPTO);
  }

  // Generate the derived key.
  derived_key->resize(kLibScryptDerivedKeySize);
  if (!Scrypt(input_key, salt, params.n_factor, params.r_factor,
              params.p_factor, derived_key)) {
    LOG(ERROR) << "scrypt failed";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocScryptCompatAuthBlockScryptFailedInParseHeader),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        CryptoError::CE_SCRYPT_CRYPTO);
  }

  return OkStatus<CryptohomeCryptoError>();
}

}  // namespace

LibScryptCompatAuthBlock::LibScryptCompatAuthBlock()
    : SyncAuthBlock(kScryptBacked) {}

LibScryptCompatAuthBlock::LibScryptCompatAuthBlock(
    DerivationType derivation_type)
    : SyncAuthBlock(derivation_type) {}

CryptoStatus LibScryptCompatAuthBlock::Create(const AuthInput& auth_input,
                                              AuthBlockState* auth_block_state,
                                              KeyBlobs* key_blobs) {
  const brillo::SecureBlob input_key = auth_input.user_input.value();

  brillo::SecureBlob derived_key;
  brillo::SecureBlob salt;
  CryptoStatus error = CreateScryptHelper(input_key, &salt, &derived_key);
  if (!error.ok()) {
    return MakeStatus<CryptohomeCryptoError>(
               CRYPTOHOME_ERR_LOC(
                   kLocScryptCompatAuthBlockInputKeyFailedInCreate),
               ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}))
        .Wrap(std::move(error));
  }

  key_blobs->scrypt_key =
      std::make_unique<LibScryptCompatKeyObjects>(derived_key, salt);

  brillo::SecureBlob derived_chaps_key;
  brillo::SecureBlob chaps_salt;
  error = CreateScryptHelper(input_key, &chaps_salt, &derived_chaps_key);
  if (!error.ok()) {
    return MakeStatus<CryptohomeCryptoError>(
               CRYPTOHOME_ERR_LOC(
                   kLocScryptCompatAuthBlockChapsKeyFailedInCreate),
               ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}))
        .Wrap(std::move(error));
  }

  key_blobs->chaps_scrypt_key = std::make_unique<LibScryptCompatKeyObjects>(
      derived_chaps_key, chaps_salt);

  brillo::SecureBlob derived_reset_seed_key;
  brillo::SecureBlob reset_seed_salt;
  error =
      CreateScryptHelper(input_key, &reset_seed_salt, &derived_reset_seed_key);
  if (!error.ok()) {
    return MakeStatus<CryptohomeCryptoError>(
               CRYPTOHOME_ERR_LOC(
                   kLocScryptCompatAuthBlockResetKeyFailedInCreate),
               ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}))
        .Wrap(std::move(error));
  }

  key_blobs->scrypt_wrapped_reset_seed_key =
      std::make_unique<LibScryptCompatKeyObjects>(derived_reset_seed_key,
                                                  reset_seed_salt);

  // libscrypt is an odd case again; the AuthBlockState is only populated on the
  // derivation flow. See the class header for a full explanation.
  LibScryptCompatAuthBlockState scrypt_state;

  // TODO(b/198394243): We should remove this because it's not actually used.
  scrypt_state.salt = CreateSecureRandomBlob(CRYPTOHOME_DEFAULT_KEY_SALT_SIZE);

  *auth_block_state = AuthBlockState{.state = std::move(scrypt_state)};
  return OkStatus<CryptohomeCryptoError>();
}

CryptoStatus LibScryptCompatAuthBlock::Derive(const AuthInput& auth_input,
                                              const AuthBlockState& auth_state,
                                              KeyBlobs* key_blobs) {
  const LibScryptCompatAuthBlockState* state;
  if (!(state =
            std::get_if<LibScryptCompatAuthBlockState>(&auth_state.state))) {
    LOG(ERROR) << "Invalid AuthBlockState";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocScryptCompatAuthBlockInvalidBlockStateInDerive),
        ErrorActionSet(
            {ErrorAction::kDevCheckUnexpectedState, ErrorAction::kAuth}),
        CryptoError::CE_OTHER_CRYPTO);
  }

  if (!state->wrapped_keyset.has_value()) {
    LOG(ERROR)
        << "Invalid LibScryptCompatAuthBlockState: missing wrapped_keyset";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocScryptCompatAuthBlockNoWrappedKeysetInDerive),
        ErrorActionSet({ErrorAction::kAuth, ErrorAction::kReboot,
                        ErrorAction::kDeleteVault}),
        CryptoError::CE_OTHER_CRYPTO);
  }

  const brillo::SecureBlob input_key = auth_input.user_input.value();
  brillo::SecureBlob wrapped_keyset = state->wrapped_keyset.value();
  brillo::SecureBlob derived_scrypt_key;
  CryptoStatus error =
      ParseHeaderAndDerive(wrapped_keyset, input_key, &derived_scrypt_key);
  if (!error.ok()) {
    return MakeStatus<CryptohomeCryptoError>(
               CRYPTOHOME_ERR_LOC(kLocScryptCompatAuthBlockInputKeyInDerive))
        .Wrap(std::move(error));
  }
  key_blobs->scrypt_key =
      std::make_unique<LibScryptCompatKeyObjects>(derived_scrypt_key);

  // This implementation is an unfortunate effect of how the libscrypt
  // encryption and decryption functions work. It generates a fresh key for each
  // buffer that is encrypted. Ideally, one key (|derived_scrypt_key|) would
  // wrap everything.
  if (state->wrapped_chaps_key.has_value()) {
    brillo::SecureBlob wrapped_chaps_key = state->wrapped_chaps_key.value();
    brillo::SecureBlob derived_chaps_key;
    error =
        ParseHeaderAndDerive(wrapped_chaps_key, input_key, &derived_chaps_key);
    if (!error.ok()) {
      return MakeStatus<CryptohomeCryptoError>(
                 CRYPTOHOME_ERR_LOC(kLocScryptCompatAuthBlockChapsKeyInDerive))
          .Wrap(std::move(error));
    }
    key_blobs->chaps_scrypt_key =
        std::make_unique<LibScryptCompatKeyObjects>(derived_chaps_key);
  }

  if (state->wrapped_reset_seed.has_value()) {
    brillo::SecureBlob wrapped_reset_seed = state->wrapped_reset_seed.value();
    brillo::SecureBlob derived_reset_seed_key;
    error = ParseHeaderAndDerive(wrapped_reset_seed, input_key,
                                 &derived_reset_seed_key);
    if (!error.ok()) {
      return MakeStatus<CryptohomeCryptoError>(
                 CRYPTOHOME_ERR_LOC(kLocScryptCompatAuthBlockResetKeyInDerive))
          .Wrap(std::move(error));
    }
    key_blobs->scrypt_wrapped_reset_seed_key =
        std::make_unique<LibScryptCompatKeyObjects>(derived_reset_seed_key);
  }

  return OkStatus<CryptohomeCryptoError>();
}

}  // namespace cryptohome
