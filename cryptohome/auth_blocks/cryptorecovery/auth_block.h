// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_BLOCKS_CRYPTORECOVERY_AUTH_BLOCK_H_
#define CRYPTOHOME_AUTH_BLOCKS_CRYPTORECOVERY_AUTH_BLOCK_H_

#include <memory>

#include <libhwsec/frontend/cryptohome/frontend.h>
#include <libhwsec/frontend/pinweaver_manager/frontend.h>
#include <libstorage/platform/platform.h>

#include "cryptohome/auth_blocks/auth_block.h"
#include "cryptohome/auth_blocks/auth_block_type.h"
#include "cryptohome/crypto.h"
#include "cryptohome/error/cryptohome_crypto_error.h"
#include "cryptohome/error/cryptohome_error.h"
#include "cryptohome/flatbuffer_schemas/auth_block_state.h"
#include "cryptohome/key_objects.h"

namespace cryptohome {

// AuthBlock for Cryptohome Recovery flow. Secret is generated on the device and
// later derived by Cryptohome Recovery process using data stored on the device
// and by Recovery Mediator service.
class CryptohomeRecoveryAuthBlock : public AuthBlock {
 public:
  // Implement the GenericAuthBlock concept.
  static constexpr auto kType = AuthBlockType::kCryptohomeRecovery;
  using StateType = CryptohomeRecoveryAuthBlockState;
  static CryptoStatus IsSupported(Crypto& crypto);
  static std::unique_ptr<AuthBlock> New(
      libstorage::Platform& platform,
      const hwsec::CryptohomeFrontend& hwsec,
      const hwsec::RecoveryCryptoFrontend& recovery_hwsec,
      const hwsec::PinWeaverManagerFrontend& hwsec_pw_manager);

  // the `tpm` pointer must outlive `this`
  explicit CryptohomeRecoveryAuthBlock(
      const hwsec::CryptohomeFrontend* hwsec,
      const hwsec::RecoveryCryptoFrontend* recovery_hwsec,
      libstorage::Platform* platform);
  explicit CryptohomeRecoveryAuthBlock(
      const hwsec::CryptohomeFrontend* hwsec,
      const hwsec::RecoveryCryptoFrontend* recovery_hwsec,
      const hwsec::PinWeaverManagerFrontend* hwsec_pw_manager,
      libstorage::Platform* platform);

  CryptohomeRecoveryAuthBlock(const CryptohomeRecoveryAuthBlock&) = delete;
  CryptohomeRecoveryAuthBlock& operator=(const CryptohomeRecoveryAuthBlock&) =
      delete;

  // `auth_input` object should have `salt` and
  // `cryptohome_recovery_auth_input.mediator_pub_key` fields set.
  void Create(const AuthInput& auth_input,
              const AuthFactorMetadata& auth_factor_metadata,
              CreateCallback callback) override;

  // `auth_input` object should have `salt`,
  // `cryptohome_recovery_auth_input.epoch_pub_key`,
  // `cryptohome_recovery_auth_input.ephemeral_pub_key` and
  // `cryptohome_recovery_auth_input.recovery_response` fields set.
  void Derive(const AuthInput& auth_input,
              const AuthFactorMetadata& auth_factor_metadata,
              const AuthBlockState& state,
              DeriveCallback callback) override;

  void PrepareForRemoval(const ObfuscatedUsername& obfuscated_username,
                         const AuthBlockState& state,
                         StatusCallback callback) override;

 private:
  CryptoStatus PrepareForRemovalInternal(const AuthBlockState& state);

  const hwsec::CryptohomeFrontend* const hwsec_;
  const hwsec::RecoveryCryptoFrontend* const recovery_hwsec_;
  const hwsec::PinWeaverManagerFrontend* const hwsec_pw_manager_;
  // Low Entropy credentials manager, needed for revocation support.
  libstorage::Platform* const platform_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_BLOCKS_CRYPTORECOVERY_AUTH_BLOCK_H_
