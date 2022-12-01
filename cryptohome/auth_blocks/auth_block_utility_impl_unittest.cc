// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_blocks/auth_block_utility_impl.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>

#include <base/bind.h>
#include <base/files/file_path.h>
#include <base/files/scoped_temp_dir.h>
#include <base/task/sequenced_task_runner.h>
#include <base/test/bind.h>
#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <base/threading/sequenced_task_runner_handle.h>
#include <brillo/cryptohome.h>
#include <brillo/secure_blob.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libhwsec/factory/tpm2_simulator_factory_for_test.h>
#include <libhwsec/frontend/cryptohome/mock_frontend.h>
#include <libhwsec/frontend/pinweaver/mock_frontend.h>
#include <libhwsec/frontend/recovery_crypto/mock_frontend.h>
#include <libhwsec-foundation/crypto/libscrypt_compat.h>
#include <libhwsec-foundation/crypto/rsa.h>
#include <libhwsec-foundation/crypto/scrypt.h>
#include <libhwsec-foundation/crypto/sha.h>
#include <libhwsec-foundation/error/testing_helper.h>

#include "cryptohome/auth_blocks/auth_block.h"
#include "cryptohome/auth_blocks/auth_block_type.h"
#include "cryptohome/auth_blocks/auth_block_utility.h"
#include "cryptohome/auth_blocks/challenge_credential_auth_block.h"
#include "cryptohome/auth_blocks/double_wrapped_compat_auth_block.h"
#include "cryptohome/auth_blocks/fp_service.h"
#include "cryptohome/auth_blocks/pin_weaver_auth_block.h"
#include "cryptohome/auth_blocks/scrypt_auth_block.h"
#include "cryptohome/auth_blocks/tpm_bound_to_pcr_auth_block.h"
#include "cryptohome/auth_blocks/tpm_ecc_auth_block.h"
#include "cryptohome/auth_blocks/tpm_not_bound_to_pcr_auth_block.h"
#include "cryptohome/challenge_credentials/mock_challenge_credentials_helper.h"
#include "cryptohome/credentials.h"
#include "cryptohome/crypto.h"
#include "cryptohome/crypto_error.h"
#include "cryptohome/cryptorecovery/fake_recovery_mediator_crypto.h"
#include "cryptohome/cryptorecovery/recovery_crypto_hsm_cbor_serialization.h"
#include "cryptohome/cryptorecovery/recovery_crypto_impl.h"
#include "cryptohome/filesystem_layout.h"
#include "cryptohome/fingerprint_manager.h"
#include "cryptohome/flatbuffer_schemas/auth_block_state.h"
#include "cryptohome/key_objects.h"
#include "cryptohome/le_credential_manager_impl.h"
#include "cryptohome/mock_cryptohome_keys_manager.h"
#include "cryptohome/mock_fingerprint_manager.h"
#include "cryptohome/mock_key_challenge_service.h"
#include "cryptohome/mock_key_challenge_service_factory.h"
#include "cryptohome/mock_keyset_management.h"
#include "cryptohome/mock_le_credential_manager.h"
#include "cryptohome/mock_platform.h"
#include "cryptohome/mock_vault_keyset.h"
#include "cryptohome/vault_keyset.h"

namespace cryptohome {
namespace {

using ::base::test::TestFuture;
using ::cryptohome::error::CryptohomeCryptoError;
using ::cryptohome::error::CryptohomeLECredError;
using ::hwsec::TPMErrorBase;
using ::hwsec_foundation::DeriveSecretsScrypt;
using ::hwsec_foundation::error::testing::IsOk;
using ::hwsec_foundation::error::testing::IsOkAndHolds;
using ::hwsec_foundation::error::testing::NotOk;
using ::hwsec_foundation::error::testing::ReturnError;
using ::hwsec_foundation::error::testing::ReturnValue;
using ::hwsec_foundation::status::StatusChainOr;
using ::testing::_;
using ::testing::ByMove;
using ::testing::DoAll;
using ::testing::Eq;
using ::testing::Exactly;
using ::testing::IsNull;
using ::testing::Mock;
using ::testing::NiceMock;
using ::testing::NotNull;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::SetArgPointee;

constexpr char kUser[] = "Test User";
constexpr const char* kKeyDelegateDBusService = "key-delegate-service";
constexpr int kWorkFactor = 16384;
constexpr int kBlockSize = 8;
constexpr int kParallelFactor = 1;

}  // namespace

class AuthBlockUtilityImplTest : public ::testing::Test {
 public:
  AuthBlockUtilityImplTest()
      : recovery_crypto_fake_backend_(
            hwsec_factory_.GetRecoveryCryptoFrontend()),
        crypto_(&hwsec_,
                &pinweaver_,
                &cryptohome_keys_manager_,
                recovery_crypto_fake_backend_.get()) {}
  AuthBlockUtilityImplTest(const AuthBlockUtilityImplTest&) = delete;
  AuthBlockUtilityImplTest& operator=(const AuthBlockUtilityImplTest&) = delete;

  void SetUp() override {
    // Setup salt for brillo functions.
    keyset_management_ = std::make_unique<KeysetManagement>(
        &platform_, &crypto_, std::make_unique<VaultKeysetFactory>());
    system_salt_ =
        brillo::SecureBlob(*brillo::cryptohome::home::GetSystemSalt());
    ON_CALL(hwsec_, IsEnabled()).WillByDefault(ReturnValue(true));
    ON_CALL(hwsec_, IsReady()).WillByDefault(ReturnValue(true));
    ON_CALL(hwsec_, IsSealingSupported()).WillByDefault(ReturnValue(true));
    ON_CALL(hwsec_, GetPubkeyHash(_))
        .WillByDefault(ReturnValue(brillo::BlobFromString("public key hash")));
    ON_CALL(pinweaver_, IsEnabled()).WillByDefault(ReturnValue(true));
  }

  // Helper function to construct a fingerprint auth block service using the
  // mocks built into this test fixture.
  std::unique_ptr<FingerprintAuthBlockService>
  MakeFingerprintAuthBlockService() {
    return std::make_unique<FingerprintAuthBlockService>(
        base::BindRepeating(&AuthBlockUtilityImplTest::GetFingerprintManager,
                            base::Unretained(this)),
        base::BindRepeating(&AuthBlockUtilityImplTest::OnFingerprintScanResult,
                            base::Unretained(this)));
  }

  // Helper function to construct a "standard" auth block utility impl using the
  // mocks built into this test fixture.
  void MakeAuthBlockUtilityImpl() {
    auth_block_utility_impl_ = std::make_unique<AuthBlockUtilityImpl>(
        keyset_management_.get(), &crypto_, &platform_,
        MakeFingerprintAuthBlockService());
  }

 protected:
  FingerprintManager* GetFingerprintManager() { return &fp_manager_; }
  void OnFingerprintScanResult(user_data_auth::FingerprintScanResult result) {
    result_ = result;
  }

  base::test::SingleThreadTaskEnvironment task_environment_ = {
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  scoped_refptr<base::SequencedTaskRunner> task_runner_ =
      base::SequencedTaskRunnerHandle::Get();

  MockPlatform platform_;
  MockFingerprintManager fp_manager_;
  brillo::SecureBlob system_salt_;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager_;
  NiceMock<hwsec::MockCryptohomeFrontend> hwsec_;
  NiceMock<hwsec::MockPinWeaverFrontend> pinweaver_;
  hwsec::Tpm2SimulatorFactoryForTest hwsec_factory_;
  std::unique_ptr<hwsec::RecoveryCryptoFrontend> recovery_crypto_fake_backend_;
  Crypto crypto_;
  std::unique_ptr<KeysetManagement> keyset_management_;
  NiceMock<MockKeyChallengeServiceFactory> key_challenge_service_factory_;
  NiceMock<MockChallengeCredentialsHelper> challenge_credentials_helper_;
  user_data_auth::FingerprintScanResult result_;
  std::unique_ptr<AuthBlockUtilityImpl> auth_block_utility_impl_;
};

TEST_F(AuthBlockUtilityImplTest, GetSupportedAuthFactors) {
  MakeAuthBlockUtilityImpl();

  EXPECT_TRUE(auth_block_utility_impl_->IsAuthFactorSupported(
      AuthFactorType::kPassword, AuthFactorStorageType::kVaultKeyset, {}));
  EXPECT_TRUE(auth_block_utility_impl_->IsAuthFactorSupported(
      AuthFactorType::kPassword, AuthFactorStorageType::kUserSecretStash, {}));
  EXPECT_TRUE(auth_block_utility_impl_->IsAuthFactorSupported(
      AuthFactorType::kPassword, AuthFactorStorageType::kUserSecretStash,
      {AuthFactorType::kPassword}));
  EXPECT_FALSE(auth_block_utility_impl_->IsAuthFactorSupported(
      AuthFactorType::kPassword, AuthFactorStorageType::kUserSecretStash,
      {AuthFactorType::kKiosk}));

  EXPECT_CALL(hwsec_, IsPinWeaverEnabled()).WillOnce(ReturnValue(false));
  EXPECT_FALSE(auth_block_utility_impl_->IsAuthFactorSupported(
      AuthFactorType::kPin, AuthFactorStorageType::kVaultKeyset, {}));
  EXPECT_CALL(hwsec_, IsPinWeaverEnabled()).WillOnce(ReturnValue(true));
  EXPECT_TRUE(auth_block_utility_impl_->IsAuthFactorSupported(
      AuthFactorType::kPin, AuthFactorStorageType::kVaultKeyset, {}));
  EXPECT_CALL(hwsec_, IsPinWeaverEnabled()).WillOnce(ReturnValue(false));
  EXPECT_FALSE(auth_block_utility_impl_->IsAuthFactorSupported(
      AuthFactorType::kPin, AuthFactorStorageType::kUserSecretStash, {}));
  EXPECT_CALL(hwsec_, IsPinWeaverEnabled()).WillOnce(ReturnValue(true));
  EXPECT_TRUE(auth_block_utility_impl_->IsAuthFactorSupported(
      AuthFactorType::kPin, AuthFactorStorageType::kUserSecretStash, {}));
  EXPECT_CALL(hwsec_, IsPinWeaverEnabled()).WillOnce(ReturnValue(true));
  EXPECT_TRUE(auth_block_utility_impl_->IsAuthFactorSupported(
      AuthFactorType::kPin, AuthFactorStorageType::kUserSecretStash,
      {AuthFactorType::kPin}));
  EXPECT_FALSE(auth_block_utility_impl_->IsAuthFactorSupported(
      AuthFactorType::kPin, AuthFactorStorageType::kUserSecretStash,
      {AuthFactorType::kKiosk}));

  EXPECT_FALSE(auth_block_utility_impl_->IsAuthFactorSupported(
      AuthFactorType::kCryptohomeRecovery, AuthFactorStorageType::kVaultKeyset,
      {}));
  EXPECT_TRUE(auth_block_utility_impl_->IsAuthFactorSupported(
      AuthFactorType::kCryptohomeRecovery,
      AuthFactorStorageType::kUserSecretStash, {}));
  EXPECT_TRUE(auth_block_utility_impl_->IsAuthFactorSupported(
      AuthFactorType::kCryptohomeRecovery,
      AuthFactorStorageType::kUserSecretStash,
      {AuthFactorType::kCryptohomeRecovery}));
  EXPECT_FALSE(auth_block_utility_impl_->IsAuthFactorSupported(
      AuthFactorType::kCryptohomeRecovery,
      AuthFactorStorageType::kUserSecretStash, {AuthFactorType::kKiosk}));

  EXPECT_TRUE(auth_block_utility_impl_->IsAuthFactorSupported(
      AuthFactorType::kKiosk, AuthFactorStorageType::kVaultKeyset, {}));
  EXPECT_TRUE(auth_block_utility_impl_->IsAuthFactorSupported(
      AuthFactorType::kKiosk, AuthFactorStorageType::kUserSecretStash, {}));
  EXPECT_TRUE(auth_block_utility_impl_->IsAuthFactorSupported(
      AuthFactorType::kKiosk, AuthFactorStorageType::kVaultKeyset,
      {AuthFactorType::kKiosk}));
  EXPECT_TRUE(auth_block_utility_impl_->IsAuthFactorSupported(
      AuthFactorType::kKiosk, AuthFactorStorageType::kUserSecretStash,
      {AuthFactorType::kKiosk}));
  EXPECT_FALSE(auth_block_utility_impl_->IsAuthFactorSupported(
      AuthFactorType::kKiosk, AuthFactorStorageType::kVaultKeyset,
      {AuthFactorType::kPassword}));
  EXPECT_FALSE(auth_block_utility_impl_->IsAuthFactorSupported(
      AuthFactorType::kKiosk, AuthFactorStorageType::kUserSecretStash,
      {AuthFactorType::kPassword}));

  EXPECT_FALSE(auth_block_utility_impl_->IsAuthFactorSupported(
      AuthFactorType::kLegacyFingerprint, AuthFactorStorageType::kVaultKeyset,
      {}));
  EXPECT_FALSE(auth_block_utility_impl_->IsAuthFactorSupported(
      AuthFactorType::kLegacyFingerprint,
      AuthFactorStorageType::kUserSecretStash, {}));

  EXPECT_FALSE(auth_block_utility_impl_->IsAuthFactorSupported(
      AuthFactorType::kUnspecified, AuthFactorStorageType::kVaultKeyset, {}));
  EXPECT_FALSE(auth_block_utility_impl_->IsAuthFactorSupported(
      AuthFactorType::kUnspecified, AuthFactorStorageType::kUserSecretStash,
      {}));
}

TEST_F(AuthBlockUtilityImplTest, IsVerifyWithAuthFactorSupported) {
  MakeAuthBlockUtilityImpl();

  EXPECT_TRUE(auth_block_utility_impl_->IsVerifyWithAuthFactorSupported(
      AuthIntent::kVerifyOnly, AuthFactorType::kPassword));
  EXPECT_FALSE(auth_block_utility_impl_->IsVerifyWithAuthFactorSupported(
      AuthIntent::kVerifyOnly, AuthFactorType::kPin));
  EXPECT_FALSE(auth_block_utility_impl_->IsVerifyWithAuthFactorSupported(
      AuthIntent::kVerifyOnly, AuthFactorType::kCryptohomeRecovery));
  EXPECT_FALSE(auth_block_utility_impl_->IsVerifyWithAuthFactorSupported(
      AuthIntent::kVerifyOnly, AuthFactorType::kKiosk));
  EXPECT_TRUE(auth_block_utility_impl_->IsVerifyWithAuthFactorSupported(
      AuthIntent::kVerifyOnly, AuthFactorType::kSmartCard));
  EXPECT_TRUE(auth_block_utility_impl_->IsVerifyWithAuthFactorSupported(
      AuthIntent::kVerifyOnly, AuthFactorType::kLegacyFingerprint));
  EXPECT_TRUE(auth_block_utility_impl_->IsVerifyWithAuthFactorSupported(
      AuthIntent::kWebAuthn, AuthFactorType::kLegacyFingerprint));
  EXPECT_FALSE(auth_block_utility_impl_->IsVerifyWithAuthFactorSupported(
      AuthIntent::kDecrypt, AuthFactorType::kLegacyFingerprint));
  EXPECT_FALSE(auth_block_utility_impl_->IsVerifyWithAuthFactorSupported(
      AuthIntent::kVerifyOnly, AuthFactorType::kUnspecified));
}

TEST_F(AuthBlockUtilityImplTest, IsPrepareAuthFactorRequired) {
  MakeAuthBlockUtilityImpl();

  EXPECT_FALSE(auth_block_utility_impl_->IsPrepareAuthFactorRequired(
      AuthFactorType::kPassword));
  EXPECT_FALSE(auth_block_utility_impl_->IsPrepareAuthFactorRequired(
      AuthFactorType::kPin));
  EXPECT_FALSE(auth_block_utility_impl_->IsPrepareAuthFactorRequired(
      AuthFactorType::kCryptohomeRecovery));
  EXPECT_FALSE(auth_block_utility_impl_->IsPrepareAuthFactorRequired(
      AuthFactorType::kKiosk));
  EXPECT_FALSE(auth_block_utility_impl_->IsPrepareAuthFactorRequired(
      AuthFactorType::kSmartCard));
  EXPECT_TRUE(auth_block_utility_impl_->IsPrepareAuthFactorRequired(
      AuthFactorType::kLegacyFingerprint));
  EXPECT_FALSE(auth_block_utility_impl_->IsPrepareAuthFactorRequired(
      AuthFactorType::kUnspecified));
}

TEST_F(AuthBlockUtilityImplTest, PreparePasswordFailure) {
  MakeAuthBlockUtilityImpl();
  // password auth factor always fails the prepare.
  TestFuture<CryptohomeStatusOr<std::unique_ptr<PreparedAuthFactorToken>>>
      prepare_result;
  auth_block_utility_impl_->PrepareAuthFactorForAuth(
      AuthFactorType::kPassword, kUser, prepare_result.GetCallback());

  EXPECT_THAT(prepare_result.Get().status()->local_legacy_error(),
              Eq(user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
}

TEST_F(AuthBlockUtilityImplTest, PrepareLegacyFingerprintSuccess) {
  MakeAuthBlockUtilityImpl();

  // Setup.
  EXPECT_CALL(fp_manager_, StartAuthSessionAsyncForUser(kUser, _))
      .WillOnce([](std::string username,
                   FingerprintManager::StartSessionCallback callback) {
        std::move(callback).Run(true);
      });
  EXPECT_CALL(fp_manager_, SetSignalCallback(_));

  // Test.
  TestFuture<CryptohomeStatusOr<std::unique_ptr<PreparedAuthFactorToken>>>
      prepare_result;
  auth_block_utility_impl_->PrepareAuthFactorForAuth(
      AuthFactorType::kLegacyFingerprint, kUser, prepare_result.GetCallback());

  // Verify.
  ASSERT_THAT(prepare_result.Get(), IsOk());
}

TEST_F(AuthBlockUtilityImplTest, PrepareLegacyFingerprintFailure) {
  MakeAuthBlockUtilityImpl();

  // Setup.
  // Signal a failed fingerprint sensor start.
  EXPECT_CALL(fp_manager_, StartAuthSessionAsyncForUser(kUser, _))
      .WillOnce([](std::string username,
                   FingerprintManager::StartSessionCallback callback) {
        std::move(callback).Run(false);
      });

  // Test.
  TestFuture<CryptohomeStatusOr<std::unique_ptr<PreparedAuthFactorToken>>>
      prepare_result;
  auth_block_utility_impl_->PrepareAuthFactorForAuth(
      AuthFactorType::kLegacyFingerprint, kUser, prepare_result.GetCallback());

  // Verify.
  EXPECT_THAT(prepare_result.Get().status()->local_legacy_error(),
              Eq(user_data_auth::CRYPTOHOME_ERROR_FINGERPRINT_ERROR_INTERNAL));
}

TEST_F(AuthBlockUtilityImplTest, CheckSignalSuccess) {
  MakeAuthBlockUtilityImpl();

  // Setup.
  // Signal a successful auth scan.
  EXPECT_CALL(fp_manager_, StartAuthSessionAsyncForUser(kUser, _))
      .WillOnce([](std::string username,
                   FingerprintManager::StartSessionCallback callback) {
        std::move(callback).Run(true);
      });
  EXPECT_CALL(fp_manager_, SetSignalCallback(_))
      .WillOnce([](FingerprintManager::SignalCallback callback) {
        std::move(callback).Run(FingerprintScanStatus::SUCCESS);
      });
  TestFuture<CryptohomeStatusOr<std::unique_ptr<PreparedAuthFactorToken>>>
      prepare_result;
  auth_block_utility_impl_->PrepareAuthFactorForAuth(
      AuthFactorType::kLegacyFingerprint, kUser, prepare_result.GetCallback());
  ASSERT_THAT(prepare_result.Get(), IsOk());

  // Verify.
  ASSERT_EQ(result_, user_data_auth::FINGERPRINT_SCAN_RESULT_SUCCESS);
}

TEST_F(AuthBlockUtilityImplTest, CreatePasswordCredentialVerifier) {
  MakeAuthBlockUtilityImpl();

  AuthInput auth_input = {.user_input = brillo::SecureBlob("fake-passkey")};
  auto verifier = auth_block_utility_impl_->CreateCredentialVerifier(
      AuthFactorType::kPassword, "password", auth_input);
  ASSERT_THAT(verifier, NotNull());
  EXPECT_THAT(verifier->auth_factor_type(), Eq(AuthFactorType::kPassword));

  TestFuture<CryptohomeStatus> status_result;
  verifier->Verify(auth_input, status_result.GetCallback());
  EXPECT_THAT(status_result.Get(), IsOk());
}

TEST_F(AuthBlockUtilityImplTest, CreateFingerprintVerifierWithLabelFails) {
  MakeAuthBlockUtilityImpl();

  auto verifier = auth_block_utility_impl_->CreateCredentialVerifier(
      AuthFactorType::kLegacyFingerprint, "legacy-fp", {});
  EXPECT_THAT(verifier, IsNull());
}

TEST_F(AuthBlockUtilityImplTest, VerifyFingerprintSuccess) {
  MakeAuthBlockUtilityImpl();

  auto verifier = auth_block_utility_impl_->CreateCredentialVerifier(
      AuthFactorType::kLegacyFingerprint, "", {});
  ASSERT_THAT(verifier, NotNull());
  EXPECT_THAT(verifier->auth_factor_type(),
              Eq(AuthFactorType::kLegacyFingerprint));

  // Signal a successful auth scan.
  EXPECT_CALL(fp_manager_, StartAuthSessionAsyncForUser(kUser, _))
      .WillOnce([](std::string username,
                   FingerprintManager::StartSessionCallback callback) {
        std::move(callback).Run(true);
      });
  EXPECT_CALL(fp_manager_, SetSignalCallback(_))
      .WillOnce([](FingerprintManager::SignalCallback callback) {
        std::move(callback).Run(FingerprintScanStatus::SUCCESS);
      });

  // legacy fingerprint auth factor needs to kicks off the prepare.
  TestFuture<CryptohomeStatusOr<std::unique_ptr<PreparedAuthFactorToken>>>
      prepare_result;
  auth_block_utility_impl_->PrepareAuthFactorForAuth(
      AuthFactorType::kLegacyFingerprint, kUser, prepare_result.GetCallback());
  ASSERT_THAT(prepare_result.Get(), IsOk());
  auto token = std::move(*prepare_result.Take());

  // Run the Verify and check the result.
  TestFuture<CryptohomeStatus> verify_result;
  verifier->Verify({}, verify_result.GetCallback());
  EXPECT_THAT(verify_result.Get(), IsOk());

  EXPECT_CALL(fp_manager_, EndAuthSession());
  CryptohomeStatus status = token->Terminate();
  EXPECT_THAT(status, IsOk());
}

TEST_F(AuthBlockUtilityImplTest, VerifyFingerprintFailure) {
  MakeAuthBlockUtilityImpl();

  auto verifier = auth_block_utility_impl_->CreateCredentialVerifier(
      AuthFactorType::kLegacyFingerprint, "", {});
  ASSERT_THAT(verifier, NotNull());
  EXPECT_THAT(verifier->auth_factor_type(),
              Eq(AuthFactorType::kLegacyFingerprint));

  // Signal a failed and not retry-able auth scan.
  EXPECT_CALL(fp_manager_, StartAuthSessionAsyncForUser(kUser, _))
      .WillOnce([](std::string username,
                   FingerprintManager::StartSessionCallback callback) {
        std::move(callback).Run(true);
      });
  EXPECT_CALL(fp_manager_, SetSignalCallback(_))
      .WillOnce([](FingerprintManager::SignalCallback callback) {
        std::move(callback).Run(
            FingerprintScanStatus::FAILED_RETRY_NOT_ALLOWED);
      });

  // legacy fingerprint auth factor needs to kicks off the prepare.
  TestFuture<CryptohomeStatusOr<std::unique_ptr<PreparedAuthFactorToken>>>
      prepare_result;
  auth_block_utility_impl_->PrepareAuthFactorForAuth(
      AuthFactorType::kLegacyFingerprint, kUser, prepare_result.GetCallback());
  ASSERT_THAT(prepare_result.Get(), IsOk());
  auto token = std::move(*prepare_result.Take());

  // Run the Verify and check the result.
  TestFuture<CryptohomeStatus> verify_result;
  verifier->Verify({}, verify_result.GetCallback());
  EXPECT_THAT(verify_result.Get()->local_legacy_error(),
              Eq(user_data_auth::CRYPTOHOME_ERROR_FINGERPRINT_DENIED));

  EXPECT_CALL(fp_manager_, EndAuthSession());
  CryptohomeStatus status = token->Terminate();
  EXPECT_THAT(status, IsOk());
}

// Test that CreateKeyBlobsWithAuthBlock creates AuthBlockState and KeyBlobs
// with PinWeaverAuthBlock when the AuthBlock type is low entropy credential.
TEST_F(AuthBlockUtilityImplTest, CreatePinweaverAuthBlockTest) {
  // Setup mock expectations and test inputs for low entropy AuthBlock.
  brillo::SecureBlob passkey(20, 'A');
  Credentials credentials(kUser, passkey);
  brillo::SecureBlob reset_secret(32, 'S');
  brillo::SecureBlob le_secret;

  MockLECredentialManager* le_cred_manager = new MockLECredentialManager();

  EXPECT_CALL(*le_cred_manager, InsertCredential(_, _, _, _, _, _, _))
      .WillOnce(
          DoAll(SaveArg<1>(&le_secret), ReturnError<CryptohomeLECredError>()));
  crypto_.set_le_manager_for_testing(
      std::unique_ptr<cryptohome::LECredentialManager>(le_cred_manager));
  crypto_.Init();

  MakeAuthBlockUtilityImpl();

  // Test
  KeyBlobs out_key_blobs;
  AuthBlockState out_state;
  EXPECT_TRUE(auth_block_utility_impl_
                  ->CreateKeyBlobsWithAuthBlock(AuthBlockType::kPinWeaver,
                                                credentials, reset_secret,
                                                out_state, out_key_blobs)
                  .ok());

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
  crypto_.Init();

  ASSERT_TRUE(DeriveSecretsScrypt(passkey, salt, {&le_secret}));

  ON_CALL(*le_cred_manager, CheckCredential(_, _, _, _))
      .WillByDefault(ReturnError<CryptohomeLECredError>());
  EXPECT_CALL(*le_cred_manager, CheckCredential(_, le_secret, _, _))
      .Times(Exactly(1));

  MakeAuthBlockUtilityImpl();

  PinWeaverAuthBlockState pin_state = {
      .le_label = 0, .salt = salt, .chaps_iv = chaps_iv, .fek_iv = fek_iv};
  AuthBlockState auth_state = {.state = pin_state};

  // Test
  // No need to check for the KeyBlobs value, it is already being tested in
  // AuthBlock unittest.
  KeyBlobs out_key_blobs;
  EXPECT_TRUE(auth_block_utility_impl_
                  ->DeriveKeyBlobsWithAuthBlock(AuthBlockType::kPinWeaver,
                                                credentials, auth_state,
                                                out_key_blobs)
                  .ok());
}

// Test that CreateKeyBlobsWithAuthBlock creates AuthBlockState and KeyBlobs
// with TpmBoundToPcrAuthBlock when the AuthBlock type is
// AuthBlockType::kTpmBoundToPcr.
TEST_F(AuthBlockUtilityImplTest, CreateTpmBackedPcrBoundAuthBlock) {
  // Setup test inputs and the mock expectations..
  brillo::SecureBlob passkey(20, 'A');
  Credentials credentials(kUser, passkey);

  brillo::SecureBlob scrypt_derived_key;
  crypto_.Init();

  brillo::SecureBlob auth_value(256, 'a');
  EXPECT_CALL(hwsec_, GetAuthValue(_, _))
      .WillOnce(
          DoAll(SaveArg<1>(&scrypt_derived_key), ReturnValue(auth_value)));
  EXPECT_CALL(hwsec_, SealWithCurrentUser(_, auth_value, _)).Times(Exactly(2));
  ON_CALL(hwsec_, SealWithCurrentUser(_, _, _))
      .WillByDefault(ReturnValue(brillo::Blob()));

  MakeAuthBlockUtilityImpl();

  // Test
  KeyBlobs out_key_blobs;
  AuthBlockState out_state;
  EXPECT_TRUE(auth_block_utility_impl_
                  ->CreateKeyBlobsWithAuthBlock(AuthBlockType::kTpmBoundToPcr,
                                                credentials, std::nullopt,
                                                out_state, out_key_blobs)
                  .ok());

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

  EXPECT_TRUE(auth_block_utility_impl_
                  ->DeriveKeyBlobsWithAuthBlock(AuthBlockType::kTpmBoundToPcr,
                                                credentials, auth_state,
                                                out_key_blobs)
                  .ok());
}

// Test that CreateKeyBlobsWithAuthBlock creates AuthBlockState and KeyBlobs
// with TpmNotBoundToPcrAuthBlock when the AuthBlock type is
// AuthBlockType::kTpmNotBoundToPcr.
TEST_F(AuthBlockUtilityImplTest, CreateTpmBackedNonPcrBoundAuthBlock) {
  // Setup test inputs and the mock expectations.
  brillo::SecureBlob passkey(20, 'A');
  Credentials credentials(kUser, passkey);
  brillo::SecureBlob aes_key;
  crypto_.Init();

  brillo::Blob encrypt_out(64, 'X');
  EXPECT_CALL(hwsec_, Encrypt(_, _)).WillOnce(ReturnValue(encrypt_out));

  // Test
  MakeAuthBlockUtilityImpl();
  KeyBlobs out_key_blobs;
  AuthBlockState out_state;
  EXPECT_TRUE(auth_block_utility_impl_
                  ->CreateKeyBlobsWithAuthBlock(
                      AuthBlockType::kTpmNotBoundToPcr, credentials,
                      std::nullopt, out_state, out_key_blobs)
                  .ok());

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
  brillo::SecureBlob tpm_key;
  brillo::SecureBlob salt(system_salt_);
  brillo::SecureBlob aes_key(32);
  crypto_.Init();
  ASSERT_TRUE(DeriveSecretsScrypt(passkey, salt, {&aes_key}));

  brillo::Blob encrypt_out(64, 'X');
  EXPECT_TRUE(hwsec_foundation::ObscureRsaMessage(
      brillo::SecureBlob(encrypt_out.begin(), encrypt_out.end()), aes_key,
      &tpm_key));

  EXPECT_CALL(hwsec_, Decrypt(_, encrypt_out))
      .WillOnce(ReturnValue(brillo::SecureBlob()));

  TpmNotBoundToPcrAuthBlockState tpm_state = {
      .scrypt_derived = true, .salt = salt, .tpm_key = tpm_key};
  AuthBlockState auth_state = {.state = tpm_state};

  // Test
  KeyBlobs out_key_blobs;
  MakeAuthBlockUtilityImpl();

  EXPECT_TRUE(
      auth_block_utility_impl_
          ->DeriveKeyBlobsWithAuthBlock(AuthBlockType::kTpmNotBoundToPcr,
                                        credentials, auth_state, out_key_blobs)
          .ok());
}

// Test that CreateKeyBlobsWithAuthBlock creates AuthBlockState and KeyBlobs
// with TpmEccAuthBlock when the AuthBlock type is AuthBlockType::kTpmEcc.
TEST_F(AuthBlockUtilityImplTest, CreateTpmBackedEccAuthBlock) {
  // Setup test inputs and the mock expectations.
  brillo::SecureBlob passkey(20, 'A');
  Credentials credentials(kUser, passkey);
  crypto_.Init();

  brillo::SecureBlob scrypt_derived_key;
  brillo::SecureBlob auth_value(32, 'a');
  EXPECT_CALL(hwsec_, GetManufacturer()).WillOnce(ReturnValue(0x43524f53));
  EXPECT_CALL(hwsec_, GetAuthValue(_, _))
      .Times(Exactly(5))
      .WillOnce(DoAll(SaveArg<1>(&scrypt_derived_key), ReturnValue(auth_value)))
      .WillRepeatedly(ReturnValue(auth_value));
  EXPECT_CALL(hwsec_, SealWithCurrentUser(_, auth_value, _))
      .WillOnce(ReturnValue(brillo::Blob()))
      .WillOnce(ReturnValue(brillo::Blob()));

  MakeAuthBlockUtilityImpl();

  // Test
  KeyBlobs out_key_blobs;
  AuthBlockState out_state;
  EXPECT_TRUE(auth_block_utility_impl_
                  ->CreateKeyBlobsWithAuthBlock(AuthBlockType::kTpmEcc,
                                                credentials, std::nullopt,
                                                out_state, out_key_blobs)
                  .ok());

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
  brillo::SecureBlob fake_hash("public key hash");
  crypto_.Init();

  EXPECT_CALL(hwsec_, PreloadSealedData(_)).WillOnce(ReturnValue(std::nullopt));
  EXPECT_CALL(hwsec_, GetAuthValue(_, _))
      .Times(Exactly(5))
      .WillRepeatedly(ReturnValue(brillo::SecureBlob()));

  brillo::SecureBlob fake_hvkkm(32, 'D');
  EXPECT_CALL(hwsec_, UnsealWithCurrentUser(_, _, _))
      .WillOnce(ReturnValue(fake_hvkkm));

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
  MakeAuthBlockUtilityImpl();

  EXPECT_TRUE(auth_block_utility_impl_
                  ->DeriveKeyBlobsWithAuthBlock(AuthBlockType::kTpmEcc,
                                                credentials, auth_state,
                                                out_key_blobs)
                  .ok());
}

// Test that CreateKeyBlobsWithAuthBlock creates AuthBlockState with
// ScryptAuthBlock when the AuthBlock type is
// AuthBlockType::kScrypt.
TEST_F(AuthBlockUtilityImplTest, CreateScryptAuthBlockTest) {
  // Setup mock expectations and test inputs for low entropy AuthBlock.
  brillo::SecureBlob passkey(20, 'A');
  Credentials credentials(kUser, passkey);

  MakeAuthBlockUtilityImpl();

  // Test
  KeyBlobs out_key_blobs;
  AuthBlockState out_state;
  EXPECT_TRUE(auth_block_utility_impl_
                  ->CreateKeyBlobsWithAuthBlock(AuthBlockType::kScrypt,
                                                credentials, std::nullopt,
                                                out_state, out_key_blobs)
                  .ok());

  // Verify that a script wrapped AuthBlock is generated.
  EXPECT_TRUE(std::holds_alternative<ScryptAuthBlockState>(out_state.state));
  auto& scrypt_state = std::get<ScryptAuthBlockState>(out_state.state);
  EXPECT_TRUE(scrypt_state.salt.has_value());
}

// Test that DeriveKeyBlobsWithAuthBlock derives AuthBlocks with
// ScryptAuthBlock when the AuthBlock type is
// AuthBlockType::kScrypt.
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

  ScryptAuthBlockState scrypt_state = {
      .salt = brillo::SecureBlob("salt"),
      .chaps_salt = brillo::SecureBlob("chaps_salt"),
      .reset_seed_salt = brillo::SecureBlob("reset_seed_salt"),
      .work_factor = kWorkFactor,
      .block_size = kBlockSize,
      .parallel_factor = kParallelFactor,
  };
  AuthBlockState auth_state = {.state = scrypt_state};

  // Test
  KeyBlobs out_key_blobs;
  MakeAuthBlockUtilityImpl();

  EXPECT_TRUE(auth_block_utility_impl_
                  ->DeriveKeyBlobsWithAuthBlock(AuthBlockType::kScrypt,
                                                credentials, auth_state,
                                                out_key_blobs)
                  .ok());
}

// Test that DeriveKeyBlobsWithAuthBlock derives AuthBlocks with
// DoubleWrappedCompatAuthBlock when the AuthBlock type is
// AuthBlockType::kDoubleWrappedCompat.
TEST_F(AuthBlockUtilityImplTest, DeriveDoubleWrappedAuthBlock) {
  // Setup test inputs and the mock expectations.
  crypto_.Init();
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

  ScryptAuthBlockState scrypt_state = {
      .salt = brillo::SecureBlob("salt"),
      .chaps_salt = brillo::SecureBlob("chaps_salt"),
      .reset_seed_salt = brillo::SecureBlob("reset_seed_salt"),
      .work_factor = kWorkFactor,
      .block_size = kBlockSize,
      .parallel_factor = kParallelFactor,
  };
  TpmNotBoundToPcrAuthBlockState tpm_state = {
      .scrypt_derived = false,
      .salt = system_salt_,
      .tpm_key = brillo::SecureBlob(20, 'A')};
  DoubleWrappedCompatAuthBlockState double_wrapped_state = {
      .scrypt_state = scrypt_state, .tpm_state = tpm_state};
  AuthBlockState auth_state = {.state = double_wrapped_state};

  // Test
  KeyBlobs out_key_blobs;
  MakeAuthBlockUtilityImpl();

  EXPECT_TRUE(
      auth_block_utility_impl_
          ->DeriveKeyBlobsWithAuthBlock(AuthBlockType::kDoubleWrappedCompat,
                                        credentials, auth_state, out_key_blobs)
          .ok());
}

// Test that CreateKeyBlobsWithAuthBlock creates AuthBlockState with
// ChallengeCredentialAuthBlock when the AuthBlock type is
// AuthBlockType::kChallengeCredential.
TEST_F(AuthBlockUtilityImplTest, CreateChallengeCredentialAuthBlock) {
  // Setup mock expectations and test inputs for low entropy AuthBlock.
  brillo::SecureBlob passkey(20, 'A');
  Credentials credentials(kUser, passkey);

  MakeAuthBlockUtilityImpl();

  // Test
  KeyBlobs out_key_blobs;
  AuthBlockState out_state;
  EXPECT_TRUE(auth_block_utility_impl_
                  ->CreateKeyBlobsWithAuthBlock(
                      AuthBlockType::kChallengeCredential, credentials,
                      std::nullopt, out_state, out_key_blobs)
                  .ok());

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

  ScryptAuthBlockState scrypt_state = {
      .salt = brillo::SecureBlob("salt"),
      .chaps_salt = brillo::SecureBlob("chaps_salt"),
      .reset_seed_salt = brillo::SecureBlob("reset_seed_salt"),
      .work_factor = kWorkFactor,
      .block_size = kBlockSize,
      .parallel_factor = kParallelFactor,
  };
  ChallengeCredentialAuthBlockState cc_state = {.scrypt_state = scrypt_state};
  AuthBlockState auth_state = {.state = cc_state};

  // Test
  KeyBlobs out_key_blobs;
  MakeAuthBlockUtilityImpl();

  EXPECT_TRUE(
      auth_block_utility_impl_
          ->DeriveKeyBlobsWithAuthBlock(AuthBlockType::kChallengeCredential,
                                        credentials, auth_state, out_key_blobs)
          .ok());
}

// Test that CreateKeyBlobsWithAuthBlockAsync creates AuthBlockState
// and KeyBlobs, internally using a SyncToAsyncAuthBlockAdapter for
// accessing the key material from TpmBoundToPcrAuthBlock.
TEST_F(AuthBlockUtilityImplTest, SyncToAsyncAdapterCreate) {
  // Setup test inputs and the mock expectations.
  brillo::SecureBlob passkey(20, 'A');
  Credentials credentials(kUser, passkey);

  brillo::SecureBlob scrypt_derived_key;
  crypto_.Init();

  brillo::SecureBlob auth_value(256, 'a');
  EXPECT_CALL(hwsec_, GetAuthValue(_, _))
      .WillOnce(
          DoAll(SaveArg<1>(&scrypt_derived_key), ReturnValue(auth_value)));
  EXPECT_CALL(hwsec_, SealWithCurrentUser(_, auth_value, _)).Times(Exactly(2));
  ON_CALL(hwsec_, SealWithCurrentUser(_, _, _))
      .WillByDefault(ReturnValue(brillo::Blob()));

  MakeAuthBlockUtilityImpl();

  AuthBlock::CreateCallback create_callback = base::BindLambdaForTesting(
      [&](CryptoStatus error, std::unique_ptr<KeyBlobs> blobs,
          std::unique_ptr<AuthBlockState> auth_state) {
        // Evaluate results of KeyBlobs and AuthBlockState returned by callback.
        EXPECT_TRUE(error.ok());
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

  AuthInput auth_input = {
      credentials.passkey(),
      /*locked_to_single_user*=*/std::nullopt, credentials.username(),
      credentials.GetObfuscatedUsername(), /*reset_secret*/ std::nullopt};

  // Test.
  auth_block_utility_impl_->CreateKeyBlobsWithAuthBlockAsync(
      AuthBlockType::kTpmBoundToPcr, auth_input, std::move(create_callback));
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
  AuthInput auth_input = {credentials.passkey(),
                          /*locked_to_single_user=*/std::nullopt};

  MakeAuthBlockUtilityImpl();

  // Test.
  AuthBlock::DeriveCallback derive_callback = base::BindLambdaForTesting(
      [&](CryptoStatus error, std::unique_ptr<KeyBlobs> blobs) {
        // Evaluate results of KeyBlobs returned by callback.
        EXPECT_TRUE(error.ok());
        EXPECT_NE(blobs->vkk_key, std::nullopt);
        EXPECT_NE(blobs->vkk_iv, std::nullopt);
        EXPECT_NE(blobs->chaps_iv, std::nullopt);
      });

  auth_block_utility_impl_->DeriveKeyBlobsWithAuthBlockAsync(
      AuthBlockType::kTpmBoundToPcr, auth_input, auth_state,
      std::move(derive_callback));
}

// Test that CreateKeyBlobsWithAuthBlockAsync creates AuthBlockState
// and KeyBlobs, internally using a AsyncChallengeCredentialAuthBlock for
// accessing the key material.
TEST_F(AuthBlockUtilityImplTest, AsyncChallengeCredentialCreate) {
  brillo::SecureBlob passkey("passkey");
  Credentials credentials(kUser, passkey);
  crypto_.Init();

  EXPECT_CALL(challenge_credentials_helper_, GenerateNew(kUser, _, _, _, _))
      .WillOnce([&](auto&&, auto public_key_info, auto&&, auto&&,
                    auto&& callback) {
        auto info = std::make_unique<structure::SignatureChallengeInfo>();
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
  auth_block_utility_impl_->InitializeChallengeCredentialsHelper(
      &challenge_credentials_helper_, &key_challenge_service_factory_);

  AuthBlock::CreateCallback create_callback = base::BindLambdaForTesting(
      [&](CryptoStatus error, std::unique_ptr<KeyBlobs> blobs,
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
  AuthInput auth_input;
  auth_input.obfuscated_username = credentials.GetObfuscatedUsername();
  auth_input.username = kUser, auth_input.locked_to_single_user = false;
  auth_input.challenge_credential_auth_input = ChallengeCredentialAuthInput{
      .public_key_spki_der = brillo::BlobFromString("public_key_spki_der"),
      .challenge_signature_algorithms =
          {structure::ChallengeSignatureAlgorithm::kRsassaPkcs1V15Sha256},
      .dbus_service_name = kKeyDelegateDBusService};

  // Test.
  auth_block_utility_impl_->CreateKeyBlobsWithAuthBlockAsync(
      AuthBlockType::kChallengeCredential, auth_input,
      std::move(create_callback));
}

// The AsyncChallengeCredentialAuthBlock::Derive should work correctly.
TEST_F(AuthBlockUtilityImplTest, AsyncChallengeCredentialDerive) {
  brillo::SecureBlob passkey("passkey");
  Credentials credentials(kUser, passkey);
  crypto_.Init();

  AuthBlockState auth_state{
      .state =
          ChallengeCredentialAuthBlockState{
              .scrypt_state =
                  ScryptAuthBlockState{
                      .salt = brillo::SecureBlob("salt"),
                      .chaps_salt = brillo::SecureBlob("chaps_salt"),
                      .reset_seed_salt = brillo::SecureBlob("reset_seed_salt"),
                      .work_factor = kWorkFactor,
                      .block_size = kBlockSize,
                      .parallel_factor = kParallelFactor,
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
  auth_block_utility_impl_->InitializeChallengeCredentialsHelper(
      &challenge_credentials_helper_, &key_challenge_service_factory_);
  // Test.
  AuthBlock::DeriveCallback derive_callback = base::BindLambdaForTesting(
      [&](CryptoStatus error, std::unique_ptr<KeyBlobs> blobs) {
        ASSERT_TRUE(error.ok());
        EXPECT_EQ(derived_key, blobs->vkk_key);
        EXPECT_EQ(derived_chaps_key, blobs->scrypt_chaps_key);
        EXPECT_EQ(derived_reset_seed_key, blobs->scrypt_reset_seed_key);
      });

  AuthInput auth_input = {
      credentials.passkey(),
      /*locked_to_single_user=*/std::nullopt, .username = kUser,
      .challenge_credential_auth_input = ChallengeCredentialAuthInput{
          .public_key_spki_der = brillo::BlobFromString("public_key_spki_der"),
          .challenge_signature_algorithms =
              {structure::ChallengeSignatureAlgorithm::kRsassaPkcs1V15Sha256},
          .dbus_service_name = kKeyDelegateDBusService}};
  auth_block_utility_impl_->DeriveKeyBlobsWithAuthBlockAsync(
      AuthBlockType::kChallengeCredential, auth_input, auth_state,
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
  crypto_.Init();

  MakeAuthBlockUtilityImpl();

  AuthInput auth_input = {
      credentials.passkey(), std::nullopt /*locked_to_single_user*=*/,
      credentials.GetObfuscatedUsername(), std::nullopt /*reset_secret*/};

  AuthBlock::CreateCallback create_callback = base::BindLambdaForTesting(
      [&](CryptoStatus error, std::unique_ptr<KeyBlobs> blobs,
          std::unique_ptr<AuthBlockState> auth_state) {
        // Evaluate results of KeyBlobs and AuthBlockState returned by callback.
        EXPECT_EQ(error->local_crypto_error(), CryptoError::CE_OTHER_CRYPTO);
        EXPECT_EQ(blobs, nullptr);
        EXPECT_EQ(auth_state, nullptr);
      });

  // Test.
  auth_block_utility_impl_->CreateKeyBlobsWithAuthBlockAsync(
      AuthBlockType::kMaxValue, auth_input, std::move(create_callback));
}

TEST_F(AuthBlockUtilityImplTest, CreateKeyBlobsWithAuthBlockWrongTypeFails) {
  // Setup mock expectations and test inputs for low entropy AuthBlock.
  brillo::SecureBlob passkey(20, 'A');
  Credentials credentials(kUser, passkey);

  MakeAuthBlockUtilityImpl();

  // Test
  KeyBlobs out_key_blobs;
  AuthBlockState out_state;
  EXPECT_EQ(
      CryptoError::CE_OTHER_CRYPTO,
      auth_block_utility_impl_
          ->CreateKeyBlobsWithAuthBlock(AuthBlockType::kMaxValue, credentials,
                                        std::nullopt, out_state, out_key_blobs)
          ->local_crypto_error());
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
  // Insert MockKeysetManagement into AuthBlockUtility
  auth_block_utility_impl_ = std::make_unique<AuthBlockUtilityImpl>(
      &keyset_management, &crypto_, &platform_,
      MakeFingerprintAuthBlockService());
  // Test
  AuthBlockState out_state;
  EXPECT_CALL(keyset_management, GetVaultKeyset(_, _))
      .WillOnce(Return(ByMove(std::move(vk))));
  auth_block_utility_impl_->GetAuthBlockStateFromVaultKeyset(
      credentials.key_data().label(), credentials.GetObfuscatedUsername(),
      out_state);
  EXPECT_TRUE(std::holds_alternative<PinWeaverAuthBlockState>(out_state.state));

  // ChallengeCredentialAuthBlockState

  // Construct the vault keyset
  serialized.set_flags(SerializedVaultKeyset::SCRYPT_WRAPPED |
                       SerializedVaultKeyset::SIGNATURE_CHALLENGE_PROTECTED);
  auto vk1 = std::make_unique<VaultKeyset>();
  vk1->InitializeFromSerialized(serialized);

  const brillo::Blob kScryptPlaintext = brillo::BlobFromString("plaintext");
  const auto blob_to_encrypt = brillo::SecureBlob(brillo::CombineBlobs(
      {kScryptPlaintext, hwsec_foundation::Sha1(kScryptPlaintext)}));
  brillo::SecureBlob wrapped_keyset;
  brillo::SecureBlob wrapped_chaps_key;
  brillo::SecureBlob wrapped_reset_seed;
  brillo::SecureBlob derived_key = {
      0x67, 0xeb, 0xcd, 0x84, 0x49, 0x5e, 0xa2, 0xf3, 0xb1, 0xe6, 0xe7,
      0x5b, 0x13, 0xb9, 0x16, 0x2f, 0x5a, 0x39, 0xc8, 0xfe, 0x6a, 0x60,
      0xd4, 0x7a, 0xd8, 0x2b, 0x44, 0xc4, 0x45, 0x53, 0x1a, 0x85, 0x4a,
      0x97, 0x9f, 0x2d, 0x06, 0xf5, 0xd0, 0xd3, 0xa6, 0xe7, 0xac, 0x9b,
      0x02, 0xaf, 0x3c, 0x08, 0xce, 0x43, 0x46, 0x32, 0x6d, 0xd7, 0x2b,
      0xe9, 0xdf, 0x8b, 0x38, 0x0e, 0x60, 0x3d, 0x64, 0x12};
  brillo::SecureBlob scrypt_salt = brillo::SecureBlob("salt");
  brillo::SecureBlob chaps_salt = brillo::SecureBlob("chaps_salt");
  brillo::SecureBlob reset_seed_salt = brillo::SecureBlob("reset_seed_salt");

  scrypt_salt.resize(hwsec_foundation::kLibScryptSaltSize);
  chaps_salt.resize(hwsec_foundation::kLibScryptSaltSize);
  reset_seed_salt.resize(hwsec_foundation::kLibScryptSaltSize);
  ASSERT_TRUE(hwsec_foundation::LibScryptCompat::Encrypt(
      derived_key, scrypt_salt, blob_to_encrypt,
      hwsec_foundation::kDefaultScryptParams, &wrapped_keyset));
  ASSERT_TRUE(hwsec_foundation::LibScryptCompat::Encrypt(
      derived_key, chaps_salt, blob_to_encrypt,
      hwsec_foundation::kDefaultScryptParams, &wrapped_chaps_key));
  ASSERT_TRUE(hwsec_foundation::LibScryptCompat::Encrypt(
      derived_key, reset_seed_salt, blob_to_encrypt,
      hwsec_foundation::kDefaultScryptParams, &wrapped_reset_seed));
  vk1->SetWrappedKeyset(wrapped_keyset);
  vk1->SetWrappedChapsKey(wrapped_chaps_key);
  vk1->SetWrappedResetSeed(wrapped_reset_seed);

  // Test
  EXPECT_CALL(keyset_management, GetVaultKeyset(_, _))
      .WillOnce(Return(ByMove(std::move(vk1))));
  auth_block_utility_impl_->GetAuthBlockStateFromVaultKeyset(
      credentials.key_data().label(), credentials.GetObfuscatedUsername(),
      out_state);
  EXPECT_TRUE(std::holds_alternative<ChallengeCredentialAuthBlockState>(
      out_state.state));

  const ChallengeCredentialAuthBlockState* cc_state =
      std::get_if<ChallengeCredentialAuthBlockState>(&out_state.state);
  EXPECT_NE(cc_state, nullptr);

  // ScryptAuthBlockState

  // Construct the vault keyset
  serialized.set_flags(SerializedVaultKeyset::SCRYPT_WRAPPED);
  auto vk2 = std::make_unique<VaultKeyset>();
  vk2->InitializeFromSerialized(serialized);
  vk2->SetWrappedKeyset(wrapped_keyset);
  vk2->SetWrappedChapsKey(wrapped_chaps_key);
  vk2->SetWrappedResetSeed(wrapped_reset_seed);

  // Test
  EXPECT_CALL(keyset_management, GetVaultKeyset(_, _))
      .WillOnce(Return(ByMove(std::move(vk2))));
  auth_block_utility_impl_->GetAuthBlockStateFromVaultKeyset(
      credentials.key_data().label(), credentials.GetObfuscatedUsername(),
      out_state);
  EXPECT_TRUE(std::holds_alternative<ScryptAuthBlockState>(out_state.state));
  const ScryptAuthBlockState* scrypt_state =
      std::get_if<ScryptAuthBlockState>(&out_state.state);
  EXPECT_NE(scrypt_state, nullptr);
  EXPECT_TRUE(scrypt_state->salt.has_value());
  EXPECT_TRUE(scrypt_state->chaps_salt.has_value());
  EXPECT_TRUE(scrypt_state->reset_seed_salt.has_value());
  EXPECT_TRUE(scrypt_state->work_factor.has_value());
  EXPECT_TRUE(scrypt_state->block_size.has_value());
  EXPECT_TRUE(scrypt_state->parallel_factor.has_value());

  // DoubleWrappedCompatAuthBlockstate fail when TPM key is not present

  // Construct the vault keyset
  serialized.set_flags(SerializedVaultKeyset::SCRYPT_WRAPPED |
                       SerializedVaultKeyset::TPM_WRAPPED);
  auto vk3 = std::make_unique<VaultKeyset>();
  vk3->InitializeFromSerialized(serialized);
  vk3->SetWrappedKeyset(wrapped_keyset);

  // Test
  // Double scrypt fail test when tpm key is not set, failure in creating
  // sub-state TpmNotBoundToPcrAuthBlockState.
  EXPECT_CALL(keyset_management, GetVaultKeyset(_, _))
      .WillOnce(Return(ByMove(std::move(vk3))));
  auth_block_utility_impl_->GetAuthBlockStateFromVaultKeyset(
      credentials.key_data().label(), credentials.GetObfuscatedUsername(),
      out_state);
  EXPECT_FALSE(std::holds_alternative<DoubleWrappedCompatAuthBlockState>(
      out_state.state));

  // DoubleWrappedCompatAuthBlockstate success

  // Construct the vault keyset
  auto vk4 = std::make_unique<VaultKeyset>();
  vk4->InitializeFromSerialized(serialized);
  vk4->SetWrappedKeyset(wrapped_keyset);
  vk4->SetTPMKey(brillo::SecureBlob("tpmkey"));

  // Test
  EXPECT_CALL(keyset_management, GetVaultKeyset(_, _))
      .WillOnce(Return(ByMove(std::move(vk4))));
  auth_block_utility_impl_->GetAuthBlockStateFromVaultKeyset(
      credentials.key_data().label(), credentials.GetObfuscatedUsername(),
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
  auth_block_utility_impl_->GetAuthBlockStateFromVaultKeyset(
      credentials.key_data().label(), credentials.GetObfuscatedUsername(),
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
  auth_block_utility_impl_->GetAuthBlockStateFromVaultKeyset(
      credentials.key_data().label(), credentials.GetObfuscatedUsername(),
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
  auth_block_utility_impl_->GetAuthBlockStateFromVaultKeyset(
      credentials.key_data().label(), credentials.GetObfuscatedUsername(),
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
  crypto_.Init();
  MakeAuthBlockUtilityImpl();

  // Test for kScrypt
  EXPECT_CALL(hwsec_, IsEnabled()).WillRepeatedly(ReturnValue(false));
  EXPECT_CALL(hwsec_, IsReady()).WillRepeatedly(ReturnValue(false));
  CryptoStatusOr<AuthBlockType> type_without_tpm =
      auth_block_utility_impl_->GetAuthBlockTypeForCreation(
          /*is_le_credential =*/false, /*is_recovery=*/false,
          /*is_challenge_credential =*/false);
  if (USE_TPM_INSECURE_FALLBACK) {
    EXPECT_THAT(type_without_tpm, IsOkAndHolds(AuthBlockType::kScrypt));
  } else {
    EXPECT_THAT(type_without_tpm, NotOk());
  }

  // Test for kPinWeaver
  KeyData key_data;
  key_data.mutable_policy()->set_low_entropy_credential(true);
  credentials.set_key_data(key_data);
  EXPECT_THAT(auth_block_utility_impl_->GetAuthBlockTypeForCreation(
                  /*is_le_credential =*/true, /*is_recovery=*/false,
                  /*is_challenge_credential =*/false),
              IsOkAndHolds(AuthBlockType::kPinWeaver));

  // Test for kChallengeResponse
  KeyData key_data2;
  key_data2.set_type(KeyData::KEY_TYPE_CHALLENGE_RESPONSE);
  credentials.set_key_data(key_data2);
  EXPECT_THAT(auth_block_utility_impl_->GetAuthBlockTypeForCreation(
                  /*is_le_credential =*/false, /*is_recovery=*/false,
                  /*is_challenge_credential =*/true),
              IsOkAndHolds(AuthBlockType::kChallengeCredential));

  // Test for Tpm backed AuthBlock types.
  EXPECT_CALL(hwsec_, IsEnabled()).WillRepeatedly(ReturnValue(true));
  EXPECT_CALL(hwsec_, IsReady()).WillRepeatedly(ReturnValue(true));
  // credentials.key_data type shouldn't be challenge credential any more.
  KeyData key_data3;
  credentials.set_key_data(key_data3);

  // Test for kTpmEcc
  EXPECT_THAT(auth_block_utility_impl_->GetAuthBlockTypeForCreation(
                  /*is_le_credential =*/false, /*is_recovery=*/false,
                  /*is_challenge_credential =*/false),
              IsOkAndHolds(AuthBlockType::kTpmEcc));

  // Test for kTpmNotBoundToPcr (No TPM or no TPM2.0)
  EXPECT_CALL(hwsec_, IsSealingSupported()).WillOnce(ReturnValue(false));
  EXPECT_THAT(auth_block_utility_impl_->GetAuthBlockTypeForCreation(
                  /*is_le_credential =*/false, /*is_recovery=*/false,
                  /*is_challenge_credential =*/false),
              IsOkAndHolds(AuthBlockType::kTpmNotBoundToPcr));

  // Test for kTpmBoundToPcr (TPM2.0 but no support for ECC key)
  EXPECT_CALL(hwsec_, IsSealingSupported()).WillOnce(ReturnValue(true));
  EXPECT_CALL(cryptohome_keys_manager_, GetKeyLoader(CryptohomeKeyType::kECC))
      .WillOnce(Return(nullptr));
  EXPECT_THAT(auth_block_utility_impl_->GetAuthBlockTypeForCreation(
                  /*is_le_credential =*/false, /*is_recovery=*/false,
                  /*is_challenge_credential =*/false),
              IsOkAndHolds(AuthBlockType::kTpmBoundToPcr));

  // Test for kCryptohomeRecovery
  EXPECT_THAT(auth_block_utility_impl_->GetAuthBlockTypeForCreation(
                  /*is_le_credential =*/false, /*is_recovery=*/true,
                  /*is_challenge_credential =*/false),
              IsOkAndHolds(AuthBlockType::kCryptohomeRecovery));
}

TEST_F(AuthBlockUtilityImplTest, GetAsyncAuthBlockWithType) {
  brillo::SecureBlob passkey("passkey");
  Credentials credentials(kUser, passkey);
  crypto_.Init();

  MakeAuthBlockUtilityImpl();
  auth_block_utility_impl_->InitializeChallengeCredentialsHelper(
      &challenge_credentials_helper_, &key_challenge_service_factory_);
  EXPECT_CALL(key_challenge_service_factory_, New(kKeyDelegateDBusService))
      .WillOnce([](const std::string& bus_name) {
        return std::make_unique<MockKeyChallengeService>();
      });

  AuthInput auth_input{
      .username = kUser,
      .challenge_credential_auth_input =
          ChallengeCredentialAuthInput{
              .public_key_spki_der =
                  brillo::BlobFromString("public_key_spki_der"),
              .challenge_signature_algorithms =
                  {structure::ChallengeSignatureAlgorithm::
                       kRsassaPkcs1V15Sha256},
              .dbus_service_name = kKeyDelegateDBusService},
  };
  // Test. All fields are valid to get an AsyncChallengeCredentialAuthBlock.
  CryptoStatusOr<std::unique_ptr<AuthBlock>> auth_block =
      auth_block_utility_impl_->GetAsyncAuthBlockWithType(
          AuthBlockType::kChallengeCredential, auth_input);
  EXPECT_TRUE(auth_block.ok());
  EXPECT_NE(auth_block.value(), nullptr);
}

TEST_F(AuthBlockUtilityImplTest, GetAsyncAuthBlockWithTypeFail) {
  brillo::SecureBlob passkey("passkey");
  Credentials credentials(kUser, passkey);
  crypto_.Init();
  // Test. No valid dbus_service_name or username.
  MakeAuthBlockUtilityImpl();

  AuthInput auth_input;
  CryptoStatusOr<std::unique_ptr<AuthBlock>> auth_block =
      auth_block_utility_impl_->GetAsyncAuthBlockWithType(
          AuthBlockType::kChallengeCredential, auth_input);
  EXPECT_FALSE(auth_block.ok());
}

// Test that PrepareAuthBlockForRemoval succeeds for
// CryptohomeRecoveryAuthBlock.
TEST_F(AuthBlockUtilityImplTest,
       RemoveCryptohomeRecoveryWithoutRevocationAuthBlock) {
  CryptohomeRecoveryAuthBlockState recovery_state = {
      .hsm_payload = brillo::SecureBlob("hsm_payload"),
      .encrypted_destination_share =
          brillo::SecureBlob("encrypted_destination_share"),
      .channel_pub_key = brillo::SecureBlob("channel_pub_key"),
      .encrypted_channel_priv_key =
          brillo::SecureBlob("encrypted_channel_priv_key"),
  };
  AuthBlockState auth_state = {.state = recovery_state};

  MakeAuthBlockUtilityImpl();

  EXPECT_TRUE(
      auth_block_utility_impl_->PrepareAuthBlockForRemoval(auth_state).ok());
}

// Test that PrepareAuthBlockForRemoval succeeds for CryptohomeRecoveryAuthBlock
// with credentials revocation enabled.
TEST_F(AuthBlockUtilityImplTest,
       RemoveCryptohomeRecoveryWithRevocationAuthBlock) {
  ON_CALL(hwsec_, IsPinWeaverEnabled()).WillByDefault(ReturnValue(true));
  MockLECredentialManager* le_cred_manager = new MockLECredentialManager();
  uint64_t fake_label = 11;
  EXPECT_CALL(*le_cred_manager, RemoveCredential(fake_label))
      .WillOnce(ReturnError<CryptohomeLECredError>());
  crypto_.set_le_manager_for_testing(
      std::unique_ptr<cryptohome::LECredentialManager>(le_cred_manager));
  crypto_.Init();

  CryptohomeRecoveryAuthBlockState recovery_state = {
      .hsm_payload = brillo::SecureBlob("hsm_payload"),
      .encrypted_destination_share =
          brillo::SecureBlob("encrypted_destination_share"),
      .channel_pub_key = brillo::SecureBlob("channel_pub_key"),
      .encrypted_channel_priv_key =
          brillo::SecureBlob("encrypted_channel_priv_key"),
  };
  RevocationState revocation_state = {
      .le_label = fake_label,
  };
  AuthBlockState auth_state = {
      .state = recovery_state,
      .revocation_state = revocation_state,
  };

  MakeAuthBlockUtilityImpl();

  EXPECT_TRUE(
      auth_block_utility_impl_->PrepareAuthBlockForRemoval(auth_state).ok());
}

class AuthBlockUtilityImplRecoveryTest : public AuthBlockUtilityImplTest {
 public:
  AuthBlockUtilityImplRecoveryTest() = default;
  ~AuthBlockUtilityImplRecoveryTest() = default;

  void SetUp() override {
    AuthBlockUtilityImplTest::SetUp();
    brillo::SecureBlob mediator_pub_key;
    ASSERT_TRUE(
        cryptorecovery::FakeRecoveryMediatorCrypto::GetFakeMediatorPublicKey(
            &mediator_pub_key));
    cryptorecovery::CryptoRecoveryEpochResponse epoch_response;
    ASSERT_TRUE(
        cryptorecovery::FakeRecoveryMediatorCrypto::GetFakeEpochResponse(
            &epoch_response));
    epoch_response_blob_ =
        brillo::BlobFromString(epoch_response.SerializeAsString());
    auto recovery = cryptorecovery::RecoveryCryptoImpl::Create(
        recovery_crypto_fake_backend_.get(), &platform_);
    ASSERT_TRUE(recovery);

    cryptorecovery::HsmPayload hsm_payload;
    brillo::SecureBlob recovery_key;
    cryptorecovery::GenerateHsmPayloadRequest generate_hsm_payload_request(
        {.mediator_pub_key = mediator_pub_key,
         .onboarding_metadata = cryptorecovery::OnboardingMetadata{},
         .obfuscated_username = "obfuscated_username"});
    cryptorecovery::GenerateHsmPayloadResponse generate_hsm_payload_response;
    EXPECT_TRUE(recovery->GenerateHsmPayload(generate_hsm_payload_request,
                                             &generate_hsm_payload_response));
    rsa_priv_key_ = generate_hsm_payload_response.encrypted_rsa_priv_key;
    destination_share_ =
        generate_hsm_payload_response.encrypted_destination_share;
    channel_pub_key_ = generate_hsm_payload_response.channel_pub_key;
    channel_priv_key_ =
        generate_hsm_payload_response.encrypted_channel_priv_key;
    recovery_key = generate_hsm_payload_response.recovery_key;
    EXPECT_TRUE(SerializeHsmPayloadToCbor(
        generate_hsm_payload_response.hsm_payload, &hsm_payload_));

    crypto_.Init();
    MakeAuthBlockUtilityImpl();
  }

 protected:
  CryptohomeRecoveryAuthBlockState GetAuthBlockState() {
    return {
        .hsm_payload = hsm_payload_,
        .encrypted_destination_share = destination_share_,
        .channel_pub_key = channel_pub_key_,
        .encrypted_channel_priv_key = channel_priv_key_,
    };
  }

  brillo::SecureBlob hsm_payload_;
  brillo::SecureBlob rsa_priv_key_;
  brillo::SecureBlob channel_pub_key_;
  brillo::SecureBlob channel_priv_key_;
  brillo::SecureBlob destination_share_;
  brillo::Blob epoch_response_blob_;
  FakePlatform platform_;
};

TEST_F(AuthBlockUtilityImplRecoveryTest, GenerateRecoveryRequestSuccess) {
  brillo::SecureBlob ephemeral_pub_key, recovery_request;
  CryptoStatus status = auth_block_utility_impl_->GenerateRecoveryRequest(
      "obfuscated_username", cryptorecovery::RequestMetadata{},
      epoch_response_blob_, GetAuthBlockState(), crypto_.GetRecoveryCrypto(),
      &recovery_request, &ephemeral_pub_key);
  EXPECT_TRUE(status.ok());
  EXPECT_FALSE(ephemeral_pub_key.empty());
  EXPECT_FALSE(recovery_request.empty());
}

TEST_F(AuthBlockUtilityImplRecoveryTest, GenerateRecoveryRequestNoHsmPayload) {
  brillo::SecureBlob ephemeral_pub_key, recovery_request;
  auto state = GetAuthBlockState();
  state.hsm_payload = brillo::SecureBlob();
  CryptoStatus status = auth_block_utility_impl_->GenerateRecoveryRequest(
      "obfuscated_username", cryptorecovery::RequestMetadata{},
      epoch_response_blob_, state, crypto_.GetRecoveryCrypto(),
      &recovery_request, &ephemeral_pub_key);
  EXPECT_FALSE(status.ok());
}

TEST_F(AuthBlockUtilityImplRecoveryTest,
       GenerateRecoveryRequestNoChannelPubKey) {
  brillo::SecureBlob ephemeral_pub_key, recovery_request;
  auto state = GetAuthBlockState();
  state.channel_pub_key = brillo::SecureBlob();
  CryptoStatus status = auth_block_utility_impl_->GenerateRecoveryRequest(
      "obfuscated_username", cryptorecovery::RequestMetadata{},
      epoch_response_blob_, state, crypto_.GetRecoveryCrypto(),
      &recovery_request, &ephemeral_pub_key);
  EXPECT_FALSE(status.ok());
}

TEST_F(AuthBlockUtilityImplRecoveryTest,
       GenerateRecoveryRequestNoChannelPrivKey) {
  brillo::SecureBlob ephemeral_pub_key, recovery_request;
  auto state = GetAuthBlockState();
  state.encrypted_channel_priv_key = brillo::SecureBlob();
  CryptoStatus status = auth_block_utility_impl_->GenerateRecoveryRequest(
      "obfuscated_username", cryptorecovery::RequestMetadata{},
      epoch_response_blob_, state, crypto_.GetRecoveryCrypto(),
      &recovery_request, &ephemeral_pub_key);
  EXPECT_FALSE(status.ok());
}

TEST_F(AuthBlockUtilityImplRecoveryTest,
       GenerateRecoveryRequestNoEpochResponse) {
  brillo::SecureBlob ephemeral_pub_key, recovery_request;
  CryptoStatus status = auth_block_utility_impl_->GenerateRecoveryRequest(
      "obfuscated_username", cryptorecovery::RequestMetadata{},
      /*epoch_response=*/brillo::Blob(), GetAuthBlockState(),
      crypto_.GetRecoveryCrypto(), &recovery_request, &ephemeral_pub_key);
  EXPECT_FALSE(status.ok());
}

}  // namespace cryptohome
