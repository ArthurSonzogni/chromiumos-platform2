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
#include <base/test/task_environment.h>
#include <base/timer/mock_timer.h>
#include <brillo/cryptohome.h>
#include <cryptohome/proto_bindings/auth_factor.pb.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>
#include <gtest/gtest.h>
#include <libhwsec-foundation/crypto/aes.h>

#include "cryptohome/auth_blocks/auth_block_state.h"
#include "cryptohome/auth_blocks/mock_auth_block_utility.h"
#include "cryptohome/auth_factor/auth_factor.h"
#include "cryptohome/auth_factor/auth_factor_manager.h"
#include "cryptohome/auth_factor/auth_factor_metadata.h"
#include "cryptohome/auth_factor/auth_factor_type.h"
#include "cryptohome/crypto_error.h"
#include "cryptohome/cryptohome_common.h"
#include "cryptohome/key_objects.h"
#include "cryptohome/mock_crypto.h"
#include "cryptohome/mock_keyset_management.h"
#include "cryptohome/mock_platform.h"
#include "cryptohome/user_secret_stash.h"
#include "cryptohome/user_secret_stash_storage.h"

using brillo::cryptohome::home::SanitizeUserName;
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
// Fake passwords to be in used in this test suite.
constexpr char kFakePass[] = "test_pass";
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

 protected:
  base::test::SingleThreadTaskEnvironment task_environment_;
  // Mock and fake objects, will be passed to AuthSession for its internal use.
  NiceMock<MockCrypto> crypto_;
  NiceMock<MockPlatform> platform_;
  NiceMock<MockKeysetManagement> keyset_management_;
  NiceMock<MockAuthBlockUtility> auth_block_utility_;
  AuthFactorManager auth_factor_manager_{&platform_};
  UserSecretStashStorage user_secret_stash_storage_{&platform_};
};

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
  MountError error;
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
  ASSERT_TRUE(auth_session.timer_.IsRunning());
  auth_session.timer_.FireNow();
  EXPECT_EQ(auth_session.GetStatus(), AuthStatus::kAuthStatusTimedOut);
  EXPECT_TRUE(called);
  cryptohome::AuthorizationRequest authorization_request;
  authorization_request.mutable_key()->set_secret(kFakePass);
  authorization_request.mutable_key()->mutable_data()->set_label(kFakeLabel);
  std::unique_ptr<Credentials> test_creds =
      auth_session.GetCredentials(authorization_request, &error);

  // VERIFY
  // SerializeToString is used in the absence of a comparator for KeyData
  // protobuf.
  std::string key_data_serialized1 = "1";
  std::string key_data_serialized2 = "2";
  test_creds->key_data().SerializeToString(&key_data_serialized1);
  authorization_request.mutable_key()->data().SerializeToString(
      &key_data_serialized2);
  EXPECT_EQ(key_data_serialized1, key_data_serialized2);
}

// This test check AuthSession::GetCredential function for a kiosk user and
// ensures that the fields are set as they should be.
TEST_F(AuthSessionTest, GetCredentialKioskUser) {
  // SETUP
  MountError error;
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
  ASSERT_TRUE(auth_session.timer_.IsRunning());
  auth_session.timer_.FireNow();
  EXPECT_EQ(auth_session.GetStatus(), AuthStatus::kAuthStatusTimedOut);
  EXPECT_TRUE(called);
  cryptohome::AuthorizationRequest authorization_request;
  authorization_request.mutable_key()->mutable_data()->set_label(kFakeLabel);
  authorization_request.mutable_key()->mutable_data()->set_type(
      KeyData::KEY_TYPE_KIOSK);
  std::unique_ptr<Credentials> test_creds =
      auth_session.GetCredentials(authorization_request, &error);

  // VERIFY
  // SerializeToString is used in the absence of a comparator for KeyData
  // protobuf.
  std::string key_data_serialized1 = "1";
  std::string key_data_serialized2 = "2";
  test_creds->key_data().SerializeToString(&key_data_serialized1);
  authorization_request.mutable_key()->data().SerializeToString(
      &key_data_serialized2);
  EXPECT_EQ(key_data_serialized1, key_data_serialized2);
  EXPECT_EQ(test_creds->passkey(), fake_pass_blob);
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
  AuthSession auth_session(kFakeUsername, flags, std::move(on_timeout),
                           &crypto_, &keyset_management_, &auth_block_utility_,
                           &auth_factor_manager_, &user_secret_stash_storage_);

  // Test.
  EXPECT_THAT(AuthStatus::kAuthStatusFurtherFactorRequired,
              auth_session.GetStatus());
  EXPECT_FALSE(auth_session.user_exists());
  ASSERT_TRUE(auth_session.timer_.IsRunning());

  user_data_auth::AddCredentialsRequest add_cred_request;
  cryptohome::AuthorizationRequest* authorization_request =
      add_cred_request.mutable_authorization();
  authorization_request->mutable_key()->set_secret(kFakePass);
  authorization_request->mutable_key()->mutable_data()->set_label(kFakeLabel);

  EXPECT_CALL(keyset_management_, AddInitialKeyset(_, _))
      .WillOnce(Return(ByMove(std::make_unique<VaultKeyset>())));

  // Verify.
  EXPECT_THAT(user_data_auth::CRYPTOHOME_ERROR_NOT_SET,
              auth_session.OnUserCreated());
  EXPECT_EQ(auth_session.GetStatus(), AuthStatus::kAuthStatusAuthenticated);
  EXPECT_THAT(user_data_auth::CRYPTOHOME_ERROR_NOT_SET,
              auth_session.AddCredentials(add_cred_request));
  EXPECT_EQ(auth_session.GetStatus(), AuthStatus::kAuthStatusAuthenticated);
}

// Test if AuthSession correctly adds new credentials for a new user, even when
// called twice. The first credential gets added as an initial keyset, and the
// second as a regular one.
TEST_F(AuthSessionTest, AddCredentialNewUserTwice) {
  // Setup.
  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;
  // Setting the expectation that the user does not exist.
  EXPECT_CALL(keyset_management_, UserExists(_)).WillRepeatedly(Return(false));
  AuthSession auth_session(kFakeUsername, flags,
                           /*on_timeout=*/base::DoNothing(), &crypto_,
                           &keyset_management_, &auth_block_utility_,
                           &auth_factor_manager_, &user_secret_stash_storage_);

  // Test adding the first credential.
  EXPECT_THAT(AuthStatus::kAuthStatusFurtherFactorRequired,
              auth_session.GetStatus());
  EXPECT_FALSE(auth_session.user_exists());
  ASSERT_TRUE(auth_session.timer_.IsRunning());

  user_data_auth::AddCredentialsRequest add_cred_request;
  cryptohome::AuthorizationRequest* authorization_request =
      add_cred_request.mutable_authorization();
  authorization_request->mutable_key()->set_secret(kFakePass);
  authorization_request->mutable_key()->mutable_data()->set_label(kFakeLabel);

  EXPECT_CALL(keyset_management_, AddInitialKeyset(_, _))
      .WillOnce(Return(ByMove(std::make_unique<VaultKeyset>())));

  EXPECT_THAT(user_data_auth::CRYPTOHOME_ERROR_NOT_SET,
              auth_session.OnUserCreated());
  EXPECT_EQ(auth_session.GetStatus(), AuthStatus::kAuthStatusAuthenticated);
  EXPECT_THAT(user_data_auth::CRYPTOHOME_ERROR_NOT_SET,
              auth_session.AddCredentials(add_cred_request));
  EXPECT_EQ(auth_session.GetStatus(), AuthStatus::kAuthStatusAuthenticated);
  // Test adding the second credential.
  user_data_auth::AddCredentialsRequest add_other_cred_request;
  cryptohome::AuthorizationRequest* other_authorization_request =
      add_other_cred_request.mutable_authorization();
  other_authorization_request->mutable_key()->set_secret(kFakeOtherPass);
  other_authorization_request->mutable_key()->mutable_data()->set_label(
      kFakeOtherLabel);

  EXPECT_CALL(keyset_management_, AddKeyset(_, _, _))
      .WillOnce(Return(CRYPTOHOME_ERROR_NOT_SET));

  EXPECT_THAT(user_data_auth::CRYPTOHOME_ERROR_NOT_SET,
              auth_session.AddCredentials(add_other_cred_request));
  EXPECT_EQ(auth_session.GetStatus(), AuthStatus::kAuthStatusAuthenticated);
}

// Test if AuthSession correctly authenticates existing credentials for a
// user.
TEST_F(AuthSessionTest, AuthenticateExistingUser) {
  // Setup.
  bool called = false;
  auto on_timeout = base::BindOnce(
      [](bool& called, const base::UnguessableToken&) { called = true; },
      std::ref(called));
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
  ASSERT_TRUE(auth_session.timer_.IsRunning());

  cryptohome::AuthorizationRequest authorization_request;
  authorization_request.mutable_key()->set_secret(kFakePass);
  authorization_request.mutable_key()->mutable_data()->set_label(kFakeLabel);

  auto vk = std::make_unique<VaultKeyset>();
  EXPECT_CALL(keyset_management_, GetValidKeyset(_, _))
      .WillOnce(Return(ByMove(std::move(vk))));
  EXPECT_CALL(keyset_management_, ReSaveKeysetIfNeeded(_, _))
      .WillOnce(Return(true));

  // Verify.
  EXPECT_THAT(user_data_auth::CRYPTOHOME_ERROR_NOT_SET,
              auth_session.Authenticate(authorization_request));
  EXPECT_EQ(AuthStatus::kAuthStatusAuthenticated, auth_session.GetStatus());
  EXPECT_TRUE(auth_session.TakeCredentialVerifier()->Verify(
      brillo::SecureBlob(kFakePass)));

  // Cleanup.
  auth_session.timer_.FireNow();
  EXPECT_THAT(AuthStatus::kAuthStatusTimedOut, auth_session.GetStatus());
  EXPECT_TRUE(called);
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
  EXPECT_THAT(AuthStatus::kAuthStatusFurtherFactorRequired,
              auth_session.GetStatus());
  EXPECT_FALSE(auth_session.user_exists());
  ASSERT_TRUE(auth_session.timer_.IsRunning());

  user_data_auth::AddCredentialsRequest add_cred_request;
  cryptohome::AuthorizationRequest* authorization_request =
      add_cred_request.mutable_authorization();
  authorization_request->mutable_key()->set_secret(kFakePass);
  authorization_request->mutable_key()->mutable_data()->set_label(kFakeLabel);

  EXPECT_CALL(keyset_management_, AddInitialKeyset(_, _)).Times(0);

  // Verify.
  EXPECT_THAT(user_data_auth::CRYPTOHOME_ERROR_NOT_SET,
              auth_session.AddCredentials(add_cred_request));
  EXPECT_EQ(auth_session.GetStatus(),
            AuthStatus::kAuthStatusFurtherFactorRequired);
}

// Test if AuthSession correctly updates existing credentials for a new user.
TEST_F(AuthSessionTest, UpdateCredentialUnauthenticatedAUthSession) {
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
  EXPECT_THAT(user_data_auth::CRYPTOHOME_ERROR_UNAUTHENTICATED_AUTH_SESSION,
              auth_session.UpdateCredential(update_cred_request));
}

// Test if AuthSession correctly updates existing credentials for a new user.
TEST_F(AuthSessionTest, UpdateCredentialuccess) {
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
  auth_session.SetStatus(AuthStatus::kAuthStatusAuthenticated);
  user_data_auth::UpdateCredentialRequest update_cred_request;
  cryptohome::AuthorizationRequest* authorization_request =
      update_cred_request.mutable_authorization();
  authorization_request->mutable_key()->set_secret(kFakePass);
  authorization_request->mutable_key()->mutable_data()->set_label(kFakeLabel);
  update_cred_request.set_old_credential_label(kFakeLabel);

  // Test.
  EXPECT_CALL(keyset_management_, UpdateKeyset(_, _))
      .WillOnce(Return(CRYPTOHOME_ERROR_NOT_SET));
  EXPECT_THAT(user_data_auth::CRYPTOHOME_ERROR_NOT_SET,
              auth_session.UpdateCredential(update_cred_request));
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
  EXPECT_THAT(user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT,
              auth_session.UpdateCredential(update_cred_request));
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
  auth_session.OnUserCreated();

  // Verify.
  EXPECT_EQ(auth_session.user_secret_stash_for_testing(), nullptr);
  EXPECT_EQ(auth_session.user_secret_stash_main_key_for_testing(),
            std::nullopt);
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
  auth_session.OnUserCreated();

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
  auth_session.OnUserCreated();

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
  auth_session.OnUserCreated();
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
        return CryptoError::CE_NONE;
      });
  // Calling AddAuthFactor.
  user_data_auth::AddAuthFactorRequest request;
  request.set_auth_session_id(auth_session.serialized_token());
  request.mutable_auth_factor()->set_type(
      user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
  request.mutable_auth_factor()->set_label(kFakeLabel);
  request.mutable_auth_factor()->mutable_password_metadata();
  request.mutable_auth_input()->mutable_password_input()->set_secret(kFakePass);
  EXPECT_EQ(auth_session.AddAuthFactor(request),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);

  // Verify.
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
  EXPECT_EQ(auth_session.AddAuthFactor(request),
            user_data_auth::CRYPTOHOME_ERROR_UNAUTHENTICATED_AUTH_SESSION);
}

TEST_F(AuthSessionWithUssExperimentTest, AuthenticatePasswordAuthFactorViaUss) {
  // Setup.
  const std::string obfuscated_username = SanitizeUserName(kFakeUsername);
  const brillo::SecureBlob kFakePerCredentialSecret("fake-vkk");
  // Setting the expectation that the user exists.
  EXPECT_CALL(keyset_management_, UserExists(_)).WillRepeatedly(Return(true));
  // Generating the USS.
  std::unique_ptr<UserSecretStash> uss =
      UserSecretStash::CreateRandom(FileSystemKeyset::CreateRandom());
  ASSERT_NE(uss, nullptr);
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
      auth_factor_manager_.SaveAuthFactor(obfuscated_username, auth_factor));
  // Adding the auth factor into the USS and persisting the latter.
  const KeyBlobs key_blobs = {.vkk_key = kFakePerCredentialSecret};
  std::optional<brillo::SecureBlob> wrapping_key =
      key_blobs.DeriveUssCredentialSecret();
  ASSERT_TRUE(wrapping_key.has_value());
  EXPECT_TRUE(uss->AddWrappedMainKey(uss_main_key.value(), kFakeLabel,
                                     wrapping_key.value()));
  std::optional<brillo::SecureBlob> encrypted_uss =
      uss->GetEncryptedContainer(uss_main_key.value());
  ASSERT_TRUE(encrypted_uss.has_value());
  EXPECT_TRUE(user_secret_stash_storage_.Persist(encrypted_uss.value(),
                                                 obfuscated_username));
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
        return CryptoError::CE_NONE;
      });
  // Calling AuthenticateAuthFactor.
  user_data_auth::AuthenticateAuthFactorRequest request;
  request.set_auth_session_id(auth_session.serialized_token());
  request.set_auth_factor_label(kFakeLabel);
  request.mutable_auth_input()->mutable_password_input()->set_secret(kFakePass);
  EXPECT_EQ(auth_session.AuthenticateAuthFactor(request),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);

  // Verify.
  EXPECT_EQ(auth_session.GetStatus(), AuthStatus::kAuthStatusAuthenticated);
  EXPECT_NE(auth_session.user_secret_stash_for_testing(), nullptr);
  EXPECT_NE(auth_session.user_secret_stash_main_key_for_testing(),
            std::nullopt);
}

}  // namespace cryptohome
