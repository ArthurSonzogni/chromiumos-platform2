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
#include "cryptohome/user_secret_stash/storage.h"
#include "cryptohome/vault_keyset.h"

namespace cryptohome {

namespace {
constexpr char kLabel[] = "label";
constexpr char kPinLabel[] = "pin";
constexpr char kUser[] = "user";
constexpr char kMigrationSecretLabel[] = "vk_to_uss_migration_secret_label";

using ::base::test::TestFuture;
using ::brillo::cryptohome::home::SanitizeUserName;
using ::hwsec_foundation::error::testing::IsOk;
using ::testing::Contains;
using ::testing::NiceMock;
using ::testing::Not;

class UssMigratorTest : public ::testing::Test {
 protected:
  UssMigratorTest()
      : file_system_keyset_(FileSystemKeyset::CreateRandom()),
        username_(kUser),
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
    TestFuture<std::optional<DecryptedUss>> migrate_future;
    migrator_.MigrateVaultKeysetToUss(
        user_uss_storage_, vault_keyset->GetLabel(), file_system_keyset_,
        migrate_future.GetCallback());
    decrypted_uss_ = migrate_future.Take();
  }

  void CorruptUssAndResetState() {
    EXPECT_TRUE(platform_.DeleteFileDurable(UserSecretStashPath(
        SanitizeUserName(username_), kUserSecretStashDefaultSlot)));
    decrypted_uss_.reset();
    // Create an empty user_secret_stash.
    EXPECT_TRUE(platform_.TouchFileDurable(UserSecretStashPath(
        SanitizeUserName(username_), kUserSecretStashDefaultSlot)));
  }

  void RemoveMigrationSecretAndResetState() {
    auto transaction = decrypted_uss_->StartTransaction();
    EXPECT_THAT(transaction.RemoveWrappedMainKey(kMigrationSecretLabel),
                IsOk());
    EXPECT_THAT(std::move(transaction).Commit(user_uss_storage_), IsOk());
    EXPECT_THAT(decrypted_uss_->encrypted().WrappedMainKeyIds(),
                Not(Contains(kMigrationSecretLabel)));
    decrypted_uss_.reset();
  }

  void SetUp() override { GenerateVaultKeysets(); }

  base::test::SingleThreadTaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  FakePlatform platform_;
  FileSystemKeyset file_system_keyset_;
  std::optional<DecryptedUss> decrypted_uss_;
  UssStorage uss_storage_{&platform_};
  UserUssStorage user_uss_storage_{uss_storage_,
                                   SanitizeUserName(Username(kUser))};
  std::map<std::string, std::unique_ptr<VaultKeyset>> vk_map_;
  std::unique_ptr<VaultKeyset> pin_vault_keyset_;
  const Username username_;
  UssMigrator migrator_;
};

// Test that user secret stash is created by the migrator if there isn't any
// existing user secret stash for the user.
TEST_F(UssMigratorTest, UserSecretStashCreatedIfDoesntExist) {
  EXPECT_EQ(std::nullopt, decrypted_uss_);

  CallMigrator(kLabel);
  EXPECT_NE(std::nullopt, decrypted_uss_);
}

// Test that if there is an existing user secret stash migrator add to the same
// user secret stash.
TEST_F(UssMigratorTest, MigratorAppendToTheSameUserSecretStash) {
  CallMigrator(kLabel);
  EXPECT_THAT(decrypted_uss_->encrypted().WrappedMainKeyIds(),
              Contains(kMigrationSecretLabel));

  CallMigrator(kPinLabel);
  EXPECT_THAT(decrypted_uss_->encrypted().WrappedMainKeyIds(),
              Contains(kMigrationSecretLabel));

  EXPECT_NE(std::nullopt, decrypted_uss_);
}

// Test that corrupted user secret stash fails the migration.
TEST_F(UssMigratorTest, MigrationFailsIfUssCorrupted) {
  CallMigrator(kLabel);
  EXPECT_THAT(decrypted_uss_->encrypted().WrappedMainKeyIds(),
              Contains(kMigrationSecretLabel));

  CorruptUssAndResetState();

  CallMigrator(kPinLabel);
  EXPECT_EQ(std::nullopt, decrypted_uss_);
}

// Test that failure in obtaining migration secret block fails the migration.
TEST_F(UssMigratorTest, MigrationFailsIfThereIsUssButNoMigrationKey) {
  CallMigrator(kLabel);
  EXPECT_THAT(decrypted_uss_->encrypted().WrappedMainKeyIds(),
              Contains(kMigrationSecretLabel));

  RemoveMigrationSecretAndResetState();

  CallMigrator(kPinLabel);
  EXPECT_EQ(std::nullopt, decrypted_uss_);
}

}  // namespace

}  // namespace cryptohome
