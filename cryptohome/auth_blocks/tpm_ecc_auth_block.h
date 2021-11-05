// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_BLOCKS_TPM_ECC_AUTH_BLOCK_H_
#define CRYPTOHOME_AUTH_BLOCKS_TPM_ECC_AUTH_BLOCK_H_

#include "cryptohome/auth_blocks/auth_block.h"

#include <memory>
#include <string>

#include <base/gtest_prod_util.h>
#include <base/macros.h>
#include <base/threading/thread.h>

#include "cryptohome/auth_blocks/auth_block_state.h"
#include "cryptohome/auth_blocks/tpm_auth_block_utils.h"
#include "cryptohome/crypto.h"
#include "cryptohome/cryptohome_keys_manager.h"
#include "cryptohome/tpm.h"
#include "cryptohome/vault_keyset.pb.h"

namespace cryptohome {

class TpmEccAuthBlock : public SyncAuthBlock {
 public:
  TpmEccAuthBlock(Tpm* tpm, CryptohomeKeysManager* cryptohome_keys_manager);
  TpmEccAuthBlock(const TpmEccAuthBlock&) = delete;
  TpmEccAuthBlock& operator=(const TpmEccAuthBlock&) = delete;

  CryptoError Create(const AuthInput& auth_input,
                     AuthBlockState* auth_block_state,
                     KeyBlobs* key_blobs) override;

  CryptoError Derive(const AuthInput& auth_input,
                     const AuthBlockState& state,
                     KeyBlobs* key_blobs) override;

 private:
  // The create process may fail due to the scalar of EC_POINT_mul out of range.
  // We should retry the process again when retry_limit is not zero.
  CryptoError TryCreate(const AuthInput& auth_input,
                        AuthBlockState* auth_block_state,
                        KeyBlobs* key_blobs,
                        int retry_limit);

  // Derive the VKK from the user input and auth state.
  CryptoError DeriveVkk(bool locked_to_single_user,
                        const brillo::SecureBlob& user_input,
                        const TpmEccAuthBlockState& auth_state,
                        brillo::SecureBlob* vkk);

  // Derive the HVKKM from sealed HVKKM, preload handle.
  CryptoError DeriveHvkkm(bool locked_to_single_user,
                          brillo::SecureBlob pass_blob,
                          const brillo::SecureBlob& sealed_hvkkm,
                          ScopedKeyHandle* preload_handle,
                          uint32_t auth_value_rounds,
                          brillo::SecureBlob* vkk);

  Tpm* tpm_;
  CryptohomeKeyLoader* cryptohome_key_loader_;
  TpmAuthBlockUtils utils_;

  // The thread for performing scrypt operations.
  std::unique_ptr<base::Thread> scrypt_thread_;
  // The task runner that belongs to the scrypt thread.
  scoped_refptr<base::SingleThreadTaskRunner> scrypt_task_runner_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_BLOCKS_TPM_ECC_AUTH_BLOCK_H_
