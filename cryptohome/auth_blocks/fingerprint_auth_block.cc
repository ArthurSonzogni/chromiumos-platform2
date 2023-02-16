// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_blocks/fingerprint_auth_block.h"

#include <base/notreached.h>

#include "cryptohome/auth_blocks/auth_block.h"
#include "cryptohome/crypto.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/error/cryptohome_crypto_error.h"
#include "cryptohome/error/location_utils.h"
#include "cryptohome/error/locations.h"
#include "cryptohome/flatbuffer_schemas/auth_block_state.h"

namespace cryptohome {

namespace {
using cryptohome::error::CryptohomeCryptoError;
using cryptohome::error::ErrorAction;
using cryptohome::error::ErrorActionSet;
using hwsec_foundation::status::MakeStatus;
}  // namespace

FingerprintAuthBlock::FingerprintAuthBlock() : AuthBlock(kBiometrics) {}

CryptoStatus FingerprintAuthBlock::IsSupported(Crypto& crypto) {
  return MakeStatus<CryptohomeCryptoError>(
      CRYPTOHOME_ERR_LOC(kLocFingerprintAuthBlockUnimplemented),
      ErrorActionSet({ErrorAction::kAuth}), CryptoError::CE_OTHER_CRYPTO);
}

void FingerprintAuthBlock::Create(const AuthInput& auth_input,
                                  CreateCallback callback) {
  NOTREACHED();
}

void FingerprintAuthBlock::Derive(const AuthInput& auth_input,
                                  const AuthBlockState& state,
                                  DeriveCallback callback) {
  NOTREACHED();
}

CryptohomeStatus FingerprintAuthBlock::PrepareForRemoval(
    const AuthBlockState& state) {
  return MakeStatus<CryptohomeCryptoError>(
      CRYPTOHOME_ERR_LOC(
          kLocFingerprintAuthBlockPrepareForRemovalUnimplemented),
      ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
      CryptoError::CE_OTHER_FATAL);
}

}  // namespace cryptohome
