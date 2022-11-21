// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_USS_MIGRATOR_H_

#define CRYPTOHOME_USS_MIGRATOR_H_

#include <memory>
#include <string>

#include <base/bind.h>
#include <brillo/secure_blob.h>

#include <cryptohome/proto_bindings/UserDataAuth.pb.h>
#include "cryptohome/error/cryptohome_error.h"
#include "cryptohome/user_secret_stash.h"
#include "cryptohome/user_secret_stash_storage.h"
#include "cryptohome/vault_keyset.h"

namespace cryptohome {
inline constexpr bool ShouldMigrateToUss() {
  return USE_USS_MIGRATION;
}

// This class object serves for migrating a user VaultKeyset to UserSecretStash
// and AuthFactor.
class UssMigrator {
 public:
  explicit UssMigrator(std::string username);
  UssMigrator(const UssMigrator&) = delete;
  UssMigrator& operator=(const UssMigrator&) = delete;

  // Completes the UserSecretStash migration by persisting AuthFactor to
  // UserSecretStash and converting the VaultKeyset to a backup VaultKeyset.
  using CompletionCallback = base::OnceCallback<void(
      std::unique_ptr<UserSecretStash> user_secret_stash,
      brillo::SecureBlob uss_main_key)>;

  // The function that migrates the VaultKeyset to AuthFactor and USS.
  // This function needs to be called during Authenticate operation after the
  // successful authentication of the VaultKeyset. Hence |vault_keyset|
  // is a VaultKeyset object with decrypted fields.
  void MigrateVaultKeysetToUss(
      const UserSecretStashStorage& user_secret_stash_storage,
      const VaultKeyset& vault_keyset,
      CompletionCallback completion_callback);

 private:
  // Generates migration secret from the filesystem keyset.
  void GenerateMigrationSecret(const VaultKeyset& vault_keyset);

  // Adds the migration secret as a |wrapped_key_block| to the given
  // user secret stash.
  bool AddMigrationSecretToUss(const brillo::SecureBlob& uss_main_key,
                               UserSecretStash& user_secret_stash);

  // Removes the |wrapped_key_block| corresponding to the migration secret from
  // the given user secret stash.
  bool RemoveMigrationSecretFromUss(UserSecretStash& user_secret_stash);

  std::string username_;
  std::unique_ptr<brillo::SecureBlob> migration_secret_;
};

}  // namespace cryptohome
#endif  // CRYPTOHOME_USS_MIGRATOR_H_
