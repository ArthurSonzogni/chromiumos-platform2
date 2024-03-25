// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_BLOCKS_MOCK_CRYPTOHOME_RECOVERY_SERVICE_H_
#define CRYPTOHOME_AUTH_BLOCKS_MOCK_CRYPTOHOME_RECOVERY_SERVICE_H_

#include <utility>

#include <gmock/gmock.h>
#include <libhwsec/frontend/recovery_crypto/frontend.h>
#include <libstorage/platform/platform.h>

#include "cryptohome/auth_blocks/cryptohome_recovery_service.h"

namespace cryptohome {

class MockCryptohomeRecoveryAuthBlockService
    : public CryptohomeRecoveryAuthBlockService {
 public:
  MockCryptohomeRecoveryAuthBlockService(
      libstorage::Platform* platform,
      const hwsec::RecoveryCryptoFrontend* recovery_hwsec)
      : CryptohomeRecoveryAuthBlockService(platform, recovery_hwsec) {
    using ::testing::_;
    using ::testing::Invoke;
    // By default forward mock calls to the parent (real) implementation.
    ON_CALL(*this, GenerateRecoveryRequest(_, _, _, _, _))
        .WillByDefault([this](auto&&... args) {
          CryptohomeRecoveryAuthBlockService::GenerateRecoveryRequest(
              std::forward<decltype(args)>(args)...);
        });
  }

  MOCK_METHOD(void,
              GenerateRecoveryRequest,
              (const ObfuscatedUsername& obfuscated_username,
               const cryptorecovery::RequestMetadata& request_metadata,
               const brillo::Blob& epoch_response,
               const CryptohomeRecoveryAuthBlockState& state,
               PreparedAuthFactorToken::Consumer on_done),
              (override));
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_BLOCKS_MOCK_CRYPTOHOME_RECOVERY_SERVICE_H_
