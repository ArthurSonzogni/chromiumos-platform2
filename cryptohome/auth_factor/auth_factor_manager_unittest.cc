// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <absl/types/variant.h>
#include <base/functional/callback_forward.h>
#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <brillo/secure_blob.h>
#include <gtest/gtest.h>
#include <libhwsec-foundation/error/testing_helper.h>

#include "cryptohome/auth_blocks/mock_auth_block_utility.h"
#include "cryptohome/auth_factor/auth_factor.h"
#include "cryptohome/auth_factor/auth_factor_manager.h"
#include "cryptohome/auth_factor/auth_factor_metadata.h"
#include "cryptohome/auth_factor/auth_factor_type.h"
#include "cryptohome/filesystem_layout.h"
#include "cryptohome/flatbuffer_schemas/auth_block_state.h"
#include "cryptohome/flatbuffer_schemas/auth_block_state_test_utils.h"
#include "cryptohome/mock_keyset_management.h"
#include "cryptohome/mock_platform.h"
#include "cryptohome/user_secret_stash/user_secret_stash.h"

namespace cryptohome {
namespace {

using ::base::test::TestFuture;
using ::brillo::SecureBlob;
using ::brillo::cryptohome::home::SanitizeUserName;
using ::cryptohome::error::CryptohomeError;
using ::hwsec_foundation::error::testing::IsOk;
using ::hwsec_foundation::status::MakeStatus;
using ::hwsec_foundation::status::OkStatus;
using ::hwsec_foundation::status::StatusChain;
using ::testing::_;
using ::testing::DoAll;
using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::NiceMock;
using ::testing::Not;
using ::testing::Pair;
using ::testing::Return;
using ::testing::StrictMock;

constexpr char kSomeIdpLabel[] = "some-idp";
constexpr char kChromeosVersion[] = "a.b.c_1_2_3";
constexpr char kChromeVersion[] = "a.b.c.d";

AuthBlockState CreatePasswordAuthBlockState(const std::string& suffix = "") {
  TpmBoundToPcrAuthBlockState tpm_bound_to_pcr_auth_block_state = {
      .scrypt_derived = false,
      .salt = SecureBlob("fake salt " + suffix),
      .tpm_key = SecureBlob("fake tpm key " + suffix),
      .extended_tpm_key = SecureBlob("fake extended tpm key " + suffix),
      .tpm_public_key_hash = SecureBlob("fake tpm public key hash"),
  };
  AuthBlockState auth_block_state = {.state =
                                         tpm_bound_to_pcr_auth_block_state};
  return auth_block_state;
}

AuthFactor CreatePasswordAuthFactor() {
  AuthFactorMetadata metadata = {
      .common =
          auth_factor::CommonMetadata{
              .chromeos_version_last_updated = kChromeosVersion,
              .chrome_version_last_updated = kChromeVersion,
          },
      .metadata = auth_factor::PasswordMetadata()};
  return AuthFactor(AuthFactorType::kPassword, kSomeIdpLabel, metadata,
                    CreatePasswordAuthBlockState());
}

class AuthFactorManagerTest : public ::testing::Test {
 protected:
  const ObfuscatedUsername kObfuscatedUsername{"obfuscated1"};

  MockPlatform platform_;
  AuthFactorManager auth_factor_manager_{&platform_};
  base::test::SingleThreadTaskEnvironment task_environment_ = {
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  scoped_refptr<base::SequencedTaskRunner> task_runner_ =
      base::SequencedTaskRunner::GetCurrentDefault();
};

// Test the `SaveAuthFactorFile()` method correctly serializes the factor into a
// file.
TEST_F(AuthFactorManagerTest, Save) {
  AuthFactor auth_factor = CreatePasswordAuthFactor();

  // Persist the auth factor.
  EXPECT_TRUE(
      auth_factor_manager_.SaveAuthFactorFile(kObfuscatedUsername, auth_factor)
          .ok());
  EXPECT_TRUE(platform_.FileExists(
      AuthFactorPath(kObfuscatedUsername,
                     /*auth_factor_type_string=*/"password", kSomeIdpLabel)));

  // Load the auth factor and verify it's the same.
  CryptohomeStatusOr<AuthFactor> loaded_auth_factor =
      auth_factor_manager_.LoadAuthFactor(
          kObfuscatedUsername, AuthFactorType::kPassword, kSomeIdpLabel);
  ASSERT_TRUE(loaded_auth_factor.ok());
  EXPECT_EQ(loaded_auth_factor->type(), AuthFactorType::kPassword);
  EXPECT_EQ(loaded_auth_factor->label(), kSomeIdpLabel);
  EXPECT_EQ(loaded_auth_factor->metadata().common.chromeos_version_last_updated,
            kChromeosVersion);
  EXPECT_EQ(loaded_auth_factor->metadata().common.chrome_version_last_updated,
            kChromeVersion);
  EXPECT_TRUE(absl::holds_alternative<auth_factor::PasswordMetadata>(
      loaded_auth_factor->metadata().metadata));
  EXPECT_EQ(auth_factor.auth_block_state(),
            loaded_auth_factor->auth_block_state());
  // TODO(b/204441443): Check other fields too. Consider using a GTest matcher.
}

// Test the `SaveAuthFactorFile()` method fails when the label is empty.
TEST_F(AuthFactorManagerTest, SaveBadEmptyLabel) {
  // Create an auth factor as a clone of a correct object, but with an empty
  // label.
  AuthFactor good_auth_factor = CreatePasswordAuthFactor();
  AuthFactor bad_auth_factor(good_auth_factor.type(),
                             /*label=*/std::string(),
                             good_auth_factor.metadata(),
                             good_auth_factor.auth_block_state());

  // Verify the manager refuses to save this auth factor.
  EXPECT_FALSE(auth_factor_manager_
                   .SaveAuthFactorFile(kObfuscatedUsername, bad_auth_factor)
                   .ok());
}

// Test the `SaveAuthFactorFile()` method fails when the label contains
// forbidden characters.
TEST_F(AuthFactorManagerTest, SaveBadMalformedLabel) {
  // Create an auth factor as a clone of a correct object, but with a malformed
  // label.
  AuthFactor good_auth_factor = CreatePasswordAuthFactor();
  AuthFactor bad_auth_factor(good_auth_factor.type(),
                             /*label=*/"foo.' bar'",
                             good_auth_factor.metadata(),
                             good_auth_factor.auth_block_state());

  // Verify the manager refuses to save this auth factor.
  EXPECT_FALSE(auth_factor_manager_
                   .SaveAuthFactorFile(kObfuscatedUsername, bad_auth_factor)
                   .ok());
}

// Test that `ListAuthFactors()` returns an empty map when there's no auth
// factor added.
TEST_F(AuthFactorManagerTest, ListEmpty) {
  AuthFactorManager::LabelToTypeMap factor_map =
      auth_factor_manager_.ListAuthFactors(kObfuscatedUsername);
  EXPECT_THAT(factor_map, IsEmpty());
}

// Test that `ListAuthFactors()` returns the auth factor that was added.
TEST_F(AuthFactorManagerTest, ListSingle) {
  // Create the auth factor file.
  AuthFactor auth_factor = CreatePasswordAuthFactor();
  EXPECT_TRUE(
      auth_factor_manager_.SaveAuthFactorFile(kObfuscatedUsername, auth_factor)
          .ok());

  // Verify the factor is listed.
  AuthFactorManager::LabelToTypeMap factor_map =
      auth_factor_manager_.ListAuthFactors(kObfuscatedUsername);
  EXPECT_THAT(factor_map,
              ElementsAre(Pair(kSomeIdpLabel, AuthFactorType::kPassword)));
}

// Test that `ListAuthFactors()` ignores an auth factor without a file name
// extension (and hence without a label).
TEST_F(AuthFactorManagerTest, ListBadNoExtension) {
  // Create files with correct and malformed names.
  platform_.WriteFile(AuthFactorsDirPath(kObfuscatedUsername)
                          .Append("password")
                          .AddExtension(kSomeIdpLabel),
                      /*blob=*/{});
  platform_.WriteFile(
      AuthFactorsDirPath(kObfuscatedUsername).Append("password"), /*blob=*/{});

  // Verify the malformed file is ignored, and the good one is still listed.
  AuthFactorManager::LabelToTypeMap factor_map =
      auth_factor_manager_.ListAuthFactors(kObfuscatedUsername);
  EXPECT_THAT(factor_map,
              ElementsAre(Pair(kSomeIdpLabel, AuthFactorType::kPassword)));
}

// Test that `ListAuthFactors()` ignores an auth factor with an empty file name
// extension (and hence without a label).
TEST_F(AuthFactorManagerTest, ListBadEmptyExtension) {
  // Create files with correct and malformed names.
  platform_.WriteFile(AuthFactorsDirPath(kObfuscatedUsername)
                          .Append("password")
                          .AddExtension(kSomeIdpLabel),
                      /*blob=*/{});
  platform_.WriteFile(
      AuthFactorsDirPath(kObfuscatedUsername).Append("password."), /*blob=*/{});

  // Verify the malformed file is ignored, and the good one is still listed.
  AuthFactorManager::LabelToTypeMap factor_map =
      auth_factor_manager_.ListAuthFactors(kObfuscatedUsername);
  EXPECT_THAT(factor_map,
              ElementsAre(Pair(kSomeIdpLabel, AuthFactorType::kPassword)));
}

// Test that `ListAuthFactors()` ignores an auth factor with multiple file name
// extensions (and hence with an incorrect label).
TEST_F(AuthFactorManagerTest, ListBadMultipleExtensions) {
  // Create files with correct and malformed names.
  platform_.WriteFile(AuthFactorsDirPath(kObfuscatedUsername)
                          .Append("password")
                          .AddExtension(kSomeIdpLabel),
                      /*blob=*/{});
  platform_.WriteFile(
      AuthFactorsDirPath(kObfuscatedUsername).Append("password.label.garbage"),
      /*blob=*/{});
  platform_.WriteFile(
      AuthFactorsDirPath(kObfuscatedUsername).Append("password.tar.gz"),
      /*blob=*/{});

  // Verify the malformed files are ignored, and the good one is still listed.
  AuthFactorManager::LabelToTypeMap factor_map =
      auth_factor_manager_.ListAuthFactors(kObfuscatedUsername);
  EXPECT_THAT(factor_map,
              ElementsAre(Pair(kSomeIdpLabel, AuthFactorType::kPassword)));
}

// Test that `ListAuthFactors()` ignores an auth factor with the file name
// consisting of just an extension (and hence without a factor type).
TEST_F(AuthFactorManagerTest, ListBadEmptyType) {
  // Create files with correct and malformed names.
  platform_.WriteFile(AuthFactorsDirPath(kObfuscatedUsername)
                          .Append("password")
                          .AddExtension(kSomeIdpLabel),
                      /*blob=*/{});
  platform_.WriteFile(AuthFactorsDirPath(kObfuscatedUsername).Append(".label"),
                      /*blob=*/{});

  // Verify the malformed file is ignored, and the good one is still listed.
  AuthFactorManager::LabelToTypeMap factor_map =
      auth_factor_manager_.ListAuthFactors(kObfuscatedUsername);
  EXPECT_THAT(factor_map,
              ElementsAre(Pair(kSomeIdpLabel, AuthFactorType::kPassword)));
}

// Test that `ListAuthFactors()` ignores an auth factor whose file name has a
// garbage instead of the factor type.
TEST_F(AuthFactorManagerTest, ListBadUnknownType) {
  // Create files with correct and malformed names.
  platform_.WriteFile(AuthFactorsDirPath(kObfuscatedUsername)
                          .Append("password")
                          .AddExtension(kSomeIdpLabel),
                      /*blob=*/{});
  platform_.WriteFile(
      AuthFactorsDirPath(kObfuscatedUsername).Append("fancytype.label"),
      /*blob=*/{});

  // Verify the malformed file is ignored, and the good one is still listed.
  AuthFactorManager::LabelToTypeMap factor_map =
      auth_factor_manager_.ListAuthFactors(kObfuscatedUsername);
  EXPECT_THAT(factor_map,
              ElementsAre(Pair(kSomeIdpLabel, AuthFactorType::kPassword)));
}

// TODO(b:208348570): Test clash of labels once more than one factor type is
// supported by AuthFactorManager.

TEST_F(AuthFactorManagerTest, RemoveSuccess) {
  AuthFactor auth_factor = CreatePasswordAuthFactor();

  // Persist the auth factor.
  EXPECT_THAT(
      auth_factor_manager_.SaveAuthFactorFile(kObfuscatedUsername, auth_factor),
      IsOk());
  CryptohomeStatusOr<AuthFactor> loaded_auth_factor =
      auth_factor_manager_.LoadAuthFactor(
          kObfuscatedUsername, AuthFactorType::kPassword, kSomeIdpLabel);
  EXPECT_THAT(loaded_auth_factor, IsOk());

  NiceMock<MockAuthBlockUtility> auth_block_utility;

  // Delete auth factor.
  TestFuture<CryptohomeStatus> remove_result;
  auth_factor_manager_.RemoveAuthFactor(kObfuscatedUsername, auth_factor,
                                        &auth_block_utility,
                                        remove_result.GetCallback());
  EXPECT_TRUE(remove_result.IsReady());
  EXPECT_THAT(remove_result.Take(), IsOk());

  // Try to load the auth factor.
  CryptohomeStatusOr<AuthFactor> loaded_auth_factor_1 =
      auth_factor_manager_.LoadAuthFactor(
          kObfuscatedUsername, AuthFactorType::kPassword, kSomeIdpLabel);
  EXPECT_THAT(loaded_auth_factor_1, Not(IsOk()));
  EXPECT_FALSE(platform_.FileExists(
      AuthFactorPath(kObfuscatedUsername,
                     /*auth_factor_type_string=*/"password", kSomeIdpLabel)
          .AddExtension(cryptohome::kChecksumExtension)));
}

TEST_F(AuthFactorManagerTest, RemoveFailureWithAuthBlock) {
  const CryptohomeError::ErrorLocationPair
      error_location_for_testing_auth_factor =
          CryptohomeError::ErrorLocationPair(
              static_cast<::cryptohome::error::CryptohomeError::ErrorLocation>(
                  1),
              std::string("MockErrorLocationAuthFactor"));

  AuthFactor auth_factor = CreatePasswordAuthFactor();

  // Persist the auth factor.
  EXPECT_THAT(
      auth_factor_manager_.SaveAuthFactorFile(kObfuscatedUsername, auth_factor),
      IsOk());
  CryptohomeStatusOr<AuthFactor> loaded_auth_factor =
      auth_factor_manager_.LoadAuthFactor(
          kObfuscatedUsername, AuthFactorType::kPassword, kSomeIdpLabel);
  EXPECT_THAT(loaded_auth_factor, IsOk());

  NiceMock<MockAuthBlockUtility> auth_block_utility;

  // Intentionally fail the PrepareAuthBlockForRemoval for password factor.
  EXPECT_CALL(auth_block_utility, PrepareAuthBlockForRemoval(_, _, _))
      .WillOnce([&](const ObfuscatedUsername& obfuscated_username,
                    const AuthBlockState& auth_state,
                    AuthBlockUtility::CryptohomeStatusCallback callback) {
        std::move(callback).Run(MakeStatus<error::CryptohomeCryptoError>(
            error_location_for_testing_auth_factor,
            error::ErrorActionSet(
                {error::PossibleAction::kDevCheckUnexpectedState}),
            CryptoError::CE_OTHER_CRYPTO));
      });

  // Try to delete auth factor.
  TestFuture<CryptohomeStatus> remove_result;
  auth_factor_manager_.RemoveAuthFactor(kObfuscatedUsername, auth_factor,
                                        &auth_block_utility,
                                        remove_result.GetCallback());
  EXPECT_TRUE(remove_result.IsReady());
  EXPECT_THAT(remove_result.Take(), Not(IsOk()));
}

TEST_F(AuthFactorManagerTest, RemoveFailureWithFactorFile) {
  AuthFactor auth_factor = CreatePasswordAuthFactor();

  // Persist the auth factor.
  EXPECT_THAT(
      auth_factor_manager_.SaveAuthFactorFile(kObfuscatedUsername, auth_factor),
      IsOk());
  CryptohomeStatusOr<AuthFactor> loaded_auth_factor =
      auth_factor_manager_.LoadAuthFactor(
          kObfuscatedUsername, AuthFactorType::kPassword, kSomeIdpLabel);
  EXPECT_THAT(loaded_auth_factor, IsOk());

  NiceMock<MockAuthBlockUtility> auth_block_utility;

  // Intentionally fail the auth factor file removal.
  auto auth_factor_file_path =
      AuthFactorPath(kObfuscatedUsername,
                     /*auth_factor_type_string=*/"password", kSomeIdpLabel);
  EXPECT_CALL(platform_, DeleteFileSecurely(auth_factor_file_path))
      .WillOnce(Return(false));
  EXPECT_CALL(platform_, DeleteFile(auth_factor_file_path))
      .WillOnce(Return(false));

  // Try to delete auth factor.
  TestFuture<CryptohomeStatus> remove_result;
  auth_factor_manager_.RemoveAuthFactor(kObfuscatedUsername, auth_factor,
                                        &auth_block_utility,
                                        remove_result.GetCallback());
  EXPECT_TRUE(remove_result.IsReady());
  EXPECT_THAT(remove_result.Take(), Not(IsOk()));
}

TEST_F(AuthFactorManagerTest, RemoveOkWithChecksumFileRemovalFailure) {
  AuthFactor auth_factor = CreatePasswordAuthFactor();

  // Persist the auth factor.
  EXPECT_THAT(
      auth_factor_manager_.SaveAuthFactorFile(kObfuscatedUsername, auth_factor),
      IsOk());
  CryptohomeStatusOr<AuthFactor> loaded_auth_factor =
      auth_factor_manager_.LoadAuthFactor(
          kObfuscatedUsername, AuthFactorType::kPassword, kSomeIdpLabel);
  EXPECT_THAT(loaded_auth_factor, IsOk());

  NiceMock<MockAuthBlockUtility> auth_block_utility;

  auto auth_factor_file_path =
      AuthFactorPath(kObfuscatedUsername,
                     /*auth_factor_type_string=*/"password", kSomeIdpLabel);
  auto auth_factor_checksum_file_path =
      auth_factor_file_path.AddExtension(kChecksumExtension);
  // Write out a checksum file. These are no longer automatically produced and
  // so to test it not being removed we need to manually create it.
  ASSERT_TRUE(platform_.TouchFileDurable(auth_factor_checksum_file_path));

  // Removes the auth factor file.
  EXPECT_CALL(platform_, DeleteFileSecurely(auth_factor_file_path))
      .WillOnce(Return(true));
  // Intentionally fail the auth factor checksum removal.
  EXPECT_CALL(platform_, DeleteFileSecurely(auth_factor_checksum_file_path))
      .WillOnce(Return(false));
  EXPECT_CALL(platform_, DeleteFile(auth_factor_checksum_file_path))
      .WillOnce(Return(false));

  // Try to delete auth factor and it should still succeed.
  TestFuture<CryptohomeStatus> remove_result;
  auth_factor_manager_.RemoveAuthFactor(kObfuscatedUsername, auth_factor,
                                        &auth_block_utility,
                                        remove_result.GetCallback());
  EXPECT_TRUE(remove_result.IsReady());
  EXPECT_THAT(remove_result.Take(), IsOk());
  EXPECT_TRUE(platform_.FileExists(auth_factor_checksum_file_path));
}

TEST_F(AuthFactorManagerTest, Update) {
  NiceMock<MockAuthBlockUtility> auth_block_utility;
  AuthFactor auth_factor = CreatePasswordAuthFactor();
  // Persist the auth factor.
  EXPECT_TRUE(
      auth_factor_manager_.SaveAuthFactorFile(kObfuscatedUsername, auth_factor)
          .ok());
  EXPECT_TRUE(platform_.FileExists(
      AuthFactorPath(kObfuscatedUsername,
                     /*auth_factor_type_string=*/"password", kSomeIdpLabel)));

  // Load the auth factor and verify it's the same.
  CryptohomeStatusOr<AuthFactor> loaded_auth_factor =
      auth_factor_manager_.LoadAuthFactor(
          kObfuscatedUsername, AuthFactorType::kPassword, kSomeIdpLabel);
  ASSERT_TRUE(loaded_auth_factor.ok());
  EXPECT_EQ(loaded_auth_factor->auth_block_state(),
            auth_factor.auth_block_state());

  AuthBlockState new_state = CreatePasswordAuthBlockState("new auth factor");
  AuthFactor new_auth_factor(auth_factor.type(), auth_factor.label(),
                             auth_factor.metadata(), new_state);
  TestFuture<CryptohomeStatus> update_result;
  // Update the auth factor.
  auth_factor_manager_.UpdateAuthFactor(
      kObfuscatedUsername, auth_factor.label(), new_auth_factor,
      &auth_block_utility, update_result.GetCallback());
  EXPECT_TRUE(update_result.IsReady());
  EXPECT_THAT(update_result.Take(), IsOk());
  EXPECT_TRUE(platform_.FileExists(
      AuthFactorPath(kObfuscatedUsername,
                     /*auth_factor_type_string=*/"password", kSomeIdpLabel)));

  // Load the auth factor and verify it's the same.
  CryptohomeStatusOr<AuthFactor> loaded_auth_factor_1 =
      auth_factor_manager_.LoadAuthFactor(
          kObfuscatedUsername, AuthFactorType::kPassword, kSomeIdpLabel);
  ASSERT_TRUE(loaded_auth_factor_1.ok());
  EXPECT_EQ(loaded_auth_factor_1->auth_block_state(), new_state);
  EXPECT_NE(loaded_auth_factor_1->auth_block_state(),
            auth_factor.auth_block_state());
}

// Test that UpdateAuthFactor fails if the removal of
// the old auth block state failed.
TEST_F(AuthFactorManagerTest, UpdateFailureWithRemoval) {
  NiceMock<MockAuthBlockUtility> auth_block_utility;
  // Intentionally fail the PrepareAuthBlockForRemoval for password factor.
  const CryptohomeError::ErrorLocationPair
      error_location_for_testing_auth_factor =
          CryptohomeError::ErrorLocationPair(
              static_cast<::cryptohome::error::CryptohomeError::ErrorLocation>(
                  1),
              std::string("MockErrorLocationAuthFactor"));
  EXPECT_CALL(auth_block_utility, PrepareAuthBlockForRemoval(_, _, _))
      .WillOnce([&](const ObfuscatedUsername& obfuscated_username,
                    const AuthBlockState& auth_state,
                    AuthBlockUtility::CryptohomeStatusCallback callback) {
        std::move(callback).Run(MakeStatus<error::CryptohomeCryptoError>(
            error_location_for_testing_auth_factor,
            error::ErrorActionSet(
                {error::PossibleAction::kDevCheckUnexpectedState}),
            CryptoError::CE_OTHER_CRYPTO));
      });
  AuthFactor auth_factor = CreatePasswordAuthFactor();
  // Persist the auth factor.
  EXPECT_TRUE(
      auth_factor_manager_.SaveAuthFactorFile(kObfuscatedUsername, auth_factor)
          .ok());
  EXPECT_TRUE(platform_.FileExists(
      AuthFactorPath(kObfuscatedUsername,
                     /*auth_factor_type_string=*/"password", kSomeIdpLabel)));

  // Load the auth factor and verify it's the same.
  CryptohomeStatusOr<AuthFactor> loaded_auth_factor =
      auth_factor_manager_.LoadAuthFactor(
          kObfuscatedUsername, AuthFactorType::kPassword, kSomeIdpLabel);
  ASSERT_TRUE(loaded_auth_factor.ok());
  EXPECT_EQ(loaded_auth_factor->auth_block_state(),
            auth_factor.auth_block_state());

  AuthBlockState new_state = CreatePasswordAuthBlockState("new auth factor");
  AuthFactor new_auth_factor(auth_factor.type(), auth_factor.label(),
                             auth_factor.metadata(), new_state);
  TestFuture<CryptohomeStatus> update_result;
  // Update the auth factor.
  auth_factor_manager_.UpdateAuthFactor(
      kObfuscatedUsername, auth_factor.label(), new_auth_factor,
      &auth_block_utility, update_result.GetCallback());
  EXPECT_TRUE(update_result.IsReady());
  EXPECT_THAT(update_result.Take(), Not(IsOk()));
}

TEST_F(AuthFactorManagerTest, UpdateFailsWhenNoAuthFactor) {
  NiceMock<MockAuthBlockUtility> auth_block_utility;
  AuthFactor auth_factor = CreatePasswordAuthFactor();
  // Try to update the auth factor.
  TestFuture<CryptohomeStatus> update_result;
  auth_factor_manager_.UpdateAuthFactor(
      kObfuscatedUsername, auth_factor.label(), auth_factor,
      &auth_block_utility, update_result.GetCallback());
  EXPECT_TRUE(update_result.IsReady());
  EXPECT_THAT(update_result.Take(), Not(IsOk()));
}

// A matcher for an AuthFactorMap element. This will check the type, label and
// storage type of the item. You generally want to combine this with
// UnorderedElementsAre to compare it against an entire AuthFactorMap, but you
// can also use it directly with individual elements in the map.
class AuthFactorMapItemMatcher
    : public ::testing::MatcherInterface<AuthFactorMap::ValueView> {
 public:
  AuthFactorMapItemMatcher(AuthFactorType type,
                           std::string label,
                           AuthFactorStorageType storage_type)
      : type_(type), label_(std::move(label)), storage_type_(storage_type) {}

  bool MatchAndExplain(
      AuthFactorMap::ValueView value,
      ::testing::MatchResultListener* listener) const override {
    bool matches = true;
    if (value.auth_factor().type() != type_) {
      matches = false;
      *listener << "type is: "
                << AuthFactorTypeToString(value.auth_factor().type()) << "\n";
    }
    if (value.auth_factor().label() != label_) {
      matches = false;
      *listener << "label is: " << value.auth_factor().label() << "\n";
    }
    if (value.storage_type() != storage_type_) {
      matches = false;
      *listener << "label is: "
                << AuthFactorStorageTypeToDebugString(value.storage_type())
                << "\n";
    }
    return matches;
  }

  void DescribeTo(std::ostream* os) const override {
    *os << "has type " << AuthFactorTypeToString(type_) << ", label " << label_
        << " and storage type "
        << AuthFactorStorageTypeToDebugString(storage_type_);
  }

  void DescribeNegationTo(std::ostream* os) const override {
    *os << "does not have type " << AuthFactorTypeToString(type_)
        << " or does not have label " << label_
        << " or does not have storage type "
        << AuthFactorStorageTypeToDebugString(storage_type_);
  }

 private:
  AuthFactorType type_;
  std::string label_;
  AuthFactorStorageType storage_type_;
};
::testing::Matcher<AuthFactorMap::ValueView> AuthFactorMapItem(
    AuthFactorType type,
    std::string label,
    AuthFactorStorageType storage_type) {
  return ::testing::MakeMatcher<AuthFactorMap::ValueView>(
      new AuthFactorMapItemMatcher(type, std::move(label), storage_type));
}

std::unique_ptr<VaultKeyset> CreatePasswordVaultKeyset(
    const std::string& label) {
  SerializedVaultKeyset serialized_vk;
  serialized_vk.set_flags(SerializedVaultKeyset::TPM_WRAPPED |
                          SerializedVaultKeyset::SCRYPT_DERIVED |
                          SerializedVaultKeyset::PCR_BOUND |
                          SerializedVaultKeyset::ECC);
  serialized_vk.set_password_rounds(1);
  serialized_vk.set_tpm_key("tpm-key");
  serialized_vk.set_extended_tpm_key("tpm-extended-key");
  serialized_vk.set_vkk_iv("iv");
  serialized_vk.mutable_key_data()->set_type(KeyData::KEY_TYPE_PASSWORD);
  serialized_vk.mutable_key_data()->set_label(label);
  auto vk = std::make_unique<VaultKeyset>();
  vk->InitializeFromSerialized(serialized_vk);
  return vk;
}

std::unique_ptr<VaultKeyset> CreateBackupVaultKeyset(const std::string& label) {
  auto backup_vk = CreatePasswordVaultKeyset(label);
  backup_vk->set_backup_vk_for_testing(true);
  backup_vk->SetResetSeed(brillo::SecureBlob(32, 'A'));
  backup_vk->SetWrappedResetSeed(brillo::SecureBlob(32, 'B'));
  return backup_vk;
}

std::unique_ptr<VaultKeyset> CreateMigratedVaultKeyset(
    const std::string& label) {
  auto migrated_vk = CreateBackupVaultKeyset(label);
  migrated_vk->set_migrated_vk_for_testing(true);
  return migrated_vk;
}

class LoadAllAuthFactorsTest : public ::testing::Test {
 protected:
  // Install mocks to set up vault keysets for testing. Expects a map of VK
  // labels to factory functions that will construct a VaultKeyset object.
  void InstallVaultKeysets(
      std::map<std::string,
               std::unique_ptr<VaultKeyset> (*)(const std::string&)>
          vk_factory_map) {
    std::vector<int> key_indicies;
    for (const auto& [label, factory] : vk_factory_map) {
      int index = key_indicies.size();
      key_indicies.push_back(index);
      EXPECT_CALL(keyset_management_,
                  LoadVaultKeysetForUser(kObfuscatedUsername, index))
          .WillRepeatedly([label = label, factory = factory](auto...) {
            return factory(label);
          });
    }
    EXPECT_CALL(keyset_management_, GetVaultKeysets(kObfuscatedUsername, _))
        .WillRepeatedly(DoAll(SetArgPointee<1>(key_indicies), Return(true)));
  }

  // Install a single USS auth factor. If you want to set up multiple factors
  // for your test, call this multiple times.
  void InstallUssFactor(AuthFactor factor) {
    EXPECT_THAT(manager_.SaveAuthFactorFile(kObfuscatedUsername, factor),
                IsOk());
  }

  FakePlatform platform_;

  // Username used for all tests.
  const Username kUsername{"user@testing.com"};
  // Computing the obfuscated name requires the system salt from FakePlatform
  // and so this must be defined after it and not before.
  const ObfuscatedUsername kObfuscatedUsername{SanitizeUserName(kUsername)};

  StrictMock<MockKeysetManagement> keyset_management_;
  AuthFactorVaultKeysetConverter converter_{&keyset_management_};
  AuthFactorManager manager_{&platform_};
};

// Test that if nothing is set up, no factors are loaded.
TEST_F(LoadAllAuthFactorsTest, NoFactors) {
  InstallVaultKeysets({});

  auto af_map =
      manager_.LoadAllAuthFactors(kObfuscatedUsername, {}, converter_);

  EXPECT_THAT(af_map, IsEmpty());
}

TEST_F(LoadAllAuthFactorsTest, LoadWithOnlyVaultKeysets) {
  auto no_uss = DisableUssExperiment();
  InstallVaultKeysets({{"primary", &CreatePasswordVaultKeyset},
                       {"secondary", &CreatePasswordVaultKeyset}});

  auto af_map =
      manager_.LoadAllAuthFactors(kObfuscatedUsername, {}, converter_);

  EXPECT_THAT(af_map,
              UnorderedElementsAre(
                  AuthFactorMapItem(AuthFactorType::kPassword, "primary",
                                    AuthFactorStorageType::kVaultKeyset),
                  AuthFactorMapItem(AuthFactorType::kPassword, "secondary",
                                    AuthFactorStorageType::kVaultKeyset)));
}

TEST_F(LoadAllAuthFactorsTest, LoadWithOnlyUss) {
  auto uss = EnableUssExperiment();
  InstallVaultKeysets({});
  InstallUssFactor(AuthFactor(AuthFactorType::kPassword, "primary",
                              {.metadata = auth_factor::PasswordMetadata()},
                              {.state = TpmBoundToPcrAuthBlockState()}));
  InstallUssFactor(AuthFactor(AuthFactorType::kPin, "secondary",
                              {.metadata = auth_factor::PinMetadata()},
                              {.state = PinWeaverAuthBlockState()}));
  auto af_map = manager_.LoadAllAuthFactors(
      kObfuscatedUsername, {"primary", "secondary"}, converter_);

  EXPECT_THAT(af_map,
              UnorderedElementsAre(
                  AuthFactorMapItem(AuthFactorType::kPassword, "primary",
                                    AuthFactorStorageType::kUserSecretStash),
                  AuthFactorMapItem(AuthFactorType::kPin, "secondary",
                                    AuthFactorStorageType::kUserSecretStash)));
}

// Test that, given a mix of regular VKs, backup VKs, and USS factors, the
// correct ones are loaded depending on whether USS is enabled or disabled.
TEST_F(LoadAllAuthFactorsTest, LoadWithMixUsesUssAndVk) {
  InstallVaultKeysets({{"tertiary", &CreatePasswordVaultKeyset},
                       {"quaternary", &CreateBackupVaultKeyset}});
  InstallUssFactor(AuthFactor(AuthFactorType::kPassword, "primary",
                              {.metadata = auth_factor::PasswordMetadata()},
                              {.state = TpmBoundToPcrAuthBlockState()}));
  InstallUssFactor(AuthFactor(AuthFactorType::kPin, "secondary",
                              {.metadata = auth_factor::PinMetadata()},
                              {.state = PinWeaverAuthBlockState()}));

  auto af_map = manager_.LoadAllAuthFactors(
      kObfuscatedUsername, {"primary", "secondary"}, converter_);

  EXPECT_THAT(af_map,
              UnorderedElementsAre(
                  AuthFactorMapItem(AuthFactorType::kPassword, "primary",
                                    AuthFactorStorageType::kUserSecretStash),
                  AuthFactorMapItem(AuthFactorType::kPin, "secondary",
                                    AuthFactorStorageType::kUserSecretStash),
                  AuthFactorMapItem(AuthFactorType::kPassword, "tertiary",
                                    AuthFactorStorageType::kVaultKeyset)));
}

// Test that, given a mix of regular VKs, migrated VKs, and USS factors, the
// correct ones are loaded.
TEST_F(LoadAllAuthFactorsTest, LoadWithMixUsesUssAndMigratedVk) {
  InstallVaultKeysets({{"secondary", &CreatePasswordVaultKeyset},
                       {"primary", &CreateMigratedVaultKeyset}});
  InstallUssFactor(AuthFactor(AuthFactorType::kPassword, "primary",
                              {.metadata = auth_factor::PasswordMetadata()},
                              {.state = TpmBoundToPcrAuthBlockState()}));

  auto af_map =
      manager_.LoadAllAuthFactors(kObfuscatedUsername, {"primary"}, converter_);

  EXPECT_THAT(af_map,
              UnorderedElementsAre(
                  AuthFactorMapItem(AuthFactorType::kPassword, "primary",
                                    AuthFactorStorageType::kUserSecretStash),
                  AuthFactorMapItem(AuthFactorType::kPassword, "secondary",
                                    AuthFactorStorageType::kVaultKeyset)));
}

TEST_F(LoadAllAuthFactorsTest, LoadWithOnlyUssAndBrokenFactors) {
  auto uss = EnableUssExperiment();
  InstallVaultKeysets({});
  InstallUssFactor(AuthFactor(AuthFactorType::kPassword, "primary",
                              {.metadata = auth_factor::PasswordMetadata()},
                              {.state = TpmBoundToPcrAuthBlockState()}));
  InstallUssFactor(AuthFactor(AuthFactorType::kPin, "secondary",
                              {.metadata = auth_factor::PinMetadata()},
                              {.state = PinWeaverAuthBlockState()}));
  InstallUssFactor(AuthFactor(AuthFactorType::kPassword, "broken",
                              {.metadata = auth_factor::PasswordMetadata()},
                              {.state = TpmBoundToPcrAuthBlockState()}));

  auto af_map = manager_.LoadAllAuthFactors(
      kObfuscatedUsername, {"primary", "secondary"}, converter_);

  EXPECT_THAT(af_map,
              UnorderedElementsAre(
                  AuthFactorMapItem(AuthFactorType::kPassword, "primary",
                                    AuthFactorStorageType::kUserSecretStash),
                  AuthFactorMapItem(AuthFactorType::kPin, "secondary",
                                    AuthFactorStorageType::kUserSecretStash)));
}

}  // namespace
}  // namespace cryptohome
