// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_blocks/pin_weaver_auth_block.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <base/files/file_path.h>
#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>
#include <gtest/gtest.h>
#include <libhwsec-foundation/crypto/aes.h>
#include <libhwsec-foundation/crypto/rsa.h>
#include <libhwsec-foundation/crypto/scrypt.h>
#include <libhwsec-foundation/crypto/secure_box.h>
#include <libhwsec-foundation/error/testing_helper.h>
#include <libhwsec/backend/pinweaver_manager/pinweaver_manager.h>
#include <libhwsec/error/pinweaver_error.h>
#include <libhwsec/error/tpm_error.h>
#include <libhwsec/error/tpm_retry_action.h>
#include <libhwsec/factory/tpm2_simulator_factory_for_test.h>
#include <libhwsec/frontend/cryptohome/mock_frontend.h>
#include <libhwsec/frontend/pinweaver_manager/mock_frontend.h>
#include <libhwsec/frontend/recovery_crypto/mock_frontend.h>

#include "cryptohome/auth_blocks/auth_block_utils.h"
#include "cryptohome/crypto.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/fake_features.h"
#include "cryptohome/features.h"
#include "cryptohome/flatbuffer_schemas/auth_block_state.h"
#include "cryptohome/mock_cryptohome_keys_manager.h"
#include "cryptohome/vault_keyset.h"

namespace cryptohome {
namespace {

using ::base::test::TestFuture;
using ::cryptohome::error::CryptohomeError;
using ::cryptohome::error::ErrorActionSet;
using ::hwsec_foundation::DeriveSecretsScrypt;
using ::hwsec_foundation::kAesBlockSize;
using ::hwsec_foundation::kDefaultAesKeySize;
using ::hwsec_foundation::kDefaultPassBlobSize;
using ::hwsec_foundation::error::testing::IsOk;
using ::hwsec_foundation::error::testing::NotOk;
using ::hwsec_foundation::error::testing::ReturnError;
using ::hwsec_foundation::error::testing::ReturnOk;
using ::hwsec_foundation::error::testing::ReturnValue;
using ::hwsec_foundation::status::MakeStatus;
using ::hwsec_foundation::status::OkStatus;
using ::testing::_;
using ::testing::DoAll;
using ::testing::Eq;
using ::testing::Exactly;
using ::testing::IsTrue;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::StrictMock;

constexpr char kObfuscatedUsername[] = "OBFUSCATED_USERNAME";

using CreateTestFuture = TestFuture<CryptohomeStatus,
                                    std::unique_ptr<KeyBlobs>,
                                    std::unique_ptr<AuthBlockState>>;

using DeriveTestFuture = TestFuture<CryptohomeStatus,
                                    std::unique_ptr<KeyBlobs>,
                                    std::optional<AuthBlock::SuggestedAction>>;

}  // namespace

class PinWeaverAuthBlockTest : public ::testing::Test {
 public:
  void SetUp() override {
    auth_block_ = std::make_unique<PinWeaverAuthBlock>(features_.async,
                                                       &hwsec_pw_manager_);
  }

 protected:
  base::test::TaskEnvironment task_environment_;

  NiceMock<hwsec::MockPinWeaverManagerFrontend> hwsec_pw_manager_;
  FakeFeaturesForTesting features_;
  std::unique_ptr<PinWeaverAuthBlock> auth_block_;
};

TEST_F(PinWeaverAuthBlockTest, CreateTestPin) {
  // Set up inputs to the test.
  brillo::SecureBlob vault_key(20, 'C');
  brillo::SecureBlob reset_secret(32, 'S');

  // Set up the mock expectations.
  brillo::SecureBlob le_secret;
  DelaySchedule delay_sched;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  EXPECT_CALL(hwsec_pw_manager_, InsertCredential(_, _, _, _, _, _))
      .WillOnce(DoAll(SaveArg<1>(&le_secret), SaveArg<4>(&delay_sched),
                      ReturnValue(0)));

  // Call the Create() method.
  AuthInput user_input = {vault_key,
                          /*locked_to_single_user=*/std::nullopt, Username(),
                          ObfuscatedUsername(kObfuscatedUsername),
                          reset_secret};
  KeyBlobs vkk_data;

  features_.SetDefaultForFeature(Features::kModernPin, true);

  CreateTestFuture result;
  auth_block_->Create(user_input, {.metadata = PinMetadata()},
                      result.GetCallback());
  ASSERT_TRUE(result.IsReady());
  auto [status, key_blobs, auth_state] = result.Take();
  ASSERT_THAT(status, IsOk());
  EXPECT_TRUE(
      std::holds_alternative<PinWeaverAuthBlockState>(auth_state->state));

  auto& pin_state = std::get<PinWeaverAuthBlockState>(auth_state->state);

  EXPECT_TRUE(pin_state.salt.has_value());
  const brillo::Blob& salt = pin_state.salt.value();
  brillo::SecureBlob le_secret_result(kDefaultAesKeySize);
  EXPECT_TRUE(DeriveSecretsScrypt(vault_key, salt, {&le_secret_result}));
  EXPECT_EQ(le_secret, le_secret_result);
  EXPECT_EQ(delay_sched, PinDelaySchedule());
}

TEST_F(PinWeaverAuthBlockTest, CreateTestPassword) {
  // Set up inputs to the test.
  brillo::SecureBlob vault_key(20, 'C');
  brillo::SecureBlob reset_secret(32, 'S');

  // Set up the mock expectations.
  brillo::SecureBlob le_secret;
  DelaySchedule delay_sched;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  EXPECT_CALL(hwsec_pw_manager_, InsertCredential(_, _, _, _, _, _))
      .WillOnce(DoAll(SaveArg<1>(&le_secret), SaveArg<4>(&delay_sched),
                      ReturnValue(0)));

  // Call the Create() method.
  AuthInput user_input = {vault_key,
                          /*locked_to_single_user=*/std::nullopt, Username(),
                          ObfuscatedUsername(kObfuscatedUsername),
                          reset_secret};
  KeyBlobs vkk_data;

  CreateTestFuture result;
  auth_block_->Create(user_input, {.metadata = PasswordMetadata()},
                      result.GetCallback());
  ASSERT_TRUE(result.IsReady());
  auto [status, key_blobs, auth_state] = result.Take();
  ASSERT_THAT(status, IsOk());
  EXPECT_TRUE(
      std::holds_alternative<PinWeaverAuthBlockState>(auth_state->state));

  auto& pin_state = std::get<PinWeaverAuthBlockState>(auth_state->state);

  EXPECT_TRUE(pin_state.salt.has_value());
  const brillo::Blob& salt = pin_state.salt.value();
  brillo::SecureBlob le_secret_result(kDefaultAesKeySize);
  EXPECT_TRUE(DeriveSecretsScrypt(vault_key, salt, {&le_secret_result}));
  EXPECT_EQ(le_secret, le_secret_result);
  EXPECT_EQ(delay_sched, PasswordDelaySchedule());
}

TEST_F(PinWeaverAuthBlockTest, CreateTestKiosk) {
  // Set up inputs to the test.
  brillo::SecureBlob vault_key(20, 'C');
  brillo::SecureBlob reset_secret(32, 'S');

  // Set up the mock expectations.
  brillo::SecureBlob le_secret;
  DelaySchedule delay_sched;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  EXPECT_CALL(hwsec_pw_manager_, InsertCredential(_, _, _, _, _, _))
      .WillOnce(DoAll(SaveArg<1>(&le_secret), SaveArg<4>(&delay_sched),
                      ReturnValue(0)));

  // Call the Create() method.
  AuthInput user_input = {vault_key,
                          /*locked_to_single_user=*/std::nullopt, Username(),
                          ObfuscatedUsername(kObfuscatedUsername),
                          reset_secret};
  KeyBlobs vkk_data;

  CreateTestFuture result;
  auth_block_->Create(user_input, {.metadata = KioskMetadata()},
                      result.GetCallback());
  ASSERT_TRUE(result.IsReady());
  auto [status, key_blobs, auth_state] = result.Take();
  ASSERT_THAT(status, IsOk());
  EXPECT_TRUE(
      std::holds_alternative<PinWeaverAuthBlockState>(auth_state->state));

  auto& pin_state = std::get<PinWeaverAuthBlockState>(auth_state->state);

  EXPECT_TRUE(pin_state.salt.has_value());
  const brillo::Blob& salt = pin_state.salt.value();
  brillo::SecureBlob le_secret_result(kDefaultAesKeySize);
  EXPECT_TRUE(DeriveSecretsScrypt(vault_key, salt, {&le_secret_result}));
  EXPECT_EQ(le_secret, le_secret_result);
  EXPECT_EQ(delay_sched, PasswordDelaySchedule());
}

TEST_F(PinWeaverAuthBlockTest, CreateTestWithoutMigratePin) {
  // Set up inputs to the test.
  brillo::SecureBlob vault_key(20, 'C');
  brillo::SecureBlob reset_secret(32, 'S');

  // Set up the mock expectations.
  brillo::SecureBlob le_secret;
  DelaySchedule delay_sched;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  EXPECT_CALL(hwsec_pw_manager_, InsertCredential(_, _, _, _, _, _))
      .WillOnce(DoAll(SaveArg<1>(&le_secret), SaveArg<4>(&delay_sched),
                      ReturnValue(0)));

  // Call the Create() method.
  AuthInput user_input = {vault_key,
                          /*locked_to_single_user=*/std::nullopt, Username(),
                          ObfuscatedUsername(kObfuscatedUsername),
                          reset_secret};
  features_.SetDefaultForFeature(Features::kMigratePin, false);
  features_.SetDefaultForFeature(Features::kModernPin, false);
  CreateTestFuture result;
  auth_block_->Create(user_input, {.metadata = PinMetadata()},
                      result.GetCallback());
  ASSERT_TRUE(result.IsReady());
  auto [status, key_blobs, auth_state] = result.Take();

  ASSERT_THAT(status, IsOk());
  EXPECT_TRUE(
      std::holds_alternative<PinWeaverAuthBlockState>(auth_state->state));

  auto& pin_state = std::get<PinWeaverAuthBlockState>(auth_state->state);

  EXPECT_TRUE(pin_state.salt.has_value());
  const brillo::Blob& salt = pin_state.salt.value();
  brillo::SecureBlob le_secret_result(kDefaultAesKeySize);
  EXPECT_TRUE(DeriveSecretsScrypt(vault_key, salt, {&le_secret_result}));
  EXPECT_EQ(le_secret, le_secret_result);
  EXPECT_EQ(delay_sched, LockoutDelaySchedule());
}

TEST_F(PinWeaverAuthBlockTest, CreateFailurePinWeaverManager) {
  const CryptohomeError::ErrorLocationPair kErrorLocationForTesting1 =
      CryptohomeError::ErrorLocationPair(
          static_cast<::cryptohome::error::CryptohomeError::ErrorLocation>(1),
          std::string("Testing1"));

  brillo::SecureBlob vault_key(20, 'C');
  brillo::SecureBlob reset_secret(32, 'S');

  // Now test that the method fails if pinweaver manager fails.
  ON_CALL(hwsec_pw_manager_, InsertCredential(_, _, _, _, _, _))
      .WillByDefault(ReturnError<hwsec::TPMError>(
          "fake", hwsec::TPMRetryAction::kNoRetry));

  // Call the Create() method.
  AuthInput user_input = {vault_key,
                          /*locked_to_single_user=*/std::nullopt, Username(),
                          ObfuscatedUsername(kObfuscatedUsername),
                          reset_secret};

  CreateTestFuture result;
  auth_block_->Create(user_input, {.metadata = PinMetadata()},
                      result.GetCallback());
  ASSERT_TRUE(result.IsReady());
  auto [status, key_blobs, auth_state] = result.Take();

  ASSERT_THAT(status, NotOk());
  EXPECT_EQ(user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED,
            status->local_legacy_error());
}

// Test PinWeaverAuthBlock create fails when there's no user_input provided.
TEST_F(PinWeaverAuthBlockTest, CreateFailureNoUserInput) {
  brillo::SecureBlob reset_secret(32, 'S');

  AuthInput auth_input = {
      .obfuscated_username = ObfuscatedUsername(kObfuscatedUsername),
      .reset_secret = reset_secret};
  CreateTestFuture result;
  auth_block_->Create(auth_input, {.metadata = PinMetadata()},
                      result.GetCallback());
  ASSERT_TRUE(result.IsReady());
  auto [status, key_blobs, auth_state] = result.Take();

  ASSERT_THAT(status, NotOk());
  EXPECT_EQ(user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED,
            status->local_legacy_error());
}

// Test PinWeaverAuthBlock create fails when there's no obfuscated_username
// provided.
TEST_F(PinWeaverAuthBlockTest, CreateFailureNoObfuscated) {
  brillo::SecureBlob user_input(20, 'C');
  brillo::SecureBlob reset_secret(32, 'S');

  AuthInput auth_input = {.user_input = user_input,
                          .reset_secret = reset_secret};
  CreateTestFuture result;
  auth_block_->Create(auth_input, {.metadata = PinMetadata()},
                      result.GetCallback());
  ASSERT_TRUE(result.IsReady());
  auto [status, key_blobs, auth_state] = result.Take();

  ASSERT_THAT(status, NotOk());
  EXPECT_EQ(user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED,
            status->local_legacy_error());
}

// Test PinWeaverAuthBlock create fails when there's no reset_secret provided.
TEST_F(PinWeaverAuthBlockTest, CreateFailureNoResetSecret) {
  brillo::SecureBlob user_input(20, 'C');

  AuthInput auth_input = {
      .user_input = user_input,
      .obfuscated_username = ObfuscatedUsername(kObfuscatedUsername)};
  CreateTestFuture result;
  auth_block_->Create(auth_input, {.metadata = PinMetadata()},
                      result.GetCallback());
  ASSERT_TRUE(result.IsReady());
  auto [status, key_blobs, auth_state] = result.Take();

  ASSERT_THAT(status, NotOk());
  EXPECT_EQ(user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED,
            status->local_legacy_error());
}

TEST_F(PinWeaverAuthBlockTest, CreateFailureUnsupportedType) {
  brillo::SecureBlob vault_key(20, 'C');
  brillo::SecureBlob reset_secret(32, 'S');
  AuthInput user_input = {vault_key,
                          /*locked_to_single_user=*/std::nullopt, Username(),
                          ObfuscatedUsername(kObfuscatedUsername),
                          reset_secret};

  CreateTestFuture result;
  auth_block_->Create(user_input, {.metadata = CryptohomeRecoveryMetadata()},
                      result.GetCallback());
  ASSERT_TRUE(result.IsReady());
  auto [status, key_blobs, auth_state] = result.Take();

  ASSERT_THAT(status, NotOk());
  EXPECT_EQ(user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT,
            status->local_legacy_error());
}

// Check required field |le_label| in PinWeaverAuthBlockState.
TEST_F(PinWeaverAuthBlockTest, DeriveFailureMissingLeLabel) {
  brillo::SecureBlob user_input(20, 'C');
  brillo::Blob salt(PKCS5_SALT_LEN, 'A');
  brillo::Blob chaps_iv(kAesBlockSize, 'F');
  brillo::Blob fek_iv(kAesBlockSize, 'X');

  // Construct the auth block state. le_label is not set.
  AuthBlockState auth_state;
  PinWeaverAuthBlockState state;
  state.salt = salt;
  state.chaps_iv = chaps_iv;
  state.fek_iv = fek_iv;
  auth_state.state = std::move(state);

  AuthInput auth_input = {.user_input = user_input};
  AuthFactorMetadata metadata = {.metadata = PinMetadata()};
  DeriveTestFuture result;
  auth_block_->Derive(auth_input, metadata, auth_state, result.GetCallback());
  ASSERT_TRUE(result.IsReady());
  auto [status, key_blobs, suggested_action] = result.Take();
  ASSERT_THAT(status, NotOk());

  ASSERT_THAT(user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED,
              status->local_legacy_error());
}

// Check required field |salt| in PinWeaverAuthBlockState.
TEST_F(PinWeaverAuthBlockTest, DeriveFailureMissingSalt) {
  brillo::SecureBlob user_input(20, 'C');
  brillo::Blob chaps_iv(kAesBlockSize, 'F');
  brillo::Blob fek_iv(kAesBlockSize, 'X');

  // Construct the auth block state. salt is not set.
  AuthBlockState auth_state;
  PinWeaverAuthBlockState state;
  state.le_label = 0;
  state.chaps_iv = chaps_iv;
  state.fek_iv = fek_iv;
  auth_state.state = std::move(state);

  AuthInput auth_input = {.user_input = user_input};
  AuthFactorMetadata metadata = {.metadata = PinMetadata()};
  DeriveTestFuture result;
  auth_block_->Derive(auth_input, metadata, auth_state, result.GetCallback());
  ASSERT_TRUE(result.IsReady());
  auto [status, key_blobs, suggested_action] = result.Take();
  ASSERT_THAT(status, NotOk());

  ASSERT_THAT(user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED,
              status->local_legacy_error());
}

// Check PinWeaverAuthBlock derive fails if user_input is missing.
TEST_F(PinWeaverAuthBlockTest, DeriveFailureNoUserInput) {
  brillo::Blob chaps_iv(kAesBlockSize, 'F');
  brillo::Blob fek_iv(kAesBlockSize, 'X');
  brillo::Blob salt(PKCS5_SALT_LEN, 'A');

  // Construct the auth block state.
  AuthBlockState auth_state;
  PinWeaverAuthBlockState state = {
      .le_label = 0, .salt = salt, .chaps_iv = chaps_iv, .fek_iv = fek_iv};
  auth_state.state = std::move(state);
  AuthInput auth_input = {};
  AuthFactorMetadata metadata = {.metadata = PinMetadata()};
  DeriveTestFuture result;
  auth_block_->Derive(auth_input, metadata, auth_state, result.GetCallback());
  ASSERT_TRUE(result.IsReady());
  auto [status, key_blobs, suggested_action] = result.Take();
  ASSERT_THAT(status, NotOk());

  ASSERT_THAT(user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED,
              status->local_legacy_error());
}

TEST_F(PinWeaverAuthBlockTest, DeriveTest) {
  brillo::SecureBlob vault_key(20, 'C');
  brillo::Blob salt(PKCS5_SALT_LEN, 'A');
  brillo::Blob chaps_iv(kAesBlockSize, 'F');
  brillo::Blob fek_iv(kAesBlockSize, 'X');

  brillo::SecureBlob le_secret(kDefaultAesKeySize);
  ASSERT_TRUE(DeriveSecretsScrypt(vault_key, salt, {&le_secret}));

  ON_CALL(hwsec_pw_manager_, CheckCredential(_, _))
      .WillByDefault(
          ReturnValue(hwsec::PinWeaverManager::CheckCredentialReply{}));
  EXPECT_CALL(hwsec_pw_manager_, CheckCredential(_, le_secret))
      .Times(Exactly(1));
  EXPECT_CALL(hwsec_pw_manager_, GetDelaySchedule(_))
      .WillOnce(Return(PinDelaySchedule()));
  features_.SetDefaultForFeature(Features::kMigratePin, true);

  // Construct the vault keyset.
  SerializedVaultKeyset serialized;
  serialized.set_flags(SerializedVaultKeyset::LE_CREDENTIAL);
  serialized.set_salt(salt.data(), salt.size());
  serialized.set_le_chaps_iv(chaps_iv.data(), chaps_iv.size());
  serialized.set_le_label(0);
  serialized.set_le_fek_iv(fek_iv.data(), fek_iv.size());

  VaultKeyset vk;
  vk.InitializeFromSerialized(serialized);
  AuthBlockState auth_state;
  EXPECT_TRUE(GetAuthBlockState(vk, auth_state));

  AuthInput auth_input = {vault_key};
  AuthFactorMetadata metadata = {.metadata = PinMetadata()};

  DeriveTestFuture result;
  auth_block_->Derive(auth_input, metadata, auth_state, result.GetCallback());
  ASSERT_TRUE(result.IsReady());
  auto [status, key_blobs, suggested_action] = result.Take();
  ASSERT_THAT(status, IsOk());

  // Set expectations of the key blobs.
  EXPECT_NE(key_blobs->reset_secret, std::nullopt);
  EXPECT_NE(key_blobs->chaps_iv, std::nullopt);
  EXPECT_NE(key_blobs->vkk_iv, std::nullopt);

  // PinWeaver should always use unique IVs.
  EXPECT_NE(key_blobs->chaps_iv.value(), key_blobs->vkk_iv.value());

  // No suggested_action with the credential.
  EXPECT_EQ(suggested_action, std::nullopt);
}

TEST_F(PinWeaverAuthBlockTest, DeriveTestWithPassword) {
  brillo::SecureBlob vault_key(20, 'C');
  brillo::Blob salt(PKCS5_SALT_LEN, 'A');
  brillo::Blob chaps_iv(kAesBlockSize, 'F');
  brillo::Blob fek_iv(kAesBlockSize, 'X');

  brillo::SecureBlob le_secret(kDefaultAesKeySize);
  ASSERT_TRUE(DeriveSecretsScrypt(vault_key, salt, {&le_secret}));

  ON_CALL(hwsec_pw_manager_, CheckCredential(_, _))
      .WillByDefault(
          ReturnValue(hwsec::PinWeaverManager::CheckCredentialReply{}));
  EXPECT_CALL(hwsec_pw_manager_, CheckCredential(_, le_secret))
      .Times(Exactly(1));
  features_.SetDefaultForFeature(Features::kMigratePin, true);

  // Construct the vault keyset.
  SerializedVaultKeyset serialized;
  serialized.set_flags(SerializedVaultKeyset::LE_CREDENTIAL);
  serialized.set_salt(salt.data(), salt.size());
  serialized.set_le_chaps_iv(chaps_iv.data(), chaps_iv.size());
  serialized.set_le_label(0);
  serialized.set_le_fek_iv(fek_iv.data(), fek_iv.size());

  VaultKeyset vk;
  vk.InitializeFromSerialized(serialized);
  AuthBlockState auth_state;
  EXPECT_TRUE(GetAuthBlockState(vk, auth_state));

  AuthInput auth_input = {vault_key};
  AuthFactorMetadata metadata = {.metadata = PasswordMetadata()};

  DeriveTestFuture result;
  auth_block_->Derive(auth_input, metadata, auth_state, result.GetCallback());
  ASSERT_TRUE(result.IsReady());
  auto [status, key_blobs, suggested_action] = result.Take();
  ASSERT_THAT(status, IsOk());

  // Set expectations of the key blobs.
  EXPECT_NE(key_blobs->reset_secret, std::nullopt);
  EXPECT_NE(key_blobs->chaps_iv, std::nullopt);
  EXPECT_NE(key_blobs->vkk_iv, std::nullopt);

  // PinWeaver should always use unique IVs.
  EXPECT_NE(key_blobs->chaps_iv.value(), key_blobs->vkk_iv.value());

  // No suggested_action with the credential.
  EXPECT_EQ(suggested_action, std::nullopt);
}

TEST_F(PinWeaverAuthBlockTest, DeriveTestWithLockoutPin) {
  brillo::SecureBlob vault_key(20, 'C');
  brillo::Blob salt(PKCS5_SALT_LEN, 'A');
  brillo::Blob chaps_iv(kAesBlockSize, 'F');
  brillo::Blob fek_iv(kAesBlockSize, 'X');

  brillo::SecureBlob le_secret(kDefaultAesKeySize);
  ASSERT_TRUE(DeriveSecretsScrypt(vault_key, salt, {&le_secret}));

  ON_CALL(hwsec_pw_manager_, CheckCredential(_, _))
      .WillByDefault(
          ReturnValue(hwsec::PinWeaverManager::CheckCredentialReply{}));
  EXPECT_CALL(hwsec_pw_manager_, CheckCredential(_, le_secret))
      .Times(Exactly(1));
  EXPECT_CALL(hwsec_pw_manager_, GetDelaySchedule(_))
      .WillOnce(Return(LockoutDelaySchedule()));
  features_.SetDefaultForFeature(Features::kMigratePin, true);

  // Construct the vault keyset.
  SerializedVaultKeyset serialized;
  serialized.set_flags(SerializedVaultKeyset::LE_CREDENTIAL);
  serialized.set_salt(salt.data(), salt.size());
  serialized.set_le_chaps_iv(chaps_iv.data(), chaps_iv.size());
  serialized.set_le_label(0);
  serialized.set_le_fek_iv(fek_iv.data(), fek_iv.size());

  VaultKeyset vk;
  vk.InitializeFromSerialized(serialized);
  AuthBlockState auth_state;
  EXPECT_TRUE(GetAuthBlockState(vk, auth_state));

  AuthInput auth_input = {vault_key};
  AuthFactorMetadata metadata = {.metadata = PinMetadata()};
  DeriveTestFuture result;
  auth_block_->Derive(auth_input, metadata, auth_state, result.GetCallback());
  ASSERT_TRUE(result.IsReady());
  auto [status, key_blobs, suggested_action] = result.Take();
  ASSERT_THAT(status, IsOk());
  // Set expectations of the key blobs.
  EXPECT_NE(key_blobs->reset_secret, std::nullopt);
  EXPECT_NE(key_blobs->chaps_iv, std::nullopt);
  EXPECT_NE(key_blobs->vkk_iv, std::nullopt);

  // PinWeaver should always use unique IVs.
  EXPECT_NE(key_blobs->chaps_iv.value(), key_blobs->vkk_iv.value());

  // No suggested_action with the credential.
  EXPECT_EQ(suggested_action, AuthBlock::SuggestedAction::kRecreate);
}

TEST_F(PinWeaverAuthBlockTest, DeriveTestWithoutMigratePin) {
  brillo::SecureBlob vault_key(20, 'C');
  brillo::Blob salt(PKCS5_SALT_LEN, 'A');
  brillo::Blob chaps_iv(kAesBlockSize, 'F');
  brillo::Blob fek_iv(kAesBlockSize, 'X');

  brillo::SecureBlob le_secret(kDefaultAesKeySize);
  ASSERT_TRUE(DeriveSecretsScrypt(vault_key, salt, {&le_secret}));

  ON_CALL(hwsec_pw_manager_, CheckCredential(_, _))
      .WillByDefault(
          ReturnValue(hwsec::PinWeaverManager::CheckCredentialReply{}));
  EXPECT_CALL(hwsec_pw_manager_, CheckCredential(_, le_secret))
      .Times(Exactly(1));
  features_.SetDefaultForFeature(Features::kMigratePin, false);

  // Construct the vault keyset.
  SerializedVaultKeyset serialized;
  serialized.set_flags(SerializedVaultKeyset::LE_CREDENTIAL);
  serialized.set_salt(salt.data(), salt.size());
  serialized.set_le_chaps_iv(chaps_iv.data(), chaps_iv.size());
  serialized.set_le_label(0);
  serialized.set_le_fek_iv(fek_iv.data(), fek_iv.size());

  VaultKeyset vk;
  vk.InitializeFromSerialized(serialized);
  AuthBlockState auth_state;
  EXPECT_TRUE(GetAuthBlockState(vk, auth_state));

  AuthInput auth_input = {vault_key};
  AuthFactorMetadata metadata = {.metadata = PinMetadata()};
  DeriveTestFuture result;
  auth_block_->Derive(auth_input, metadata, auth_state, result.GetCallback());
  ASSERT_TRUE(result.IsReady());
  auto [status, key_blobs, suggested_action] = result.Take();
  ASSERT_THAT(status, IsOk());

  // Set expectations of the key blobs.
  EXPECT_NE(key_blobs->reset_secret, std::nullopt);
  EXPECT_NE(key_blobs->chaps_iv, std::nullopt);
  EXPECT_NE(key_blobs->vkk_iv, std::nullopt);

  // PinWeaver should always use unique IVs.
  EXPECT_NE(key_blobs->chaps_iv.value(), key_blobs->vkk_iv.value());

  // No suggested_action with the credential.
  EXPECT_EQ(suggested_action, std::nullopt);
}

// Test that derive function works as intended when fek_iv and le_chaps_iv is
// not set.
TEST_F(PinWeaverAuthBlockTest, DeriveOptionalValuesTest) {
  brillo::SecureBlob vault_key(20, 'C');
  brillo::Blob salt(PKCS5_SALT_LEN, 'A');

  brillo::SecureBlob le_secret(kDefaultAesKeySize);
  ASSERT_TRUE(DeriveSecretsScrypt(vault_key, salt, {&le_secret}));

  ON_CALL(hwsec_pw_manager_, CheckCredential(_, _))
      .WillByDefault(
          ReturnValue(hwsec::PinWeaverManager::CheckCredentialReply{}));
  EXPECT_CALL(hwsec_pw_manager_, CheckCredential(_, le_secret))
      .Times(Exactly(1));
  EXPECT_CALL(hwsec_pw_manager_, GetDelaySchedule(_))
      .WillOnce(Return(PinDelaySchedule()));

  // Construct the vault keyset.
  // Notice that it does not set fek_iv and le_chaps_iv;
  SerializedVaultKeyset serialized;
  serialized.set_flags(SerializedVaultKeyset::LE_CREDENTIAL);
  serialized.set_salt(salt.data(), salt.size());
  serialized.set_le_label(0);

  VaultKeyset vk;
  vk.InitializeFromSerialized(serialized);
  AuthBlockState auth_state;
  EXPECT_TRUE(GetAuthBlockState(vk, auth_state));

  AuthInput auth_input = {vault_key};
  AuthFactorMetadata metadata = {.metadata = PinMetadata()};
  DeriveTestFuture result;
  auth_block_->Derive(auth_input, metadata, auth_state, result.GetCallback());
  ASSERT_TRUE(result.IsReady());
  auto [status, key_blobs, suggested_action] = result.Take();
  ASSERT_THAT(status, IsOk());

  // Set expectations of the key blobs.
  EXPECT_NE(key_blobs->reset_secret, std::nullopt);
  // We expect this to be null because it was not set earlier.
  EXPECT_EQ(key_blobs->chaps_iv, std::nullopt);
  EXPECT_EQ(key_blobs->vkk_iv, std::nullopt);

  // No suggested_action with the credential.
  EXPECT_EQ(suggested_action, std::nullopt);
}

TEST_F(PinWeaverAuthBlockTest, CheckCredentialFailureTest) {
  const CryptohomeError::ErrorLocationPair kErrorLocationForTesting1 =
      CryptohomeError::ErrorLocationPair(
          static_cast<::cryptohome::error::CryptohomeError::ErrorLocation>(1),
          std::string("Testing1"));

  brillo::SecureBlob vault_key(20, 'C');
  brillo::Blob salt(PKCS5_SALT_LEN, 'A');
  brillo::Blob chaps_iv(kAesBlockSize, 'F');
  brillo::Blob fek_iv(kAesBlockSize, 'X');

  brillo::SecureBlob le_secret(kDefaultAesKeySize);
  ASSERT_TRUE(DeriveSecretsScrypt(vault_key, salt, {&le_secret}));

  ON_CALL(hwsec_pw_manager_, CheckCredential(_, _))
      .WillByDefault(ReturnError<hwsec::TPMError>(
          "fake", hwsec::TPMRetryAction::kUserAuth));
  EXPECT_CALL(hwsec_pw_manager_, CheckCredential(_, le_secret))
      .Times(Exactly(1));
  EXPECT_CALL(hwsec_pw_manager_, GetDelayInSeconds(_)).WillOnce(ReturnValue(0));

  // Construct the vault keyset.
  SerializedVaultKeyset serialized;
  serialized.set_flags(SerializedVaultKeyset::LE_CREDENTIAL);
  serialized.set_salt(salt.data(), salt.size());
  serialized.set_le_chaps_iv(chaps_iv.data(), chaps_iv.size());
  serialized.set_le_label(0);
  serialized.set_le_fek_iv(fek_iv.data(), fek_iv.size());

  VaultKeyset vk;
  vk.InitializeFromSerialized(serialized);
  AuthBlockState auth_state;
  EXPECT_TRUE(GetAuthBlockState(vk, auth_state));

  AuthInput auth_input = {vault_key};
  AuthFactorMetadata metadata = {.metadata = PinMetadata()};
  DeriveTestFuture result;
  auth_block_->Derive(auth_input, metadata, auth_state, result.GetCallback());
  ASSERT_TRUE(result.IsReady());
  auto [status, key_blobs, suggested_action] = result.Take();
  ASSERT_THAT(status, NotOk());

  ASSERT_THAT(user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED,
              status->local_legacy_error());
}

TEST_F(PinWeaverAuthBlockTest, CheckCredentialFailureLeFiniteTimeout) {
  const CryptohomeError::ErrorLocationPair kErrorLocationForTesting1 =
      CryptohomeError::ErrorLocationPair(
          static_cast<::cryptohome::error::CryptohomeError::ErrorLocation>(1),
          std::string("Testing1"));

  brillo::SecureBlob vault_key(20, 'C');
  brillo::Blob salt(PKCS5_SALT_LEN, 'A');
  brillo::Blob chaps_iv(kAesBlockSize, 'F');
  brillo::Blob fek_iv(kAesBlockSize, 'X');

  brillo::SecureBlob le_secret(kDefaultAesKeySize);
  ASSERT_TRUE(DeriveSecretsScrypt(vault_key, salt, {&le_secret}));

  ON_CALL(hwsec_pw_manager_, CheckCredential(_, _))
      .WillByDefault(ReturnError<hwsec::TPMError>(
          "fake", hwsec::TPMRetryAction::kPinWeaverLockedOut));
  EXPECT_CALL(hwsec_pw_manager_, CheckCredential(_, le_secret))
      .Times(Exactly(1));
  // Simulate a 30 second timeout duration on the le credential.
  EXPECT_CALL(hwsec_pw_manager_, GetDelayInSeconds(_))
      .WillOnce(ReturnValue(30));

  PinWeaverAuthBlockState state;
  state.le_label = 0;
  state.salt = salt;
  state.chaps_iv = chaps_iv;
  state.fek_iv = fek_iv;
  AuthBlockState auth_state;
  auth_state.state = std::move(state);

  AuthInput auth_input = {vault_key};
  AuthFactorMetadata metadata = {.metadata = PinMetadata()};
  DeriveTestFuture result;
  auth_block_->Derive(auth_input, metadata, auth_state, result.GetCallback());
  ASSERT_TRUE(result.IsReady());
  auto [status, key_blobs, suggested_action] = result.Take();
  ASSERT_THAT(status, NotOk());

  ASSERT_THAT(user_data_auth::CRYPTOHOME_ERROR_TPM_DEFEND_LOCK,
              status->local_legacy_error());
}

TEST_F(PinWeaverAuthBlockTest, CheckCredentialNotFatalCryptoErrorTest) {
  const CryptohomeError::ErrorLocationPair kErrorLocationForTesting1 =
      CryptohomeError::ErrorLocationPair(
          static_cast<::cryptohome::error::CryptohomeError::ErrorLocation>(1),
          std::string("Testing1"));

  brillo::SecureBlob vault_key(20, 'C');
  brillo::Blob salt(PKCS5_SALT_LEN, 'A');
  brillo::Blob chaps_iv(kAesBlockSize, 'F');
  brillo::Blob fek_iv(kAesBlockSize, 'X');

  brillo::SecureBlob le_secret(kDefaultAesKeySize);
  ASSERT_TRUE(DeriveSecretsScrypt(vault_key, salt, {&le_secret}));

  ON_CALL(hwsec_pw_manager_, CheckCredential(_, _))
      .WillByDefault(ReturnError<hwsec::TPMError>(
          "fake", hwsec::TPMRetryAction::kNoRetry));
  EXPECT_CALL(hwsec_pw_manager_, CheckCredential(_, le_secret))
      .WillOnce(ReturnError<hwsec::TPMError>("fake",
                                             hwsec::TPMRetryAction::kUserAuth))
      .WillOnce(
          ReturnError<hwsec::TPMError>("fake", hwsec::TPMRetryAction::kReboot))
      .WillOnce(
          ReturnError<hwsec::TPMError>("fake", hwsec::TPMRetryAction::kNoRetry))
      .WillOnce(ReturnError<hwsec::TPMError>(
          "fake", hwsec::TPMRetryAction::kPinWeaverExpired))
      .WillOnce(ReturnError<hwsec::TPMError>(
          "fake", hwsec::TPMRetryAction::kPinWeaverLockedOut))
      .WillOnce(ReturnError<hwsec::TPMError>(
          "fake", hwsec::TPMRetryAction::kPinWeaverOutOfSync));
  EXPECT_CALL(hwsec_pw_manager_, GetDelayInSeconds(_))
      .WillRepeatedly(ReturnValue(0));

  // Construct the vault keyset.
  SerializedVaultKeyset serialized;
  serialized.set_flags(SerializedVaultKeyset::LE_CREDENTIAL);
  serialized.set_salt(salt.data(), salt.size());
  serialized.set_le_chaps_iv(chaps_iv.data(), chaps_iv.size());
  serialized.set_le_label(0);
  serialized.set_le_fek_iv(fek_iv.data(), fek_iv.size());

  VaultKeyset vk;
  vk.InitializeFromSerialized(serialized);
  AuthBlockState auth_state;
  EXPECT_TRUE(GetAuthBlockState(vk, auth_state));

  AuthInput auth_input = {vault_key};
  AuthFactorMetadata metadata = {.metadata = PinMetadata()};
  for (int i = 0; i < 6; i++) {
    DeriveTestFuture result;
    auth_block_->Derive(auth_input, metadata, auth_state, result.GetCallback());
    ASSERT_TRUE(result.IsReady());
    auto [status, key_blobs, suggested_action] = result.Take();
    EXPECT_NE(user_data_auth::CRYPTOHOME_ERROR_VAULT_UNRECOVERABLE,
              status->local_legacy_error());
    EXPECT_NE(user_data_auth::CRYPTOHOME_ERROR_VAULT_UNRECOVERABLE,
              status->local_legacy_error());
  }
}

TEST_F(PinWeaverAuthBlockTest, PrepareForRemovalSuccess) {
  const uint64_t kLabel = 100;

  AuthBlockState auth_state;
  PinWeaverAuthBlockState pinweaver_auth_state;
  pinweaver_auth_state.le_label = kLabel;
  auth_state.state = pinweaver_auth_state;

  EXPECT_CALL(hwsec_pw_manager_, RemoveCredential(kLabel))
      .WillOnce(ReturnOk<hwsec::PinWeaverError>());

  TestFuture<CryptohomeStatus> result;
  auth_block_->PrepareForRemoval(ObfuscatedUsername(kObfuscatedUsername),
                                 auth_state, result.GetCallback());
  EXPECT_TRUE(result.IsReady());
  EXPECT_THAT(result.Take(), IsOk());
}

TEST_F(PinWeaverAuthBlockTest, PrepareForRemovalNoState) {
  AuthBlockState auth_state;

  // Prepare for removal should still succeed when there is no valid auth state.
  TestFuture<CryptohomeStatus> result;
  auth_block_->PrepareForRemoval(ObfuscatedUsername(kObfuscatedUsername),
                                 auth_state, result.GetCallback());
  EXPECT_TRUE(result.IsReady());
  EXPECT_THAT(result.Take(), IsOk());
}

TEST_F(PinWeaverAuthBlockTest, PrepareForRemovalRemoveError) {
  const CryptohomeError::ErrorLocationPair kErrorLocationForTesting1 =
      CryptohomeError::ErrorLocationPair(
          static_cast<::cryptohome::error::CryptohomeError::ErrorLocation>(1),
          std::string("Testing1"));
  const uint64_t kLabel = 100;

  AuthBlockState auth_state;
  PinWeaverAuthBlockState pinweaver_auth_state;
  pinweaver_auth_state.le_label = kLabel;
  auth_state.state = pinweaver_auth_state;

  EXPECT_CALL(hwsec_pw_manager_, RemoveCredential(kLabel))
      .WillOnce(ReturnError<hwsec::TPMError>(
          "fake", hwsec::TPMRetryAction::kPinWeaverLockedOut))
      .WillOnce(ReturnError<hwsec::TPMError>(
          "fake", hwsec::TPMRetryAction::kSpaceNotFound));

  TestFuture<CryptohomeStatus> result;
  auth_block_->PrepareForRemoval(ObfuscatedUsername(kObfuscatedUsername),
                                 auth_state, result.GetCallback());
  EXPECT_TRUE(result.IsReady());
  EXPECT_THAT(result.Take(), NotOk());
  // Prepare for removal again should still succeed when the label doesn't exist
  // in the tree.
  TestFuture<CryptohomeStatus> retry_result;
  auth_block_->PrepareForRemoval(ObfuscatedUsername(kObfuscatedUsername),
                                 auth_state, retry_result.GetCallback());
  EXPECT_TRUE(retry_result.IsReady());
  EXPECT_THAT(retry_result.Take(), IsOk());
}

// Test implementation of the non-pinweaver blocks. Just implements the calls
// with mock functions. We use the ECC backed TPM identifier, but that doesn't
// really matter it could be any block used for passwords.
class TestPasswordAuthBlock : public NonPinweaverPasswordAuthBlock {
 public:
  TestPasswordAuthBlock(AsyncInitFeatures& features,
                        const hwsec::CryptohomeFrontend& hwsec)
      : NonPinweaverPasswordAuthBlock(kTpmBackedEcc, features, hwsec) {}

  MOCK_METHOD(void,
              Create,
              (const AuthInput& user_input,
               const AuthFactorMetadata& auth_factor_metadata,
               CreateCallback callback),
              (override));
  MOCK_METHOD(void,
              DerivePassword,
              (const AuthInput& auth_input,
               const AuthFactorMetadata& auth_factor_metadata,
               const AuthBlockState& state,
               DeriveCallback callback),
              (override));
};

class NonPinweaverPasswordAuthBlockTest : public ::testing::Test {
 public:
  NonPinweaverPasswordAuthBlockTest() : auth_block_(features_.async, hwsec_) {}

 protected:
  // Fake location for when we want the mock to return an error.
  const CryptohomeError::ErrorLocationPair kErrorLocationForTesting =
      CryptohomeError::ErrorLocationPair(
          static_cast<CryptohomeError::ErrorLocation>(1), std::string("test"));

  base::test::TaskEnvironment task_environment_;

  FakeFeaturesForTesting features_;
  StrictMock<hwsec::MockCryptohomeFrontend> hwsec_;
  TestPasswordAuthBlock auth_block_;
};

TEST_F(NonPinweaverPasswordAuthBlockTest, DeriveSuggestsRecreate) {
  features_.SetDefaultForFeature(Features::kPinweaverForPassword, true);
  KeyBlobs* key_blobs_ptr = nullptr;
  EXPECT_CALL(hwsec_, IsReady()).WillOnce(ReturnValue(true));
  EXPECT_CALL(hwsec_, IsPinWeaverEnabled()).WillOnce(ReturnValue(true));
  EXPECT_CALL(auth_block_, DerivePassword(_, _, _, _))
      .WillOnce([&](auto, auto, auto, AuthBlock::DeriveCallback callback) {
        auto key_blobs = std::make_unique<KeyBlobs>();
        key_blobs_ptr = key_blobs.get();
        std::move(callback).Run(OkStatus<CryptohomeError>(),
                                std::move(key_blobs), std::nullopt);
      });

  DeriveTestFuture result;
  auth_block_.Derive({}, {}, {}, result.GetCallback());

  ASSERT_THAT(result.IsReady(), IsTrue());
  auto [status, key_blobs, suggested_action] = result.Take();
  EXPECT_THAT(status, IsOk());
  EXPECT_THAT(key_blobs.get(), Eq(key_blobs_ptr));
  EXPECT_THAT(suggested_action, Eq(AuthBlock::SuggestedAction::kRecreate));
}

TEST_F(NonPinweaverPasswordAuthBlockTest, DeriveSuggestsNothingIfNoFeature) {
  features_.SetDefaultForFeature(Features::kPinweaverForPassword, false);
  KeyBlobs* key_blobs_ptr = nullptr;
  EXPECT_CALL(auth_block_, DerivePassword(_, _, _, _))
      .WillOnce([&](auto, auto, auto, AuthBlock::DeriveCallback callback) {
        auto key_blobs = std::make_unique<KeyBlobs>();
        key_blobs_ptr = key_blobs.get();
        std::move(callback).Run(OkStatus<CryptohomeError>(),
                                std::move(key_blobs), std::nullopt);
      });

  DeriveTestFuture result;
  auth_block_.Derive({}, {}, {}, result.GetCallback());

  ASSERT_THAT(result.IsReady(), IsTrue());
  auto [status, key_blobs, suggested_action] = result.Take();
  EXPECT_THAT(status, IsOk());
  EXPECT_THAT(key_blobs.get(), Eq(key_blobs_ptr));
  EXPECT_THAT(suggested_action, Eq(std::nullopt));
}

TEST_F(NonPinweaverPasswordAuthBlockTest, DeriveSuggestsNothingIfNoPinweaver) {
  features_.SetDefaultForFeature(Features::kPinweaverForPassword, true);
  KeyBlobs* key_blobs_ptr = nullptr;
  EXPECT_CALL(hwsec_, IsReady()).WillOnce(ReturnValue(true));
  EXPECT_CALL(hwsec_, IsPinWeaverEnabled()).WillOnce(ReturnValue(false));
  EXPECT_CALL(auth_block_, DerivePassword(_, _, _, _))
      .WillOnce([&](auto, auto, auto, AuthBlock::DeriveCallback callback) {
        auto key_blobs = std::make_unique<KeyBlobs>();
        key_blobs_ptr = key_blobs.get();
        std::move(callback).Run(OkStatus<CryptohomeError>(),
                                std::move(key_blobs), std::nullopt);
      });

  DeriveTestFuture result;
  auth_block_.Derive({}, {}, {}, result.GetCallback());

  ASSERT_THAT(result.IsReady(), IsTrue());
  auto [status, key_blobs, suggested_action] = result.Take();
  EXPECT_THAT(status, IsOk());
  EXPECT_THAT(key_blobs.get(), Eq(key_blobs_ptr));
  EXPECT_THAT(suggested_action, Eq(std::nullopt));
}

TEST_F(NonPinweaverPasswordAuthBlockTest, DeriveSuggestsNothingOnError) {
  features_.SetDefaultForFeature(Features::kPinweaverForPassword, true);
  EXPECT_CALL(auth_block_, DerivePassword(_, _, _, _))
      .WillOnce([&](auto, auto, auto, AuthBlock::DeriveCallback callback) {
        std::move(callback).Run(MakeStatus<CryptohomeError>(
                                    kErrorLocationForTesting, ErrorActionSet()),
                                nullptr, std::nullopt);
      });

  DeriveTestFuture result;
  auth_block_.Derive({}, {}, {}, result.GetCallback());

  ASSERT_THAT(result.IsReady(), IsTrue());
  auto [status, key_blobs, suggested_action] = result.Take();
  EXPECT_THAT(status, NotOk());
  EXPECT_THAT(key_blobs.get(), Eq(nullptr));
  EXPECT_THAT(suggested_action, Eq(std::nullopt));
}

}  // namespace cryptohome
