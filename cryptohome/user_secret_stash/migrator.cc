// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/user_secret_stash/migrator.h"

#include <memory>
#include <string>
#include <utility>

#include <base/functional/bind.h>
#include <brillo/cryptohome.h>
#include <brillo/secure_blob.h>
#include <libhwsec-foundation/crypto/hmac.h>
#include <libhwsec-foundation/crypto/sha.h>

#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/error/cryptohome_error.h"
#include "cryptohome/error/reap.h"
#include "cryptohome/storage/file_system_keyset.h"
#include "cryptohome/user_secret_stash/decrypted.h"
#include "cryptohome/user_secret_stash/storage.h"
#include "cryptohome/vault_keyset.h"

namespace cryptohome {
namespace {

using ::cryptohome::error::CryptohomeError;
using ::cryptohome::error::ReapAndReportError;
using ::hwsec::StatusOr;
using ::hwsec_foundation::HmacSha256;
using ::hwsec_foundation::status::OkStatus;

constexpr char kMigrationSecretDerivationPublicInfo[] =
    "CHROMEOS_USS_MIGRATION_SECRET";
constexpr char kMigrationSecretLabel[] = "vk_to_uss_migration_secret_label";

}  // namespace

UssMigrator::UssMigrator(ObfuscatedUsername username)
    : username_(std::move(username)) {}

void UssMigrator::MigrateVaultKeysetToUss(
    UssManager& uss_manager,
    UserUssStorage& user_uss_storage,
    const std::string& label,
    const FileSystemKeyset& filesystem_keyset,
    CompletionCallback completion_callback) {
  // Create migration secret.
  GenerateMigrationSecret(filesystem_keyset);

  // Get the existing UserSecretStash and the main key if it exists, generate a
  // new UserSecretStash otherwise. This UserSecretStash will contain only one
  // key_block, with migration secret. The other key_blocks are added as the
  // credentials are migrated to AuthFactors and USS.

  // Load the USS container with the encrypted payload.
  std::optional<UssManager::DecryptToken> decrypt_token;
  if (!uss_manager.LoadEncrypted(username_).ok()) {
    // If no USS for the user can be loaded at all create a new UserSecretStash
    // from the passed VaultKeyset and add the migration_secret block.
    auto new_uss = DecryptedUss::CreateWithRandomMainKey(user_uss_storage,
                                                         filesystem_keyset);
    if (!new_uss.ok()) {
      LOG(ERROR) << "UserSecretStash creation failed during migration of "
                    "VaultKeyset with label: "
                 << label;
      ReapAndReportError(std::move(new_uss).status(),
                         kCryptohomeErrorUssMigrationErrorBucket);
      ReportVkToUssMigrationStatus(VkToUssMigrationStatus::kFailedUssCreation);
      std::move(completion_callback).Run(std::nullopt);
      return;
    }
    auto new_token = uss_manager.AddDecrypted(username_, std::move(*new_uss));
    if (!new_token.ok()) {
      LOG(ERROR) << "UserSecretStash addition failed during migration of "
                    "VaultKeyset with label: "
                 << label;
      ReapAndReportError(std::move(new_token).status(),
                         kCryptohomeErrorUssMigrationErrorBucket);
      ReportVkToUssMigrationStatus(VkToUssMigrationStatus::kFailedUssCreation);
      std::move(completion_callback).Run(std::nullopt);
      return;
    }
    {
      DecryptedUss& decrypted_uss = uss_manager.GetDecrypted(*new_token);
      auto transaction = decrypted_uss.StartTransaction();
      if (auto status = transaction.InsertWrappedMainKey(kMigrationSecretLabel,
                                                         *migration_secret_);
          !status.ok()) {
        LOG(ERROR)
            << "Failed to add the migration secret to the UserSecretStash.";
        ReapAndReportError(std::move(status),
                           kCryptohomeErrorUssMigrationErrorBucket);
        ReportVkToUssMigrationStatus(
            VkToUssMigrationStatus::kFailedAddingMigrationSecret);
        std::move(completion_callback).Run(std::nullopt);
        return;
      }
      if (auto status = std::move(transaction).Commit(); !status.ok()) {
        LOG(ERROR) << "Failed to persist the new UserSecretStash.";
        ReapAndReportError(std::move(status),
                           kCryptohomeErrorUssMigrationErrorBucket);
        ReportVkToUssMigrationStatus(
            VkToUssMigrationStatus::kFailedAddingMigrationSecret);
        std::move(completion_callback).Run(std::nullopt);
        return;
      }
    }
    decrypt_token = std::move(*new_token);
  } else {
    // Decrypt the existing UserSecretStash payload with the migration secret
    // and obtain the main key.
    auto existing_token = uss_manager.LoadDecrypted(
        username_, kMigrationSecretLabel, *migration_secret_);
    if (!existing_token.ok()) {
      LOG(ERROR) << "Failed to decrypt the UserSecretStash during migration.";
      ReapAndReportError(std::move(existing_token).status(),
                         kCryptohomeErrorUssMigrationErrorBucket);
      ReportVkToUssMigrationStatus(VkToUssMigrationStatus::kFailedUssDecrypt);
      std::move(completion_callback).Run(std::nullopt);
      return;
    }
    decrypt_token = std::move(*existing_token);
  }
  std::move(completion_callback).Run(std::move(decrypt_token));
}

void UssMigrator::GenerateMigrationSecret(
    const FileSystemKeyset& filesystem_keyset) {
  migration_secret_ = std::make_unique<brillo::SecureBlob>(
      HmacSha256(brillo::SecureBlob::Combine(filesystem_keyset.Key().fek,
                                             filesystem_keyset.Key().fnek),
                 brillo::BlobFromString(kMigrationSecretDerivationPublicInfo)));
}

}  // namespace cryptohome
