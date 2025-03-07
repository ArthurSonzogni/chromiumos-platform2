// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_FACTOR_MANAGER_H_
#define CRYPTOHOME_AUTH_FACTOR_MANAGER_H_

#include <string>

#include <absl/container/flat_hash_map.h>
#include <base/memory/weak_ptr.h>
#include <libhwsec-foundation/status/status_chain_or.h>
#include <libstorage/platform/platform.h>

#include "cryptohome/auth_blocks/auth_block_utility.h"
#include "cryptohome/auth_factor/auth_factor.h"
#include "cryptohome/auth_factor/map.h"
#include "cryptohome/auth_factor/type.h"
#include "cryptohome/auth_factor_vault_keyset_converter.h"
#include "cryptohome/error/cryptohome_error.h"
#include "cryptohome/keyset_management.h"
#include "cryptohome/user_secret_stash/manager.h"
#include "cryptohome/username.h"

namespace cryptohome {

// Manages the persistently stored auth factors.
//
// The basic assumption is that each factor has a unique label (among all
// factors configured for a given user).
class AuthFactorManager final {
 public:
  AuthFactorManager(libstorage::Platform* platform,
                    KeysetManagement* keyset_management,
                    UssManager* uss_manager);

  AuthFactorManager(const AuthFactorManager&) = delete;
  AuthFactorManager& operator=(const AuthFactorManager&) = delete;

  // ========= In-Memory AuthFactor Functions =========
  // Functions for loading and accessing the in-memory AuthFactor objects via
  // the per-user AuthFactorMap instances.

  // Returns a reference to the auth factor map for the given user. This may
  // load the factors from storage.
  //
  // The reference to the map itself is valid until a Discard function is called
  // to discard either this user's map or all map. However, as a general rule
  // callers should still avoid storing persistent references to the map.
  AuthFactorMap& GetAuthFactorMap(const ObfuscatedUsername& username);

  // Discard the in-memory map for an individual user, or for all users.
  void DiscardAuthFactorMap(const ObfuscatedUsername& username);
  void DiscardAllAuthFactorMaps();

  // ========= Stored AuthFactor functions =========
  // Functions for accessing and modifying the stored AuthFactor files.

  // Serializes and persists as a file the given auth factor in the user's data
  // vault.
  CryptohomeStatus SaveAuthFactorFile(
      const ObfuscatedUsername& obfuscated_username,
      const AuthFactor& auth_factor);

  // Deletes the file for the given auth factor in the user's data vault.
  CryptohomeStatus DeleteAuthFactorFile(
      const ObfuscatedUsername& obfuscated_username,
      const AuthFactor& auth_factor);

  // Loads from the auth factor with the given type and label from the file in
  // the user's data vault.
  CryptohomeStatusOr<AuthFactor> LoadAuthFactor(
      const ObfuscatedUsername& obfuscated_username,
      AuthFactorType auth_factor_type,
      const std::string& auth_factor_label);

  // Loads the list of configured auth factors from the user's data vault.
  absl::flat_hash_map<std::string, AuthFactorType> ListAuthFactors(
      const ObfuscatedUsername& obfuscated_username);

  // Removes the auth factor:
  // 1. Calls PrepareForRemoval() on the AuthBlock. A failure in
  // `PrepareForRemoval()` aborts the auth factor removal from disk.
  // 2. Removes the file containing state (AuthBlockState) of the given auth
  // factor from the user's data vault.
  void RemoveAuthFactor(const ObfuscatedUsername& obfuscated_username,
                        const AuthFactor& auth_factor,
                        AuthBlockUtility* auth_block_utility,
                        StatusCallback callback);

  // Updates the auth factor:
  // 1. Removes the auth factor with the given `auth_factor.type()` and
  // `auth_factor_label`.
  // 2. Saves the new auth factor on disk.
  // 3. Calls PrepareForRemoval() on the AuthBlock.
  // Unlike calling `RemoveAuthFactor()`+`SaveAuthFactorFile()`, this operation
  // is atomic, to the extent possible - it makes sure that we don't end up with
  // no auth factor available.
  void UpdateAuthFactor(const ObfuscatedUsername& obfuscated_username,
                        const std::string& auth_factor_label,
                        AuthFactor& auth_factor,
                        AuthBlockUtility* auth_block_utility,
                        StatusCallback callback);

  // Deletes the migrated fingerprint auth factors in the user's data vault.
  // Useful in ensuring a clean sheet before a re-migration of legacy
  // fingerprints.
  void RemoveMigratedFingerprintAuthFactors(
      const ObfuscatedUsername& obfuscated_username,
      AuthBlockUtility* auth_block_utility,
      StatusCallback callback);

 private:
  // Loads all configured auth factors for the given user from the disk. If any
  // factors are malformed they will be logged and skipped.
  AuthFactorMap LoadAllAuthFactors(
      const ObfuscatedUsername& obfuscated_username);

  // RemoveAuthFactorFiles removes files related to |auth_factor|
  // when passed-in |status| is ok. Any error status will be passed to
  // |callback|.
  void RemoveAuthFactorFiles(const ObfuscatedUsername& obfuscated_username,
                             const AuthFactor& auth_factor,
                             StatusCallback callback,
                             CryptohomeStatus status);

  // LogPrepareForRemovalStatus logs |status| if it is an error.
  // Any error status will be passed to |callback|.
  void LogPrepareForRemovalStatus(const ObfuscatedUsername& obfuscated_username,
                                  const AuthFactor& auth_factor,
                                  StatusCallback callback,
                                  CryptohomeStatus status);

  // ContinueRemoveAuthFactors removes the auth factor with |auth_factor_label|
  // from the in-memory map, then calls RemoveMigratedFingerprintAuthFactor,
  // if the passed-in |status| is ok. Otherwise, any error
  // in |status| will be passed to |callback| and the auth factors' removal
  // is aborted.
  void ContinueRemoveFpAuthFactors(
      const ObfuscatedUsername& obfuscated_username,
      const std::string& auth_factor_label,
      AuthBlockUtility* auth_block_utility,
      StatusCallback callback,
      CryptohomeStatus status);

  libstorage::Platform* const platform_;
  UssManager* const uss_manager_;

  // Converter used for VK -> AF conversion.
  AuthFactorVaultKeysetConverter converter_;

  // All loaded auth factor maps, per-user.
  absl::flat_hash_map<ObfuscatedUsername, AuthFactorMap> map_of_af_maps_;

  // The last member, to invalidate weak references first on destruction.
  base::WeakPtrFactory<AuthFactorManager> weak_factory_{this};
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_FACTOR_MANAGER_H_
