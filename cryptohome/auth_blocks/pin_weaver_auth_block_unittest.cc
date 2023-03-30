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
#include <gtest/gtest.h>
#include <libhwsec/factory/tpm2_simulator_factory_for_test.h>
#include <libhwsec/frontend/recovery_crypto/mock_frontend.h>
#include <libhwsec-foundation/crypto/aes.h>
#include <libhwsec-foundation/crypto/rsa.h>
#include <libhwsec-foundation/crypto/scrypt.h>
#include <libhwsec-foundation/error/testing_helper.h>

#include "cryptohome/auth_blocks/auth_block_utils.h"
#include "cryptohome/crypto.h"
#include "cryptohome/crypto_error.h"
#include "cryptohome/fake_features.h"
#include "cryptohome/fake_platform.h"
#include "cryptohome/flatbuffer_schemas/auth_block_state.h"
#include "cryptohome/mock_cryptohome_keys_manager.h"
#include "cryptohome/mock_le_credential_manager.h"
#include "cryptohome/vault_keyset.h"

namespace cryptohome {
namespace {

using cryptohome::error::CryptohomeError;
using cryptohome::error::CryptohomeLECredError;
using cryptohome::error::ErrorActionSet;
using cryptohome::error::PossibleAction;

using ::hwsec_foundation::DeriveSecretsScrypt;
using ::hwsec_foundation::kAesBlockSize;
using ::hwsec_foundation::kDefaultAesKeySize;
using ::hwsec_foundation::kDefaultPassBlobSize;
using ::hwsec_foundation::error::testing::ReturnError;
using ::hwsec_foundation::error::testing::ReturnValue;
using ::testing::_;
using ::testing::DoAll;
using ::testing::Exactly;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SaveArg;

constexpr char kObfuscatedUsername[] = "OBFUSCATED_USERNAME";

}  // namespace

TEST(PinWeaverAuthBlockTest, CreateTest) {
  // Set up inputs to the test.
  brillo::SecureBlob vault_key(20, 'C');
  brillo::SecureBlob reset_secret(32, 'S');

  // Set up the mock expectations.
  brillo::SecureBlob le_secret;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  NiceMock<MockLECredentialManager> le_cred_manager;
  EXPECT_CALL(le_cred_manager, InsertCredential(_, _, _, _, _, _, _))
      .WillOnce(
          DoAll(SaveArg<1>(&le_secret), ReturnError<CryptohomeLECredError>()));

  // Call the Create() method.
  AuthInput user_input = {vault_key,
                          /*locked_to_single_user=*/std::nullopt, Username(),
                          ObfuscatedUsername(kObfuscatedUsername),
                          reset_secret};
  KeyBlobs vkk_data;

  FakeFeaturesForTesting features;
  PinWeaverAuthBlock auth_block(features.async, &le_cred_manager);
  AuthBlockState auth_state;
  EXPECT_TRUE(auth_block.Create(user_input, &auth_state, &vkk_data).ok());
  EXPECT_TRUE(
      std::holds_alternative<PinWeaverAuthBlockState>(auth_state.state));

  auto& pin_state = std::get<PinWeaverAuthBlockState>(auth_state.state);

  EXPECT_TRUE(pin_state.salt.has_value());
  const brillo::SecureBlob& salt = pin_state.salt.value();
  brillo::SecureBlob le_secret_result(kDefaultAesKeySize);
  EXPECT_TRUE(DeriveSecretsScrypt(vault_key, salt, {&le_secret_result}));
  EXPECT_EQ(le_secret, le_secret_result);
}

TEST(PinWeaverAuthBlockTest, CreateFailureLeManager) {
  const CryptohomeError::ErrorLocationPair kErrorLocationForTesting1 =
      CryptohomeError::ErrorLocationPair(
          static_cast<::cryptohome::error::CryptohomeError::ErrorLocation>(1),
          std::string("Testing1"));

  brillo::SecureBlob vault_key(20, 'C');
  brillo::SecureBlob reset_secret(32, 'S');

  // Now test that the method fails if the le_cred_manager fails.
  NiceMock<MockLECredentialManager> le_cred_manager_fail;
  ON_CALL(le_cred_manager_fail, InsertCredential(_, _, _, _, _, _, _))
      .WillByDefault(ReturnError<CryptohomeLECredError>(
          kErrorLocationForTesting1, ErrorActionSet({PossibleAction::kFatal}),
          LECredError::LE_CRED_ERROR_HASH_TREE));

  FakeFeaturesForTesting features;
  PinWeaverAuthBlock auth_block_fail(features.async, &le_cred_manager_fail);
  // Call the Create() method.
  AuthInput user_input = {vault_key,
                          /*locked_to_single_user=*/std::nullopt, Username(),
                          ObfuscatedUsername(kObfuscatedUsername),
                          reset_secret};
  KeyBlobs vkk_data;
  AuthBlockState auth_state;
  EXPECT_EQ(CryptoError::CE_OTHER_CRYPTO,
            auth_block_fail.Create(user_input, &auth_state, &vkk_data)
                ->local_crypto_error());
}

// Test PinWeaverAuthBlock create fails when there's no user_input provided.
TEST(PinWeaverAuthBlockTest, CreateFailureNoUserInput) {
  brillo::SecureBlob reset_secret(32, 'S');

  NiceMock<MockLECredentialManager> le_cred_manager;
  FakeFeaturesForTesting features;
  PinWeaverAuthBlock auth_block(features.async, &le_cred_manager);

  AuthInput auth_input = {
      .obfuscated_username = ObfuscatedUsername(kObfuscatedUsername),
      .reset_secret = reset_secret};
  KeyBlobs vkk_data;
  AuthBlockState auth_state;
  EXPECT_EQ(CryptoError::CE_OTHER_CRYPTO,
            auth_block.Create(auth_input, &auth_state, &vkk_data)
                ->local_crypto_error());
}

// Test PinWeaverAuthBlock create fails when there's no obfuscated_username
// provided.
TEST(PinWeaverAuthBlockTest, CreateFailureNoObfuscated) {
  brillo::SecureBlob user_input(20, 'C');
  brillo::SecureBlob reset_secret(32, 'S');

  NiceMock<MockLECredentialManager> le_cred_manager;
  FakeFeaturesForTesting features;
  PinWeaverAuthBlock auth_block(features.async, &le_cred_manager);

  AuthInput auth_input = {.user_input = user_input,
                          .reset_secret = reset_secret};
  KeyBlobs vkk_data;
  AuthBlockState auth_state;
  EXPECT_EQ(CryptoError::CE_OTHER_CRYPTO,
            auth_block.Create(auth_input, &auth_state, &vkk_data)
                ->local_crypto_error());
}

// Test PinWeaverAuthBlock create fails when there's no reset_secret provided.
TEST(PinWeaverAuthBlockTest, CreateFailureNoResetSecret) {
  brillo::SecureBlob user_input(20, 'C');

  NiceMock<MockLECredentialManager> le_cred_manager;
  FakeFeaturesForTesting features;
  PinWeaverAuthBlock auth_block(features.async, &le_cred_manager);

  AuthInput auth_input = {
      .user_input = user_input,
      .obfuscated_username = ObfuscatedUsername(kObfuscatedUsername)};
  KeyBlobs vkk_data;
  AuthBlockState auth_state;
  EXPECT_EQ(CryptoError::CE_OTHER_CRYPTO,
            auth_block.Create(auth_input, &auth_state, &vkk_data)
                ->local_crypto_error());
}

// Check required field |le_label| in PinWeaverAuthBlockState.
TEST(PinWeaverAuthBlockTest, DeriveFailureMissingLeLabel) {
  brillo::SecureBlob user_input(20, 'C');
  brillo::SecureBlob salt(PKCS5_SALT_LEN, 'A');
  brillo::SecureBlob chaps_iv(kAesBlockSize, 'F');
  brillo::SecureBlob fek_iv(kAesBlockSize, 'X');

  NiceMock<MockLECredentialManager> le_cred_manager;
  FakeFeaturesForTesting features;
  PinWeaverAuthBlock auth_block(features.async, &le_cred_manager);

  // Construct the auth block state. le_label is not set.
  AuthBlockState auth_state;
  PinWeaverAuthBlockState state;
  state.salt = salt;
  state.chaps_iv = chaps_iv;
  state.fek_iv = fek_iv;
  auth_state.state = std::move(state);

  KeyBlobs key_blobs;
  AuthInput auth_input = {.user_input = user_input};
  EXPECT_EQ(CryptoError::CE_OTHER_CRYPTO,
            auth_block.Derive(auth_input, auth_state, &key_blobs)
                ->local_crypto_error());
}

// Check required field |salt| in PinWeaverAuthBlockState.
TEST(PinWeaverAuthBlockTest, DeriveFailureMissingSalt) {
  brillo::SecureBlob user_input(20, 'C');
  brillo::SecureBlob chaps_iv(kAesBlockSize, 'F');
  brillo::SecureBlob fek_iv(kAesBlockSize, 'X');

  NiceMock<MockLECredentialManager> le_cred_manager;
  FakeFeaturesForTesting features;
  PinWeaverAuthBlock auth_block(features.async, &le_cred_manager);

  // Construct the auth block state. salt is not set.
  AuthBlockState auth_state;
  PinWeaverAuthBlockState state;
  state.le_label = 0;
  state.chaps_iv = chaps_iv;
  state.fek_iv = fek_iv;
  auth_state.state = std::move(state);

  KeyBlobs key_blobs;
  AuthInput auth_input = {.user_input = user_input};
  EXPECT_EQ(CryptoError::CE_OTHER_CRYPTO,
            auth_block.Derive(auth_input, auth_state, &key_blobs)
                ->local_crypto_error());
}

// Check PinWeaverAuthBlock derive fails if user_input is missing.
TEST(PinWeaverAuthBlockTest, DeriveFailureNoUserInput) {
  brillo::SecureBlob chaps_iv(kAesBlockSize, 'F');
  brillo::SecureBlob fek_iv(kAesBlockSize, 'X');
  brillo::SecureBlob salt(PKCS5_SALT_LEN, 'A');

  NiceMock<MockLECredentialManager> le_cred_manager;
  FakeFeaturesForTesting features;
  PinWeaverAuthBlock auth_block(features.async, &le_cred_manager);

  // Construct the auth block state.
  AuthBlockState auth_state;
  PinWeaverAuthBlockState state = {
      .le_label = 0, .salt = salt, .chaps_iv = chaps_iv, .fek_iv = fek_iv};
  auth_state.state = std::move(state);

  KeyBlobs key_blobs;
  AuthInput auth_input = {};
  EXPECT_EQ(CryptoError::CE_OTHER_CRYPTO,
            auth_block.Derive(auth_input, auth_state, &key_blobs)
                ->local_crypto_error());
}

TEST(PinWeaverAuthBlockTest, DeriveTest) {
  brillo::SecureBlob vault_key(20, 'C');
  brillo::SecureBlob salt(PKCS5_SALT_LEN, 'A');
  brillo::SecureBlob chaps_iv(kAesBlockSize, 'F');
  brillo::SecureBlob fek_iv(kAesBlockSize, 'X');

  brillo::SecureBlob le_secret(kDefaultAesKeySize);
  ASSERT_TRUE(DeriveSecretsScrypt(vault_key, salt, {&le_secret}));

  NiceMock<MockLECredentialManager> le_cred_manager;
  ON_CALL(le_cred_manager, CheckCredential(_, _, _, _))
      .WillByDefault(ReturnError<CryptohomeLECredError>());
  EXPECT_CALL(le_cred_manager, CheckCredential(_, le_secret, _, _))
      .Times(Exactly(1));
  FakeFeaturesForTesting features;
  PinWeaverAuthBlock auth_block(features.async, &le_cred_manager);

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

  KeyBlobs key_blobs;
  AuthInput auth_input = {vault_key};
  EXPECT_TRUE(auth_block.Derive(auth_input, auth_state, &key_blobs).ok());

  // Set expectations of the key blobs.
  EXPECT_NE(key_blobs.reset_secret, std::nullopt);
  EXPECT_NE(key_blobs.chaps_iv, std::nullopt);
  EXPECT_NE(key_blobs.vkk_iv, std::nullopt);

  // PinWeaver should always use unique IVs.
  EXPECT_NE(key_blobs.chaps_iv.value(), key_blobs.vkk_iv.value());
}

// Test that derive function works as intended when fek_iv and le_chaps_iv is
// not set.
TEST(PinWeaverAuthBlockTest, DeriveOptionalValuesTest) {
  brillo::SecureBlob vault_key(20, 'C');
  brillo::SecureBlob salt(PKCS5_SALT_LEN, 'A');

  brillo::SecureBlob le_secret(kDefaultAesKeySize);
  ASSERT_TRUE(DeriveSecretsScrypt(vault_key, salt, {&le_secret}));

  NiceMock<MockLECredentialManager> le_cred_manager;
  ON_CALL(le_cred_manager, CheckCredential(_, _, _, _))
      .WillByDefault(ReturnError<CryptohomeLECredError>());
  EXPECT_CALL(le_cred_manager, CheckCredential(_, le_secret, _, _))
      .Times(Exactly(1));
  FakeFeaturesForTesting features;
  PinWeaverAuthBlock auth_block(features.async, &le_cred_manager);

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

  KeyBlobs key_blobs;
  AuthInput auth_input = {vault_key};
  EXPECT_TRUE(auth_block.Derive(auth_input, auth_state, &key_blobs).ok());

  // Set expectations of the key blobs.
  EXPECT_NE(key_blobs.reset_secret, std::nullopt);
  // We expect this to be null because it was not set earlier.
  EXPECT_EQ(key_blobs.chaps_iv, std::nullopt);
  EXPECT_EQ(key_blobs.vkk_iv, std::nullopt);
}

TEST(PinWeaverAuthBlockTest, CheckCredentialFailureTest) {
  const CryptohomeError::ErrorLocationPair kErrorLocationForTesting1 =
      CryptohomeError::ErrorLocationPair(
          static_cast<::cryptohome::error::CryptohomeError::ErrorLocation>(1),
          std::string("Testing1"));

  brillo::SecureBlob vault_key(20, 'C');
  brillo::SecureBlob salt(PKCS5_SALT_LEN, 'A');
  brillo::SecureBlob chaps_iv(kAesBlockSize, 'F');
  brillo::SecureBlob fek_iv(kAesBlockSize, 'X');

  brillo::SecureBlob le_secret(kDefaultAesKeySize);
  ASSERT_TRUE(DeriveSecretsScrypt(vault_key, salt, {&le_secret}));

  NiceMock<MockLECredentialManager> le_cred_manager;
  ON_CALL(le_cred_manager, CheckCredential(_, _, _, _))
      .WillByDefault(ReturnError<CryptohomeLECredError>(
          kErrorLocationForTesting1, ErrorActionSet({PossibleAction::kFatal}),
          LECredError::LE_CRED_ERROR_INVALID_LE_SECRET));
  EXPECT_CALL(le_cred_manager, CheckCredential(_, le_secret, _, _))
      .Times(Exactly(1));
  EXPECT_CALL(le_cred_manager, GetDelayInSeconds(_)).WillOnce(ReturnValue(0));
  FakeFeaturesForTesting features;
  PinWeaverAuthBlock auth_block(features.async, &le_cred_manager);

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

  KeyBlobs key_blobs;
  AuthInput auth_input = {vault_key};
  EXPECT_EQ(CryptoError::CE_LE_INVALID_SECRET,
            auth_block.Derive(auth_input, auth_state, &key_blobs)
                ->local_crypto_error());
}

TEST(PinWeaverAuthBlockTest, CheckCredentialFailureLeFiniteTimeout) {
  const CryptohomeError::ErrorLocationPair kErrorLocationForTesting1 =
      CryptohomeError::ErrorLocationPair(
          static_cast<::cryptohome::error::CryptohomeError::ErrorLocation>(1),
          std::string("Testing1"));

  brillo::SecureBlob vault_key(20, 'C');
  brillo::SecureBlob salt(PKCS5_SALT_LEN, 'A');
  brillo::SecureBlob chaps_iv(kAesBlockSize, 'F');
  brillo::SecureBlob fek_iv(kAesBlockSize, 'X');

  brillo::SecureBlob le_secret(kDefaultAesKeySize);
  ASSERT_TRUE(DeriveSecretsScrypt(vault_key, salt, {&le_secret}));

  NiceMock<MockLECredentialManager> le_cred_manager;
  ON_CALL(le_cred_manager, CheckCredential(_, _, _, _))
      .WillByDefault(ReturnError<CryptohomeLECredError>(
          kErrorLocationForTesting1, ErrorActionSet({PossibleAction::kFatal}),
          LECredError::LE_CRED_ERROR_TOO_MANY_ATTEMPTS));
  EXPECT_CALL(le_cred_manager, CheckCredential(_, le_secret, _, _))
      .Times(Exactly(1));
  // Simulate a 30 second timeout duration on the le credential.
  EXPECT_CALL(le_cred_manager, GetDelayInSeconds(_)).WillOnce(ReturnValue(30));
  FakeFeaturesForTesting features;
  PinWeaverAuthBlock auth_block(features.async, &le_cred_manager);

  PinWeaverAuthBlockState state;
  state.le_label = 0;
  state.salt = salt;
  state.chaps_iv = chaps_iv;
  state.fek_iv = fek_iv;
  AuthBlockState auth_state;
  auth_state.state = std::move(state);

  KeyBlobs key_blobs;
  AuthInput auth_input = {vault_key};
  EXPECT_EQ(CryptoError::CE_TPM_DEFEND_LOCK,
            auth_block.Derive(auth_input, auth_state, &key_blobs)
                ->local_crypto_error());
}

TEST(PinWeaverAuthBlockTest, CheckCredentialNotFatalCryptoErrorTest) {
  const CryptohomeError::ErrorLocationPair kErrorLocationForTesting1 =
      CryptohomeError::ErrorLocationPair(
          static_cast<::cryptohome::error::CryptohomeError::ErrorLocation>(1),
          std::string("Testing1"));

  brillo::SecureBlob vault_key(20, 'C');
  brillo::SecureBlob salt(PKCS5_SALT_LEN, 'A');
  brillo::SecureBlob chaps_iv(kAesBlockSize, 'F');
  brillo::SecureBlob fek_iv(kAesBlockSize, 'X');

  brillo::SecureBlob le_secret(kDefaultAesKeySize);
  ASSERT_TRUE(DeriveSecretsScrypt(vault_key, salt, {&le_secret}));

  NiceMock<MockLECredentialManager> le_cred_manager;
  ON_CALL(le_cred_manager, CheckCredential(_, _, _, _))
      .WillByDefault(ReturnError<CryptohomeLECredError>(
          kErrorLocationForTesting1, ErrorActionSet({PossibleAction::kFatal}),
          LE_CRED_ERROR_HASH_TREE));
  EXPECT_CALL(le_cred_manager, CheckCredential(_, le_secret, _, _))
      .WillOnce(ReturnError<CryptohomeLECredError>(
          kErrorLocationForTesting1, ErrorActionSet({PossibleAction::kFatal}),
          LE_CRED_ERROR_INVALID_LE_SECRET))
      .WillOnce(ReturnError<CryptohomeLECredError>(
          kErrorLocationForTesting1, ErrorActionSet({PossibleAction::kFatal}),
          LE_CRED_ERROR_INVALID_RESET_SECRET))
      .WillOnce(ReturnError<CryptohomeLECredError>(
          kErrorLocationForTesting1, ErrorActionSet({PossibleAction::kFatal}),
          LE_CRED_ERROR_TOO_MANY_ATTEMPTS))
      .WillOnce(ReturnError<CryptohomeLECredError>(
          kErrorLocationForTesting1, ErrorActionSet({PossibleAction::kFatal}),
          LE_CRED_ERROR_HASH_TREE))
      .WillOnce(ReturnError<CryptohomeLECredError>(
          kErrorLocationForTesting1, ErrorActionSet({PossibleAction::kFatal}),
          LE_CRED_ERROR_INVALID_LABEL))
      .WillOnce(ReturnError<CryptohomeLECredError>(
          kErrorLocationForTesting1, ErrorActionSet({PossibleAction::kFatal}),
          LE_CRED_ERROR_NO_FREE_LABEL))
      .WillOnce(ReturnError<CryptohomeLECredError>(
          kErrorLocationForTesting1, ErrorActionSet({PossibleAction::kFatal}),
          LE_CRED_ERROR_INVALID_METADATA))
      .WillOnce(ReturnError<CryptohomeLECredError>(
          kErrorLocationForTesting1, ErrorActionSet({PossibleAction::kFatal}),
          LE_CRED_ERROR_UNCLASSIFIED))
      .WillOnce(ReturnError<CryptohomeLECredError>(
          kErrorLocationForTesting1, ErrorActionSet({PossibleAction::kFatal}),
          LE_CRED_ERROR_LE_LOCKED))
      .WillOnce(ReturnError<CryptohomeLECredError>(
          kErrorLocationForTesting1, ErrorActionSet({PossibleAction::kFatal}),
          LE_CRED_ERROR_PCR_NOT_MATCH));
  EXPECT_CALL(le_cred_manager, GetDelayInSeconds(_))
      .WillRepeatedly(ReturnValue(0));
  FakeFeaturesForTesting features;
  PinWeaverAuthBlock auth_block(features.async, &le_cred_manager);

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

  KeyBlobs key_blobs;
  AuthInput auth_input = {vault_key};
  for (int i = 0; i < 10; i++) {
    CryptoStatus error = auth_block.Derive(auth_input, auth_state, &key_blobs);
    EXPECT_NE(CryptoError::CE_TPM_FATAL, error->local_crypto_error());
    EXPECT_NE(CryptoError::CE_OTHER_FATAL, error->local_crypto_error());
  }
}

}  // namespace cryptohome
