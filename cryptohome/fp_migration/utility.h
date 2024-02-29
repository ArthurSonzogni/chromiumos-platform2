
// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_FP_MIGRATION_UTILITY_H_
#define CRYPTOHOME_FP_MIGRATION_UTILITY_H_

#include <string>

#include "cryptohome/auth_blocks/biometrics_auth_block_service.h"
#include "cryptohome/crypto.h"
#include "cryptohome/error/cryptohome_error.h"
#include "cryptohome/key_objects.h"
#include "cryptohome/util/async_init.h"

namespace cryptohome {

// A utility class to interact with biometrics service on legacy
// fingerprint migration related functionalities.
class FpMigrationUtility final {
 public:
  using LegacyRecordsCallback =
      BiometricsAuthBlockService::LegacyRecordsCallback;

  // Helper to construct an auth factor label from a index value.
  // Legacy fingerprint migration utilizes this helper to derive
  // a label automatically.
  static std::string MigratedLegacyFpLabel(size_t index);

  FpMigrationUtility(Crypto* crypto,
                     AsyncInitPtr<BiometricsAuthBlockService> bio_service)
      : crypto_(crypto), bio_service_(bio_service) {}
  FpMigrationUtility(const FpMigrationUtility&) = delete;
  FpMigrationUtility& operator=(const FpMigrationUtility&) = delete;

  // Prepare a legacy fingerprint for later being added as an auth factor.
  // Unlike normal enrollment where a service session is established for user
  // input, the preparation of legacy fp completes as soon as biod finishes the
  // template loading. Returns through the asynchronous |callback|.
  void PrepareLegacyTemplate(const AuthInput& auth_input,
                             StatusCallback callback);

  // Calls BiometricsAuthBlockService::ListLegacyRecords. It returns a list of
  // legacy fingerprint records from biod's daemon store.
  void ListLegacyRecords(LegacyRecordsCallback callback);

 private:
  // Enrolls a legacy fp template through biod, with obtained |nonce|.
  // Intended as a callback for BiometricsAuthBlockService::GetNonce.
  void EnrollLegacyTemplate(StatusCallback callback,
                            const AuthInput& auth_input,
                            std::optional<brillo::Blob> nonce);

  Crypto* crypto_;
  // Biometrics service, used by operations that need to interact with biod.
  AsyncInitPtr<BiometricsAuthBlockService> bio_service_;

  base::WeakPtrFactory<FpMigrationUtility> weak_factory_{this};
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_FP_MIGRATION_UTILITY_H_
