// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_blocks/auth_block.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <absl/types/variant.h>
#include <base/files/file_path.h>
#include <gtest/gtest.h>
#include <libhwsec-foundation/error/testing_helper.h>

#include "cryptohome/auth_blocks/auth_block_state.h"
#include "cryptohome/auth_blocks/cryptohome_recovery_auth_block.h"
#include "cryptohome/auth_blocks/double_wrapped_compat_auth_block.h"
#include "cryptohome/auth_blocks/libscrypt_compat_auth_block.h"
#include "cryptohome/auth_blocks/pin_weaver_auth_block.h"
#include "cryptohome/auth_blocks/tpm_bound_to_pcr_auth_block.h"
#include "cryptohome/auth_blocks/tpm_ecc_auth_block.h"
#include "cryptohome/auth_blocks/tpm_not_bound_to_pcr_auth_block.h"
#include "cryptohome/crypto.h"
#include "cryptohome/crypto/aes.h"
#include "cryptohome/crypto/elliptic_curve_error.h"
#include "cryptohome/crypto/scrypt.h"
#include "cryptohome/crypto_error.h"
#include "cryptohome/cryptorecovery/fake_recovery_mediator_crypto.h"
#include "cryptohome/cryptorecovery/recovery_crypto_fake_tpm_backend_impl.h"
#include "cryptohome/cryptorecovery/recovery_crypto_hsm_cbor_serialization.h"
#include "cryptohome/cryptorecovery/recovery_crypto_impl.h"
#include "cryptohome/mock_cryptohome_keys_manager.h"
#include "cryptohome/mock_le_credential_backend.h"
#include "cryptohome/mock_le_credential_manager.h"
#include "cryptohome/mock_tpm.h"
#include "cryptohome/vault_keyset.h"

using cryptohome::cryptorecovery::FakeRecoveryMediatorCrypto;
using cryptohome::cryptorecovery::RecoveryCryptoImpl;
using ::hwsec::error::TPMError;
using ::hwsec::error::TPMErrorBase;
using ::hwsec::error::TPMRetryAction;
using ::hwsec_foundation::error::testing::ReturnError;
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
TpmEccAuthBlockState GetDefaultEccAuthBlockState() {
  TpmEccAuthBlockState auth_block_state;
  auth_block_state.salt = brillo::SecureBlob(32, 'A');
  auth_block_state.vkk_iv = brillo::SecureBlob(32, 'B');
  auth_block_state.sealed_hvkkm = brillo::SecureBlob(32, 'C');
  auth_block_state.extended_sealed_hvkkm = brillo::SecureBlob(32, 'D');
  auth_block_state.auth_value_rounds = 5;
  return auth_block_state;
}
}  // namespace

TEST(TpmBoundToPcrTest, CreateTest) {
  // Set up inputs to the test.
  brillo::SecureBlob vault_key(20, 'C');
  std::string obfuscated_username = "OBFUSCATED_USERNAME";
  SerializedVaultKeyset serialized;

  // Set up the mock expectations.
  brillo::SecureBlob scrypt_derived_key;
  NiceMock<MockTpm> tpm;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  brillo::SecureBlob auth_value(256, 'a');
  EXPECT_CALL(tpm, GetAuthValue(_, _, _))
      .WillOnce(DoAll(SaveArg<1>(&scrypt_derived_key),
                      SetArgPointee<2>(auth_value),
                      ReturnError<TPMErrorBase>()));
  EXPECT_CALL(tpm, SealToPcrWithAuthorization(_, auth_value, _, _))
      .Times(Exactly(2));
  ON_CALL(tpm, SealToPcrWithAuthorization(_, _, _, _))
      .WillByDefault(ReturnError<TPMErrorBase>());

  AuthInput user_input = {vault_key,
                          /*locked_to_single_user=*/base::nullopt,
                          obfuscated_username,
                          /*reset_secret=*/base::nullopt};
  KeyBlobs vkk_data;

  TpmBoundToPcrAuthBlock auth_block(&tpm, &cryptohome_keys_manager);
  AuthBlockState auth_state;
  EXPECT_EQ(CryptoError::CE_NONE,
            auth_block.Create(user_input, &auth_state, &vkk_data));
  EXPECT_TRUE(
      absl::holds_alternative<TpmBoundToPcrAuthBlockState>(auth_state.state));

  EXPECT_NE(vkk_data.vkk_key, base::nullopt);
  EXPECT_NE(vkk_data.vkk_iv, base::nullopt);
  EXPECT_NE(vkk_data.chaps_iv, base::nullopt);

  auto& tpm_state = absl::get<TpmBoundToPcrAuthBlockState>(auth_state.state);

  EXPECT_TRUE(tpm_state.salt.has_value());
  const brillo::SecureBlob& salt = tpm_state.salt.value();
  brillo::SecureBlob scrypt_derived_key_result(kDefaultPassBlobSize);
  EXPECT_TRUE(
      DeriveSecretsScrypt(vault_key, salt, {&scrypt_derived_key_result}));
  EXPECT_EQ(scrypt_derived_key, scrypt_derived_key_result);
}

TEST(TpmBoundToPcrTest, CreateFailTest) {
  // Set up inputs to the test.
  brillo::SecureBlob vault_key(20, 'C');
  std::string obfuscated_username = "OBFUSCATED_USERNAME";
  SerializedVaultKeyset serialized;

  // Set up the mock expectations.
  NiceMock<MockTpm> tpm;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;

  ON_CALL(tpm, SealToPcrWithAuthorization(_, _, _, _))
      .WillByDefault(ReturnError<TPMError>("fake", TPMRetryAction::kNoRetry));

  AuthInput user_input = {vault_key,
                          /*locked_to_single_user=*/base::nullopt,
                          obfuscated_username,
                          /*reset_secret=*/base::nullopt};
  KeyBlobs vkk_data;
  TpmBoundToPcrAuthBlock auth_block(&tpm, &cryptohome_keys_manager);
  AuthBlockState auth_state;
  EXPECT_EQ(CryptoError::CE_TPM_CRYPTO,
            auth_block.Create(user_input, &auth_state, &vkk_data));
}

TEST(TpmNotBoundToPcrTest, CreateTest) {
  // Set up inputs to the test.
  brillo::SecureBlob vault_key(20, 'C');
  std::string obfuscated_username = "OBFUSCATED_USERNAME";
  SerializedVaultKeyset serialized;

  // Set up the mock expectations.
  brillo::SecureBlob aes_skey;
  NiceMock<MockTpm> tpm;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  EXPECT_CALL(tpm, EncryptBlob(_, _, _, _))
      .WillOnce(DoAll(SaveArg<2>(&aes_skey), ReturnError<TPMErrorBase>()));

  AuthInput user_input = {vault_key,
                          /*locked_to_single_user=*/base::nullopt,
                          obfuscated_username,
                          /*reset_secret=*/base::nullopt};
  KeyBlobs vkk_data;
  TpmNotBoundToPcrAuthBlock auth_block(&tpm, &cryptohome_keys_manager);
  AuthBlockState auth_state;
  EXPECT_EQ(CryptoError::CE_NONE,
            auth_block.Create(user_input, &auth_state, &vkk_data));
  EXPECT_TRUE(absl::holds_alternative<TpmNotBoundToPcrAuthBlockState>(
      auth_state.state));

  EXPECT_NE(vkk_data.vkk_key, base::nullopt);
  EXPECT_NE(vkk_data.vkk_iv, base::nullopt);
  EXPECT_NE(vkk_data.chaps_iv, base::nullopt);

  auto& tpm_state = absl::get<TpmNotBoundToPcrAuthBlockState>(auth_state.state);

  EXPECT_TRUE(tpm_state.salt.has_value());
  const brillo::SecureBlob& salt = tpm_state.salt.value();
  brillo::SecureBlob aes_skey_result(kDefaultAesKeySize);
  EXPECT_TRUE(DeriveSecretsScrypt(vault_key, salt, {&aes_skey_result}));
  EXPECT_EQ(aes_skey, aes_skey_result);
}

TEST(TpmNotBoundToPcrTest, CreateFailTest) {
  // Set up inputs to the test.
  brillo::SecureBlob vault_key(20, 'C');
  std::string obfuscated_username = "OBFUSCATED_USERNAME";
  SerializedVaultKeyset serialized;

  // Set up the mock expectations.
  NiceMock<MockTpm> tpm;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  ON_CALL(tpm, EncryptBlob(_, _, _, _))
      .WillByDefault(ReturnError<TPMError>("fake", TPMRetryAction::kNoRetry));

  AuthInput user_input = {vault_key,
                          /*locked_to_single_user=*/base::nullopt,
                          obfuscated_username,
                          /*reset_secret=*/base::nullopt};
  KeyBlobs vkk_data;
  TpmNotBoundToPcrAuthBlock auth_block(&tpm, &cryptohome_keys_manager);
  AuthBlockState auth_state;
  EXPECT_EQ(CryptoError::CE_TPM_CRYPTO,
            auth_block.Create(user_input, &auth_state, &vkk_data));
}

// Check required field |salt| in TpmNotBoundToPcrAuthBlockState.
TEST(TpmNotBoundToPcrTest, DeriveFailureMissingSalt) {
  brillo::SecureBlob tpm_key(20, 'C');
  NiceMock<MockTpm> tpm;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  TpmNotBoundToPcrAuthBlock auth_block(&tpm, &cryptohome_keys_manager);

  AuthBlockState auth_state;
  TpmNotBoundToPcrAuthBlockState state;
  state.scrypt_derived = true;
  state.tpm_key = tpm_key;
  auth_state.state = std::move(state);

  KeyBlobs key_blobs;
  AuthInput auth_input = {};
  EXPECT_EQ(CryptoError::CE_OTHER_CRYPTO,
            auth_block.Derive(auth_input, auth_state, &key_blobs));
}

// Check required field |tpm_key| in TpmNotBoundToPcrAuthBlockState.
TEST(TpmNotBoundToPcrTest, DeriveFailureMissingTpmKey) {
  brillo::SecureBlob salt(PKCS5_SALT_LEN, 'A');
  NiceMock<MockTpm> tpm;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  TpmNotBoundToPcrAuthBlock auth_block(&tpm, &cryptohome_keys_manager);

  AuthBlockState auth_state;
  TpmNotBoundToPcrAuthBlockState state;
  state.scrypt_derived = true;
  state.salt = salt;
  auth_state.state = std::move(state);

  KeyBlobs key_blobs;
  AuthInput auth_input = {};
  EXPECT_EQ(CryptoError::CE_OTHER_CRYPTO,
            auth_block.Derive(auth_input, auth_state, &key_blobs));
}

TEST(TpmNotBoundToPcrTest, DeriveSuccess) {
  brillo::SecureBlob tpm_key(20, 'A');
  brillo::SecureBlob salt(PKCS5_SALT_LEN, 'B');
  brillo::SecureBlob vault_key(20, 'C');
  brillo::SecureBlob aes_key(kDefaultAesKeySize);
  ASSERT_TRUE(DeriveSecretsScrypt(vault_key, salt, {&aes_key}));

  NiceMock<MockTpm> tpm;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  TpmNotBoundToPcrAuthBlock auth_block(&tpm, &cryptohome_keys_manager);
  EXPECT_CALL(tpm, DecryptBlob(_, tpm_key, aes_key, _, _)).Times(Exactly(1));
  AuthBlockState auth_state;
  TpmNotBoundToPcrAuthBlockState state;
  state.scrypt_derived = true;
  state.salt = salt;
  state.tpm_key = tpm_key;
  auth_state.state = std::move(state);

  KeyBlobs key_blobs;
  AuthInput auth_input = {.user_input = vault_key};
  EXPECT_EQ(CryptoError::CE_NONE,
            auth_block.Derive(auth_input, auth_state, &key_blobs));
}

TEST(PinWeaverAuthBlockTest, CreateTest) {
  // Set up inputs to the test.
  brillo::SecureBlob vault_key(20, 'C');
  std::string obfuscated_username = "OBFUSCATED_USERNAME";
  brillo::SecureBlob reset_secret(32, 'S');

  // Set up the mock expectations.
  brillo::SecureBlob le_secret;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  NiceMock<MockLECredentialManager> le_cred_manager;
  EXPECT_CALL(le_cred_manager, InsertCredential(_, _, _, _, _, _))
      .WillOnce(DoAll(SaveArg<0>(&le_secret), Return(LE_CRED_SUCCESS)));

  // Call the Create() method.
  AuthInput user_input = {vault_key,
                          /*locked_to_single_user=*/base::nullopt,
                          obfuscated_username, reset_secret};
  KeyBlobs vkk_data;

  PinWeaverAuthBlock auth_block(&le_cred_manager, &cryptohome_keys_manager);
  AuthBlockState auth_state;
  EXPECT_EQ(CryptoError::CE_NONE,
            auth_block.Create(user_input, &auth_state, &vkk_data));
  EXPECT_TRUE(
      absl::holds_alternative<PinWeaverAuthBlockState>(auth_state.state));

  auto& pin_state = absl::get<PinWeaverAuthBlockState>(auth_state.state);

  EXPECT_TRUE(pin_state.salt.has_value());
  const brillo::SecureBlob& salt = pin_state.salt.value();
  brillo::SecureBlob le_secret_result(kDefaultAesKeySize);
  EXPECT_TRUE(DeriveSecretsScrypt(vault_key, salt, {&le_secret_result}));
  EXPECT_EQ(le_secret, le_secret_result);
}

TEST(PinWeaverAuthBlockTest, CreateFailTest) {
  brillo::SecureBlob vault_key(20, 'C');
  std::string obfuscated_username = "OBFUSCATED_USERNAME";
  brillo::SecureBlob reset_secret(32, 'S');

  // Now test that the method fails if the le_cred_manager fails.
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager_fail;
  NiceMock<MockLECredentialManager> le_cred_manager_fail;
  ON_CALL(le_cred_manager_fail, InsertCredential(_, _, _, _, _, _))
      .WillByDefault(Return(LE_CRED_ERROR_HASH_TREE));

  PinWeaverAuthBlock auth_block_fail(&le_cred_manager_fail,
                                     &cryptohome_keys_manager_fail);
  // Call the Create() method.
  AuthInput user_input = {vault_key,
                          /*locked_to_single_user=*/base::nullopt,
                          obfuscated_username, reset_secret};
  KeyBlobs vkk_data;
  AuthBlockState auth_state;
  EXPECT_EQ(CryptoError::CE_OTHER_CRYPTO,
            auth_block_fail.Create(user_input, &auth_state, &vkk_data));
}

// Check required field |le_label| in PinWeaverAuthBlockState.
TEST(PinWeaverAuthBlockTest, DeriveFailureMissingLeLabel) {
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
  AuthInput auth_input = {};
  EXPECT_EQ(CryptoError::CE_OTHER_CRYPTO,
            auth_block.Derive(auth_input, auth_state, &key_blobs));
}

// Check required field |salt| in PinWeaverAuthBlockState.
TEST(PinWeaverAuthBlockTest, DeriveFailureMissingSalt) {
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
  AuthInput auth_input = {};
  EXPECT_EQ(CryptoError::CE_OTHER_CRYPTO,
            auth_block.Derive(auth_input, auth_state, &key_blobs));
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
      .WillByDefault(Return(LE_CRED_SUCCESS));
  EXPECT_CALL(le_cred_manager, CheckCredential(_, le_secret, _, _))
      .Times(Exactly(1));

  NiceMock<MockTpm> tpm;
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
  EXPECT_TRUE(vk.GetAuthBlockState(&auth_state));

  KeyBlobs key_blobs;
  AuthInput auth_input = {vault_key};
  EXPECT_EQ(CryptoError::CE_NONE,
            auth_block.Derive(auth_input, auth_state, &key_blobs));

  // Set expectations of the key blobs.
  EXPECT_NE(key_blobs.reset_secret, base::nullopt);
  EXPECT_NE(key_blobs.chaps_iv, base::nullopt);
  EXPECT_NE(key_blobs.vkk_iv, base::nullopt);

  // PinWeaver should always use unique IVs.
  EXPECT_NE(key_blobs.chaps_iv.value(), key_blobs.vkk_iv.value());
}

TEST(PinWeaverAuthBlockTest, CheckCredentialFailureTest) {
  brillo::SecureBlob vault_key(20, 'C');
  brillo::SecureBlob salt(PKCS5_SALT_LEN, 'A');
  brillo::SecureBlob chaps_iv(kAesBlockSize, 'F');
  brillo::SecureBlob fek_iv(kAesBlockSize, 'X');

  brillo::SecureBlob le_secret(kDefaultAesKeySize);
  ASSERT_TRUE(DeriveSecretsScrypt(vault_key, salt, {&le_secret}));

  NiceMock<MockLECredentialManager> le_cred_manager;

  ON_CALL(le_cred_manager, CheckCredential(_, _, _, _))
      .WillByDefault(Return(LE_CRED_ERROR_INVALID_LE_SECRET));
  EXPECT_CALL(le_cred_manager, CheckCredential(_, le_secret, _, _))
      .Times(Exactly(1));

  NiceMock<MockTpm> tpm;
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
  EXPECT_TRUE(vk.GetAuthBlockState(&auth_state));

  KeyBlobs key_blobs;
  AuthInput auth_input = {vault_key};
  EXPECT_EQ(CryptoError::CE_LE_INVALID_SECRET,
            auth_block.Derive(auth_input, auth_state, &key_blobs));
}

TEST(PinWeaverAuthBlockTest, CheckCredentialNotFatalCryptoErrorTest) {
  brillo::SecureBlob vault_key(20, 'C');
  brillo::SecureBlob salt(PKCS5_SALT_LEN, 'A');
  brillo::SecureBlob chaps_iv(kAesBlockSize, 'F');
  brillo::SecureBlob fek_iv(kAesBlockSize, 'X');

  brillo::SecureBlob le_secret(kDefaultAesKeySize);
  ASSERT_TRUE(DeriveSecretsScrypt(vault_key, salt, {&le_secret}));

  NiceMock<MockLECredentialManager> le_cred_manager;

  ON_CALL(le_cred_manager, CheckCredential(_, _, _, _))
      .WillByDefault(Return(LE_CRED_ERROR_HASH_TREE));
  EXPECT_CALL(le_cred_manager, CheckCredential(_, le_secret, _, _))
      .WillOnce(Return(LE_CRED_ERROR_INVALID_LE_SECRET))
      .WillOnce(Return(LE_CRED_ERROR_INVALID_RESET_SECRET))
      .WillOnce(Return(LE_CRED_ERROR_TOO_MANY_ATTEMPTS))
      .WillOnce(Return(LE_CRED_ERROR_HASH_TREE))
      .WillOnce(Return(LE_CRED_ERROR_INVALID_LABEL))
      .WillOnce(Return(LE_CRED_ERROR_NO_FREE_LABEL))
      .WillOnce(Return(LE_CRED_ERROR_INVALID_METADATA))
      .WillOnce(Return(LE_CRED_ERROR_UNCLASSIFIED))
      .WillOnce(Return(LE_CRED_ERROR_LE_LOCKED))
      .WillOnce(Return(LE_CRED_ERROR_PCR_NOT_MATCH));

  NiceMock<MockTpm> tpm;
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
  EXPECT_TRUE(vk.GetAuthBlockState(&auth_state));

  KeyBlobs key_blobs;
  AuthInput auth_input = {vault_key};
  for (int i = 0; i < 10; i++) {
    CryptoError error = auth_block.Derive(auth_input, auth_state, &key_blobs);
    EXPECT_NE(CryptoError::CE_TPM_FATAL, error);
    EXPECT_NE(CryptoError::CE_OTHER_FATAL, error);
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

  NiceMock<MockTpm> tpm;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  ScopedKeyHandle handle;

  EXPECT_CALL(tpm, PreloadSealedData(_, _))
      .WillOnce(Invoke(
          [&](const brillo::SecureBlob&, ScopedKeyHandle* preload_handle) {
            preload_handle->reset(&tpm, 5566);
            return nullptr;
          }));
  brillo::SecureBlob auth_value(256, 'a');
  EXPECT_CALL(tpm, GetAuthValue(_, pass_blob, _))
      .WillOnce(
          DoAll(SetArgPointee<2>(auth_value), ReturnError<TPMErrorBase>()));
  EXPECT_CALL(tpm, UnsealWithAuthorization(base::Optional<TpmKeyHandle>(5566),
                                           _, auth_value, _, _))
      .Times(Exactly(1));

  TpmBoundToPcrAuthBlock tpm_auth_block(&tpm, &cryptohome_keys_manager);
  EXPECT_EQ(CryptoError::CE_NONE,
            tpm_auth_block.DecryptTpmBoundToPcr(vault_key, tpm_key, salt,
                                                &vkk_iv, &vkk_key));
}

TEST(TPMAuthBlockTest, DecryptBoundToPcrNoPreloadTest) {
  brillo::SecureBlob vault_key(20, 'C');
  brillo::SecureBlob tpm_key(20, 'B');
  brillo::SecureBlob salt(PKCS5_SALT_LEN, 'A');

  brillo::SecureBlob vkk_iv(kDefaultAesKeySize);
  brillo::SecureBlob vkk_key;

  brillo::SecureBlob pass_blob(kDefaultPassBlobSize);
  ASSERT_TRUE(DeriveSecretsScrypt(vault_key, salt, {&pass_blob}));

  NiceMock<MockTpm> tpm;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  ScopedKeyHandle handle;
  EXPECT_CALL(tpm, PreloadSealedData(_, _))
      .WillOnce(ReturnError<TPMErrorBase>());
  brillo::SecureBlob auth_value(256, 'a');
  EXPECT_CALL(tpm, GetAuthValue(_, pass_blob, _))
      .WillOnce(
          DoAll(SetArgPointee<2>(auth_value), ReturnError<TPMErrorBase>()));
  base::Optional<TpmKeyHandle> nullopt;
  EXPECT_CALL(tpm, UnsealWithAuthorization(nullopt, _, auth_value, _, _))
      .Times(Exactly(1));

  TpmBoundToPcrAuthBlock tpm_auth_block(&tpm, &cryptohome_keys_manager);
  EXPECT_EQ(CryptoError::CE_NONE,
            tpm_auth_block.DecryptTpmBoundToPcr(vault_key, tpm_key, salt,
                                                &vkk_iv, &vkk_key));
}

TEST(TPMAuthBlockTest, DecryptNotBoundToPcrTest) {
  brillo::SecureBlob vault_key(20, 'C');
  brillo::SecureBlob tpm_key(20, 'B');
  brillo::SecureBlob salt(PKCS5_SALT_LEN, 'A');

  brillo::SecureBlob vkk_key;
  brillo::SecureBlob vkk_iv(kDefaultAesKeySize);
  brillo::SecureBlob aes_key(kDefaultAesKeySize);

  ASSERT_TRUE(DeriveSecretsScrypt(vault_key, salt, {&aes_key}));

  NiceMock<MockTpm> tpm;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  EXPECT_CALL(tpm, DecryptBlob(_, tpm_key, aes_key, _, _)).Times(Exactly(1));

  TpmNotBoundToPcrAuthBlockState tpm_state;
  tpm_state.scrypt_derived = true;
  tpm_state.password_rounds = 0x5000;

  TpmNotBoundToPcrAuthBlock tpm_auth_block(&tpm, &cryptohome_keys_manager);
  EXPECT_EQ(CryptoError::CE_NONE,
            tpm_auth_block.DecryptTpmNotBoundToPcr(
                tpm_state, vault_key, tpm_key, salt, &vkk_iv, &vkk_key));
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
  NiceMock<MockTpm> tpm;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  EXPECT_CALL(tpm, PreloadSealedData(_, _)).Times(Exactly(1));
  EXPECT_CALL(tpm, GetAuthValue(_, _, _)).Times(Exactly(1));
  EXPECT_CALL(tpm, UnsealWithAuthorization(_, _, _, _, _)).Times(Exactly(1));

  TpmBoundToPcrAuthBlock auth_block(&tpm, &cryptohome_keys_manager);

  KeyBlobs key_out_data;
  AuthInput auth_input;
  auth_input.user_input = key;
  auth_input.locked_to_single_user = false;

  VaultKeyset vk;
  vk.InitializeFromSerialized(serialized);
  AuthBlockState auth_state;
  EXPECT_TRUE(vk.GetAuthBlockState(&auth_state));

  EXPECT_EQ(CryptoError::CE_NONE,
            auth_block.Derive(auth_input, auth_state, &key_out_data));

  // Assert that the returned key blobs isn't uninitialized.
  EXPECT_NE(key_out_data.vkk_iv, base::nullopt);
  EXPECT_NE(key_out_data.vkk_key, base::nullopt);
  EXPECT_EQ(key_out_data.vkk_iv.value(), key_out_data.chaps_iv.value());
}

// Check required field |salt| in TpmBoundToPcrAuthBlockState.
TEST(TpmAuthBlockTest, DeriveFailureMissingSalt) {
  brillo::SecureBlob tpm_key(20, 'C');
  NiceMock<MockTpm> tpm;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  TpmBoundToPcrAuthBlock auth_block(&tpm, &cryptohome_keys_manager);

  AuthBlockState auth_state;
  TpmBoundToPcrAuthBlockState state;
  state.scrypt_derived = true;
  state.tpm_key = tpm_key;
  state.extended_tpm_key = tpm_key;
  auth_state.state = std::move(state);

  KeyBlobs key_blobs;
  AuthInput auth_input = {};
  EXPECT_EQ(CryptoError::CE_OTHER_CRYPTO,
            auth_block.Derive(auth_input, auth_state, &key_blobs));
}

// Check required field |tpm_key| in TpmBoundToPcrAuthBlockState.
TEST(TpmAuthBlockTest, DeriveFailureMissingTpmKey) {
  brillo::SecureBlob tpm_key(20, 'C');
  brillo::SecureBlob salt(PKCS5_SALT_LEN, 'A');
  NiceMock<MockTpm> tpm;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  TpmBoundToPcrAuthBlock auth_block(&tpm, &cryptohome_keys_manager);

  AuthBlockState auth_state;
  TpmBoundToPcrAuthBlockState state;
  state.scrypt_derived = true;
  state.salt = salt;
  state.extended_tpm_key = tpm_key;
  auth_state.state = std::move(state);

  KeyBlobs key_blobs;
  AuthInput auth_input = {};
  EXPECT_EQ(CryptoError::CE_OTHER_CRYPTO,
            auth_block.Derive(auth_input, auth_state, &key_blobs));
}

// Check required field |extended_tpm_key| in TpmBoundToPcrAuthBlockState.
TEST(TpmAuthBlockTest, DeriveFailureMissingExtendedTpmKey) {
  brillo::SecureBlob tpm_key(20, 'C');
  brillo::SecureBlob salt(PKCS5_SALT_LEN, 'A');
  NiceMock<MockTpm> tpm;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  TpmBoundToPcrAuthBlock auth_block(&tpm, &cryptohome_keys_manager);

  AuthBlockState auth_state;
  TpmBoundToPcrAuthBlockState state;
  state.scrypt_derived = true;
  state.salt = salt;
  state.tpm_key = tpm_key;
  auth_state.state = std::move(state);

  KeyBlobs key_blobs;
  AuthInput auth_input = {};
  EXPECT_EQ(CryptoError::CE_OTHER_CRYPTO,
            auth_block.Derive(auth_input, auth_state, &key_blobs));
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
  EXPECT_TRUE(vk.GetAuthBlockState(&auth_state));

  NiceMock<MockTpm> tpm;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  DoubleWrappedCompatAuthBlock auth_block(&tpm, &cryptohome_keys_manager);

  EXPECT_EQ(CryptoError::CE_NONE,
            auth_block.Derive(auth_input, auth_state, &key_out_data));
}

TEST(LibScryptCompatAuthBlockTest, CreateTest) {
  AuthInput auth_input;
  auth_input.user_input = brillo::SecureBlob("foo");

  KeyBlobs blobs;

  LibScryptCompatAuthBlock auth_block;
  AuthBlockState auth_state;
  EXPECT_EQ(CryptoError::CE_NONE,
            auth_block.Create(auth_input, &auth_state, &blobs));

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
  EXPECT_TRUE(vk.GetAuthBlockState(&auth_state));

  LibScryptCompatAuthBlock auth_block;
  EXPECT_EQ(CryptoError::CE_NONE,
            auth_block.Derive(auth_input, auth_state, &key_out_data));

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

TEST(CryptohomeRecoveryAuthBlockTest, SuccessTest) {
  brillo::SecureBlob mediator_pub_key;
  ASSERT_TRUE(
      FakeRecoveryMediatorCrypto::GetFakeMediatorPublicKey(&mediator_pub_key));
  brillo::SecureBlob epoch_pub_key;
  ASSERT_TRUE(
      FakeRecoveryMediatorCrypto::GetFakeEpochPublicKey(&epoch_pub_key));
  cryptorecovery::CryptoRecoveryEpochResponse epoch_response;
  ASSERT_TRUE(
      FakeRecoveryMediatorCrypto::GetFakeEpochResponse(&epoch_response));
  AuthInput auth_input;
  CryptohomeRecoveryAuthInput cryptohome_recovery_auth_input;
  cryptohome_recovery_auth_input.mediator_pub_key = mediator_pub_key;
  auth_input.cryptohome_recovery_auth_input = cryptohome_recovery_auth_input;

  // Create recovery key and generate Cryptohome Recovery secrets.
  KeyBlobs created_key_blobs;

  cryptorecovery::RecoveryCryptoFakeTpmBackendImpl
      recovery_crypto_fake_tpm_backend;
  NiceMock<MockTpm> tpm;
  EXPECT_CALL(tpm, GetRecoveryCryptoBackend())
      .WillRepeatedly(Return(&recovery_crypto_fake_tpm_backend));
  CryptohomeRecoveryAuthBlock auth_block(&tpm);
  AuthBlockState auth_state;
  EXPECT_EQ(CryptoError::CE_NONE,
            auth_block.Create(auth_input, &auth_state, &created_key_blobs));
  ASSERT_TRUE(created_key_blobs.vkk_key.has_value());
  ASSERT_TRUE(created_key_blobs.vkk_iv.has_value());
  ASSERT_TRUE(created_key_blobs.chaps_iv.has_value());
  ASSERT_TRUE(absl::holds_alternative<CryptohomeRecoveryAuthBlockState>(
      auth_state.state));

  const CryptohomeRecoveryAuthBlockState& cryptohome_recovery_state =
      absl::get<CryptohomeRecoveryAuthBlockState>(auth_state.state);
  ASSERT_TRUE(cryptohome_recovery_state.hsm_payload.has_value());
  ASSERT_TRUE(
      cryptohome_recovery_state.plaintext_destination_share.has_value());
  ASSERT_TRUE(cryptohome_recovery_state.channel_priv_key.has_value());
  ASSERT_TRUE(cryptohome_recovery_state.channel_pub_key.has_value());

  brillo::SecureBlob channel_priv_key =
      cryptohome_recovery_state.channel_priv_key.value();
  brillo::SecureBlob channel_pub_key =
      cryptohome_recovery_state.channel_pub_key.value();
  brillo::SecureBlob hsm_payload_cbor =
      cryptohome_recovery_state.hsm_payload.value();

  // Deserialize HSM payload stored on disk.
  cryptorecovery::HsmPayload hsm_payload;
  EXPECT_TRUE(DeserializeHsmPayloadFromCbor(hsm_payload_cbor, &hsm_payload));

  // Start recovery process.
  std::unique_ptr<cryptorecovery::RecoveryCryptoImpl> recovery =
      cryptorecovery::RecoveryCryptoImpl::Create(
          tpm.GetRecoveryCryptoBackend());
  ASSERT_TRUE(recovery);
  brillo::SecureBlob ephemeral_pub_key;
  brillo::SecureBlob recovery_request_cbor;
  cryptorecovery::RequestMetadata request_metadata;
  ASSERT_TRUE(recovery->GenerateRecoveryRequest(
      hsm_payload, request_metadata, epoch_response, channel_priv_key,
      channel_pub_key, &recovery_request_cbor, &ephemeral_pub_key));

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

  brillo::SecureBlob response_cbor;
  ASSERT_TRUE(mediator->MediateRequestPayload(
      epoch_pub_key, epoch_priv_key, mediator_priv_key, recovery_request_cbor,
      &response_cbor));

  CryptohomeRecoveryAuthInput derive_cryptohome_recovery_auth_input;
  // Save data required for key derivation in auth_input.
  derive_cryptohome_recovery_auth_input.recovery_response = response_cbor;
  derive_cryptohome_recovery_auth_input.epoch_pub_key = epoch_pub_key;
  derive_cryptohome_recovery_auth_input.ephemeral_pub_key = ephemeral_pub_key;
  auth_input.cryptohome_recovery_auth_input =
      derive_cryptohome_recovery_auth_input;

  KeyBlobs derived_key_blobs;
  EXPECT_EQ(CryptoError::CE_NONE,
            auth_block.Derive(auth_input, auth_state, &derived_key_blobs));
  ASSERT_TRUE(derived_key_blobs.vkk_key.has_value());
  ASSERT_TRUE(derived_key_blobs.vkk_iv.has_value());
  ASSERT_TRUE(derived_key_blobs.chaps_iv.has_value());

  // KeyBlobs generated by `Create` should be the same as KeyBlobs generated by
  // `Derive`.
  EXPECT_EQ(created_key_blobs.vkk_key, derived_key_blobs.vkk_key);
  EXPECT_EQ(created_key_blobs.vkk_iv, derived_key_blobs.vkk_iv);
  EXPECT_EQ(created_key_blobs.chaps_iv, derived_key_blobs.chaps_iv);
}

// Test the TpmEccAuthBlock::Create works correctly.
TEST(TpmEccAuthBlockTest, CreateTest) {
  // Set up inputs to the test.
  brillo::SecureBlob vault_key(20, 'C');
  std::string obfuscated_username = "OBFUSCATED_USERNAME";

  // Set up the mock expectations.
  brillo::SecureBlob scrypt_derived_key;
  NiceMock<MockTpm> tpm;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  brillo::SecureBlob auth_value(32, 'a');
  Tpm::TpmVersionInfo version_info;
  version_info.manufacturer = 0x43524f53;
  EXPECT_CALL(tpm, GetVersionInfo(_))
      .WillOnce(DoAll(SetArgPointee<0>(version_info), Return(true)));
  EXPECT_CALL(tpm, GetEccAuthValue(_, _, _))
      .Times(Exactly(5))
      .WillOnce(DoAll(SaveArg<1>(&scrypt_derived_key),
                      SetArgPointee<2>(auth_value),
                      ReturnError<TPMErrorBase>()))
      .WillRepeatedly(
          DoAll(SetArgPointee<2>(auth_value), ReturnError<TPMErrorBase>()));
  EXPECT_CALL(tpm, SealToPcrWithAuthorization(_, auth_value, _, _))
      .WillOnce(ReturnError<TPMErrorBase>())
      .WillOnce(ReturnError<TPMErrorBase>());

  AuthInput user_input = {vault_key,
                          /*locked_to_single_user=*/base::nullopt,
                          obfuscated_username,
                          /*reset_secret=*/base::nullopt};
  KeyBlobs vkk_data;

  TpmEccAuthBlock auth_block(&tpm, &cryptohome_keys_manager);
  AuthBlockState auth_state;
  EXPECT_EQ(CryptoError::CE_NONE,
            auth_block.Create(user_input, &auth_state, &vkk_data));
  EXPECT_TRUE(absl::holds_alternative<TpmEccAuthBlockState>(auth_state.state));

  EXPECT_NE(vkk_data.vkk_key, base::nullopt);
  EXPECT_NE(vkk_data.vkk_iv, base::nullopt);
  EXPECT_NE(vkk_data.chaps_iv, base::nullopt);

  auto& tpm_state = absl::get<TpmEccAuthBlockState>(auth_state.state);

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
  std::string obfuscated_username = "OBFUSCATED_USERNAME";

  // Set up the mock expectations.
  brillo::SecureBlob scrypt_derived_key;
  NiceMock<MockTpm> tpm;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  brillo::SecureBlob auth_value(32, 'a');
  Tpm::TpmVersionInfo version_info;
  version_info.manufacturer = 0x43524f53;
  EXPECT_CALL(tpm, GetVersionInfo(_))
      .Times(Exactly(2))
      .WillRepeatedly(DoAll(SetArgPointee<0>(version_info), Return(true)));

  // Add some communication errors and retry errors that may come from TPM
  // daemon.
  EXPECT_CALL(tpm, GetEccAuthValue(_, _, _))
      .Times(Exactly(11))
      .WillOnce(ReturnError<TPMError>("comm", TPMRetryAction::kCommunication))
      .WillOnce(ReturnError<TPMError>("later", TPMRetryAction::kLater))
      .WillOnce(ReturnError<EllipticCurveError>(
          EllipticCurveErrorCode::kScalarOutOfRange))
      .WillOnce(ReturnError<TPMError>("comm", TPMRetryAction::kCommunication))
      .WillOnce(ReturnError<TPMError>("later", TPMRetryAction::kLater))
      .WillOnce(DoAll(SaveArg<1>(&scrypt_derived_key),
                      SetArgPointee<2>(auth_value),
                      ReturnError<TPMErrorBase>()))
      .WillOnce(ReturnError<TPMError>("comm", TPMRetryAction::kCommunication))
      .WillRepeatedly(
          DoAll(SetArgPointee<2>(auth_value), ReturnError<TPMErrorBase>()));

  // Add some communication errors that may come from TPM daemon.
  EXPECT_CALL(tpm, SealToPcrWithAuthorization(_, auth_value, _, _))
      .WillOnce(ReturnError<TPMError>("comm", TPMRetryAction::kCommunication))
      .WillOnce(ReturnError<TPMErrorBase>())
      .WillOnce(ReturnError<TPMError>("comm", TPMRetryAction::kCommunication))
      .WillOnce(ReturnError<TPMErrorBase>());

  // Add some communication errors that may come from TPM daemon.
  EXPECT_CALL(tpm, GetPublicKeyHash(_, _))
      .WillOnce(ReturnError<TPMError>("comm", TPMRetryAction::kCommunication))
      .WillOnce(ReturnError<TPMError>("comm", TPMRetryAction::kCommunication))
      .WillOnce(ReturnError<TPMErrorBase>());

  AuthInput user_input = {vault_key,
                          /*locked_to_single_user=*/base::nullopt,
                          obfuscated_username,
                          /*reset_secret=*/base::nullopt};
  KeyBlobs vkk_data;

  TpmEccAuthBlock auth_block(&tpm, &cryptohome_keys_manager);
  AuthBlockState auth_state;
  EXPECT_EQ(CryptoError::CE_NONE,
            auth_block.Create(user_input, &auth_state, &vkk_data));
  EXPECT_TRUE(absl::holds_alternative<TpmEccAuthBlockState>(auth_state.state));

  EXPECT_NE(vkk_data.vkk_key, base::nullopt);
  EXPECT_NE(vkk_data.vkk_iv, base::nullopt);
  EXPECT_NE(vkk_data.chaps_iv, base::nullopt);

  auto& tpm_state = absl::get<TpmEccAuthBlockState>(auth_state.state);

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
  std::string obfuscated_username = "OBFUSCATED_USERNAME";

  // Set up the mock expectations.
  brillo::SecureBlob scrypt_derived_key;
  NiceMock<MockTpm> tpm;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  brillo::SecureBlob auth_value(32, 'a');
  Tpm::TpmVersionInfo version_info;
  version_info.manufacturer = 0x43524f53;
  EXPECT_CALL(tpm, GetVersionInfo(_))
      .WillRepeatedly(DoAll(SetArgPointee<0>(version_info), Return(true)));
  // The TpmEccAuthBlock shouldn't retry forever if the TPM always returning
  // error.
  EXPECT_CALL(tpm, GetEccAuthValue(_, _, _))
      .WillRepeatedly(ReturnError<TPMError>("later", TPMRetryAction::kLater));

  AuthInput user_input = {vault_key,
                          /*locked_to_single_user=*/base::nullopt,
                          obfuscated_username,
                          /*reset_secret=*/base::nullopt};
  KeyBlobs vkk_data;
  TpmEccAuthBlock auth_block(&tpm, &cryptohome_keys_manager);
  AuthBlockState auth_state;
  EXPECT_EQ(CryptoError::CE_TPM_REBOOT,
            auth_block.Create(user_input, &auth_state, &vkk_data));
}

// Test SealToPcr in TpmEccAuthBlock::Create failed as expected.
TEST(TpmEccAuthBlockTest, CreateSealToPcrFailTest) {
  // Set up inputs to the test.
  brillo::SecureBlob vault_key(20, 'C');
  std::string obfuscated_username = "OBFUSCATED_USERNAME";

  // Set up the mock expectations.
  NiceMock<MockTpm> tpm;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  brillo::SecureBlob auth_value(32, 'a');
  Tpm::TpmVersionInfo version_info;
  version_info.manufacturer = 0x49465800;
  EXPECT_CALL(tpm, GetVersionInfo(_))
      .WillOnce(DoAll(SetArgPointee<0>(version_info), Return(true)));
  EXPECT_CALL(tpm, GetEccAuthValue(_, _, _))
      .Times(2)
      .WillRepeatedly(
          DoAll(SetArgPointee<2>(auth_value), ReturnError<TPMErrorBase>()));

  EXPECT_CALL(tpm, SealToPcrWithAuthorization(_, auth_value, _, _))
      .WillOnce(ReturnError<TPMError>("fake", TPMRetryAction::kNoRetry));

  AuthInput user_input = {vault_key,
                          /*locked_to_single_user=*/base::nullopt,
                          obfuscated_username,
                          /*reset_secret=*/base::nullopt};
  KeyBlobs vkk_data;
  TpmEccAuthBlock auth_block(&tpm, &cryptohome_keys_manager);
  AuthBlockState auth_state;
  EXPECT_EQ(CryptoError::CE_TPM_CRYPTO,
            auth_block.Create(user_input, &auth_state, &vkk_data));
}

// Test second SealToPcr in TpmEccAuthBlock::Create failed as expected.
TEST(TpmEccAuthBlockTest, CreateSecondSealToPcrFailTest) {
  // Set up inputs to the test.
  brillo::SecureBlob vault_key(20, 'C');
  std::string obfuscated_username = "OBFUSCATED_USERNAME";

  // Set up the mock expectations.
  NiceMock<MockTpm> tpm;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  brillo::SecureBlob auth_value(32, 'a');
  Tpm::TpmVersionInfo version_info;
  version_info.manufacturer = 0x49465800;
  EXPECT_CALL(tpm, GetVersionInfo(_))
      .WillOnce(DoAll(SetArgPointee<0>(version_info), Return(true)));
  EXPECT_CALL(tpm, GetEccAuthValue(_, _, _))
      .Times(2)
      .WillRepeatedly(
          DoAll(SetArgPointee<2>(auth_value), ReturnError<TPMErrorBase>()));

  EXPECT_CALL(tpm, SealToPcrWithAuthorization(_, auth_value, _, _))
      .WillOnce(ReturnError<TPMErrorBase>())
      .WillOnce(ReturnError<TPMError>("fake", TPMRetryAction::kNoRetry));

  AuthInput user_input = {vault_key,
                          /*locked_to_single_user=*/base::nullopt,
                          obfuscated_username,
                          /*reset_secret=*/base::nullopt};
  KeyBlobs vkk_data;
  TpmEccAuthBlock auth_block(&tpm, &cryptohome_keys_manager);
  AuthBlockState auth_state;
  EXPECT_EQ(CryptoError::CE_TPM_CRYPTO,
            auth_block.Create(user_input, &auth_state, &vkk_data));
}

// Test GetEccAuthValue in TpmEccAuthBlock::Create failed as expected.
TEST(TpmEccAuthBlockTest, CreateEccAuthValueFailTest) {
  // Set up inputs to the test.
  brillo::SecureBlob vault_key(20, 'C');
  std::string obfuscated_username = "OBFUSCATED_USERNAME";

  // Set up the mock expectations.
  NiceMock<MockTpm> tpm;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  brillo::SecureBlob auth_value(32, 'a');

  EXPECT_CALL(tpm, GetVersionInfo(_)).WillOnce(Return(false));
  EXPECT_CALL(tpm, GetEccAuthValue(_, _, _))
      .WillOnce(ReturnError<TPMError>("fake", TPMRetryAction::kNoRetry));

  AuthInput user_input = {vault_key,
                          /*locked_to_single_user=*/base::nullopt,
                          obfuscated_username,
                          /*reset_secret=*/base::nullopt};
  KeyBlobs vkk_data;
  TpmEccAuthBlock auth_block(&tpm, &cryptohome_keys_manager);
  AuthBlockState auth_state;
  EXPECT_EQ(CryptoError::CE_TPM_CRYPTO,
            auth_block.Create(user_input, &auth_state, &vkk_data));
}

// Test TpmEccAuthBlock::DeriveTest works correctly.
TEST(TpmEccAuthBlockTest, DeriveTest) {
  TpmEccAuthBlockState auth_block_state = GetDefaultEccAuthBlockState();

  brillo::SecureBlob fake_hash(32, 'X');
  auth_block_state.tpm_public_key_hash = fake_hash;

  // Set up the mock expectations.
  NiceMock<MockTpm> tpm;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  EXPECT_CALL(tpm, GetPublicKeyHash(_, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(fake_hash), ReturnError<TPMErrorBase>()));
  EXPECT_CALL(tpm, PreloadSealedData(_, _)).Times(Exactly(1));
  EXPECT_CALL(tpm, GetEccAuthValue(_, _, _)).Times(Exactly(5));

  brillo::SecureBlob fake_hvkkm(32, 'F');
  EXPECT_CALL(tpm, UnsealWithAuthorization(_, _, _, _, _))
      .WillOnce(
          DoAll(SetArgPointee<4>(fake_hvkkm), ReturnError<TPMErrorBase>()));

  TpmEccAuthBlock auth_block(&tpm, &cryptohome_keys_manager);

  KeyBlobs key_out_data;
  AuthInput auth_input;
  auth_input.user_input = brillo::SecureBlob(20, 'E');
  auth_input.locked_to_single_user = false;

  AuthBlockState auth_state{.state = std::move(auth_block_state)};

  EXPECT_EQ(CryptoError::CE_NONE,
            auth_block.Derive(auth_input, auth_state, &key_out_data));

  // Assert that the returned key blobs isn't uninitialized.
  EXPECT_NE(key_out_data.vkk_iv, base::nullopt);
  EXPECT_NE(key_out_data.vkk_key, base::nullopt);
  EXPECT_EQ(key_out_data.vkk_iv.value(), key_out_data.chaps_iv.value());
}

// Test the retry function of TpmEccAuthBlock::Derive works correctly.
TEST(TpmEccAuthBlockTest, DeriveRetryTest) {
  TpmEccAuthBlockState auth_block_state = GetDefaultEccAuthBlockState();

  // Set up the mock expectations.
  NiceMock<MockTpm> tpm;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;

  // Add some communication errors that may come from TPM daemon.
  EXPECT_CALL(tpm, PreloadSealedData(_, _))
      .WillOnce(ReturnError<TPMError>("comm", TPMRetryAction::kCommunication))
      .WillRepeatedly(ReturnError<TPMErrorBase>());

  // Add some communication errors and retry errors that may come from TPM
  // daemon.
  EXPECT_CALL(tpm, GetEccAuthValue(_, _, _))
      .Times(Exactly(8))
      .WillOnce(ReturnError<TPMError>("comm", TPMRetryAction::kCommunication))
      .WillOnce(ReturnError<TPMError>("later", TPMRetryAction::kLater))
      .WillOnce(ReturnError<TPMError>("comm", TPMRetryAction::kCommunication))
      .WillRepeatedly(ReturnError<TPMErrorBase>());

  // Add some communication errors that may come from TPM daemon.
  brillo::SecureBlob fake_hvkkm(32, 'F');
  EXPECT_CALL(tpm, UnsealWithAuthorization(_, _, _, _, _))
      .WillOnce(ReturnError<TPMError>("comm", TPMRetryAction::kCommunication))
      .WillOnce(
          DoAll(SetArgPointee<4>(fake_hvkkm), ReturnError<TPMErrorBase>()));

  EXPECT_CALL(*cryptohome_keys_manager.get_mock_cryptohome_key_loader(),
              ReloadCryptohomeKey())
      .WillOnce(Return(true));

  TpmEccAuthBlock auth_block(&tpm, &cryptohome_keys_manager);

  KeyBlobs key_out_data;
  AuthInput auth_input;
  auth_input.user_input = brillo::SecureBlob(20, 'E');
  auth_input.locked_to_single_user = true;

  AuthBlockState auth_state{.state = std::move(auth_block_state)};

  EXPECT_EQ(CryptoError::CE_NONE,
            auth_block.Derive(auth_input, auth_state, &key_out_data));

  // Assert that the returned key blobs isn't uninitialized.
  EXPECT_NE(key_out_data.vkk_iv, base::nullopt);
  EXPECT_NE(key_out_data.vkk_key, base::nullopt);
  EXPECT_EQ(key_out_data.vkk_iv.value(), key_out_data.chaps_iv.value());
}

// Test GetEccAuthValue in TpmEccAuthBlock::Derive failed as expected.
TEST(TpmEccAuthBlockTest, DeriveGetEccAuthFailTest) {
  TpmEccAuthBlockState auth_block_state = GetDefaultEccAuthBlockState();

  // Set up the mock expectations.
  NiceMock<MockTpm> tpm;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  EXPECT_CALL(tpm, PreloadSealedData(_, _))
      .WillRepeatedly(ReturnError<TPMErrorBase>());

  EXPECT_CALL(tpm, GetEccAuthValue(_, _, _))
      .WillOnce(ReturnError<TPMError>("fake", TPMRetryAction::kNoRetry));

  TpmEccAuthBlock auth_block(&tpm, &cryptohome_keys_manager);

  KeyBlobs key_out_data;
  AuthInput auth_input;
  auth_input.user_input = brillo::SecureBlob(20, 'E');
  auth_input.locked_to_single_user = false;

  AuthBlockState auth_state{.state = std::move(auth_block_state)};

  EXPECT_EQ(CryptoError::CE_TPM_CRYPTO,
            auth_block.Derive(auth_input, auth_state, &key_out_data));
}

// Test PreloadSealedData in TpmEccAuthBlock::Derive failed as expected.
TEST(TpmEccAuthBlockTest, DerivePreloadSealedDataFailTest) {
  TpmEccAuthBlockState auth_block_state = GetDefaultEccAuthBlockState();

  // Set up the mock expectations.
  NiceMock<MockTpm> tpm;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  EXPECT_CALL(tpm, PreloadSealedData(_, _))
      .WillOnce(ReturnError<TPMError>("fake", TPMRetryAction::kNoRetry));

  TpmEccAuthBlock auth_block(&tpm, &cryptohome_keys_manager);

  KeyBlobs key_out_data;
  AuthInput auth_input;
  auth_input.user_input = brillo::SecureBlob(20, 'E');
  auth_input.locked_to_single_user = false;

  AuthBlockState auth_state{.state = std::move(auth_block_state)};

  EXPECT_EQ(CryptoError::CE_TPM_CRYPTO,
            auth_block.Derive(auth_input, auth_state, &key_out_data));
}

// Test GetPublicKeyHash in TpmEccAuthBlock::Derive failed as expected.
TEST(TpmEccAuthBlockTest, DeriveGetPublicKeyHashFailTest) {
  TpmEccAuthBlockState auth_block_state = GetDefaultEccAuthBlockState();

  auth_block_state.tpm_public_key_hash = brillo::SecureBlob(32, 'X');

  // Set up the mock expectations.
  NiceMock<MockTpm> tpm;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  EXPECT_CALL(tpm, GetPublicKeyHash(_, _))
      .WillOnce(ReturnError<TPMError>("fake", TPMRetryAction::kNoRetry));

  TpmEccAuthBlock auth_block(&tpm, &cryptohome_keys_manager);

  KeyBlobs key_out_data;
  AuthInput auth_input;
  auth_input.user_input = brillo::SecureBlob(20, 'E');
  auth_input.locked_to_single_user = false;

  AuthBlockState auth_state{.state = std::move(auth_block_state)};

  EXPECT_EQ(CryptoError::CE_TPM_CRYPTO,
            auth_block.Derive(auth_input, auth_state, &key_out_data));
}

// Test PublicKeyHashMismatch in TpmEccAuthBlock::Derive failed as expected.
TEST(TpmEccAuthBlockTest, DerivePublicKeyHashMismatchTest) {
  TpmEccAuthBlockState auth_block_state = GetDefaultEccAuthBlockState();

  auth_block_state.tpm_public_key_hash = brillo::SecureBlob(32, 'X');

  brillo::SecureBlob fake_hash(32, 'Z');
  // Set up the mock expectations.
  NiceMock<MockTpm> tpm;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  EXPECT_CALL(tpm, GetPublicKeyHash(_, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(fake_hash), ReturnError<TPMErrorBase>()));

  TpmEccAuthBlock auth_block(&tpm, &cryptohome_keys_manager);

  KeyBlobs key_out_data;
  AuthInput auth_input;
  auth_input.user_input = brillo::SecureBlob(20, 'E');
  auth_input.locked_to_single_user = false;

  AuthBlockState auth_state{.state = std::move(auth_block_state)};

  EXPECT_EQ(CryptoError::CE_TPM_FATAL,
            auth_block.Derive(auth_input, auth_state, &key_out_data));
}

// Test the retry function in TpmEccAuthBlock::Derive failed as expected.
TEST(TpmEccAuthBlockTest, DeriveRetryFailTest) {
  TpmEccAuthBlockState auth_block_state = GetDefaultEccAuthBlockState();

  // Set up the mock expectations.
  NiceMock<MockTpm> tpm;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  EXPECT_CALL(tpm, PreloadSealedData(_, _))
      .WillRepeatedly(ReturnError<TPMErrorBase>());

  // The TpmEccAuthBlock shouldn't retry forever if the TPM always returning
  // error.
  EXPECT_CALL(tpm, GetEccAuthValue(_, _, _))
      .WillRepeatedly(ReturnError<TPMError>("later", TPMRetryAction::kLater));

  TpmEccAuthBlock auth_block(&tpm, &cryptohome_keys_manager);

  KeyBlobs key_out_data;
  AuthInput auth_input;
  auth_input.user_input = brillo::SecureBlob(20, 'E');
  auth_input.locked_to_single_user = true;

  AuthBlockState auth_state{.state = std::move(auth_block_state)};

  EXPECT_EQ(CryptoError::CE_TPM_REBOOT,
            auth_block.Derive(auth_input, auth_state, &key_out_data));
}

// Test Unseal in TpmEccAuthBlock::Derive failed as expected.
TEST(TpmEccAuthBlockTest, DeriveUnsealFailTest) {
  TpmEccAuthBlockState auth_block_state = GetDefaultEccAuthBlockState();

  brillo::SecureBlob fake_hash(32, 'X');
  auth_block_state.tpm_public_key_hash = fake_hash;

  // Set up the mock expectations.
  NiceMock<MockTpm> tpm;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  EXPECT_CALL(tpm, GetPublicKeyHash(_, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(fake_hash), ReturnError<TPMErrorBase>()));
  EXPECT_CALL(tpm, PreloadSealedData(_, _)).Times(Exactly(1));
  EXPECT_CALL(tpm, GetEccAuthValue(_, _, _)).Times(Exactly(5));

  brillo::SecureBlob fake_hvkkm(32, 'F');
  EXPECT_CALL(tpm, UnsealWithAuthorization(_, _, _, _, _))
      .WillOnce(ReturnError<TPMError>("fake", TPMRetryAction::kNoRetry));

  TpmEccAuthBlock auth_block(&tpm, &cryptohome_keys_manager);

  KeyBlobs key_out_data;
  AuthInput auth_input;
  auth_input.user_input = brillo::SecureBlob(20, 'E');
  auth_input.locked_to_single_user = false;

  AuthBlockState auth_state{.state = std::move(auth_block_state)};

  EXPECT_EQ(CryptoError::CE_TPM_CRYPTO,
            auth_block.Derive(auth_input, auth_state, &key_out_data));
}

// Test CryptohomeKey in TpmEccAuthBlock::Derive failed as expected.
TEST(TpmEccAuthBlockTest, DeriveCryptohomeKeyFailTest) {
  TpmEccAuthBlockState auth_block_state = GetDefaultEccAuthBlockState();

  // Set up the mock expectations.
  NiceMock<MockTpm> tpm;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;

  EXPECT_CALL(*cryptohome_keys_manager.get_mock_cryptohome_key_loader(),
              HasCryptohomeKey())
      .WillRepeatedly(Return(false));

  TpmEccAuthBlock auth_block(&tpm, &cryptohome_keys_manager);

  KeyBlobs key_out_data;
  AuthInput auth_input;
  auth_input.user_input = brillo::SecureBlob(20, 'E');
  auth_input.locked_to_single_user = true;

  AuthBlockState auth_state{.state = std::move(auth_block_state)};

  EXPECT_EQ(CryptoError::CE_TPM_REBOOT,
            auth_block.Derive(auth_input, auth_state, &key_out_data));
}

}  // namespace cryptohome
