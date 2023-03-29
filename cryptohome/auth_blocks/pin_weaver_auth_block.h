// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_BLOCKS_PIN_WEAVER_AUTH_BLOCK_H_
#define CRYPTOHOME_AUTH_BLOCKS_PIN_WEAVER_AUTH_BLOCK_H_

#include <memory>

#include "cryptohome/auth_blocks/auth_block.h"
#include "cryptohome/auth_blocks/auth_block_type.h"
#include "cryptohome/crypto.h"
#include "cryptohome/error/cryptohome_crypto_error.h"
#include "cryptohome/features.h"
#include "cryptohome/flatbuffer_schemas/auth_block_state.h"
#include "cryptohome/le_credential_manager.h"

namespace cryptohome {

// Define the standard lockout policy to use for all LE credentials. This policy
// will allow for 5 attempts with no delay, and then permanent lockout until the
// credential is reset.
const LECredentialManager::DelaySchedule& LockoutDelaySchedule();

// Define the PIN delay policy. This applies a gradually increasing delay after
// more and more attempts are made.
const LECredentialManager::DelaySchedule& PinDelaySchedule();

class PinWeaverAuthBlock : public SyncAuthBlock {
 public:
  // Implement the GenericAuthBlock concept.
  static constexpr auto kType = AuthBlockType::kPinWeaver;
  using StateType = PinWeaverAuthBlockState;
  static CryptoStatus IsSupported(Crypto& crypto);
  static std::unique_ptr<AuthBlock> New(AsyncInitFeatures& features,
                                        LECredentialManager* le_manager);

  PinWeaverAuthBlock(AsyncInitFeatures& features,
                     LECredentialManager* le_manager);

  PinWeaverAuthBlock(const PinWeaverAuthBlock&) = delete;
  PinWeaverAuthBlock& operator=(const PinWeaverAuthBlock&) = delete;

  CryptoStatus Create(const AuthInput& user_input,
                      AuthBlockState* auth_block_state,
                      KeyBlobs* key_blobs) override;

  CryptoStatus Derive(const AuthInput& auth_input,
                      const AuthBlockState& state,
                      KeyBlobs* key_blobs) override;

  // Removing the underlying Pinweaver leaf node before the AuthFactor is
  // removed.
  CryptohomeStatus PrepareForRemoval(const AuthBlockState& state) override;

  uint32_t GetLockoutDelay(uint64_t label);

 private:
  // Feature lookup interface.
  AsyncInitFeatures* features_;
  // Handler for Low Entropy credentials.
  LECredentialManager* le_manager_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_BLOCKS_PIN_WEAVER_AUTH_BLOCK_H_
