// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_blocks/auth_block_utility_impl.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>

#include <base/files/file_path.h>
#include <base/files/scoped_temp_dir.h>
#include <base/test/bind.h>
#include <brillo/cryptohome.h>
#include <brillo/secure_blob.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libhwsec-foundation/crypto/scrypt.h>
#include <libhwsec-foundation/error/testing_helper.h>

#include "cryptohome/auth_blocks/auth_block.h"
#include "cryptohome/auth_blocks/auth_block_state.h"
#include "cryptohome/auth_blocks/auth_block_type.h"
#include "cryptohome/auth_blocks/challenge_credential_auth_block.h"
#include "cryptohome/auth_blocks/double_wrapped_compat_auth_block.h"
#include "cryptohome/auth_blocks/libscrypt_compat_auth_block.h"
#include "cryptohome/auth_blocks/pin_weaver_auth_block.h"
#include "cryptohome/auth_blocks/tpm_bound_to_pcr_auth_block.h"
#include "cryptohome/auth_blocks/tpm_ecc_auth_block.h"
#include "cryptohome/auth_blocks/tpm_not_bound_to_pcr_auth_block.h"
#include "cryptohome/challenge_credentials/mock_challenge_credentials_helper.h"
#include "cryptohome/credentials.h"
#include "cryptohome/crypto.h"
#include "cryptohome/crypto_error.h"
#include "cryptohome/fake_le_credential_backend.h"
#include "cryptohome/filesystem_layout.h"
#include "cryptohome/key_objects.h"
#include "cryptohome/le_credential_manager_impl.h"
#include "cryptohome/mock_cryptohome_keys_manager.h"
#include "cryptohome/mock_key_challenge_service.h"
#include "cryptohome/mock_keyset_management.h"
#include "cryptohome/mock_le_credential_manager.h"
#include "cryptohome/mock_platform.h"
#include "cryptohome/mock_tpm.h"
#include "cryptohome/mock_vault_keyset.h"
#include "cryptohome/vault_keyset.h"

using ::hwsec::TPMErrorBase;
using ::hwsec_foundation::DeriveSecretsScrypt;
using ::hwsec_foundation::error::testing::ReturnError;
using ::testing::_;
using ::testing::ByMove;
using ::testing::DoAll;
using ::testing::Exactly;
using ::testing::Mock;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::SetArgPointee;

namespace cryptohome {

namespace {
constexpr char kUser[] = "Test User";
}  // namespace

class AuthBlockUtilityImplTest : public ::testing::Test {
 public:
  AuthBlockUtilityImplTest() : crypto_(&platform_) {}
  AuthBlockUtilityImplTest(const AuthBlockUtilityImplTest&) = delete;
  AuthBlockUtilityImplTest& operator=(const AuthBlockUtilityImplTest&) = delete;
  virtual ~AuthBlockUtilityImplTest() {}

  void SetUp() override {
    // Setup salt for brillo functions.
    keyset_management_ = std::make_unique<KeysetManagement>(
        &platform_, &crypto_, std::make_unique<VaultKeysetFactory>());
    system_salt_ =
        brillo::SecureBlob(*brillo::cryptohome::home::GetSystemSalt());
  }

 protected:
  MockPlatform platform_;
  Crypto crypto_;
  brillo::SecureBlob system_salt_;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager_;
  NiceMock<MockTpm> tpm_;
  std::unique_ptr<KeysetManagement> keyset_management_;
  std::unique_ptr<AuthBlockUtilityImpl> auth_block_utility_impl_;
  NiceMock<MockChallengeCredentialsHelper> challenge_credentials_helper_;
};

// Test that CreateKeyBlobsWithAuthBlock creates AuthBlockState and KeyBlobs
// with PinWeaverAuthBlock when the AuthBlock type is low entropy credential.
TEST_F(AuthBlockUtilityImplTest, CreatePinweaverAuthBlockTest) {
  // Setup mock expectations and test inputs for low entropy AuthBlock.
  brillo::SecureBlob passkey(20, 'A');
  Credentials credentials(kUser, passkey);
  brillo::SecureBlob reset_secret(32, 'S');
  brillo::SecureBlob le_secret;

  MockLECredentialManager* le_cred_manager = new MockLECredentialManager();

  EXPECT_CALL(*le_cred_manager, InsertCredential(_, _, _, _, _, _))
      .WillOnce(DoAll(SaveArg<0>(&le_secret), Return(LE_CRED_SUCCESS)));
  crypto_.set_le_manager_for_testing(
      std::unique_ptr<cryptohome::LECredentialManager>(le_cred_manager));
  crypto_.Init(&tpm_, &cryptohome_keys_manager_);

  auth_block_utility_impl_ = std::make_unique<AuthBlockUtilityImpl>(
      keyset_management_.get(), &crypto_, &platform_);

  // Test
  KeyBlobs out_key_blobs;
  AuthBlockState out_state;
  EXPECT_EQ(CryptoError::CE_NONE,
            auth_block_utility_impl_->CreateKeyBlobsWithAuthBlock(
                AuthBlockType::kPinWeaver, credentials, reset_secret, out_state,
                out_key_blobs));

  // Verify that a PinWeaver AuthBlock is generated.
  EXPECT_TRUE(std::holds_alternative<PinWeaverAuthBlockState>(out_state.state));
  auto& pinweaver_state = std::get<PinWeaverAuthBlockState>(out_state.state);
  EXPECT_TRUE(pinweaver_state.salt.has_value());
}

// Test that DeriveKeyBlobsWithAuthBlock derives KeyBlobs with
// PinWeaverAuthBlock type when the Authblock type is low entropy credential.
TEST_F(AuthBlockUtilityImplTest, DerivePinWeaverAuthBlock) {
  // Setup mock expectations and test inputs for low entropy AuthBlock.
  brillo::SecureBlob passkey(20, 'C');
  Credentials credentials(kUser, passkey);
  brillo::SecureBlob le_secret(32);
  brillo::SecureBlob chaps_iv(16, 'F');
  brillo::SecureBlob fek_iv(16, 'X');
  brillo::SecureBlob salt(system_salt_);

  MockLECredentialManager* le_cred_manager = new MockLECredentialManager();

  crypto_.set_le_manager_for_testing(
      std::unique_ptr<cryptohome::LECredentialManager>(le_cred_manager));
  crypto_.Init(&tpm_, &cryptohome_keys_manager_);

  ASSERT_TRUE(DeriveSecretsScrypt(passkey, salt, {&le_secret}));

  ON_CALL(*le_cred_manager, CheckCredential(_, _, _, _))
      .WillByDefault(Return(LE_CRED_SUCCESS));
  EXPECT_CALL(*le_cred_manager, CheckCredential(_, le_secret, _, _))
      .Times(Exactly(1));

  auth_block_utility_impl_ = std::make_unique<AuthBlockUtilityImpl>(
      keyset_management_.get(), &crypto_, &platform_);

  PinWeaverAuthBlockState pin_state = {
      .le_label = 0, .salt = salt, .chaps_iv = chaps_iv, .fek_iv = fek_iv};
  AuthBlockState auth_state = {.state = pin_state};

  // Test
  // No need to check for the KeyBlobs value, it is already being tested in
  // AuthBlock unittest.
  KeyBlobs out_key_blobs;
  EXPECT_EQ(
      CryptoError::CE_NONE,
      auth_block_utility_impl_->DeriveKeyBlobsWithAuthBlock(
          AuthBlockType::kPinWeaver, credentials, auth_state, out_key_blobs));
}

// Test that CreateKeyBlobsWithAuthBlock creates AuthBlockState and KeyBlobs
// with TpmBoundToPcrAuthBlock when the AuthBlock type is
// AuthBlockType::kTpmBoundToPcr.
TEST_F(AuthBlockUtilityImplTest, CreateTpmBackedPcrBoundAuthBlock) {
  // Setup test inputs and the mock expectations..
  brillo::SecureBlob passkey(20, 'A');
  Credentials credentials(kUser, passkey);

  brillo::SecureBlob scrypt_derived_key;
  crypto_.Init(&tpm_, &cryptohome_keys_manager_);

  brillo::SecureBlob auth_value(256, 'a');
  EXPECT_CALL(tpm_, GetAuthValue(_, _, _))
      .WillOnce(DoAll(SaveArg<1>(&scrypt_derived_key),
                      SetArgPointee<2>(auth_value),
                      ReturnError<TPMErrorBase>()));
  EXPECT_CALL(tpm_, SealToPcrWithAuthorization(_, auth_value, _, _))
      .Times(Exactly(2));
  ON_CALL(tpm_, SealToPcrWithAuthorization(_, _, _, _))
      .WillByDefault(ReturnError<TPMErrorBase>());

  auth_block_utility_impl_ = std::make_unique<AuthBlockUtilityImpl>(
      keyset_management_.get(), &crypto_, &platform_);

  // Test
  KeyBlobs out_key_blobs;
  AuthBlockState out_state;
  EXPECT_EQ(CryptoError::CE_NONE,
            auth_block_utility_impl_->CreateKeyBlobsWithAuthBlock(
                AuthBlockType::kTpmBoundToPcr, credentials, std::nullopt,
                out_state, out_key_blobs));

  // Verify that tpm backed pcr bound auth block is created.
  EXPECT_TRUE(
      std::holds_alternative<TpmBoundToPcrAuthBlockState>(out_state.state));
  EXPECT_NE(out_key_blobs.vkk_key, std::nullopt);
  EXPECT_NE(out_key_blobs.vkk_iv, std::nullopt);
  EXPECT_NE(out_key_blobs.chaps_iv, std::nullopt);
  auto& tpm_state = std::get<TpmBoundToPcrAuthBlockState>(out_state.state);
  EXPECT_TRUE(tpm_state.salt.has_value());
}

// Test that DeriveKeyBlobsWithAuthBlock derive KeyBlobs successfully with
// TpmBoundToPcrAuthBlock when the AuthBlock type is
// AuthBlockType::kTpmBoundToPcr.
TEST_F(AuthBlockUtilityImplTest, DeriveTpmBackedPcrBoundAuthBlock) {
  // Setup test inputs and the mock expectations.
  brillo::SecureBlob passkey(20, 'A');
  Credentials credentials(kUser, passkey);
  brillo::SecureBlob tpm_key(20, 'B');
  brillo::SecureBlob salt(system_salt_);
  crypto_.Init(&tpm_, &cryptohome_keys_manager_);

  // Make sure TpmAuthBlock calls DecryptTpmBoundToPcr in this case.
  EXPECT_CALL(tpm_, PreloadSealedData(_, _)).Times(Exactly(1));
  EXPECT_CALL(tpm_, GetAuthValue(_, _, _)).Times(Exactly(1));
  EXPECT_CALL(tpm_, UnsealWithAuthorization(_, _, _, _, _)).Times(Exactly(1));

  TpmBoundToPcrAuthBlockState tpm_state = {.scrypt_derived = true,
                                           .salt = salt,
                                           .tpm_key = tpm_key,
                                           .extended_tpm_key = tpm_key};
  AuthBlockState auth_state = {.state = tpm_state};

  // Test
  KeyBlobs out_key_blobs;
  auth_block_utility_impl_ = std::make_unique<AuthBlockUtilityImpl>(
      keyset_management_.get(), &crypto_, &platform_);

  EXPECT_EQ(CryptoError::CE_NONE,
            auth_block_utility_impl_->DeriveKeyBlobsWithAuthBlock(
                AuthBlockType::kTpmBoundToPcr, credentials, auth_state,
                out_key_blobs));
}

// Test that CreateKeyBlobsWithAuthBlock creates AuthBlockState and KeyBlobs
// with TpmNotBoundToPcrAuthBlock when the AuthBlock type is
// AuthBlockType::kTpmNotBoundToPcr.
TEST_F(AuthBlockUtilityImplTest, CreateTpmBackedNonPcrBoundAuthBlock) {
  // Setup test inputs and the mock expectations.
  brillo::SecureBlob passkey(20, 'A');
  Credentials credentials(kUser, passkey);
  brillo::SecureBlob aes_key;
  crypto_.Init(&tpm_, &cryptohome_keys_manager_);

  EXPECT_CALL(tpm_, EncryptBlob(_, _, _, _))
      .WillOnce(DoAll(SaveArg<2>(&aes_key), ReturnError<TPMErrorBase>()));

  // Test
  auth_block_utility_impl_ = std::make_unique<AuthBlockUtilityImpl>(
      keyset_management_.get(), &crypto_, &platform_);
  KeyBlobs out_key_blobs;
  AuthBlockState out_state;
  EXPECT_EQ(CryptoError::CE_NONE,
            auth_block_utility_impl_->CreateKeyBlobsWithAuthBlock(
                AuthBlockType::kTpmNotBoundToPcr, credentials, std::nullopt,
                out_state, out_key_blobs));

  // Verify that Tpm backed not pcr bound Authblock is created.
  EXPECT_TRUE(
      std::holds_alternative<TpmNotBoundToPcrAuthBlockState>(out_state.state));
  EXPECT_NE(out_key_blobs.vkk_key, std::nullopt);
  EXPECT_NE(out_key_blobs.vkk_iv, std::nullopt);
  EXPECT_NE(out_key_blobs.chaps_iv, std::nullopt);
  auto& tpm_state = std::get<TpmNotBoundToPcrAuthBlockState>(out_state.state);
  EXPECT_TRUE(tpm_state.salt.has_value());
}

// Test that DeriveKeyBlobsWithAuthBlock derive KeyBlobs successfully with
// TpmNotBoundToPcrAuthBlock when the AuthBlock type is
// AuthBlockType::kTpmNotBoundToPcr.
TEST_F(AuthBlockUtilityImplTest, DeriveTpmBackedNonPcrBoundAuthBlock) {
  // Setup test inputs and the mock expectations.
  brillo::SecureBlob passkey(20, 'A');
  Credentials credentials(kUser, passkey);
  brillo::SecureBlob tpm_key(20, 'B');
  brillo::SecureBlob salt(system_salt_);
  brillo::SecureBlob aes_key(32);
  crypto_.Init(&tpm_, &cryptohome_keys_manager_);
  ASSERT_TRUE(DeriveSecretsScrypt(passkey, salt, {&aes_key}));
  EXPECT_CALL(tpm_, DecryptBlob(_, tpm_key, aes_key, _, _)).Times(Exactly(1));

  TpmNotBoundToPcrAuthBlockState tpm_state = {
      .scrypt_derived = true, .salt = salt, .tpm_key = tpm_key};
  AuthBlockState auth_state = {.state = tpm_state};

  // Test
  KeyBlobs out_key_blobs;
  auth_block_utility_impl_ = std::make_unique<AuthBlockUtilityImpl>(
      keyset_management_.get(), &crypto_, &platform_);

  EXPECT_EQ(CryptoError::CE_NONE,
            auth_block_utility_impl_->DeriveKeyBlobsWithAuthBlock(
                AuthBlockType::kTpmNotBoundToPcr, credentials, auth_state,
                out_key_blobs));
}

// Test that CreateKeyBlobsWithAuthBlock creates AuthBlockState and KeyBlobs
// with TpmEccAuthBlock when the AuthBlock type is AuthBlockType::kTpmEcc.
TEST_F(AuthBlockUtilityImplTest, CreateTpmBackedEccAuthBlock) {
  // Setup test inputs and the mock expectations.
  brillo::SecureBlob passkey(20, 'A');
  Credentials credentials(kUser, passkey);
  crypto_.Init(&tpm_, &cryptohome_keys_manager_);

  brillo::SecureBlob scrypt_derived_key;
  brillo::SecureBlob auth_value(32, 'a');
  Tpm::TpmVersionInfo version_info;
  version_info.manufacturer = 0x43524f53;
  EXPECT_CALL(tpm_, GetVersionInfo(_))
      .WillOnce(DoAll(SetArgPointee<0>(version_info), Return(true)));
  EXPECT_CALL(tpm_, GetEccAuthValue(_, _, _))
      .Times(Exactly(5))
      .WillOnce(DoAll(SaveArg<1>(&scrypt_derived_key),
                      SetArgPointee<2>(auth_value),
                      ReturnError<TPMErrorBase>()))
      .WillRepeatedly(
          DoAll(SetArgPointee<2>(auth_value), ReturnError<TPMErrorBase>()));
  EXPECT_CALL(tpm_, SealToPcrWithAuthorization(_, auth_value, _, _))
      .WillOnce(ReturnError<TPMErrorBase>())
      .WillOnce(ReturnError<TPMErrorBase>());

  auth_block_utility_impl_ = std::make_unique<AuthBlockUtilityImpl>(
      keyset_management_.get(), &crypto_, &platform_);

  // Test
  KeyBlobs out_key_blobs;
  AuthBlockState out_state;
  EXPECT_EQ(CryptoError::CE_NONE,
            auth_block_utility_impl_->CreateKeyBlobsWithAuthBlock(
                AuthBlockType::kTpmEcc, credentials, std::nullopt, out_state,
                out_key_blobs));

  // Verify that Tpm Ecc AuthBlock is created.
  EXPECT_TRUE(std::holds_alternative<TpmEccAuthBlockState>(out_state.state));
  EXPECT_NE(out_key_blobs.vkk_key, std::nullopt);
  EXPECT_NE(out_key_blobs.vkk_iv, std::nullopt);
  EXPECT_NE(out_key_blobs.chaps_iv, std::nullopt);
  auto& tpm_state = std::get<TpmEccAuthBlockState>(out_state.state);
  EXPECT_TRUE(tpm_state.salt.has_value());
}

// Test that DeriveKeyBlobsWithAuthBlock derives KeyBlobs successfully with
// TpmEccAuthBlock when the AuthBlock type is
// AuthBlockType::kTpmEcc.
TEST_F(AuthBlockUtilityImplTest, DeriveTpmBackedEccAuthBlock) {
  // Setup test inputs and the mock expectations.
  brillo::SecureBlob passkey(20, 'A');
  Credentials credentials(kUser, passkey);
  brillo::SecureBlob salt(system_salt_);
  brillo::SecureBlob fake_hash(32, 'C');
  crypto_.Init(&tpm_, &cryptohome_keys_manager_);

  EXPECT_CALL(tpm_, GetPublicKeyHash(_, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(fake_hash), ReturnError<TPMErrorBase>()));
  EXPECT_CALL(tpm_, PreloadSealedData(_, _)).Times(Exactly(1));
  EXPECT_CALL(tpm_, GetEccAuthValue(_, _, _)).Times(Exactly(5));

  brillo::SecureBlob fake_hvkkm(32, 'D');
  EXPECT_CALL(tpm_, UnsealWithAuthorization(_, _, _, _, _))
      .WillOnce(
          DoAll(SetArgPointee<4>(fake_hvkkm), ReturnError<TPMErrorBase>()));

  TpmEccAuthBlockState tpm_state;
  tpm_state.salt = salt;
  tpm_state.vkk_iv = brillo::SecureBlob(32, 'E');
  tpm_state.sealed_hvkkm = brillo::SecureBlob(32, 'F');
  tpm_state.extended_sealed_hvkkm = brillo::SecureBlob(32, 'G');
  tpm_state.auth_value_rounds = 5;
  tpm_state.tpm_public_key_hash = fake_hash;
  AuthBlockState auth_state = {.state = tpm_state};

  // Test
  KeyBlobs out_key_blobs;
  auth_block_utility_impl_ = std::make_unique<AuthBlockUtilityImpl>(
      keyset_management_.get(), &crypto_, &platform_);

  EXPECT_EQ(
      CryptoError::CE_NONE,
      auth_block_utility_impl_->DeriveKeyBlobsWithAuthBlock(
          AuthBlockType::kTpmEcc, credentials, auth_state, out_key_blobs));
}

// Test that CreateKeyBlobsWithAuthBlock creates AuthBlockState with
// LibScryptCompatAuthBlock when the AuthBlock type is
// AuthBlockType::kLibScryptCompat.
TEST_F(AuthBlockUtilityImplTest, CreateScryptAuthBlockTest) {
  // Setup mock expectations and test inputs for low entropy AuthBlock.
  brillo::SecureBlob passkey(20, 'A');
  Credentials credentials(kUser, passkey);

  auth_block_utility_impl_ = std::make_unique<AuthBlockUtilityImpl>(
      keyset_management_.get(), &crypto_, &platform_);

  // Test
  KeyBlobs out_key_blobs;
  AuthBlockState out_state;
  EXPECT_EQ(CryptoError::CE_NONE,
            auth_block_utility_impl_->CreateKeyBlobsWithAuthBlock(
                AuthBlockType::kLibScryptCompat, credentials, std::nullopt,
                out_state, out_key_blobs));

  // Verify that a script wrapped AuthBlock is generated.
  EXPECT_TRUE(
      std::holds_alternative<LibScryptCompatAuthBlockState>(out_state.state));
  auto& scrypt_state = std::get<LibScryptCompatAuthBlockState>(out_state.state);
  EXPECT_TRUE(scrypt_state.salt.has_value());
}

// Test that DeriveKeyBlobsWithAuthBlock derives AuthBlocks with
// LibScryptCompatAuthBlock when the AuthBlock type is
// AuthBlockType::kLibScryptCompat.
TEST_F(AuthBlockUtilityImplTest, DeriveScryptAuthBlock) {
  // Setup test inputs and the mock expectations.
  brillo::SecureBlob wrapped_keyset = {
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

  brillo::SecureBlob wrapped_chaps_key = {
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

  brillo::SecureBlob wrapped_reset_seed = {
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

  brillo::SecureBlob passkey = {0x31, 0x35, 0x64, 0x64, 0x38, 0x38, 0x66, 0x36,
                                0x35, 0x31, 0x30, 0x65, 0x30, 0x64, 0x35, 0x64,
                                0x35, 0x35, 0x36, 0x35, 0x35, 0x35, 0x38, 0x36,
                                0x31, 0x32, 0x62, 0x37, 0x39, 0x36, 0x30, 0x65};

  Credentials credentials(kUser, passkey);

  LibScryptCompatAuthBlockState scrypt_state = {
      .wrapped_keyset = wrapped_keyset,
      .wrapped_chaps_key = wrapped_chaps_key,
      .wrapped_reset_seed = wrapped_reset_seed,
      .salt = system_salt_};
  AuthBlockState auth_state = {.state = scrypt_state};

  // Test
  KeyBlobs out_key_blobs;
  auth_block_utility_impl_ = std::make_unique<AuthBlockUtilityImpl>(
      keyset_management_.get(), &crypto_, &platform_);

  EXPECT_EQ(CryptoError::CE_NONE,
            auth_block_utility_impl_->DeriveKeyBlobsWithAuthBlock(
                AuthBlockType::kLibScryptCompat, credentials, auth_state,
                out_key_blobs));
}

// Test that DeriveKeyBlobsWithAuthBlock derives AuthBlocks with
// DoubleWrappedCompatAuthBlock when the AuthBlock type is
// AuthBlockType::kDoubleWrappedCompat.
TEST_F(AuthBlockUtilityImplTest, DeriveDoubleWrappedAuthBlock) {
  // Setup test inputs and the mock expectations.
  crypto_.Init(&tpm_, &cryptohome_keys_manager_);
  brillo::SecureBlob wrapped_keyset = {
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

  brillo::SecureBlob wrapped_chaps_key = {
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

  brillo::SecureBlob wrapped_reset_seed = {
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

  brillo::SecureBlob passkey = {0x31, 0x35, 0x64, 0x64, 0x38, 0x38, 0x66, 0x36,
                                0x35, 0x31, 0x30, 0x65, 0x30, 0x64, 0x35, 0x64,
                                0x35, 0x35, 0x36, 0x35, 0x35, 0x35, 0x38, 0x36,
                                0x31, 0x32, 0x62, 0x37, 0x39, 0x36, 0x30, 0x65};

  Credentials credentials(kUser, passkey);

  LibScryptCompatAuthBlockState scrypt_state = {
      .wrapped_keyset = wrapped_keyset,
      .wrapped_chaps_key = wrapped_chaps_key,
      .wrapped_reset_seed = wrapped_reset_seed,
      .salt = system_salt_};
  TpmNotBoundToPcrAuthBlockState tpm_state = {
      .scrypt_derived = false,
      .salt = system_salt_,
      .tpm_key = brillo::SecureBlob(20, 'A')};
  DoubleWrappedCompatAuthBlockState double_wrapped_state = {
      .scrypt_state = scrypt_state, .tpm_state = tpm_state};
  AuthBlockState auth_state = {.state = double_wrapped_state};

  // Test
  KeyBlobs out_key_blobs;
  auth_block_utility_impl_ = std::make_unique<AuthBlockUtilityImpl>(
      keyset_management_.get(), &crypto_, &platform_);

  EXPECT_EQ(CryptoError::CE_NONE,
            auth_block_utility_impl_->DeriveKeyBlobsWithAuthBlock(
                AuthBlockType::kDoubleWrappedCompat, credentials, auth_state,
                out_key_blobs));
}

// Test that CreateKeyBlobsWithAuthBlock creates AuthBlockState with
// ChallengeCredentialAuthBlock when the AuthBlock type is
// AuthBlockType::kChallengeCredential.
TEST_F(AuthBlockUtilityImplTest, CreateChallengeCredentialAuthBlock) {
  // Setup mock expectations and test inputs for low entropy AuthBlock.
  brillo::SecureBlob passkey(20, 'A');
  Credentials credentials(kUser, passkey);

  auth_block_utility_impl_ = std::make_unique<AuthBlockUtilityImpl>(
      keyset_management_.get(), &crypto_, &platform_);

  // Test
  KeyBlobs out_key_blobs;
  AuthBlockState out_state;
  EXPECT_EQ(CryptoError::CE_NONE,
            auth_block_utility_impl_->CreateKeyBlobsWithAuthBlock(
                AuthBlockType::kChallengeCredential, credentials, std::nullopt,
                out_state, out_key_blobs));

  // Verify that a script wrapped AuthBlock is generated.
  // TODO(betuls): Update verifications after the integration of the
  // asynchronous AuthBlock.
  EXPECT_TRUE(std::holds_alternative<ChallengeCredentialAuthBlockState>(
      out_state.state));
}

// Test that DeriveKeyBlobsWithAuthBlock derives AuthBlocks with
// ChallengeCredentialAuthBlock when the AuthBlock type is
// AuthBlockType::kChallengeCredential.
TEST_F(AuthBlockUtilityImplTest, DeriveChallengeCredentialAuthBlock) {
  // Setup test inputs.
  brillo::SecureBlob wrapped_keyset = {
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

  brillo::SecureBlob wrapped_chaps_key = {
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

  brillo::SecureBlob wrapped_reset_seed = {
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

  brillo::SecureBlob passkey = {0x31, 0x35, 0x64, 0x64, 0x38, 0x38, 0x66, 0x36,
                                0x35, 0x31, 0x30, 0x65, 0x30, 0x64, 0x35, 0x64,
                                0x35, 0x35, 0x36, 0x35, 0x35, 0x35, 0x38, 0x36,
                                0x31, 0x32, 0x62, 0x37, 0x39, 0x36, 0x30, 0x65};

  Credentials credentials(kUser, passkey);

  LibScryptCompatAuthBlockState scrypt_state = {
      .wrapped_keyset = wrapped_keyset,
      .wrapped_chaps_key = wrapped_chaps_key,
      .wrapped_reset_seed = wrapped_reset_seed,
      .salt = system_salt_};
  ChallengeCredentialAuthBlockState cc_state = {.scrypt_state = scrypt_state};
  AuthBlockState auth_state = {.state = cc_state};

  // Test
  KeyBlobs out_key_blobs;
  auth_block_utility_impl_ = std::make_unique<AuthBlockUtilityImpl>(
      keyset_management_.get(), &crypto_, &platform_);

  EXPECT_EQ(CryptoError::CE_NONE,
            auth_block_utility_impl_->DeriveKeyBlobsWithAuthBlock(
                AuthBlockType::kChallengeCredential, credentials, auth_state,
                out_key_blobs));
}

// Test that CreateKeyBlobsWithAuthBlockAsync creates AuthBlockState
// and KeyBlobs, internally using a SyncToAsyncAuthBlockAdapter for
// accessing the key material from TpmBoundToPcrAuthBlock.
TEST_F(AuthBlockUtilityImplTest, SyncToAsyncAdapterCreate) {
  // Setup test inputs and the mock expectations.
  brillo::SecureBlob passkey(20, 'A');
  Credentials credentials(kUser, passkey);

  brillo::SecureBlob scrypt_derived_key;
  crypto_.Init(&tpm_, &cryptohome_keys_manager_);

  brillo::SecureBlob auth_value(256, 'a');
  EXPECT_CALL(tpm_, GetAuthValue(_, _, _))
      .WillOnce(DoAll(SaveArg<1>(&scrypt_derived_key),
                      SetArgPointee<2>(auth_value),
                      ReturnError<TPMErrorBase>()));
  EXPECT_CALL(tpm_, SealToPcrWithAuthorization(_, auth_value, _, _))
      .Times(Exactly(2));
  ON_CALL(tpm_, SealToPcrWithAuthorization(_, _, _, _))
      .WillByDefault(ReturnError<TPMErrorBase>());

  auth_block_utility_impl_ = std::make_unique<AuthBlockUtilityImpl>(
      keyset_management_.get(), &crypto_, &platform_);

  AuthBlock::CreateCallback create_callback = base::BindLambdaForTesting(
      [&](CryptoError error, std::unique_ptr<KeyBlobs> blobs,
          std::unique_ptr<AuthBlockState> auth_state) {
        // Evaluate results of KeyBlobs and AuthBlockState returned by callback.
        EXPECT_EQ(error, CryptoError::CE_NONE);
        EXPECT_TRUE(std::holds_alternative<TpmBoundToPcrAuthBlockState>(
            auth_state->state));
        EXPECT_NE(blobs->vkk_key, std::nullopt);
        EXPECT_NE(blobs->vkk_iv, std::nullopt);
        EXPECT_NE(blobs->chaps_iv, std::nullopt);
        // Verify that tpm backed pcr bound auth block is created.
        auto& tpm_state =
            std::get<TpmBoundToPcrAuthBlockState>(auth_state->state);
        EXPECT_TRUE(tpm_state.salt.has_value());
      });

  // Test.
  EXPECT_EQ(true, auth_block_utility_impl_->CreateKeyBlobsWithAuthBlockAsync(
                      AuthBlockType::kTpmBoundToPcr, credentials, std::nullopt,
                      std::move(create_callback)));
}

// Test that DeriveKeyBlobsWithAuthBlockAsync derives KeyBlobs,
// internally using a SyncToAsyncAuthBlockAdapter for
// accessing the key material from TpmBoundToPcrAuthBlock.
TEST_F(AuthBlockUtilityImplTest, SyncToAsyncAdapterDerive) {
  // Setup test inputs and the mock expectations.
  brillo::SecureBlob passkey(20, 'A');
  Credentials credentials(kUser, passkey);
  brillo::SecureBlob tpm_key(20, 'B');
  brillo::SecureBlob salt(system_salt_);
  crypto_.Init(&tpm_, &cryptohome_keys_manager_);

  // Make sure TpmAuthBlock calls DecryptTpmBoundToPcr in this case.
  EXPECT_CALL(tpm_, PreloadSealedData(_, _)).Times(Exactly(1));
  EXPECT_CALL(tpm_, GetAuthValue(_, _, _)).Times(Exactly(1));
  EXPECT_CALL(tpm_, UnsealWithAuthorization(_, _, _, _, _)).Times(Exactly(1));

  TpmBoundToPcrAuthBlockState tpm_state = {.scrypt_derived = true,
                                           .salt = salt,
                                           .tpm_key = tpm_key,
                                           .extended_tpm_key = tpm_key};
  AuthBlockState auth_state = {.state = tpm_state};

  auth_block_utility_impl_ = std::make_unique<AuthBlockUtilityImpl>(
      keyset_management_.get(), &crypto_, &platform_);

  // Test.
  AuthBlock::DeriveCallback derive_callback = base::BindLambdaForTesting(
      [&](CryptoError error, std::unique_ptr<KeyBlobs> blobs) {
        // Evaluate results of KeyBlobs returned by callback.
        EXPECT_EQ(error, CryptoError::CE_NONE);
        EXPECT_NE(blobs->vkk_key, std::nullopt);
        EXPECT_NE(blobs->vkk_iv, std::nullopt);
        EXPECT_NE(blobs->chaps_iv, std::nullopt);
      });

  EXPECT_EQ(true, auth_block_utility_impl_->DeriveKeyBlobsWithAuthBlockAsync(
                      AuthBlockType::kTpmBoundToPcr, credentials, auth_state,
                      std::move(derive_callback)));
}

// Test that CreateKeyBlobsWithAuthBlockAsync creates AuthBlockState
// and KeyBlobs, internally using a AsyncChallengeCredentialAuthBlock for
// accessing the key material.
TEST_F(AuthBlockUtilityImplTest, AsyncChallengeCredentialCreate) {
  brillo::SecureBlob passkey("passkey");
  Credentials credentials(kUser, passkey);
  crypto_.Init(&tpm_, &cryptohome_keys_manager_);

  EXPECT_CALL(challenge_credentials_helper_, GenerateNew(kUser, _, _, _, _, _))
      .WillOnce([&](auto&&, auto public_key_info, auto&&, auto&&, auto&&,
                    auto&& callback) {
        auto info = std::make_unique<structure::SignatureChallengeInfo>();
        info->public_key_spki_der = public_key_info.public_key_spki_der;
        info->salt_signature_algorithm = public_key_info.signature_algorithm[0];
        auto passkey = std::make_unique<brillo::SecureBlob>("passkey");
        std::move(callback).Run(std::move(info), std::move(passkey));
      });

  auto mock_key_challenge_service =
      std::make_unique<NiceMock<MockKeyChallengeService>>();

  auth_block_utility_impl_ = std::make_unique<AuthBlockUtilityImpl>(
      keyset_management_.get(), &crypto_, &platform_,
      &challenge_credentials_helper_, std::move(mock_key_challenge_service),
      credentials.username());

  AuthBlock::CreateCallback create_callback = base::BindLambdaForTesting(
      [&](CryptoError error, std::unique_ptr<KeyBlobs> blobs,
          std::unique_ptr<AuthBlockState> auth_state) {
        // Evaluate results of KeyBlobs and AuthBlockState returned by callback.
        EXPECT_EQ(error, CryptoError::CE_NONE);

        // Because the salt is generated randomly inside the auth block, this
        // test cannot check the exact values returned. The salt() could be
        // passed through in some test specific harness, but the underlying
        // scrypt code is tested in so many other places, it's unnecessary.
        EXPECT_FALSE(blobs->scrypt_key->derived_key().empty());
        EXPECT_FALSE(blobs->scrypt_key->ConsumeSalt().empty());

        EXPECT_FALSE(blobs->chaps_scrypt_key->derived_key().empty());
        EXPECT_FALSE(blobs->chaps_scrypt_key->ConsumeSalt().empty());

        EXPECT_FALSE(
            blobs->scrypt_wrapped_reset_seed_key->derived_key().empty());
        EXPECT_FALSE(
            blobs->scrypt_wrapped_reset_seed_key->ConsumeSalt().empty());

        ASSERT_TRUE(std::holds_alternative<ChallengeCredentialAuthBlockState>(
            auth_state->state));

        auto& tpm_state =
            std::get<ChallengeCredentialAuthBlockState>(auth_state->state);

        AuthInput auth_input{
            .challenge_credential_auth_input =
                ChallengeCredentialAuthInput{
                    .public_key_spki_der =
                        brillo::BlobFromString("public_key_spki_der"),
                    .challenge_signature_algorithms =
                        {structure::ChallengeSignatureAlgorithm::
                             kRsassaPkcs1V15Sha256},
                },
        };

        ASSERT_TRUE(tpm_state.keyset_challenge_info.has_value());
        EXPECT_EQ(tpm_state.keyset_challenge_info.value().public_key_spki_der,
                  auth_input.challenge_credential_auth_input.value()
                      .public_key_spki_der);
        EXPECT_EQ(
            tpm_state.keyset_challenge_info.value().salt_signature_algorithm,
            auth_input.challenge_credential_auth_input.value()
                .challenge_signature_algorithms[0]);
      });

  // Test.
  EXPECT_EQ(true, auth_block_utility_impl_->CreateKeyBlobsWithAuthBlockAsync(
                      AuthBlockType::kChallengeCredential, credentials,
                      std::nullopt, std::move(create_callback)));
}

// The AsyncChallengeCredentialAuthBlock::Derive should work correctly.
TEST_F(AuthBlockUtilityImplTest, AsyncChallengeCredentialDerive) {
  brillo::SecureBlob passkey("passkey");
  Credentials credentials(kUser, passkey);
  crypto_.Init(&tpm_, &cryptohome_keys_manager_);

  AuthBlockState auth_state{
      .state =
          ChallengeCredentialAuthBlockState{
              .scrypt_state =
                  LibScryptCompatAuthBlockState{
                      .wrapped_keyset =
                          brillo::SecureBlob{
                              0x73, 0x63, 0x72, 0x79, 0x70, 0x74, 0x00, 0x0E,
                              0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x01,
                              0x4D, 0xEE, 0xFC, 0x79, 0x0D, 0x79, 0x08, 0x79,
                              0xD5, 0xF6, 0x07, 0x65, 0xDF, 0x76, 0x5A, 0xAE,
                              0xD1, 0xBD, 0x1D, 0xCF, 0x29, 0xF6, 0xFF, 0x5C,
                              0x31, 0x30, 0x23, 0xD1, 0x22, 0x17, 0xDF, 0x74,
                              0x26, 0xD5, 0x11, 0x88, 0x8D, 0x40, 0xA6, 0x9C,
                              0xB9, 0x72, 0xCE, 0x37, 0x71, 0xB7, 0x39, 0x0E,
                              0x3E, 0x34, 0x0F, 0x73, 0x29, 0xF4, 0x0F, 0x89,
                              0x15, 0xF7, 0x6E, 0xA1, 0x5A, 0x29, 0x78, 0x21,
                              0xB7, 0xC0, 0x76, 0x50, 0x14, 0x5C, 0xAD, 0x77,
                              0x53, 0xC9, 0xD0, 0xFE, 0xD1, 0xB9, 0x81, 0x32,
                              0x75, 0x0E, 0x1E, 0x45, 0x34, 0xBD, 0x0B, 0xF7,
                              0xFA, 0xED, 0x9A, 0xD7, 0x6B, 0xE4, 0x2F, 0xC0,
                              0x2F, 0x58, 0xBE, 0x3A, 0x26, 0xD1, 0x82, 0x41,
                              0x09, 0x82, 0x7F, 0x17, 0xA8, 0x5C, 0x66, 0x0E,
                              0x24, 0x8B, 0x7B, 0xF5, 0xEB, 0x0C, 0x6D, 0xAE,
                              0x19, 0x5C, 0x7D, 0xC4, 0x0D, 0x8D, 0xB2, 0x18,
                              0x13, 0xD4, 0xC0, 0x32, 0x34, 0x15, 0xAE, 0x1D,
                              0xA1, 0x44, 0x2E, 0x80, 0xD8, 0x00, 0x8A, 0xB9,
                              0xDD, 0xA4, 0xC0, 0x33, 0xAE, 0x26, 0xD3, 0xE6,
                              0x53, 0xD6, 0x31, 0x5C, 0x4C, 0x10, 0xBB, 0xA9,
                              0xD5, 0x53, 0xD7, 0xAD, 0xCD, 0x97, 0x20, 0x83,
                              0xFC, 0x18, 0x4B, 0x7F, 0xC1, 0xBD, 0x85, 0x43,
                              0x12, 0x85, 0x4F, 0x6F, 0xAA, 0xDB, 0x58, 0xA0,
                              0x0F, 0x2C, 0xAB, 0xEA, 0x74, 0x8E, 0x2C, 0x28,
                              0x01, 0x88, 0x48, 0xA5, 0x0A, 0xFC, 0x2F, 0xB4,
                              0x59, 0x4B, 0xF6, 0xD9, 0xE5, 0x47, 0x94, 0x42,
                              0xA5, 0x61, 0x06, 0x8C, 0x5A, 0x9C, 0xD3, 0xA6,
                              0x30, 0x2C, 0x13, 0xCA, 0xF1, 0xFF, 0xFE, 0x5C,
                              0xE8, 0x21, 0x25, 0x9A, 0xE0, 0x50, 0xC3, 0x2F,
                              0x14, 0x71, 0x38, 0xD0, 0xE7, 0x79, 0x5D, 0xF0,
                              0x71, 0x80, 0xF0, 0x3D, 0x05, 0xB6, 0xF7, 0x67,
                              0x3F, 0x22, 0x21, 0x7A, 0xED, 0x48, 0xC4, 0x2D,
                              0xEA, 0x2E, 0xAE, 0xE9, 0xA8, 0xFF, 0xA0, 0xB6,
                              0xB4, 0x0A, 0x94, 0x34, 0x40, 0xD1, 0x6C, 0x6C,
                              0xC7, 0x90, 0x9C, 0xF7, 0xED, 0x0B, 0xED, 0x90,
                              0xB1, 0x4D, 0x6D, 0xB4, 0x3D, 0x04, 0x7E, 0x7B,
                              0x16, 0x59, 0xFF, 0xFE},
                      .wrapped_chaps_key =
                          brillo::SecureBlob{
                              0x73, 0x63, 0x72, 0x79, 0x70, 0x74, 0x00, 0x0E,
                              0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x01,
                              0xC9, 0x80, 0xA1, 0x30, 0x82, 0x40, 0xE6, 0xCF,
                              0xC8, 0x59, 0xE9, 0xB6, 0xB0, 0xE8, 0xBF, 0x95,
                              0x82, 0x79, 0x71, 0xF9, 0x86, 0x8A, 0xCA, 0x53,
                              0x23, 0xCF, 0x31, 0xFE, 0x4B, 0xD2, 0xA5, 0x26,
                              0xA4, 0x46, 0x3D, 0x35, 0xEF, 0x69, 0x02, 0xC4,
                              0xBF, 0x72, 0xDC, 0xF8, 0x90, 0x77, 0xFB, 0x59,
                              0x0D, 0x41, 0xCB, 0x5B, 0x58, 0xC6, 0x08, 0x0F,
                              0x19, 0x4E, 0xC8, 0x4A, 0x57, 0xE7, 0x63, 0x43,
                              0x39, 0x79, 0xD7, 0x6E, 0x0D, 0xD0, 0xE4, 0x4F,
                              0xFA, 0x55, 0x32, 0xE1, 0x6B, 0xE4, 0xFF, 0x12,
                              0xB1, 0xA3, 0x75, 0x9C, 0x44, 0x3A, 0x16, 0x68,
                              0x5C, 0x11, 0xD0, 0xA5, 0x4C, 0x65, 0xB0, 0xBF,
                              0x04, 0x41, 0x94, 0xFE, 0xC5, 0xDD, 0x5C, 0x78,
                              0x5B, 0x14, 0xA1, 0x3F, 0x0B, 0x17, 0x9C, 0x75,
                              0xA5, 0x9E, 0x36, 0x14, 0x5B, 0xC4, 0xAC, 0x77,
                              0x28, 0xDE, 0xEB, 0xB4, 0x51, 0x5F, 0x33, 0x36},
                      .wrapped_reset_seed =
                          brillo::SecureBlob{
                              0x73, 0x63, 0x72, 0x79, 0x70, 0x74, 0x00, 0x0E,
                              0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x01,
                              0x7F, 0x40, 0x30, 0x51, 0x2F, 0x15, 0x62, 0x15,
                              0xB1, 0x2E, 0x58, 0x27, 0x52, 0xE4, 0xFF, 0xC5,
                              0x3C, 0x1E, 0x19, 0x05, 0x84, 0xD8, 0xE8, 0xD4,
                              0xFD, 0x8C, 0x33, 0xE8, 0x06, 0x1A, 0x38, 0x28,
                              0x2D, 0xD7, 0x01, 0xD2, 0xB3, 0xE1, 0x95, 0xC3,
                              0x49, 0x63, 0x39, 0xA2, 0xB2, 0xE3, 0xDA, 0xE2,
                              0x76, 0x40, 0x40, 0x11, 0xD1, 0x98, 0xD2, 0x03,
                              0xFB, 0x60, 0xD0, 0xA1, 0xA5, 0xB5, 0x51, 0xAA,
                              0xEF, 0x6C, 0xB3, 0xAB, 0x23, 0x65, 0xCA, 0x44,
                              0x84, 0x7A, 0x71, 0xCA, 0x0C, 0x36, 0x33, 0x7F,
                              0x53, 0x06, 0x0E, 0x03, 0xBB, 0xC1, 0x9A, 0x9D,
                              0x40, 0x1C, 0x2F, 0x46, 0xB7, 0x84, 0x00, 0x59,
                              0x5B, 0xD6, 0x53, 0xE4, 0x51, 0x82, 0xC2, 0x3D,
                              0xF4, 0x46, 0xD2, 0xDD, 0xE5, 0x7A, 0x0A, 0xEB,
                              0xC8, 0x45, 0x7C, 0x37, 0x01, 0xD5, 0x37, 0x4E,
                              0xE3, 0xC7, 0xBC, 0xC6, 0x5E, 0x25, 0xFE, 0xE2,
                              0x05, 0x14, 0x60, 0x33, 0xB8, 0x1A, 0xF1, 0x17,
                              0xE1, 0x0C, 0x25, 0x00, 0xA5, 0x0A, 0xD5, 0x03},
                  },
              .keyset_challenge_info =
                  structure::SignatureChallengeInfo{
                      .public_key_spki_der =
                          brillo::BlobFromString("public_key_spki_der"),
                      .salt_signature_algorithm = structure::
                          ChallengeSignatureAlgorithm::kRsassaPkcs1V15Sha256,
                  },
          },
  };

  brillo::SecureBlob scrypt_passkey = {
      0x31, 0x35, 0x64, 0x64, 0x38, 0x38, 0x66, 0x36, 0x35, 0x31, 0x30,
      0x65, 0x30, 0x64, 0x35, 0x64, 0x35, 0x35, 0x36, 0x35, 0x35, 0x35,
      0x38, 0x36, 0x31, 0x32, 0x62, 0x37, 0x39, 0x36, 0x30, 0x65};

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

  auto mock_key_challenge_service =
      std::make_unique<NiceMock<MockKeyChallengeService>>();

  auth_block_utility_impl_ = std::make_unique<AuthBlockUtilityImpl>(
      keyset_management_.get(), &crypto_, &platform_,
      &challenge_credentials_helper_, std::move(mock_key_challenge_service),
      credentials.username());

  EXPECT_CALL(challenge_credentials_helper_,
              Decrypt(kUser, _, _, /*locked_to_single_user=*/false, _, _))
      .WillOnce([&](auto&&, auto&&, auto&&, auto&&, auto&&, auto&& callback) {
        auto passkey = std::make_unique<brillo::SecureBlob>(scrypt_passkey);
        std::move(callback).Run(std::move(passkey));
      });
  // Test.
  AuthBlock::DeriveCallback derive_callback = base::BindLambdaForTesting(
      [&](CryptoError error, std::unique_ptr<KeyBlobs> blobs) {
        ASSERT_EQ(error, CryptoError::CE_NONE);
        EXPECT_EQ(derived_key, blobs->scrypt_key->derived_key());
        EXPECT_EQ(derived_chaps_key, blobs->chaps_scrypt_key->derived_key());
        EXPECT_EQ(derived_reset_seed_key,
                  blobs->scrypt_wrapped_reset_seed_key->derived_key());
      });

  auth_block_utility_impl_->DeriveKeyBlobsWithAuthBlockAsync(
      AuthBlockType::kChallengeCredential, credentials, auth_state,
      std::move(derive_callback));
}

// Test that CreateKeyBlobsWithAuthBlockAsync fails, callback
// returns CE_OTHER_CRYPTO and nullptrs for AuthBlockState and
// KeyBlobs.
TEST_F(AuthBlockUtilityImplTest, CreateKeyBlobsWithAuthBlockAsyncFails) {
  // Setup test inputs and the mock expectations.
  brillo::SecureBlob passkey(20, 'A');
  Credentials credentials(kUser, passkey);

  brillo::SecureBlob scrypt_derived_key;
  crypto_.Init(&tpm_, &cryptohome_keys_manager_);

  auth_block_utility_impl_ = std::make_unique<AuthBlockUtilityImpl>(
      keyset_management_.get(), &crypto_, &platform_);

  AuthBlock::CreateCallback create_callback = base::BindLambdaForTesting(
      [&](CryptoError error, std::unique_ptr<KeyBlobs> blobs,
          std::unique_ptr<AuthBlockState> auth_state) {
        // Evaluate results of KeyBlobs and AuthBlockState returned by callback.
        EXPECT_EQ(error, CryptoError::CE_OTHER_CRYPTO);
        EXPECT_EQ(blobs, nullptr);
        EXPECT_EQ(auth_state, nullptr);
      });

  // Test.
  EXPECT_EQ(false, auth_block_utility_impl_->CreateKeyBlobsWithAuthBlockAsync(
                       AuthBlockType::kMaxValue, credentials, std::nullopt,
                       std::move(create_callback)));
}

TEST_F(AuthBlockUtilityImplTest, CreateKeyBlobsWithAuthBlockWrongTypeFails) {
  // Setup mock expectations and test inputs for low entropy AuthBlock.
  brillo::SecureBlob passkey(20, 'A');
  Credentials credentials(kUser, passkey);

  auth_block_utility_impl_ = std::make_unique<AuthBlockUtilityImpl>(
      keyset_management_.get(), &crypto_, &platform_);

  // Test
  KeyBlobs out_key_blobs;
  AuthBlockState out_state;
  EXPECT_EQ(CryptoError::CE_OTHER_CRYPTO,
            auth_block_utility_impl_->CreateKeyBlobsWithAuthBlock(
                AuthBlockType::kMaxValue, credentials, std::nullopt, out_state,
                out_key_blobs));
}

// Test that GetAuthBlockStateFromVaultKeyset() gives correct AuthblockState
// for each AuthBlock type.
TEST_F(AuthBlockUtilityImplTest, DeriveAuthBlockStateFromVaultKeysetTest) {
  brillo::SecureBlob chaps_iv(16, 'F');
  brillo::SecureBlob fek_iv(16, 'X');
  brillo::SecureBlob vkk_iv(16, 'Y');

  brillo::SecureBlob passkey(20, 'A');
  Credentials credentials(kUser, passkey);
  NiceMock<MockKeysetManagement> keyset_management;

  // PinWeaverAuthBlockState

  // Construct the vault keyset
  SerializedVaultKeyset serialized;
  serialized.set_flags(SerializedVaultKeyset::LE_CREDENTIAL);
  serialized.set_salt(system_salt_.data(), system_salt_.size());
  serialized.set_le_chaps_iv(chaps_iv.data(), chaps_iv.size());
  serialized.set_le_label(0);
  serialized.set_le_fek_iv(fek_iv.data(), fek_iv.size());

  auto vk = std::make_unique<VaultKeyset>();
  vk->InitializeFromSerialized(serialized);
  EXPECT_EQ(SerializedVaultKeyset::LE_CREDENTIAL, vk->GetFlags());

  KeyBlobs out_key_blobs;
  auth_block_utility_impl_ = std::make_unique<AuthBlockUtilityImpl>(
      &keyset_management, &crypto_, &platform_);

  // Test
  AuthBlockState out_state;
  EXPECT_CALL(keyset_management, GetVaultKeyset(_, _))
      .WillOnce(Return(ByMove(std::move(vk))));
  auth_block_utility_impl_->GetAuthBlockStateFromVaultKeyset(credentials,
                                                             out_state);
  EXPECT_TRUE(std::holds_alternative<PinWeaverAuthBlockState>(out_state.state));

  // ChallengeCredentialAuthBlockState

  // Construct the vault keyset
  serialized.set_flags(SerializedVaultKeyset::SCRYPT_WRAPPED |
                       SerializedVaultKeyset::SIGNATURE_CHALLENGE_PROTECTED);
  auto vk1 = std::make_unique<VaultKeyset>();
  vk1->InitializeFromSerialized(serialized);

  // Test
  EXPECT_CALL(keyset_management, GetVaultKeyset(_, _))
      .WillOnce(Return(ByMove(std::move(vk1))));
  auth_block_utility_impl_->GetAuthBlockStateFromVaultKeyset(credentials,
                                                             out_state);
  EXPECT_TRUE(std::holds_alternative<ChallengeCredentialAuthBlockState>(
      out_state.state));

  const ChallengeCredentialAuthBlockState* cc_state =
      std::get_if<ChallengeCredentialAuthBlockState>(&out_state.state);
  EXPECT_NE(cc_state, nullptr);

  // LibScryptCompatAuthBlockState

  // Construct the vault keyset
  serialized.set_flags(SerializedVaultKeyset::SCRYPT_WRAPPED);
  auto vk2 = std::make_unique<VaultKeyset>();
  vk2->InitializeFromSerialized(serialized);
  vk2->SetWrappedKeyset(brillo::SecureBlob("foo"));
  vk2->SetWrappedChapsKey(brillo::SecureBlob("bar"));
  vk2->SetWrappedResetSeed(brillo::SecureBlob("baz"));

  // Test
  EXPECT_CALL(keyset_management, GetVaultKeyset(_, _))
      .WillOnce(Return(ByMove(std::move(vk2))));
  auth_block_utility_impl_->GetAuthBlockStateFromVaultKeyset(credentials,
                                                             out_state);
  EXPECT_TRUE(
      std::holds_alternative<LibScryptCompatAuthBlockState>(out_state.state));
  const LibScryptCompatAuthBlockState* scrypt_state =
      std::get_if<LibScryptCompatAuthBlockState>(&out_state.state);
  EXPECT_NE(scrypt_state, nullptr);
  EXPECT_TRUE(scrypt_state->wrapped_keyset.has_value());
  EXPECT_TRUE(scrypt_state->wrapped_chaps_key.has_value());
  EXPECT_TRUE(scrypt_state->wrapped_reset_seed.has_value());

  // DoubleWrappedCompatAuthBlockstate fail when TPM key is not present

  // Construct the vault keyset
  serialized.set_flags(SerializedVaultKeyset::SCRYPT_WRAPPED |
                       SerializedVaultKeyset::TPM_WRAPPED);
  auto vk3 = std::make_unique<VaultKeyset>();
  vk3->InitializeFromSerialized(serialized);

  // Test
  // Double scrypt fail test when tpm key is not set, failure in creating
  // sub-state TpmNotBoundToPcrAuthBlockState.
  EXPECT_CALL(keyset_management, GetVaultKeyset(_, _))
      .WillOnce(Return(ByMove(std::move(vk3))));
  auth_block_utility_impl_->GetAuthBlockStateFromVaultKeyset(credentials,
                                                             out_state);
  EXPECT_FALSE(std::holds_alternative<DoubleWrappedCompatAuthBlockState>(
      out_state.state));

  // DoubleWrappedCompatAuthBlockstate success

  // Construct the vault keyset
  auto vk4 = std::make_unique<VaultKeyset>();
  vk4->InitializeFromSerialized(serialized);
  vk4->SetTPMKey(brillo::SecureBlob("tpmkey"));

  // Test
  EXPECT_CALL(keyset_management, GetVaultKeyset(_, _))
      .WillOnce(Return(ByMove(std::move(vk4))));
  auth_block_utility_impl_->GetAuthBlockStateFromVaultKeyset(credentials,
                                                             out_state);
  EXPECT_TRUE(std::holds_alternative<DoubleWrappedCompatAuthBlockState>(
      out_state.state));

  const DoubleWrappedCompatAuthBlockState* double_wrapped_state =
      std::get_if<DoubleWrappedCompatAuthBlockState>(&out_state.state);
  EXPECT_NE(double_wrapped_state, nullptr);

  // TpmBoundToPcrAuthBlockState

  // Construct the vault keyset
  serialized.set_flags(SerializedVaultKeyset::TPM_WRAPPED |
                       SerializedVaultKeyset::SCRYPT_DERIVED |
                       SerializedVaultKeyset::PCR_BOUND);
  auto vk5 = std::make_unique<VaultKeyset>();
  vk5->InitializeFromSerialized(serialized);
  vk5->SetTpmPublicKeyHash(brillo::SecureBlob("publickeyhash"));
  vk5->SetTPMKey(brillo::SecureBlob("tpmkey"));
  vk5->SetExtendedTPMKey(brillo::SecureBlob("extpmkey"));

  // Test
  EXPECT_CALL(keyset_management, GetVaultKeyset(_, _))
      .WillOnce(Return(ByMove(std::move(vk5))));
  auth_block_utility_impl_->GetAuthBlockStateFromVaultKeyset(credentials,
                                                             out_state);
  EXPECT_TRUE(
      std::holds_alternative<TpmBoundToPcrAuthBlockState>(out_state.state));

  const TpmBoundToPcrAuthBlockState* tpm_state =
      std::get_if<TpmBoundToPcrAuthBlockState>(&out_state.state);
  EXPECT_NE(tpm_state, nullptr);
  EXPECT_TRUE(tpm_state->scrypt_derived.value());
  EXPECT_TRUE(tpm_state->extended_tpm_key.has_value());
  EXPECT_TRUE(tpm_state->tpm_key.has_value());

  // TpmNotBoundToPcrAuthBlockState

  // Construct the vault keyset
  serialized.set_flags(SerializedVaultKeyset::TPM_WRAPPED);
  auto vk6 = std::make_unique<VaultKeyset>();
  vk6->InitializeFromSerialized(serialized);
  vk6->SetTpmPublicKeyHash(brillo::SecureBlob("publickeyhash"));
  vk6->SetTPMKey(brillo::SecureBlob("tpmkey"));
  // Test
  EXPECT_CALL(keyset_management, GetVaultKeyset(_, _))
      .WillOnce(Return(ByMove(std::move(vk6))));
  auth_block_utility_impl_->GetAuthBlockStateFromVaultKeyset(credentials,
                                                             out_state);
  EXPECT_TRUE(
      std::holds_alternative<TpmNotBoundToPcrAuthBlockState>(out_state.state));
  const TpmNotBoundToPcrAuthBlockState* tpm_state2 =
      std::get_if<TpmNotBoundToPcrAuthBlockState>(&out_state.state);
  EXPECT_NE(tpm_state2, nullptr);
  EXPECT_FALSE(tpm_state2->scrypt_derived.value());
  EXPECT_TRUE(tpm_state2->tpm_key.has_value());

  // EccAuthBlockStateTest

  // Construct the vault keyset
  SerializedVaultKeyset serialized2;
  serialized2.set_password_rounds(5);
  serialized2.set_vkk_iv(vkk_iv.data(), vkk_iv.size());
  serialized2.set_flags(SerializedVaultKeyset::TPM_WRAPPED |
                        SerializedVaultKeyset::SCRYPT_DERIVED |
                        SerializedVaultKeyset::ECC |
                        SerializedVaultKeyset::PCR_BOUND);
  auto vk7 = std::make_unique<VaultKeyset>();
  vk7->InitializeFromSerialized(serialized2);
  vk7->SetTpmPublicKeyHash(brillo::SecureBlob("publickeyhash"));
  vk7->SetTPMKey(brillo::SecureBlob("tpmkey"));
  vk7->SetExtendedTPMKey(brillo::SecureBlob("extpmkey"));

  // Test
  EXPECT_CALL(keyset_management, GetVaultKeyset(_, _))
      .WillOnce(Return(ByMove(std::move(vk7))));
  auth_block_utility_impl_->GetAuthBlockStateFromVaultKeyset(credentials,
                                                             out_state);
  EXPECT_TRUE(std::holds_alternative<TpmEccAuthBlockState>(out_state.state));

  const TpmEccAuthBlockState* tpm_ecc_state =
      std::get_if<TpmEccAuthBlockState>(&out_state.state);

  EXPECT_NE(tpm_ecc_state, nullptr);
  EXPECT_TRUE(tpm_ecc_state->salt.has_value());
  EXPECT_TRUE(tpm_ecc_state->sealed_hvkkm.has_value());
  EXPECT_TRUE(tpm_ecc_state->extended_sealed_hvkkm.has_value());
  EXPECT_TRUE(tpm_ecc_state->tpm_public_key_hash.has_value());
  EXPECT_TRUE(tpm_ecc_state->vkk_iv.has_value());
  EXPECT_EQ(tpm_ecc_state->auth_value_rounds.value(), 5);
}

TEST_F(AuthBlockUtilityImplTest, MatchAuthBlockForCreation) {
  brillo::SecureBlob passkey(20, 'A');
  Credentials credentials(kUser, passkey);
  crypto_.Init(&tpm_, &cryptohome_keys_manager_);
  auth_block_utility_impl_ = std::make_unique<AuthBlockUtilityImpl>(
      keyset_management_.get(), &crypto_, &platform_);

  // Test for kLibScryptCompat
  EXPECT_EQ(
      AuthBlockType::kLibScryptCompat,
      auth_block_utility_impl_->GetAuthBlockTypeForCreation(
          /*is_le_credential =*/false, /*is_challenge_credential =*/false));
  // Test for kPinWeaver
  KeyData key_data;
  key_data.mutable_policy()->set_low_entropy_credential(true);
  credentials.set_key_data(key_data);
  EXPECT_EQ(
      AuthBlockType::kPinWeaver,
      auth_block_utility_impl_->GetAuthBlockTypeForCreation(
          /*is_le_credential =*/true, /*is_challenge_credential =*/false));
  // Test for kChallengeResponse
  KeyData key_data2;
  key_data2.set_type(KeyData::KEY_TYPE_CHALLENGE_RESPONSE);
  credentials.set_key_data(key_data2);
  EXPECT_EQ(
      AuthBlockType::kChallengeCredential,
      auth_block_utility_impl_->GetAuthBlockTypeForCreation(
          /*is_le_credential =*/false, /*is_challenge_credential =*/true));
  // Test for Tpm backed AuthBlock types.
  ON_CALL(tpm_, IsOwned()).WillByDefault(Return(true));
  // credentials.key_data type shouldn't be challenge credential any more.
  KeyData key_data3;
  credentials.set_key_data(key_data3);

  // Test for kTpmEcc
  EXPECT_EQ(
      AuthBlockType::kTpmEcc,
      auth_block_utility_impl_->GetAuthBlockTypeForCreation(
          /*is_le_credential =*/false, /*is_challenge_credential =*/false));

  // Test for kTpmNotBoundToPcr (No TPM or no TPM2.0)
  EXPECT_CALL(tpm_, GetVersion()).WillOnce(Return(Tpm::TPM_1_2));
  EXPECT_EQ(
      AuthBlockType::kTpmNotBoundToPcr,
      auth_block_utility_impl_->GetAuthBlockTypeForCreation(
          /*is_le_credential =*/false, /*is_challenge_credential =*/false));

  // Test for kTpmBoundToPcr (TPM2.0 but no support for ECC key)
  EXPECT_CALL(tpm_, GetVersion()).WillOnce(Return(Tpm::TPM_2_0));
  EXPECT_CALL(cryptohome_keys_manager_, GetKeyLoader(CryptohomeKeyType::kECC))
      .WillOnce(Return(nullptr));
  EXPECT_EQ(
      AuthBlockType::kTpmBoundToPcr,
      auth_block_utility_impl_->GetAuthBlockTypeForCreation(
          /*is_le_credential =*/false, /*is_challenge_credential =*/false));
}

TEST_F(AuthBlockUtilityImplTest, MatchAuthBlockForDerivation) {
  // Setup
  brillo::SecureBlob passkey(20, 'A');
  Credentials credentials(kUser, passkey);

  SerializedVaultKeyset serialized;
  auto vk = std::make_unique<VaultKeyset>();

  NiceMock<MockKeysetManagement> keyset_management;
  auth_block_utility_impl_ = std::make_unique<AuthBlockUtilityImpl>(
      &keyset_management, &crypto_, &platform_);

  // Test for kLibScryptCompat
  serialized.set_flags(SerializedVaultKeyset::SCRYPT_WRAPPED);
  vk->InitializeFromSerialized(serialized);
  EXPECT_CALL(keyset_management, GetVaultKeyset(_, _))
      .WillOnce(Return(ByMove(std::move(vk))));
  EXPECT_EQ(
      AuthBlockType::kLibScryptCompat,
      auth_block_utility_impl_->GetAuthBlockTypeForDerivation(credentials));
  serialized.set_flags(SerializedVaultKeyset::SCRYPT_WRAPPED |
                       SerializedVaultKeyset::TPM_WRAPPED);
  vk = std::make_unique<VaultKeyset>();
  vk->InitializeFromSerialized(serialized);
  EXPECT_CALL(keyset_management, GetVaultKeyset(_, _))
      .WillOnce(Return(ByMove(std::move(vk))));
  EXPECT_NE(
      AuthBlockType::kLibScryptCompat,
      auth_block_utility_impl_->GetAuthBlockTypeForDerivation(credentials));

  // Test for DoubleWrappedCompat
  serialized.set_flags(SerializedVaultKeyset::SCRYPT_WRAPPED |
                       SerializedVaultKeyset::TPM_WRAPPED);
  vk = std::make_unique<VaultKeyset>();
  vk->InitializeFromSerialized(serialized);
  EXPECT_CALL(keyset_management, GetVaultKeyset(_, _))
      .WillOnce(Return(ByMove(std::move(vk))));
  EXPECT_EQ(
      AuthBlockType::kDoubleWrappedCompat,
      auth_block_utility_impl_->GetAuthBlockTypeForDerivation(credentials));

  // Test for kPinWeaver
  serialized.set_flags(SerializedVaultKeyset::LE_CREDENTIAL);
  vk = std::make_unique<VaultKeyset>();
  vk->InitializeFromSerialized(serialized);
  EXPECT_CALL(keyset_management, GetVaultKeyset(_, _))
      .WillOnce(Return(ByMove(std::move(vk))));
  EXPECT_EQ(
      AuthBlockType::kPinWeaver,
      auth_block_utility_impl_->GetAuthBlockTypeForDerivation(credentials));

  // Test for kChallengeResponse
  serialized.set_flags(SerializedVaultKeyset::SIGNATURE_CHALLENGE_PROTECTED);
  vk = std::make_unique<VaultKeyset>();
  vk->InitializeFromSerialized(serialized);
  EXPECT_CALL(keyset_management, GetVaultKeyset(_, _))
      .WillOnce(Return(ByMove(std::move(vk))));
  EXPECT_EQ(
      AuthBlockType::kChallengeCredential,
      auth_block_utility_impl_->GetAuthBlockTypeForDerivation(credentials));

  // Test for kTpmNotBoundToPcrFlags
  serialized.set_flags(SerializedVaultKeyset::TPM_WRAPPED);
  vk = std::make_unique<VaultKeyset>();
  vk->InitializeFromSerialized(serialized);
  EXPECT_CALL(keyset_management, GetVaultKeyset(_, _))
      .WillOnce(Return(ByMove(std::move(vk))));
  EXPECT_EQ(
      AuthBlockType::kTpmNotBoundToPcr,
      auth_block_utility_impl_->GetAuthBlockTypeForDerivation(credentials));
  serialized.set_flags(SerializedVaultKeyset::TPM_WRAPPED |
                       SerializedVaultKeyset::SCRYPT_WRAPPED);
  vk = std::make_unique<VaultKeyset>();
  vk->InitializeFromSerialized(serialized);
  EXPECT_CALL(keyset_management, GetVaultKeyset(_, _))
      .WillOnce(Return(ByMove(std::move(vk))));
  EXPECT_NE(
      AuthBlockType::kTpmNotBoundToPcr,
      auth_block_utility_impl_->GetAuthBlockTypeForDerivation(credentials));
  serialized.set_flags(SerializedVaultKeyset::TPM_WRAPPED |
                       SerializedVaultKeyset::PCR_BOUND);
  vk = std::make_unique<VaultKeyset>();
  vk->InitializeFromSerialized(serialized);
  EXPECT_CALL(keyset_management, GetVaultKeyset(_, _))
      .WillOnce(Return(ByMove(std::move(vk))));
  EXPECT_NE(
      AuthBlockType::kTpmNotBoundToPcr,
      auth_block_utility_impl_->GetAuthBlockTypeForDerivation(credentials));
  serialized.set_flags(SerializedVaultKeyset::TPM_WRAPPED |
                       SerializedVaultKeyset::ECC);
  vk = std::make_unique<VaultKeyset>();
  vk->InitializeFromSerialized(serialized);
  EXPECT_CALL(keyset_management, GetVaultKeyset(_, _))
      .WillOnce(Return(ByMove(std::move(vk))));
  EXPECT_NE(
      AuthBlockType::kTpmNotBoundToPcr,
      auth_block_utility_impl_->GetAuthBlockTypeForDerivation(credentials));

  // Test for kTpmEcc
  serialized.set_flags(SerializedVaultKeyset::TPM_WRAPPED |
                       SerializedVaultKeyset::SCRYPT_DERIVED |
                       SerializedVaultKeyset::PCR_BOUND |
                       SerializedVaultKeyset::ECC);
  vk = std::make_unique<VaultKeyset>();
  vk->InitializeFromSerialized(serialized);
  EXPECT_CALL(keyset_management, GetVaultKeyset(_, _))
      .WillOnce(Return(ByMove(std::move(vk))));
  EXPECT_EQ(
      AuthBlockType::kTpmEcc,
      auth_block_utility_impl_->GetAuthBlockTypeForDerivation(credentials));
  serialized.set_flags(
      SerializedVaultKeyset::TPM_WRAPPED |
      SerializedVaultKeyset::SCRYPT_DERIVED | SerializedVaultKeyset::PCR_BOUND |
      SerializedVaultKeyset::ECC | SerializedVaultKeyset::SCRYPT_WRAPPED);
  vk = std::make_unique<VaultKeyset>();
  vk->InitializeFromSerialized(serialized);
  EXPECT_CALL(keyset_management, GetVaultKeyset(_, _))
      .WillOnce(Return(ByMove(std::move(vk))));
  EXPECT_NE(
      AuthBlockType::kTpmEcc,
      auth_block_utility_impl_->GetAuthBlockTypeForDerivation(credentials));

  // Test for kTpmBoundToPcr
  serialized.set_flags(SerializedVaultKeyset::TPM_WRAPPED |
                       SerializedVaultKeyset::PCR_BOUND);
  vk = std::make_unique<VaultKeyset>();
  vk->InitializeFromSerialized(serialized);
  EXPECT_CALL(keyset_management, GetVaultKeyset(_, _))
      .WillOnce(Return(ByMove(std::move(vk))));
  EXPECT_EQ(
      AuthBlockType::kTpmBoundToPcr,
      auth_block_utility_impl_->GetAuthBlockTypeForDerivation(credentials));
  serialized.set_flags(SerializedVaultKeyset::TPM_WRAPPED |
                       SerializedVaultKeyset::PCR_BOUND |
                       SerializedVaultKeyset::ECC);
  vk = std::make_unique<VaultKeyset>();
  vk->InitializeFromSerialized(serialized);
  EXPECT_CALL(keyset_management, GetVaultKeyset(_, _))
      .WillOnce(Return(ByMove(std::move(vk))));
  EXPECT_NE(
      AuthBlockType::kTpmBoundToPcr,
      auth_block_utility_impl_->GetAuthBlockTypeForDerivation(credentials));
  serialized.set_flags(SerializedVaultKeyset::TPM_WRAPPED |
                       SerializedVaultKeyset::PCR_BOUND |
                       SerializedVaultKeyset::SCRYPT_WRAPPED);
  vk = std::make_unique<VaultKeyset>();
  vk->InitializeFromSerialized(serialized);
  EXPECT_CALL(keyset_management, GetVaultKeyset(_, _))
      .WillOnce(Return(ByMove(std::move(vk))));
  EXPECT_NE(
      AuthBlockType::kTpmBoundToPcr,
      auth_block_utility_impl_->GetAuthBlockTypeForDerivation(credentials));
}

TEST_F(AuthBlockUtilityImplTest, GetAsyncAuthBlockWithType) {
  brillo::SecureBlob passkey("passkey");
  Credentials credentials(kUser, passkey);
  crypto_.Init(&tpm_, &cryptohome_keys_manager_);
  auto mock_key_challenge_service =
      std::make_unique<NiceMock<MockKeyChallengeService>>();

  auth_block_utility_impl_ = std::make_unique<AuthBlockUtilityImpl>(
      keyset_management_.get(), &crypto_, &platform_,
      &challenge_credentials_helper_, std::move(mock_key_challenge_service),
      credentials.username());
  // Test. All fields are valid to get an AsyncChallengeCredentialAuthBlock.
  std::unique_ptr<AuthBlock> auth_block =
      auth_block_utility_impl_->GetAsyncAuthBlockWithType(
          AuthBlockType::kChallengeCredential);
  EXPECT_NE(auth_block, nullptr);
}

TEST_F(AuthBlockUtilityImplTest, GetAsyncAuthBlockWithTypeFail) {
  brillo::SecureBlob passkey("passkey");
  Credentials credentials(kUser, passkey);
  crypto_.Init(&tpm_, &cryptohome_keys_manager_);
  // Test. No valid challenge_credentials, no valid key_challenge_service or
  // account_id.
  auth_block_utility_impl_ = std::make_unique<AuthBlockUtilityImpl>(
      keyset_management_.get(), &crypto_, &platform_);

  std::unique_ptr<AuthBlock> auth_block =
      auth_block_utility_impl_->GetAsyncAuthBlockWithType(
          AuthBlockType::kChallengeCredential);
  EXPECT_EQ(auth_block, nullptr);
}

}  // namespace cryptohome
