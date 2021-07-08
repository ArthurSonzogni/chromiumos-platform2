// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_TPM_BOUND_TO_PCR_AUTH_BLOCK_H_
#define CRYPTOHOME_TPM_BOUND_TO_PCR_AUTH_BLOCK_H_

#include "cryptohome/auth_block.h"

#include <memory>
#include <string>

#include <base/gtest_prod_util.h>
#include <base/macros.h>
#include <base/threading/thread.h>

#include "cryptohome/crypto.h"
#include "cryptohome/cryptohome_keys_manager.h"
#include "cryptohome/tpm.h"
#include "cryptohome/tpm_auth_block_utils.h"
#include "cryptohome/vault_keyset.pb.h"

namespace cryptohome {

class TpmBoundToPcrAuthBlock : public AuthBlock {
 public:
  TpmBoundToPcrAuthBlock(Tpm* tpm,
                         CryptohomeKeysManager* cryptohome_keys_manager);
  TpmBoundToPcrAuthBlock(const TpmBoundToPcrAuthBlock&) = delete;
  TpmBoundToPcrAuthBlock& operator=(const TpmBoundToPcrAuthBlock&) = delete;

  base::Optional<AuthBlockState> Create(const AuthInput& user_input,
                                        KeyBlobs* key_blobs,
                                        CryptoError* error) override;

  bool Derive(const AuthInput& auth_input,
              const AuthBlockState& state,
              KeyBlobs* key_blobs,
              CryptoError* error) override;

 private:
  // Decrypt the |vault_key| that is bound to PCR, returning the |vkk_iv|
  // and |vkk_key|.
  bool DecryptTpmBoundToPcr(const brillo::SecureBlob& vault_key,
                            const brillo::SecureBlob& tpm_key,
                            const brillo::SecureBlob& salt,
                            CryptoError* error,
                            brillo::SecureBlob* vkk_iv,
                            brillo::SecureBlob* vkk_key) const;

  Tpm* tpm_;
  CryptohomeKeyLoader* cryptohome_key_loader_;
  TpmAuthBlockUtils utils_;

  // The thread for performing scrypt operations.
  std::unique_ptr<base::Thread> scrypt_thread_;
  // The task runner that belongs to the scrypt thread.
  scoped_refptr<base::SingleThreadTaskRunner> scrypt_task_runner_;

  FRIEND_TEST_ALL_PREFIXES(TPMAuthBlockTest, DecryptBoundToPcrTest);
  FRIEND_TEST_ALL_PREFIXES(TPMAuthBlockTest, DecryptBoundToPcrNoPreloadTest);
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_TPM_BOUND_TO_PCR_AUTH_BLOCK_H_
