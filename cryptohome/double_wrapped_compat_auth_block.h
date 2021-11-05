// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_DOUBLE_WRAPPED_COMPAT_AUTH_BLOCK_H_
#define CRYPTOHOME_DOUBLE_WRAPPED_COMPAT_AUTH_BLOCK_H_

#include "cryptohome/auth_block.h"

#include <memory>

#include <base/gtest_prod_util.h>
#include <base/macros.h>

#include "cryptohome/auth_block_state.h"
#include "cryptohome/libscrypt_compat_auth_block.h"
#include "cryptohome/tpm_not_bound_to_pcr_auth_block.h"

namespace cryptohome {

class Tpm;
class CryptohomeKeysManager;

class DoubleWrappedCompatAuthBlock : public SyncAuthBlock {
 public:
  DoubleWrappedCompatAuthBlock(Tpm* tpm,
                               CryptohomeKeysManager* cryptohome_keys_manager);
  DoubleWrappedCompatAuthBlock(const DoubleWrappedCompatAuthBlock&) = delete;
  DoubleWrappedCompatAuthBlock& operator=(const DoubleWrappedCompatAuthBlock&) =
      delete;

  // This auth block represents legacy keysets left in an inconsistent state, so
  // calling Create() here is FATAL.
  CryptoError Create(const AuthInput& user_input,
                     AuthBlockState* auth_block_state,
                     KeyBlobs* key_blobs) override;

  // First tries to derive the keys with scrypt, and falls back to the TPM.
  CryptoError Derive(const AuthInput& auth_input,
                     const AuthBlockState& state,
                     KeyBlobs* key_blobs) override;

 private:
  TpmNotBoundToPcrAuthBlock tpm_auth_block_;
  LibScryptCompatAuthBlock lib_scrypt_compat_auth_block_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_DOUBLE_WRAPPED_COMPAT_AUTH_BLOCK_H_
