// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_BLOCKS_CRYPTOHOME_RECOVERY_AUTH_BLOCK_H_
#define CRYPTOHOME_AUTH_BLOCKS_CRYPTOHOME_RECOVERY_AUTH_BLOCK_H_

#include <libhwsec/frontend/cryptohome/frontend.h>

#include "cryptohome/auth_blocks/auth_block.h"
#include "cryptohome/crypto.h"
#include "cryptohome/flatbuffer_schemas/auth_block_state.h"
#include "cryptohome/key_objects.h"
#include "cryptohome/vault_keyset.h"

namespace cryptohome {

// AuthBlock for Cryptohome Recovery flow. Secret is generated on the device and
// later derived by Cryptohome Recovery process using data stored on the device
// and by Recovery Mediator service.
class CryptohomeRecoveryAuthBlock : public SyncAuthBlock {
 public:
  // the `tpm` pointer must outlive `this`
  explicit CryptohomeRecoveryAuthBlock(
      hwsec::CryptohomeFrontend* hwsec,
      hwsec::RecoveryCryptoFrontend* recovery_hwsec,
      Platform* platform);
  explicit CryptohomeRecoveryAuthBlock(
      hwsec::CryptohomeFrontend* hwsec,
      hwsec::RecoveryCryptoFrontend* recovery_hwsec,
      LECredentialManager* le_manager,
      Platform* platform);

  CryptohomeRecoveryAuthBlock(const CryptohomeRecoveryAuthBlock&) = delete;
  CryptohomeRecoveryAuthBlock& operator=(const CryptohomeRecoveryAuthBlock&) =
      delete;
  ~CryptohomeRecoveryAuthBlock() = default;

  // `auth_input` object should have `salt` and
  // `cryptohome_recovery_auth_input.mediator_pub_key` fields set.
  CryptoStatus Create(const AuthInput& auth_input,
                      AuthBlockState* auth_block_state,
                      KeyBlobs* key_blobs) override;

  // `auth_input` object should have `salt`,
  // `cryptohome_recovery_auth_input.epoch_pub_key`,
  // `cryptohome_recovery_auth_input.ephemeral_pub_key` and
  // `cryptohome_recovery_auth_input.recovery_response` fields set.
  CryptoStatus Derive(const AuthInput& auth_input,
                      const AuthBlockState& state,
                      KeyBlobs* key_blobs) override;

  CryptoStatus PrepareForRemoval(const AuthBlockState& state) override;

 private:
  CryptoStatus PrepareForRemovalInternal(const AuthBlockState& state);

  hwsec::CryptohomeFrontend* const hwsec_;
  hwsec::RecoveryCryptoFrontend* const recovery_hwsec_;
  // Low Entropy credentials manager, needed for revocation support.
  LECredentialManager* const le_manager_;
  Platform* const platform_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_BLOCKS_CRYPTOHOME_RECOVERY_AUTH_BLOCK_H_
