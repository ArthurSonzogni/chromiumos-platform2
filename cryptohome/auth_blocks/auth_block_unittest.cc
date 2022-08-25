// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_blocks/auth_block.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <base/files/file_path.h>
#include <gtest/gtest.h>
#include <libhwsec-foundation/crypto/aes.h>
#include <libhwsec-foundation/crypto/rsa.h>
#include <libhwsec-foundation/crypto/scrypt.h>
#include <libhwsec-foundation/error/testing_helper.h>

#include "cryptohome/auth_blocks/auth_block_utils.h"
#include "cryptohome/auth_blocks/cryptohome_recovery_auth_block.h"
#include "cryptohome/auth_blocks/double_wrapped_compat_auth_block.h"
#include "cryptohome/auth_blocks/libscrypt_compat_auth_block.h"
#include "cryptohome/auth_blocks/pin_weaver_auth_block.h"
#include "cryptohome/auth_blocks/tpm_bound_to_pcr_auth_block.h"
#include "cryptohome/auth_blocks/tpm_ecc_auth_block.h"
#include "cryptohome/auth_blocks/tpm_not_bound_to_pcr_auth_block.h"
#include "cryptohome/crypto.h"
#include "cryptohome/crypto_error.h"
#include "cryptohome/cryptorecovery/fake_recovery_mediator_crypto.h"
#include "cryptohome/cryptorecovery/recovery_crypto_fake_tpm_backend_impl.h"
#include "cryptohome/cryptorecovery/recovery_crypto_hsm_cbor_serialization.h"
#include "cryptohome/cryptorecovery/recovery_crypto_impl.h"
#include "cryptohome/fake_platform.h"
#include "cryptohome/flatbuffer_schemas/auth_block_state.h"
#include "cryptohome/mock_cryptohome_keys_manager.h"
#include "cryptohome/mock_le_credential_manager.h"
#include "cryptohome/mock_tpm.h"
#include "cryptohome/vault_keyset.h"

using cryptohome::cryptorecovery::FakeRecoveryMediatorCrypto;
using cryptohome::cryptorecovery::RecoveryCryptoImpl;
using cryptohome::error::CryptohomeError;
using cryptohome::error::CryptohomeLECredError;
using cryptohome::error::ErrorAction;
using cryptohome::error::ErrorActionSet;

using ::hwsec::TPMError;
using ::hwsec::TPMErrorBase;
using ::hwsec::TPMRetryAction;
using ::hwsec_foundation::DeriveSecretsScrypt;
using ::hwsec_foundation::kAesBlockSize;
using ::hwsec_foundation::kDefaultAesKeySize;
using ::hwsec_foundation::kDefaultPassBlobSize;
using ::hwsec_foundation::error::testing::ReturnError;
using ::hwsec_foundation::error::testing::ReturnValue;
using ::testing::_;
using ::testing::DoAll;
using ::testing::Exactly;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::SetArgPointee;

namespace cryptohome {
namespace {
constexpr char kObfuscatedUsername[] = "OBFUSCATED_USERNAME";

TpmEccAuthBlockState GetDefaultEccAuthBlockState() {
  TpmEccAuthBlockState auth_block_state;
  auth_block_state.salt = brillo::SecureBlob(32, 'A');
  auth_block_state.vkk_iv = brillo::SecureBlob(32, 'B');
  auth_block_state.sealed_hvkkm = brillo::SecureBlob(32, 'C');
  auth_block_state.extended_sealed_hvkkm = brillo::SecureBlob(32, 'D');
  auth_block_state.auth_value_rounds = 5;
  return auth_block_state;
}

void SetupMockHwsec(NiceMock<hwsec::MockCryptohomeFrontend>& hwsec) {
  ON_CALL(hwsec, GetPubkeyHash(_))
      .WillByDefault(ReturnValue(brillo::BlobFromString("public key hash")));
  ON_CALL(hwsec, IsEnabled()).WillByDefault(ReturnValue(true));
  ON_CALL(hwsec, IsReady()).WillByDefault(ReturnValue(true));
}
}  // namespace

TEST(TpmBoundToPcrTest, CreateTest) {
  // Set up inputs to the test.
  brillo::SecureBlob vault_key(20, 'C');
  SerializedVaultKeyset serialized;

  // Set up the mock expectations.
  brillo::SecureBlob scrypt_derived_key;
  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  brillo::SecureBlob auth_value(256, 'a');

  SetupMockHwsec(hwsec);

  EXPECT_CALL(hwsec, GetAuthValue(_, _))
      .WillOnce(
          DoAll(SaveArg<1>(&scrypt_derived_key), ReturnValue(auth_value)));
  EXPECT_CALL(hwsec, SealWithCurrentUser(_, auth_value, _)).Times(Exactly(2));
  ON_CALL(hwsec, SealWithCurrentUser(_, _, _))
      .WillByDefault(ReturnValue(brillo::Blob()));

  AuthInput user_input = {vault_key,
                          /*locked_to_single_user=*/std::nullopt,
                          kObfuscatedUsername,
                          /*reset_secret=*/std::nullopt};
  KeyBlobs vkk_data;

  TpmBoundToPcrAuthBlock auth_block(&hwsec, &cryptohome_keys_manager);
  AuthBlockState auth_state;
  EXPECT_TRUE(auth_block.Create(user_input, &auth_state, &vkk_data).ok());
  EXPECT_TRUE(
      std::holds_alternative<TpmBoundToPcrAuthBlockState>(auth_state.state));

  EXPECT_NE(vkk_data.vkk_key, std::nullopt);
  EXPECT_NE(vkk_data.vkk_iv, std::nullopt);
  EXPECT_NE(vkk_data.chaps_iv, std::nullopt);

  auto& tpm_state = std::get<TpmBoundToPcrAuthBlockState>(auth_state.state);

  EXPECT_TRUE(tpm_state.salt.has_value());
  const brillo::SecureBlob& salt = tpm_state.salt.value();
  brillo::SecureBlob scrypt_derived_key_result(kDefaultPassBlobSize);
  EXPECT_TRUE(
      DeriveSecretsScrypt(vault_key, salt, {&scrypt_derived_key_result}));
  EXPECT_EQ(scrypt_derived_key, scrypt_derived_key_result);
}

TEST(TpmBoundToPcrTest, CreateFailTpm) {
  // Set up inputs to the test.
  brillo::SecureBlob vault_key(20, 'C');
  SerializedVaultKeyset serialized;

  // Set up the mock expectations.
  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;

  brillo::SecureBlob auth_value(256, 'a');

  SetupMockHwsec(hwsec);

  EXPECT_CALL(hwsec, GetAuthValue(_, _))
      .WillOnce(DoAll(ReturnValue(brillo::Blob())));

  ON_CALL(hwsec, SealWithCurrentUser(_, _, _))
      .WillByDefault(ReturnError<TPMError>("fake", TPMRetryAction::kNoRetry));

  AuthInput user_input = {vault_key,
                          /*locked_to_single_user=*/std::nullopt,
                          kObfuscatedUsername,
                          /*reset_secret=*/std::nullopt};
  KeyBlobs vkk_data;
  TpmBoundToPcrAuthBlock auth_block(&hwsec, &cryptohome_keys_manager);
  AuthBlockState auth_state;
  EXPECT_EQ(CryptoError::CE_TPM_CRYPTO,
            auth_block.Create(user_input, &auth_state, &vkk_data)
                ->local_crypto_error());
}

// Test the Create operation fails when there's no user_input provided.
TEST(TpmBoundToPcrTest, CreateFailNoUserInput) {
  // Prepare.
  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  TpmBoundToPcrAuthBlock auth_block(&hwsec, &cryptohome_keys_manager);
  AuthInput auth_input = {.obfuscated_username = kObfuscatedUsername};

  // Test.
  AuthBlockState auth_state;
  KeyBlobs vkk_data;
  EXPECT_EQ(CryptoError::CE_OTHER_CRYPTO,
            auth_block.Create(auth_input, &auth_state, &vkk_data)
                ->local_crypto_error());
}

// Test the Create operation fails when there's no obfuscated_username provided.
TEST(TpmBoundToPcrTest, CreateFailNoObfuscated) {
  // Prepare.
  brillo::SecureBlob user_input(20, 'C');
  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  TpmBoundToPcrAuthBlock auth_block(&hwsec, &cryptohome_keys_manager);
  AuthInput auth_input = {.user_input = user_input};

  // Test.
  AuthBlockState auth_state;
  KeyBlobs vkk_data;
  EXPECT_EQ(CryptoError::CE_OTHER_CRYPTO,
            auth_block.Create(auth_input, &auth_state, &vkk_data)
                ->local_crypto_error());
}

TEST(TpmNotBoundToPcrTest, Success) {
  // Set up inputs to the test.
  brillo::SecureBlob vault_key(20, 'C');
  SerializedVaultKeyset serialized;

  // Set up the mock expectations.
  brillo::Blob encrypt_out(64, 'X');
  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;

  SetupMockHwsec(hwsec);

  EXPECT_CALL(hwsec, Encrypt(_, _)).WillOnce(ReturnValue(encrypt_out));
  EXPECT_CALL(hwsec, GetPubkeyHash(_)).WillOnce(ReturnValue(brillo::Blob()));

  AuthInput user_input = {vault_key,
                          /*locked_to_single_user=*/std::nullopt,
                          kObfuscatedUsername,
                          /*reset_secret=*/std::nullopt};
  KeyBlobs vkk_data;
  TpmNotBoundToPcrAuthBlock auth_block(&hwsec, &cryptohome_keys_manager);
  AuthBlockState auth_state;
  EXPECT_TRUE(auth_block.Create(user_input, &auth_state, &vkk_data).ok());
  EXPECT_TRUE(
      std::holds_alternative<TpmNotBoundToPcrAuthBlockState>(auth_state.state));

  EXPECT_NE(vkk_data.vkk_key, std::nullopt);
  EXPECT_NE(vkk_data.vkk_iv, std::nullopt);
  EXPECT_NE(vkk_data.chaps_iv, std::nullopt);

  auto& tpm_state = std::get<TpmNotBoundToPcrAuthBlockState>(auth_state.state);

  EXPECT_TRUE(tpm_state.salt.has_value());
  const brillo::SecureBlob& salt = tpm_state.salt.value();
  brillo::SecureBlob aes_skey_result(kDefaultAesKeySize);
  EXPECT_TRUE(DeriveSecretsScrypt(vault_key, salt, {&aes_skey_result}));

  brillo::SecureBlob tpm_key_retult;
  EXPECT_TRUE(hwsec_foundation::ObscureRsaMessage(
      brillo::SecureBlob(encrypt_out.begin(), encrypt_out.end()),
      aes_skey_result, &tpm_key_retult));

  EXPECT_EQ(tpm_state.tpm_key.value(), tpm_key_retult);

  EXPECT_CALL(hwsec, Decrypt(_, encrypt_out))
      .WillOnce(ReturnValue(brillo::SecureBlob()));

  TpmNotBoundToPcrAuthBlockState state;
  state.scrypt_derived = true;
  state.salt = tpm_state.salt.value();
  state.tpm_key = tpm_key_retult;
  auth_state.state = std::move(state);

  KeyBlobs key_blobs;
  AuthInput auth_input = {.user_input = vault_key};
  EXPECT_TRUE(auth_block.Derive(auth_input, auth_state, &key_blobs).ok());
}

TEST(TpmNotBoundToPcrTest, CreateFailTpm) {
  // Set up inputs to the test.
  brillo::SecureBlob vault_key(20, 'C');
  SerializedVaultKeyset serialized;

  // Set up the mock expectations.
  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  ON_CALL(hwsec, Encrypt(_, _))
      .WillByDefault(ReturnError<TPMError>("fake", TPMRetryAction::kNoRetry));

  AuthInput user_input = {vault_key,
                          /*locked_to_single_user=*/std::nullopt,
                          kObfuscatedUsername,
                          /*reset_secret=*/std::nullopt};
  KeyBlobs vkk_data;
  TpmNotBoundToPcrAuthBlock auth_block(&hwsec, &cryptohome_keys_manager);
  AuthBlockState auth_state;
  EXPECT_EQ(CryptoError::CE_TPM_CRYPTO,
            auth_block.Create(user_input, &auth_state, &vkk_data)
                ->local_crypto_error());
}

// Test the Create operation fails when there's no user_input provided.
TEST(TpmNotBoundToPcrTest, CreateFailNoUserInput) {
  // Prepare.
  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  TpmNotBoundToPcrAuthBlock auth_block(&hwsec, &cryptohome_keys_manager);
  AuthInput auth_input;

  // Test.
  AuthBlockState auth_state;
  KeyBlobs vkk_data;
  EXPECT_EQ(auth_block.Create(auth_input, &auth_state, &vkk_data)
                ->local_crypto_error(),
            CryptoError::CE_OTHER_CRYPTO);
}

// Check required field |salt| in TpmNotBoundToPcrAuthBlockState.
TEST(TpmNotBoundToPcrTest, DeriveFailureMissingSalt) {
  brillo::SecureBlob tpm_key(20, 'C');
  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  TpmNotBoundToPcrAuthBlock auth_block(&hwsec, &cryptohome_keys_manager);

  AuthBlockState auth_state;
  TpmNotBoundToPcrAuthBlockState state;
  state.scrypt_derived = true;
  state.tpm_key = tpm_key;
  auth_state.state = std::move(state);

  KeyBlobs key_blobs;
  AuthInput auth_input = {};
  EXPECT_EQ(CryptoError::CE_OTHER_CRYPTO,
            auth_block.Derive(auth_input, auth_state, &key_blobs)
                ->local_crypto_error());
}

// Check required field |tpm_key| in TpmNotBoundToPcrAuthBlockState.
TEST(TpmNotBoundToPcrTest, DeriveFailureMissingTpmKey) {
  brillo::SecureBlob salt(PKCS5_SALT_LEN, 'A');
  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  TpmNotBoundToPcrAuthBlock auth_block(&hwsec, &cryptohome_keys_manager);

  AuthBlockState auth_state;
  TpmNotBoundToPcrAuthBlockState state;
  state.scrypt_derived = true;
  state.salt = salt;
  auth_state.state = std::move(state);

  KeyBlobs key_blobs;
  AuthInput auth_input = {};
  EXPECT_EQ(CryptoError::CE_OTHER_CRYPTO,
            auth_block.Derive(auth_input, auth_state, &key_blobs)
                ->local_crypto_error());
}

// Test TpmNotBoundToPcrAuthBlock derive fails when there's no user_input
// provided.
TEST(TpmNotBoundToPcrTest, DeriveFailureNoUserInput) {
  brillo::SecureBlob tpm_key(20, 'C');
  brillo::SecureBlob salt(PKCS5_SALT_LEN, 'A');
  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  TpmNotBoundToPcrAuthBlock auth_block(&hwsec, &cryptohome_keys_manager);

  AuthBlockState auth_state;
  TpmBoundToPcrAuthBlockState state;
  state.scrypt_derived = true;
  state.salt = salt;
  state.tpm_key = tpm_key;
  state.extended_tpm_key = tpm_key;
  auth_state.state = std::move(state);

  AuthInput auth_input;
  KeyBlobs key_blobs;
  EXPECT_EQ(auth_block.Derive(auth_input, auth_state, &key_blobs)
                ->local_crypto_error(),
            CryptoError::CE_OTHER_CRYPTO);
}

TEST(TpmNotBoundToPcrTest, DeriveSuccess) {
  brillo::SecureBlob tpm_key(20, 'A');
  brillo::SecureBlob salt(PKCS5_SALT_LEN, 'B');
  brillo::SecureBlob vault_key(20, 'C');
  brillo::SecureBlob aes_key(kDefaultAesKeySize);
  brillo::SecureBlob encrypt_out(64, 'X');
  ASSERT_TRUE(DeriveSecretsScrypt(vault_key, salt, {&aes_key}));
  ASSERT_TRUE(
      hwsec_foundation::ObscureRsaMessage(encrypt_out, aes_key, &tpm_key));

  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  SetupMockHwsec(hwsec);
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  TpmNotBoundToPcrAuthBlock auth_block(&hwsec, &cryptohome_keys_manager);

  brillo::Blob encrypt_out_blob(encrypt_out.begin(), encrypt_out.end());
  EXPECT_CALL(hwsec, Decrypt(_, encrypt_out_blob))
      .WillOnce(ReturnValue(brillo::SecureBlob()));
  AuthBlockState auth_state;
  TpmNotBoundToPcrAuthBlockState state;
  state.scrypt_derived = true;
  state.salt = salt;
  state.tpm_key = tpm_key;
  auth_state.state = std::move(state);

  KeyBlobs key_blobs;
  AuthInput auth_input = {.user_input = vault_key};
  EXPECT_TRUE(auth_block.Derive(auth_input, auth_state, &key_blobs).ok());
}

TEST(PinWeaverAuthBlockTest, CreateTest) {
  // Set up inputs to the test.
  brillo::SecureBlob vault_key(20, 'C');
  brillo::SecureBlob reset_secret(32, 'S');

  // Set up the mock expectations.
  brillo::SecureBlob le_secret;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  NiceMock<MockLECredentialManager> le_cred_manager;
  EXPECT_CALL(le_cred_manager, InsertCredential(_, _, _, _, _, _))
      .WillOnce(
          DoAll(SaveArg<1>(&le_secret), ReturnError<CryptohomeLECredError>()));

  // Call the Create() method.
  AuthInput user_input = {vault_key,
                          /*locked_to_single_user=*/std::nullopt,
                          kObfuscatedUsername, reset_secret};
  KeyBlobs vkk_data;

  PinWeaverAuthBlock auth_block(&le_cred_manager, &cryptohome_keys_manager);
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
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager_fail;
  NiceMock<MockLECredentialManager> le_cred_manager_fail;
  ON_CALL(le_cred_manager_fail, InsertCredential(_, _, _, _, _, _))
      .WillByDefault(ReturnError<CryptohomeLECredError>(
          kErrorLocationForTesting1, ErrorActionSet({ErrorAction::kFatal}),
          LECredError::LE_CRED_ERROR_HASH_TREE));

  PinWeaverAuthBlock auth_block_fail(&le_cred_manager_fail,
                                     &cryptohome_keys_manager_fail);
  // Call the Create() method.
  AuthInput user_input = {vault_key,
                          /*locked_to_single_user=*/std::nullopt,
                          kObfuscatedUsername, reset_secret};
  KeyBlobs vkk_data;
  AuthBlockState auth_state;
  EXPECT_EQ(CryptoError::CE_OTHER_CRYPTO,
            auth_block_fail.Create(user_input, &auth_state, &vkk_data)
                ->local_crypto_error());
}

// Test PinWeaverAuthBlock create fails when there's no user_input provided.
TEST(PinWeaverAuthBlockTest, CreateFailureNoUserInput) {
  brillo::SecureBlob reset_secret(32, 'S');

  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  NiceMock<MockLECredentialManager> le_cred_manager;

  PinWeaverAuthBlock auth_block(&le_cred_manager, &cryptohome_keys_manager);
  AuthInput auth_input = {.obfuscated_username = kObfuscatedUsername,
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

  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  NiceMock<MockLECredentialManager> le_cred_manager;

  PinWeaverAuthBlock auth_block(&le_cred_manager, &cryptohome_keys_manager);
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

  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  NiceMock<MockLECredentialManager> le_cred_manager;

  PinWeaverAuthBlock auth_block(&le_cred_manager, &cryptohome_keys_manager);
  AuthInput auth_input = {.user_input = user_input,
                          .obfuscated_username = kObfuscatedUsername};
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
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  PinWeaverAuthBlock auth_block(&le_cred_manager, &cryptohome_keys_manager);

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
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  PinWeaverAuthBlock auth_block(&le_cred_manager, &cryptohome_keys_manager);

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
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  PinWeaverAuthBlock auth_block(&le_cred_manager, &cryptohome_keys_manager);

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

  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  PinWeaverAuthBlock auth_block(&le_cred_manager, &cryptohome_keys_manager);

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

  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  PinWeaverAuthBlock auth_block(&le_cred_manager, &cryptohome_keys_manager);

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
          kErrorLocationForTesting1, ErrorActionSet({ErrorAction::kFatal}),
          LECredError::LE_CRED_ERROR_INVALID_LE_SECRET));
  EXPECT_CALL(le_cred_manager, CheckCredential(_, le_secret, _, _))
      .Times(Exactly(1));
  EXPECT_CALL(le_cred_manager, GetDelayInSeconds(_)).WillOnce(ReturnValue(0));

  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  PinWeaverAuthBlock auth_block(&le_cred_manager, &cryptohome_keys_manager);

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
          kErrorLocationForTesting1, ErrorActionSet({ErrorAction::kFatal}),
          LE_CRED_ERROR_HASH_TREE));
  EXPECT_CALL(le_cred_manager, CheckCredential(_, le_secret, _, _))
      .WillOnce(ReturnError<CryptohomeLECredError>(
          kErrorLocationForTesting1, ErrorActionSet({ErrorAction::kFatal}),
          LE_CRED_ERROR_INVALID_LE_SECRET))
      .WillOnce(ReturnError<CryptohomeLECredError>(
          kErrorLocationForTesting1, ErrorActionSet({ErrorAction::kFatal}),
          LE_CRED_ERROR_INVALID_RESET_SECRET))
      .WillOnce(ReturnError<CryptohomeLECredError>(
          kErrorLocationForTesting1, ErrorActionSet({ErrorAction::kFatal}),
          LE_CRED_ERROR_TOO_MANY_ATTEMPTS))
      .WillOnce(ReturnError<CryptohomeLECredError>(
          kErrorLocationForTesting1, ErrorActionSet({ErrorAction::kFatal}),
          LE_CRED_ERROR_HASH_TREE))
      .WillOnce(ReturnError<CryptohomeLECredError>(
          kErrorLocationForTesting1, ErrorActionSet({ErrorAction::kFatal}),
          LE_CRED_ERROR_INVALID_LABEL))
      .WillOnce(ReturnError<CryptohomeLECredError>(
          kErrorLocationForTesting1, ErrorActionSet({ErrorAction::kFatal}),
          LE_CRED_ERROR_NO_FREE_LABEL))
      .WillOnce(ReturnError<CryptohomeLECredError>(
          kErrorLocationForTesting1, ErrorActionSet({ErrorAction::kFatal}),
          LE_CRED_ERROR_INVALID_METADATA))
      .WillOnce(ReturnError<CryptohomeLECredError>(
          kErrorLocationForTesting1, ErrorActionSet({ErrorAction::kFatal}),
          LE_CRED_ERROR_UNCLASSIFIED))
      .WillOnce(ReturnError<CryptohomeLECredError>(
          kErrorLocationForTesting1, ErrorActionSet({ErrorAction::kFatal}),
          LE_CRED_ERROR_LE_LOCKED))
      .WillOnce(ReturnError<CryptohomeLECredError>(
          kErrorLocationForTesting1, ErrorActionSet({ErrorAction::kFatal}),
          LE_CRED_ERROR_PCR_NOT_MATCH));
  EXPECT_CALL(le_cred_manager, GetDelayInSeconds(_))
      .WillRepeatedly(ReturnValue(0));

  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  PinWeaverAuthBlock auth_block(&le_cred_manager, &cryptohome_keys_manager);

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

TEST(TPMAuthBlockTest, DecryptBoundToPcrTest) {
  brillo::SecureBlob vault_key(20, 'C');
  brillo::SecureBlob tpm_key(20, 'B');
  brillo::SecureBlob salt(PKCS5_SALT_LEN, 'A');

  brillo::SecureBlob vkk_iv(kDefaultAesKeySize);
  brillo::SecureBlob vkk_key;

  brillo::SecureBlob pass_blob(kDefaultPassBlobSize);
  ASSERT_TRUE(DeriveSecretsScrypt(vault_key, salt, {&pass_blob}));

  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  ScopedKeyHandle handle;

  SetupMockHwsec(hwsec);

  EXPECT_CALL(hwsec, PreloadSealedData(_)).WillOnce(Invoke([&](auto&&) {
    return hwsec::ScopedKey(hwsec::Key{.token = 5566},
                            hwsec.GetFakeMiddlewareDerivative());
  }));
  brillo::SecureBlob auth_value(256, 'a');
  EXPECT_CALL(hwsec, GetAuthValue(_, pass_blob))
      .WillOnce(ReturnValue(auth_value));
  EXPECT_CALL(hwsec, UnsealWithCurrentUser(_, auth_value, _))
      .WillOnce([](std::optional<hwsec::Key> preload_data, auto&&, auto&&) {
        EXPECT_TRUE(preload_data.has_value());
        EXPECT_EQ(preload_data->token, 5566);
        return brillo::SecureBlob();
      });

  TpmBoundToPcrAuthBlock tpm_auth_block(&hwsec, &cryptohome_keys_manager);
  EXPECT_TRUE(
      tpm_auth_block
          .DecryptTpmBoundToPcr(vault_key, tpm_key, salt, &vkk_iv, &vkk_key)
          .ok());
}

TEST(TPMAuthBlockTest, DecryptBoundToPcrNoPreloadTest) {
  brillo::SecureBlob vault_key(20, 'C');
  brillo::SecureBlob tpm_key(20, 'B');
  brillo::SecureBlob salt(PKCS5_SALT_LEN, 'A');

  brillo::SecureBlob vkk_iv(kDefaultAesKeySize);
  brillo::SecureBlob vkk_key;

  brillo::SecureBlob pass_blob(kDefaultPassBlobSize);
  ASSERT_TRUE(DeriveSecretsScrypt(vault_key, salt, {&pass_blob}));

  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  SetupMockHwsec(hwsec);
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  ScopedKeyHandle handle;
  EXPECT_CALL(hwsec, PreloadSealedData(_)).WillOnce(ReturnValue(std::nullopt));
  brillo::SecureBlob auth_value(256, 'a');
  EXPECT_CALL(hwsec, GetAuthValue(_, pass_blob))
      .WillOnce(ReturnValue(auth_value));
  EXPECT_CALL(hwsec, UnsealWithCurrentUser(_, auth_value, _))
      .WillOnce([](std::optional<hwsec::Key> preload_data, auto&&, auto&&) {
        EXPECT_FALSE(preload_data.has_value());
        return brillo::SecureBlob();
      });

  TpmBoundToPcrAuthBlock tpm_auth_block(&hwsec, &cryptohome_keys_manager);
  EXPECT_TRUE(
      tpm_auth_block
          .DecryptTpmBoundToPcr(vault_key, tpm_key, salt, &vkk_iv, &vkk_key)
          .ok());
}

TEST(TPMAuthBlockTest, DecryptBoundToPcrPreloadFailedTest) {
  brillo::SecureBlob vault_key(20, 'C');
  brillo::SecureBlob tpm_key(20, 'B');
  brillo::SecureBlob salt(PKCS5_SALT_LEN, 'A');

  brillo::SecureBlob vkk_iv(kDefaultAesKeySize);
  brillo::SecureBlob vkk_key;

  brillo::SecureBlob pass_blob(kDefaultPassBlobSize);
  ASSERT_TRUE(DeriveSecretsScrypt(vault_key, salt, {&pass_blob}));

  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  SetupMockHwsec(hwsec);
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  ScopedKeyHandle handle;
  EXPECT_CALL(hwsec, PreloadSealedData(_))
      .WillOnce(ReturnError<TPMError>("fake", TPMRetryAction::kNoRetry));

  TpmBoundToPcrAuthBlock tpm_auth_block(&hwsec, &cryptohome_keys_manager);
  EXPECT_FALSE(
      tpm_auth_block
          .DecryptTpmBoundToPcr(vault_key, tpm_key, salt, &vkk_iv, &vkk_key)
          .ok());
}

TEST(TPMAuthBlockTest, DecryptNotBoundToPcrTest) {
  brillo::SecureBlob vault_key(20, 'C');
  brillo::SecureBlob tpm_key;
  brillo::SecureBlob salt(PKCS5_SALT_LEN, 'A');

  brillo::SecureBlob vkk_key;
  brillo::SecureBlob vkk_iv(kDefaultAesKeySize);
  brillo::SecureBlob aes_key(kDefaultAesKeySize);
  ASSERT_TRUE(DeriveSecretsScrypt(vault_key, salt, {&aes_key}));

  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  SetupMockHwsec(hwsec);
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;

  brillo::Blob encrypt_out(64, 'X');
  EXPECT_TRUE(hwsec_foundation::ObscureRsaMessage(
      brillo::SecureBlob(encrypt_out.begin(), encrypt_out.end()), aes_key,
      &tpm_key));
  EXPECT_CALL(hwsec, Decrypt(_, encrypt_out))
      .WillOnce(ReturnValue(brillo::SecureBlob()));

  TpmNotBoundToPcrAuthBlockState tpm_state;
  tpm_state.scrypt_derived = true;
  tpm_state.password_rounds = 0x5000;

  TpmNotBoundToPcrAuthBlock tpm_auth_block(&hwsec, &cryptohome_keys_manager);
  EXPECT_TRUE(tpm_auth_block
                  .DecryptTpmNotBoundToPcr(tpm_state, vault_key, tpm_key, salt,
                                           &vkk_iv, &vkk_key)
                  .ok());
}

TEST(TpmAuthBlockTest, DeriveTest) {
  SerializedVaultKeyset serialized;
  serialized.set_flags(SerializedVaultKeyset::TPM_WRAPPED |
                       SerializedVaultKeyset::PCR_BOUND |
                       SerializedVaultKeyset::SCRYPT_DERIVED);

  brillo::SecureBlob key(20, 'B');
  brillo::SecureBlob tpm_key(20, 'C');
  std::string salt(PKCS5_SALT_LEN, 'A');

  serialized.set_salt(salt);
  serialized.set_tpm_key(tpm_key.data(), tpm_key.size());
  serialized.set_extended_tpm_key(tpm_key.data(), tpm_key.size());

  // Make sure TpmAuthBlock calls DecryptTpmBoundToPcr in this case.
  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  SetupMockHwsec(hwsec);
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  EXPECT_CALL(hwsec, PreloadSealedData(_)).WillOnce(ReturnValue(std::nullopt));
  EXPECT_CALL(hwsec, GetAuthValue(_, _))
      .WillOnce(ReturnValue(brillo::SecureBlob()));
  EXPECT_CALL(hwsec, UnsealWithCurrentUser(_, _, _))
      .WillOnce(ReturnValue(brillo::SecureBlob()));

  TpmBoundToPcrAuthBlock auth_block(&hwsec, &cryptohome_keys_manager);

  KeyBlobs key_out_data;
  AuthInput auth_input;
  auth_input.user_input = key;
  auth_input.locked_to_single_user = false;

  VaultKeyset vk;
  vk.InitializeFromSerialized(serialized);
  AuthBlockState auth_state;
  EXPECT_TRUE(GetAuthBlockState(vk, auth_state));

  EXPECT_TRUE(auth_block.Derive(auth_input, auth_state, &key_out_data).ok());

  // Assert that the returned key blobs isn't uninitialized.
  EXPECT_NE(key_out_data.vkk_iv, std::nullopt);
  EXPECT_NE(key_out_data.vkk_key, std::nullopt);
  EXPECT_EQ(key_out_data.vkk_iv.value(), key_out_data.chaps_iv.value());
}

// Test TpmBoundToPcrAuthBlock derive fails when there's no user_input provided.
TEST(TpmAuthBlockTest, DeriveFailureNoUserInput) {
  brillo::SecureBlob tpm_key(20, 'C');
  brillo::SecureBlob salt(PKCS5_SALT_LEN, 'A');
  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  TpmBoundToPcrAuthBlock auth_block(&hwsec, &cryptohome_keys_manager);

  AuthBlockState auth_state;
  TpmBoundToPcrAuthBlockState state;
  state.scrypt_derived = true;
  state.salt = salt;
  state.tpm_key = tpm_key;
  state.extended_tpm_key = tpm_key;
  auth_state.state = std::move(state);

  AuthInput auth_input = {};
  KeyBlobs key_blobs;
  EXPECT_EQ(CryptoError::CE_OTHER_CRYPTO,
            auth_block.Derive(auth_input, auth_state, &key_blobs)
                ->local_crypto_error());
}

// Check required field |salt| in TpmBoundToPcrAuthBlockState.
TEST(TpmAuthBlockTest, DeriveFailureMissingSalt) {
  brillo::SecureBlob tpm_key(20, 'C');
  brillo::SecureBlob user_input("foo");
  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  TpmBoundToPcrAuthBlock auth_block(&hwsec, &cryptohome_keys_manager);

  AuthBlockState auth_state;
  TpmBoundToPcrAuthBlockState state;
  state.scrypt_derived = true;
  state.tpm_key = tpm_key;
  state.extended_tpm_key = tpm_key;
  auth_state.state = std::move(state);

  KeyBlobs key_blobs;
  AuthInput auth_input = {.user_input = user_input};
  EXPECT_EQ(CryptoError::CE_OTHER_CRYPTO,
            auth_block.Derive(auth_input, auth_state, &key_blobs)
                ->local_crypto_error());
}

// Check required field |tpm_key| in TpmBoundToPcrAuthBlockState.
TEST(TpmAuthBlockTest, DeriveFailureMissingTpmKey) {
  brillo::SecureBlob tpm_key(20, 'C');
  brillo::SecureBlob salt(PKCS5_SALT_LEN, 'A');
  brillo::SecureBlob user_input("foo");
  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  TpmBoundToPcrAuthBlock auth_block(&hwsec, &cryptohome_keys_manager);

  AuthBlockState auth_state;
  TpmBoundToPcrAuthBlockState state;
  state.scrypt_derived = true;
  state.salt = salt;
  state.extended_tpm_key = tpm_key;
  auth_state.state = std::move(state);

  KeyBlobs key_blobs;
  AuthInput auth_input = {.user_input = user_input};
  EXPECT_EQ(CryptoError::CE_OTHER_CRYPTO,
            auth_block.Derive(auth_input, auth_state, &key_blobs)
                ->local_crypto_error());
}

// Check required field |extended_tpm_key| in TpmBoundToPcrAuthBlockState.
TEST(TpmAuthBlockTest, DeriveFailureMissingExtendedTpmKey) {
  brillo::SecureBlob tpm_key(20, 'C');
  brillo::SecureBlob salt(PKCS5_SALT_LEN, 'A');
  brillo::SecureBlob user_input("foo");
  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  TpmBoundToPcrAuthBlock auth_block(&hwsec, &cryptohome_keys_manager);

  AuthBlockState auth_state;
  TpmBoundToPcrAuthBlockState state;
  state.scrypt_derived = true;
  state.salt = salt;
  state.tpm_key = tpm_key;
  auth_state.state = std::move(state);

  KeyBlobs key_blobs;
  AuthInput auth_input = {.user_input = user_input};
  EXPECT_EQ(CryptoError::CE_OTHER_CRYPTO,
            auth_block.Derive(auth_input, auth_state, &key_blobs)
                ->local_crypto_error());
}

TEST(DoubleWrappedCompatAuthBlockTest, DeriveTest) {
  SerializedVaultKeyset serialized;
  serialized.set_flags(SerializedVaultKeyset::SCRYPT_WRAPPED |
                       SerializedVaultKeyset::TPM_WRAPPED);

  std::vector<uint8_t> wrapped_keyset = {
      0x73, 0x63, 0x72, 0x79, 0x70, 0x74, 0x00, 0x0E, 0x00, 0x00, 0x00, 0x08,
      0x00, 0x00, 0x00, 0x01, 0x4D, 0xEE, 0xFC, 0x79, 0x0D, 0x79, 0x08, 0x79,
      0xD5, 0xF6, 0x07, 0x65, 0xDF, 0x76, 0x5A, 0xAE, 0xD1, 0xBD, 0x1D, 0xCF,
      0x29, 0xF6, 0xFF, 0x5C, 0x31, 0x30, 0x23, 0xD1, 0x22, 0x17, 0xDF, 0x74,
      0x26, 0xD5, 0x11, 0x88, 0x8D, 0x40, 0xA6, 0x9C, 0xB9, 0x72, 0xCE, 0x37,
      0x71, 0xB7, 0x39, 0x0E, 0x3E, 0x34, 0x0F, 0x73, 0x29, 0xF4, 0x0F, 0x89,
      0x15, 0xF7, 0x6E, 0xA1, 0x5A, 0x29, 0x78, 0x21, 0xB7, 0xC0, 0x76, 0x50,
      0x14, 0x5C, 0xAD, 0x77, 0x53, 0xC9, 0xD0, 0xFE, 0xD1, 0xB9, 0x81, 0x32,
      0x75, 0x0E, 0x1E, 0x45, 0x34, 0xBD, 0x0B, 0xF7, 0xFA, 0xED, 0x9A, 0xD7,
      0x6B, 0xE4, 0x2F, 0xC0, 0x2F, 0x58, 0xBE, 0x3A, 0x26, 0xD1, 0x82, 0x41,
      0x09, 0x82, 0x7F, 0x17, 0xA8, 0x5C, 0x66, 0x0E, 0x24, 0x8B, 0x7B, 0xF5,
      0xEB, 0x0C, 0x6D, 0xAE, 0x19, 0x5C, 0x7D, 0xC4, 0x0D, 0x8D, 0xB2, 0x18,
      0x13, 0xD4, 0xC0, 0x32, 0x34, 0x15, 0xAE, 0x1D, 0xA1, 0x44, 0x2E, 0x80,
      0xD8, 0x00, 0x8A, 0xB9, 0xDD, 0xA4, 0xC0, 0x33, 0xAE, 0x26, 0xD3, 0xE6,
      0x53, 0xD6, 0x31, 0x5C, 0x4C, 0x10, 0xBB, 0xA9, 0xD5, 0x53, 0xD7, 0xAD,
      0xCD, 0x97, 0x20, 0x83, 0xFC, 0x18, 0x4B, 0x7F, 0xC1, 0xBD, 0x85, 0x43,
      0x12, 0x85, 0x4F, 0x6F, 0xAA, 0xDB, 0x58, 0xA0, 0x0F, 0x2C, 0xAB, 0xEA,
      0x74, 0x8E, 0x2C, 0x28, 0x01, 0x88, 0x48, 0xA5, 0x0A, 0xFC, 0x2F, 0xB4,
      0x59, 0x4B, 0xF6, 0xD9, 0xE5, 0x47, 0x94, 0x42, 0xA5, 0x61, 0x06, 0x8C,
      0x5A, 0x9C, 0xD3, 0xA6, 0x30, 0x2C, 0x13, 0xCA, 0xF1, 0xFF, 0xFE, 0x5C,
      0xE8, 0x21, 0x25, 0x9A, 0xE0, 0x50, 0xC3, 0x2F, 0x14, 0x71, 0x38, 0xD0,
      0xE7, 0x79, 0x5D, 0xF0, 0x71, 0x80, 0xF0, 0x3D, 0x05, 0xB6, 0xF7, 0x67,
      0x3F, 0x22, 0x21, 0x7A, 0xED, 0x48, 0xC4, 0x2D, 0xEA, 0x2E, 0xAE, 0xE9,
      0xA8, 0xFF, 0xA0, 0xB6, 0xB4, 0x0A, 0x94, 0x34, 0x40, 0xD1, 0x6C, 0x6C,
      0xC7, 0x90, 0x9C, 0xF7, 0xED, 0x0B, 0xED, 0x90, 0xB1, 0x4D, 0x6D, 0xB4,
      0x3D, 0x04, 0x7E, 0x7B, 0x16, 0x59, 0xFF, 0xFE};

  std::vector<uint8_t> wrapped_chaps_key = {
      0x73, 0x63, 0x72, 0x79, 0x70, 0x74, 0x00, 0x0E, 0x00, 0x00, 0x00, 0x08,
      0x00, 0x00, 0x00, 0x01, 0xC9, 0x80, 0xA1, 0x30, 0x82, 0x40, 0xE6, 0xCF,
      0xC8, 0x59, 0xE9, 0xB6, 0xB0, 0xE8, 0xBF, 0x95, 0x82, 0x79, 0x71, 0xF9,
      0x86, 0x8A, 0xCA, 0x53, 0x23, 0xCF, 0x31, 0xFE, 0x4B, 0xD2, 0xA5, 0x26,
      0xA4, 0x46, 0x3D, 0x35, 0xEF, 0x69, 0x02, 0xC4, 0xBF, 0x72, 0xDC, 0xF8,
      0x90, 0x77, 0xFB, 0x59, 0x0D, 0x41, 0xCB, 0x5B, 0x58, 0xC6, 0x08, 0x0F,
      0x19, 0x4E, 0xC8, 0x4A, 0x57, 0xE7, 0x63, 0x43, 0x39, 0x79, 0xD7, 0x6E,
      0x0D, 0xD0, 0xE4, 0x4F, 0xFA, 0x55, 0x32, 0xE1, 0x6B, 0xE4, 0xFF, 0x12,
      0xB1, 0xA3, 0x75, 0x9C, 0x44, 0x3A, 0x16, 0x68, 0x5C, 0x11, 0xD0, 0xA5,
      0x4C, 0x65, 0xB0, 0xBF, 0x04, 0x41, 0x94, 0xFE, 0xC5, 0xDD, 0x5C, 0x78,
      0x5B, 0x14, 0xA1, 0x3F, 0x0B, 0x17, 0x9C, 0x75, 0xA5, 0x9E, 0x36, 0x14,
      0x5B, 0xC4, 0xAC, 0x77, 0x28, 0xDE, 0xEB, 0xB4, 0x51, 0x5F, 0x33, 0x36};

  std::vector<uint8_t> wrapped_reset_seed = {
      0x73, 0x63, 0x72, 0x79, 0x70, 0x74, 0x00, 0x0E, 0x00, 0x00, 0x00, 0x08,
      0x00, 0x00, 0x00, 0x01, 0x7F, 0x40, 0x30, 0x51, 0x2F, 0x15, 0x62, 0x15,
      0xB1, 0x2E, 0x58, 0x27, 0x52, 0xE4, 0xFF, 0xC5, 0x3C, 0x1E, 0x19, 0x05,
      0x84, 0xD8, 0xE8, 0xD4, 0xFD, 0x8C, 0x33, 0xE8, 0x06, 0x1A, 0x38, 0x28,
      0x2D, 0xD7, 0x01, 0xD2, 0xB3, 0xE1, 0x95, 0xC3, 0x49, 0x63, 0x39, 0xA2,
      0xB2, 0xE3, 0xDA, 0xE2, 0x76, 0x40, 0x40, 0x11, 0xD1, 0x98, 0xD2, 0x03,
      0xFB, 0x60, 0xD0, 0xA1, 0xA5, 0xB5, 0x51, 0xAA, 0xEF, 0x6C, 0xB3, 0xAB,
      0x23, 0x65, 0xCA, 0x44, 0x84, 0x7A, 0x71, 0xCA, 0x0C, 0x36, 0x33, 0x7F,
      0x53, 0x06, 0x0E, 0x03, 0xBB, 0xC1, 0x9A, 0x9D, 0x40, 0x1C, 0x2F, 0x46,
      0xB7, 0x84, 0x00, 0x59, 0x5B, 0xD6, 0x53, 0xE4, 0x51, 0x82, 0xC2, 0x3D,
      0xF4, 0x46, 0xD2, 0xDD, 0xE5, 0x7A, 0x0A, 0xEB, 0xC8, 0x45, 0x7C, 0x37,
      0x01, 0xD5, 0x37, 0x4E, 0xE3, 0xC7, 0xBC, 0xC6, 0x5E, 0x25, 0xFE, 0xE2,
      0x05, 0x14, 0x60, 0x33, 0xB8, 0x1A, 0xF1, 0x17, 0xE1, 0x0C, 0x25, 0x00,
      0xA5, 0x0A, 0xD5, 0x03};

  serialized.set_wrapped_keyset(wrapped_keyset.data(), wrapped_keyset.size());
  serialized.set_wrapped_chaps_key(wrapped_chaps_key.data(),
                                   wrapped_chaps_key.size());
  serialized.set_wrapped_reset_seed(wrapped_reset_seed.data(),
                                    wrapped_reset_seed.size());

  brillo::SecureBlob tpm_key(20, 'C');
  serialized.set_tpm_key(tpm_key.data(), tpm_key.size());

  brillo::SecureBlob key = {0x31, 0x35, 0x64, 0x64, 0x38, 0x38, 0x66, 0x36,
                            0x35, 0x31, 0x30, 0x65, 0x30, 0x64, 0x35, 0x64,
                            0x35, 0x35, 0x36, 0x35, 0x35, 0x35, 0x38, 0x36,
                            0x31, 0x32, 0x62, 0x37, 0x39, 0x36, 0x30, 0x65};

  KeyBlobs key_out_data;
  AuthInput auth_input;
  auth_input.user_input = key;
  auth_input.locked_to_single_user = false;

  VaultKeyset vk;
  vk.InitializeFromSerialized(serialized);
  AuthBlockState auth_state;
  EXPECT_TRUE(GetAuthBlockState(vk, auth_state));

  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  DoubleWrappedCompatAuthBlock auth_block(&hwsec, &cryptohome_keys_manager);

  EXPECT_TRUE(auth_block.Derive(auth_input, auth_state, &key_out_data).ok());
}

TEST(LibScryptCompatAuthBlockTest, CreateTest) {
  AuthInput auth_input;
  auth_input.user_input = brillo::SecureBlob("foo");

  KeyBlobs blobs;

  LibScryptCompatAuthBlock auth_block;
  AuthBlockState auth_state;
  EXPECT_TRUE(auth_block.Create(auth_input, &auth_state, &blobs).ok());

  // Because the salt is generated randomly inside the auth block, this test
  // cannot check the exact values returned. The salt() could be passed through
  // in some test specific harness, but the underlying scrypt code is tested in
  // so many other places, it's unnecessary.
  EXPECT_FALSE(blobs.scrypt_key->derived_key().empty());
  EXPECT_FALSE(blobs.scrypt_key->ConsumeSalt().empty());

  EXPECT_FALSE(blobs.chaps_scrypt_key->derived_key().empty());
  EXPECT_FALSE(blobs.chaps_scrypt_key->ConsumeSalt().empty());

  EXPECT_FALSE(blobs.scrypt_wrapped_reset_seed_key->derived_key().empty());
  EXPECT_FALSE(blobs.scrypt_wrapped_reset_seed_key->ConsumeSalt().empty());
}

TEST(LibScryptCompatAuthBlockTest, DeriveTest) {
  SerializedVaultKeyset serialized;
  serialized.set_flags(SerializedVaultKeyset::SCRYPT_WRAPPED);

  std::vector<uint8_t> wrapped_keyset = {
      0x73, 0x63, 0x72, 0x79, 0x70, 0x74, 0x00, 0x0E, 0x00, 0x00, 0x00, 0x08,
      0x00, 0x00, 0x00, 0x01, 0x4D, 0xEE, 0xFC, 0x79, 0x0D, 0x79, 0x08, 0x79,
      0xD5, 0xF6, 0x07, 0x65, 0xDF, 0x76, 0x5A, 0xAE, 0xD1, 0xBD, 0x1D, 0xCF,
      0x29, 0xF6, 0xFF, 0x5C, 0x31, 0x30, 0x23, 0xD1, 0x22, 0x17, 0xDF, 0x74,
      0x26, 0xD5, 0x11, 0x88, 0x8D, 0x40, 0xA6, 0x9C, 0xB9, 0x72, 0xCE, 0x37,
      0x71, 0xB7, 0x39, 0x0E, 0x3E, 0x34, 0x0F, 0x73, 0x29, 0xF4, 0x0F, 0x89,
      0x15, 0xF7, 0x6E, 0xA1, 0x5A, 0x29, 0x78, 0x21, 0xB7, 0xC0, 0x76, 0x50,
      0x14, 0x5C, 0xAD, 0x77, 0x53, 0xC9, 0xD0, 0xFE, 0xD1, 0xB9, 0x81, 0x32,
      0x75, 0x0E, 0x1E, 0x45, 0x34, 0xBD, 0x0B, 0xF7, 0xFA, 0xED, 0x9A, 0xD7,
      0x6B, 0xE4, 0x2F, 0xC0, 0x2F, 0x58, 0xBE, 0x3A, 0x26, 0xD1, 0x82, 0x41,
      0x09, 0x82, 0x7F, 0x17, 0xA8, 0x5C, 0x66, 0x0E, 0x24, 0x8B, 0x7B, 0xF5,
      0xEB, 0x0C, 0x6D, 0xAE, 0x19, 0x5C, 0x7D, 0xC4, 0x0D, 0x8D, 0xB2, 0x18,
      0x13, 0xD4, 0xC0, 0x32, 0x34, 0x15, 0xAE, 0x1D, 0xA1, 0x44, 0x2E, 0x80,
      0xD8, 0x00, 0x8A, 0xB9, 0xDD, 0xA4, 0xC0, 0x33, 0xAE, 0x26, 0xD3, 0xE6,
      0x53, 0xD6, 0x31, 0x5C, 0x4C, 0x10, 0xBB, 0xA9, 0xD5, 0x53, 0xD7, 0xAD,
      0xCD, 0x97, 0x20, 0x83, 0xFC, 0x18, 0x4B, 0x7F, 0xC1, 0xBD, 0x85, 0x43,
      0x12, 0x85, 0x4F, 0x6F, 0xAA, 0xDB, 0x58, 0xA0, 0x0F, 0x2C, 0xAB, 0xEA,
      0x74, 0x8E, 0x2C, 0x28, 0x01, 0x88, 0x48, 0xA5, 0x0A, 0xFC, 0x2F, 0xB4,
      0x59, 0x4B, 0xF6, 0xD9, 0xE5, 0x47, 0x94, 0x42, 0xA5, 0x61, 0x06, 0x8C,
      0x5A, 0x9C, 0xD3, 0xA6, 0x30, 0x2C, 0x13, 0xCA, 0xF1, 0xFF, 0xFE, 0x5C,
      0xE8, 0x21, 0x25, 0x9A, 0xE0, 0x50, 0xC3, 0x2F, 0x14, 0x71, 0x38, 0xD0,
      0xE7, 0x79, 0x5D, 0xF0, 0x71, 0x80, 0xF0, 0x3D, 0x05, 0xB6, 0xF7, 0x67,
      0x3F, 0x22, 0x21, 0x7A, 0xED, 0x48, 0xC4, 0x2D, 0xEA, 0x2E, 0xAE, 0xE9,
      0xA8, 0xFF, 0xA0, 0xB6, 0xB4, 0x0A, 0x94, 0x34, 0x40, 0xD1, 0x6C, 0x6C,
      0xC7, 0x90, 0x9C, 0xF7, 0xED, 0x0B, 0xED, 0x90, 0xB1, 0x4D, 0x6D, 0xB4,
      0x3D, 0x04, 0x7E, 0x7B, 0x16, 0x59, 0xFF, 0xFE};

  std::vector<uint8_t> wrapped_chaps_key = {
      0x73, 0x63, 0x72, 0x79, 0x70, 0x74, 0x00, 0x0E, 0x00, 0x00, 0x00, 0x08,
      0x00, 0x00, 0x00, 0x01, 0xC9, 0x80, 0xA1, 0x30, 0x82, 0x40, 0xE6, 0xCF,
      0xC8, 0x59, 0xE9, 0xB6, 0xB0, 0xE8, 0xBF, 0x95, 0x82, 0x79, 0x71, 0xF9,
      0x86, 0x8A, 0xCA, 0x53, 0x23, 0xCF, 0x31, 0xFE, 0x4B, 0xD2, 0xA5, 0x26,
      0xA4, 0x46, 0x3D, 0x35, 0xEF, 0x69, 0x02, 0xC4, 0xBF, 0x72, 0xDC, 0xF8,
      0x90, 0x77, 0xFB, 0x59, 0x0D, 0x41, 0xCB, 0x5B, 0x58, 0xC6, 0x08, 0x0F,
      0x19, 0x4E, 0xC8, 0x4A, 0x57, 0xE7, 0x63, 0x43, 0x39, 0x79, 0xD7, 0x6E,
      0x0D, 0xD0, 0xE4, 0x4F, 0xFA, 0x55, 0x32, 0xE1, 0x6B, 0xE4, 0xFF, 0x12,
      0xB1, 0xA3, 0x75, 0x9C, 0x44, 0x3A, 0x16, 0x68, 0x5C, 0x11, 0xD0, 0xA5,
      0x4C, 0x65, 0xB0, 0xBF, 0x04, 0x41, 0x94, 0xFE, 0xC5, 0xDD, 0x5C, 0x78,
      0x5B, 0x14, 0xA1, 0x3F, 0x0B, 0x17, 0x9C, 0x75, 0xA5, 0x9E, 0x36, 0x14,
      0x5B, 0xC4, 0xAC, 0x77, 0x28, 0xDE, 0xEB, 0xB4, 0x51, 0x5F, 0x33, 0x36};

  std::vector<uint8_t> wrapped_reset_seed = {
      0x73, 0x63, 0x72, 0x79, 0x70, 0x74, 0x00, 0x0E, 0x00, 0x00, 0x00, 0x08,
      0x00, 0x00, 0x00, 0x01, 0x7F, 0x40, 0x30, 0x51, 0x2F, 0x15, 0x62, 0x15,
      0xB1, 0x2E, 0x58, 0x27, 0x52, 0xE4, 0xFF, 0xC5, 0x3C, 0x1E, 0x19, 0x05,
      0x84, 0xD8, 0xE8, 0xD4, 0xFD, 0x8C, 0x33, 0xE8, 0x06, 0x1A, 0x38, 0x28,
      0x2D, 0xD7, 0x01, 0xD2, 0xB3, 0xE1, 0x95, 0xC3, 0x49, 0x63, 0x39, 0xA2,
      0xB2, 0xE3, 0xDA, 0xE2, 0x76, 0x40, 0x40, 0x11, 0xD1, 0x98, 0xD2, 0x03,
      0xFB, 0x60, 0xD0, 0xA1, 0xA5, 0xB5, 0x51, 0xAA, 0xEF, 0x6C, 0xB3, 0xAB,
      0x23, 0x65, 0xCA, 0x44, 0x84, 0x7A, 0x71, 0xCA, 0x0C, 0x36, 0x33, 0x7F,
      0x53, 0x06, 0x0E, 0x03, 0xBB, 0xC1, 0x9A, 0x9D, 0x40, 0x1C, 0x2F, 0x46,
      0xB7, 0x84, 0x00, 0x59, 0x5B, 0xD6, 0x53, 0xE4, 0x51, 0x82, 0xC2, 0x3D,
      0xF4, 0x46, 0xD2, 0xDD, 0xE5, 0x7A, 0x0A, 0xEB, 0xC8, 0x45, 0x7C, 0x37,
      0x01, 0xD5, 0x37, 0x4E, 0xE3, 0xC7, 0xBC, 0xC6, 0x5E, 0x25, 0xFE, 0xE2,
      0x05, 0x14, 0x60, 0x33, 0xB8, 0x1A, 0xF1, 0x17, 0xE1, 0x0C, 0x25, 0x00,
      0xA5, 0x0A, 0xD5, 0x03};

  serialized.set_wrapped_keyset(wrapped_keyset.data(), wrapped_keyset.size());
  serialized.set_wrapped_chaps_key(wrapped_chaps_key.data(),
                                   wrapped_chaps_key.size());
  serialized.set_wrapped_reset_seed(wrapped_reset_seed.data(),
                                    wrapped_reset_seed.size());

  brillo::SecureBlob key = {0x31, 0x35, 0x64, 0x64, 0x38, 0x38, 0x66, 0x36,
                            0x35, 0x31, 0x30, 0x65, 0x30, 0x64, 0x35, 0x64,
                            0x35, 0x35, 0x36, 0x35, 0x35, 0x35, 0x38, 0x36,
                            0x31, 0x32, 0x62, 0x37, 0x39, 0x36, 0x30, 0x65};

  KeyBlobs key_out_data;
  AuthInput auth_input;
  auth_input.user_input = key;

  VaultKeyset vk;
  vk.InitializeFromSerialized(serialized);
  AuthBlockState auth_state;
  EXPECT_TRUE(GetAuthBlockState(vk, auth_state));

  LibScryptCompatAuthBlock auth_block;
  EXPECT_TRUE(auth_block.Derive(auth_input, auth_state, &key_out_data).ok());

  brillo::SecureBlob derived_key = {
      0x58, 0x2A, 0x41, 0x1F, 0xC0, 0x27, 0x2D, 0xC7, 0xF8, 0xEC, 0xA3,
      0x4E, 0xC0, 0x3F, 0x6C, 0x56, 0x6D, 0x88, 0x69, 0x3F, 0x50, 0x20,
      0x37, 0xE3, 0x77, 0x5F, 0xDD, 0xC3, 0x61, 0x2D, 0x27, 0xAD, 0xD3,
      0x55, 0x4D, 0x66, 0xE5, 0x83, 0xD2, 0x5E, 0x02, 0x0C, 0x22, 0x59,
      0x6C, 0x39, 0x35, 0x86, 0xEC, 0x46, 0xB0, 0x85, 0x89, 0xE3, 0x4C,
      0xB9, 0xE2, 0x0C, 0xA1, 0x27, 0x60, 0x85, 0x5A, 0x37};

  brillo::SecureBlob derived_chaps_key = {
      0x16, 0x53, 0xEE, 0x4D, 0x76, 0x47, 0x68, 0x09, 0xB3, 0x39, 0x1D,
      0xD3, 0x6F, 0xA2, 0x8F, 0x8A, 0x3E, 0xB3, 0x64, 0xDD, 0x4D, 0xC4,
      0x64, 0x6F, 0xE1, 0xB8, 0x82, 0x28, 0x68, 0x72, 0x68, 0x84, 0x93,
      0xE2, 0xDB, 0x2F, 0x27, 0x91, 0x08, 0x2C, 0xA0, 0xD9, 0xA1, 0x6E,
      0x6F, 0x0E, 0x13, 0x66, 0x1D, 0x94, 0x12, 0x6F, 0xF4, 0x98, 0x7B,
      0x44, 0x62, 0x57, 0x47, 0x33, 0x46, 0xD2, 0x30, 0x42};

  brillo::SecureBlob derived_reset_seed_key = {
      0xFA, 0x93, 0x57, 0xCE, 0x21, 0xBB, 0x82, 0x4D, 0x3A, 0x3B, 0x26,
      0x88, 0x8C, 0x7E, 0x61, 0x52, 0x52, 0xF0, 0x12, 0x25, 0xA3, 0x59,
      0xCA, 0x71, 0xD2, 0x0C, 0x52, 0x8A, 0x5B, 0x7A, 0x7D, 0xBF, 0x8E,
      0xC7, 0x4D, 0x1D, 0xB5, 0xF9, 0x01, 0xA6, 0xE5, 0x5D, 0x47, 0x2E,
      0xFD, 0x7C, 0x78, 0x1D, 0x9B, 0xAD, 0xE6, 0x71, 0x35, 0x2B, 0x32,
      0x1E, 0x59, 0x19, 0x47, 0x88, 0x92, 0x50, 0x28, 0x09};

  EXPECT_EQ(derived_key, key_out_data.scrypt_key->derived_key());
  EXPECT_EQ(derived_chaps_key, key_out_data.chaps_scrypt_key->derived_key());
  EXPECT_EQ(derived_reset_seed_key,
            key_out_data.scrypt_wrapped_reset_seed_key->derived_key());
}

class CryptohomeRecoveryAuthBlockTest : public testing::Test {
 public:
  void SetUp() override {
    ASSERT_TRUE(FakeRecoveryMediatorCrypto::GetFakeMediatorPublicKey(
        &mediator_pub_key_));
    ASSERT_TRUE(
        FakeRecoveryMediatorCrypto::GetFakeEpochPublicKey(&epoch_pub_key_));
    ASSERT_TRUE(
        FakeRecoveryMediatorCrypto::GetFakeEpochResponse(&epoch_response_));
  }

  void PerformRecovery(
      cryptorecovery::RecoveryCryptoTpmBackend* tpm_backend,
      const CryptohomeRecoveryAuthBlockState& cryptohome_recovery_state,
      cryptorecovery::CryptoRecoveryRpcResponse* response_proto,
      brillo::SecureBlob* ephemeral_pub_key) {
    EXPECT_FALSE(cryptohome_recovery_state.hsm_payload.empty());
    EXPECT_FALSE(cryptohome_recovery_state.encrypted_destination_share.empty());
    EXPECT_FALSE(cryptohome_recovery_state.encrypted_channel_priv_key.empty());
    EXPECT_FALSE(cryptohome_recovery_state.channel_pub_key.empty());

    // Deserialize HSM payload stored on disk.
    cryptorecovery::HsmPayload hsm_payload;
    EXPECT_TRUE(DeserializeHsmPayloadFromCbor(
        cryptohome_recovery_state.hsm_payload, &hsm_payload));

    // Start recovery process.
    std::unique_ptr<cryptorecovery::RecoveryCryptoImpl> recovery =
        cryptorecovery::RecoveryCryptoImpl::Create(tpm_backend, &platform_);
    ASSERT_TRUE(recovery);
    brillo::SecureBlob rsa_priv_key;

    cryptorecovery::RequestMetadata request_metadata;
    cryptorecovery::GenerateRecoveryRequestRequest
        generate_recovery_request_input_param(
            {.hsm_payload = hsm_payload,
             .request_meta_data = request_metadata,
             .epoch_response = epoch_response_,
             .encrypted_rsa_priv_key = rsa_priv_key,
             .encrypted_channel_priv_key =
                 cryptohome_recovery_state.encrypted_channel_priv_key,
             .channel_pub_key = cryptohome_recovery_state.channel_pub_key,
             .obfuscated_username = kObfuscatedUsername});
    cryptorecovery::CryptoRecoveryRpcRequest recovery_request;
    ASSERT_TRUE(recovery->GenerateRecoveryRequest(
        generate_recovery_request_input_param, &recovery_request,
        ephemeral_pub_key));

    // Simulate mediation (it will be done by Recovery Mediator service).
    std::unique_ptr<FakeRecoveryMediatorCrypto> mediator =
        FakeRecoveryMediatorCrypto::Create();
    ASSERT_TRUE(mediator);
    brillo::SecureBlob mediator_priv_key;
    ASSERT_TRUE(FakeRecoveryMediatorCrypto::GetFakeMediatorPrivateKey(
        &mediator_priv_key));
    brillo::SecureBlob epoch_priv_key;
    ASSERT_TRUE(
        FakeRecoveryMediatorCrypto::GetFakeEpochPrivateKey(&epoch_priv_key));

    ASSERT_TRUE(mediator->MediateRequestPayload(
        epoch_pub_key_, epoch_priv_key, mediator_priv_key, recovery_request,
        response_proto));
  }

 protected:
  brillo::SecureBlob mediator_pub_key_;
  brillo::SecureBlob epoch_pub_key_;
  cryptorecovery::CryptoRecoveryEpochResponse epoch_response_;
  FakePlatform platform_;
};

TEST_F(CryptohomeRecoveryAuthBlockTest, SuccessTest) {
  AuthInput auth_input;
  CryptohomeRecoveryAuthInput cryptohome_recovery_auth_input;
  cryptohome_recovery_auth_input.mediator_pub_key = mediator_pub_key_;
  auth_input.cryptohome_recovery_auth_input = cryptohome_recovery_auth_input;
  auth_input.obfuscated_username = kObfuscatedUsername;

  // IsPinWeaverEnabled()) returns `false` -> revocation is not supported.
  cryptorecovery::RecoveryCryptoFakeTpmBackendImpl
      recovery_crypto_fake_tpm_backend;

  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  SetupMockHwsec(hwsec);
  EXPECT_CALL(hwsec, IsPinWeaverEnabled()).WillRepeatedly(ReturnValue(false));

  KeyBlobs created_key_blobs;
  CryptohomeRecoveryAuthBlock auth_block(
      &hwsec, &recovery_crypto_fake_tpm_backend, nullptr, &platform_);
  AuthBlockState auth_state;
  EXPECT_TRUE(
      auth_block.Create(auth_input, &auth_state, &created_key_blobs).ok());
  ASSERT_TRUE(created_key_blobs.vkk_key.has_value());
  EXPECT_FALSE(auth_state.revocation_state.has_value());

  ASSERT_TRUE(std::holds_alternative<CryptohomeRecoveryAuthBlockState>(
      auth_state.state));
  const CryptohomeRecoveryAuthBlockState& cryptohome_recovery_state =
      std::get<CryptohomeRecoveryAuthBlockState>(auth_state.state);

  brillo::SecureBlob ephemeral_pub_key;
  cryptorecovery::CryptoRecoveryRpcResponse response_proto;
  PerformRecovery(&recovery_crypto_fake_tpm_backend, cryptohome_recovery_state,
                  &response_proto, &ephemeral_pub_key);

  CryptohomeRecoveryAuthInput derive_cryptohome_recovery_auth_input;
  // Save data required for key derivation in auth_input.
  std::string serialized_response_proto, serialized_epoch_response;
  EXPECT_TRUE(response_proto.SerializeToString(&serialized_response_proto));
  EXPECT_TRUE(epoch_response_.SerializeToString(&serialized_epoch_response));
  derive_cryptohome_recovery_auth_input.recovery_response =
      brillo::SecureBlob(serialized_response_proto);
  derive_cryptohome_recovery_auth_input.epoch_response =
      brillo::SecureBlob(serialized_epoch_response);
  derive_cryptohome_recovery_auth_input.ephemeral_pub_key = ephemeral_pub_key;
  auth_input.cryptohome_recovery_auth_input =
      derive_cryptohome_recovery_auth_input;

  KeyBlobs derived_key_blobs;
  EXPECT_TRUE(
      auth_block.Derive(auth_input, auth_state, &derived_key_blobs).ok());
  ASSERT_TRUE(derived_key_blobs.vkk_key.has_value());

  // KeyBlobs generated by `Create` should be the same as KeyBlobs generated by
  // `Derive`.
  EXPECT_EQ(created_key_blobs.vkk_key, derived_key_blobs.vkk_key);
  EXPECT_EQ(created_key_blobs.vkk_iv, derived_key_blobs.vkk_iv);
  EXPECT_EQ(created_key_blobs.chaps_iv, derived_key_blobs.chaps_iv);
}

TEST_F(CryptohomeRecoveryAuthBlockTest, SuccessTestWithRevocation) {
  AuthInput auth_input;
  CryptohomeRecoveryAuthInput cryptohome_recovery_auth_input;
  cryptohome_recovery_auth_input.mediator_pub_key = mediator_pub_key_;
  auth_input.cryptohome_recovery_auth_input = cryptohome_recovery_auth_input;
  auth_input.obfuscated_username = kObfuscatedUsername;

  // IsPinWeaverEnabled() returns `true` -> revocation is supported.
  cryptorecovery::RecoveryCryptoFakeTpmBackendImpl
      recovery_crypto_fake_tpm_backend;
  NiceMock<MockLECredentialManager> le_cred_manager;
  brillo::SecureBlob le_secret, he_secret;
  uint64_t le_label = 1;
  EXPECT_CALL(le_cred_manager, InsertCredential(_, _, _, _, _, _))
      .WillOnce(DoAll(SaveArg<1>(&le_secret), SaveArg<2>(&he_secret),
                      SetArgPointee<5>(le_label),
                      ReturnError<CryptohomeLECredError>()));

  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  SetupMockHwsec(hwsec);
  EXPECT_CALL(hwsec, IsPinWeaverEnabled()).WillRepeatedly(ReturnValue(true));

  KeyBlobs created_key_blobs;
  CryptohomeRecoveryAuthBlock auth_block(
      &hwsec, &recovery_crypto_fake_tpm_backend, &le_cred_manager, &platform_);
  AuthBlockState auth_state;
  EXPECT_TRUE(
      auth_block.Create(auth_input, &auth_state, &created_key_blobs).ok());
  ASSERT_TRUE(created_key_blobs.vkk_key.has_value());

  // The revocation state should be created with the le_label returned by
  // InsertCredential().
  ASSERT_TRUE(auth_state.revocation_state.has_value());
  EXPECT_EQ(le_label, auth_state.revocation_state.value().le_label);
  EXPECT_FALSE(he_secret.empty());

  ASSERT_TRUE(std::holds_alternative<CryptohomeRecoveryAuthBlockState>(
      auth_state.state));
  const CryptohomeRecoveryAuthBlockState& cryptohome_recovery_state =
      std::get<CryptohomeRecoveryAuthBlockState>(auth_state.state);

  brillo::SecureBlob ephemeral_pub_key;
  cryptorecovery::CryptoRecoveryRpcResponse response_proto;
  PerformRecovery(&recovery_crypto_fake_tpm_backend, cryptohome_recovery_state,
                  &response_proto, &ephemeral_pub_key);

  CryptohomeRecoveryAuthInput derive_cryptohome_recovery_auth_input;
  // Save data required for key derivation in auth_input.
  std::string serialized_response_proto, serialized_epoch_response;
  EXPECT_TRUE(response_proto.SerializeToString(&serialized_response_proto));
  EXPECT_TRUE(epoch_response_.SerializeToString(&serialized_epoch_response));
  derive_cryptohome_recovery_auth_input.recovery_response =
      brillo::SecureBlob(serialized_response_proto);
  derive_cryptohome_recovery_auth_input.epoch_response =
      brillo::SecureBlob(serialized_epoch_response);
  derive_cryptohome_recovery_auth_input.ephemeral_pub_key = ephemeral_pub_key;
  auth_input.cryptohome_recovery_auth_input =
      derive_cryptohome_recovery_auth_input;

  brillo::SecureBlob le_secret_1;
  EXPECT_CALL(le_cred_manager, CheckCredential(le_label, _, _, _))
      .WillOnce(DoAll(SaveArg<1>(&le_secret_1), SetArgPointee<2>(he_secret),
                      ReturnError<CryptohomeLECredError>()));
  KeyBlobs derived_key_blobs;
  EXPECT_TRUE(
      auth_block.Derive(auth_input, auth_state, &derived_key_blobs).ok());
  ASSERT_TRUE(derived_key_blobs.vkk_key.has_value());

  // LE secret should be the same in InsertCredential and CheckCredential.
  EXPECT_EQ(le_secret, le_secret_1);

  // KeyBlobs generated by `Create` should be the same as KeyBlobs generated by
  // `Derive`.
  EXPECT_EQ(created_key_blobs.vkk_key, derived_key_blobs.vkk_key);
  EXPECT_EQ(created_key_blobs.vkk_iv, derived_key_blobs.vkk_iv);
  EXPECT_EQ(created_key_blobs.chaps_iv, derived_key_blobs.chaps_iv);
}

TEST_F(CryptohomeRecoveryAuthBlockTest, MissingObfuscatedUsername) {
  AuthInput auth_input;
  CryptohomeRecoveryAuthInput cryptohome_recovery_auth_input;
  cryptohome_recovery_auth_input.mediator_pub_key = mediator_pub_key_;
  auth_input.cryptohome_recovery_auth_input = cryptohome_recovery_auth_input;

  // Tpm::GetLECredentialBackend() returns `nullptr` -> revocation is not
  // supported.
  cryptorecovery::RecoveryCryptoFakeTpmBackendImpl
      recovery_crypto_fake_tpm_backend;

  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  SetupMockHwsec(hwsec);

  KeyBlobs created_key_blobs;
  CryptohomeRecoveryAuthBlock auth_block(
      &hwsec, &recovery_crypto_fake_tpm_backend,
      /*LECredentialManager*=*/nullptr, &platform_);
  AuthBlockState auth_state;
  EXPECT_FALSE(
      auth_block.Create(auth_input, &auth_state, &created_key_blobs).ok());
  EXPECT_FALSE(created_key_blobs.vkk_key.has_value());
  EXPECT_FALSE(created_key_blobs.vkk_iv.has_value());
  EXPECT_FALSE(created_key_blobs.chaps_iv.has_value());
  EXPECT_FALSE(auth_state.revocation_state.has_value());
}

// Test the TpmEccAuthBlock::Create works correctly.
TEST(TpmEccAuthBlockTest, CreateTest) {
  // Set up inputs to the test.
  brillo::SecureBlob vault_key(20, 'C');

  // Set up the mock expectations.
  brillo::SecureBlob scrypt_derived_key;
  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  SetupMockHwsec(hwsec);
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  brillo::SecureBlob auth_value(32, 'a');
  EXPECT_CALL(hwsec, GetManufacturer()).WillOnce(ReturnValue(0x43524f53));
  EXPECT_CALL(hwsec, GetAuthValue(_, _))
      .Times(Exactly(5))
      .WillOnce(DoAll(SaveArg<1>(&scrypt_derived_key), ReturnValue(auth_value)))
      .WillRepeatedly(ReturnValue(auth_value));
  EXPECT_CALL(hwsec, SealWithCurrentUser(_, auth_value, _))
      .WillOnce(ReturnValue(brillo::Blob()))
      .WillOnce(ReturnValue(brillo::Blob()));

  AuthInput user_input = {vault_key,
                          /*locked_to_single_user=*/std::nullopt,
                          kObfuscatedUsername,
                          /*reset_secret=*/std::nullopt};
  KeyBlobs vkk_data;

  TpmEccAuthBlock auth_block(&hwsec, &cryptohome_keys_manager);
  AuthBlockState auth_state;
  EXPECT_TRUE(auth_block.Create(user_input, &auth_state, &vkk_data).ok());
  EXPECT_TRUE(std::holds_alternative<TpmEccAuthBlockState>(auth_state.state));

  EXPECT_NE(vkk_data.vkk_key, std::nullopt);
  EXPECT_NE(vkk_data.vkk_iv, std::nullopt);
  EXPECT_NE(vkk_data.chaps_iv, std::nullopt);

  auto& tpm_state = std::get<TpmEccAuthBlockState>(auth_state.state);

  EXPECT_TRUE(tpm_state.salt.has_value());
  const brillo::SecureBlob& salt = tpm_state.salt.value();
  brillo::SecureBlob scrypt_derived_key_result(kDefaultPassBlobSize);
  EXPECT_TRUE(
      DeriveSecretsScrypt(vault_key, salt, {&scrypt_derived_key_result}));
  EXPECT_EQ(scrypt_derived_key, scrypt_derived_key_result);
}

// Test the retry function of TpmEccAuthBlock::Create works correctly.
TEST(TpmEccAuthBlockTest, CreateRetryTest) {
  // Set up inputs to the test.
  brillo::SecureBlob vault_key(20, 'C');

  // Set up the mock expectations.
  brillo::SecureBlob scrypt_derived_key;
  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  SetupMockHwsec(hwsec);
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  brillo::SecureBlob auth_value(32, 'a');
  EXPECT_CALL(hwsec, GetManufacturer())
      .Times(Exactly(2))
      .WillRepeatedly(ReturnValue(0x43524f53));

  // Add some communication errors and retry errors that may come from TPM
  // daemon.
  EXPECT_CALL(hwsec, GetAuthValue(_, _))
      .Times(Exactly(6))
      .WillOnce(
          ReturnError<TPMError>("ECC scalar out of range",
                                TPMRetryAction::kEllipticCurveScalarOutOfRange))
      .WillOnce(DoAll(SaveArg<1>(&scrypt_derived_key), ReturnValue(auth_value)))
      .WillRepeatedly(ReturnValue(auth_value));

  // Add some communication errors that may come from TPM daemon.
  EXPECT_CALL(hwsec, SealWithCurrentUser(_, auth_value, _))
      .WillOnce(ReturnValue(brillo::Blob()))
      .WillOnce(ReturnValue(brillo::Blob()));

  AuthInput user_input = {vault_key,
                          /*locked_to_single_user=*/std::nullopt,
                          kObfuscatedUsername,
                          /*reset_secret=*/std::nullopt};
  KeyBlobs vkk_data;

  TpmEccAuthBlock auth_block(&hwsec, &cryptohome_keys_manager);
  AuthBlockState auth_state;
  EXPECT_TRUE(auth_block.Create(user_input, &auth_state, &vkk_data).ok());
  EXPECT_TRUE(std::holds_alternative<TpmEccAuthBlockState>(auth_state.state));

  EXPECT_NE(vkk_data.vkk_key, std::nullopt);
  EXPECT_NE(vkk_data.vkk_iv, std::nullopt);
  EXPECT_NE(vkk_data.chaps_iv, std::nullopt);

  auto& tpm_state = std::get<TpmEccAuthBlockState>(auth_state.state);

  EXPECT_TRUE(tpm_state.salt.has_value());
  const brillo::SecureBlob& salt = tpm_state.salt.value();
  brillo::SecureBlob scrypt_derived_key_result(kDefaultPassBlobSize);
  EXPECT_TRUE(
      DeriveSecretsScrypt(vault_key, salt, {&scrypt_derived_key_result}));
  EXPECT_EQ(scrypt_derived_key, scrypt_derived_key_result);
}

// Test the retry function of TpmEccAuthBlock::Create failed as expected.
TEST(TpmEccAuthBlockTest, CreateRetryFailTest) {
  // Set up inputs to the test.
  brillo::SecureBlob vault_key(20, 'C');

  // Set up the mock expectations.
  brillo::SecureBlob scrypt_derived_key;
  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  SetupMockHwsec(hwsec);
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  brillo::SecureBlob auth_value(32, 'a');
  EXPECT_CALL(hwsec, GetManufacturer()).WillRepeatedly(ReturnValue(0x43524f53));
  // The TpmEccAuthBlock shouldn't retry forever if the TPM always returning
  // error.
  EXPECT_CALL(hwsec, GetAuthValue(_, _))
      .WillRepeatedly(ReturnError<TPMError>("reboot", TPMRetryAction::kReboot));

  AuthInput user_input = {vault_key,
                          /*locked_to_single_user=*/std::nullopt,
                          kObfuscatedUsername,
                          /*reset_secret=*/std::nullopt};
  KeyBlobs vkk_data;
  TpmEccAuthBlock auth_block(&hwsec, &cryptohome_keys_manager);
  AuthBlockState auth_state;
  EXPECT_EQ(CryptoError::CE_TPM_REBOOT,
            auth_block.Create(user_input, &auth_state, &vkk_data)
                ->local_crypto_error());
}

// Test the Create operation fails when there's no user_input provided.
TEST(TpmEccAuthBlockTest, CreateFailNoUserInput) {
  // Prepare.
  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  TpmEccAuthBlock auth_block(&hwsec, &cryptohome_keys_manager);
  AuthInput auth_input = {.obfuscated_username = kObfuscatedUsername};

  // Test.
  AuthBlockState auth_state;
  KeyBlobs vkk_data;
  EXPECT_EQ(auth_block.Create(auth_input, &auth_state, &vkk_data)
                ->local_crypto_error(),
            CryptoError::CE_OTHER_CRYPTO);
}

// Test the Create operation fails when there's no obfuscated_username provided.
TEST(TpmEccAuthBlockTest, CreateFailNoObfuscated) {
  // Prepare.
  brillo::SecureBlob user_input(20, 'C');
  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  TpmBoundToPcrAuthBlock auth_block(&hwsec, &cryptohome_keys_manager);
  AuthInput auth_input = {.user_input = user_input};

  // Test.
  AuthBlockState auth_state;
  KeyBlobs vkk_data;
  EXPECT_EQ(auth_block.Create(auth_input, &auth_state, &vkk_data)
                ->local_crypto_error(),
            CryptoError::CE_OTHER_CRYPTO);
}

// Test SealToPcr in TpmEccAuthBlock::Create failed as expected.
TEST(TpmEccAuthBlockTest, CreateSealToPcrFailTest) {
  // Set up inputs to the test.
  brillo::SecureBlob vault_key(20, 'C');

  // Set up the mock expectations.
  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  SetupMockHwsec(hwsec);
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  brillo::SecureBlob auth_value(32, 'a');
  EXPECT_CALL(hwsec, GetManufacturer()).WillOnce(ReturnValue(0x49465800));
  EXPECT_CALL(hwsec, GetAuthValue(_, _))
      .Times(2)
      .WillRepeatedly(ReturnValue(auth_value));

  EXPECT_CALL(hwsec, SealWithCurrentUser(_, auth_value, _))
      .WillOnce(ReturnError<TPMError>("fake", TPMRetryAction::kNoRetry));

  AuthInput user_input = {vault_key,
                          /*locked_to_single_user=*/std::nullopt,
                          kObfuscatedUsername,
                          /*reset_secret=*/std::nullopt};
  KeyBlobs vkk_data;
  TpmEccAuthBlock auth_block(&hwsec, &cryptohome_keys_manager);
  AuthBlockState auth_state;
  EXPECT_EQ(CryptoError::CE_TPM_CRYPTO,
            auth_block.Create(user_input, &auth_state, &vkk_data)
                ->local_crypto_error());
}

// Test second SealToPcr in TpmEccAuthBlock::Create failed as expected.
TEST(TpmEccAuthBlockTest, CreateSecondSealToPcrFailTest) {
  // Set up inputs to the test.
  brillo::SecureBlob vault_key(20, 'C');

  // Set up the mock expectations.
  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  SetupMockHwsec(hwsec);
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  brillo::SecureBlob auth_value(32, 'a');
  EXPECT_CALL(hwsec, GetManufacturer()).WillOnce(ReturnValue(0x49465800));
  EXPECT_CALL(hwsec, GetAuthValue(_, _))
      .Times(2)
      .WillRepeatedly(ReturnValue(auth_value));

  EXPECT_CALL(hwsec, SealWithCurrentUser(_, auth_value, _))
      .WillOnce(ReturnValue(brillo::Blob()))
      .WillOnce(ReturnError<TPMError>("fake", TPMRetryAction::kNoRetry));

  AuthInput user_input = {vault_key,
                          /*locked_to_single_user=*/std::nullopt,
                          kObfuscatedUsername,
                          /*reset_secret=*/std::nullopt};
  KeyBlobs vkk_data;
  TpmEccAuthBlock auth_block(&hwsec, &cryptohome_keys_manager);
  AuthBlockState auth_state;
  EXPECT_EQ(CryptoError::CE_TPM_CRYPTO,
            auth_block.Create(user_input, &auth_state, &vkk_data)
                ->local_crypto_error());
}

// Test GetEccAuthValue in TpmEccAuthBlock::Create failed as expected.
TEST(TpmEccAuthBlockTest, CreateEccAuthValueFailTest) {
  // Set up inputs to the test.
  brillo::SecureBlob vault_key(20, 'C');

  // Set up the mock expectations.
  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  SetupMockHwsec(hwsec);
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  brillo::SecureBlob auth_value(32, 'a');

  EXPECT_CALL(hwsec, GetManufacturer())
      .WillOnce(ReturnError<TPMError>("fake", TPMRetryAction::kNoRetry));
  EXPECT_CALL(hwsec, GetAuthValue(_, _))
      .WillOnce(ReturnError<TPMError>("fake", TPMRetryAction::kNoRetry));

  AuthInput user_input = {vault_key,
                          /*locked_to_single_user=*/std::nullopt,
                          kObfuscatedUsername,
                          /*reset_secret=*/std::nullopt};
  KeyBlobs vkk_data;
  TpmEccAuthBlock auth_block(&hwsec, &cryptohome_keys_manager);
  AuthBlockState auth_state;
  EXPECT_EQ(CryptoError::CE_TPM_CRYPTO,
            auth_block.Create(user_input, &auth_state, &vkk_data)
                ->local_crypto_error());
}

// Test TpmEccAuthBlock::DeriveTest works correctly.
TEST(TpmEccAuthBlockTest, DeriveTest) {
  TpmEccAuthBlockState auth_block_state = GetDefaultEccAuthBlockState();

  brillo::Blob fake_hash(32, 'X');
  auth_block_state.tpm_public_key_hash =
      brillo::SecureBlob(fake_hash.begin(), fake_hash.end());

  // Set up the mock expectations.
  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  SetupMockHwsec(hwsec);
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  EXPECT_CALL(hwsec, GetPubkeyHash(_)).WillOnce(ReturnValue(fake_hash));
  EXPECT_CALL(hwsec, PreloadSealedData(_)).WillOnce(Invoke([&](auto&&) {
    return hwsec::ScopedKey(hwsec::Key{.token = 5566},
                            hwsec.GetFakeMiddlewareDerivative());
  }));
  EXPECT_CALL(hwsec, GetAuthValue(_, _))
      .Times(Exactly(5))
      .WillRepeatedly(ReturnValue(brillo::SecureBlob()));

  brillo::SecureBlob fake_hvkkm(32, 'F');
  EXPECT_CALL(hwsec, UnsealWithCurrentUser(_, _, _))
      .WillOnce(ReturnValue(fake_hvkkm));

  TpmEccAuthBlock auth_block(&hwsec, &cryptohome_keys_manager);

  KeyBlobs key_out_data;
  AuthInput auth_input;
  auth_input.user_input = brillo::SecureBlob(20, 'E');
  auth_input.locked_to_single_user = false;

  AuthBlockState auth_state{.state = std::move(auth_block_state)};

  EXPECT_TRUE(auth_block.Derive(auth_input, auth_state, &key_out_data).ok());

  // Assert that the returned key blobs isn't uninitialized.
  EXPECT_NE(key_out_data.vkk_iv, std::nullopt);
  EXPECT_NE(key_out_data.vkk_key, std::nullopt);
  EXPECT_EQ(key_out_data.vkk_iv.value(), key_out_data.chaps_iv.value());
}

// Test TpmEccAuthBlock::Derive failure when there's no auth_input provided.
TEST(TpmEccAuthBlockTest, DeriveFailNoAuthInput) {
  TpmEccAuthBlockState auth_block_state = GetDefaultEccAuthBlockState();
  AuthBlockState auth_state{.state = std::move(auth_block_state)};

  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  TpmEccAuthBlock auth_block(&hwsec, &cryptohome_keys_manager);

  KeyBlobs key_out_data;
  AuthInput auth_input;
  EXPECT_EQ(auth_block.Derive(auth_input, auth_state, &key_out_data)
                ->local_crypto_error(),
            CryptoError::CE_OTHER_CRYPTO);
}

// Test GetEccAuthValue in TpmEccAuthBlock::Derive failed as expected.
TEST(TpmEccAuthBlockTest, DeriveGetEccAuthFailTest) {
  TpmEccAuthBlockState auth_block_state = GetDefaultEccAuthBlockState();

  // Set up the mock expectations.
  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  SetupMockHwsec(hwsec);
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  EXPECT_CALL(hwsec, PreloadSealedData(_)).WillOnce(ReturnValue(std::nullopt));

  EXPECT_CALL(hwsec, GetAuthValue(_, _))
      .WillOnce(ReturnError<TPMError>("fake", TPMRetryAction::kNoRetry));

  TpmEccAuthBlock auth_block(&hwsec, &cryptohome_keys_manager);

  KeyBlobs key_out_data;
  AuthInput auth_input;
  auth_input.user_input = brillo::SecureBlob(20, 'E');
  auth_input.locked_to_single_user = false;

  AuthBlockState auth_state{.state = std::move(auth_block_state)};

  EXPECT_EQ(CryptoError::CE_TPM_CRYPTO,
            auth_block.Derive(auth_input, auth_state, &key_out_data)
                ->local_crypto_error());
}

// Test PreloadSealedData in TpmEccAuthBlock::Derive failed as expected.
TEST(TpmEccAuthBlockTest, DerivePreloadSealedDataFailTest) {
  TpmEccAuthBlockState auth_block_state = GetDefaultEccAuthBlockState();

  // Set up the mock expectations.
  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  SetupMockHwsec(hwsec);
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;

  EXPECT_CALL(hwsec, PreloadSealedData(_))
      .WillOnce(ReturnError<TPMError>("fake", TPMRetryAction::kNoRetry));

  TpmEccAuthBlock auth_block(&hwsec, &cryptohome_keys_manager);

  KeyBlobs key_out_data;
  AuthInput auth_input;
  auth_input.user_input = brillo::SecureBlob(20, 'E');
  auth_input.locked_to_single_user = false;

  AuthBlockState auth_state{.state = std::move(auth_block_state)};

  EXPECT_EQ(CryptoError::CE_TPM_CRYPTO,
            auth_block.Derive(auth_input, auth_state, &key_out_data)
                ->local_crypto_error());
}

// Test GetPublicKeyHash in TpmEccAuthBlock::Derive failed as expected.
TEST(TpmEccAuthBlockTest, DeriveGetPublicKeyHashFailTest) {
  TpmEccAuthBlockState auth_block_state = GetDefaultEccAuthBlockState();

  auth_block_state.tpm_public_key_hash = brillo::SecureBlob(32, 'X');

  // Set up the mock expectations.
  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  SetupMockHwsec(hwsec);
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  EXPECT_CALL(hwsec, GetPubkeyHash(_))
      .WillOnce(ReturnError<TPMError>("fake", TPMRetryAction::kNoRetry));

  TpmEccAuthBlock auth_block(&hwsec, &cryptohome_keys_manager);

  KeyBlobs key_out_data;
  AuthInput auth_input;
  auth_input.user_input = brillo::SecureBlob(20, 'E');
  auth_input.locked_to_single_user = false;

  AuthBlockState auth_state{.state = std::move(auth_block_state)};

  EXPECT_EQ(CryptoError::CE_TPM_CRYPTO,
            auth_block.Derive(auth_input, auth_state, &key_out_data)
                ->local_crypto_error());
}

// Test PublicKeyHashMismatch in TpmEccAuthBlock::Derive failed as expected.
TEST(TpmEccAuthBlockTest, DerivePublicKeyHashMismatchTest) {
  TpmEccAuthBlockState auth_block_state = GetDefaultEccAuthBlockState();

  auth_block_state.tpm_public_key_hash = brillo::SecureBlob(32, 'X');

  brillo::Blob fake_hash(32, 'Z');
  // Set up the mock expectations.
  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  SetupMockHwsec(hwsec);
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  EXPECT_CALL(hwsec, GetPubkeyHash(_)).WillOnce(ReturnValue(fake_hash));

  TpmEccAuthBlock auth_block(&hwsec, &cryptohome_keys_manager);

  KeyBlobs key_out_data;
  AuthInput auth_input;
  auth_input.user_input = brillo::SecureBlob(20, 'E');
  auth_input.locked_to_single_user = false;

  AuthBlockState auth_state{.state = std::move(auth_block_state)};

  EXPECT_EQ(CryptoError::CE_TPM_FATAL,
            auth_block.Derive(auth_input, auth_state, &key_out_data)
                ->local_crypto_error());
}

// Test the retry function in TpmEccAuthBlock::Derive failed as expected.
TEST(TpmEccAuthBlockTest, DeriveRetryFailTest) {
  TpmEccAuthBlockState auth_block_state = GetDefaultEccAuthBlockState();

  // Set up the mock expectations.
  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  SetupMockHwsec(hwsec);
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  EXPECT_CALL(hwsec, PreloadSealedData(_)).WillOnce(ReturnValue(std::nullopt));

  // The TpmEccAuthBlock shouldn't retry forever if the TPM always returning
  // error.
  EXPECT_CALL(hwsec, GetAuthValue(_, _))
      .WillRepeatedly(ReturnError<TPMError>("reboot", TPMRetryAction::kReboot));

  TpmEccAuthBlock auth_block(&hwsec, &cryptohome_keys_manager);

  KeyBlobs key_out_data;
  AuthInput auth_input;
  auth_input.user_input = brillo::SecureBlob(20, 'E');
  auth_input.locked_to_single_user = true;

  AuthBlockState auth_state{.state = std::move(auth_block_state)};

  EXPECT_EQ(CryptoError::CE_TPM_REBOOT,
            auth_block.Derive(auth_input, auth_state, &key_out_data)
                ->local_crypto_error());
}

// Test Unseal in TpmEccAuthBlock::Derive failed as expected.
TEST(TpmEccAuthBlockTest, DeriveUnsealFailTest) {
  TpmEccAuthBlockState auth_block_state = GetDefaultEccAuthBlockState();

  auth_block_state.tpm_public_key_hash = brillo::SecureBlob("public key hash");

  // Set up the mock expectations.
  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  SetupMockHwsec(hwsec);
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  EXPECT_CALL(hwsec, PreloadSealedData(_)).WillOnce(ReturnValue(std::nullopt));
  EXPECT_CALL(hwsec, GetAuthValue(_, _))
      .Times(Exactly(5))
      .WillRepeatedly(ReturnValue(brillo::SecureBlob()));

  brillo::SecureBlob fake_hvkkm(32, 'F');
  EXPECT_CALL(hwsec, UnsealWithCurrentUser(_, _, _))
      .WillOnce(ReturnError<TPMError>("fake", TPMRetryAction::kNoRetry));

  TpmEccAuthBlock auth_block(&hwsec, &cryptohome_keys_manager);

  KeyBlobs key_out_data;
  AuthInput auth_input;
  auth_input.user_input = brillo::SecureBlob(20, 'E');
  auth_input.locked_to_single_user = false;

  AuthBlockState auth_state{.state = std::move(auth_block_state)};

  EXPECT_EQ(CryptoError::CE_TPM_CRYPTO,
            auth_block.Derive(auth_input, auth_state, &key_out_data)
                ->local_crypto_error());
}

// Test CryptohomeKey in TpmEccAuthBlock::Derive failed as expected.
TEST(TpmEccAuthBlockTest, DeriveCryptohomeKeyFailTest) {
  TpmEccAuthBlockState auth_block_state = GetDefaultEccAuthBlockState();

  // Set up the mock expectations.
  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  SetupMockHwsec(hwsec);
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;

  EXPECT_CALL(*cryptohome_keys_manager.get_mock_cryptohome_key_loader(),
              HasCryptohomeKey())
      .WillRepeatedly(Return(false));

  TpmEccAuthBlock auth_block(&hwsec, &cryptohome_keys_manager);

  KeyBlobs key_out_data;
  AuthInput auth_input;
  auth_input.user_input = brillo::SecureBlob(20, 'E');
  auth_input.locked_to_single_user = true;

  AuthBlockState auth_state{.state = std::move(auth_block_state)};

  EXPECT_EQ(CryptoError::CE_TPM_REBOOT,
            auth_block.Derive(auth_input, auth_state, &key_out_data)
                ->local_crypto_error());
}

}  // namespace cryptohome
