// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_BLOCKS_TPM_BOUND_TO_PCR_AUTH_BLOCK_H_
#define CRYPTOHOME_AUTH_BLOCKS_TPM_BOUND_TO_PCR_AUTH_BLOCK_H_

#include "cryptohome/auth_blocks/auth_block.h"

#include <memory>
#include <string>

#include <base/gtest_prod_util.h>
#include <base/threading/thread.h>
#include <libhwsec/frontend/cryptohome/frontend.h>
#include <libhwsec/status.h>

#include "cryptohome/auth_blocks/auth_block_type.h"
#include "cryptohome/auth_blocks/tpm_auth_block_utils.h"
#include "cryptohome/crypto.h"
#include "cryptohome/cryptohome_keys_manager.h"
#include "cryptohome/error/cryptohome_crypto_error.h"
#include "cryptohome/flatbuffer_schemas/auth_block_state.h"
#include "cryptohome/vault_keyset.pb.h"

namespace cryptohome {

class TpmBoundToPcrAuthBlock : public SyncAuthBlock {
 public:
  // Implement the GenericAuthBlock concept.
  static constexpr auto kType = AuthBlockType::kTpmBoundToPcr;
  using StateType = TpmBoundToPcrAuthBlockState;
  static CryptoStatus IsSupported(Crypto& crypto);

  TpmBoundToPcrAuthBlock(hwsec::CryptohomeFrontend* hwsec,
                         CryptohomeKeysManager* cryptohome_keys_manager);

  TpmBoundToPcrAuthBlock(const TpmBoundToPcrAuthBlock&) = delete;
  TpmBoundToPcrAuthBlock& operator=(const TpmBoundToPcrAuthBlock&) = delete;

  CryptoStatus Create(const AuthInput& user_input,
                      AuthBlockState* auth_block_state,
                      KeyBlobs* key_blobs) override;

  CryptoStatus Derive(const AuthInput& auth_input,
                      const AuthBlockState& state,
                      KeyBlobs* key_blobs) override;

 private:
  // Decrypt the |vault_key| that is bound to PCR, returning the |vkk_iv|
  // and |vkk_key|.
  CryptoStatus DecryptTpmBoundToPcr(const brillo::SecureBlob& vault_key,
                                    const brillo::SecureBlob& tpm_key,
                                    const brillo::SecureBlob& salt,
                                    brillo::SecureBlob* vkk_iv,
                                    brillo::SecureBlob* vkk_key) const;

  hwsec::CryptohomeFrontend* hwsec_;
  CryptohomeKeyLoader* cryptohome_key_loader_;
  TpmAuthBlockUtils utils_;

  // The thread for performing scrypt operations.
  std::unique_ptr<base::Thread> scrypt_thread_;
  // The task runner that belongs to the scrypt thread.
  scoped_refptr<base::SingleThreadTaskRunner> scrypt_task_runner_;

  FRIEND_TEST_ALL_PREFIXES(TPMAuthBlockTest, DecryptBoundToPcrTest);
  FRIEND_TEST_ALL_PREFIXES(TPMAuthBlockTest, DecryptBoundToPcrNoPreloadTest);
  FRIEND_TEST_ALL_PREFIXES(TPMAuthBlockTest,
                           DecryptBoundToPcrPreloadFailedTest);
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_BLOCKS_TPM_BOUND_TO_PCR_AUTH_BLOCK_H_
