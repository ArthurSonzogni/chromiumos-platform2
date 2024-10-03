// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_BLOCKS_PIN_WEAVER_AUTH_BLOCK_H_
#define CRYPTOHOME_AUTH_BLOCKS_PIN_WEAVER_AUTH_BLOCK_H_

#include <memory>

#include <libhwsec/frontend/cryptohome/frontend.h>
#include <libhwsec/frontend/pinweaver_manager/frontend.h>

#include "cryptohome/auth_blocks/auth_block.h"
#include "cryptohome/auth_blocks/auth_block_type.h"
#include "cryptohome/error/cryptohome_crypto_error.h"
#include "cryptohome/error/cryptohome_error.h"
#include "cryptohome/features.h"
#include "cryptohome/flatbuffer_schemas/auth_block_state.h"

namespace cryptohome {

using DelaySchedule = hwsec::PinWeaverManagerFrontend::DelaySchedule;

// Define the standard lockout policy to use for all LE credentials. This policy
// will allow for 5 attempts with no delay, and then permanent lockout until the
// credential is reset.
const DelaySchedule& LockoutDelaySchedule();

// Define the (non-lockout) delay policies for PINs and passwords. These apply a
// gradually increasing delay after more and more attempts are made.
const DelaySchedule& PinDelaySchedule();
const DelaySchedule& PasswordDelaySchedule();

class PinWeaverAuthBlock : public AuthBlock {
 public:
  // Implement the GenericAuthBlock concept.
  static constexpr auto kType = AuthBlockType::kPinWeaver;
  using StateType = PinWeaverAuthBlockState;
  static CryptoStatus IsSupported(const hwsec::CryptohomeFrontend& hwsec);
  static std::unique_ptr<AuthBlock> New(
      AsyncInitFeatures& features,
      const hwsec::PinWeaverManagerFrontend& hwsec_pw_manager);

  PinWeaverAuthBlock(AsyncInitFeatures& features,
                     const hwsec::PinWeaverManagerFrontend* hwsec_pw_manager);

  PinWeaverAuthBlock(const PinWeaverAuthBlock&) = delete;
  PinWeaverAuthBlock& operator=(const PinWeaverAuthBlock&) = delete;

  void Create(const AuthInput& user_input,
              const AuthFactorMetadata& auth_factor_metadata,
              CreateCallback callback) override;

  void Derive(const AuthInput& auth_input,
              const AuthFactorMetadata& auth_factor_metadata,
              const AuthBlockState& state,
              DeriveCallback callback) override;

  // Removing the underlying Pinweaver leaf node before the AuthFactor is
  // removed.
  void PrepareForRemoval(const ObfuscatedUsername& obfuscated_username,
                         const AuthBlockState& state,
                         StatusCallback callback) override;

  uint32_t GetLockoutDelay(uint64_t label);

 private:
  // Feature lookup interface.
  AsyncInitFeatures* features_;
  const hwsec::PinWeaverManagerFrontend* const hwsec_pw_manager_;
};

// Wrapper implementation for password auth blocks which are not pinweaver.
//
// This abstract base will wrap the underlying Derive operation to request
// recreating the block if pinweaver is available. This will upgrade the
// blocks to pinweaver on such systems.
class NonPinweaverPasswordAuthBlock : public AuthBlock {
 protected:
  NonPinweaverPasswordAuthBlock(DerivationType derivation_type,
                                AsyncInitFeatures& features,
                                const hwsec::CryptohomeFrontend& hwsec);

 private:
  // Call DerivePassword, possibly adjusting the suggested action to request
  // recreating the auth block.
  void Derive(const AuthInput& auth_input,
              const AuthFactorMetadata& auth_factor_metadata,
              const AuthBlockState& state,
              DeriveCallback callback) final;

  // Subclasses should implement the actual underlying Derive operation here.
  virtual void DerivePassword(const AuthInput& auth_input,
                              const AuthFactorMetadata& auth_factor_metadata,
                              const AuthBlockState& state,
                              DeriveCallback callback) = 0;

  AsyncInitFeatures* features_;
  const hwsec::CryptohomeFrontend* hwsec_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_BLOCKS_PIN_WEAVER_AUTH_BLOCK_H_
