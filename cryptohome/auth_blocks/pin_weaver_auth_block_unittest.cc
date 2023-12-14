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
#include <gtest/gtest.h>
#include <libhwsec/backend/pinweaver_manager/pinweaver_manager.h>
#include <libhwsec/error/pinweaver_error.h>
#include <libhwsec/error/tpm_error.h>
#include <libhwsec/error/tpm_retry_action.h>
#include <libhwsec/factory/tpm2_simulator_factory_for_test.h>
#include <libhwsec/frontend/pinweaver_manager/mock_frontend.h>
#include <libhwsec/frontend/recovery_crypto/mock_frontend.h>
#include <libhwsec-foundation/crypto/aes.h>
#include <libhwsec-foundation/crypto/rsa.h>
#include <libhwsec-foundation/crypto/scrypt.h>
#include <libhwsec-foundation/crypto/secure_box.h>
#include <libhwsec-foundation/error/testing_helper.h>

#include "cryptohome/auth_blocks/auth_block_utils.h"
#include "cryptohome/crypto.h"
#include "cryptohome/crypto_error.h"
#include "cryptohome/error/cryptohome_tpm_error.h"
#include "cryptohome/fake_features.h"
#include "cryptohome/fake_platform.h"
#include "cryptohome/flatbuffer_schemas/auth_block_state.h"
#include "cryptohome/flatbuffer_schemas/auth_factor.h"
#include "cryptohome/mock_cryptohome_keys_manager.h"
#include "cryptohome/recoverable_key_store/mock_backend_cert_provider.h"
#include "cryptohome/recoverable_key_store/type.h"
#include "cryptohome/vault_keyset.h"

namespace cryptohome {
namespace {

using base::test::TestFuture;
using cryptohome::error::CryptohomeError;
using cryptohome::error::CryptohomeTPMError;
using cryptohome::error::ErrorActionSet;
using cryptohome::error::PossibleAction;
using hwsec_foundation::error::testing::IsOk;
using hwsec_foundation::error::testing::NotOk;
using hwsec_foundation::error::testing::OkStatus;

using ::hwsec_foundation::DeriveSecretsScrypt;
using ::hwsec_foundation::kAesBlockSize;
using ::hwsec_foundation::kDefaultAesKeySize;
using ::hwsec_foundation::kDefaultPassBlobSize;
using ::hwsec_foundation::error::testing::ReturnError;
using ::hwsec_foundation::error::testing::ReturnOk;
using ::hwsec_foundation::error::testing::ReturnValue;
using ::testing::_;
using ::testing::DoAll;
using ::testing::Exactly;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SaveArg;

constexpr char kObfuscatedUsername[] = "OBFUSCATED_USERNAME";

using CreateTestFuture = TestFuture<CryptohomeStatus,
                                    std::unique_ptr<KeyBlobs>,
                                    std::unique_ptr<AuthBlockState>>;

using DeriveTestFuture = TestFuture<CryptohomeStatus,
                                    std::unique_ptr<KeyBlobs>,
                                    std::optional<AuthBlock::SuggestedAction>>;

constexpr size_t kSecurityDomainWrappingKeySize = 32;
constexpr size_t kPinInputSaltSize = 32;

std::optional<SecurityDomainKeys> GetValidSecurityDomainKeys() {
  const brillo::SecureBlob kSeed("seed_abc");
  const brillo::SecureBlob kWrappingKey(kSecurityDomainWrappingKeySize, 0xAA);
  std::optional<hwsec_foundation::secure_box::KeyPair> key_pair =
      hwsec_foundation::secure_box::DeriveKeyPairFromSeed(kSeed);
  if (!key_pair.has_value()) {
    return std::nullopt;
  }
  return SecurityDomainKeys{.key_pair = *key_pair,
                            .wrapping_key = kWrappingKey};
}

std::optional<RecoverableKeyStoreBackendCert> GetValidBackendCert() {
  const brillo::SecureBlob kSeed("seed_123");
  std::optional<hwsec_foundation::secure_box::KeyPair> key_pair =
      hwsec_foundation::secure_box::DeriveKeyPairFromSeed(kSeed);
  if (!key_pair.has_value()) {
    return std::nullopt;
  }
  return RecoverableKeyStoreBackendCert{
      .version = 1000,
      .public_key = key_pair->public_key,
  };
}

}  // namespace

class PinWeaverAuthBlockTest : public ::testing::Test {
 public:
  void SetUp() override {
    auth_block_ = std::make_unique<PinWeaverAuthBlock>(
        features_.async, &cert_provider_, &hwsec_pw_manager_);
  }

 protected:
  base::test::TaskEnvironment task_environment_;

  NiceMock<hwsec::MockPinWeaverManagerFrontend> hwsec_pw_manager_;
  FakeFeaturesForTesting features_;
  NiceMock<MockRecoverableKeyStoreBackendCertProvider> cert_provider_;
  std::unique_ptr<PinWeaverAuthBlock> auth_block_;
};

TEST_F(PinWeaverAuthBlockTest, CreateTest) {
  // Set up inputs to the test.
  brillo::SecureBlob vault_key(20, 'C');
  brillo::SecureBlob reset_secret(32, 'S');

  // Set up the mock expectations.
  brillo::SecureBlob le_secret;
  DelaySchedule delay_sched;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  EXPECT_CALL(hwsec_pw_manager_, InsertCredential(_, _, _, _, _, _))
      .WillOnce(DoAll(SaveArg<1>(&le_secret), SaveArg<4>(&delay_sched),
                      ReturnValue(/* ret_label */ 0)));

  // Call the Create() method.
  AuthInput user_input = {vault_key,
                          /*locked_to_single_user=*/std::nullopt, Username(),
                          ObfuscatedUsername(kObfuscatedUsername),
                          reset_secret};
  KeyBlobs vkk_data;

  features_.SetDefaultForFeature(Features::kModernPin, true);

  CreateTestFuture result;
  auth_block_->Create(user_input, {}, result.GetCallback());
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
                      ReturnValue(/* ret_label */ 0)));

  // Call the Create() method.
  AuthInput user_input = {vault_key,
                          /*locked_to_single_user=*/std::nullopt, Username(),
                          ObfuscatedUsername(kObfuscatedUsername),
                          reset_secret};
  features_.SetDefaultForFeature(Features::kMigratePin, false);
  CreateTestFuture result;
  auth_block_->Create(user_input, {}, result.GetCallback());
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

TEST_F(PinWeaverAuthBlockTest, CreateFailureLeManager) {
  const CryptohomeError::ErrorLocationPair kErrorLocationForTesting1 =
      CryptohomeError::ErrorLocationPair(
          static_cast<::cryptohome::error::CryptohomeError::ErrorLocation>(1),
          std::string("Testing1"));

  brillo::SecureBlob vault_key(20, 'C');
  brillo::SecureBlob reset_secret(32, 'S');

  // Now test that the method fails if the le_cred_manager fails.
  ON_CALL(hwsec_pw_manager_, InsertCredential(_, _, _, _, _, _))
      .WillByDefault(ReturnError<hwsec::TPMError>(
          "fake", hwsec::TPMRetryAction::kNoRetry));

  // Call the Create() method.
  AuthInput user_input = {vault_key,
                          /*locked_to_single_user=*/std::nullopt, Username(),
                          ObfuscatedUsername(kObfuscatedUsername),
                          reset_secret};

  CreateTestFuture result;
  auth_block_->Create(user_input, {}, result.GetCallback());
  ASSERT_TRUE(result.IsReady());
  auto [status, key_blobs, auth_state] = result.Take();

  ASSERT_THAT(status, NotOk());
  EXPECT_EQ(CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED,
            status->local_legacy_error());
}

// Test PinWeaverAuthBlock create fails when there's no user_input provided.
TEST_F(PinWeaverAuthBlockTest, CreateFailureNoUserInput) {
  brillo::SecureBlob reset_secret(32, 'S');

  AuthInput auth_input = {
      .obfuscated_username = ObfuscatedUsername(kObfuscatedUsername),
      .reset_secret = reset_secret};
  CreateTestFuture result;
  auth_block_->Create(auth_input, {}, result.GetCallback());
  ASSERT_TRUE(result.IsReady());
  auto [status, key_blobs, auth_state] = result.Take();

  ASSERT_THAT(status, NotOk());
  EXPECT_EQ(CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED,
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
  auth_block_->Create(auth_input, {}, result.GetCallback());
  ASSERT_TRUE(result.IsReady());
  auto [status, key_blobs, auth_state] = result.Take();

  ASSERT_THAT(status, NotOk());
  EXPECT_EQ(CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED,
            status->local_legacy_error());
}

// Test PinWeaverAuthBlock create fails when there's no reset_secret provided.
TEST_F(PinWeaverAuthBlockTest, CreateFailureNoResetSecret) {
  brillo::SecureBlob user_input(20, 'C');

  AuthInput auth_input = {
      .user_input = user_input,
      .obfuscated_username = ObfuscatedUsername(kObfuscatedUsername)};
  CreateTestFuture result;
  auth_block_->Create(auth_input, {}, result.GetCallback());
  ASSERT_TRUE(result.IsReady());
  auto [status, key_blobs, auth_state] = result.Take();

  ASSERT_THAT(status, NotOk());
  EXPECT_EQ(CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED,
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
  DeriveTestFuture result;
  auth_block_->Derive(auth_input, auth_state, result.GetCallback());
  ASSERT_TRUE(result.IsReady());
  auto [status, key_blobs, suggested_action] = result.Take();
  ASSERT_THAT(status, NotOk());

  ASSERT_THAT(CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED,
              status->local_legacy_error());
}

TEST_F(PinWeaverAuthBlockTest, CreateTestWithRecoverableKeyStore) {
  // Set up inputs to the test.
  brillo::SecureBlob vault_key(20, 'C');
  brillo::SecureBlob reset_secret(32, 'S');

  // Set up the mock expectations.
  brillo::SecureBlob le_secret;
  DelaySchedule delay_sched;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  EXPECT_CALL(hwsec_pw_manager_, InsertCredential(_, _, _, _, _, _))
      .WillOnce(DoAll(SaveArg<1>(&le_secret), SaveArg<4>(&delay_sched),
                      ReturnValue(/* ret_label */ 0)));

  std::optional<RecoverableKeyStoreBackendCert> backend_cert =
      GetValidBackendCert();
  ASSERT_TRUE(backend_cert.has_value());
  EXPECT_CALL(cert_provider_, GetBackendCert).WillOnce(Return(*backend_cert));

  std::optional<SecurityDomainKeys> security_domain_keys =
      GetValidSecurityDomainKeys();
  ASSERT_TRUE(security_domain_keys.has_value());

  // Call the Create() method.
  AuthInput user_input = {
      .user_input = vault_key,
      .locked_to_single_user = std::nullopt,
      .obfuscated_username = ObfuscatedUsername(kObfuscatedUsername),
      .reset_secret = reset_secret,
      .security_domain_keys = *security_domain_keys,
  };
  AuthFactorMetadata metadata = {
      .metadata = PinMetadata{
          .hash_info = SerializedKnowledgeFactorHashInfo{
              .algorithm = SerializedLockScreenKnowledgeFactorHashAlgorithm::
                  PBKDF2_AES256_1234,
              .salt = brillo::Blob(kPinInputSaltSize, 0xBB),
          }}};
  KeyBlobs vkk_data;

  features_.SetDefaultForFeature(Features::kModernPin, true);
  features_.SetDefaultForFeature(Features::kGenerateRecoverableKeyStore, true);

  CreateTestFuture result;
  auth_block_->Create(user_input, metadata, result.GetCallback());
  ASSERT_TRUE(result.IsReady());
  auto [status, key_blobs, auth_state] = result.Take();
  ASSERT_THAT(status, IsOk());
  EXPECT_TRUE(
      std::holds_alternative<PinWeaverAuthBlockState>(auth_state->state));
  EXPECT_TRUE(auth_state->recoverable_key_store_state.has_value());
}

TEST_F(PinWeaverAuthBlockTest, CreateTestWithRecoverableKeyStoreDisabled) {
  // Set up inputs to the test.
  brillo::SecureBlob vault_key(20, 'C');
  brillo::SecureBlob reset_secret(32, 'S');

  // Set up the mock expectations.
  brillo::SecureBlob le_secret;
  DelaySchedule delay_sched;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  EXPECT_CALL(hwsec_pw_manager_, InsertCredential(_, _, _, _, _, _))
      .WillOnce(DoAll(SaveArg<1>(&le_secret), SaveArg<4>(&delay_sched),
                      ReturnValue(/* ret_label */ 0)));

  EXPECT_CALL(cert_provider_, GetBackendCert).Times(0);

  std::optional<SecurityDomainKeys> security_domain_keys =
      GetValidSecurityDomainKeys();
  ASSERT_TRUE(security_domain_keys.has_value());

  // Call the Create() method.
  AuthInput user_input = {
      .user_input = vault_key,
      .locked_to_single_user = std::nullopt,
      .obfuscated_username = ObfuscatedUsername(kObfuscatedUsername),
      .reset_secret = reset_secret,
      .security_domain_keys = *security_domain_keys,
  };
  AuthFactorMetadata metadata = {
      .metadata = PinMetadata{
          .hash_info = SerializedKnowledgeFactorHashInfo{
              .algorithm = SerializedLockScreenKnowledgeFactorHashAlgorithm::
                  PBKDF2_AES256_1234,
              .salt = brillo::Blob(kPinInputSaltSize, 0xBB),
          }}};
  KeyBlobs vkk_data;

  features_.SetDefaultForFeature(Features::kModernPin, true);
  // If the feature is disabled, no recoverable key store state should be
  // generated.
  features_.SetDefaultForFeature(Features::kGenerateRecoverableKeyStore, false);

  CreateTestFuture result;
  auth_block_->Create(user_input, metadata, result.GetCallback());
  ASSERT_TRUE(result.IsReady());
  auto [status, key_blobs, auth_state] = result.Take();
  ASSERT_THAT(status, IsOk());
  EXPECT_TRUE(
      std::holds_alternative<PinWeaverAuthBlockState>(auth_state->state));
  EXPECT_FALSE(auth_state->recoverable_key_store_state.has_value());
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
  DeriveTestFuture result;
  auth_block_->Derive(auth_input, auth_state, result.GetCallback());
  ASSERT_TRUE(result.IsReady());
  auto [status, key_blobs, suggested_action] = result.Take();
  ASSERT_THAT(status, NotOk());

  ASSERT_THAT(CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED,
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
  DeriveTestFuture result;
  auth_block_->Derive(auth_input, auth_state, result.GetCallback());
  ASSERT_TRUE(result.IsReady());
  auto [status, key_blobs, suggested_action] = result.Take();
  ASSERT_THAT(status, NotOk());

  ASSERT_THAT(CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED,
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

  DeriveTestFuture result;
  auth_block_->Derive(auth_input, auth_state, result.GetCallback());
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
  DeriveTestFuture result;
  auth_block_->Derive(auth_input, auth_state, result.GetCallback());
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
  DeriveTestFuture result;
  auth_block_->Derive(auth_input, auth_state, result.GetCallback());
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
  DeriveTestFuture result;
  auth_block_->Derive(auth_input, auth_state, result.GetCallback());
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
  DeriveTestFuture result;
  auth_block_->Derive(auth_input, auth_state, result.GetCallback());
  ASSERT_TRUE(result.IsReady());
  auto [status, key_blobs, suggested_action] = result.Take();
  ASSERT_THAT(status, NotOk());

  ASSERT_THAT(CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED,
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
  DeriveTestFuture result;
  auth_block_->Derive(auth_input, auth_state, result.GetCallback());
  ASSERT_TRUE(result.IsReady());
  auto [status, key_blobs, suggested_action] = result.Take();
  ASSERT_THAT(status, NotOk());

  ASSERT_THAT(CRYPTOHOME_ERROR_TPM_DEFEND_LOCK, status->local_legacy_error());
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
  for (int i = 0; i < 6; i++) {
    DeriveTestFuture result;
    auth_block_->Derive(auth_input, auth_state, result.GetCallback());
    ASSERT_TRUE(result.IsReady());
    auto [status, key_blobs, suggested_action] = result.Take();
    EXPECT_NE(CRYPTOHOME_ERROR_VAULT_UNRECOVERABLE,
              status->local_legacy_error());
    EXPECT_NE(CRYPTOHOME_ERROR_VAULT_UNRECOVERABLE,
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

}  // namespace cryptohome
