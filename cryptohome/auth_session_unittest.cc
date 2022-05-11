// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Unit tests for AuthSession.

#include "cryptohome/auth_session.h"

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <base/callback_helpers.h>
#include <base/test/bind.h>
#include <base/test/task_environment.h>
#include <base/timer/mock_timer.h>
#include <brillo/cryptohome.h>
#include <cryptohome/proto_bindings/auth_factor.pb.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>
#include <gtest/gtest.h>
#include <libhwsec-foundation/crypto/aes.h>
#include <libhwsec-foundation/error/testing_helper.h>

#include "cryptohome/auth_blocks/auth_block_state.h"
#include "cryptohome/auth_blocks/auth_block_utility_impl.h"
#include "cryptohome/auth_blocks/mock_auth_block_utility.h"
#include "cryptohome/auth_factor/auth_factor.h"
#include "cryptohome/auth_factor/auth_factor_manager.h"
#include "cryptohome/auth_factor/auth_factor_metadata.h"
#include "cryptohome/auth_factor/auth_factor_type.h"
#include "cryptohome/crypto_error.h"
#include "cryptohome/cryptohome_common.h"
#include "cryptohome/cryptorecovery/recovery_crypto_fake_tpm_backend_impl.h"
#include "cryptohome/key_objects.h"
#include "cryptohome/mock_crypto.h"
#include "cryptohome/mock_cryptohome_keys_manager.h"
#include "cryptohome/mock_keyset_management.h"
#include "cryptohome/mock_platform.h"
#include "cryptohome/mock_tpm.h"
#include "cryptohome/user_secret_stash.h"
#include "cryptohome/user_secret_stash_storage.h"

using brillo::cryptohome::home::SanitizeUserName;
using cryptohome::error::CryptohomeCryptoError;
using cryptohome::error::CryptohomeError;
using cryptohome::error::CryptohomeMountError;
using hwsec_foundation::error::testing::ReturnError;
using hwsec_foundation::status::MakeStatus;
using hwsec_foundation::status::OkStatus;
using hwsec_foundation::status::StatusChain;
using ::testing::_;
using ::testing::ByMove;
using ::testing::ElementsAre;
using ::testing::NiceMock;
using ::testing::Pair;
using ::testing::Return;

namespace {
// Fake labels to be in used in this test suite.
constexpr char kFakeLabel[] = "test_label";
constexpr char kFakeOtherLabel[] = "test_other_label";
constexpr char kFakePinLabel[] = "test_pin_label";
// Fake passwords to be in used in this test suite.
constexpr char kFakePass[] = "test_pass";
constexpr char kFakePin[] = "123456";
constexpr char kFakeOtherPass[] = "test_other_pass";
// Fake username to be used in this test suite.
constexpr char kFakeUsername[] = "test_username";

}  // namespace

namespace cryptohome {

class AuthSessionTest : public ::testing::Test {
 public:
  AuthSessionTest() = default;
  AuthSessionTest(const AuthSessionTest&) = delete;
  AuthSessionTest& operator=(const AuthSessionTest&) = delete;
  ~AuthSessionTest() override = default;

  void SetUp() override {
    EXPECT_CALL(tpm_, IsEnabled()).WillRepeatedly(Return(true));
    EXPECT_CALL(tpm_, IsOwned()).WillRepeatedly(Return(true));
    crypto_.Init(&tpm_, &cryptohome_keys_manager_);
  }

 protected:
  base::test::SingleThreadTaskEnvironment task_environment_;
  // Mock and fake objects, will be passed to AuthSession for its internal use.
  NiceMock<MockTpm> tpm_;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager_;
  NiceMock<MockCrypto> crypto_;
  NiceMock<MockPlatform> platform_;
  NiceMock<MockKeysetManagement> keyset_management_;
  NiceMock<MockAuthBlockUtility> auth_block_utility_;
  AuthFactorManager auth_factor_manager_{&platform_};
  UserSecretStashStorage user_secret_stash_storage_{&platform_};
};

const CryptohomeError::ErrorLocationPair kErrorLocationForTestingAuthSession =
    CryptohomeError::ErrorLocationPair(
        static_cast<::cryptohome::error::CryptohomeError::ErrorLocation>(1),
        std::string("MockErrorLocationAuthSession"));

TEST_F(AuthSessionTest, Username) {
  AuthSession auth_session(
      kFakeUsername, user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE,
      /*on_timeout=*/base::DoNothing(), &crypto_, &keyset_management_,
      &auth_block_utility_, &auth_factor_manager_, &user_secret_stash_storage_);

  EXPECT_EQ(auth_session.username(), kFakeUsername);
  EXPECT_EQ(auth_session.obfuscated_username(),
            SanitizeUserName(kFakeUsername));
}

TEST_F(AuthSessionTest, TimeoutTest) {
  bool called = false;
  auto on_timeout = base::BindOnce(
      [](bool* called, const base::UnguessableToken&) { *called = true; },
      base::Unretained(&called));
  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;
  AuthSession auth_session(kFakeUsername, flags, std::move(on_timeout),
                           &crypto_, &keyset_management_, &auth_block_utility_,
                           &auth_factor_manager_, &user_secret_stash_storage_);
  EXPECT_EQ(auth_session.GetStatus(),
            AuthStatus::kAuthStatusFurtherFactorRequired);
  auth_session.SetAuthSessionAsAuthenticated();
  ASSERT_TRUE(auth_session.timer_.IsRunning());
  auth_session.timer_.FireNow();
  EXPECT_EQ(auth_session.GetStatus(), AuthStatus::kAuthStatusTimedOut);
  EXPECT_TRUE(called);
}

TEST_F(AuthSessionTest, SerializedStringFromNullToken) {
  base::UnguessableToken token = base::UnguessableToken::Null();
  std::optional<std::string> serialized_token =
      AuthSession::GetSerializedStringFromToken(token);
  EXPECT_FALSE(serialized_token.has_value());
}

TEST_F(AuthSessionTest, TokenFromEmptyString) {
  std::string serialized_string = "";
  std::optional<base::UnguessableToken> unguessable_token =
      AuthSession::GetTokenFromSerializedString(serialized_string);
  EXPECT_FALSE(unguessable_token.has_value());
}

TEST_F(AuthSessionTest, TokenFromUnexpectedSize) {
  std::string serialized_string = "unexpected_sized_string";
  std::optional<base::UnguessableToken> unguessable_token =
      AuthSession::GetTokenFromSerializedString(serialized_string);
  EXPECT_FALSE(unguessable_token.has_value());
}

TEST_F(AuthSessionTest, TokenFromString) {
  base::UnguessableToken original_token = base::UnguessableToken::Create();
  std::optional<std::string> serialized_token =
      AuthSession::GetSerializedStringFromToken(original_token);
  EXPECT_TRUE(serialized_token.has_value());
  std::optional<base::UnguessableToken> deserialized_token =
      AuthSession::GetTokenFromSerializedString(serialized_token.value());
  EXPECT_TRUE(deserialized_token.has_value());
  EXPECT_EQ(deserialized_token.value(), original_token);
}

// This test check AuthSession::GetCredential function for a regular user and
// ensures that the fields are set as they should be.
TEST_F(AuthSessionTest, GetCredentialRegularUser) {
  // SETUP
  bool called = false;
  auto on_timeout = base::BindOnce(
      [](bool* called, const base::UnguessableToken&) { *called = true; },
      base::Unretained(&called));
  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;
  AuthSession auth_session(kFakeUsername, flags, std::move(on_timeout),
                           &crypto_, &keyset_management_, &auth_block_utility_,
                           &auth_factor_manager_, &user_secret_stash_storage_);
  EXPECT_EQ(auth_session.GetStatus(),
            AuthStatus::kAuthStatusFurtherFactorRequired);

  // TEST
  EXPECT_FALSE(called);
  cryptohome::AuthorizationRequest authorization_request;
  authorization_request.mutable_key()->set_secret(kFakePass);
  authorization_request.mutable_key()->mutable_data()->set_label(kFakeLabel);
  MountStatusOr<std::unique_ptr<Credentials>> test_creds =
      auth_session.GetCredentials(authorization_request);
  ASSERT_TRUE(test_creds.ok());

  // VERIFY
  // SerializeToString is used in the absence of a comparator for KeyData
  // protobuf.
  std::string key_data_serialized1 = "1";
  std::string key_data_serialized2 = "2";
  test_creds.value()->key_data().SerializeToString(&key_data_serialized1);
  authorization_request.mutable_key()->data().SerializeToString(
      &key_data_serialized2);
  EXPECT_EQ(key_data_serialized1, key_data_serialized2);
}

// This test check AuthSession::GetCredential function for a kiosk user and
// ensures that the fields are set as they should be.
TEST_F(AuthSessionTest, GetCredentialKioskUser) {
  // SETUP
  bool called = false;
  auto on_timeout = base::BindOnce(
      [](bool* called, const base::UnguessableToken&) { *called = true; },
      base::Unretained(&called));
  // SecureBlob for kFakePass above
  const brillo::SecureBlob fake_pass_blob(
      brillo::BlobFromString(kFakeUsername));

  AuthSession auth_session(kFakeUsername, 0, std::move(on_timeout), &crypto_,
                           &keyset_management_, &auth_block_utility_,
                           &auth_factor_manager_, &user_secret_stash_storage_);
  EXPECT_CALL(keyset_management_, GetPublicMountPassKey(_))
      .WillOnce(Return(ByMove(fake_pass_blob)));
  EXPECT_EQ(auth_session.GetStatus(),
            AuthStatus::kAuthStatusFurtherFactorRequired);

  // TEST
  EXPECT_FALSE(called);
  cryptohome::AuthorizationRequest authorization_request;
  authorization_request.mutable_key()->mutable_data()->set_label(kFakeLabel);
  authorization_request.mutable_key()->mutable_data()->set_type(
      KeyData::KEY_TYPE_KIOSK);
  MountStatusOr<std::unique_ptr<Credentials>> test_creds =
      auth_session.GetCredentials(authorization_request);
  ASSERT_TRUE(test_creds.ok());

  // VERIFY
  // SerializeToString is used in the absence of a comparator for KeyData
  // protobuf.
  std::string key_data_serialized1 = "1";
  std::string key_data_serialized2 = "2";
  test_creds.value()->key_data().SerializeToString(&key_data_serialized1);
  authorization_request.mutable_key()->data().SerializeToString(
      &key_data_serialized2);
  EXPECT_EQ(key_data_serialized1, key_data_serialized2);
  EXPECT_EQ(test_creds.value()->passkey(), fake_pass_blob);
}

// Test if AuthSession correctly adds new credentials for a new user.
TEST_F(AuthSessionTest, AddCredentialNewUser) {
  // Setup.
  bool called = false;
  auto on_timeout = base::BindOnce(
      [](bool& called, const base::UnguessableToken&) { called = true; },
      std::ref(called));
  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;
  // Setting the expectation that the user does not exist.
  EXPECT_CALL(keyset_management_, UserExists(_)).WillRepeatedly(Return(false));
  // For AuthSession::AddInitialKeyset/AddKeyset callback to properly
  // execute auth_block_utility_ cannot be a mock
  std::unique_ptr<AuthBlockUtilityImpl> auth_block_utility_impl_ =
      std::make_unique<AuthBlockUtilityImpl>(&keyset_management_, &crypto_,
                                             &platform_);
  // Setting the expectation that the user does not exist.
  AuthSession auth_session(kFakeUsername, flags, std::move(on_timeout),
                           &crypto_, &keyset_management_,
                           auth_block_utility_impl_.get(),
                           &auth_factor_manager_, &user_secret_stash_storage_);

  // Test.
  EXPECT_THAT(AuthStatus::kAuthStatusFurtherFactorRequired,
              auth_session.GetStatus());
  EXPECT_FALSE(auth_session.user_exists());

  user_data_auth::AddCredentialsRequest add_cred_request;
  cryptohome::AuthorizationRequest* authorization_request =
      add_cred_request.mutable_authorization();
  authorization_request->mutable_key()->set_secret(kFakePass);
  authorization_request->mutable_key()->mutable_data()->set_label(kFakeLabel);

  EXPECT_CALL(keyset_management_,
              AddInitialKeysetWithKeyBlobs(_, _, _, _, _, _))
      .WillOnce(Return(ByMove(std::make_unique<VaultKeyset>())));

  base::OnceCallback<void(const user_data_auth::AddCredentialsReply&)> on_done =
      base::BindLambdaForTesting(
          [](const user_data_auth::AddCredentialsReply& reply) {
            // Evaluate error returned by callback.
            EXPECT_EQ(user_data_auth::CRYPTOHOME_ERROR_NOT_SET, reply.error());
          });

  // Verify.
  EXPECT_TRUE(auth_session.OnUserCreated().ok());
  ASSERT_TRUE(auth_session.timer_.IsRunning());

  EXPECT_EQ(auth_session.GetStatus(), AuthStatus::kAuthStatusAuthenticated);
  auth_session.AddCredentials(add_cred_request, std::move(on_done));
  EXPECT_EQ(auth_session.GetStatus(), AuthStatus::kAuthStatusAuthenticated);
}

// Test if AuthSession correctly adds new credentials for a new user, even when
// called twice. The first credential gets added as an initial keyset, and the
// second as a regular one.
TEST_F(AuthSessionTest, AddCredentialNewUserTwice) {
  // Setup.
  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;
  // For AuthSession::AddInitialKeyset/AddKeyset callback to properly
  // execute auth_block_utility_ cannot be a mock
  std::unique_ptr<AuthBlockUtilityImpl> auth_block_utility_impl_ =
      std::make_unique<AuthBlockUtilityImpl>(&keyset_management_, &crypto_,
                                             &platform_);
  // Setting the expectation that the user does not exist.
  EXPECT_CALL(keyset_management_, UserExists(_)).WillRepeatedly(Return(false));
  AuthSession auth_session(kFakeUsername, flags,
                           /*on_timeout=*/base::DoNothing(), &crypto_,
                           &keyset_management_, auth_block_utility_impl_.get(),
                           &auth_factor_manager_, &user_secret_stash_storage_);

  base::OnceCallback<void(const user_data_auth::AddCredentialsReply&)> on_done =
      base::BindLambdaForTesting(
          [](const user_data_auth::AddCredentialsReply& reply) {
            // Evaluate error returned by callback.
            EXPECT_EQ(user_data_auth::CRYPTOHOME_ERROR_NOT_SET, reply.error());
          });

  // Test adding the first credential.
  EXPECT_THAT(AuthStatus::kAuthStatusFurtherFactorRequired,
              auth_session.GetStatus());
  EXPECT_FALSE(auth_session.user_exists());

  user_data_auth::AddCredentialsRequest add_cred_request;
  cryptohome::AuthorizationRequest* authorization_request =
      add_cred_request.mutable_authorization();
  authorization_request->mutable_key()->set_secret(kFakePass);
  authorization_request->mutable_key()->mutable_data()->set_label(kFakeLabel);

  EXPECT_CALL(keyset_management_,
              AddInitialKeysetWithKeyBlobs(_, _, _, _, _, _))
      .WillOnce(Return(ByMove(std::make_unique<VaultKeyset>())));

  EXPECT_TRUE(auth_session.OnUserCreated().ok());
  ASSERT_TRUE(auth_session.timer_.IsRunning());

  EXPECT_EQ(auth_session.GetStatus(), AuthStatus::kAuthStatusAuthenticated);
  auth_session.AddCredentials(add_cred_request, std::move(on_done));
  EXPECT_EQ(auth_session.GetStatus(), AuthStatus::kAuthStatusAuthenticated);
  // Test adding the second credential.
  // Set up expectation in callback for success.
  base::OnceCallback<void(const user_data_auth::AddCredentialsReply&)>
      other_on_done = base::BindLambdaForTesting(
          [](const user_data_auth::AddCredentialsReply& reply) {
            // Evaluate error returned by callback.
            EXPECT_EQ(user_data_auth::CRYPTOHOME_ERROR_NOT_SET, reply.error());
          });
  user_data_auth::AddCredentialsRequest add_other_cred_request;
  cryptohome::AuthorizationRequest* other_authorization_request =
      add_other_cred_request.mutable_authorization();
  other_authorization_request->mutable_key()->set_secret(kFakeOtherPass);
  other_authorization_request->mutable_key()->mutable_data()->set_label(
      kFakeOtherLabel);

  EXPECT_CALL(keyset_management_, AddKeysetWithKeyBlobs(_, _, _, _, _, _))
      .WillOnce(Return(CRYPTOHOME_ERROR_NOT_SET));
  auth_session.AddCredentials(add_other_cred_request, std::move(other_on_done));
  EXPECT_EQ(auth_session.GetStatus(), AuthStatus::kAuthStatusAuthenticated);
  ASSERT_TRUE(auth_session.timer_.IsRunning());
}

// Test if AuthSession correctly authenticates existing credentials for a
// user.
TEST_F(AuthSessionTest, AuthenticateExistingUser) {
  // Setup.
  bool called_timeout = false;
  auto on_timeout = base::BindOnce(
      [](bool& called_timeout, const base::UnguessableToken&) {
        called_timeout = true;
      },
      std::ref(called_timeout));
  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;
  // Setting the expectation that the user does not exist.
  EXPECT_CALL(keyset_management_, UserExists(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(keyset_management_, GetVaultKeysetLabelsAndData(_, _));
  AuthSession auth_session(kFakeUsername, flags, std::move(on_timeout),
                           &crypto_, &keyset_management_, &auth_block_utility_,
                           &auth_factor_manager_, &user_secret_stash_storage_);

  // Test.
  EXPECT_THAT(AuthStatus::kAuthStatusFurtherFactorRequired,
              auth_session.GetStatus());
  EXPECT_TRUE(auth_session.user_exists());

  cryptohome::AuthorizationRequest authorization_request;
  authorization_request.mutable_key()->set_secret(kFakePass);
  authorization_request.mutable_key()->mutable_data()->set_label(kFakeLabel);

  EXPECT_CALL(auth_block_utility_, GetAuthBlockTypeForDerivation(_, _))
      .WillOnce(Return(AuthBlockType::kTpmBoundToPcr));
  EXPECT_CALL(auth_block_utility_, GetAuthBlockStateFromVaultKeyset(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(keyset_management_, GetValidKeysetWithKeyBlobs(_, _, _))
      .WillOnce(Return(ByMove(std::make_unique<VaultKeyset>())));
  EXPECT_CALL(keyset_management_, ShouldReSaveKeyset(_))
      .WillOnce(Return(false));

  auto key_blobs = std::make_unique<KeyBlobs>();
  EXPECT_CALL(auth_block_utility_, DeriveKeyBlobsWithAuthBlockAsync(_, _, _, _))
      .WillOnce([&](AuthBlockType auth_block_type, const AuthInput& auth_input,
                    const AuthBlockState& auth_state,
                    AuthBlock::DeriveCallback derive_callback) {
        std::move(derive_callback)
            .Run(OkStatus<CryptohomeCryptoError>(), std::move(key_blobs));
        return true;
      });

  bool called = false;
  user_data_auth::CryptohomeErrorCode error =
      user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
  auth_session.Authenticate(
      authorization_request,
      base::BindOnce(
          [](bool& called, user_data_auth::CryptohomeErrorCode& error,
             const user_data_auth::AuthenticateAuthSessionReply& reply) {
            called = true;
            error = reply.error();
            // Evaluate error returned by callback.
            EXPECT_EQ(user_data_auth::CRYPTOHOME_ERROR_NOT_SET, reply.error());
          },
          std::ref(called), std::ref(error)));

  // Verify.
  EXPECT_TRUE(called);
  ASSERT_TRUE(auth_session.timer_.IsRunning());

  EXPECT_EQ(AuthStatus::kAuthStatusAuthenticated, auth_session.GetStatus());
  EXPECT_TRUE(auth_session.TakeCredentialVerifier()->Verify(
      brillo::SecureBlob(kFakePass)));

  // Cleanup.
  auth_session.timer_.FireNow();
  EXPECT_THAT(AuthStatus::kAuthStatusTimedOut, auth_session.GetStatus());
  EXPECT_TRUE(called);
}

// AuthSession fails authentication, test for failure reply code
// and ensure |credential_verifier_| is not set.
TEST_F(AuthSessionTest, AuthenticateExistingUserFailure) {
  // Setup.
  auto on_timeout = base::DoNothing();
  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;
  // Setting the expectation that the user does not exist.
  std::string obfuscated_username = SanitizeUserName(kFakeUsername);
  EXPECT_CALL(keyset_management_, UserExists(obfuscated_username))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(keyset_management_,
              GetVaultKeysetLabelsAndData(obfuscated_username, _));
  AuthSession auth_session(kFakeUsername, flags, std::move(on_timeout),
                           &crypto_, &keyset_management_, &auth_block_utility_,
                           &auth_factor_manager_, &user_secret_stash_storage_);

  // Test.
  EXPECT_THAT(AuthStatus::kAuthStatusFurtherFactorRequired,
              auth_session.GetStatus());
  EXPECT_TRUE(auth_session.user_exists());

  cryptohome::AuthorizationRequest authorization_request;
  authorization_request.mutable_key()->set_secret(kFakePass);
  authorization_request.mutable_key()->mutable_data()->set_label(kFakeLabel);

  EXPECT_CALL(auth_block_utility_, GetAuthBlockTypeForDerivation(_, _))
      .WillOnce(Return(AuthBlockType::kTpmBoundToPcr));
  EXPECT_CALL(auth_block_utility_, GetAuthBlockStateFromVaultKeyset(_, _, _))
      .WillOnce(Return(true));

  // Failure is achieved by having the callback return an empty key_blobs
  // and a CryptohomeCryptoError.
  auto key_blobs = nullptr;
  EXPECT_CALL(auth_block_utility_, DeriveKeyBlobsWithAuthBlockAsync(_, _, _, _))
      .WillOnce([&](AuthBlockType auth_block_type, const AuthInput& auth_input,
                    const AuthBlockState& auth_state,
                    AuthBlock::DeriveCallback derive_callback) {
        std::move(derive_callback)
            .Run(MakeStatus<CryptohomeCryptoError>(
                     kErrorLocationForTestingAuthSession,
                     error::ErrorActionSet(
                         {error::ErrorAction::kDevCheckUnexpectedState}),
                     CryptoError::CE_TPM_FATAL),
                 std::move(key_blobs));
        return true;
      });

  bool called = false;
  user_data_auth::CryptohomeErrorCode error =
      user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
  auth_session.Authenticate(
      authorization_request,
      base::BindOnce(
          [](bool& called, user_data_auth::CryptohomeErrorCode& error,
             const user_data_auth::AuthenticateAuthSessionReply& reply) {
            called = true;
            error = reply.error();
            // Evaluate error returned by callback.
            EXPECT_EQ(user_data_auth::CRYPTOHOME_ERROR_VAULT_UNRECOVERABLE,
                      reply.error());
          },
          std::ref(called), std::ref(error)));

  // Verify, should not be authenticated and CredentialVerifier should not be
  // set.
  EXPECT_TRUE(called);
  ASSERT_FALSE(auth_session.timer_.IsRunning());

  EXPECT_EQ(AuthStatus::kAuthStatusFurtherFactorRequired,
            auth_session.GetStatus());
  EXPECT_EQ(auth_session.TakeCredentialVerifier(), nullptr);
}

// Test if AuthSession::Addcredentials skips adding/saving credential to disk
// for an ephemeral user.
TEST_F(AuthSessionTest, AddCredentialNewEphemeralUser) {
  // Setup.
  bool called = false;
  auto on_timeout = base::BindOnce(
      [](bool& called, const base::UnguessableToken&) { called = true; },
      std::ref(called));
  int flags =
      user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_EPHEMERAL_USER;
  // Setting the expectation that the user does not exist.
  EXPECT_CALL(keyset_management_, UserExists(_)).WillRepeatedly(Return(false));
  AuthSession auth_session(kFakeUsername, flags, std::move(on_timeout),
                           &crypto_, &keyset_management_, &auth_block_utility_,
                           &auth_factor_manager_, &user_secret_stash_storage_);

  // Test.
  EXPECT_THAT(AuthStatus::kAuthStatusAuthenticated, auth_session.GetStatus());
  EXPECT_FALSE(auth_session.user_exists());
  ASSERT_TRUE(auth_session.timer_.IsRunning());

  user_data_auth::AddCredentialsRequest add_cred_request;
  cryptohome::AuthorizationRequest* authorization_request =
      add_cred_request.mutable_authorization();
  authorization_request->mutable_key()->set_secret(kFakePass);
  authorization_request->mutable_key()->mutable_data()->set_label(kFakeLabel);

  EXPECT_CALL(keyset_management_,
              AddInitialKeysetWithKeyBlobs(_, _, _, _, _, _))
      .Times(0);

  base::OnceCallback<void(const user_data_auth::AddCredentialsReply&)> on_done =
      base::BindLambdaForTesting(
          [](const user_data_auth::AddCredentialsReply& reply) {
            // Evaluate error returned by callback.
            EXPECT_EQ(user_data_auth::CRYPTOHOME_ERROR_NOT_SET, reply.error());
          });

  // Verify.
  auth_session.AddCredentials(add_cred_request, std::move(on_done));
  EXPECT_EQ(auth_session.GetStatus(), AuthStatus::kAuthStatusAuthenticated);
}

// Test if AuthSession correctly updates existing credentials for a new user.
TEST_F(AuthSessionTest, UpdateCredentialUnauthenticatedAuthSession) {
  // Setup.
  bool called = false;
  auto on_timeout = base::BindOnce(
      [](bool& called, const base::UnguessableToken&) { called = true; },
      std::ref(called));
  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;
  // Setting the expectation that the user does exist.
  EXPECT_CALL(keyset_management_, UserExists(_)).WillRepeatedly(Return(true));
  AuthSession auth_session(kFakeUsername, flags, std::move(on_timeout),
                           &crypto_, &keyset_management_, &auth_block_utility_,
                           &auth_factor_manager_, &user_secret_stash_storage_);
  user_data_auth::UpdateCredentialRequest update_cred_request;
  cryptohome::AuthorizationRequest* authorization_request =
      update_cred_request.mutable_authorization();
  authorization_request->mutable_key()->set_secret(kFakePass);
  authorization_request->mutable_key()->mutable_data()->set_label(kFakeLabel);
  update_cred_request.set_old_credential_label(kFakeLabel);

  // Test.
  base::OnceCallback<void(const user_data_auth::UpdateCredentialReply&)>
      on_done = base::BindLambdaForTesting(
          [](const user_data_auth::UpdateCredentialReply& reply) {
            // Evaluate error returned by callback.
            EXPECT_EQ(
                user_data_auth::CRYPTOHOME_ERROR_UNAUTHENTICATED_AUTH_SESSION,
                reply.error());
          });
  auth_session.UpdateCredential(update_cred_request, std::move(on_done));
}

// Test if AuthSession correctly updates existing credentials for a new user.
TEST_F(AuthSessionTest, UpdateCredentialSuccess) {
  // Setup.
  bool called = false;
  auto on_timeout = base::BindOnce(
      [](bool& called, const base::UnguessableToken&) { called = true; },
      std::ref(called));
  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;
  // For AuthSession::UpdateKeyset callback to properly
  // execute auth_block_utility_ cannot be a mock
  std::unique_ptr<AuthBlockUtilityImpl> auth_block_utility_impl_ =
      std::make_unique<AuthBlockUtilityImpl>(&keyset_management_, &crypto_,
                                             &platform_);

  // Setting the expectation that the user does exist.
  EXPECT_CALL(keyset_management_, UserExists(_)).WillRepeatedly(Return(true));
  AuthSession auth_session(kFakeUsername, flags, std::move(on_timeout),
                           &crypto_, &keyset_management_,
                           auth_block_utility_impl_.get(),
                           &auth_factor_manager_, &user_secret_stash_storage_);
  auth_session.SetStatus(AuthStatus::kAuthStatusAuthenticated);
  user_data_auth::UpdateCredentialRequest update_cred_request;
  cryptohome::AuthorizationRequest* authorization_request =
      update_cred_request.mutable_authorization();
  authorization_request->mutable_key()->set_secret(kFakePass);
  authorization_request->mutable_key()->mutable_data()->set_label(kFakeLabel);
  update_cred_request.set_old_credential_label(kFakeLabel);

  // Test.
  EXPECT_CALL(keyset_management_, UpdateKeysetWithKeyBlobs(_, _, _, _, _))
      .WillOnce(Return(CRYPTOHOME_ERROR_NOT_SET));
  base::OnceCallback<void(const user_data_auth::UpdateCredentialReply&)>
      on_done = base::BindLambdaForTesting(
          [](const user_data_auth::UpdateCredentialReply& reply) {
            // Evaluate error returned by callback.
            EXPECT_EQ(user_data_auth::CRYPTOHOME_ERROR_NOT_SET, reply.error());
          });
  auth_session.UpdateCredential(update_cred_request, std::move(on_done));
}
// Test if AuthSession correctly updates existing credentials for a new user.
TEST_F(AuthSessionTest, UpdateCredentialInvalidLabel) {
  // Setup.
  bool called = false;
  auto on_timeout = base::BindOnce(
      [](bool& called, const base::UnguessableToken&) { called = true; },
      std::ref(called));
  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;
  // Setting the expectation that the user does exist.
  EXPECT_CALL(keyset_management_, UserExists(_)).WillRepeatedly(Return(true));
  AuthSession auth_session(kFakeUsername, flags, std::move(on_timeout),
                           &crypto_, &keyset_management_, &auth_block_utility_,
                           &auth_factor_manager_, &user_secret_stash_storage_);
  user_data_auth::UpdateCredentialRequest update_cred_request;
  cryptohome::AuthorizationRequest* authorization_request =
      update_cred_request.mutable_authorization();
  authorization_request->mutable_key()->set_secret(kFakePass);
  authorization_request->mutable_key()->mutable_data()->set_label(kFakeLabel);
  update_cred_request.set_old_credential_label("wrong-label");

  // Test.
  base::OnceCallback<void(const user_data_auth::UpdateCredentialReply&)>
      on_done = base::BindLambdaForTesting(
          [](const user_data_auth::UpdateCredentialReply& reply) {
            // Evaluate error returned by callback.
            EXPECT_EQ(user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT,
                      reply.error());
          });
  auth_session.UpdateCredential(update_cred_request, std::move(on_done));
}

// Test that the UserSecretStash isn't created by default when a new user is
// created.
TEST_F(AuthSessionTest, NoUssByDefault) {
  // Setup.
  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;
  // Setting the expectation that the user does not exist.
  EXPECT_CALL(keyset_management_, UserExists(_)).WillRepeatedly(Return(false));
  AuthSession auth_session(kFakeUsername, flags,
                           /*on_timeout=*/base::DoNothing(), &crypto_,
                           &keyset_management_, &auth_block_utility_,
                           &auth_factor_manager_, &user_secret_stash_storage_);

  // Test.
  EXPECT_EQ(auth_session.user_secret_stash_for_testing(), nullptr);
  EXPECT_EQ(auth_session.user_secret_stash_main_key_for_testing(),
            std::nullopt);
  EXPECT_TRUE(auth_session.OnUserCreated().ok());

  // Verify.
  EXPECT_EQ(auth_session.user_secret_stash_for_testing(), nullptr);
  EXPECT_EQ(auth_session.user_secret_stash_main_key_for_testing(),
            std::nullopt);
}

// Test if AuthenticateAuthFactor authenticates existing credentials for a
// user with VK.
TEST_F(AuthSessionTest, AuthenticateAuthFactorExistingVKUserNoResave) {
  // Setup AuthSession.
  AuthBlockState auth_block_state;
  auth_block_state.state = TpmBoundToPcrAuthBlockState();
  std::map<std::string, std::unique_ptr<AuthFactor>> auth_factor_map;
  auth_factor_map.emplace(
      kFakeLabel,
      std::make_unique<AuthFactor>(AuthFactorType::kPassword, kFakeLabel,
                                   AuthFactorMetadata(), auth_block_state));
  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;

  EXPECT_CALL(keyset_management_, UserExists(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(keyset_management_, GetVaultKeysetLabelsAndData(_, _));

  AuthSession auth_session(kFakeUsername, flags,
                           /*on_timeout=*/base::DoNothing(), &crypto_,
                           &keyset_management_, &auth_block_utility_,
                           &auth_factor_manager_, &user_secret_stash_storage_);
  EXPECT_THAT(AuthStatus::kAuthStatusFurtherFactorRequired,
              auth_session.GetStatus());
  EXPECT_TRUE(auth_session.user_exists());
  auth_session.set_label_to_auth_factor_for_testing(std::move(auth_factor_map));

  // Test
  // Calling AuthenticateAuthFactor.
  user_data_auth::AuthenticateAuthFactorRequest request;
  request.set_auth_session_id(auth_session.serialized_token());
  request.set_auth_factor_label(kFakeLabel);
  request.mutable_auth_input()->mutable_password_input()->set_secret(kFakePass);

  // Called within the converter_.PopulateKeyDataForVK()
  KeyData key_data;
  key_data.set_label(kFakeLabel);
  auto vk = std::make_unique<VaultKeyset>();
  vk->SetKeyData(key_data);
  EXPECT_CALL(keyset_management_, GetVaultKeyset(_, kFakeLabel))
      .WillOnce(Return(ByMove(std::move(vk))));

  EXPECT_CALL(auth_block_utility_, GetAuthBlockTypeForDerivation(_, _))
      .WillOnce(Return(AuthBlockType::kTpmBoundToPcr));
  EXPECT_CALL(auth_block_utility_, GetAuthBlockStateFromVaultKeyset(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(keyset_management_, GetValidKeysetWithKeyBlobs(_, _, _))
      .WillOnce(Return(ByMove(std::make_unique<VaultKeyset>())));
  EXPECT_CALL(keyset_management_, ShouldReSaveKeyset(_))
      .WillOnce(Return(false));

  auto key_blobs = std::make_unique<KeyBlobs>();
  EXPECT_CALL(auth_block_utility_, DeriveKeyBlobsWithAuthBlockAsync(_, _, _, _))
      .WillOnce([&](AuthBlockType auth_block_type, const AuthInput& auth_input,
                    const AuthBlockState& auth_state,
                    AuthBlock::DeriveCallback derive_callback) {
        std::move(derive_callback)
            .Run(OkStatus<CryptohomeCryptoError>(), std::move(key_blobs));
        return true;
      });

  bool called = false;
  bool authenticated = true;
  user_data_auth::CryptohomeErrorCode error =
      user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
  EXPECT_TRUE(auth_session.AuthenticateAuthFactor(
      request,
      base::BindOnce(
          [](bool& called, user_data_auth::CryptohomeErrorCode& error,
             bool& authenticated,
             const user_data_auth::AuthenticateAuthFactorReply& reply) {
            called = true;
            error = reply.error();
            authenticated = reply.authenticated();
          },
          std::ref(called), std::ref(error), std::ref(authenticated))));

  // Verify.
  EXPECT_TRUE(called);
  EXPECT_EQ(CRYPTOHOME_ERROR_NOT_SET, error);
  EXPECT_TRUE(authenticated);
  EXPECT_EQ(auth_session.GetStatus(), AuthStatus::kAuthStatusAuthenticated);
}

// Test if AuthenticateAuthFactor authenticates existing credentials for a
// user with VK and resaves it.
TEST_F(AuthSessionTest, AuthenticateAuthFactorExistingVKUserAndResave) {
  // Setup AuthSession.
  AuthBlockState auth_block_state;
  auth_block_state.state = LibScryptCompatAuthBlockState();
  std::map<std::string, std::unique_ptr<AuthFactor>> auth_factor_map;
  auth_factor_map.emplace(
      kFakeLabel,
      std::make_unique<AuthFactor>(AuthFactorType::kPassword, kFakeLabel,
                                   AuthFactorMetadata(), auth_block_state));
  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;

  EXPECT_CALL(keyset_management_, UserExists(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(keyset_management_, GetVaultKeysetLabelsAndData(_, _));

  AuthSession auth_session(kFakeUsername, flags,
                           /*on_timeout=*/base::DoNothing(), &crypto_,
                           &keyset_management_, &auth_block_utility_,
                           &auth_factor_manager_, &user_secret_stash_storage_);
  EXPECT_THAT(AuthStatus::kAuthStatusFurtherFactorRequired,
              auth_session.GetStatus());
  EXPECT_TRUE(auth_session.user_exists());
  auth_session.set_label_to_auth_factor_for_testing(std::move(auth_factor_map));

  // Test
  // Calling AuthenticateAuthFactor.
  user_data_auth::AuthenticateAuthFactorRequest request;
  request.set_auth_session_id(auth_session.serialized_token());
  request.set_auth_factor_label(kFakeLabel);
  request.mutable_auth_input()->mutable_password_input()->set_secret(kFakePass);

  // Called within the converter_.PopulateKeyDataForVK()
  KeyData key_data;
  key_data.set_label(kFakeLabel);
  auto vk = std::make_unique<VaultKeyset>();
  vk->SetKeyData(key_data);
  EXPECT_CALL(keyset_management_, GetVaultKeyset(_, kFakeLabel))
      .WillOnce(Return(ByMove(std::move(vk))));

  EXPECT_CALL(auth_block_utility_, GetAuthBlockTypeForDerivation(_, _))
      .WillOnce(Return(AuthBlockType::kLibScryptCompat));
  EXPECT_CALL(auth_block_utility_, GetAuthBlockStateFromVaultKeyset(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(keyset_management_, GetValidKeysetWithKeyBlobs(_, _, _))
      .WillOnce(Return(ByMove(std::make_unique<VaultKeyset>())));

  EXPECT_CALL(keyset_management_, ShouldReSaveKeyset(_)).WillOnce(Return(true));
  EXPECT_CALL(auth_block_utility_, GetAuthBlockTypeForCreation(_, _, _))
      .WillOnce(Return(AuthBlockType::kTpmBoundToPcr));
  EXPECT_CALL(keyset_management_, ReSaveKeysetWithKeyBlobs(_, _, _));

  auto key_blobs = std::make_unique<KeyBlobs>();
  auto auth_block_state2 = std::make_unique<AuthBlockState>();
  EXPECT_CALL(auth_block_utility_, CreateKeyBlobsWithAuthBlockAsync(_, _, _))
      .WillOnce([&](AuthBlockType auth_block_type, const AuthInput& auth_input,
                    AuthBlock::CreateCallback create_callback) {
        std::move(create_callback)
            .Run(OkStatus<CryptohomeCryptoError>(), std::move(key_blobs),
                 std::move(auth_block_state2));
        return true;
      });

  auto key_blobs2 = std::make_unique<KeyBlobs>();
  EXPECT_CALL(auth_block_utility_, DeriveKeyBlobsWithAuthBlockAsync(_, _, _, _))
      .WillOnce([&](AuthBlockType auth_block_type, const AuthInput& auth_input,
                    const AuthBlockState& auth_state,
                    AuthBlock::DeriveCallback derive_callback) {
        std::move(derive_callback)
            .Run(OkStatus<CryptohomeCryptoError>(), std::move(key_blobs2));
        return true;
      });

  bool called = false;
  user_data_auth::CryptohomeErrorCode error =
      user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
  EXPECT_TRUE(auth_session.AuthenticateAuthFactor(
      request,
      base::BindOnce(
          [](bool& called, user_data_auth::CryptohomeErrorCode& error,
             const user_data_auth::AuthenticateAuthFactorReply& reply) {
            called = true;
            error = reply.error();
          },
          std::ref(called), std::ref(error))));

  // Verify.
  EXPECT_TRUE(called);
  EXPECT_EQ(auth_session.GetStatus(), AuthStatus::kAuthStatusAuthenticated);
}

// Test if AddAuthFactor correctly adds initial VaultKeyset password AuthFactor
// for a new user.
TEST_F(AuthSessionTest, AddAuthFactorNewUser) {
  // Setup.
  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;
  // Setting the expectation that the user does not exist.
  EXPECT_CALL(keyset_management_, UserExists(_)).WillRepeatedly(Return(false));

  std::unique_ptr<AuthBlockUtilityImpl> auth_block_utility_impl =
      std::make_unique<AuthBlockUtilityImpl>(&keyset_management_, &crypto_,
                                             &platform_);
  AuthSession auth_session(kFakeUsername, flags,
                           /*on_timeout=*/base::DoNothing(), &crypto_,
                           &keyset_management_, auth_block_utility_impl.get(),
                           &auth_factor_manager_, &user_secret_stash_storage_);

  // Setting the expectation that the user does not exist.
  EXPECT_EQ(auth_session.GetStatus(),
            AuthStatus::kAuthStatusFurtherFactorRequired);
  EXPECT_FALSE(auth_session.user_exists());

  // Creating the user.
  EXPECT_TRUE(auth_session.OnUserCreated().ok());
  EXPECT_EQ(auth_session.GetStatus(), AuthStatus::kAuthStatusAuthenticated);
  EXPECT_TRUE(auth_session.user_exists());

  user_data_auth::AddAuthFactorRequest request;
  request.set_auth_session_id(auth_session.serialized_token());
  request.mutable_auth_factor()->set_type(
      user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
  request.mutable_auth_factor()->set_label(kFakeLabel);
  request.mutable_auth_factor()->mutable_password_metadata();
  request.mutable_auth_input()->mutable_password_input()->set_secret(kFakePass);

  EXPECT_CALL(keyset_management_,
              AddInitialKeysetWithKeyBlobs(_, _, _, _, _, _))
      .WillOnce(Return(ByMove(std::make_unique<VaultKeyset>())));

  bool called = false;
  user_data_auth::CryptohomeErrorCode error =
      user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
  auth_session.AddAuthFactor(
      request, base::BindOnce(
                   [](bool& called, user_data_auth::CryptohomeErrorCode& error,
                      const user_data_auth::AddAuthFactorReply& reply) {
                     called = true;
                     error = reply.error();
                   },
                   std::ref(called), std::ref(error)));

  // Verify.
  EXPECT_TRUE(called);
  EXPECT_EQ(user_data_auth::CRYPTOHOME_ERROR_NOT_SET, error);
}

// Test that AddAuthFactor can add multiple VaultKeyset-AuthFactor. The first
// one is added as initial factor, the second is added as the second password
// factor, and the third one as added as a PIN factor.
TEST_F(AuthSessionTest, AddMultipleAuthFactor) {
  // Setup.
  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;
  // Setting the expectation that the user does not exist.
  EXPECT_CALL(keyset_management_, UserExists(_)).WillRepeatedly(Return(false));

  AuthSession auth_session(kFakeUsername, flags,
                           /*on_timeout=*/base::DoNothing(), &crypto_,
                           &keyset_management_, &auth_block_utility_,
                           &auth_factor_manager_, &user_secret_stash_storage_);

  // Setting the expectation that the user does not exist.
  EXPECT_EQ(auth_session.GetStatus(),
            AuthStatus::kAuthStatusFurtherFactorRequired);
  EXPECT_FALSE(auth_session.user_exists());

  // Creating the user.
  EXPECT_TRUE(auth_session.OnUserCreated().ok());
  EXPECT_EQ(auth_session.GetStatus(), AuthStatus::kAuthStatusAuthenticated);
  EXPECT_TRUE(auth_session.user_exists());

  user_data_auth::AddAuthFactorRequest request;
  request.set_auth_session_id(auth_session.serialized_token());
  request.mutable_auth_factor()->set_type(
      user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
  request.mutable_auth_factor()->set_label(kFakeLabel);
  request.mutable_auth_factor()->mutable_password_metadata();
  request.mutable_auth_input()->mutable_password_input()->set_secret(kFakePass);

  // GetauthBlockTypeForCreation() and CreateKeyBlobsWithAuthBlockAsync() are
  // called for each of the key addition operations below.
  EXPECT_CALL(auth_block_utility_, GetAuthBlockTypeForCreation(_, _, _))
      .WillRepeatedly(Return(AuthBlockType::kTpmBoundToPcr));
  EXPECT_CALL(auth_block_utility_, CreateKeyBlobsWithAuthBlockAsync(_, _, _))
      .WillRepeatedly([&](AuthBlockType auth_block_type,
                          const AuthInput& auth_input,
                          AuthBlock::CreateCallback create_callback) {
        std::move(create_callback)
            .Run(OkStatus<CryptohomeCryptoError>(),
                 std::make_unique<KeyBlobs>(),
                 std::make_unique<AuthBlockState>());
        return true;
      });
  EXPECT_CALL(keyset_management_,
              AddInitialKeysetWithKeyBlobs(_, _, _, _, _, _))
      .WillOnce(Return(ByMove(std::make_unique<VaultKeyset>())));

  bool called = false;
  user_data_auth::CryptohomeErrorCode error =
      user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
  auth_session.AddAuthFactor(
      request, base::BindOnce(
                   [](bool& called, user_data_auth::CryptohomeErrorCode& error,
                      const user_data_auth::AddAuthFactorReply& reply) {
                     called = true;
                     error = reply.error();
                   },
                   std::ref(called), std::ref(error)));

  // Verify.
  EXPECT_TRUE(called);
  EXPECT_EQ(user_data_auth::CRYPTOHOME_ERROR_NOT_SET, error);

  // Test adding new password AuthFactor
  user_data_auth::AddAuthFactorRequest request2;
  request2.set_auth_session_id(auth_session.serialized_token());
  request2.mutable_auth_factor()->set_type(
      user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
  request2.mutable_auth_factor()->set_label(kFakeOtherLabel);
  request2.mutable_auth_factor()->mutable_password_metadata();
  request2.mutable_auth_input()->mutable_password_input()->set_secret(
      kFakeOtherPass);

  EXPECT_CALL(keyset_management_, AddKeysetWithKeyBlobs(_, _, _, _, _, _))
      .WillOnce(Return(CRYPTOHOME_ERROR_NOT_SET));

  called = false;
  error = user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
  auth_session.AddAuthFactor(
      request2, base::BindOnce(
                    [](bool& called, user_data_auth::CryptohomeErrorCode& error,
                       const user_data_auth::AddAuthFactorReply& reply) {
                      called = true;
                      error = reply.error();
                    },
                    std::ref(called), std::ref(error)));

  // Verify.
  EXPECT_TRUE(called);
  EXPECT_EQ(user_data_auth::CRYPTOHOME_ERROR_NOT_SET, error);

  // TODO(b:223222440) Add test to for adding a PIN after reset secret
  // generation function is updated.
}

// A variant of the auth session test that has the UserSecretStash experiment
// enabled.
class AuthSessionWithUssExperimentTest : public AuthSessionTest {
 protected:
  AuthSessionWithUssExperimentTest() {
    SetUserSecretStashExperimentForTesting(/*enabled=*/true);
  }

  ~AuthSessionWithUssExperimentTest() {
    // Reset this global variable to avoid affecting unrelated test cases.
    SetUserSecretStashExperimentForTesting(/*enabled=*/std::nullopt);
  }
};

// Test that the UserSecretStash is created on the user creation, in case the
// UserSecretStash experiment is on.
TEST_F(AuthSessionWithUssExperimentTest, UssCreation) {
  // Setup.
  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;
  // Setting the expectation that the user does not exist.
  EXPECT_CALL(keyset_management_, UserExists(_)).WillRepeatedly(Return(false));
  AuthSession auth_session(kFakeUsername, flags,
                           /*on_timeout=*/base::DoNothing(), &crypto_,
                           &keyset_management_, &auth_block_utility_,
                           &auth_factor_manager_, &user_secret_stash_storage_);

  // Test.
  EXPECT_EQ(auth_session.user_secret_stash_for_testing(), nullptr);
  EXPECT_EQ(auth_session.user_secret_stash_main_key_for_testing(),
            std::nullopt);
  EXPECT_TRUE(auth_session.OnUserCreated().ok());

  // Verify.
  EXPECT_NE(auth_session.user_secret_stash_for_testing(), nullptr);
  EXPECT_NE(auth_session.user_secret_stash_main_key_for_testing(),
            std::nullopt);
}

// Test that no UserSecretStash is created for an ephemeral user.
TEST_F(AuthSessionWithUssExperimentTest, NoUssForEphemeral) {
  // Setup.
  int flags =
      user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_EPHEMERAL_USER;
  // Setting the expectation that the user does not exist.
  EXPECT_CALL(keyset_management_, UserExists(_)).WillRepeatedly(Return(false));
  AuthSession auth_session(kFakeUsername, flags,
                           /*on_timeout=*/base::DoNothing(), &crypto_,
                           &keyset_management_, &auth_block_utility_,
                           &auth_factor_manager_, &user_secret_stash_storage_);

  // Test.
  EXPECT_TRUE(auth_session.OnUserCreated().ok());

  // Verify.
  EXPECT_EQ(auth_session.user_secret_stash_for_testing(), nullptr);
  EXPECT_EQ(auth_session.user_secret_stash_main_key_for_testing(),
            std::nullopt);
}

// Test that a new auth factor can be added to the newly created user, in case
// the UserSecretStash experiment is on.
TEST_F(AuthSessionWithUssExperimentTest, AddPasswordAuthFactorViaUss) {
  // Setup.
  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;
  // Setting the expectation that the user does not exist.
  EXPECT_CALL(keyset_management_, UserExists(_)).WillRepeatedly(Return(false));
  AuthSession auth_session(kFakeUsername, flags,
                           /*on_timeout=*/base::DoNothing(), &crypto_,
                           &keyset_management_, &auth_block_utility_,
                           &auth_factor_manager_, &user_secret_stash_storage_);
  // Creating the user.
  EXPECT_TRUE(auth_session.OnUserCreated().ok());
  EXPECT_NE(auth_session.user_secret_stash_for_testing(), nullptr);
  EXPECT_NE(auth_session.user_secret_stash_main_key_for_testing(),
            std::nullopt);

  // Test.
  // Setting the expectation that the auth block utility will create key blobs.
  EXPECT_CALL(auth_block_utility_,
              CreateKeyBlobsWithAuthFactorType(AuthFactorType::kPassword,
                                               /*auth_input=*/_,
                                               /*out_auth_block_state=*/_,
                                               /*out_key_blobs=*/_))
      .WillOnce([](AuthFactorType auth_factor_type, const AuthInput& auth_input,
                   AuthBlockState& out_auth_block_state,
                   KeyBlobs& out_key_blobs) {
        // An arbitrary auth block state type can be used in this test.
        out_auth_block_state.state = TpmBoundToPcrAuthBlockState();
        out_key_blobs.vkk_key = brillo::SecureBlob("fake vkk key");
        return OkStatus<CryptohomeCryptoError>();
      });
  // Calling AddAuthFactor.
  user_data_auth::AddAuthFactorRequest request;
  request.set_auth_session_id(auth_session.serialized_token());
  request.mutable_auth_factor()->set_type(
      user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
  request.mutable_auth_factor()->set_label(kFakeLabel);
  request.mutable_auth_factor()->mutable_password_metadata();
  request.mutable_auth_input()->mutable_password_input()->set_secret(kFakePass);

  bool called = false;
  user_data_auth::CryptohomeErrorCode error =
      user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
  auth_session.AddAuthFactor(
      request, base::BindOnce(
                   [](bool& called, user_data_auth::CryptohomeErrorCode& error,
                      const user_data_auth::AddAuthFactorReply& reply) {
                     called = true;
                     error = reply.error();
                   },
                   std::ref(called), std::ref(error)));

  // Verify.
  EXPECT_TRUE(called);
  EXPECT_EQ(user_data_auth::CRYPTOHOME_ERROR_NOT_SET, error);
  std::map<std::string, AuthFactorType> stored_factors =
      auth_factor_manager_.ListAuthFactors(SanitizeUserName(kFakeUsername));
  EXPECT_THAT(stored_factors,
              ElementsAre(Pair(kFakeLabel, AuthFactorType::kPassword)));
  EXPECT_NE(auth_session.label_to_auth_factor_.find(kFakeLabel),
            auth_session.label_to_auth_factor_.end());
}

// Test that a new auth factor cannot be added for an unauthenticated
// authsession.
TEST_F(AuthSessionWithUssExperimentTest, AddPasswordAuthFactorUnAuthenticated) {
  // Setup.
  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;
  // Setting the expectation that the user exist.
  EXPECT_CALL(keyset_management_, UserExists(_)).WillRepeatedly(Return(true));
  AuthSession auth_session(kFakeUsername, flags,
                           /*on_timeout=*/base::DoNothing(), &crypto_,
                           &keyset_management_, &auth_block_utility_,
                           &auth_factor_manager_, &user_secret_stash_storage_);

  user_data_auth::AddAuthFactorRequest request;
  request.set_auth_session_id(auth_session.serialized_token());
  request.mutable_auth_factor()->set_type(
      user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
  request.mutable_auth_factor()->set_label(kFakeLabel);
  request.mutable_auth_factor()->mutable_password_metadata();
  request.mutable_auth_input()->mutable_password_input()->set_secret(kFakePass);

  // Test and Verify.
  bool called = false;
  user_data_auth::CryptohomeErrorCode error =
      user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
  auth_session.AddAuthFactor(
      request, base::BindOnce(
                   [](bool& called, user_data_auth::CryptohomeErrorCode& error,
                      const user_data_auth::AddAuthFactorReply& reply) {
                     called = true;
                     error = reply.error();
                   },
                   std::ref(called), std::ref(error)));
  EXPECT_TRUE(called);
  EXPECT_EQ(CRYPTOHOME_ERROR_UNAUTHENTICATED_AUTH_SESSION, error);
}

// Test that a new auth factor and a pin can be added to the newly created user,
// in case the UserSecretStash experiment is on.
TEST_F(AuthSessionWithUssExperimentTest, AddPasswordAndPinAuthFactorViaUss) {
  // Setup.
  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;
  // Setting the expectation that the user does not exist.
  EXPECT_CALL(keyset_management_, UserExists(_)).WillRepeatedly(Return(false));
  AuthSession auth_session(kFakeUsername, flags,
                           /*on_timeout=*/base::DoNothing(), &crypto_,
                           &keyset_management_, &auth_block_utility_,
                           &auth_factor_manager_, &user_secret_stash_storage_);
  // Creating the user.
  EXPECT_TRUE(auth_session.OnUserCreated().ok());
  EXPECT_NE(auth_session.user_secret_stash_for_testing(), nullptr);
  EXPECT_NE(auth_session.user_secret_stash_main_key_for_testing(),
            std::nullopt);
  // Add a password first.
  // Setting the expectation that the auth block utility will create key blobs.
  EXPECT_CALL(auth_block_utility_,
              CreateKeyBlobsWithAuthFactorType(AuthFactorType::kPassword,
                                               /*auth_input=*/_,
                                               /*out_auth_block_state=*/_,
                                               /*out_key_blobs=*/_))
      .WillOnce([](AuthFactorType auth_factor_type, const AuthInput& auth_input,
                   AuthBlockState& out_auth_block_state,
                   KeyBlobs& out_key_blobs) {
        // An arbitrary auth block state type can be used in this test.
        out_auth_block_state.state = TpmBoundToPcrAuthBlockState();
        out_key_blobs.vkk_key = brillo::SecureBlob("fake vkk key");
        return OkStatus<CryptohomeCryptoError>();
      });
  // Calling AddAuthFactor.
  user_data_auth::AddAuthFactorRequest request;
  request.set_auth_session_id(auth_session.serialized_token());
  request.mutable_auth_factor()->set_type(
      user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
  request.mutable_auth_factor()->set_label(kFakeLabel);
  request.mutable_auth_factor()->mutable_password_metadata();
  request.mutable_auth_input()->mutable_password_input()->set_secret(kFakePass);

  bool called = false;
  user_data_auth::CryptohomeErrorCode error =
      user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
  auth_session.AddAuthFactor(
      request, base::BindOnce(
                   [](bool& called, user_data_auth::CryptohomeErrorCode& error,
                      const user_data_auth::AddAuthFactorReply& reply) {
                     called = true;
                     error = reply.error();
                   },
                   std::ref(called), std::ref(error)));

  // Test.
  EXPECT_TRUE(called);
  EXPECT_EQ(user_data_auth::CRYPTOHOME_ERROR_NOT_SET, error);

  // Setting the expectation that the auth block utility will create key blobs.
  EXPECT_CALL(auth_block_utility_,
              CreateKeyBlobsWithAuthFactorType(AuthFactorType::kPin,
                                               /*auth_input=*/_,
                                               /*out_auth_block_state=*/_,
                                               /*out_key_blobs=*/_))
      .WillOnce([](AuthFactorType auth_factor_type, const AuthInput& auth_input,
                   AuthBlockState& out_auth_block_state,
                   KeyBlobs& out_key_blobs) {
        // An arbitrary auth block state type can be used in this test.
        out_auth_block_state.state = PinWeaverAuthBlockState();
        out_key_blobs.vkk_key = brillo::SecureBlob("fake vkk key");
        return OkStatus<CryptohomeCryptoError>();
      });
  // Calling AddAuthFactor.
  user_data_auth::AddAuthFactorRequest add_pin_request;
  add_pin_request.set_auth_session_id(auth_session.serialized_token());
  add_pin_request.mutable_auth_factor()->set_type(
      user_data_auth::AUTH_FACTOR_TYPE_PIN);
  add_pin_request.mutable_auth_factor()->set_label(kFakePinLabel);
  add_pin_request.mutable_auth_factor()->mutable_pin_metadata();
  add_pin_request.mutable_auth_input()->mutable_pin_input()->set_secret(
      kFakePin);
  called = false;
  error = user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
  auth_session.AddAuthFactor(
      add_pin_request,
      base::BindOnce(
          [](bool& called, user_data_auth::CryptohomeErrorCode& error,
             const user_data_auth::AddAuthFactorReply& reply) {
            called = true;
            error = reply.error();
          },
          std::ref(called), std::ref(error)));

  // Verify.
  EXPECT_TRUE(called);
  EXPECT_EQ(user_data_auth::CRYPTOHOME_ERROR_NOT_SET, error);
  std::map<std::string, AuthFactorType> stored_factors =
      auth_factor_manager_.ListAuthFactors(SanitizeUserName(kFakeUsername));
  EXPECT_THAT(stored_factors,
              ElementsAre(Pair(kFakeLabel, AuthFactorType::kPassword),
                          Pair(kFakePinLabel, AuthFactorType::kPin)));

  // Ensure that a reset secret for the PIN was added.
  const auto reset_secret =
      auth_session.user_secret_stash_for_testing()->GetResetSecretForLabel(
          kFakePinLabel);
  EXPECT_TRUE(reset_secret.has_value());
  EXPECT_EQ(CRYPTOHOME_RESET_SECRET_LENGTH, reset_secret->size());
}

TEST_F(AuthSessionWithUssExperimentTest, AuthenticatePasswordAuthFactorViaUss) {
  // Setup.
  const std::string obfuscated_username = SanitizeUserName(kFakeUsername);
  const brillo::SecureBlob kFakePerCredentialSecret("fake-vkk");
  // Setting the expectation that the user exists.
  EXPECT_CALL(keyset_management_, UserExists(_)).WillRepeatedly(Return(true));
  // Generating the USS.
  CryptohomeStatusOr<std::unique_ptr<UserSecretStash>> uss_status =
      UserSecretStash::CreateRandom(FileSystemKeyset::CreateRandom());
  ASSERT_TRUE(uss_status.ok());
  std::unique_ptr<UserSecretStash> uss = std::move(uss_status).value();
  std::optional<brillo::SecureBlob> uss_main_key =
      UserSecretStash::CreateRandomMainKey();
  ASSERT_TRUE(uss_main_key.has_value());
  // Creating the auth factor. An arbitrary auth block state is used in this
  // test.
  AuthFactor auth_factor(
      AuthFactorType::kPassword, kFakeLabel,
      AuthFactorMetadata{.metadata = PasswordAuthFactorMetadata()},
      AuthBlockState{.state = TpmBoundToPcrAuthBlockState()});
  EXPECT_TRUE(
      auth_factor_manager_.SaveAuthFactor(obfuscated_username, auth_factor)
          .ok());
  // Adding the auth factor into the USS and persisting the latter.
  const KeyBlobs key_blobs = {.vkk_key = kFakePerCredentialSecret};
  std::optional<brillo::SecureBlob> wrapping_key =
      key_blobs.DeriveUssCredentialSecret();
  ASSERT_TRUE(wrapping_key.has_value());
  EXPECT_TRUE(uss->AddWrappedMainKey(uss_main_key.value(), kFakeLabel,
                                     wrapping_key.value())
                  .ok());
  CryptohomeStatusOr<brillo::Blob> encrypted_uss =
      uss->GetEncryptedContainer(uss_main_key.value());
  ASSERT_TRUE(encrypted_uss.ok());
  EXPECT_TRUE(user_secret_stash_storage_
                  .Persist(encrypted_uss.value(), obfuscated_username)
                  .ok());
  // Creating the auth session.
  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;
  AuthSession auth_session(kFakeUsername, flags,
                           /*on_timeout=*/base::DoNothing(), &crypto_,
                           &keyset_management_, &auth_block_utility_,
                           &auth_factor_manager_, &user_secret_stash_storage_);
  EXPECT_TRUE(auth_session.user_exists());

  // Test.
  // Setting the expectation that the auth block utility will derive key blobs.
  EXPECT_CALL(auth_block_utility_, DeriveKeyBlobs(_, _, _))
      .WillOnce([&](const AuthInput& auth_input,
                    const AuthBlockState& auth_block_state,
                    KeyBlobs& out_key_blobs) {
        out_key_blobs.vkk_key = kFakePerCredentialSecret;
        return OkStatus<CryptohomeCryptoError>();
      });
  // Calling AuthenticateAuthFactor.
  user_data_auth::AuthenticateAuthFactorRequest request;
  request.set_auth_session_id(auth_session.serialized_token());
  request.set_auth_factor_label(kFakeLabel);
  request.mutable_auth_input()->mutable_password_input()->set_secret(kFakePass);
  bool called = false;
  user_data_auth::CryptohomeErrorCode error =
      user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
  EXPECT_TRUE(auth_session.AuthenticateAuthFactor(
      request,
      base::BindOnce(
          [](bool& called, user_data_auth::CryptohomeErrorCode& error,
             const user_data_auth::AuthenticateAuthFactorReply& reply) {
            called = true;
            error = reply.error();
          },
          std::ref(called), std::ref(error))));

  // Verify.
  EXPECT_EQ(auth_session.GetStatus(), AuthStatus::kAuthStatusAuthenticated);
  EXPECT_NE(auth_session.user_secret_stash_for_testing(), nullptr);
  EXPECT_NE(auth_session.user_secret_stash_main_key_for_testing(),
            std::nullopt);
}

TEST_F(AuthSessionWithUssExperimentTest, AuthenticatePinAuthFactorViaUss) {
  // Setup.
  const std::string obfuscated_username = SanitizeUserName(kFakeUsername);
  const brillo::SecureBlob kFakePerCredentialSecret("fake-vkk");
  // Setting the expectation that the user exists.
  EXPECT_CALL(keyset_management_, UserExists(_)).WillRepeatedly(Return(true));
  // Generating the USS.
  CryptohomeStatusOr<std::unique_ptr<UserSecretStash>> uss_status =
      UserSecretStash::CreateRandom(FileSystemKeyset::CreateRandom());
  ASSERT_TRUE(uss_status.ok());
  std::unique_ptr<UserSecretStash> uss = std::move(uss_status).value();
  std::optional<brillo::SecureBlob> uss_main_key =
      UserSecretStash::CreateRandomMainKey();
  ASSERT_TRUE(uss_main_key.has_value());
  // Creating the auth factor. An arbitrary auth block state is used in this
  // test.
  AuthFactor auth_factor(
      AuthFactorType::kPin, kFakePinLabel,
      AuthFactorMetadata{.metadata = PinAuthFactorMetadata()},
      AuthBlockState{.state = PinWeaverAuthBlockState()});
  EXPECT_TRUE(
      auth_factor_manager_.SaveAuthFactor(obfuscated_username, auth_factor)
          .ok());
  // Adding the auth factor into the USS and persisting the latter.
  const KeyBlobs key_blobs = {.vkk_key = kFakePerCredentialSecret};
  std::optional<brillo::SecureBlob> wrapping_key =
      key_blobs.DeriveUssCredentialSecret();
  ASSERT_TRUE(wrapping_key.has_value());
  EXPECT_TRUE(uss->AddWrappedMainKey(uss_main_key.value(), kFakePinLabel,
                                     wrapping_key.value())
                  .ok());
  CryptohomeStatusOr<brillo::Blob> encrypted_uss =
      uss->GetEncryptedContainer(uss_main_key.value());
  ASSERT_TRUE(encrypted_uss.ok());
  EXPECT_TRUE(user_secret_stash_storage_
                  .Persist(encrypted_uss.value(), obfuscated_username)
                  .ok());
  // Creating the auth session.
  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;
  AuthSession auth_session(kFakeUsername, flags,
                           /*on_timeout=*/base::DoNothing(), &crypto_,
                           &keyset_management_, &auth_block_utility_,
                           &auth_factor_manager_, &user_secret_stash_storage_);
  EXPECT_TRUE(auth_session.user_exists());

  // Test.
  // Setting the expectation that the auth block utility will derive key blobs.
  EXPECT_CALL(auth_block_utility_, DeriveKeyBlobs(_, _, _))
      .WillOnce([&](const AuthInput& auth_input,
                    const AuthBlockState& auth_block_state,
                    KeyBlobs& out_key_blobs) {
        out_key_blobs.vkk_key = kFakePerCredentialSecret;
        return OkStatus<CryptohomeCryptoError>();
      });
  // Calling AuthenticateAuthFactor.
  user_data_auth::AuthenticateAuthFactorRequest request;
  request.set_auth_session_id(auth_session.serialized_token());
  request.set_auth_factor_label(kFakePinLabel);
  request.mutable_auth_input()->mutable_pin_input()->set_secret(kFakePin);
  bool called = false;
  user_data_auth::CryptohomeErrorCode error =
      user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
  EXPECT_TRUE(auth_session.AuthenticateAuthFactor(
      request,
      base::BindOnce(
          [](bool& called, user_data_auth::CryptohomeErrorCode& error,
             const user_data_auth::AuthenticateAuthFactorReply& reply) {
            called = true;
            error = reply.error();
          },
          std::ref(called), std::ref(error))));

  // Verify.
  EXPECT_EQ(auth_session.GetStatus(), AuthStatus::kAuthStatusAuthenticated);
  EXPECT_NE(auth_session.user_secret_stash_for_testing(), nullptr);
  EXPECT_NE(auth_session.user_secret_stash_main_key_for_testing(),
            std::nullopt);
}

TEST_F(AuthSessionWithUssExperimentTest, AddCryptohomeRecoveryAuthFactor) {
  // Setup.
  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;
  // Setting the expectation that the user does not exist.
  EXPECT_CALL(keyset_management_, UserExists(_)).WillRepeatedly(Return(false));
  AuthSession auth_session(kFakeUsername, flags,
                           /*on_timeout=*/base::DoNothing(), &crypto_,
                           &keyset_management_, &auth_block_utility_,
                           &auth_factor_manager_, &user_secret_stash_storage_);
  // Creating the user.
  EXPECT_TRUE(auth_session.OnUserCreated().ok());
  EXPECT_NE(auth_session.user_secret_stash_for_testing(), nullptr);
  EXPECT_NE(auth_session.user_secret_stash_main_key_for_testing(),
            std::nullopt);
  // Setting the expectation that the auth block utility will create key blobs.
  EXPECT_CALL(auth_block_utility_, CreateKeyBlobsWithAuthFactorType(
                                       AuthFactorType::kCryptohomeRecovery,
                                       /*auth_input=*/_,
                                       /*out_auth_block_state=*/_,
                                       /*out_key_blobs=*/_))
      .WillOnce([](AuthFactorType auth_factor_type, const AuthInput& auth_input,
                   AuthBlockState& out_auth_block_state,
                   KeyBlobs& out_key_blobs) {
        // An arbitrary auth block state type can be used in this test.
        out_auth_block_state.state = CryptohomeRecoveryAuthBlockState();
        out_key_blobs.vkk_key = brillo::SecureBlob("fake vkk key");
        return OkStatus<CryptohomeCryptoError>();
      });
  // Calling AddAuthFactor.
  user_data_auth::AddAuthFactorRequest request;
  request.set_auth_session_id(auth_session.serialized_token());
  request.mutable_auth_factor()->set_type(
      user_data_auth::AUTH_FACTOR_TYPE_CRYPTOHOME_RECOVERY);
  request.mutable_auth_factor()->set_label(kFakeLabel);
  request.mutable_auth_factor()->mutable_cryptohome_recovery_metadata();
  request.mutable_auth_input()
      ->mutable_cryptohome_recovery_input()
      ->set_mediator_pub_key("mediator pub key");
  bool called = false;
  user_data_auth::CryptohomeErrorCode error =
      user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
  auth_session.AddAuthFactor(
      request, base::BindOnce(
                   [](bool& called, user_data_auth::CryptohomeErrorCode& error,
                      const user_data_auth::AddAuthFactorReply& reply) {
                     called = true;
                     error = reply.error();
                   },
                   std::ref(called), std::ref(error)));

  // Verify.
  EXPECT_TRUE(called);
  EXPECT_EQ(user_data_auth::CRYPTOHOME_ERROR_NOT_SET, error);
  std::map<std::string, AuthFactorType> stored_factors =
      auth_factor_manager_.ListAuthFactors(SanitizeUserName(kFakeUsername));
  EXPECT_THAT(
      stored_factors,
      ElementsAre(Pair(kFakeLabel, AuthFactorType::kCryptohomeRecovery)));
}

TEST_F(AuthSessionWithUssExperimentTest,
       AuthenticateCryptohomeRecoveryAuthFactor) {
  // Setup.
  const std::string obfuscated_username = SanitizeUserName(kFakeUsername);
  const brillo::SecureBlob kFakePerCredentialSecret("fake-vkk");
  // Setting the expectation that the user exists.
  EXPECT_CALL(keyset_management_, UserExists(_)).WillRepeatedly(Return(true));
  // Generating the USS.
  CryptohomeStatusOr<std::unique_ptr<UserSecretStash>> uss_status =
      UserSecretStash::CreateRandom(FileSystemKeyset::CreateRandom());
  ASSERT_TRUE(uss_status.ok());
  std::unique_ptr<UserSecretStash> uss = std::move(uss_status).value();
  std::optional<brillo::SecureBlob> uss_main_key =
      UserSecretStash::CreateRandomMainKey();
  ASSERT_TRUE(uss_main_key.has_value());
  // Creating the auth factor.
  AuthFactor auth_factor(
      AuthFactorType::kCryptohomeRecovery, kFakeLabel,
      AuthFactorMetadata{.metadata = CryptohomeRecoveryAuthFactorMetadata()},
      AuthBlockState{.state = CryptohomeRecoveryAuthBlockState()});
  EXPECT_TRUE(
      auth_factor_manager_.SaveAuthFactor(obfuscated_username, auth_factor)
          .ok());
  // Adding the auth factor into the USS and persisting the latter.
  const KeyBlobs key_blobs = {.vkk_key = kFakePerCredentialSecret};
  std::optional<brillo::SecureBlob> wrapping_key =
      key_blobs.DeriveUssCredentialSecret();
  ASSERT_TRUE(wrapping_key.has_value());
  EXPECT_TRUE(uss->AddWrappedMainKey(uss_main_key.value(), kFakeLabel,
                                     wrapping_key.value())
                  .ok());
  CryptohomeStatusOr<brillo::Blob> encrypted_uss =
      uss->GetEncryptedContainer(uss_main_key.value());
  ASSERT_TRUE(encrypted_uss.ok());
  EXPECT_TRUE(user_secret_stash_storage_
                  .Persist(encrypted_uss.value(), obfuscated_username)
                  .ok());
  // Creating the auth session.
  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;
  AuthSession auth_session(kFakeUsername, flags,
                           /*on_timeout=*/base::DoNothing(), &crypto_,
                           &keyset_management_, &auth_block_utility_,
                           &auth_factor_manager_, &user_secret_stash_storage_);
  EXPECT_TRUE(auth_session.user_exists());

  // Test.
  // Setting the expectation that the auth block utility will generate recovery
  // request.
  EXPECT_CALL(auth_block_utility_, GenerateRecoveryRequest(_, _, _, _, _, _))
      .WillOnce([&](const cryptorecovery::RequestMetadata& request_metadata,
                    const brillo::Blob& epoch_response,
                    const CryptohomeRecoveryAuthBlockState& state, Tpm* tpm,
                    brillo::SecureBlob* out_recovery_request,
                    brillo::SecureBlob* out_ephemeral_pub_key) {
        *out_ephemeral_pub_key = brillo::SecureBlob("test");
        return OkStatus<CryptohomeCryptoError>();
      });
  EXPECT_EQ(auth_session.user_secret_stash_for_testing(), nullptr);

  // Calling GetRecoveryRequest.
  user_data_auth::GetRecoveryRequestRequest request;
  request.set_auth_session_id(auth_session.serialized_token());
  request.set_auth_factor_label(kFakeLabel);
  bool called = false;
  user_data_auth::CryptohomeErrorCode error =
      user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
  EXPECT_TRUE(auth_session.GetRecoveryRequest(
      request, base::BindOnce(
                   [](bool& called, user_data_auth::CryptohomeErrorCode& error,
                      const user_data_auth::GetRecoveryRequestReply& reply) {
                     called = true;
                     error = reply.error();
                   },
                   std::ref(called), std::ref(error))));

  // Verify.
  EXPECT_TRUE(called);
  EXPECT_EQ(user_data_auth::CRYPTOHOME_ERROR_NOT_SET, error);
  EXPECT_EQ(auth_session.GetStatus(),
            AuthStatus::kAuthStatusFurtherFactorRequired);
  EXPECT_TRUE(auth_session.cryptohome_recovery_ephemeral_pub_key_for_testing()
                  .has_value());
  EXPECT_EQ(
      auth_session.cryptohome_recovery_ephemeral_pub_key_for_testing().value(),
      brillo::SecureBlob("test"));

  // Test.
  // Setting the expectation that the auth block utility will derive key blobs.
  EXPECT_CALL(auth_block_utility_, DeriveKeyBlobs(_, _, _))
      .WillOnce([&](const AuthInput& auth_input,
                    const AuthBlockState& auth_block_state,
                    KeyBlobs& out_key_blobs) {
        out_key_blobs.vkk_key = kFakePerCredentialSecret;
        return OkStatus<CryptohomeCryptoError>();
      });
  // Calling AuthenticateAuthFactor.
  user_data_auth::AuthenticateAuthFactorRequest authenticate_request;
  authenticate_request.set_auth_session_id(auth_session.serialized_token());
  authenticate_request.set_auth_factor_label(kFakeLabel);
  authenticate_request.mutable_auth_input()
      ->mutable_cryptohome_recovery_input()
      ->mutable_recovery_response();
  bool authenticate_called = false;
  user_data_auth::CryptohomeErrorCode authenticate_error =
      user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
  EXPECT_TRUE(auth_session.AuthenticateAuthFactor(
      authenticate_request,
      base::BindOnce(
          [](bool& called, user_data_auth::CryptohomeErrorCode& error,
             const user_data_auth::AuthenticateAuthFactorReply& reply) {
            called = true;
            error = reply.error();
          },
          std::ref(authenticate_called), std::ref(authenticate_error))));

  // Verify.
  EXPECT_EQ(auth_session.GetStatus(), AuthStatus::kAuthStatusAuthenticated);
  EXPECT_NE(auth_session.user_secret_stash_for_testing(), nullptr);
  EXPECT_NE(auth_session.user_secret_stash_main_key_for_testing(),
            std::nullopt);
}

}  // namespace cryptohome
