// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/double_wrapped_compat_auth_block.h"

#include <memory>
#include <utility>

#include "cryptohome/cryptohome_key_loader.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/libscrypt_compat_auth_block.h"
#include "cryptohome/tpm.h"
#include "cryptohome/tpm_not_bound_to_pcr_auth_block.h"

#include <base/check.h>

namespace cryptohome {

DoubleWrappedCompatAuthBlock::DoubleWrappedCompatAuthBlock(
    Tpm* tpm, CryptohomeKeyLoader* cryptohome_key_loader)
    : AuthBlock(kDoubleWrapped),
      tpm_auth_block_(tpm, cryptohome_key_loader),
      lib_scrypt_compat_auth_block_() {}

base::Optional<AuthBlockState> DoubleWrappedCompatAuthBlock::Create(
    const AuthInput& user_input, KeyBlobs* key_blobs, CryptoError* error) {
  LOG(FATAL) << "Cannot create a keyset wrapped with both scrypt and TPM.";
  return base::nullopt;
}

bool DoubleWrappedCompatAuthBlock::Derive(const AuthInput& auth_input,
                                          const AuthBlockState& state,
                                          KeyBlobs* key_blobs,
                                          CryptoError* error) {
  if (!state.has_double_wrapped_compat_state()) {
    DLOG(FATAL) << "Invalid AuthBlockState";
    return false;
  }

  AuthBlockState scrypt_state;
  *(scrypt_state.mutable_libscrypt_compat_state()) =
      state.double_wrapped_compat_state().scrypt_state();
  if (lib_scrypt_compat_auth_block_.Derive(auth_input, scrypt_state, key_blobs,
                                           error)) {
    return true;
  }

  AuthBlockState tpm_state;
  *(tpm_state.mutable_tpm_not_bound_to_pcr_state()) =
      state.double_wrapped_compat_state().tpm_state();
  return tpm_auth_block_.Derive(auth_input, tpm_state, key_blobs, error);
}

}  // namespace cryptohome
