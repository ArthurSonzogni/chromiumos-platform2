// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/user_secret_stash/migrator.h"

#include <map>
#include <memory>
#include <string>
#include <utility>

#include <base/files/file_path.h>
#include <base/files/scoped_temp_dir.h>
#include <base/functional/bind.h>
#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <brillo/cryptohome.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libhwsec-foundation/error/testing_helper.h>

#include "cryptohome/fake_platform.h"
#include "cryptohome/filesystem_layout.h"
#include "cryptohome/user_secret_stash/decrypted.h"
#include "cryptohome/user_secret_stash/manager.h"
#include "cryptohome/user_secret_stash/storage.h"
#include "cryptohome/username.h"
#include "cryptohome/vault_keyset.h"

namespace cryptohome {
namespace {

using ::base::test::TestFuture;
using ::brillo::cryptohome::home::SanitizeUserName;
using ::hwsec_foundation::error::testing::IsOk;
using ::testing::Contains;
using ::testing::NiceMock;
using ::testing::Not;

constexpr char kLabel[] = "label";
constexpr char kPinLabel[] = "pin";
constexpr char kUser[] = "user";
constexpr char kMigrationSecretLabel[] = "vk_to_uss_migration_secret_label";

class UssMigratorTest : public ::testing::Test {
 protected:
  UssMigratorTest()
      : username_(SanitizeUserName(Username(kUser))),
        file_system_keyset_(FileSystemKeyset::CreateRandom()),
        migrator_(UssMigrator(username_)) {}

  void GenerateVaultKeysets() {
    std::unique_ptr<VaultKeyset> vk = std::make_unique<VaultKeyset>();
    vk->CreateFromFileSystemKeyset(file_system_keyset_);
    KeyData key_data;
    key_data.set_label(kLabel);
    vk->SetKeyData(key_data);
    vk_map_.emplace(vk->GetLabel(), std::move(vk));

    std::unique_ptr<VaultKeyset> pin_vk = std::make_unique<VaultKeyset>();
    pin_vk->CreateFromFileSystemKeyset(file_system_keyset_);
    KeyData pin_key_data;
    pin_key_data.set_label(kPinLabel);
    pin_vk->SetKeyData(pin_key_data);
    vk_map_.emplace(pin_vk->GetLabel(), std::move(pin_vk));
  }

  void CallMigrator(std::string label) {
    auto iter = vk_map_.find(label);
    std::unique_ptr<VaultKeyset> vault_keyset = std::move(iter->second);
    TestFuture<std::optional<UssManager::DecryptToken>> migrate_future;
    migrator_.MigrateVaultKeysetToUss(
        uss_manager_, user_uss_storage_, vault_keyset->GetLabel(),
        file_system_keyset_, migrate_future.GetCallback());
    decrypt_token_ = migrate_future.Take();
  }

  void CorruptUssAndResetState() {
    EXPECT_TRUE(platform_.DeleteFileDurable(
        UserSecretStashPath(username_, kUserSecretStashDefaultSlot)));
    decrypt_token_.reset();
    EXPECT_THAT(uss_manager_.DiscardEncrypted(username_), IsOk());
    // Create an empty user_secret_stash.
    EXPECT_TRUE(platform_.TouchFileDurable(
        UserSecretStashPath(username_, kUserSecretStashDefaultSlot)));
  }

  void RemoveMigrationSecretAndResetState() {
    DecryptedUss& decrypted_uss = uss_manager_.GetDecrypted(*decrypt_token_);
    auto transaction = decrypted_uss.StartTransaction();
    EXPECT_THAT(transaction.RemoveWrappingId(kMigrationSecretLabel), IsOk());
    EXPECT_THAT(std::move(transaction).Commit(), IsOk());
    EXPECT_THAT(decrypted_uss.encrypted().WrappedMainKeyIds(),
                Not(Contains(kMigrationSecretLabel)));
    decrypt_token_.reset();
    EXPECT_THAT(uss_manager_.DiscardEncrypted(username_), IsOk());
  }

  void SetUp() override { GenerateVaultKeysets(); }

  base::test::SingleThreadTaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  FakePlatform platform_;
  const ObfuscatedUsername username_;

  FileSystemKeyset file_system_keyset_;
  UssStorage uss_storage_{&platform_};
  UssManager uss_manager_{uss_storage_};
  UserUssStorage user_uss_storage_{uss_storage_, username_};
  std::map<std::string, std::unique_ptr<VaultKeyset>> vk_map_;
  std::unique_ptr<VaultKeyset> pin_vault_keyset_;
  UssMigrator migrator_;

  std::optional<UssManager::DecryptToken> decrypt_token_;
};

// Test that user secret stash is created by the migrator if there isn't any
// existing user secret stash for the user.
TEST_F(UssMigratorTest, UserSecretStashCreatedIfDoesntExist) {
  EXPECT_EQ(std::nullopt, decrypt_token_);

  CallMigrator(kLabel);
  EXPECT_NE(std::nullopt, decrypt_token_);
}

// Test that if there is an existing user secret stash migrator add to the same
// user secret stash.
TEST_F(UssMigratorTest, MigratorAppendToTheSameUserSecretStash) {
  CallMigrator(kLabel);
  EXPECT_THAT(uss_manager_.GetDecrypted(*decrypt_token_)
                  .encrypted()
                  .WrappedMainKeyIds(),
              Contains(kMigrationSecretLabel));

  CallMigrator(kPinLabel);
  EXPECT_THAT(uss_manager_.GetDecrypted(*decrypt_token_)
                  .encrypted()
                  .WrappedMainKeyIds(),
              Contains(kMigrationSecretLabel));
}

// Test that user secret stash is created by the migrator if there is an
// existing user secret stash but it is corrupt and can't be loaded.
TEST_F(UssMigratorTest, UserSecretStashCreatedIfUssCorrupted) {
  CallMigrator(kLabel);
  EXPECT_THAT(uss_manager_.GetDecrypted(*decrypt_token_)
                  .encrypted()
                  .WrappedMainKeyIds(),
              Contains(kMigrationSecretLabel));

  CorruptUssAndResetState();

  EXPECT_EQ(std::nullopt, decrypt_token_);
  CallMigrator(kPinLabel);
  EXPECT_NE(std::nullopt, decrypt_token_);
}

// Test that failure in obtaining migration secret block fails the migration.
TEST_F(UssMigratorTest, MigrationFailsIfThereIsUssButNoMigrationKey) {
  CallMigrator(kLabel);
  EXPECT_THAT(uss_manager_.GetDecrypted(*decrypt_token_)
                  .encrypted()
                  .WrappedMainKeyIds(),
              Contains(kMigrationSecretLabel));

  RemoveMigrationSecretAndResetState();

  CallMigrator(kPinLabel);
  EXPECT_EQ(std::nullopt, decrypt_token_);
}

}  // namespace

}  // namespace cryptohome
