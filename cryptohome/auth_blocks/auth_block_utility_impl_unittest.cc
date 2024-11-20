// Copyright 2022 The ChromiumOS Authors
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
#include <base/functional/bind.h>
#include <base/task/sequenced_task_runner.h>
#include <base/test/bind.h>
#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <brillo/cryptohome.h>
#include <brillo/secure_blob.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libhwsec-foundation/crypto/libscrypt_compat.h>
#include <libhwsec-foundation/crypto/rsa.h>
#include <libhwsec-foundation/crypto/scrypt.h>
#include <libhwsec-foundation/crypto/sha.h>
#include <libhwsec-foundation/error/testing_helper.h>
#include <libhwsec/error/pinweaver_error.h>
#include <libhwsec/factory/tpm2_simulator_factory_for_test.h>
#include <libhwsec/frontend/cryptohome/mock_frontend.h>
#include <libhwsec/frontend/pinweaver_manager/mock_frontend.h>
#include <libhwsec/frontend/recovery_crypto/mock_frontend.h>
#include <libstorage/platform/mock_platform.h>

#include "cryptohome/auth_blocks/auth_block.h"
#include "cryptohome/auth_blocks/auth_block_type.h"
#include "cryptohome/auth_blocks/cryptorecovery/service.h"
#include "cryptohome/auth_blocks/fp_service.h"
#include "cryptohome/auth_blocks/mock_biometrics_command_processor.h"
#include "cryptohome/auth_blocks/pin_weaver_auth_block.h"
#include "cryptohome/auth_factor/type.h"
#include "cryptohome/auth_factor/types/manager.h"
#include "cryptohome/challenge_credentials/mock_challenge_credentials_helper.h"
#include "cryptohome/crypto.h"
#include "cryptohome/fake_features.h"
#include "cryptohome/fake_platform.h"
#include "cryptohome/fingerprint_manager.h"
#include "cryptohome/flatbuffer_schemas/auth_block_state.h"
#include "cryptohome/key_objects.h"
#include "cryptohome/mock_cryptohome_keys_manager.h"
#include "cryptohome/mock_fingerprint_manager.h"
#include "cryptohome/mock_key_challenge_service.h"
#include "cryptohome/mock_key_challenge_service_factory.h"
#include "cryptohome/mock_signalling.h"
#include "cryptohome/signalling.h"
#include "cryptohome/user_secret_stash/manager.h"
#include "cryptohome/userdataauth_test_utils.h"
#include "cryptohome/username.h"

namespace cryptohome {
namespace {

using ::base::test::TestFuture;
using ::brillo::cryptohome::home::SanitizeUserName;
using ::cryptohome::error::CryptohomeCryptoError;
using ::hwsec::TPMErrorBase;
using ::hwsec_foundation::DeriveSecretsScrypt;
using ::hwsec_foundation::error::testing::IsOk;
using ::hwsec_foundation::error::testing::IsOkAndHolds;
using ::hwsec_foundation::error::testing::NotOk;
using ::hwsec_foundation::error::testing::ReturnError;
using ::hwsec_foundation::error::testing::ReturnOk;
using ::hwsec_foundation::error::testing::ReturnValue;
using ::hwsec_foundation::status::StatusChainOr;
using ::testing::_;
using ::testing::ByMove;
using ::testing::Eq;
using ::testing::Exactly;
using ::testing::IsNull;
using ::testing::Mock;
using ::testing::NiceMock;
using ::testing::NotNull;
using ::testing::Optional;
using ::testing::Return;
using ::testing::SaveArg;

constexpr const char* kKeyDelegateDBusService = "key-delegate-service";
constexpr int kWorkFactor = 16384;
constexpr int kBlockSize = 8;
constexpr int kParallelFactor = 1;

}  // namespace

class AuthBlockUtilityImplTest : public ::testing::Test {
 public:
  void SetUp() override {
    // Setup salt for brillo functions.
    keyset_management_ = std::make_unique<KeysetManagement>(
        &platform_, &crypto_, std::make_unique<VaultKeysetFactory>());
    system_salt_ =
        brillo::BlobFromString(*brillo::cryptohome::home::GetSystemSalt());
    ON_CALL(hwsec_, IsEnabled()).WillByDefault(ReturnValue(true));
    ON_CALL(hwsec_, IsReady()).WillByDefault(ReturnValue(true));
    ON_CALL(hwsec_, IsSealingSupported()).WillByDefault(ReturnValue(true));
    ON_CALL(hwsec_, GetPubkeyHash(_))
        .WillByDefault(ReturnValue(brillo::BlobFromString("public key hash")));
    ON_CALL(hwsec_pw_manager_, IsEnabled()).WillByDefault(ReturnValue(true));
  }

  // Helper function to construct a "standard" auth block utility impl using the
  // mocks built into this test fixture.
  void MakeAuthBlockUtilityImpl() {
    auth_block_utility_impl_ = std::make_unique<AuthBlockUtilityImpl>(
        keyset_management_.get(), &crypto_, &platform_, &features_.async,
        scrypt_thread_.task_runner.get(),
        AsyncInitPtr<ChallengeCredentialsHelper>(
            &challenge_credentials_helper_),
        &key_challenge_service_factory_,
        AsyncInitPtr<BiometricsAuthBlockService>(base::BindRepeating(
            &AuthBlockUtilityImplTest::GetBioService, base::Unretained(this))));
  }

 protected:
  BiometricsAuthBlockService* GetBioService() { return bio_service_.get(); }

  void SetupBiometricsService() {
    auto mock_processor =
        std::make_unique<NiceMock<MockBiometricsCommandProcessor>>();
    bio_processor_ = mock_processor.get();
    bio_service_ = std::make_unique<BiometricsAuthBlockService>(
        std::move(mock_processor), /*enroll_signal_sender=*/base::DoNothing(),
        /*auth_signal_sender=*/base::DoNothing());
  }

  base::span<const AuthBlockType> GetBlockTypes(
      AuthFactorType auth_factor_type) {
    return auth_factor_driver_manager_.GetDriver(auth_factor_type)
        .block_types();
  }

  const Username kUser{"Test User"};
  const ObfuscatedUsername kObfuscated{"ABCD1234"};

  base::test::SingleThreadTaskEnvironment task_environment_ = {
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  scoped_refptr<base::SequencedTaskRunner> task_runner_ =
      base::SequencedTaskRunner::GetCurrentDefault();
  TestScryptThread scrypt_thread_;

  libstorage::MockPlatform platform_{std::make_unique<FakePlatform>()};
  MockFingerprintManager fp_manager_;
  brillo::Blob system_salt_;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager_;
  NiceMock<hwsec::MockCryptohomeFrontend> hwsec_;
  NiceMock<hwsec::MockPinWeaverManagerFrontend> hwsec_pw_manager_;
  hwsec::Tpm2SimulatorFactoryForTest hwsec_factory_;
  std::unique_ptr<const hwsec::RecoveryCryptoFrontend>
      recovery_crypto_fake_backend_{hwsec_factory_.GetRecoveryCryptoFrontend()};
  Crypto crypto_{&hwsec_, &hwsec_pw_manager_, &cryptohome_keys_manager_,
                 recovery_crypto_fake_backend_.get()};
  UssStorage uss_storage_{&platform_};
  UssManager uss_manager_{uss_storage_};
  std::unique_ptr<KeysetManagement> keyset_management_;
  NiceMock<MockKeyChallengeServiceFactory> key_challenge_service_factory_;
  NiceMock<MockChallengeCredentialsHelper> challenge_credentials_helper_;
  NiceMock<MockSignalling> signalling_;
  CryptohomeRecoveryAuthBlockService cr_service_{
      &platform_, recovery_crypto_fake_backend_.get()};
  FingerprintAuthBlockService fp_service_{
      AsyncInitPtr<FingerprintManager>(&fp_manager_),
      AsyncInitPtr<SignallingInterface>(&signalling_)};
  std::unique_ptr<BiometricsAuthBlockService> bio_service_;
  NiceMock<MockBiometricsCommandProcessor>* bio_processor_;

  FakeFeaturesForTesting features_;
  AuthFactorDriverManager auth_factor_driver_manager_{
      &platform_,
      &crypto_,
      &uss_manager_,
      AsyncInitPtr<ChallengeCredentialsHelper>(&challenge_credentials_helper_),
      &key_challenge_service_factory_,
      &cr_service_,
      &fp_service_,
      AsyncInitPtr<BiometricsAuthBlockService>(base::BindRepeating(
          &AuthBlockUtilityImplTest::GetBioService, base::Unretained(this))),
      &features_.async};

  std::unique_ptr<AuthBlockUtilityImpl> auth_block_utility_impl_;
};

// Test that CreateKeyBlobsWithAuthBlock creates AuthBlockState and KeyBlobs
// with PinWeaverAuthBlock when the AuthBlock type is low entropy credential.
TEST_F(AuthBlockUtilityImplTest, CreatePinweaverAuthBlockTest) {
  // Setup mock expectations and test inputs for low entropy AuthBlock.
  AuthInput auth_input = {
      .user_input = brillo::SecureBlob(20, 'A'),
      .username = kUser,
      .obfuscated_username = kObfuscated,
      .reset_secret = brillo::SecureBlob(32, 'S'),
  };
  EXPECT_CALL(hwsec_, IsPinWeaverEnabled()).WillRepeatedly(ReturnValue(true));
  EXPECT_CALL(hwsec_pw_manager_, InsertCredential(_, _, _, _, _, _))
      .WillOnce(ReturnValue(/* ret_label*/ 0));
  crypto_.Init();
  MakeAuthBlockUtilityImpl();

  // Test
  TestFuture<CryptohomeStatus, std::unique_ptr<KeyBlobs>,
             std::unique_ptr<AuthBlockState>>
      create_result;
  auth_block_utility_impl_->CreateKeyBlobsWithAuthBlock(
      AuthBlockType::kPinWeaver, auth_input, {.metadata = PinMetadata()},
      create_result.GetCallback());

  // Verify
  ASSERT_THAT(create_result.Get<2>(), NotNull());
  auto* state =
      std::get_if<PinWeaverAuthBlockState>(&create_result.Get<2>()->state);
  ASSERT_THAT(state, NotNull());
  EXPECT_THAT(state->salt, Optional(_));
}

// Test that DeriveKeyBlobsWithAuthBlock derives KeyBlobs with
// PinWeaverAuthBlock type when the Authblock type is low entropy credential.
TEST_F(AuthBlockUtilityImplTest, DerivePinWeaverAuthBlock) {
  // Setup mock expectations and test inputs for low entropy AuthBlock.
  AuthInput auth_input = {
      .user_input = brillo::SecureBlob(20, 'C'),
      .username = kUser,
      .obfuscated_username = kObfuscated,
  };
  AuthFactorMetadata metadata = {.metadata = PinMetadata()};
  brillo::SecureBlob le_secret(32);
  brillo::Blob chaps_iv(16, 'F');
  brillo::Blob fek_iv(16, 'X');
  brillo::Blob salt(system_salt_);

  EXPECT_CALL(hwsec_, IsPinWeaverEnabled()).WillRepeatedly(ReturnValue(true));
  crypto_.Init();

  ASSERT_TRUE(DeriveSecretsScrypt(*auth_input.user_input, salt, {&le_secret}));

  ON_CALL(hwsec_pw_manager_, CheckCredential(_, _))
      .WillByDefault(
          ReturnValue(hwsec::PinWeaverManagerFrontend::CheckCredentialReply{}));
  EXPECT_CALL(hwsec_pw_manager_, CheckCredential(_, le_secret))
      .Times(Exactly(1));
  EXPECT_CALL(hwsec_pw_manager_, GetDelaySchedule(_))
      .WillOnce(Return(PinDelaySchedule()));

  MakeAuthBlockUtilityImpl();

  PinWeaverAuthBlockState pin_state = {
      .le_label = 0, .salt = salt, .chaps_iv = chaps_iv, .fek_iv = fek_iv};
  AuthBlockState auth_state = {.state = pin_state};

  // Test
  // No need to check for the KeyBlobs value, it is already being tested in
  // AuthBlock unittest.
  TestFuture<CryptohomeStatus, std::unique_ptr<KeyBlobs>,
             std::optional<AuthBlock::SuggestedAction>>
      derive_result;
  auth_block_utility_impl_->DeriveKeyBlobsWithAuthBlock(
      AuthBlockType::kPinWeaver, auth_input, metadata, auth_state,
      derive_result.GetCallback());
  EXPECT_THAT(derive_result.Get<0>(), IsOk());
}

// Test that CreateKeyBlobsWithAuthBlock creates AuthBlockState and KeyBlobs
// with TpmBoundToPcrAuthBlock when the AuthBlock type is
// AuthBlockType::kTpmBoundToPcr.
TEST_F(AuthBlockUtilityImplTest, CreateTpmBackedPcrBoundAuthBlock) {
  // Setup test inputs and the mock expectations..
  AuthInput auth_input = {
      .user_input = brillo::SecureBlob(20, 'A'),
      .username = kUser,
      .obfuscated_username = kObfuscated,
  };
  crypto_.Init();
  brillo::SecureBlob auth_value(256, 'a');
  EXPECT_CALL(hwsec_, GetAuthValue(_, _)).WillOnce(ReturnValue(auth_value));
  EXPECT_CALL(hwsec_, SealWithCurrentUser(_, auth_value, _)).Times(Exactly(2));
  ON_CALL(hwsec_, SealWithCurrentUser(_, _, _))
      .WillByDefault(ReturnValue(brillo::Blob()));

  MakeAuthBlockUtilityImpl();

  // Test
  TestFuture<CryptohomeStatus, std::unique_ptr<KeyBlobs>,
             std::unique_ptr<AuthBlockState>>
      create_result;
  auth_block_utility_impl_->CreateKeyBlobsWithAuthBlock(
      AuthBlockType::kTpmBoundToPcr, auth_input, {},
      create_result.GetCallback());

  // Verify
  ASSERT_THAT(create_result.Get<2>(), NotNull());
  auto* state =
      std::get_if<TpmBoundToPcrAuthBlockState>(&create_result.Get<2>()->state);
  ASSERT_THAT(state, NotNull());
  EXPECT_NE(create_result.Get<1>()->vkk_key, std::nullopt);
  EXPECT_NE(create_result.Get<1>()->vkk_iv, std::nullopt);
  EXPECT_NE(create_result.Get<1>()->chaps_iv, std::nullopt);
  EXPECT_THAT(state->salt, Optional(_));
}

// Test that CreateKeyBlobsWithAuthBlock with TpmBoundToPcr returns error (but
// doesn't crash) when the RSA key loader is unavailable.
TEST_F(AuthBlockUtilityImplTest,
       CreateTpmBackedPcrBoundAuthBlockErrorNoLoader) {
  // Setup
  AuthInput auth_input = {
      .user_input = brillo::SecureBlob(20, 'A'),
      .username = kUser,
      .obfuscated_username = kObfuscated,
  };
  EXPECT_CALL(cryptohome_keys_manager_, GetKeyLoader(CryptohomeKeyType::kRSA))
      .WillRepeatedly(Return(nullptr));
  MakeAuthBlockUtilityImpl();

  // Test
  TestFuture<CryptohomeStatus, std::unique_ptr<KeyBlobs>,
             std::unique_ptr<AuthBlockState>>
      create_result;
  auth_block_utility_impl_->CreateKeyBlobsWithAuthBlock(
      AuthBlockType::kTpmBoundToPcr, auth_input, {},
      create_result.GetCallback());

  // Verify
  EXPECT_THAT(create_result.Get<0>(), NotOk());
}

// Test that DeriveKeyBlobsWithAuthBlock derive KeyBlobs successfully with
// TpmBoundToPcrAuthBlock when the AuthBlock type is
// AuthBlockType::kTpmBoundToPcr.
TEST_F(AuthBlockUtilityImplTest, DeriveTpmBackedPcrBoundAuthBlock) {
  // Setup test inputs and the mock expectations.
  AuthInput auth_input = {
      .user_input = brillo::SecureBlob(20, 'A'),
      .username = kUser,
      .obfuscated_username = kObfuscated,
  };
  AuthFactorMetadata metadata = {.metadata = PasswordMetadata()};
  brillo::Blob tpm_key(20, 'B');
  brillo::Blob salt(system_salt_);
  crypto_.Init();

  // Make sure TpmAuthBlock calls DecryptTpmBoundToPcr in this case.
  EXPECT_CALL(hwsec_, PreloadSealedData(_)).WillOnce(ReturnValue(std::nullopt));
  EXPECT_CALL(hwsec_, GetAuthValue(_, _))
      .WillOnce(ReturnValue(brillo::SecureBlob()));
  EXPECT_CALL(hwsec_, UnsealWithCurrentUser(_, _, _))
      .WillOnce(ReturnValue(brillo::SecureBlob()));

  TpmBoundToPcrAuthBlockState tpm_state = {.scrypt_derived = true,
                                           .salt = salt,
                                           .tpm_key = tpm_key,
                                           .extended_tpm_key = tpm_key};
  AuthBlockState auth_state = {.state = tpm_state};

  // Test
  KeyBlobs out_key_blobs;
  MakeAuthBlockUtilityImpl();

  // Verify.
  TestFuture<CryptohomeStatus, std::unique_ptr<KeyBlobs>,
             std::optional<AuthBlock::SuggestedAction>>
      derive_result;
  auth_block_utility_impl_->DeriveKeyBlobsWithAuthBlock(
      AuthBlockType::kTpmBoundToPcr, auth_input, metadata, auth_state,
      derive_result.GetCallback());
  EXPECT_THAT(derive_result.Get<0>(), IsOk());
}

// Test that CreateKeyBlobsWithAuthBlock creates AuthBlockState and KeyBlobs
// with TpmNotBoundToPcrAuthBlock when the AuthBlock type is
// AuthBlockType::kTpmNotBoundToPcr.
TEST_F(AuthBlockUtilityImplTest, CreateTpmBackedNonPcrBoundAuthBlock) {
  // Setup test inputs and the mock expectations.
  AuthInput auth_input = {
      .user_input = brillo::SecureBlob(20, 'A'),
      .username = kUser,
      .obfuscated_username = kObfuscated,
  };
  crypto_.Init();
  brillo::SecureBlob aes_key;
  brillo::Blob encrypt_out(64, 'X');
  EXPECT_CALL(hwsec_, Encrypt(_, _)).WillOnce(ReturnValue(encrypt_out));
  MakeAuthBlockUtilityImpl();

  // Test
  TestFuture<CryptohomeStatus, std::unique_ptr<KeyBlobs>,
             std::unique_ptr<AuthBlockState>>
      create_result;
  auth_block_utility_impl_->CreateKeyBlobsWithAuthBlock(
      AuthBlockType::kTpmNotBoundToPcr, auth_input, {},
      create_result.GetCallback());

  // Verify
  ASSERT_THAT(create_result.Get<2>(), NotNull());
  auto* state = std::get_if<TpmNotBoundToPcrAuthBlockState>(
      &create_result.Get<2>()->state);
  ASSERT_THAT(state, NotNull());
  EXPECT_NE(create_result.Get<1>()->vkk_key, std::nullopt);
  EXPECT_NE(create_result.Get<1>()->vkk_iv, std::nullopt);
  EXPECT_NE(create_result.Get<1>()->chaps_iv, std::nullopt);
  EXPECT_THAT(state->salt, Optional(_));
}

// Test that CreateKeyBlobsWithAuthBlock with TpmNotBoundToPcr returns error
// (but doesn't crash) when the RSA key loader is unavailable.
TEST_F(AuthBlockUtilityImplTest,
       CreateTpmBackedNonPcrBoundAuthBlockErrorNoLoader) {
  // Setup
  AuthInput auth_input = {
      .user_input = brillo::SecureBlob(20, 'A'),
      .username = kUser,
      .obfuscated_username = kObfuscated,
  };
  EXPECT_CALL(cryptohome_keys_manager_, GetKeyLoader(CryptohomeKeyType::kRSA))
      .WillRepeatedly(Return(nullptr));
  MakeAuthBlockUtilityImpl();

  // Test
  TestFuture<CryptohomeStatus, std::unique_ptr<KeyBlobs>,
             std::unique_ptr<AuthBlockState>>
      create_result;
  auth_block_utility_impl_->CreateKeyBlobsWithAuthBlock(
      AuthBlockType::kTpmNotBoundToPcr, auth_input, {},
      create_result.GetCallback());

  // Verify
  EXPECT_THAT(create_result.Get<0>(), NotOk());
}

// Test that DeriveKeyBlobsWithAuthBlock derive KeyBlobs successfully with
// TpmNotBoundToPcrAuthBlock when the AuthBlock type is
// AuthBlockType::kTpmNotBoundToPcr.
TEST_F(AuthBlockUtilityImplTest, DeriveTpmBackedNonPcrBoundAuthBlock) {
  // Setup test inputs and the mock expectations.
  AuthInput auth_input = {
      .user_input = brillo::SecureBlob(20, 'A'),
      .username = kUser,
      .obfuscated_username = kObfuscated,
  };
  AuthFactorMetadata metadata = {.metadata = PasswordMetadata()};
  brillo::Blob tpm_key;
  brillo::Blob salt(system_salt_);
  brillo::SecureBlob aes_key(32);
  crypto_.Init();
  ASSERT_TRUE(DeriveSecretsScrypt(*auth_input.user_input, salt, {&aes_key}));

  brillo::Blob encrypt_out(64, 'X');
  EXPECT_TRUE(
      hwsec_foundation::ObscureRsaMessage(encrypt_out, aes_key, &tpm_key));

  EXPECT_CALL(hwsec_, Decrypt(_, encrypt_out))
      .WillOnce(ReturnValue(brillo::SecureBlob()));

  TpmNotBoundToPcrAuthBlockState tpm_state = {
      .scrypt_derived = true, .salt = salt, .tpm_key = tpm_key};
  AuthBlockState auth_state = {.state = tpm_state};

  // Test
  KeyBlobs out_key_blobs;
  MakeAuthBlockUtilityImpl();

  // Verify
  TestFuture<CryptohomeStatus, std::unique_ptr<KeyBlobs>,
             std::optional<AuthBlock::SuggestedAction>>
      derive_result;
  auth_block_utility_impl_->DeriveKeyBlobsWithAuthBlock(
      AuthBlockType::kTpmNotBoundToPcr, auth_input, metadata, auth_state,
      derive_result.GetCallback());
  EXPECT_THAT(derive_result.Get<0>(), IsOk());
}

// Test that CreateKeyBlobsWithAuthBlock creates AuthBlockState and KeyBlobs
// with TpmEccAuthBlock when the AuthBlock type is AuthBlockType::kTpmEcc.
TEST_F(AuthBlockUtilityImplTest, CreateTpmBackedEccAuthBlock) {
  // Setup test inputs and the mock expectations.
  AuthInput auth_input = {
      .user_input = brillo::SecureBlob(20, 'A'),
      .username = kUser,
      .obfuscated_username = kObfuscated,
  };
  crypto_.Init();
  brillo::SecureBlob auth_value(32, 'a');
  EXPECT_CALL(hwsec_, GetManufacturer()).WillOnce(ReturnValue(0x43524f53));
  EXPECT_CALL(hwsec_, GetAuthValue(_, _))
      .Times(Exactly(5))
      .WillRepeatedly(ReturnValue(auth_value));
  EXPECT_CALL(hwsec_, SealWithCurrentUser(_, auth_value, _))
      .WillOnce(ReturnValue(brillo::Blob()))
      .WillOnce(ReturnValue(brillo::Blob()));
  MakeAuthBlockUtilityImpl();

  // Test
  TestFuture<CryptohomeStatus, std::unique_ptr<KeyBlobs>,
             std::unique_ptr<AuthBlockState>>
      create_result;
  auth_block_utility_impl_->CreateKeyBlobsWithAuthBlock(
      AuthBlockType::kTpmEcc, auth_input, {}, create_result.GetCallback());

  // Verify
  ASSERT_THAT(create_result.Get<2>(), NotNull());
  auto* state =
      std::get_if<TpmEccAuthBlockState>(&create_result.Get<2>()->state);
  ASSERT_THAT(state, NotNull());
  EXPECT_NE(create_result.Get<1>()->vkk_key, std::nullopt);
  EXPECT_NE(create_result.Get<1>()->vkk_iv, std::nullopt);
  EXPECT_NE(create_result.Get<1>()->chaps_iv, std::nullopt);
  EXPECT_THAT(state->salt, Optional(_));
}

// Test that CreateKeyBlobsWithAuthBlock with TpmEcc returns error (but doesn't
// crash) when the ECC key loader is unavailable.
TEST_F(AuthBlockUtilityImplTest, CreateTpmBackedEccAuthBlockErrorNoLoader) {
  // Setup
  AuthInput auth_input = {
      .user_input = brillo::SecureBlob(20, 'A'),
      .username = kUser,
      .obfuscated_username = kObfuscated,
  };
  EXPECT_CALL(cryptohome_keys_manager_, GetKeyLoader(CryptohomeKeyType::kECC))
      .WillRepeatedly(Return(nullptr));
  MakeAuthBlockUtilityImpl();

  // Test
  TestFuture<CryptohomeStatus, std::unique_ptr<KeyBlobs>,
             std::unique_ptr<AuthBlockState>>
      create_result;
  auth_block_utility_impl_->CreateKeyBlobsWithAuthBlock(
      AuthBlockType::kTpmEcc, auth_input, {}, create_result.GetCallback());

  // Verify
  EXPECT_THAT(create_result.Get<0>(), NotOk());
}

// Test that DeriveKeyBlobsWithAuthBlock derives KeyBlobs successfully with
// TpmEccAuthBlock when the AuthBlock type is
// AuthBlockType::kTpmEcc.
TEST_F(AuthBlockUtilityImplTest, DeriveTpmBackedEccAuthBlock) {
  // Setup test inputs and the mock expectations.
  AuthInput auth_input = {
      .user_input = brillo::SecureBlob(20, 'A'),
      .username = kUser,
      .obfuscated_username = kObfuscated,
  };
  AuthFactorMetadata metadata = {.metadata = PasswordMetadata()};
  brillo::Blob salt(system_salt_);
  brillo::Blob fake_hash = brillo::BlobFromString("public key hash");
  crypto_.Init();

  EXPECT_CALL(hwsec_, PreloadSealedData(_)).WillOnce(ReturnValue(std::nullopt));
  EXPECT_CALL(hwsec_, GetAuthValue(_, _))
      .Times(Exactly(5))
      .WillRepeatedly(ReturnValue(brillo::SecureBlob()));

  brillo::Blob fake_hvkkm(32, 'D');
  EXPECT_CALL(hwsec_, UnsealWithCurrentUser(_, _, _))
      .WillOnce(ReturnValue(fake_hvkkm));

  TpmEccAuthBlockState tpm_state;
  tpm_state.salt = salt;
  tpm_state.vkk_iv = brillo::Blob(32, 'E');
  tpm_state.sealed_hvkkm = brillo::Blob(32, 'F');
  tpm_state.extended_sealed_hvkkm = brillo::Blob(32, 'G');
  tpm_state.auth_value_rounds = 5;
  tpm_state.tpm_public_key_hash = fake_hash;
  AuthBlockState auth_state = {.state = tpm_state};

  // Test
  KeyBlobs out_key_blobs;
  MakeAuthBlockUtilityImpl();

  // Verify
  TestFuture<CryptohomeStatus, std::unique_ptr<KeyBlobs>,
             std::optional<AuthBlock::SuggestedAction>>
      derive_result;
  auth_block_utility_impl_->DeriveKeyBlobsWithAuthBlock(
      AuthBlockType::kTpmEcc, auth_input, metadata, auth_state,
      derive_result.GetCallback());
  EXPECT_THAT(derive_result.Get<0>(), IsOk());
}

// Test that CreateKeyBlobsWithAuthBlock creates AuthBlockState with
// ScryptAuthBlock when the AuthBlock type is
// AuthBlockType::kScrypt.
TEST_F(AuthBlockUtilityImplTest, CreateScryptAuthBlockTest) {
  // Setup mock expectations and test inputs for low entropy AuthBlock.
  AuthInput auth_input = {
      .user_input = brillo::SecureBlob(20, 'A'),
      .username = kUser,
      .obfuscated_username = kObfuscated,
  };
  MakeAuthBlockUtilityImpl();

  // Test
  TestFuture<CryptohomeStatus, std::unique_ptr<KeyBlobs>,
             std::unique_ptr<AuthBlockState>>
      create_result;
  auth_block_utility_impl_->CreateKeyBlobsWithAuthBlock(
      AuthBlockType::kScrypt, auth_input, {}, create_result.GetCallback());

  // Verify
  ASSERT_THAT(create_result.Get<2>(), NotNull());
  auto* state =
      std::get_if<ScryptAuthBlockState>(&create_result.Get<2>()->state);
  ASSERT_THAT(state, NotNull());
  EXPECT_THAT(state->salt, Optional(_));
}

// Test that DeriveKeyBlobsWithAuthBlock derives AuthBlocks with
// ScryptAuthBlock when the AuthBlock type is
// AuthBlockType::kScrypt.
TEST_F(AuthBlockUtilityImplTest, DeriveScryptAuthBlock) {
  // Setup test inputs and the mock expectations.
  brillo::Blob wrapped_keyset = {
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

  brillo::Blob wrapped_chaps_key = {
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

  brillo::Blob wrapped_reset_seed = {
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

  AuthInput auth_input = {
      .user_input = passkey,
      .username = kUser,
      .obfuscated_username = kObfuscated,
  };
  AuthFactorMetadata metadata = {.metadata = PasswordMetadata()};
  ScryptAuthBlockState scrypt_state = {
      .salt = brillo::BlobFromString("salt"),
      .chaps_salt = brillo::BlobFromString("chaps_salt"),
      .reset_seed_salt = brillo::BlobFromString("reset_seed_salt"),
      .work_factor = kWorkFactor,
      .block_size = kBlockSize,
      .parallel_factor = kParallelFactor,
  };
  AuthBlockState auth_state = {.state = scrypt_state};

  MakeAuthBlockUtilityImpl();

  // Test
  // Verify
  TestFuture<CryptohomeStatus, std::unique_ptr<KeyBlobs>,
             std::optional<AuthBlock::SuggestedAction>>
      derive_result;
  auth_block_utility_impl_->DeriveKeyBlobsWithAuthBlock(
      AuthBlockType::kScrypt, auth_input, metadata, auth_state,
      derive_result.GetCallback());
  EXPECT_THAT(derive_result.Get<0>(), IsOk());
}

// Test that DeriveKeyBlobsWithAuthBlock derives AuthBlocks with
// DoubleWrappedCompatAuthBlock when the AuthBlock type is
// AuthBlockType::kDoubleWrappedCompat.
TEST_F(AuthBlockUtilityImplTest, DeriveDoubleWrappedAuthBlock) {
  // Setup test inputs and the mock expectations.
  crypto_.Init();
  brillo::Blob wrapped_keyset = {
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

  brillo::Blob wrapped_chaps_key = {
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

  brillo::Blob wrapped_reset_seed = {
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

  AuthInput auth_input = {
      .user_input = passkey,
      .username = kUser,
      .obfuscated_username = kObfuscated,
  };
  AuthFactorMetadata metadata = {.metadata = PasswordMetadata()};
  ScryptAuthBlockState scrypt_state = {
      .salt = brillo::BlobFromString("salt"),
      .chaps_salt = brillo::BlobFromString("chaps_salt"),
      .reset_seed_salt = brillo::BlobFromString("reset_seed_salt"),
      .work_factor = kWorkFactor,
      .block_size = kBlockSize,
      .parallel_factor = kParallelFactor,
  };
  TpmNotBoundToPcrAuthBlockState tpm_state = {.scrypt_derived = false,
                                              .salt = system_salt_,
                                              .tpm_key = brillo::Blob(20, 'A')};
  DoubleWrappedCompatAuthBlockState double_wrapped_state = {
      .scrypt_state = scrypt_state, .tpm_state = tpm_state};
  AuthBlockState auth_state = {.state = double_wrapped_state};

  // Test
  MakeAuthBlockUtilityImpl();

  // Verify
  TestFuture<CryptohomeStatus, std::unique_ptr<KeyBlobs>,
             std::optional<AuthBlock::SuggestedAction>>
      derive_result;
  auth_block_utility_impl_->DeriveKeyBlobsWithAuthBlock(
      AuthBlockType::kDoubleWrappedCompat, auth_input, metadata, auth_state,
      derive_result.GetCallback());
  EXPECT_THAT(derive_result.Get<0>(), IsOk());
}

// Test that CreateKeyBlobsWithAuthBlock creates AuthBlockState
// and KeyBlobs, internally using a AsyncChallengeCredentialAuthBlock for
// accessing the key material.
TEST_F(AuthBlockUtilityImplTest, ChallengeCredentialCreate) {
  brillo::SecureBlob passkey("passkey");
  crypto_.Init();

  EXPECT_CALL(challenge_credentials_helper_, GenerateNew(kUser, _, _, _, _))
      .WillOnce([&](auto&&, auto public_key_info, auto&&, auto&&,
                    auto&& callback) {
        auto info = std::make_unique<SerializedSignatureChallengeInfo>();
        info->public_key_spki_der = public_key_info.public_key_spki_der;
        info->salt_signature_algorithm = public_key_info.signature_algorithm[0];
        auto passkey = std::make_unique<brillo::SecureBlob>("passkey");
        std::move(callback).Run(
            ChallengeCredentialsHelper::GenerateNewOrDecryptResult(
                std::move(info), std::move(passkey)));
      });
  EXPECT_CALL(key_challenge_service_factory_, New(kKeyDelegateDBusService))
      .WillOnce([](const std::string& bus_name) {
        return std::make_unique<MockKeyChallengeService>();
      });
  MakeAuthBlockUtilityImpl();

  AuthBlock::CreateCallback create_callback = base::BindLambdaForTesting(
      [&](CryptohomeStatus error, std::unique_ptr<KeyBlobs> blobs,
          std::unique_ptr<AuthBlockState> auth_state) {
        // Evaluate results of KeyBlobs and AuthBlockState returned by callback.
        EXPECT_TRUE(error.ok());

        // Because the salt is generated randomly inside the auth block, this
        // test cannot check the exact values returned. The salt() could be
        // passed through in some test specific harness, but the underlying
        // scrypt code is tested in so many other places, it's unnecessary.
        auto& tpm_state =
            std::get<ChallengeCredentialAuthBlockState>(auth_state->state);

        EXPECT_FALSE(blobs->vkk_key->empty());
        EXPECT_FALSE(tpm_state.scrypt_state.salt->empty());

        EXPECT_FALSE(blobs->scrypt_chaps_key->empty());
        EXPECT_FALSE(tpm_state.scrypt_state.chaps_salt->empty());

        EXPECT_FALSE(blobs->scrypt_reset_seed_key->empty());
        EXPECT_FALSE(tpm_state.scrypt_state.reset_seed_salt->empty());

        ASSERT_TRUE(std::holds_alternative<ChallengeCredentialAuthBlockState>(
            auth_state->state));

        AuthInput auth_input{
            .challenge_credential_auth_input =
                ChallengeCredentialAuthInput{
                    .challenge_signature_algorithms =
                        {SerializedChallengeSignatureAlgorithm::
                             kRsassaPkcs1V15Sha256},
                },
        };
        const SmartCardMetadata metadata = {
            .public_key_spki_der =
                brillo::BlobFromString("public_key_spki_der")};

        ASSERT_TRUE(tpm_state.keyset_challenge_info.has_value());
        EXPECT_EQ(tpm_state.keyset_challenge_info.value().public_key_spki_der,
                  metadata.public_key_spki_der);
        EXPECT_EQ(
            tpm_state.keyset_challenge_info.value().salt_signature_algorithm,
            auth_input.challenge_credential_auth_input.value()
                .challenge_signature_algorithms[0]);
      });
  AuthInput auth_input;
  auth_input.obfuscated_username = kObfuscated;
  auth_input.username = kUser, auth_input.locked_to_single_user = false;
  auth_input.challenge_credential_auth_input = ChallengeCredentialAuthInput{
      .challenge_signature_algorithms =
          {SerializedChallengeSignatureAlgorithm::kRsassaPkcs1V15Sha256},
      .dbus_service_name = kKeyDelegateDBusService};
  const AuthFactorMetadata auth_factor_metadata = {
      .metadata =
          SmartCardMetadata{.public_key_spki_der =
                                brillo::BlobFromString("public_key_spki_der")}};

  // Test.
  auth_block_utility_impl_->CreateKeyBlobsWithAuthBlock(
      AuthBlockType::kChallengeCredential, auth_input, auth_factor_metadata,
      std::move(create_callback));
}

// The ChallengeCredentialAuthBlock::Derive should work correctly.
TEST_F(AuthBlockUtilityImplTest, ChallengeCredentialDerive) {
  brillo::SecureBlob passkey("passkey");
  crypto_.Init();

  AuthBlockState auth_state{
      .state =
          ChallengeCredentialAuthBlockState{
              .scrypt_state =
                  ScryptAuthBlockState{
                      .salt = brillo::BlobFromString("salt"),
                      .chaps_salt = brillo::BlobFromString("chaps_salt"),
                      .reset_seed_salt =
                          brillo::BlobFromString("reset_seed_salt"),
                      .work_factor = kWorkFactor,
                      .block_size = kBlockSize,
                      .parallel_factor = kParallelFactor,
                  },
              .keyset_challenge_info =
                  SerializedSignatureChallengeInfo{
                      .public_key_spki_der =
                          brillo::BlobFromString("public_key_spki_der"),
                      .salt_signature_algorithm =
                          SerializedChallengeSignatureAlgorithm::
                              kRsassaPkcs1V15Sha256,
                  },
          },
  };

  brillo::SecureBlob scrypt_passkey = {
      0x31, 0x35, 0x64, 0x64, 0x38, 0x38, 0x66, 0x36, 0x35, 0x31, 0x30,
      0x65, 0x30, 0x64, 0x35, 0x64, 0x35, 0x35, 0x36, 0x35, 0x35, 0x35,
      0x38, 0x36, 0x31, 0x32, 0x62, 0x37, 0x39, 0x36, 0x30, 0x65};

  brillo::SecureBlob derived_key = {
      0x67, 0xeb, 0xcd, 0x84, 0x49, 0x5e, 0xa2, 0xf3, 0xb1, 0xe6, 0xe7,
      0x5b, 0x13, 0xb9, 0x16, 0x2f, 0x5a, 0x39, 0xc8, 0xfe, 0x6a, 0x60,
      0xd4, 0x7a, 0xd8, 0x2b, 0x44, 0xc4, 0x45, 0x53, 0x1a, 0x85, 0x4a,
      0x97, 0x9f, 0x2d, 0x06, 0xf5, 0xd0, 0xd3, 0xa6, 0xe7, 0xac, 0x9b,
      0x02, 0xaf, 0x3c, 0x08, 0xce, 0x43, 0x46, 0x32, 0x6d, 0xd7, 0x2b,
      0xe9, 0xdf, 0x8b, 0x38, 0x0e, 0x60, 0x3d, 0x64, 0x12};

  brillo::SecureBlob derived_chaps_key = {
      0x7a, 0xc3, 0x70, 0x54, 0x4d, 0x04, 0x4c, 0xa6, 0x48, 0xcc, 0x4d,
      0xcf, 0x94, 0x13, 0xa7, 0x97, 0x28, 0x80, 0x9f, 0xec, 0xa0, 0xaf,
      0x2d, 0x3c, 0xef, 0xf0, 0x34, 0xd6, 0xbd, 0x02, 0x45, 0x1e, 0x3d,
      0xe1, 0xc2, 0x42, 0xd8, 0x40, 0x75, 0x85, 0x15, 0x87, 0xaf, 0x29,
      0x2c, 0x44, 0xbc, 0x77, 0x86, 0x87, 0xd2, 0x0b, 0xea, 0xba, 0x51,
      0x8d, 0xc4, 0x3a, 0xf8, 0x05, 0xb6, 0x20, 0x5d, 0xfd};

  brillo::SecureBlob derived_reset_seed_key = {
      0xd4, 0x78, 0x3b, 0xfb, 0x81, 0xfe, 0xb3, 0x84, 0x23, 0x06, 0x18,
      0xc0, 0x30, 0x1c, 0x40, 0xcb, 0x71, 0x04, 0x46, 0xeb, 0x91, 0x9e,
      0xa2, 0x7b, 0xd7, 0xcf, 0xcb, 0x5e, 0x67, 0xd3, 0x5a, 0x07, 0x7c,
      0x5f, 0xc2, 0x92, 0x3f, 0x98, 0x32, 0x75, 0x80, 0xe8, 0xed, 0xda,
      0x2c, 0x1e, 0x41, 0x1c, 0xd2, 0x07, 0x48, 0x39, 0x2a, 0xfd, 0x6c,
      0xd6, 0x6f, 0x1c, 0x8e, 0xca, 0x00, 0x79, 0x91, 0x52};

  MakeAuthBlockUtilityImpl();
  EXPECT_CALL(key_challenge_service_factory_, New(kKeyDelegateDBusService))
      .WillOnce([](const std::string& bus_name) {
        return std::make_unique<MockKeyChallengeService>();
      });
  EXPECT_CALL(challenge_credentials_helper_, Decrypt(kUser, _, _, _, _))
      .WillOnce([&](auto&&, auto&&, auto&&, auto&&, auto&& callback) {
        auto passkey = std::make_unique<brillo::SecureBlob>(scrypt_passkey);
        std::move(callback).Run(
            ChallengeCredentialsHelper::GenerateNewOrDecryptResult(
                nullptr, std::move(passkey)));
      });
  // Test.
  AuthBlock::DeriveCallback derive_callback = base::BindLambdaForTesting(
      [&](CryptohomeStatus error, std::unique_ptr<KeyBlobs> blobs,
          std::optional<AuthBlock::SuggestedAction> suggested_action) {
        ASSERT_TRUE(error.ok());
        EXPECT_EQ(derived_key, blobs->vkk_key);
        EXPECT_EQ(derived_chaps_key, blobs->scrypt_chaps_key);
        EXPECT_EQ(derived_reset_seed_key, blobs->scrypt_reset_seed_key);
        EXPECT_EQ(suggested_action, std::nullopt);
      });

  AuthInput auth_input = {
      passkey,
      /*locked_to_single_user=*/std::nullopt, .username = kUser,
      .challenge_credential_auth_input = ChallengeCredentialAuthInput{
          .challenge_signature_algorithms =
              {SerializedChallengeSignatureAlgorithm::kRsassaPkcs1V15Sha256},
          .dbus_service_name = kKeyDelegateDBusService}};
  AuthFactorMetadata metadata = {.metadata = SmartCardMetadata()};
  auth_block_utility_impl_->DeriveKeyBlobsWithAuthBlock(
      AuthBlockType::kChallengeCredential, auth_input, metadata, auth_state,
      std::move(derive_callback));
}

TEST_F(AuthBlockUtilityImplTest, SelectAuthBlockTypeForCreationNoTPM) {
  crypto_.Init();
  MakeAuthBlockUtilityImpl();

  // Setup: no TPM.
  ON_CALL(hwsec_, IsEnabled()).WillByDefault(ReturnValue(false));
  ON_CALL(hwsec_, IsReady()).WillByDefault(ReturnValue(false));

  // Password and Kiosk auth factor maps to Scrypt Auth Block.
  if (USE_TPM_INSECURE_FALLBACK) {
    EXPECT_THAT(auth_block_utility_impl_->SelectAuthBlockTypeForCreation(
                    GetBlockTypes(AuthFactorType::kPassword)),
                IsOkAndHolds(AuthBlockType::kScrypt));
    EXPECT_THAT(auth_block_utility_impl_->SelectAuthBlockTypeForCreation(
                    GetBlockTypes(AuthFactorType::kKiosk)),
                IsOkAndHolds(AuthBlockType::kScrypt));
  } else {
    EXPECT_THAT(auth_block_utility_impl_->SelectAuthBlockTypeForCreation(
                    GetBlockTypes(AuthFactorType::kPassword)),
                NotOk());
    EXPECT_THAT(auth_block_utility_impl_->SelectAuthBlockTypeForCreation(
                    GetBlockTypes(AuthFactorType::kKiosk)),
                NotOk());
  }

  // Auth factor that requires tpm to function will fail to get
  // the auth block.
  EXPECT_THAT(auth_block_utility_impl_->SelectAuthBlockTypeForCreation(
                  GetBlockTypes(AuthFactorType::kPin)),
              NotOk());
  EXPECT_THAT(auth_block_utility_impl_->SelectAuthBlockTypeForCreation(
                  GetBlockTypes(AuthFactorType::kSmartCard)),
              NotOk());
  EXPECT_THAT(auth_block_utility_impl_->SelectAuthBlockTypeForCreation(
                  GetBlockTypes(AuthFactorType::kFingerprint)),
              NotOk());

  // legacy fingerprint never maps to any auth block.
  EXPECT_THAT(auth_block_utility_impl_->SelectAuthBlockTypeForCreation(
                  GetBlockTypes(AuthFactorType::kLegacyFingerprint)),
              NotOk());

  EXPECT_THAT(auth_block_utility_impl_->SelectAuthBlockTypeForCreation(
                  GetBlockTypes(AuthFactorType::kUnspecified)),
              NotOk());
}

TEST_F(AuthBlockUtilityImplTest, SelectAuthBlockTypeForCreationWithTpm20) {
  crypto_.Init();
  MakeAuthBlockUtilityImpl();

  // Setup: TPM with PinWeaver and ECC support.
  ON_CALL(hwsec_, IsEnabled()).WillByDefault(ReturnValue(true));
  ON_CALL(hwsec_, IsReady()).WillByDefault(ReturnValue(true));
  ON_CALL(hwsec_, IsSealingSupported()).WillByDefault(ReturnValue(true));
  ON_CALL(hwsec_, IsPinWeaverEnabled()).WillByDefault(ReturnValue(true));
  EXPECT_CALL(cryptohome_keys_manager_, GetKeyLoader(CryptohomeKeyType::kECC))
      .WillRepeatedly(
          Return(cryptohome_keys_manager_.get_mock_cryptohome_key_loader()));

  // Password and Kiosk auth factor maps to TpmEcc Auth Block.
  EXPECT_THAT(auth_block_utility_impl_->SelectAuthBlockTypeForCreation(
                  GetBlockTypes(AuthFactorType::kPassword)),
              IsOkAndHolds(AuthBlockType::kTpmEcc));
  EXPECT_THAT(auth_block_utility_impl_->SelectAuthBlockTypeForCreation(
                  GetBlockTypes(AuthFactorType::kKiosk)),
              IsOkAndHolds(AuthBlockType::kTpmEcc));

  // Other Tpm related auth block works as expected.
  EXPECT_THAT(auth_block_utility_impl_->SelectAuthBlockTypeForCreation(
                  GetBlockTypes(AuthFactorType::kPin)),
              IsOkAndHolds(AuthBlockType::kPinWeaver));
  EXPECT_THAT(auth_block_utility_impl_->SelectAuthBlockTypeForCreation(
                  GetBlockTypes(AuthFactorType::kSmartCard)),
              IsOkAndHolds(AuthBlockType::kChallengeCredential));

  // Fingerprint auth block needs additional biod service setup to gain
  // support.
  EXPECT_THAT(auth_block_utility_impl_->SelectAuthBlockTypeForCreation(
                  GetBlockTypes(AuthFactorType::kFingerprint)),
              NotOk());

  // legacy fingerprint never maps to any auth block.
  EXPECT_THAT(auth_block_utility_impl_->SelectAuthBlockTypeForCreation(
                  GetBlockTypes(AuthFactorType::kLegacyFingerprint)),
              NotOk());

  EXPECT_THAT(auth_block_utility_impl_->SelectAuthBlockTypeForCreation(
                  GetBlockTypes(AuthFactorType::kUnspecified)),
              NotOk());
}

TEST_F(AuthBlockUtilityImplTest, SelectAuthBlockTypeForCreationWithTpm20NoEcc) {
  crypto_.Init();
  MakeAuthBlockUtilityImpl();

  // Setup: TPM with PinWeaver but without ECC support.
  ON_CALL(hwsec_, IsEnabled()).WillByDefault(ReturnValue(true));
  ON_CALL(hwsec_, IsReady()).WillByDefault(ReturnValue(true));
  ON_CALL(hwsec_, IsSealingSupported()).WillByDefault(ReturnValue(true));
  ON_CALL(hwsec_, IsPinWeaverEnabled()).WillByDefault(ReturnValue(true));
  EXPECT_CALL(cryptohome_keys_manager_, GetKeyLoader(CryptohomeKeyType::kECC))
      .WillRepeatedly(Return(nullptr));
  EXPECT_CALL(cryptohome_keys_manager_, GetKeyLoader(CryptohomeKeyType::kRSA))
      .WillRepeatedly(
          Return(cryptohome_keys_manager_.get_mock_cryptohome_key_loader()));

  // Password and Kiosk auth factor maps to TpmBoundToPcr Auth Block.
  EXPECT_THAT(auth_block_utility_impl_->SelectAuthBlockTypeForCreation(
                  GetBlockTypes(AuthFactorType::kPassword)),
              IsOkAndHolds(AuthBlockType::kTpmBoundToPcr));
  EXPECT_THAT(auth_block_utility_impl_->SelectAuthBlockTypeForCreation(
                  GetBlockTypes(AuthFactorType::kKiosk)),
              IsOkAndHolds(AuthBlockType::kTpmBoundToPcr));

  // Other TPM related auth block works as expected.
  EXPECT_THAT(auth_block_utility_impl_->SelectAuthBlockTypeForCreation(
                  GetBlockTypes(AuthFactorType::kPin)),
              IsOkAndHolds(AuthBlockType::kPinWeaver));
  EXPECT_THAT(auth_block_utility_impl_->SelectAuthBlockTypeForCreation(
                  GetBlockTypes(AuthFactorType::kSmartCard)),
              IsOkAndHolds(AuthBlockType::kChallengeCredential));

  // Fingerprint auth block needs additional biod service setup to gain
  // support.
  EXPECT_THAT(auth_block_utility_impl_->SelectAuthBlockTypeForCreation(
                  GetBlockTypes(AuthFactorType::kFingerprint)),
              NotOk());

  // legacy fingerprint never maps to any auth block.
  EXPECT_THAT(auth_block_utility_impl_->SelectAuthBlockTypeForCreation(
                  GetBlockTypes(AuthFactorType::kLegacyFingerprint)),
              NotOk());

  EXPECT_THAT(auth_block_utility_impl_->SelectAuthBlockTypeForCreation(
                  GetBlockTypes(AuthFactorType::kUnspecified)),
              NotOk());
}

TEST_F(AuthBlockUtilityImplTest, SelectAuthBlockTypeForCreationWithTpm11) {
  crypto_.Init();
  MakeAuthBlockUtilityImpl();

  // Setup: TPM with PinWeaver but without ECC and sealing support.
  // In other words, this is a TPM 1.1 environment.
  ON_CALL(hwsec_, IsEnabled()).WillByDefault(ReturnValue(true));
  ON_CALL(hwsec_, IsReady()).WillByDefault(ReturnValue(true));
  ON_CALL(hwsec_, IsSealingSupported()).WillByDefault(ReturnValue(false));
  ON_CALL(hwsec_, IsPinWeaverEnabled()).WillByDefault(ReturnValue(true));
  EXPECT_CALL(cryptohome_keys_manager_, GetKeyLoader(CryptohomeKeyType::kECC))
      .WillRepeatedly(nullptr);
  EXPECT_CALL(cryptohome_keys_manager_, GetKeyLoader(CryptohomeKeyType::kRSA))
      .WillRepeatedly(
          Return(cryptohome_keys_manager_.get_mock_cryptohome_key_loader()));

  // Password and Kiosk auth factor maps to TpmBoundToPcr Auth Block.
  EXPECT_THAT(auth_block_utility_impl_->SelectAuthBlockTypeForCreation(
                  GetBlockTypes(AuthFactorType::kPassword)),
              IsOkAndHolds(AuthBlockType::kTpmNotBoundToPcr));
  EXPECT_THAT(auth_block_utility_impl_->SelectAuthBlockTypeForCreation(
                  GetBlockTypes(AuthFactorType::kKiosk)),
              IsOkAndHolds(AuthBlockType::kTpmNotBoundToPcr));

  // Other tpm related auth block works as expected.
  EXPECT_THAT(auth_block_utility_impl_->SelectAuthBlockTypeForCreation(
                  GetBlockTypes(AuthFactorType::kPin)),
              IsOkAndHolds(AuthBlockType::kPinWeaver));
  EXPECT_THAT(auth_block_utility_impl_->SelectAuthBlockTypeForCreation(
                  GetBlockTypes(AuthFactorType::kSmartCard)),
              IsOkAndHolds(AuthBlockType::kChallengeCredential));

  // Fingerprint auth block needs additional biod service setup to gain
  // support.
  EXPECT_THAT(auth_block_utility_impl_->SelectAuthBlockTypeForCreation(
                  GetBlockTypes(AuthFactorType::kFingerprint)),
              NotOk());

  // legacy fingerprint never maps to any auth block.
  EXPECT_THAT(auth_block_utility_impl_->SelectAuthBlockTypeForCreation(
                  GetBlockTypes(AuthFactorType::kLegacyFingerprint)),
              NotOk());

  EXPECT_THAT(auth_block_utility_impl_->SelectAuthBlockTypeForCreation(
                  GetBlockTypes(AuthFactorType::kUnspecified)),
              NotOk());
}

TEST_F(AuthBlockUtilityImplTest, SelectAuthBlockTypeForCreationFingerprint) {
  crypto_.Init();
  MakeAuthBlockUtilityImpl();
  ON_CALL(hwsec_, IsEnabled()).WillByDefault(ReturnValue(true));
  ON_CALL(hwsec_, IsReady()).WillByDefault(ReturnValue(true));
  ON_CALL(hwsec_, IsBiometricsPinWeaverEnabled())
      .WillByDefault(ReturnValue(true));

  // Should fail before the bid service is ready.
  EXPECT_THAT(auth_block_utility_impl_->SelectAuthBlockTypeForCreation(
                  GetBlockTypes(AuthFactorType::kFingerprint)),
              NotOk());

  // Should succeed after setup the biod service
  SetupBiometricsService();
  EXPECT_CALL(*bio_processor_, IsReady).WillRepeatedly(Return(true));

  EXPECT_THAT(auth_block_utility_impl_->SelectAuthBlockTypeForCreation(
                  GetBlockTypes(AuthFactorType::kFingerprint)),
              IsOkAndHolds(AuthBlockType::kFingerprint));
}

// Test `GetAuthBlockWithType()` with the `kPinWeaver` type succeeds.
TEST_F(AuthBlockUtilityImplTest, GetAuthBlockWithTypeTpmPinWeaver) {
  // Setup.
  EXPECT_CALL(hwsec_, IsPinWeaverEnabled()).WillRepeatedly(ReturnValue(true));
  crypto_.Init();
  MakeAuthBlockUtilityImpl();
  AuthInput auth_input{
      .user_input = brillo::SecureBlob("fake-passkey"),
      .username = kUser,
      .obfuscated_username = SanitizeUserName(kUser),
      .reset_secret = brillo::SecureBlob("fake-reset-secret"),
  };

  // Test.
  CryptoStatusOr<std::unique_ptr<AuthBlock>> auth_block =
      auth_block_utility_impl_->GetAuthBlockWithType(AuthBlockType::kPinWeaver,
                                                     auth_input);

  // Verify.
  ASSERT_THAT(auth_block, IsOk());
  EXPECT_NE(auth_block.value(), nullptr);
}

// Test `GetAuthBlockWithType()` with the `kPinWeaver` type fails when
// there's no pinweaver manager.
TEST_F(AuthBlockUtilityImplTest,
       GetAuthBlockWithTypeTpmPinWeaverFailNoManager) {
  // Setup. Set the PinWeaver hwsec backend to disabled; this will lead to
  // having no pinweaver manager being created.
  EXPECT_CALL(hwsec_, IsPinWeaverEnabled()).WillRepeatedly(ReturnValue(false));
  EXPECT_CALL(hwsec_pw_manager_, IsEnabled())
      .WillRepeatedly(ReturnValue(false));
  crypto_.Init();
  MakeAuthBlockUtilityImpl();
  AuthInput auth_input{
      .user_input = brillo::SecureBlob("fake-passkey"),
      .username = kUser,
      .obfuscated_username = SanitizeUserName(kUser),
      .reset_secret = brillo::SecureBlob("fake-reset-secret"),
  };

  // Test.
  CryptoStatusOr<std::unique_ptr<AuthBlock>> auth_block =
      auth_block_utility_impl_->GetAuthBlockWithType(AuthBlockType::kPinWeaver,
                                                     auth_input);

  // Verify.
  EXPECT_THAT(auth_block, NotOk());
}

TEST_F(AuthBlockUtilityImplTest, GetAuthBlockWithTypeChallengeCredential) {
  crypto_.Init();

  MakeAuthBlockUtilityImpl();
  EXPECT_CALL(key_challenge_service_factory_, New(kKeyDelegateDBusService))
      .WillOnce([](const std::string& bus_name) {
        return std::make_unique<MockKeyChallengeService>();
      });

  AuthInput auth_input{
      .username = kUser,
      .challenge_credential_auth_input =
          ChallengeCredentialAuthInput{
              .challenge_signature_algorithms =
                  {SerializedChallengeSignatureAlgorithm::
                       kRsassaPkcs1V15Sha256},
              .dbus_service_name = kKeyDelegateDBusService},
  };
  // Test. All fields are valid to get an ChallengeCredentialAuthBlock.
  CryptoStatusOr<std::unique_ptr<AuthBlock>> auth_block =
      auth_block_utility_impl_->GetAuthBlockWithType(
          AuthBlockType::kChallengeCredential, auth_input);
  EXPECT_TRUE(auth_block.ok());
  EXPECT_NE(auth_block.value(), nullptr);
}

TEST_F(AuthBlockUtilityImplTest, GetAuthBlockWithTypeChallengeCredentialFail) {
  crypto_.Init();
  // Test. No valid dbus_service_name or username.
  MakeAuthBlockUtilityImpl();

  AuthInput auth_input;
  CryptoStatusOr<std::unique_ptr<AuthBlock>> auth_block =
      auth_block_utility_impl_->GetAuthBlockWithType(
          AuthBlockType::kChallengeCredential, auth_input);
  EXPECT_FALSE(auth_block.ok());
}

// Test `GetAuthBlockWithType()` with the `kDoubleWrappedCompat` type
// succeeds.
TEST_F(AuthBlockUtilityImplTest, GetAuthBlockWithTypeDoubleWrappedCompat) {
  // Setup.
  crypto_.Init();
  MakeAuthBlockUtilityImpl();
  AuthInput auth_input{
      .user_input = brillo::SecureBlob("fake-passkey"),
      .username = kUser,
      .obfuscated_username = SanitizeUserName(kUser),
  };

  // Test.
  CryptoStatusOr<std::unique_ptr<AuthBlock>> auth_block =
      auth_block_utility_impl_->GetAuthBlockWithType(
          AuthBlockType::kDoubleWrappedCompat, auth_input);

  // Verify.
  ASSERT_THAT(auth_block, IsOk());
  EXPECT_NE(auth_block.value(), nullptr);
}

// Test `GetAuthBlockWithType()` with the `kDoubleWrappedCompat` type fails
// when the RSA key loader is unavailable.
TEST_F(AuthBlockUtilityImplTest,
       GetAuthBlockWithTypeDoubleWrappedCompatFailNoLoader) {
  // Setup.
  EXPECT_CALL(cryptohome_keys_manager_, GetKeyLoader(CryptohomeKeyType::kRSA))
      .WillRepeatedly(Return(nullptr));
  crypto_.Init();
  MakeAuthBlockUtilityImpl();
  AuthInput auth_input{
      .user_input = brillo::SecureBlob("fake-passkey"),
      .username = kUser,
      .obfuscated_username = SanitizeUserName(kUser),
  };

  // Test.
  CryptoStatusOr<std::unique_ptr<AuthBlock>> auth_block =
      auth_block_utility_impl_->GetAuthBlockWithType(
          AuthBlockType::kDoubleWrappedCompat, auth_input);

  // Verify.
  EXPECT_THAT(auth_block, NotOk());
}

// Test `GetAuthBlockWithType()` with the `kTpmBoundToPcr` type succeeds.
TEST_F(AuthBlockUtilityImplTest, GetAuthBlockWithTypeTpmBoundToPcr) {
  // Setup.
  crypto_.Init();
  MakeAuthBlockUtilityImpl();
  AuthInput auth_input{
      .user_input = brillo::SecureBlob("fake-passkey"),
      .username = kUser,
      .obfuscated_username = SanitizeUserName(kUser),
  };

  // Test.
  CryptoStatusOr<std::unique_ptr<AuthBlock>> auth_block =
      auth_block_utility_impl_->GetAuthBlockWithType(
          AuthBlockType::kTpmBoundToPcr, auth_input);

  // Verify.
  ASSERT_THAT(auth_block, IsOk());
  EXPECT_NE(auth_block.value(), nullptr);
}

// Test `GetAuthBlockWithType()` with the `kTpmBoundToPcr` type fails when
// the RSA key loader is unavailable.
TEST_F(AuthBlockUtilityImplTest,
       GetAuthBlockWithTypeTpmBoundToPcrFailNoLoader) {
  // Setup.
  EXPECT_CALL(cryptohome_keys_manager_, GetKeyLoader(CryptohomeKeyType::kRSA))
      .WillRepeatedly(Return(nullptr));
  crypto_.Init();
  MakeAuthBlockUtilityImpl();
  AuthInput auth_input{
      .user_input = brillo::SecureBlob("fake-passkey"),
      .username = kUser,
      .obfuscated_username = SanitizeUserName(kUser),
  };

  // Test.
  CryptoStatusOr<std::unique_ptr<AuthBlock>> auth_block =
      auth_block_utility_impl_->GetAuthBlockWithType(
          AuthBlockType::kTpmBoundToPcr, auth_input);

  // Verify.
  EXPECT_THAT(auth_block, NotOk());
}

// Test `GetAuthBlockWithType()` with the `kTpmEcc` type succeeds.
TEST_F(AuthBlockUtilityImplTest, GetAuthBlockWithTypeTpmEcc) {
  // Setup.
  crypto_.Init();
  MakeAuthBlockUtilityImpl();
  AuthInput auth_input{
      .user_input = brillo::SecureBlob("fake-passkey"),
      .username = kUser,
      .obfuscated_username = SanitizeUserName(kUser),
  };

  // Test.
  CryptoStatusOr<std::unique_ptr<AuthBlock>> auth_block =
      auth_block_utility_impl_->GetAuthBlockWithType(AuthBlockType::kTpmEcc,
                                                     auth_input);

  // Verify.
  ASSERT_THAT(auth_block, IsOk());
  EXPECT_NE(auth_block.value(), nullptr);
}

// Test `GetAuthBlockWithType()` with the `kTpmEcc` type fails when the ECC
// key loader is unavailable.
TEST_F(AuthBlockUtilityImplTest, GetAuthBlockWithTypeTpmEccFailNoLoader) {
  // Setup.
  EXPECT_CALL(cryptohome_keys_manager_, GetKeyLoader(CryptohomeKeyType::kECC))
      .WillRepeatedly(Return(nullptr));
  crypto_.Init();
  MakeAuthBlockUtilityImpl();
  AuthInput auth_input{
      .user_input = brillo::SecureBlob("fake-passkey"),
      .username = kUser,
      .obfuscated_username = SanitizeUserName(kUser),
  };

  // Test.
  CryptoStatusOr<std::unique_ptr<AuthBlock>> auth_block =
      auth_block_utility_impl_->GetAuthBlockWithType(AuthBlockType::kTpmEcc,
                                                     auth_input);

  // Verify.
  EXPECT_THAT(auth_block, NotOk());
}

// Test that PrepareAuthBlockForRemoval succeeds for
// CryptohomeRecoveryAuthBlock.
TEST_F(AuthBlockUtilityImplTest,
       RemoveCryptohomeRecoveryWithoutRevocationAuthBlock) {
  CryptohomeRecoveryAuthBlockState recovery_state = {
      .hsm_payload = brillo::BlobFromString("hsm_payload"),
      .encrypted_destination_share =
          brillo::BlobFromString("encrypted_destination_share"),
      .channel_pub_key = brillo::BlobFromString("channel_pub_key"),
      .encrypted_channel_priv_key =
          brillo::BlobFromString("encrypted_channel_priv_key"),
  };
  AuthBlockState auth_state = {.state = recovery_state};

  MakeAuthBlockUtilityImpl();

  TestFuture<CryptohomeStatus> result;
  auth_block_utility_impl_->PrepareAuthBlockForRemoval(kObfuscated, auth_state,
                                                       result.GetCallback());
  ASSERT_TRUE(result.IsReady());
  ASSERT_THAT(result.Take(), IsOk());
}

// Test that PrepareAuthBlockForRemoval succeeds for CryptohomeRecoveryAuthBlock
// with credentials revocation enabled.
TEST_F(AuthBlockUtilityImplTest,
       RemoveCryptohomeRecoveryWithRevocationAuthBlock) {
  ON_CALL(hwsec_, IsPinWeaverEnabled()).WillByDefault(ReturnValue(true));
  uint64_t fake_label = 11;
  EXPECT_CALL(hwsec_pw_manager_, RemoveCredential(fake_label))
      .WillOnce(ReturnOk<hwsec::PinWeaverError>());
  crypto_.Init();

  CryptohomeRecoveryAuthBlockState recovery_state = {
      .hsm_payload = brillo::BlobFromString("hsm_payload"),
      .encrypted_destination_share =
          brillo::BlobFromString("encrypted_destination_share"),
      .channel_pub_key = brillo::BlobFromString("channel_pub_key"),
      .encrypted_channel_priv_key =
          brillo::BlobFromString("encrypted_channel_priv_key"),
  };
  RevocationState revocation_state = {
      .le_label = fake_label,
  };
  AuthBlockState auth_state = {
      .state = recovery_state,
      .revocation_state = revocation_state,
  };

  MakeAuthBlockUtilityImpl();

  TestFuture<CryptohomeStatus> result;
  auth_block_utility_impl_->PrepareAuthBlockForRemoval(kObfuscated, auth_state,
                                                       result.GetCallback());
  ASSERT_TRUE(result.IsReady());
  ASSERT_THAT(result.Take(), IsOk());
}

TEST_F(AuthBlockUtilityImplTest, EmptyAuthBlockState) {
  MakeAuthBlockUtilityImpl();
  AuthBlockState state;
  EXPECT_THAT(auth_block_utility_impl_->GetAuthBlockTypeFromState(state),
              Eq(std::nullopt));
}

TEST_F(AuthBlockUtilityImplTest, NonEmptyAuthBlockState) {
  MakeAuthBlockUtilityImpl();
  // We don't want to copy the full list of types in this test, since that's
  // just repeating the same list we have elsewhere. However, we want to at
  // least ensure a basic lookup works, so spot check a couple values.
  AuthBlockState first_state;
  first_state.state = PinWeaverAuthBlockState();
  EXPECT_THAT(auth_block_utility_impl_->GetAuthBlockTypeFromState(first_state),
              Eq(AuthBlockType::kPinWeaver));
  AuthBlockState second_state;
  second_state.state = CryptohomeRecoveryAuthBlockState();
  EXPECT_THAT(auth_block_utility_impl_->GetAuthBlockTypeFromState(second_state),
              Eq(AuthBlockType::kCryptohomeRecovery));
}

}  // namespace cryptohome
