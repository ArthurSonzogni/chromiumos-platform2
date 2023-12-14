// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_USER_SECRET_STASH_MIGRATOR_H_
#define CRYPTOHOME_USER_SECRET_STASH_MIGRATOR_H_

#include <memory>
#include <string>

#include <base/functional/bind.h>
#include <brillo/secure_blob.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>

#include "cryptohome/storage/file_system_keyset.h"
#include "cryptohome/user_secret_stash/manager.h"
#include "cryptohome/username.h"
#include "cryptohome/vault_keyset.h"

namespace cryptohome {

// This class object serves for migrating a user VaultKeyset to UserSecretStash
// and AuthFactor.
class UssMigrator {
 public:
  explicit UssMigrator(ObfuscatedUsername username);

  UssMigrator(const UssMigrator&) = delete;
  UssMigrator& operator=(const UssMigrator&) = delete;

  // Called upon completion of USS migration with the DecryptedUss token being
  // provided when successful and null otherwise.
  using CompletionCallback =
      base::OnceCallback<void(std::optional<UssManager::DecryptToken>)>;

  // The function that migrates the VaultKeyset with |label| and
  // |filesystem_keyset| to AuthFactor and USS.
  void MigrateVaultKeysetToUss(UssManager& uss_manager,
                               UserUssStorage& user_uss_storage,
                               const std::string& label,
                               const FileSystemKeyset& filesystem_keyset,
                               CompletionCallback completion_callback);

 private:
  // Generates migration secret from the filesystem keyset.
  void GenerateMigrationSecret(const FileSystemKeyset& filesystem_keyset);

  ObfuscatedUsername username_;
  std::unique_ptr<brillo::SecureBlob> migration_secret_;
};

}  // namespace cryptohome
#endif  // CRYPTOHOME_USER_SECRET_STASH_MIGRATOR_H_
