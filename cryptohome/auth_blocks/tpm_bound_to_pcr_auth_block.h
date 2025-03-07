// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_BLOCKS_TPM_BOUND_TO_PCR_AUTH_BLOCK_H_
#define CRYPTOHOME_AUTH_BLOCKS_TPM_BOUND_TO_PCR_AUTH_BLOCK_H_

#include <memory>

#include <base/gtest_prod_util.h>
#include <base/task/sequenced_task_runner.h>
#include <base/threading/thread.h>
#include <libhwsec/frontend/cryptohome/frontend.h>
#include <libhwsec/status.h>

#include "cryptohome/auth_blocks/auth_block.h"
#include "cryptohome/auth_blocks/auth_block_type.h"
#include "cryptohome/auth_blocks/pin_weaver_auth_block.h"
#include "cryptohome/auth_blocks/tpm_auth_block_utils.h"
#include "cryptohome/crypto.h"
#include "cryptohome/cryptohome_keys_manager.h"
#include "cryptohome/error/cryptohome_crypto_error.h"
#include "cryptohome/features.h"
#include "cryptohome/flatbuffer_schemas/auth_block_state.h"

namespace cryptohome {

class TpmBoundToPcrAuthBlock : public NonPinweaverPasswordAuthBlock {
 public:
  // Implement the GenericAuthBlock concept.
  static constexpr auto kType = AuthBlockType::kTpmBoundToPcr;
  using StateType = TpmBoundToPcrAuthBlockState;
  static CryptoStatus IsSupported(Crypto& crypto);
  static std::unique_ptr<AuthBlock> New(
      AsyncInitFeatures& features,
      base::SequencedTaskRunner& scrypt_task_runner,
      const hwsec::CryptohomeFrontend& hwsec,
      CryptohomeKeysManager& cryptohome_keys_manager);

  TpmBoundToPcrAuthBlock(AsyncInitFeatures& features,
                         base::SequencedTaskRunner& scrypt_task_runner,
                         const hwsec::CryptohomeFrontend& hwsec,
                         CryptohomeKeysManager& cryptohome_keys_manager);

  TpmBoundToPcrAuthBlock(const TpmBoundToPcrAuthBlock&) = delete;
  TpmBoundToPcrAuthBlock& operator=(const TpmBoundToPcrAuthBlock&) = delete;

  void Create(const AuthInput& user_input,
              const AuthFactorMetadata& auth_factor_metadata,
              CreateCallback callback) override;

  void DerivePassword(const AuthInput& auth_input,
                      const AuthFactorMetadata& auth_factor_metadata,
                      const AuthBlockState& state,
                      DeriveCallback callback) override;

 private:
  base::SequencedTaskRunner* scrypt_task_runner_;
  const hwsec::CryptohomeFrontend* hwsec_;
  CryptohomeKeyLoader* cryptohome_key_loader_;
  TpmAuthBlockUtils utils_;

  FRIEND_TEST_ALL_PREFIXES(TPMAuthBlockTest, DecryptBoundToPcrTest);
  FRIEND_TEST_ALL_PREFIXES(TPMAuthBlockTest, DecryptBoundToPcrNoPreloadTest);
  FRIEND_TEST_ALL_PREFIXES(TPMAuthBlockTest,
                           DecryptBoundToPcrPreloadFailedTest);
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_BLOCKS_TPM_BOUND_TO_PCR_AUTH_BLOCK_H_
