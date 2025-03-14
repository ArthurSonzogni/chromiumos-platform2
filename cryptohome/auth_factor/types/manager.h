// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_FACTOR_TYPES_MANAGER_H_
#define CRYPTOHOME_AUTH_FACTOR_TYPES_MANAGER_H_

#include <memory>

#include <absl/container/flat_hash_map.h>
#include <libstorage/platform/platform.h>

#include "cryptohome/auth_blocks/biometrics_auth_block_service.h"
#include "cryptohome/auth_blocks/cryptorecovery/service.h"
#include "cryptohome/auth_blocks/fp_service.h"
#include "cryptohome/auth_factor/type.h"
#include "cryptohome/auth_factor/types/interface.h"
#include "cryptohome/challenge_credentials/challenge_credentials_helper.h"
#include "cryptohome/crypto.h"
#include "cryptohome/features.h"
#include "cryptohome/key_challenge_service_factory.h"
#include "cryptohome/user_secret_stash/manager.h"
#include "cryptohome/util/async_init.h"

namespace cryptohome {

// Manager class that will construct all of the auth factor driver instances.
// This will only construct one instance of the driver for each type and so
// multiple lookups of the driver will return the same object, shared between
// all of them.
class AuthFactorDriverManager {
 public:
  AuthFactorDriverManager(
      libstorage::Platform* platform,
      Crypto* crypto,
      UssManager* uss_manager,
      AsyncInitPtr<ChallengeCredentialsHelper> challenge_credentials_helper,
      KeyChallengeServiceFactory* key_challenge_service_factory,
      CryptohomeRecoveryAuthBlockService* cr_service,
      FingerprintAuthBlockService* fp_service,
      AsyncInitPtr<BiometricsAuthBlockService> bio_service,
      AsyncInitFeatures* features);

  AuthFactorDriverManager(const AuthFactorDriverManager&) = delete;
  AuthFactorDriverManager& operator=(const AuthFactorDriverManager&) = delete;

  // Return a reference to the driver for the given factor type. The references
  // returned are valid until the driver manager itself is destroyed.
  AuthFactorDriver& GetDriver(AuthFactorType auth_factor_type);
  const AuthFactorDriver& GetDriver(AuthFactorType auth_factor_type) const;

 private:
  // The null driver, used when no valid driver implementation is available.
  const std::unique_ptr<AuthFactorDriver> null_driver_;

  // Store all of the real drivers.
  const absl::flat_hash_map<AuthFactorType, std::unique_ptr<AuthFactorDriver>>
      driver_map_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_FACTOR_TYPES_MANAGER_H_
