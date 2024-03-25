// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_BLOCKS_CRYPTOHOME_RECOVERY_SERVICE_H_
#define CRYPTOHOME_AUTH_BLOCKS_CRYPTOHOME_RECOVERY_SERVICE_H_

#include <brillo/secure_blob.h>
#include <libhwsec/frontend/recovery_crypto/frontend.h>
#include <libstorage/platform/platform.h>

#include "cryptohome/auth_blocks/prepare_token.h"
#include "cryptohome/cryptorecovery/recovery_crypto_util.h"
#include "cryptohome/error/cryptohome_error.h"
#include "cryptohome/flatbuffer_schemas/auth_block_state.h"
#include "cryptohome/key_objects.h"
#include "cryptohome/username.h"

namespace cryptohome {

// This service handles ongoing recovery requests for a recovery auth block.
//
// Note that this service is unrelated to the off-machine "recovery service"
// which supports the recovery process. This is simply an in-process service
// used to implement auth factor.
class CryptohomeRecoveryAuthBlockService {
 public:
  CryptohomeRecoveryAuthBlockService(
      libstorage::Platform* platform,
      const hwsec::RecoveryCryptoFrontend* recovery_hwsec);

  virtual ~CryptohomeRecoveryAuthBlockService() = default;

  // Initiate a recovery operation, generating the requests needed to execute
  // the actual authentication operation.
  virtual void GenerateRecoveryRequest(
      const ObfuscatedUsername& obfuscated_username,
      const cryptorecovery::RequestMetadata& request_metadata,
      const brillo::Blob& epoch_response,
      const CryptohomeRecoveryAuthBlockState& state,
      PreparedAuthFactorToken::Consumer on_done);

 private:
  // Token associated with a prepared auth request. The token exists to store
  // the output of the preparation process and it has no other active state.
  class Token : public PreparedAuthFactorToken {
   public:
    explicit Token(CryptohomeRecoveryPrepareOutput output);

   private:
    CryptohomeStatus TerminateAuthFactor() override;

    TerminateOnDestruction terminate_;
  };

  libstorage::Platform* platform_;
  const hwsec::RecoveryCryptoFrontend* recovery_hwsec_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_BLOCKS_CRYPTOHOME_RECOVERY_SERVICE_H_
