// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/libscrypt_compat_auth_block.h"

#include <memory>
#include <utility>

#include <absl/types/variant.h>
#include <base/logging.h>

#include "cryptohome/auth_block_state.h"
#include "cryptohome/crypto/scrypt.h"
#include "cryptohome/crypto/secure_blob_util.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/key_objects.h"
#include "cryptohome/libscrypt_compat.h"

namespace cryptohome {

namespace {

bool CreateScryptHelper(const brillo::SecureBlob& input_key,
                        brillo::SecureBlob* salt,
                        brillo::SecureBlob* derived_key,
                        CryptoError* error) {
  // Because of the implementation peculiarity of libscrypt, the salt MUST be
  // unique for each key, and the same key can never be repurposed.
  *salt = CreateSecureRandomBlob(kLibScryptSaltSize);

  derived_key->resize(kLibScryptDerivedKeySize);
  if (!Scrypt(input_key, *salt, kDefaultScryptParams.n_factor,
              kDefaultScryptParams.r_factor, kDefaultScryptParams.p_factor,
              derived_key)) {
    LOG(ERROR) << "scrypt failed";
    PopulateError(error, CryptoError::CE_SCRYPT_CRYPTO);
    return false;
  }

  return true;
}

bool ParseHeaderAndDerive(const brillo::SecureBlob& wrapped_blob,
                          const brillo::SecureBlob& input_key,
                          brillo::SecureBlob* derived_key,
                          CryptoError* error) {
  ScryptParameters params;
  brillo::SecureBlob salt;
  if (!LibScryptCompat::ParseHeader(wrapped_blob, &params, &salt)) {
    LOG(ERROR) << "Failed to parse header.";
    PopulateError(error, CryptoError::CE_SCRYPT_CRYPTO);
    return false;
  }

  // Generate the derived key.
  derived_key->resize(kLibScryptDerivedKeySize);
  if (!Scrypt(input_key, salt, params.n_factor, params.r_factor,
              params.p_factor, derived_key)) {
    LOG(ERROR) << "scrypt failed";
    PopulateError(error, CryptoError::CE_SCRYPT_CRYPTO);
    return false;
  }

  return true;
}

}  // namespace

LibScryptCompatAuthBlock::LibScryptCompatAuthBlock()
    : AuthBlock(kScryptBacked) {}

LibScryptCompatAuthBlock::LibScryptCompatAuthBlock(
    DerivationType derivation_type)
    : AuthBlock(derivation_type) {}

base::Optional<AuthBlockState> LibScryptCompatAuthBlock::Create(
    const AuthInput& auth_input, KeyBlobs* key_blobs, CryptoError* error) {
  const brillo::SecureBlob input_key = auth_input.user_input.value();

  brillo::SecureBlob derived_key;
  brillo::SecureBlob salt;
  if (!CreateScryptHelper(input_key, &salt, &derived_key, error)) {
    return base::nullopt;
  }

  key_blobs->scrypt_key =
      std::make_unique<LibScryptCompatKeyObjects>(derived_key, salt);

  brillo::SecureBlob derived_chaps_key;
  brillo::SecureBlob chaps_salt;
  if (!CreateScryptHelper(input_key, &chaps_salt, &derived_chaps_key, error)) {
    return base::nullopt;
  }

  key_blobs->chaps_scrypt_key = std::make_unique<LibScryptCompatKeyObjects>(
      derived_chaps_key, chaps_salt);

  brillo::SecureBlob derived_reset_seed_key;
  brillo::SecureBlob reset_seed_salt;
  if (!CreateScryptHelper(input_key, &reset_seed_salt, &derived_reset_seed_key,
                          error)) {
    return base::nullopt;
  }

  key_blobs->scrypt_wrapped_reset_seed_key =
      std::make_unique<LibScryptCompatKeyObjects>(derived_reset_seed_key,
                                                  reset_seed_salt);

  // libscrypt is an odd case again; the AuthBlockState is only populated on the
  // derivation flow. See the class header for a full explanation.
  LibScryptCompatAuthBlockState scrypt_state;

  // TODO(b/198394243): We should remove this because it's not actually used.
  scrypt_state.salt = CreateSecureRandomBlob(CRYPTOHOME_DEFAULT_KEY_SALT_SIZE);

  AuthBlockState auth_state = {.state = std::move(scrypt_state)};
  return auth_state;
}

bool LibScryptCompatAuthBlock::Derive(const AuthInput& auth_input,
                                      const AuthBlockState& auth_state,
                                      KeyBlobs* key_blobs,
                                      CryptoError* error) {
  const LibScryptCompatAuthBlockState* state;
  if (!(state =
            absl::get_if<LibScryptCompatAuthBlockState>(&auth_state.state))) {
    LOG(ERROR) << "Invalid AuthBlockState";
    return false;
  }

  if (!state->wrapped_keyset.has_value()) {
    LOG(ERROR)
        << "Invalid LibScryptCompatAuthBlockState: missing wrapped_keyset";
    return false;
  }

  const brillo::SecureBlob input_key = auth_input.user_input.value();
  brillo::SecureBlob wrapped_keyset = state->wrapped_keyset.value();
  brillo::SecureBlob derived_scrypt_key;
  if (!ParseHeaderAndDerive(wrapped_keyset, input_key, &derived_scrypt_key,
                            error)) {
    return false;
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
    if (!ParseHeaderAndDerive(wrapped_chaps_key, input_key, &derived_chaps_key,
                              error)) {
      return false;
    }
    key_blobs->chaps_scrypt_key =
        std::make_unique<LibScryptCompatKeyObjects>(derived_chaps_key);
  }

  if (state->wrapped_reset_seed.has_value()) {
    brillo::SecureBlob wrapped_reset_seed = state->wrapped_reset_seed.value();
    brillo::SecureBlob derived_reset_seed_key;
    if (!ParseHeaderAndDerive(wrapped_reset_seed, input_key,
                              &derived_reset_seed_key, error)) {
      return false;
    }
    key_blobs->scrypt_wrapped_reset_seed_key =
        std::make_unique<LibScryptCompatKeyObjects>(derived_reset_seed_key);
  }

  return true;
}

}  // namespace cryptohome
