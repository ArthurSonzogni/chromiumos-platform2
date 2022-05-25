// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_blocks/scrypt_auth_block.h"

#include <memory>
#include <utility>
#include <variant>

#include <base/logging.h>
#include <libhwsec-foundation/crypto/aes.h>
#include <libhwsec-foundation/crypto/scrypt.h>
#include <libhwsec-foundation/crypto/secure_blob_util.h>

#include "cryptohome/auth_blocks/auth_block_state.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/error/location_utils.h"

using ::cryptohome::error::CryptohomeCryptoError;
using ::cryptohome::error::ErrorAction;
using ::cryptohome::error::ErrorActionSet;
using ::hwsec_foundation::CreateSecureRandomBlob;
using ::hwsec_foundation::kAesBlockSize;
using ::hwsec_foundation::kDefaultAesKeySize;
using ::hwsec_foundation::kDefaultScryptParams;
using ::hwsec_foundation::Scrypt;
using ::hwsec_foundation::status::MakeStatus;
using ::hwsec_foundation::status::OkStatus;

namespace cryptohome {

ScryptAuthBlock::ScryptAuthBlock() : SyncAuthBlock(kScryptBacked) {}

CryptoStatus ScryptAuthBlock::Create(const AuthInput& auth_input,
                                     AuthBlockState* auth_block_state,
                                     KeyBlobs* key_blobs) {
  const brillo::SecureBlob input_key = auth_input.user_input.value();

  brillo::SecureBlob salt =
      CreateSecureRandomBlob(CRYPTOHOME_DEFAULT_KEY_SALT_SIZE);
  int32_t work_factor = kDefaultScryptParams.n_factor;
  uint32_t block_size = kDefaultScryptParams.r_factor;
  uint32_t parallel_factor = kDefaultScryptParams.p_factor;
  brillo::SecureBlob derived_key(kDefaultAesKeySize);

  if (!Scrypt(input_key, salt, work_factor, block_size, parallel_factor,
              &derived_key)) {
    LOG(ERROR) << "scrypt failed";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocScryptAuthBlockScryptFailedInCreate),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        CryptoError::CE_SCRYPT_CRYPTO);
  }

  ScryptAuthBlockState scrypt_state{
      .salt = std::move(salt),
      .work_factor = work_factor,
      .block_size = block_size,
      .parallel_factor = parallel_factor,
  };

  key_blobs->vkk_key = std::move(derived_key);

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

  brillo::SecureBlob derived_key(kDefaultAesKeySize);

  if (!Scrypt(input_key, state->salt.value(), state->work_factor.value(),
              state->block_size.value(), state->parallel_factor.value(),
              &derived_key)) {
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocScryptAuthBlockScryptFailedInDeriveFromSalt),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        CryptoError::CE_SCRYPT_CRYPTO);
  }

  key_blobs->vkk_key = std::move(derived_key);

  return OkStatus<CryptohomeCryptoError>();
}

}  // namespace cryptohome
