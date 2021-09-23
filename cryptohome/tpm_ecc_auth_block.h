// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_TPM_ECC_AUTH_BLOCK_H_
#define CRYPTOHOME_TPM_ECC_AUTH_BLOCK_H_

#include "cryptohome/auth_block.h"

#include <memory>
#include <string>

#include <base/gtest_prod_util.h>
#include <base/macros.h>
#include <base/threading/thread.h>

#include "cryptohome/auth_block_state.h"
#include "cryptohome/crypto.h"
#include "cryptohome/cryptohome_keys_manager.h"
#include "cryptohome/tpm.h"
#include "cryptohome/tpm_auth_block_utils.h"
#include "cryptohome/vault_keyset.pb.h"

namespace cryptohome {

class TpmEccAuthBlock : public AuthBlock {
 public:
  TpmEccAuthBlock(Tpm* tpm, CryptohomeKeysManager* cryptohome_keys_manager);
  TpmEccAuthBlock(const TpmEccAuthBlock&) = delete;
  TpmEccAuthBlock& operator=(const TpmEccAuthBlock&) = delete;

  base::Optional<AuthBlockState> Create(const AuthInput& auth_input,
                                        KeyBlobs* key_blobs,
                                        CryptoError* error) override;

  bool Derive(const AuthInput& auth_input,
              const AuthBlockState& state,
              KeyBlobs* key_blobs,
              CryptoError* error) override;

 private:
  // The create process may fail due to the scalar of EC_POINT_mul out of range.
  // We should retry the process again when this function sets |retry| to true.
  base::Optional<AuthBlockState> TryCreate(const AuthInput& auth_input,
                                           KeyBlobs* key_blobs,
                                           CryptoError* error,
                                           bool* retry);

  // Derive the VKK from the user input and auth state.
  base::Optional<brillo::SecureBlob> DeriveVkk(
      bool locked_to_single_user,
      const brillo::SecureBlob& user_input,
      const TpmEccAuthBlockState& auth_state,
      CryptoError* error);

  // Derive the HVKKM from sealed HVKKM, preload handle.
  base::Optional<brillo::SecureBlob> DeriveHvkkm(
      bool locked_to_single_user,
      brillo::SecureBlob pass_blob,
      const brillo::SecureBlob& sealed_hvkkm,
      ScopedKeyHandle* preload_handle,
      uint32_t auth_value_rounds,
      CryptoError* error);

  Tpm* tpm_;
  CryptohomeKeyLoader* cryptohome_key_loader_;
  TpmAuthBlockUtils utils_;

  // The thread for performing scrypt operations.
  std::unique_ptr<base::Thread> scrypt_thread_;
  // The task runner that belongs to the scrypt thread.
  scoped_refptr<base::SingleThreadTaskRunner> scrypt_task_runner_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_TPM_ECC_AUTH_BLOCK_H_
