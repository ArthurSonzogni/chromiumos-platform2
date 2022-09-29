// Copyright 2020 The ChromiumOS Authors
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
#include <base/run_loop.h>
#include <base/task/sequenced_task_runner.h>
#include <base/test/bind.h>
#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <base/threading/sequenced_task_runner_handle.h>
#include <base/timer/mock_timer.h>
#include <brillo/cryptohome.h>
#include <cryptohome/proto_bindings/auth_factor.pb.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libhwsec/frontend/cryptohome/mock_frontend.h>
#include <libhwsec/frontend/pinweaver/mock_frontend.h>
#include <libhwsec-foundation/crypto/aes.h>
#include <libhwsec-foundation/error/testing_helper.h>

#include "cryptohome/auth_blocks/auth_block_utility_impl.h"
#include "cryptohome/auth_blocks/mock_auth_block_utility.h"
#include "cryptohome/auth_factor/auth_factor.h"
#include "cryptohome/auth_factor/auth_factor_manager.h"
#include "cryptohome/auth_factor/auth_factor_metadata.h"
#include "cryptohome/auth_factor/auth_factor_type.h"
#include "cryptohome/credential_verifier_test_utils.h"
#include "cryptohome/crypto_error.h"
#include "cryptohome/cryptohome_common.h"
#include "cryptohome/flatbuffer_schemas/auth_block_state.h"
#include "cryptohome/key_objects.h"
#include "cryptohome/mock_cryptohome_keys_manager.h"
#include "cryptohome/mock_keyset_management.h"
#include "cryptohome/mock_platform.h"
#include "cryptohome/user_secret_stash.h"
#include "cryptohome/user_secret_stash_storage.h"
#include "cryptohome/user_session/mock_user_session.h"
#include "cryptohome/user_session/user_session_map.h"

namespace cryptohome {
namespace {

using base::test::TestFuture;
using brillo::cryptohome::home::SanitizeUserName;
using cryptohome::error::CryptohomeCryptoError;
using cryptohome::error::CryptohomeError;
using cryptohome::error::CryptohomeMountError;
using hwsec_foundation::error::testing::IsOk;
using hwsec_foundation::error::testing::NotOk;
using hwsec_foundation::error::testing::ReturnError;
using hwsec_foundation::error::testing::ReturnValue;
using hwsec_foundation::status::MakeStatus;
using hwsec_foundation::status::OkStatus;
using hwsec_foundation::status::StatusChain;
using ::testing::_;
using ::testing::ByMove;
using ::testing::ElementsAre;
using ::testing::Field;
using ::testing::IsEmpty;
using ::testing::Matcher;
using ::testing::NiceMock;
using ::testing::Pair;
using ::testing::Return;
using ::testing::UnorderedElementsAre;
using ::testing::VariantWith;

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

// Set to match the 5 minute timer and a 1 minute extension in AuthSession.
constexpr int kAuthSessionExtensionDuration = 60;
constexpr auto kAuthSessionTimeout = base::Minutes(5);
constexpr auto kAuthSessionExtension =
    base::Seconds(kAuthSessionExtensionDuration);

// Returns a blob "derived" from provided blob to generate fake vkk_key from
// user secret in tests.
brillo::SecureBlob GetFakeDerivedSecret(const brillo::SecureBlob& blob) {
  return brillo::SecureBlob::Combine(blob,
                                     brillo::SecureBlob(" derived secret"));
}

// A matcher that checks if an auth block state has a particular type.
template <typename StateType>
Matcher<const AuthBlockState&> AuthBlockStateTypeIs() {
  return Field(&AuthBlockState::state, VariantWith<StateType>(_));
}

SerializedVaultKeyset CreateFakePasswordVk(const std::string& label) {
  SerializedVaultKeyset serialized_vk;
  serialized_vk.set_flags(SerializedVaultKeyset::TPM_WRAPPED |
                          SerializedVaultKeyset::SCRYPT_DERIVED |
                          SerializedVaultKeyset::PCR_BOUND |
                          SerializedVaultKeyset::ECC);
  serialized_vk.set_password_rounds(1);
  serialized_vk.set_tpm_key("tpm-key");
  serialized_vk.set_extended_tpm_key("tpm-extended-key");
  serialized_vk.set_vkk_iv("iv");
  serialized_vk.mutable_key_data()->set_type(KeyData::KEY_TYPE_PASSWORD);
  serialized_vk.mutable_key_data()->set_label(label);
  return serialized_vk;
}

}  // namespace

class AuthSessionTest : public ::testing::Test {
 public:
  AuthSessionTest() = default;
  AuthSessionTest(const AuthSessionTest&) = delete;
  AuthSessionTest& operator=(const AuthSessionTest&) = delete;
  ~AuthSessionTest() override = default;

  void SetUp() override {
    EXPECT_CALL(hwsec_, IsEnabled()).WillRepeatedly(ReturnValue(true));
    EXPECT_CALL(hwsec_, IsReady()).WillRepeatedly(ReturnValue(true));
    EXPECT_CALL(hwsec_, IsSealingSupported()).WillRepeatedly(ReturnValue(true));
    EXPECT_CALL(hwsec_, GetManufacturer())
        .WillRepeatedly(ReturnValue(0x43524f53));
    EXPECT_CALL(hwsec_, GetAuthValue(_, _))
        .WillRepeatedly(ReturnValue(brillo::SecureBlob()));
    EXPECT_CALL(hwsec_, SealWithCurrentUser(_, _, _))
        .WillRepeatedly(ReturnValue(brillo::Blob()));
    EXPECT_CALL(hwsec_, GetPubkeyHash(_))
        .WillRepeatedly(ReturnValue(brillo::Blob()));
    EXPECT_CALL(pinweaver_, IsEnabled()).WillRepeatedly(ReturnValue(true));
    crypto_.Init();
  }

 protected:
  user_data_auth::CryptohomeErrorCode AuthenticateAuthFactorVK(
      const std::string& label,
      const std::string& passkey,
      AuthSession& auth_session) {
    EXPECT_CALL(keyset_management_, GetVaultKeyset(_, label))
        .WillRepeatedly(Return(ByMove(std::make_unique<VaultKeyset>())));

    EXPECT_CALL(auth_block_utility_,
                GetAuthBlockStateFromVaultKeyset(label, _, _))
        .WillRepeatedly(Return(true));
    EXPECT_CALL(auth_block_utility_, GetAuthBlockTypeFromState(_))
        .WillRepeatedly(Return(AuthBlockType::kTpmBoundToPcr));
    EXPECT_CALL(keyset_management_, GetValidKeysetWithKeyBlobs(_, _, _))
        .WillRepeatedly(Return(ByMove(std::make_unique<VaultKeyset>())));

    EXPECT_CALL(keyset_management_, ShouldReSaveKeyset(_))
        .WillRepeatedly(Return(false));
    EXPECT_CALL(keyset_management_, AddResetSeedIfMissing(_))
        .WillRepeatedly(Return(false));

    auto key_blobs = std::make_unique<KeyBlobs>();
    EXPECT_CALL(auth_block_utility_,
                DeriveKeyBlobsWithAuthBlockAsync(_, _, _, _))
        .WillRepeatedly(
            [&key_blobs](AuthBlockType auth_block_type,
                         const AuthInput& auth_input,
                         const AuthBlockState& auth_state,
                         AuthBlock::DeriveCallback derive_callback) {
              std::move(derive_callback)
                  .Run(OkStatus<CryptohomeCryptoError>(), std::move(key_blobs));
              return true;
            });

    user_data_auth::AuthenticateAuthFactorRequest request;
    request.set_auth_session_id(auth_session.serialized_token());
    request.set_auth_factor_label(label);
    request.mutable_auth_input()->mutable_password_input()->set_secret(passkey);

    TestFuture<CryptohomeStatus> authenticate_future;
    auth_session.AuthenticateAuthFactor(request,
                                        authenticate_future.GetCallback());

    if (authenticate_future.Get().ok()) {
      return user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
    }
    return authenticate_future.Get()->local_legacy_error().value();
  }

  base::test::SingleThreadTaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  scoped_refptr<base::SequencedTaskRunner> task_runner_ =
      base::SequencedTaskRunnerHandle::Get();

  // Mock and fake objects, will be passed to AuthSession for its internal
  // use.
  NiceMock<hwsec::MockCryptohomeFrontend> hwsec_;
  NiceMock<hwsec::MockPinWeaverFrontend> pinweaver_;
  NiceMock<MockPlatform> platform_;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager_;
  Crypto crypto_{&hwsec_, &pinweaver_, &cryptohome_keys_manager_, nullptr};
  NiceMock<MockKeysetManagement> keyset_management_;
  NiceMock<MockAuthBlockUtility> auth_block_utility_;
  AuthFactorManager auth_factor_manager_{&platform_};
  UserSecretStashStorage user_secret_stash_storage_{&platform_};
  UserSessionMap user_session_map_;
};

const CryptohomeError::ErrorLocationPair kErrorLocationForTestingAuthSession =
    CryptohomeError::ErrorLocationPair(
        static_cast<::cryptohome::error::CryptohomeError::ErrorLocation>(1),
        std::string("MockErrorLocationAuthSession"));

TEST_F(AuthSessionTest, InitiallyNotAuthenticated) {
  AuthSession auth_session(
      kFakeUsername, user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE,
      AuthIntent::kDecrypt,
      /*on_timeout=*/base::DoNothing(), &crypto_, &platform_,
      &user_session_map_, &keyset_management_, &auth_block_utility_,
      &auth_factor_manager_, &user_secret_stash_storage_,
      /*enable_create_backup_vk_with_uss =*/false);

  EXPECT_EQ(auth_session.GetStatus(),
            AuthStatus::kAuthStatusFurtherFactorRequired);
  EXPECT_THAT(auth_session.authorized_intents(), IsEmpty());
}

TEST_F(AuthSessionTest, InitiallyNotAuthenticatedForExistingUser) {
  EXPECT_CALL(keyset_management_, UserExists(_)).WillRepeatedly(Return(true));
  AuthSession auth_session(
      kFakeUsername, user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE,
      AuthIntent::kDecrypt,
      /*on_timeout=*/base::DoNothing(), &crypto_, &platform_,
      &user_session_map_, &keyset_management_, &auth_block_utility_,
      &auth_factor_manager_, &user_secret_stash_storage_,
      /*enable_create_backup_vk_with_uss =*/false);

  EXPECT_EQ(auth_session.GetStatus(),
            AuthStatus::kAuthStatusFurtherFactorRequired);
  EXPECT_THAT(auth_session.authorized_intents(), IsEmpty());
}

TEST_F(AuthSessionTest, Username) {
  AuthSession auth_session(
      kFakeUsername, user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE,
      AuthIntent::kDecrypt,
      /*on_timeout=*/base::DoNothing(), &crypto_, &platform_,
      &user_session_map_, &keyset_management_, &auth_block_utility_,
      &auth_factor_manager_, &user_secret_stash_storage_,
      /*enable_create_backup_vk_with_uss =*/false);

  EXPECT_EQ(auth_session.username(), kFakeUsername);
  EXPECT_EQ(auth_session.obfuscated_username(),
            SanitizeUserName(kFakeUsername));
}

TEST_F(AuthSessionTest, Intent) {
  AuthSession decryption_auth_session(
      kFakeUsername, user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE,
      AuthIntent::kDecrypt,
      /*on_timeout=*/base::DoNothing(), &crypto_, &platform_,
      &user_session_map_, &keyset_management_, &auth_block_utility_,
      &auth_factor_manager_, &user_secret_stash_storage_,
      /*enable_create_backup_vk_with_uss =*/false);
  AuthSession verification_auth_session(
      kFakeUsername, user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE,
      AuthIntent::kVerifyOnly,
      /*on_timeout=*/base::DoNothing(), &crypto_, &platform_,
      &user_session_map_, &keyset_management_, &auth_block_utility_,
      &auth_factor_manager_, &user_secret_stash_storage_,
      /*enable_create_backup_vk_with_uss =*/false);
  AuthSession webauthn_auth_session(
      kFakeUsername, user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE,
      AuthIntent::kWebAuthn,
      /*on_timeout=*/base::DoNothing(), &crypto_, &platform_,
      &user_session_map_, &keyset_management_, &auth_block_utility_,
      &auth_factor_manager_, &user_secret_stash_storage_,
      /*enable_create_backup_vk_with_uss =*/false);

  EXPECT_EQ(decryption_auth_session.auth_intent(), AuthIntent::kDecrypt);
  EXPECT_EQ(verification_auth_session.auth_intent(), AuthIntent::kVerifyOnly);
  EXPECT_EQ(webauthn_auth_session.auth_intent(), AuthIntent::kWebAuthn);
}

TEST_F(AuthSessionTest, TimeoutTest) {
  bool called = false;
  auto on_timeout = base::BindOnce(
      [](bool* called, const base::UnguessableToken&) { *called = true; },
      base::Unretained(&called));
  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;
  AuthSession auth_session(
      kFakeUsername, flags, AuthIntent::kDecrypt, std::move(on_timeout),
      &crypto_, &platform_, &user_session_map_, &keyset_management_,
      &auth_block_utility_, &auth_factor_manager_, &user_secret_stash_storage_,
      /*enable_create_backup_vk_with_uss =*/false);
  EXPECT_EQ(auth_session.GetStatus(),
            AuthStatus::kAuthStatusFurtherFactorRequired);
  auth_session.SetAuthSessionAsAuthenticated(kAuthorizedIntentsForFullAuth);

  ASSERT_TRUE(auth_session.timeout_timer_.IsRunning());
  auth_session.timeout_timer_.FireNow();
  EXPECT_EQ(auth_session.GetStatus(), AuthStatus::kAuthStatusTimedOut);
  EXPECT_THAT(auth_session.authorized_intents(), IsEmpty());
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
  AuthSession auth_session(
      kFakeUsername, flags, AuthIntent::kDecrypt, std::move(on_timeout),
      &crypto_, &platform_, &user_session_map_, &keyset_management_,
      &auth_block_utility_, &auth_factor_manager_, &user_secret_stash_storage_,
      /*enable_create_backup_vk_with_uss =*/false);
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

  AuthSession auth_session(
      kFakeUsername, 0, AuthIntent::kDecrypt, std::move(on_timeout), &crypto_,
      &platform_, &user_session_map_, &keyset_management_, &auth_block_utility_,
      &auth_factor_manager_, &user_secret_stash_storage_,
      /*enable_create_backup_vk_with_uss =*/false);
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
      std::make_unique<AuthBlockUtilityImpl>(
          &keyset_management_, &crypto_, &platform_,
          FingerprintAuthBlockService::MakeNullService());
  // Setting the expectation that the user does not exist.
  AuthSession auth_session(
      kFakeUsername, flags, AuthIntent::kDecrypt, std::move(on_timeout),
      &crypto_, &platform_, &user_session_map_, &keyset_management_,
      auth_block_utility_impl_.get(), &auth_factor_manager_,
      &user_secret_stash_storage_, /*enable_create_backup_vk_with_uss =*/false);

  // Test.
  EXPECT_THAT(AuthStatus::kAuthStatusFurtherFactorRequired,
              auth_session.GetStatus());
  EXPECT_FALSE(auth_session.user_exists());
  EXPECT_TRUE(auth_session.OnUserCreated().ok());
  EXPECT_EQ(auth_session.GetStatus(), AuthStatus::kAuthStatusAuthenticated);

  EXPECT_CALL(keyset_management_,
              AddInitialKeysetWithKeyBlobs(_, _, _, _, _, _))
      .WillOnce(Return(ByMove(std::make_unique<VaultKeyset>())));
  EXPECT_CALL(keyset_management_, GetVaultKeyset(_, kFakeLabel))
      .WillOnce([](const std::string&, const std::string&) {
        auto vk = std::make_unique<VaultKeyset>();
        vk->InitializeFromSerialized(CreateFakePasswordVk(kFakeLabel));
        return vk;
      });

  EXPECT_EQ(auth_session.GetStatus(), AuthStatus::kAuthStatusAuthenticated);
  EXPECT_THAT(
      auth_session.authorized_intents(),
      UnorderedElementsAre(AuthIntent::kDecrypt, AuthIntent::kVerifyOnly));

  user_data_auth::AddCredentialsRequest add_cred_request;
  cryptohome::AuthorizationRequest* authorization_request =
      add_cred_request.mutable_authorization();
  authorization_request->mutable_key()->set_secret(kFakePass);
  authorization_request->mutable_key()->mutable_data()->set_label(kFakeLabel);

  TestFuture<CryptohomeStatus> add_future;
  auth_session.AddCredentials(add_cred_request, add_future.GetCallback());

  // Verify.
  EXPECT_THAT(add_future.Get(), IsOk());
  ASSERT_TRUE(auth_session.timeout_timer_.IsRunning());

  EXPECT_EQ(auth_session.GetStatus(), AuthStatus::kAuthStatusAuthenticated);
  EXPECT_THAT(
      auth_session.authorized_intents(),
      UnorderedElementsAre(AuthIntent::kDecrypt, AuthIntent::kVerifyOnly));
  EXPECT_THAT(auth_session.TakeCredentialVerifier(),
              IsVerifierPtrForPassword(kFakePass));
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
      std::make_unique<AuthBlockUtilityImpl>(
          &keyset_management_, &crypto_, &platform_,
          FingerprintAuthBlockService::MakeNullService());
  // Setting the expectation that the user does not exist.
  EXPECT_CALL(keyset_management_, UserExists(_)).WillRepeatedly(Return(false));
  AuthSession auth_session(kFakeUsername, flags, AuthIntent::kDecrypt,
                           /*on_timeout=*/base::DoNothing(), &crypto_,
                           &platform_, &user_session_map_, &keyset_management_,
                           auth_block_utility_impl_.get(),
                           &auth_factor_manager_, &user_secret_stash_storage_,
                           /*enable_create_backup_vk_with_uss =*/false);

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
  EXPECT_CALL(keyset_management_, GetVaultKeyset(_, _))
      .WillRepeatedly([](const std::string&, const std::string& label) {
        auto vk = std::make_unique<VaultKeyset>();
        vk->InitializeFromSerialized(CreateFakePasswordVk(label));
        return vk;
      });

  EXPECT_TRUE(auth_session.OnUserCreated().ok());
  ASSERT_TRUE(auth_session.timeout_timer_.IsRunning());

  EXPECT_EQ(auth_session.GetStatus(), AuthStatus::kAuthStatusAuthenticated);

  EXPECT_THAT(
      auth_session.authorized_intents(),
      UnorderedElementsAre(AuthIntent::kDecrypt, AuthIntent::kVerifyOnly));

  TestFuture<CryptohomeStatus> add_future;
  auth_session.AddCredentials(add_cred_request, add_future.GetCallback());

  // Verify.
  EXPECT_EQ(auth_session.GetStatus(), AuthStatus::kAuthStatusAuthenticated);
  EXPECT_THAT(
      auth_session.authorized_intents(),
      UnorderedElementsAre(AuthIntent::kDecrypt, AuthIntent::kVerifyOnly));

  // Test adding the second credential.
  // Set up expectation in callback for success.
  user_data_auth::AddCredentialsRequest add_other_cred_request;
  cryptohome::AuthorizationRequest* other_authorization_request =
      add_other_cred_request.mutable_authorization();
  other_authorization_request->mutable_key()->set_secret(kFakeOtherPass);
  other_authorization_request->mutable_key()->mutable_data()->set_label(
      kFakeOtherLabel);

  EXPECT_CALL(keyset_management_, AddKeysetWithKeyBlobs(_, _, _, _, _, _))
      .WillOnce(Return(CRYPTOHOME_ERROR_NOT_SET));

  // Test.
  TestFuture<CryptohomeStatus> add_other_future;
  auth_session.AddCredentials(add_other_cred_request,
                              add_other_future.GetCallback());

  // Verify.
  ASSERT_THAT(add_other_future.Get(), IsOk());

  EXPECT_EQ(auth_session.GetStatus(), AuthStatus::kAuthStatusAuthenticated);
  EXPECT_THAT(
      auth_session.authorized_intents(),
      UnorderedElementsAre(AuthIntent::kDecrypt, AuthIntent::kVerifyOnly));
  ASSERT_TRUE(auth_session.timeout_timer_.IsRunning());
  EXPECT_THAT(auth_session.TakeCredentialVerifier(),
              IsVerifierPtrForPassword(kFakePass));
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
  AuthSession auth_session(
      kFakeUsername, flags, AuthIntent::kDecrypt, std::move(on_timeout),
      &crypto_, &platform_, &user_session_map_, &keyset_management_,
      &auth_block_utility_, &auth_factor_manager_, &user_secret_stash_storage_,
      /*enable_create_backup_vk_with_uss =*/false);

  // Test.
  EXPECT_THAT(AuthStatus::kAuthStatusFurtherFactorRequired,
              auth_session.GetStatus());
  EXPECT_TRUE(auth_session.user_exists());

  cryptohome::AuthorizationRequest authorization_request;
  authorization_request.mutable_key()->set_secret(kFakePass);
  authorization_request.mutable_key()->mutable_data()->set_label(kFakeLabel);

  EXPECT_CALL(auth_block_utility_, GetAuthBlockStateFromVaultKeyset(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(auth_block_utility_, GetAuthBlockTypeFromState(_))
      .WillRepeatedly(Return(AuthBlockType::kTpmBoundToPcr));
  EXPECT_CALL(keyset_management_, GetValidKeysetWithKeyBlobs(_, _, _))
      .WillOnce(Return(ByMove(std::make_unique<VaultKeyset>())));
  EXPECT_CALL(keyset_management_, ShouldReSaveKeyset(_))
      .WillOnce(Return(false));

  auto key_blobs = std::make_unique<KeyBlobs>();
  EXPECT_CALL(auth_block_utility_, DeriveKeyBlobsWithAuthBlockAsync(_, _, _, _))
      .WillOnce([&key_blobs](AuthBlockType auth_block_type,
                             const AuthInput& auth_input,
                             const AuthBlockState& auth_state,
                             AuthBlock::DeriveCallback derive_callback) {
        std::move(derive_callback)
            .Run(OkStatus<CryptohomeCryptoError>(), std::move(key_blobs));
        return true;
      });

  TestFuture<CryptohomeStatus> authenticate_future;
  auth_session.Authenticate(authorization_request,
                            authenticate_future.GetCallback());

  // Verify.
  EXPECT_THAT(authenticate_future.Get(), IsOk());
  ASSERT_TRUE(auth_session.timeout_timer_.IsRunning());

  EXPECT_EQ(AuthStatus::kAuthStatusAuthenticated, auth_session.GetStatus());
  EXPECT_THAT(
      auth_session.authorized_intents(),
      UnorderedElementsAre(AuthIntent::kDecrypt, AuthIntent::kVerifyOnly));
  EXPECT_TRUE(auth_session.TakeCredentialVerifier()->Verify(
      brillo::SecureBlob(kFakePass)));

  // Cleanup.
  auth_session.timeout_timer_.FireNow();
  EXPECT_THAT(AuthStatus::kAuthStatusTimedOut, auth_session.GetStatus());
  EXPECT_THAT(auth_session.authorized_intents(), IsEmpty());
}

// Test Authenticate() authenticates the existing user with PIN credentials.
TEST_F(AuthSessionTest, AuthenticateWithPIN) {
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
  AuthSession auth_session(
      kFakeUsername, flags, AuthIntent::kDecrypt, std::move(on_timeout),
      &crypto_, &platform_, &user_session_map_, &keyset_management_,
      &auth_block_utility_, &auth_factor_manager_, &user_secret_stash_storage_,
      /*enable_create_backup_vk_with_uss =*/false);

  // Test.
  EXPECT_THAT(AuthStatus::kAuthStatusFurtherFactorRequired,
              auth_session.GetStatus());
  EXPECT_TRUE(auth_session.user_exists());

  cryptohome::AuthorizationRequest authorization_request;
  authorization_request.mutable_key()->set_secret(kFakePin);
  authorization_request.mutable_key()->mutable_data()->set_label(kFakePinLabel);
  authorization_request.mutable_key()
      ->mutable_data()
      ->mutable_policy()
      ->set_low_entropy_credential(true);

  EXPECT_CALL(auth_block_utility_, GetAuthBlockStateFromVaultKeyset(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(auth_block_utility_, GetAuthBlockTypeFromState(_))
      .WillRepeatedly(Return(AuthBlockType::kPinWeaver));
  EXPECT_CALL(keyset_management_, GetValidKeysetWithKeyBlobs(_, _, _))
      .WillOnce(Return(ByMove(std::make_unique<VaultKeyset>())));
  EXPECT_CALL(keyset_management_, ShouldReSaveKeyset(_))
      .WillOnce(Return(false));

  auto key_blobs = std::make_unique<KeyBlobs>();
  EXPECT_CALL(auth_block_utility_, DeriveKeyBlobsWithAuthBlockAsync(_, _, _, _))
      .WillOnce([&key_blobs](AuthBlockType auth_block_type,
                             const AuthInput& auth_input,
                             const AuthBlockState& auth_state,
                             AuthBlock::DeriveCallback derive_callback) {
        std::move(derive_callback)
            .Run(OkStatus<CryptohomeCryptoError>(), std::move(key_blobs));
        return true;
      });

  TestFuture<CryptohomeStatus> authenticate_future;
  auth_session.Authenticate(authorization_request,
                            authenticate_future.GetCallback());

  // Verify.
  EXPECT_THAT(authenticate_future.Get(), IsOk());
  ASSERT_TRUE(auth_session.timeout_timer_.IsRunning());

  EXPECT_EQ(AuthStatus::kAuthStatusAuthenticated, auth_session.GetStatus());
  EXPECT_THAT(
      auth_session.authorized_intents(),
      UnorderedElementsAre(AuthIntent::kDecrypt, AuthIntent::kVerifyOnly));
  EXPECT_TRUE(auth_session.TakeCredentialVerifier()->Verify(
      brillo::SecureBlob(kFakePin)));

  // Cleanup.
  auth_session.timeout_timer_.FireNow();
  EXPECT_THAT(AuthStatus::kAuthStatusTimedOut, auth_session.GetStatus());
  EXPECT_THAT(auth_session.authorized_intents(), IsEmpty());
}

// Test whether PIN is locked out right after the last workable wrong attempt.
TEST_F(AuthSessionTest, AuthenticateFailsOnPINLock) {
  // Setup.
  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;
  // Setting the expectation that the user exists.
  EXPECT_CALL(keyset_management_, UserExists(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(keyset_management_, GetVaultKeysetLabelsAndData(_, _));
  AuthSession auth_session(kFakeUsername, flags, AuthIntent::kDecrypt,
                           /*on_timeout=*/base::DoNothing(), &crypto_,
                           &platform_, &user_session_map_, &keyset_management_,
                           &auth_block_utility_, &auth_factor_manager_,
                           &user_secret_stash_storage_,
                           /*enable_create_backup_vk_with_uss =*/false);

  // Test.
  EXPECT_THAT(AuthStatus::kAuthStatusFurtherFactorRequired,
              auth_session.GetStatus());
  EXPECT_TRUE(auth_session.user_exists());

  cryptohome::AuthorizationRequest authorization_request;
  authorization_request.mutable_key()->set_secret(kFakePin);
  authorization_request.mutable_key()->mutable_data()->set_label(kFakePinLabel);
  authorization_request.mutable_key()
      ->mutable_data()
      ->mutable_policy()
      ->set_low_entropy_credential(true);

  EXPECT_CALL(auth_block_utility_, GetAuthBlockStateFromVaultKeyset(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(auth_block_utility_, GetAuthBlockTypeFromState(_))
      .WillRepeatedly(Return(AuthBlockType::kPinWeaver));
  auto vk = std::make_unique<VaultKeyset>();
  vk->Initialize(&platform_, &crypto_);
  vk->SetAuthLocked(false);
  EXPECT_CALL(keyset_management_, GetVaultKeyset(_, kFakePinLabel))
      .WillOnce(Return(ByMove(std::move(vk))));

  EXPECT_CALL(auth_block_utility_, DeriveKeyBlobsWithAuthBlockAsync(_, _, _, _))
      .WillOnce([](AuthBlockType auth_block_type, const AuthInput& auth_input,
                   const AuthBlockState& auth_state,
                   AuthBlock::DeriveCallback derive_callback) {
        CryptoStatus status = MakeStatus<CryptohomeCryptoError>(
            kErrorLocationForTestingAuthSession,
            error::ErrorActionSet({error::ErrorAction::kAuth}),
            CryptoError::CE_CREDENTIAL_LOCKED);

        std::move(derive_callback)
            .Run(std::move(status), std::make_unique<KeyBlobs>());
        return true;
      });

  TestFuture<CryptohomeStatus> authenticate_future;
  auth_session.Authenticate(authorization_request,
                            authenticate_future.GetCallback());

  // Verify.
  ASSERT_THAT(authenticate_future.Get(), NotOk());
  EXPECT_EQ(authenticate_future.Get()->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED);
  EXPECT_NE(AuthStatus::kAuthStatusAuthenticated, auth_session.GetStatus());
  EXPECT_THAT(auth_session.authorized_intents(), IsEmpty());
  EXPECT_EQ(auth_session.TakeCredentialVerifier(), nullptr);
}

// Test whether PIN is locked out when TpmLockout action is received.
TEST_F(AuthSessionTest, AuthenticateFailsAfterPINLock) {
  // Setup.
  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;
  // Setting the expectation that the user exists.
  EXPECT_CALL(keyset_management_, UserExists(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(keyset_management_, GetVaultKeysetLabelsAndData(_, _));
  AuthSession auth_session(kFakeUsername, flags, AuthIntent::kDecrypt,
                           /*on_timeout=*/base::DoNothing(), &crypto_,
                           &platform_, &user_session_map_, &keyset_management_,
                           &auth_block_utility_, &auth_factor_manager_,
                           &user_secret_stash_storage_,
                           /*enable_create_backup_vk_with_uss =*/false);

  // Test.
  EXPECT_THAT(AuthStatus::kAuthStatusFurtherFactorRequired,
              auth_session.GetStatus());
  EXPECT_TRUE(auth_session.user_exists());

  cryptohome::AuthorizationRequest authorization_request;
  authorization_request.mutable_key()->set_secret(kFakePin);
  authorization_request.mutable_key()->mutable_data()->set_label(kFakePinLabel);
  authorization_request.mutable_key()
      ->mutable_data()
      ->mutable_policy()
      ->set_low_entropy_credential(true);

  EXPECT_CALL(auth_block_utility_, GetAuthBlockStateFromVaultKeyset(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(auth_block_utility_, GetAuthBlockTypeFromState(_))
      .WillRepeatedly(Return(AuthBlockType::kPinWeaver));

  EXPECT_CALL(auth_block_utility_, DeriveKeyBlobsWithAuthBlockAsync(_, _, _, _))
      .WillOnce([](AuthBlockType auth_block_type, const AuthInput& auth_input,
                   const AuthBlockState& auth_state,
                   AuthBlock::DeriveCallback derive_callback) {
        CryptoStatus status = MakeStatus<CryptohomeCryptoError>(
            kErrorLocationForTestingAuthSession,
            error::ErrorActionSet({error::ErrorAction::kTpmLockout}),
            CryptoError::CE_TPM_DEFEND_LOCK);

        std::move(derive_callback)
            .Run(std::move(status), std::make_unique<KeyBlobs>());
        return true;
      });

  TestFuture<CryptohomeStatus> authenticate_future;
  auth_session.Authenticate(authorization_request,
                            authenticate_future.GetCallback());

  // Verify.
  ASSERT_THAT(authenticate_future.Get(), NotOk());
  EXPECT_EQ(authenticate_future.Get()->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_TPM_DEFEND_LOCK);
  EXPECT_NE(AuthStatus::kAuthStatusAuthenticated, auth_session.GetStatus());
  EXPECT_THAT(auth_session.authorized_intents(), IsEmpty());
  EXPECT_EQ(auth_session.TakeCredentialVerifier(), nullptr);
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
  AuthSession auth_session(
      kFakeUsername, flags, AuthIntent::kDecrypt, std::move(on_timeout),
      &crypto_, &platform_, &user_session_map_, &keyset_management_,
      &auth_block_utility_, &auth_factor_manager_, &user_secret_stash_storage_,
      /*enable_create_backup_vk_with_uss =*/false);

  // Test.
  EXPECT_THAT(AuthStatus::kAuthStatusFurtherFactorRequired,
              auth_session.GetStatus());
  EXPECT_TRUE(auth_session.user_exists());

  cryptohome::AuthorizationRequest authorization_request;
  authorization_request.mutable_key()->set_secret(kFakePass);
  authorization_request.mutable_key()->mutable_data()->set_label(kFakeLabel);

  EXPECT_CALL(auth_block_utility_, GetAuthBlockStateFromVaultKeyset(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(auth_block_utility_, GetAuthBlockTypeFromState(_))
      .WillRepeatedly(Return(AuthBlockType::kTpmBoundToPcr));

  // Failure is achieved by having the callback return an empty key_blobs
  // and a CryptohomeCryptoError.
  auto key_blobs = nullptr;
  EXPECT_CALL(auth_block_utility_, DeriveKeyBlobsWithAuthBlockAsync(_, _, _, _))
      .WillOnce([&key_blobs](AuthBlockType auth_block_type,
                             const AuthInput& auth_input,
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

  TestFuture<CryptohomeStatus> authenticate_future;
  auth_session.Authenticate(authorization_request,
                            authenticate_future.GetCallback());

  // Verify, should not be authenticated and CredentialVerifier should not be
  // set.
  ASSERT_THAT(authenticate_future.Get(), NotOk());
  EXPECT_EQ(authenticate_future.Get()->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_VAULT_UNRECOVERABLE);
  ASSERT_FALSE(auth_session.timeout_timer_.IsRunning());

  EXPECT_EQ(AuthStatus::kAuthStatusFurtherFactorRequired,
            auth_session.GetStatus());
  EXPECT_THAT(auth_session.authorized_intents(), IsEmpty());
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
  AuthSession auth_session(
      kFakeUsername, flags, AuthIntent::kDecrypt, std::move(on_timeout),
      &crypto_, &platform_, &user_session_map_, &keyset_management_,
      &auth_block_utility_, &auth_factor_manager_, &user_secret_stash_storage_,
      /*enable_create_backup_vk_with_uss =*/false);
  EXPECT_THAT(auth_session.OnUserCreated(), IsOk());

  // Test.
  EXPECT_THAT(AuthStatus::kAuthStatusAuthenticated, auth_session.GetStatus());
  EXPECT_TRUE(auth_session.user_exists());
  ASSERT_TRUE(auth_session.timeout_timer_.IsRunning());

  user_data_auth::AddCredentialsRequest add_cred_request;
  cryptohome::AuthorizationRequest* authorization_request =
      add_cred_request.mutable_authorization();
  authorization_request->mutable_key()->set_secret(kFakePass);
  authorization_request->mutable_key()->mutable_data()->set_label(kFakeLabel);

  EXPECT_CALL(keyset_management_,
              AddInitialKeysetWithKeyBlobs(_, _, _, _, _, _))
      .Times(0);

  // Test.
  TestFuture<CryptohomeStatus> add_future;
  auth_session.AddCredentials(add_cred_request, add_future.GetCallback());

  // Verify.
  EXPECT_THAT(add_future.Get(), IsOk());
  EXPECT_EQ(auth_session.GetStatus(), AuthStatus::kAuthStatusAuthenticated);
  EXPECT_THAT(
      auth_session.authorized_intents(),
      UnorderedElementsAre(AuthIntent::kDecrypt, AuthIntent::kVerifyOnly));
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
  AuthSession auth_session(
      kFakeUsername, flags, AuthIntent::kDecrypt, std::move(on_timeout),
      &crypto_, &platform_, &user_session_map_, &keyset_management_,
      &auth_block_utility_, &auth_factor_manager_, &user_secret_stash_storage_,
      /*enable_create_backup_vk_with_uss =*/false);
  user_data_auth::UpdateCredentialRequest update_cred_request;
  cryptohome::AuthorizationRequest* authorization_request =
      update_cred_request.mutable_authorization();
  authorization_request->mutable_key()->set_secret(kFakePass);
  authorization_request->mutable_key()->mutable_data()->set_label(kFakeLabel);
  update_cred_request.set_old_credential_label(kFakeLabel);

  // Test.
  TestFuture<CryptohomeStatus> update_future;
  auth_session.UpdateCredential(update_cred_request,
                                update_future.GetCallback());

  // Verify.
  ASSERT_THAT(update_future.Get(), NotOk());
  EXPECT_EQ(update_future.Get()->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_UNAUTHENTICATED_AUTH_SESSION);
  EXPECT_EQ(auth_session.TakeCredentialVerifier(), nullptr);
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
      std::make_unique<AuthBlockUtilityImpl>(
          &keyset_management_, &crypto_, &platform_,
          FingerprintAuthBlockService::MakeNullService());

  // Setting the expectation that the user does exist.
  EXPECT_CALL(keyset_management_, UserExists(_)).WillRepeatedly(Return(true));
  AuthSession auth_session(
      kFakeUsername, flags, AuthIntent::kDecrypt, std::move(on_timeout),
      &crypto_, &platform_, &user_session_map_, &keyset_management_,
      auth_block_utility_impl_.get(), &auth_factor_manager_,
      &user_secret_stash_storage_, /*enable_create_backup_vk_with_uss =*/false);
  auth_session.SetStatus(AuthStatus::kAuthStatusAuthenticated);
  user_data_auth::UpdateCredentialRequest update_cred_request;
  cryptohome::AuthorizationRequest* authorization_request =
      update_cred_request.mutable_authorization();
  authorization_request->mutable_key()->set_secret(kFakePass);
  authorization_request->mutable_key()->mutable_data()->set_label(kFakeLabel);
  update_cred_request.set_old_credential_label(kFakeLabel);

  // Test.
  TestFuture<CryptohomeStatus> update_future;
  auth_session.UpdateCredential(update_cred_request,
                                update_future.GetCallback());

  // Verify.
  ASSERT_THAT(update_future.Get(), IsOk());
  EXPECT_THAT(auth_session.TakeCredentialVerifier(),
              IsVerifierPtrForPassword(kFakePass));
}

// Test if UpdateAuthSession fails for not matching label.
TEST_F(AuthSessionTest, UpdateCredentialInvalidLabel) {
  // Setup.
  bool called = false;
  auto on_timeout = base::BindOnce(
      [](bool& called, const base::UnguessableToken&) { called = true; },
      std::ref(called));
  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;
  // Setting the expectation that the user does exist.
  EXPECT_CALL(keyset_management_, UserExists(_)).WillRepeatedly(Return(true));
  AuthSession auth_session(
      kFakeUsername, flags, AuthIntent::kDecrypt, std::move(on_timeout),
      &crypto_, &platform_, &user_session_map_, &keyset_management_,
      &auth_block_utility_, &auth_factor_manager_, &user_secret_stash_storage_,
      /*enable_create_backup_vk_with_uss =*/false);
  user_data_auth::UpdateCredentialRequest update_cred_request;
  cryptohome::AuthorizationRequest* authorization_request =
      update_cred_request.mutable_authorization();
  authorization_request->mutable_key()->set_secret(kFakePass);
  authorization_request->mutable_key()->mutable_data()->set_label(kFakeLabel);
  update_cred_request.set_old_credential_label("wrong-label");

  // Test.
  TestFuture<CryptohomeStatus> update_future;
  auth_session.UpdateCredential(update_cred_request,
                                update_future.GetCallback());

  // Verify.
  ASSERT_THAT(update_future.Get(), NotOk());
  EXPECT_EQ(update_future.Get()->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(auth_session.TakeCredentialVerifier(), nullptr);
}

// Test that the UserSecretStash isn't created by default when a new user is
// created.
TEST_F(AuthSessionTest, NoUssByDefault) {
  // Setup.
  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;
  // Setting the expectation that the user does not exist.
  EXPECT_CALL(keyset_management_, UserExists(_)).WillRepeatedly(Return(false));
  AuthSession auth_session(kFakeUsername, flags, AuthIntent::kDecrypt,
                           /*on_timeout=*/base::DoNothing(), &crypto_,
                           &platform_, &user_session_map_, &keyset_management_,
                           &auth_block_utility_, &auth_factor_manager_,
                           &user_secret_stash_storage_,
                           /*enable_create_backup_vk_with_uss =*/false);

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

  AuthSession auth_session(kFakeUsername, flags, AuthIntent::kDecrypt,
                           /*on_timeout=*/base::DoNothing(), &crypto_,
                           &platform_, &user_session_map_, &keyset_management_,
                           &auth_block_utility_, &auth_factor_manager_,
                           &user_secret_stash_storage_,
                           /*enable_create_backup_vk_with_uss =*/false);
  EXPECT_THAT(AuthStatus::kAuthStatusFurtherFactorRequired,
              auth_session.GetStatus());
  EXPECT_TRUE(auth_session.user_exists());
  auth_session.set_label_to_auth_factor_for_testing(std::move(auth_factor_map));

  // Test
  // Calling AuthenticateAuthFactor.
  EXPECT_EQ(AuthenticateAuthFactorVK(kFakeLabel, kFakePass, auth_session),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  EXPECT_EQ(auth_session.GetStatus(), AuthStatus::kAuthStatusAuthenticated);
  EXPECT_THAT(
      auth_session.authorized_intents(),
      UnorderedElementsAre(AuthIntent::kDecrypt, AuthIntent::kVerifyOnly));
  EXPECT_THAT(auth_session.TakeCredentialVerifier(),
              IsVerifierPtrForPassword(kFakePass));
}

// Test if AuthenticateAuthFactor authenticates existing credentials for a
// user with VK and resaves it.
TEST_F(AuthSessionTest,
       AuthenticateAuthFactorExistingVKUserAndResaveForUpdate) {
  // Setup AuthSession.
  AuthBlockState auth_block_state;
  auth_block_state.state = ScryptAuthBlockState();
  std::map<std::string, std::unique_ptr<AuthFactor>> auth_factor_map;
  auth_factor_map.emplace(
      kFakeLabel,
      std::make_unique<AuthFactor>(AuthFactorType::kPassword, kFakeLabel,
                                   AuthFactorMetadata(), auth_block_state));
  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;

  EXPECT_CALL(keyset_management_, UserExists(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(keyset_management_, GetVaultKeysetLabelsAndData(_, _));

  AuthSession auth_session(kFakeUsername, flags, AuthIntent::kDecrypt,
                           /*on_timeout=*/base::DoNothing(), &crypto_,
                           &platform_, &user_session_map_, &keyset_management_,
                           &auth_block_utility_, &auth_factor_manager_,
                           &user_secret_stash_storage_,
                           /*enable_create_backup_vk_with_uss =*/false);
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

  EXPECT_CALL(auth_block_utility_, GetAuthBlockStateFromVaultKeyset(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(auth_block_utility_, GetAuthBlockTypeFromState(_))
      .WillRepeatedly(Return(AuthBlockType::kScrypt));
  EXPECT_CALL(keyset_management_, GetValidKeysetWithKeyBlobs(_, _, _))
      .WillOnce(Return(ByMove(std::make_unique<VaultKeyset>())));

  EXPECT_CALL(keyset_management_, ShouldReSaveKeyset(_)).WillOnce(Return(true));
  EXPECT_CALL(auth_block_utility_, GetAuthBlockTypeForCreation(_, _, _))
      .WillOnce(Return(AuthBlockType::kTpmBoundToPcr));
  EXPECT_CALL(keyset_management_, ReSaveKeysetWithKeyBlobs(_, _, _));

  auto key_blobs = std::make_unique<KeyBlobs>();
  auto auth_block_state2 = std::make_unique<AuthBlockState>();
  EXPECT_CALL(auth_block_utility_, CreateKeyBlobsWithAuthBlockAsync(_, _, _))
      .WillOnce([&key_blobs, &auth_block_state2](
                    AuthBlockType auth_block_type, const AuthInput& auth_input,
                    AuthBlock::CreateCallback create_callback) {
        std::move(create_callback)
            .Run(OkStatus<CryptohomeCryptoError>(), std::move(key_blobs),
                 std::move(auth_block_state2));
        return true;
      });

  auto key_blobs2 = std::make_unique<KeyBlobs>();
  EXPECT_CALL(auth_block_utility_, DeriveKeyBlobsWithAuthBlockAsync(_, _, _, _))
      .WillOnce([&key_blobs2](AuthBlockType auth_block_type,
                              const AuthInput& auth_input,
                              const AuthBlockState& auth_state,
                              AuthBlock::DeriveCallback derive_callback) {
        std::move(derive_callback)
            .Run(OkStatus<CryptohomeCryptoError>(), std::move(key_blobs2));
        return true;
      });

  TestFuture<CryptohomeStatus> authenticate_future;
  EXPECT_TRUE(auth_session.AuthenticateAuthFactor(
      request, authenticate_future.GetCallback()));

  // Verify.
  EXPECT_THAT(authenticate_future.Get(), IsOk());
  EXPECT_EQ(auth_session.GetStatus(), AuthStatus::kAuthStatusAuthenticated);
  EXPECT_THAT(
      auth_session.authorized_intents(),
      UnorderedElementsAre(AuthIntent::kDecrypt, AuthIntent::kVerifyOnly));
  EXPECT_THAT(auth_session.TakeCredentialVerifier(),
              IsVerifierPtrForPassword(kFakePass));
}

// Test if AuthenticateAuthFactor authenticates existing credentials for a
// user with VK and resaves it.
TEST_F(AuthSessionTest,
       AuthenticateAuthFactorExistingVKUserAndResaveForResetSeed) {
  // Setup AuthSession.
  AuthBlockState auth_block_state;
  auth_block_state.state = ScryptAuthBlockState();
  std::map<std::string, std::unique_ptr<AuthFactor>> auth_factor_map;
  auth_factor_map.emplace(
      kFakeLabel,
      std::make_unique<AuthFactor>(AuthFactorType::kPassword, kFakeLabel,
                                   AuthFactorMetadata(), auth_block_state));
  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;

  EXPECT_CALL(keyset_management_, UserExists(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(keyset_management_, GetVaultKeysetLabelsAndData(_, _));

  AuthSession auth_session(kFakeUsername, flags, AuthIntent::kDecrypt,
                           /*on_timeout=*/base::DoNothing(), &crypto_,
                           &platform_, &user_session_map_, &keyset_management_,
                           &auth_block_utility_, &auth_factor_manager_,
                           &user_secret_stash_storage_,
                           /*enable_create_backup_vk_with_uss =*/false);
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

  EXPECT_CALL(auth_block_utility_, GetAuthBlockStateFromVaultKeyset(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(auth_block_utility_, GetAuthBlockTypeFromState(_))
      .WillRepeatedly(Return(AuthBlockType::kScrypt));
  EXPECT_CALL(keyset_management_, GetValidKeysetWithKeyBlobs(_, _, _))
      .WillOnce(Return(ByMove(std::make_unique<VaultKeyset>())));

  EXPECT_CALL(keyset_management_, ShouldReSaveKeyset(_))
      .WillOnce(Return(false));
  EXPECT_CALL(keyset_management_, AddResetSeedIfMissing(_))
      .WillOnce(Return(true));
  EXPECT_CALL(auth_block_utility_, GetAuthBlockTypeForCreation(_, _, _))
      .WillOnce(Return(AuthBlockType::kTpmBoundToPcr));
  EXPECT_CALL(keyset_management_, ReSaveKeysetWithKeyBlobs(_, _, _));

  auto key_blobs = std::make_unique<KeyBlobs>();
  auto auth_block_state2 = std::make_unique<AuthBlockState>();
  EXPECT_CALL(auth_block_utility_, CreateKeyBlobsWithAuthBlockAsync(_, _, _))
      .WillOnce([&key_blobs, &auth_block_state2](
                    AuthBlockType auth_block_type, const AuthInput& auth_input,
                    AuthBlock::CreateCallback create_callback) {
        std::move(create_callback)
            .Run(OkStatus<CryptohomeCryptoError>(), std::move(key_blobs),
                 std::move(auth_block_state2));
        return true;
      });

  auto key_blobs2 = std::make_unique<KeyBlobs>();
  EXPECT_CALL(auth_block_utility_, DeriveKeyBlobsWithAuthBlockAsync(_, _, _, _))
      .WillOnce([&key_blobs2](AuthBlockType auth_block_type,
                              const AuthInput& auth_input,
                              const AuthBlockState& auth_state,
                              AuthBlock::DeriveCallback derive_callback) {
        std::move(derive_callback)
            .Run(OkStatus<CryptohomeCryptoError>(), std::move(key_blobs2));
        return true;
      });

  TestFuture<CryptohomeStatus> authenticate_future;
  EXPECT_TRUE(auth_session.AuthenticateAuthFactor(
      request, authenticate_future.GetCallback()));

  // Verify.
  EXPECT_THAT(authenticate_future.Get(), IsOk());
  EXPECT_EQ(auth_session.GetStatus(), AuthStatus::kAuthStatusAuthenticated);
  EXPECT_THAT(
      auth_session.authorized_intents(),
      UnorderedElementsAre(AuthIntent::kDecrypt, AuthIntent::kVerifyOnly));
  EXPECT_THAT(auth_session.TakeCredentialVerifier(),
              IsVerifierPtrForPassword(kFakePass));
}

// Test that AuthenticateAuthFactor doesn't add reset seed to LECredentials.
TEST_F(AuthSessionTest,
       AuthenticateAuthFactorNotAddingResetSeedToPINVaultKeyset) {
  // Setup AuthSession.
  AuthBlockState auth_block_state;
  auth_block_state.state = PinWeaverAuthBlockState();
  std::map<std::string, std::unique_ptr<AuthFactor>> auth_factor_map;
  auth_factor_map.emplace(
      kFakePinLabel,
      std::make_unique<AuthFactor>(AuthFactorType::kPin, kFakePinLabel,
                                   AuthFactorMetadata(), auth_block_state));
  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;

  EXPECT_CALL(keyset_management_, UserExists(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(keyset_management_, GetVaultKeysetLabelsAndData(_, _));

  AuthSession auth_session(kFakeUsername, flags, AuthIntent::kDecrypt,
                           /*on_timeout=*/base::DoNothing(), &crypto_,
                           &platform_, &user_session_map_, &keyset_management_,
                           &auth_block_utility_, &auth_factor_manager_,
                           &user_secret_stash_storage_,
                           /*enable_create_backup_vk_with_uss =*/false);
  EXPECT_THAT(AuthStatus::kAuthStatusFurtherFactorRequired,
              auth_session.GetStatus());
  EXPECT_TRUE(auth_session.user_exists());
  auth_session.set_label_to_auth_factor_for_testing(std::move(auth_factor_map));

  // Test
  // Calling AuthenticateAuthFactor.
  user_data_auth::AuthenticateAuthFactorRequest request;
  request.set_auth_session_id(auth_session.serialized_token());
  request.set_auth_factor_label(kFakePinLabel);
  request.mutable_auth_input()->mutable_pin_input()->set_secret(kFakePin);

  // Called within the converter_.PopulateKeyDataForVK()
  KeyData key_data;
  key_data.set_label(kFakePinLabel);
  key_data.mutable_policy()->set_low_entropy_credential(true);
  auto vk = std::make_unique<VaultKeyset>();
  vk->SetKeyData(key_data);

  EXPECT_CALL(keyset_management_, GetVaultKeyset(_, kFakePinLabel))
      .WillOnce(Return(ByMove(std::move(vk))));

  EXPECT_CALL(auth_block_utility_, GetAuthBlockStateFromVaultKeyset(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(auth_block_utility_, GetAuthBlockTypeFromState(_))
      .WillRepeatedly(Return(AuthBlockType::kPinWeaver));
  EXPECT_CALL(keyset_management_, GetValidKeysetWithKeyBlobs(_, _, _))
      .WillOnce(Return(ByMove(std::make_unique<VaultKeyset>())));

  EXPECT_CALL(keyset_management_, ShouldReSaveKeyset(_))
      .WillOnce(Return(false));
  EXPECT_CALL(keyset_management_, AddResetSeedIfMissing(_))
      .WillOnce(Return(false));

  auto key_blobs2 = std::make_unique<KeyBlobs>();
  EXPECT_CALL(auth_block_utility_, DeriveKeyBlobsWithAuthBlockAsync(_, _, _, _))
      .WillOnce([&key_blobs2](AuthBlockType auth_block_type,
                              const AuthInput& auth_input,
                              const AuthBlockState& auth_state,
                              AuthBlock::DeriveCallback derive_callback) {
        std::move(derive_callback)
            .Run(OkStatus<CryptohomeCryptoError>(), std::move(key_blobs2));
        return true;
      });

  TestFuture<CryptohomeStatus> authenticate_future;
  EXPECT_TRUE(auth_session.AuthenticateAuthFactor(
      request, authenticate_future.GetCallback()));

  // Verify.
  EXPECT_THAT(authenticate_future.Get(), IsOk());
  EXPECT_EQ(auth_session.GetStatus(), AuthStatus::kAuthStatusAuthenticated);
  EXPECT_THAT(
      auth_session.authorized_intents(),
      UnorderedElementsAre(AuthIntent::kDecrypt, AuthIntent::kVerifyOnly));
}

// Test that AuthenticateAuthFactor returns an error when supplied label and
// type mismatch.
TEST_F(AuthSessionTest, AuthenticateAuthFactorMismatchLabelAndType) {
  // Setup AuthSession.
  AuthBlockState auth_block_state;
  auth_block_state.state = PinWeaverAuthBlockState();
  std::map<std::string, std::unique_ptr<AuthFactor>> auth_factor_map;
  auth_factor_map.emplace(
      kFakePinLabel,
      std::make_unique<AuthFactor>(AuthFactorType::kPin, kFakePinLabel,
                                   AuthFactorMetadata(), auth_block_state));
  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;

  EXPECT_CALL(keyset_management_, UserExists(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(keyset_management_, GetVaultKeysetLabelsAndData(_, _));

  AuthSession auth_session(kFakeUsername, flags, AuthIntent::kDecrypt,
                           /*on_timeout=*/base::DoNothing(), &crypto_,
                           &platform_, &user_session_map_, &keyset_management_,
                           &auth_block_utility_, &auth_factor_manager_,
                           &user_secret_stash_storage_,
                           /*enable_create_backup_vk_with_uss =*/false);
  EXPECT_THAT(AuthStatus::kAuthStatusFurtherFactorRequired,
              auth_session.GetStatus());
  EXPECT_TRUE(auth_session.user_exists());
  auth_session.set_label_to_auth_factor_for_testing(std::move(auth_factor_map));

  // Test
  // Calling AuthenticateAuthFactor.
  user_data_auth::AuthenticateAuthFactorRequest request;
  request.set_auth_session_id(auth_session.serialized_token());
  request.set_auth_factor_label(kFakePinLabel);
  // Note: Intentially creating a missmatch in type and label.
  request.mutable_auth_input()->mutable_password_input()->set_secret(kFakePin);

  TestFuture<CryptohomeStatus> authenticate_future;
  EXPECT_FALSE(auth_session.AuthenticateAuthFactor(
      request, authenticate_future.GetCallback()));

  // Verify.
  ASSERT_THAT(authenticate_future.Get(), NotOk());
  EXPECT_EQ(authenticate_future.Get()->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(auth_session.GetStatus(),
            AuthStatus::kAuthStatusFurtherFactorRequired);
}

// Test if AddAuthFactor correctly adds initial VaultKeyset password AuthFactor
// for a new user.
TEST_F(AuthSessionTest, AddAuthFactorNewUser) {
  // Setup.
  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;
  // Setting the expectation that the user does not exist.
  EXPECT_CALL(keyset_management_, UserExists(_)).WillRepeatedly(Return(false));

  std::unique_ptr<AuthBlockUtilityImpl> auth_block_utility_impl =
      std::make_unique<AuthBlockUtilityImpl>(
          &keyset_management_, &crypto_, &platform_,
          FingerprintAuthBlockService::MakeNullService());
  AuthSession auth_session(kFakeUsername, flags, AuthIntent::kDecrypt,
                           /*on_timeout=*/base::DoNothing(), &crypto_,
                           &platform_, &user_session_map_, &keyset_management_,
                           auth_block_utility_impl.get(), &auth_factor_manager_,
                           &user_secret_stash_storage_,
                           /*enable_create_backup_vk_with_uss =*/false);

  // Setting the expectation that the user does not exist.
  EXPECT_EQ(auth_session.GetStatus(),
            AuthStatus::kAuthStatusFurtherFactorRequired);
  EXPECT_FALSE(auth_session.user_exists());

  // Creating the user.
  EXPECT_TRUE(auth_session.OnUserCreated().ok());
  EXPECT_EQ(auth_session.GetStatus(), AuthStatus::kAuthStatusAuthenticated);
  EXPECT_THAT(
      auth_session.authorized_intents(),
      UnorderedElementsAre(AuthIntent::kDecrypt, AuthIntent::kVerifyOnly));
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
  EXPECT_CALL(keyset_management_, GetVaultKeyset(_, kFakeLabel))
      .WillOnce([](const std::string&, const std::string&) {
        auto vk = std::make_unique<VaultKeyset>();
        vk->InitializeFromSerialized(CreateFakePasswordVk(kFakeLabel));
        return vk;
      });

  // Test.
  TestFuture<CryptohomeStatus> add_future;
  auth_session.AddAuthFactor(request, add_future.GetCallback());

  // Verify.
  EXPECT_THAT(add_future.Get(), IsOk());
  EXPECT_THAT(auth_session.TakeCredentialVerifier(),
              IsVerifierPtrForPassword(kFakePass));
}

// Test that AddAuthFactor can add multiple VaultKeyset-AuthFactor. The first
// one is added as initial factor, the second is added as the second password
// factor, and the third one as added as a PIN factor.
TEST_F(AuthSessionTest, AddMultipleAuthFactor) {
  // Setup.
  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;
  // Setting the expectation that the user does not exist.
  EXPECT_CALL(keyset_management_, UserExists(_)).WillRepeatedly(Return(false));

  AuthSession auth_session(kFakeUsername, flags, AuthIntent::kDecrypt,
                           /*on_timeout=*/base::DoNothing(), &crypto_,
                           &platform_, &user_session_map_, &keyset_management_,
                           &auth_block_utility_, &auth_factor_manager_,
                           &user_secret_stash_storage_,
                           /*enable_create_backup_vk_with_uss =*/false);

  // Setting the expectation that the user does not exist.
  EXPECT_EQ(auth_session.GetStatus(),
            AuthStatus::kAuthStatusFurtherFactorRequired);
  EXPECT_FALSE(auth_session.user_exists());

  // Creating the user.
  EXPECT_TRUE(auth_session.OnUserCreated().ok());
  EXPECT_EQ(auth_session.GetStatus(), AuthStatus::kAuthStatusAuthenticated);
  EXPECT_THAT(
      auth_session.authorized_intents(),
      UnorderedElementsAre(AuthIntent::kDecrypt, AuthIntent::kVerifyOnly));
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
      .WillRepeatedly([](AuthBlockType auth_block_type,
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
  EXPECT_CALL(keyset_management_, GetVaultKeyset(_, _))
      .WillRepeatedly([](const std::string&, const std::string& label) {
        auto vk = std::make_unique<VaultKeyset>();
        vk->InitializeFromSerialized(CreateFakePasswordVk(label));
        return vk;
      });

  // Test.
  TestFuture<CryptohomeStatus> add_future;
  auth_session.AddAuthFactor(request, add_future.GetCallback());

  // Verify.
  EXPECT_THAT(add_future.Get(), IsOk());

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

  // Test.
  TestFuture<CryptohomeStatus> add_future2;
  auth_session.AddAuthFactor(request2, add_future2.GetCallback());

  // Verify.
  ASSERT_THAT(add_future2.Get(), IsOk());
  // The credential verifier should still use the original password.
  EXPECT_THAT(auth_session.TakeCredentialVerifier(),
              IsVerifierPtrForPassword(kFakePass));

  // TODO(b:223222440) Add test to for adding a PIN after reset secret
  // generation function is updated.
}

// Test that AddAuthFactor succeeds for an ephemeral user and creates a
// credential verifier.
TEST_F(AuthSessionTest, AddPasswordFactorToEphemeral) {
  // Setup.
  AuthSession auth_session(
      kFakeUsername, user_data_auth::AUTH_SESSION_FLAGS_EPHEMERAL_USER,
      AuthIntent::kDecrypt,
      /*on_timeout=*/base::DoNothing(), &crypto_, &platform_,
      &user_session_map_, &keyset_management_, &auth_block_utility_,
      &auth_factor_manager_, &user_secret_stash_storage_,
      /*enable_create_backup_vk_with_uss =*/false);
  EXPECT_THAT(auth_session.OnUserCreated(), IsOk());
  EXPECT_THAT(
      auth_session.authorized_intents(),
      UnorderedElementsAre(AuthIntent::kDecrypt, AuthIntent::kVerifyOnly));

  // Test.
  user_data_auth::AddAuthFactorRequest request;
  request.set_auth_session_id(auth_session.serialized_token());
  user_data_auth::AuthFactor& request_factor = *request.mutable_auth_factor();
  request_factor.set_type(user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
  request_factor.set_label(kFakeLabel);
  request_factor.mutable_password_metadata();
  request.mutable_auth_input()->mutable_password_input()->set_secret(kFakePass);

  TestFuture<CryptohomeStatus> add_future;
  auth_session.AddAuthFactor(request, add_future.GetCallback());

  // Verify.
  EXPECT_THAT(add_future.Get(), IsOk());
  EXPECT_THAT(auth_session.TakeCredentialVerifier(),
              IsVerifierPtrForPassword(kFakePass));
}

// Test that AddAuthFactor fails for an ephemeral user when PIN is added.
TEST_F(AuthSessionTest, AddPinFactorToEphemeralFails) {
  // Setup.
  AuthSession auth_session(
      kFakeUsername, user_data_auth::AUTH_SESSION_FLAGS_EPHEMERAL_USER,
      AuthIntent::kDecrypt,
      /*on_timeout=*/base::DoNothing(), &crypto_, &platform_,
      &user_session_map_, &keyset_management_, &auth_block_utility_,
      &auth_factor_manager_, &user_secret_stash_storage_,
      /*enable_create_backup_vk_with_uss =*/false);
  EXPECT_THAT(auth_session.OnUserCreated(), IsOk());
  EXPECT_THAT(
      auth_session.authorized_intents(),
      UnorderedElementsAre(AuthIntent::kDecrypt, AuthIntent::kVerifyOnly));

  // Test.
  user_data_auth::AddAuthFactorRequest request;
  request.set_auth_session_id(auth_session.serialized_token());
  user_data_auth::AuthFactor& request_factor = *request.mutable_auth_factor();
  request_factor.set_type(user_data_auth::AUTH_FACTOR_TYPE_PIN);
  request_factor.set_label(kFakePinLabel);
  request_factor.mutable_pin_metadata();
  request.mutable_auth_input()->mutable_pin_input()->set_secret(kFakePin);

  TestFuture<CryptohomeStatus> add_future;
  auth_session.AddAuthFactor(request, add_future.GetCallback());

  // Verify.
  ASSERT_THAT(add_future.Get(), NotOk());
  EXPECT_EQ(add_future.Get()->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  EXPECT_EQ(auth_session.TakeCredentialVerifier(), nullptr);
}

// Test that AddAuthFactor fails for an ephemeral user when a second password is
// added.
TEST_F(AuthSessionTest, AddSecondPasswordFactorToEphemeralFails) {
  // Setup.
  AuthSession auth_session(
      kFakeUsername, user_data_auth::AUTH_SESSION_FLAGS_EPHEMERAL_USER,
      AuthIntent::kDecrypt,
      /*on_timeout=*/base::DoNothing(), &crypto_, &platform_,
      &user_session_map_, &keyset_management_, &auth_block_utility_,
      &auth_factor_manager_, &user_secret_stash_storage_,
      /*enable_create_backup_vk_with_uss =*/false);
  EXPECT_THAT(auth_session.OnUserCreated(), IsOk());
  EXPECT_THAT(
      auth_session.authorized_intents(),
      UnorderedElementsAre(AuthIntent::kDecrypt, AuthIntent::kVerifyOnly));
  // Add the first password.
  user_data_auth::AddAuthFactorRequest request;
  request.set_auth_session_id(auth_session.serialized_token());
  user_data_auth::AuthFactor& request_factor = *request.mutable_auth_factor();
  request_factor.set_type(user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
  request_factor.set_label(kFakeLabel);
  request_factor.mutable_password_metadata();
  request.mutable_auth_input()->mutable_password_input()->set_secret(kFakePass);
  TestFuture<CryptohomeStatus> first_add_future;
  auth_session.AddAuthFactor(request, first_add_future.GetCallback());
  EXPECT_THAT(first_add_future.Get(), IsOk());

  // Test.
  request_factor.set_label(kFakeOtherLabel);
  request.mutable_auth_input()->mutable_password_input()->set_secret(
      kFakeOtherPass);
  TestFuture<CryptohomeStatus> second_add_future;
  auth_session.AddAuthFactor(request, second_add_future.GetCallback());

  // Verify.
  ASSERT_THAT(second_add_future.Get(), NotOk());
  EXPECT_EQ(second_add_future.Get()->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  // The verifier still uses the first password.
  EXPECT_THAT(auth_session.TakeCredentialVerifier(),
              IsVerifierPtrForPassword(kFakePass));
}

// UpdateAuthFactor request success when updating authenticated password VK.
TEST_F(AuthSessionTest, UpdateAuthFactorSucceedsForPasswordVK) {
  // Setup.
  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;

  EXPECT_CALL(keyset_management_, UserExists(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(keyset_management_, GetVaultKeysetLabelsAndData(_, _));

  AuthSession auth_session(kFakeUsername, flags, AuthIntent::kDecrypt,
                           /*on_timeout=*/base::DoNothing(), &crypto_,
                           &platform_, &user_session_map_, &keyset_management_,
                           &auth_block_utility_, &auth_factor_manager_,
                           &user_secret_stash_storage_,
                           /*enable_create_backup_vk_with_uss =*/false);
  EXPECT_THAT(AuthStatus::kAuthStatusFurtherFactorRequired,
              auth_session.GetStatus());
  EXPECT_TRUE(auth_session.user_exists());

  AuthBlockState auth_block_state;
  auth_block_state.state = TpmBoundToPcrAuthBlockState();
  std::map<std::string, std::unique_ptr<AuthFactor>> auth_factor_map;
  auth_factor_map.emplace(
      kFakeLabel,
      std::make_unique<AuthFactor>(AuthFactorType::kPassword, kFakeLabel,
                                   AuthFactorMetadata(), auth_block_state));
  auth_session.set_label_to_auth_factor_for_testing(std::move(auth_factor_map));

  // Creating the user.
  EXPECT_TRUE(auth_session.OnUserCreated().ok());
  EXPECT_EQ(auth_session.GetStatus(), AuthStatus::kAuthStatusAuthenticated);
  EXPECT_TRUE(auth_session.user_exists());

  // GetAuthBlockTypeForCreation() and CreateKeyBlobsWithAuthBlockAsync() are
  // called for the key update operations below.
  EXPECT_CALL(auth_block_utility_, GetAuthBlockTypeForCreation(_, _, _))
      .WillRepeatedly(Return(AuthBlockType::kTpmBoundToPcr));
  EXPECT_CALL(auth_block_utility_, CreateKeyBlobsWithAuthBlockAsync(_, _, _))
      .WillRepeatedly([&](AuthBlockType auth_block_type,
                          const AuthInput& auth_input,
                          AuthBlock::CreateCallback create_callback) {
        std::move(create_callback)
            .Run(OkStatus<CryptohomeCryptoError>(),
                 std::make_unique<KeyBlobs>(),
                 std::make_unique<AuthBlockState>(auth_block_state));
        return true;
      });
  EXPECT_CALL(keyset_management_, UpdateKeysetWithKeyBlobs(_, _, _, _, _))
      .WillOnce(Return(CRYPTOHOME_ERROR_NOT_SET));

  // Set a valid |vault_keyset_| to update.
  auto vk = std::make_unique<VaultKeyset>();
  vk->Initialize(&platform_, &crypto_);
  vk->CreateFromFileSystemKeyset(FileSystemKeyset::CreateRandom());
  vk->SetAuthBlockState(auth_block_state);

  auth_session.set_vault_keyset_for_testing(std::move(vk));

  user_data_auth::UpdateAuthFactorRequest request;
  request.set_auth_session_id(auth_session.serialized_token());
  request.set_auth_factor_label(kFakeLabel);
  request.mutable_auth_factor()->set_type(
      user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
  request.mutable_auth_factor()->set_label(kFakeLabel);
  request.mutable_auth_factor()->mutable_password_metadata();
  request.mutable_auth_input()->mutable_password_input()->set_secret(kFakePass);

  TestFuture<CryptohomeStatus> update_future;
  auth_session.UpdateAuthFactor(request, update_future.GetCallback());

  // Verify.
  ASSERT_THAT(update_future.Get(), IsOk());
  EXPECT_TRUE(auth_session.TakeCredentialVerifier()->Verify(
      brillo::SecureBlob(kFakePass)));
}

// UpdateAuthFactor fails if label doesn't exist.
TEST_F(AuthSessionTest, UpdateAuthFactorFailsLabelNotMatchForVK) {
  // Setup.
  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;

  EXPECT_CALL(keyset_management_, UserExists(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(keyset_management_, GetVaultKeysetLabelsAndData(_, _));

  AuthSession auth_session(kFakeUsername, flags, AuthIntent::kDecrypt,
                           /*on_timeout=*/base::DoNothing(), &crypto_,
                           &platform_, &user_session_map_, &keyset_management_,
                           &auth_block_utility_, &auth_factor_manager_,
                           &user_secret_stash_storage_,
                           /*enable_create_backup_vk_with_uss =*/false);
  EXPECT_THAT(AuthStatus::kAuthStatusFurtherFactorRequired,
              auth_session.GetStatus());
  EXPECT_TRUE(auth_session.user_exists());

  AuthBlockState auth_block_state;
  auth_block_state.state = TpmBoundToPcrAuthBlockState();
  std::map<std::string, std::unique_ptr<AuthFactor>> auth_factor_map;
  auth_factor_map.emplace(
      kFakeLabel,
      std::make_unique<AuthFactor>(AuthFactorType::kPassword, kFakeLabel,
                                   AuthFactorMetadata(), auth_block_state));
  auth_session.set_label_to_auth_factor_for_testing(std::move(auth_factor_map));

  // Creating the user.
  EXPECT_TRUE(auth_session.OnUserCreated().ok());
  EXPECT_EQ(auth_session.GetStatus(), AuthStatus::kAuthStatusAuthenticated);
  EXPECT_TRUE(auth_session.user_exists());

  user_data_auth::UpdateAuthFactorRequest request;
  request.set_auth_session_id(auth_session.serialized_token());
  request.set_auth_factor_label(kFakeLabel);
  request.mutable_auth_factor()->set_type(
      user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
  request.mutable_auth_factor()->set_label(kFakeOtherLabel);
  request.mutable_auth_factor()->mutable_password_metadata();
  request.mutable_auth_input()->mutable_password_input()->set_secret(
      kFakeOtherPass);

  TestFuture<CryptohomeStatus> update_future;
  auth_session.UpdateAuthFactor(request, update_future.GetCallback());

  // Verify.
  ASSERT_THAT(update_future.Get(), NotOk());
  // Verify that the credential_verifier is not updated on failure.
  EXPECT_EQ(auth_session.TakeCredentialVerifier(), nullptr);
}

// UpdateAuthFactor fails if label doesn't exist in the existing keysets.
TEST_F(AuthSessionTest, UpdateAuthFactorFailsLabelNotFoundForVK) {
  // Setup.
  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;

  EXPECT_CALL(keyset_management_, UserExists(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(keyset_management_, GetVaultKeysetLabelsAndData(_, _));

  AuthSession auth_session(kFakeUsername, flags, AuthIntent::kDecrypt,
                           /*on_timeout=*/base::DoNothing(), &crypto_,
                           &platform_, &user_session_map_, &keyset_management_,
                           &auth_block_utility_, &auth_factor_manager_,
                           &user_secret_stash_storage_,
                           /*enable_create_backup_vk_with_uss =*/false);

  EXPECT_THAT(AuthStatus::kAuthStatusFurtherFactorRequired,
              auth_session.GetStatus());
  EXPECT_TRUE(auth_session.user_exists());

  AuthBlockState auth_block_state;
  auth_block_state.state = TpmBoundToPcrAuthBlockState();
  std::map<std::string, std::unique_ptr<AuthFactor>> auth_factor_map;
  auth_factor_map.emplace(
      kFakeLabel,
      std::make_unique<AuthFactor>(AuthFactorType::kPassword, kFakeLabel,
                                   AuthFactorMetadata(), auth_block_state));
  auth_session.set_label_to_auth_factor_for_testing(std::move(auth_factor_map));

  // Creating the user.
  EXPECT_TRUE(auth_session.OnUserCreated().ok());
  EXPECT_EQ(auth_session.GetStatus(), AuthStatus::kAuthStatusAuthenticated);
  EXPECT_TRUE(auth_session.user_exists());

  user_data_auth::UpdateAuthFactorRequest request;
  request.set_auth_session_id(auth_session.serialized_token());
  request.set_auth_factor_label(kFakeOtherLabel);
  request.mutable_auth_factor()->set_type(
      user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
  request.mutable_auth_factor()->set_label(kFakeOtherLabel);
  request.mutable_auth_factor()->mutable_password_metadata();
  request.mutable_auth_input()->mutable_password_input()->set_secret(
      kFakeOtherPass);

  TestFuture<CryptohomeStatus> update_future;
  auth_session.UpdateAuthFactor(request, update_future.GetCallback());

  // Verify.
  ASSERT_THAT(update_future.Get(), NotOk());
  // Verify that the credential_verifier is not updated on failure.
  EXPECT_EQ(auth_session.TakeCredentialVerifier(), nullptr);
}

TEST_F(AuthSessionTest, ExtensionTest) {
  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;
  AuthSession auth_session(
      kFakeUsername, flags, AuthIntent::kDecrypt, base::DoNothing(), &crypto_,
      &platform_, &user_session_map_, &keyset_management_, &auth_block_utility_,
      &auth_factor_manager_, &user_secret_stash_storage_,
      /*enable_create_backup_vk_with_uss =*/false);
  EXPECT_EQ(auth_session.GetStatus(),
            AuthStatus::kAuthStatusFurtherFactorRequired);
  auth_session.SetAuthSessionAsAuthenticated(kAuthorizedIntentsForFullAuth);

  ASSERT_TRUE(auth_session.timeout_timer_.IsRunning());

  EXPECT_TRUE(auth_session.ExtendTimeoutTimer(kAuthSessionExtension).ok());

  // Verify that timer has changed, within a resaonsable degree of error.
  auto requested_delay = kAuthSessionTimeout + kAuthSessionExtension;
  EXPECT_EQ(auth_session.timeout_timer_.GetCurrentDelay(), requested_delay);

  auth_session.timeout_timer_.FireNow();
  EXPECT_EQ(auth_session.GetStatus(), AuthStatus::kAuthStatusTimedOut);
  EXPECT_THAT(auth_session.authorized_intents(), IsEmpty());
}

// Test that AuthFactor map is updated after successful RemoveAuthFactor and
// not updated after unsuccessful RemoveAuthFactor.
TEST_F(AuthSessionTest, RemoveAuthFactorUpdatesAuthFactorMap) {
  // Setup.
  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;
  // Setting the expectation that the user does not exist.
  EXPECT_CALL(keyset_management_, UserExists(_)).WillRepeatedly(Return(false));

  // Prepare the AuthFactor.
  AuthBlockState auth_block_state;
  auth_block_state.state = TpmBoundToPcrAuthBlockState();

  std::map<std::string, std::unique_ptr<AuthFactor>> auth_factor_map;
  auth_factor_map[kFakeLabel] =
      std::make_unique<AuthFactor>(AuthFactorType::kPassword, kFakeLabel,
                                   AuthFactorMetadata(), auth_block_state);
  auth_factor_map[kFakeOtherLabel] =
      std::make_unique<AuthFactor>(AuthFactorType::kPassword, kFakeOtherLabel,
                                   AuthFactorMetadata(), auth_block_state);

  // Create AuthSession.
  EXPECT_CALL(keyset_management_, UserExists(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(keyset_management_, GetVaultKeysetLabelsAndData(_, _))
      .WillRepeatedly(Return(true));

  EXPECT_CALL(keyset_management_, GetVaultKeysets(_, _))
      .WillRepeatedly(Return(true));
  AuthSession auth_session(kFakeUsername, flags, AuthIntent::kDecrypt,
                           /*on_timeout=*/base::DoNothing(), &crypto_,
                           &platform_, &user_session_map_, &keyset_management_,
                           &auth_block_utility_, &auth_factor_manager_,
                           &user_secret_stash_storage_,
                           /*enable_create_backup_vk_with_uss =*/false);

  EXPECT_EQ(auth_session.GetStatus(),
            AuthStatus::kAuthStatusFurtherFactorRequired);
  EXPECT_TRUE(auth_session.user_exists());
  auth_session.set_label_to_auth_factor_for_testing(std::move(auth_factor_map));

  EXPECT_EQ(AuthenticateAuthFactorVK(kFakeLabel, kFakePass, auth_session),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  EXPECT_EQ(auth_session.GetStatus(), AuthStatus::kAuthStatusAuthenticated);

  // Test that RemoveAuthFactor success removes the factor from the map.
  user_data_auth::RemoveAuthFactorRequest remove_request;
  remove_request.set_auth_session_id(auth_session.serialized_token());
  remove_request.set_auth_factor_label(kFakeOtherLabel);
  // RemoveauthFactor loads the VK to remove.
  EXPECT_CALL(keyset_management_, GetVaultKeyset(_, kFakeOtherLabel))
      .WillOnce(Return(ByMove(std::make_unique<VaultKeyset>())));
  TestFuture<CryptohomeStatus> remove_future;
  auth_session.RemoveAuthFactor(remove_request, remove_future.GetCallback());

  // Verify that AuthFactor is removed and the Authentication doesn't succeed
  // with the removed factor.
  ASSERT_THAT(remove_future.Get(), IsOk());
  EXPECT_EQ(AuthenticateAuthFactorVK(kFakeOtherLabel, kFakePass, auth_session),
            user_data_auth::CRYPTOHOME_ERROR_KEY_NOT_FOUND);
  EXPECT_EQ(auth_session.GetStatus(), AuthStatus::kAuthStatusAuthenticated);

  // Test that RemoveAuthFactor failure doesn't remove the factor from the map.
  user_data_auth::RemoveAuthFactorRequest remove_request2;
  remove_request2.set_auth_session_id(auth_session.serialized_token());
  remove_request2.set_auth_factor_label(kFakeLabel);

  TestFuture<CryptohomeStatus> remove_future2;
  auth_session.RemoveAuthFactor(remove_request2, remove_future2.GetCallback());

  // Verify that AuthFactor is not removed and the Authentication doesn't
  // succeed with the removed factor.
  ASSERT_THAT(remove_future2.Get(), NotOk());
  EXPECT_EQ(remove_future2.Get()->local_legacy_error().value(),
            user_data_auth::CRYPTOHOME_REMOVE_CREDENTIALS_FAILED);
  EXPECT_EQ(AuthenticateAuthFactorVK(kFakeLabel, kFakePass, auth_session),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  EXPECT_EQ(auth_session.GetStatus(), AuthStatus::kAuthStatusAuthenticated);
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

  user_data_auth::CryptohomeErrorCode AddPasswordAuthFactor(
      const std::string& password, AuthSession& auth_session) {
    EXPECT_CALL(auth_block_utility_,
                GetAuthBlockTypeForCreation(false, false, false))
        .WillRepeatedly(Return(AuthBlockType::kTpmBoundToPcr));
    EXPECT_CALL(auth_block_utility_, CreateKeyBlobsWithAuthBlockAsync(
                                         AuthBlockType::kTpmBoundToPcr, _, _))
        .WillOnce([](AuthBlockType auth_block_type, const AuthInput& auth_input,
                     AuthBlock::CreateCallback create_callback) {
          // Make an arbitrary auth block state type can be used in this test.
          auto key_blobs = std::make_unique<KeyBlobs>();
          key_blobs->vkk_key =
              GetFakeDerivedSecret(auth_input.user_input.value());
          auto auth_block_state = std::make_unique<AuthBlockState>();
          auth_block_state->state = TpmBoundToPcrAuthBlockState();
          std::move(create_callback)
              .Run(OkStatus<CryptohomeCryptoError>(), std::move(key_blobs),
                   std::move(auth_block_state));
          return true;
        });

    user_data_auth::AddAuthFactorRequest request;
    request.mutable_auth_factor()->set_type(
        user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
    request.mutable_auth_factor()->set_label(kFakeLabel);
    request.mutable_auth_factor()->mutable_password_metadata();
    request.mutable_auth_input()->mutable_password_input()->set_secret(
        password);
    request.set_auth_session_id(auth_session.serialized_token());

    TestFuture<CryptohomeStatus> add_future;
    auth_session.AddAuthFactor(request, add_future.GetCallback());

    if (add_future.Get().ok()) {
      return user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
    }

    return add_future.Get()->local_legacy_error().value();
  }

  user_data_auth::CryptohomeErrorCode AuthenticatePasswordAuthFactor(
      const std::string& password, AuthSession& auth_session) {
    EXPECT_CALL(auth_block_utility_,
                GetAuthBlockTypeFromState(
                    AuthBlockStateTypeIs<TpmBoundToPcrAuthBlockState>()))
        .WillRepeatedly(Return(AuthBlockType::kTpmBoundToPcr));
    EXPECT_CALL(auth_block_utility_,
                DeriveKeyBlobsWithAuthBlockAsync(AuthBlockType::kTpmBoundToPcr,
                                                 _, _, _))
        .WillOnce([](AuthBlockType auth_block_type, const AuthInput& auth_input,
                     const AuthBlockState& auth_state,
                     AuthBlock::DeriveCallback derive_callback) {
          auto key_blobs = std::make_unique<KeyBlobs>();
          key_blobs->vkk_key =
              GetFakeDerivedSecret(auth_input.user_input.value());
          std::move(derive_callback)
              .Run(OkStatus<CryptohomeCryptoError>(), std::move(key_blobs));
          return true;
        });

    user_data_auth::AuthenticateAuthFactorRequest request;
    request.set_auth_session_id(auth_session.serialized_token());
    request.set_auth_factor_label(kFakeLabel);
    request.mutable_auth_input()->mutable_password_input()->set_secret(
        password);
    TestFuture<CryptohomeStatus> authenticate_future;
    auth_session.AuthenticateAuthFactor(request,
                                        authenticate_future.GetCallback());

    // Verify.
    if (authenticate_future.Get().ok() ||
        !authenticate_future.Get()->local_legacy_error().has_value()) {
      return user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
    }
    return authenticate_future.Get()->local_legacy_error().value();
  }

  user_data_auth::CryptohomeErrorCode UpdatePasswordAuthFactor(
      const std::string& new_password, AuthSession& auth_session) {
    EXPECT_CALL(auth_block_utility_,
                GetAuthBlockTypeForCreation(false, false, false))
        .WillRepeatedly(Return(AuthBlockType::kTpmBoundToPcr));
    EXPECT_CALL(auth_block_utility_, CreateKeyBlobsWithAuthBlockAsync(
                                         AuthBlockType::kTpmBoundToPcr, _, _))
        .WillOnce([](AuthBlockType auth_block_type, const AuthInput& auth_input,
                     AuthBlock::CreateCallback create_callback) {
          // Make an arbitrary auth block state type can be used in this test.
          auto key_blobs = std::make_unique<KeyBlobs>();
          key_blobs->vkk_key =
              GetFakeDerivedSecret(auth_input.user_input.value());
          auto auth_block_state = std::make_unique<AuthBlockState>();
          auth_block_state->state = TpmBoundToPcrAuthBlockState();
          std::move(create_callback)
              .Run(OkStatus<CryptohomeCryptoError>(), std::move(key_blobs),
                   std::move(auth_block_state));
          return true;
        });

    user_data_auth::UpdateAuthFactorRequest request;
    request.set_auth_session_id(auth_session.serialized_token());
    request.set_auth_factor_label(kFakeLabel);
    request.mutable_auth_factor()->set_type(
        user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
    request.mutable_auth_factor()->set_label(kFakeLabel);
    request.mutable_auth_factor()->mutable_password_metadata();
    request.mutable_auth_input()->mutable_password_input()->set_secret(
        new_password);

    TestFuture<CryptohomeStatus> update_future;
    auth_session.UpdateAuthFactor(request, update_future.GetCallback());

    if (update_future.Get().ok()) {
      return user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
    }

    return update_future.Get()->local_legacy_error().value();
  }

  user_data_auth::CryptohomeErrorCode AddPinAuthFactor(
      const std::string& pin, AuthSession& auth_session) {
    EXPECT_CALL(auth_block_utility_,
                GetAuthBlockTypeForCreation(true, false, false))
        .WillRepeatedly(Return(AuthBlockType::kPinWeaver));
    EXPECT_CALL(auth_block_utility_, CreateKeyBlobsWithAuthBlockAsync(
                                         AuthBlockType::kPinWeaver, _, _))
        .WillOnce([](AuthBlockType auth_block_type, const AuthInput& auth_input,
                     AuthBlock::CreateCallback create_callback) {
          // Make an arbitrary auth block state type can be used in this test.
          auto key_blobs = std::make_unique<KeyBlobs>();
          key_blobs->vkk_key =
              GetFakeDerivedSecret(auth_input.user_input.value());
          auto auth_block_state = std::make_unique<AuthBlockState>();
          auth_block_state->state = PinWeaverAuthBlockState();
          std::move(create_callback)
              .Run(OkStatus<CryptohomeCryptoError>(), std::move(key_blobs),
                   std::move(auth_block_state));
          return true;
        });
    // Calling AddAuthFactor.
    user_data_auth::AddAuthFactorRequest add_pin_request;
    add_pin_request.set_auth_session_id(auth_session.serialized_token());
    add_pin_request.mutable_auth_factor()->set_type(
        user_data_auth::AUTH_FACTOR_TYPE_PIN);
    add_pin_request.mutable_auth_factor()->set_label(kFakePinLabel);
    add_pin_request.mutable_auth_factor()->mutable_pin_metadata();
    add_pin_request.mutable_auth_input()->mutable_pin_input()->set_secret(pin);
    TestFuture<CryptohomeStatus> add_future;
    auth_session.AddAuthFactor(add_pin_request, add_future.GetCallback());

    if (add_future.Get().ok()) {
      return user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
    }

    return add_future.Get()->local_legacy_error().value();
  }
};

// Test that the UserSecretStash is created on the user creation, in case the
// UserSecretStash experiment is on.
TEST_F(AuthSessionWithUssExperimentTest, UssCreation) {
  // Setup.
  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;
  // Setting the expectation that the user does not exist.
  EXPECT_CALL(keyset_management_, UserExists(_)).WillRepeatedly(Return(false));
  AuthSession auth_session(kFakeUsername, flags, AuthIntent::kDecrypt,
                           /*on_timeout=*/base::DoNothing(), &crypto_,
                           &platform_, &user_session_map_, &keyset_management_,
                           &auth_block_utility_, &auth_factor_manager_,
                           &user_secret_stash_storage_,
                           /*enable_create_backup_vk_with_uss =*/false);

  // Test.
  EXPECT_EQ(auth_session.user_secret_stash_for_testing(), nullptr);
  EXPECT_EQ(auth_session.user_secret_stash_main_key_for_testing(),
            std::nullopt);
  EXPECT_TRUE(auth_session.OnUserCreated().ok());

  // Verify.
  EXPECT_NE(auth_session.user_secret_stash_for_testing(), nullptr);
  EXPECT_NE(auth_session.user_secret_stash_main_key_for_testing(),
            std::nullopt);
  EXPECT_EQ(auth_session.TakeCredentialVerifier(), nullptr);
}

// Test that no UserSecretStash is created for an ephemeral user.
TEST_F(AuthSessionWithUssExperimentTest, NoUssForEphemeral) {
  // Setup.
  int flags =
      user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_EPHEMERAL_USER;
  // Setting the expectation that the user does not exist.
  EXPECT_CALL(keyset_management_, UserExists(_)).WillRepeatedly(Return(false));
  AuthSession auth_session(kFakeUsername, flags, AuthIntent::kDecrypt,
                           /*on_timeout=*/base::DoNothing(), &crypto_,
                           &platform_, &user_session_map_, &keyset_management_,
                           &auth_block_utility_, &auth_factor_manager_,
                           &user_secret_stash_storage_,
                           /*enable_create_backup_vk_with_uss =*/false);

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
  AuthSession auth_session(kFakeUsername, flags, AuthIntent::kDecrypt,
                           /*on_timeout=*/base::DoNothing(), &crypto_,
                           &platform_, &user_session_map_, &keyset_management_,
                           &auth_block_utility_, &auth_factor_manager_,
                           &user_secret_stash_storage_,
                           /*enable_create_backup_vk_with_uss =*/false);
  // Creating the user.
  EXPECT_TRUE(auth_session.OnUserCreated().ok());
  EXPECT_NE(auth_session.user_secret_stash_for_testing(), nullptr);
  EXPECT_NE(auth_session.user_secret_stash_main_key_for_testing(),
            std::nullopt);

  // Test.
  // Setting the expectation that the auth block utility will create key blobs.
  EXPECT_CALL(auth_block_utility_,
              GetAuthBlockTypeForCreation(false, false, false))
      .WillRepeatedly(Return(AuthBlockType::kTpmBoundToPcr));
  EXPECT_CALL(auth_block_utility_, CreateKeyBlobsWithAuthBlockAsync(
                                       AuthBlockType::kTpmBoundToPcr, _, _))
      .WillOnce([](AuthBlockType auth_block_type, const AuthInput& auth_input,
                   AuthBlock::CreateCallback create_callback) {
        // Make an arbitrary auth block state type can be used in this test.
        auto key_blobs = std::make_unique<KeyBlobs>();
        key_blobs->vkk_key = brillo::SecureBlob("fake vkk key");
        auto auth_block_state = std::make_unique<AuthBlockState>();
        auth_block_state->state = TpmBoundToPcrAuthBlockState();
        std::move(create_callback)
            .Run(OkStatus<CryptohomeCryptoError>(), std::move(key_blobs),
                 std::move(auth_block_state));
        return true;
      });
  // Calling AddAuthFactor.
  user_data_auth::AddAuthFactorRequest request;
  request.set_auth_session_id(auth_session.serialized_token());
  request.mutable_auth_factor()->set_type(
      user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
  request.mutable_auth_factor()->set_label(kFakeLabel);
  request.mutable_auth_factor()->mutable_password_metadata();
  request.mutable_auth_input()->mutable_password_input()->set_secret(kFakePass);

  TestFuture<CryptohomeStatus> add_future;
  auth_session.AddAuthFactor(request, add_future.GetCallback());

  // Verify
  EXPECT_THAT(add_future.Get(), IsOk());
  EXPECT_THAT(auth_session.TakeCredentialVerifier(),
              IsVerifierPtrForPassword(kFakePass));

  std::map<std::string, AuthFactorType> stored_factors =
      auth_factor_manager_.ListAuthFactors(SanitizeUserName(kFakeUsername));
  EXPECT_THAT(stored_factors,
              ElementsAre(Pair(kFakeLabel, AuthFactorType::kPassword)));
  EXPECT_NE(auth_session.label_to_auth_factor_.find(kFakeLabel),
            auth_session.label_to_auth_factor_.end());
}

// Test that a new auth factor can be added to the newly created user using
// asynchronous key creation.
TEST_F(AuthSessionWithUssExperimentTest, AddPasswordAuthFactorViaAsyncUss) {
  // Setup.
  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;
  // Setting the expectation that the user does not exist.
  EXPECT_CALL(keyset_management_, UserExists(_)).WillRepeatedly(Return(false));
  AuthSession auth_session(kFakeUsername, flags, AuthIntent::kDecrypt,
                           /*on_timeout=*/base::DoNothing(), &crypto_,
                           &platform_, &user_session_map_, &keyset_management_,
                           &auth_block_utility_, &auth_factor_manager_,
                           &user_secret_stash_storage_,
                           /*enable_create_backup_vk_with_uss =*/false);
  // Creating the user.
  EXPECT_TRUE(auth_session.OnUserCreated().ok());
  EXPECT_NE(auth_session.user_secret_stash_for_testing(), nullptr);
  EXPECT_NE(auth_session.user_secret_stash_main_key_for_testing(),
            std::nullopt);

  // Test.
  // Setting the expectation that the auth block utility will create key blobs.
  EXPECT_CALL(auth_block_utility_,
              GetAuthBlockTypeForCreation(false, false, false))
      .WillRepeatedly(Return(AuthBlockType::kTpmBoundToPcr));
  EXPECT_CALL(auth_block_utility_, CreateKeyBlobsWithAuthBlockAsync(
                                       AuthBlockType::kTpmBoundToPcr, _, _))
      .WillOnce([this](AuthBlockType, const AuthInput&,
                       AuthBlock::CreateCallback create_callback) {
        // Make an arbitrary auth block state, but schedule it to run later to
        // simulate an proper async key creation.
        auto key_blobs = std::make_unique<KeyBlobs>();
        key_blobs->vkk_key = brillo::SecureBlob("fake vkk key");
        auto auth_block_state = std::make_unique<AuthBlockState>();
        auth_block_state->state = TpmBoundToPcrAuthBlockState();
        task_runner_->PostTask(
            FROM_HERE,
            base::BindOnce(std::move(create_callback),
                           OkStatus<CryptohomeCryptoError>(),
                           std::move(key_blobs), std::move(auth_block_state)));
        return true;
      });
  // Calling AddAuthFactor.
  user_data_auth::AddAuthFactorRequest request;
  request.set_auth_session_id(auth_session.serialized_token());
  request.mutable_auth_factor()->set_type(
      user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
  request.mutable_auth_factor()->set_label(kFakeLabel);
  request.mutable_auth_factor()->mutable_password_metadata();
  request.mutable_auth_input()->mutable_password_input()->set_secret(kFakePass);

  TestFuture<CryptohomeStatus> add_future;
  auth_session.AddAuthFactor(request, add_future.GetCallback());

  // Verify.
  EXPECT_THAT(add_future.Get(), IsOk());
  EXPECT_THAT(auth_session.TakeCredentialVerifier(),
              IsVerifierPtrForPassword(kFakePass));

  std::map<std::string, AuthFactorType> stored_factors =
      auth_factor_manager_.ListAuthFactors(SanitizeUserName(kFakeUsername));
  EXPECT_THAT(stored_factors,
              ElementsAre(Pair(kFakeLabel, AuthFactorType::kPassword)));
  EXPECT_NE(auth_session.label_to_auth_factor_.find(kFakeLabel),
            auth_session.label_to_auth_factor_.end());
}

// Test the new auth factor failure path when asynchronous key creation fails.
TEST_F(AuthSessionWithUssExperimentTest,
       AddPasswordAuthFactorViaAsyncUssFails) {
  // Setup.
  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;
  // Setting the expectation that the user does not exist.
  EXPECT_CALL(keyset_management_, UserExists(_)).WillRepeatedly(Return(false));
  AuthSession auth_session(kFakeUsername, flags, AuthIntent::kDecrypt,
                           /*on_timeout=*/base::DoNothing(), &crypto_,
                           &platform_, &user_session_map_, &keyset_management_,
                           &auth_block_utility_, &auth_factor_manager_,
                           &user_secret_stash_storage_,
                           /*enable_create_backup_vk_with_uss =*/false);
  // Creating the user.
  EXPECT_TRUE(auth_session.OnUserCreated().ok());
  EXPECT_NE(auth_session.user_secret_stash_for_testing(), nullptr);
  EXPECT_NE(auth_session.user_secret_stash_main_key_for_testing(),
            std::nullopt);

  // Test.
  // Setting the expectation that the auth block utility will be called an that
  // key blob creation will fail.
  EXPECT_CALL(auth_block_utility_,
              GetAuthBlockTypeForCreation(false, false, false))
      .WillRepeatedly(Return(AuthBlockType::kTpmBoundToPcr));
  EXPECT_CALL(auth_block_utility_, CreateKeyBlobsWithAuthBlockAsync(
                                       AuthBlockType::kTpmBoundToPcr, _, _))
      .WillOnce([this](AuthBlockType, const AuthInput&,
                       AuthBlock::CreateCallback create_callback) {
        // Have the creation callback report an error.
        task_runner_->PostTask(
            FROM_HERE,
            base::BindOnce(
                std::move(create_callback),
                MakeStatus<CryptohomeCryptoError>(
                    kErrorLocationForTestingAuthSession,
                    error::ErrorActionSet(
                        {error::ErrorAction::kDevCheckUnexpectedState}),
                    CryptoError::CE_OTHER_CRYPTO),
                nullptr, nullptr));
        return true;
      });
  // Calling AddAuthFactor.
  user_data_auth::AddAuthFactorRequest request;
  request.set_auth_session_id(auth_session.serialized_token());
  request.mutable_auth_factor()->set_type(
      user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
  request.mutable_auth_factor()->set_label(kFakeLabel);
  request.mutable_auth_factor()->mutable_password_metadata();
  request.mutable_auth_input()->mutable_password_input()->set_secret(kFakePass);

  TestFuture<CryptohomeStatus> add_future;
  auth_session.AddAuthFactor(request, add_future.GetCallback());

  // Verify.
  ASSERT_THAT(add_future.Get(), NotOk());
  EXPECT_EQ(auth_session.TakeCredentialVerifier(), nullptr);
  ASSERT_EQ(add_future.Get()->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ADD_CREDENTIALS_FAILED);
  std::map<std::string, AuthFactorType> stored_factors =
      auth_factor_manager_.ListAuthFactors(SanitizeUserName(kFakeUsername));
  EXPECT_THAT(stored_factors, IsEmpty());
}

// Test that a new auth factor cannot be added for an unauthenticated
// authsession.
TEST_F(AuthSessionWithUssExperimentTest, AddPasswordAuthFactorUnAuthenticated) {
  // Setup.
  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;
  // Setting the expectation that the user exist.
  EXPECT_CALL(keyset_management_, UserExists(_)).WillRepeatedly(Return(true));
  AuthSession auth_session(kFakeUsername, flags, AuthIntent::kDecrypt,
                           /*on_timeout=*/base::DoNothing(), &crypto_,
                           &platform_, &user_session_map_, &keyset_management_,
                           &auth_block_utility_, &auth_factor_manager_,
                           &user_secret_stash_storage_,
                           /*enable_create_backup_vk_with_uss =*/false);

  user_data_auth::AddAuthFactorRequest request;
  request.set_auth_session_id(auth_session.serialized_token());
  request.mutable_auth_factor()->set_type(
      user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
  request.mutable_auth_factor()->set_label(kFakeLabel);
  request.mutable_auth_factor()->mutable_password_metadata();
  request.mutable_auth_input()->mutable_password_input()->set_secret(kFakePass);

  // Test and Verify.
  TestFuture<CryptohomeStatus> add_future;
  auth_session.AddAuthFactor(request, add_future.GetCallback());

  // Verify.
  ASSERT_THAT(add_future.Get(), NotOk());
  EXPECT_EQ(auth_session.TakeCredentialVerifier(), nullptr);
  ASSERT_EQ(add_future.Get()->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_UNAUTHENTICATED_AUTH_SESSION);
}

// Test that a new auth factor and a pin can be added to the newly created user,
// in case the UserSecretStash experiment is on.
TEST_F(AuthSessionWithUssExperimentTest, AddPasswordAndPinAuthFactorViaUss) {
  // Setup.
  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;
  // Setting the expectation that the user does not exist.
  EXPECT_CALL(keyset_management_, UserExists(_)).WillRepeatedly(Return(false));
  AuthSession auth_session(kFakeUsername, flags, AuthIntent::kDecrypt,
                           /*on_timeout=*/base::DoNothing(), &crypto_,
                           &platform_, &user_session_map_, &keyset_management_,
                           &auth_block_utility_, &auth_factor_manager_,
                           &user_secret_stash_storage_,
                           /*enable_create_backup_vk_with_uss =*/false);
  // Creating the user.
  EXPECT_TRUE(auth_session.OnUserCreated().ok());
  EXPECT_NE(auth_session.user_secret_stash_for_testing(), nullptr);
  EXPECT_NE(auth_session.user_secret_stash_main_key_for_testing(),
            std::nullopt);
  // Add a password first.
  // Setting the expectation that the auth block utility will create key blobs.
  EXPECT_CALL(auth_block_utility_,
              GetAuthBlockTypeForCreation(false, false, false))
      .WillRepeatedly(Return(AuthBlockType::kTpmBoundToPcr));
  EXPECT_CALL(auth_block_utility_, CreateKeyBlobsWithAuthBlockAsync(
                                       AuthBlockType::kTpmBoundToPcr, _, _))
      .WillOnce([](AuthBlockType auth_block_type, const AuthInput& auth_input,
                   AuthBlock::CreateCallback create_callback) {
        // Make an arbitrary auth block state type can be used in this test.
        auto key_blobs = std::make_unique<KeyBlobs>();
        key_blobs->vkk_key = brillo::SecureBlob("fake vkk key");
        auto auth_block_state = std::make_unique<AuthBlockState>();
        auth_block_state->state = TpmBoundToPcrAuthBlockState();
        std::move(create_callback)
            .Run(OkStatus<CryptohomeCryptoError>(), std::move(key_blobs),
                 std::move(auth_block_state));
        return true;
      });
  // Calling AddAuthFactor.
  user_data_auth::AddAuthFactorRequest request;
  request.set_auth_session_id(auth_session.serialized_token());
  request.mutable_auth_factor()->set_type(
      user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
  request.mutable_auth_factor()->set_label(kFakeLabel);
  request.mutable_auth_factor()->mutable_password_metadata();
  request.mutable_auth_input()->mutable_password_input()->set_secret(kFakePass);

  // Test and Verify.
  TestFuture<CryptohomeStatus> add_future;
  auth_session.AddAuthFactor(request, add_future.GetCallback());

  // Verify.
  EXPECT_THAT(add_future.Get(), IsOk());

  // Setting the expectation that the auth block utility will create key blobs.
  EXPECT_CALL(auth_block_utility_,
              GetAuthBlockTypeForCreation(true, false, false))
      .WillRepeatedly(Return(AuthBlockType::kPinWeaver));
  EXPECT_CALL(auth_block_utility_,
              CreateKeyBlobsWithAuthBlockAsync(AuthBlockType::kPinWeaver, _, _))
      .WillOnce([](AuthBlockType auth_block_type, const AuthInput& auth_input,
                   AuthBlock::CreateCallback create_callback) {
        // Make an arbitrary auth block state type can be used in this test.
        auto key_blobs = std::make_unique<KeyBlobs>();
        key_blobs->vkk_key = brillo::SecureBlob("fake vkk key");
        auto auth_block_state = std::make_unique<AuthBlockState>();
        auth_block_state->state = PinWeaverAuthBlockState();
        std::move(create_callback)
            .Run(OkStatus<CryptohomeCryptoError>(), std::move(key_blobs),
                 std::move(auth_block_state));
        return true;
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
  // Test and Verify.
  TestFuture<CryptohomeStatus> add_pin_future;
  auth_session.AddAuthFactor(add_pin_request, add_pin_future.GetCallback());

  // Verify.
  ASSERT_THAT(add_pin_future.Get(), IsOk());
  std::map<std::string, AuthFactorType> stored_factors =
      auth_factor_manager_.ListAuthFactors(SanitizeUserName(kFakeUsername));
  EXPECT_THAT(stored_factors,
              ElementsAre(Pair(kFakeLabel, AuthFactorType::kPassword),
                          Pair(kFakePinLabel, AuthFactorType::kPin)));
  EXPECT_THAT(auth_session.TakeCredentialVerifier(),
              IsVerifierPtrForPassword(kFakePass));

  // Ensure that a reset secret for the PIN was added.
  const auto reset_secret =
      auth_session.user_secret_stash_for_testing()->GetResetSecretForLabel(
          kFakePinLabel);
  EXPECT_TRUE(reset_secret.has_value());
  EXPECT_EQ(CRYPTOHOME_RESET_SECRET_LENGTH, reset_secret->size());
}

// Test that an existing user with an existing password auth factor can be
// authenticated, in case the UserSecretStash experiment is on.
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
  AuthSession auth_session(kFakeUsername, flags, AuthIntent::kDecrypt,
                           /*on_timeout=*/base::DoNothing(), &crypto_,
                           &platform_, &user_session_map_, &keyset_management_,
                           &auth_block_utility_, &auth_factor_manager_,
                           &user_secret_stash_storage_,
                           /*enable_create_backup_vk_with_uss =*/false);
  EXPECT_TRUE(auth_session.user_exists());

  // Test.
  // Setting the expectation that the auth block utility will derive key blobs.
  EXPECT_CALL(auth_block_utility_,
              GetAuthBlockTypeFromState(
                  AuthBlockStateTypeIs<TpmBoundToPcrAuthBlockState>()))
      .WillRepeatedly(Return(AuthBlockType::kTpmBoundToPcr));
  EXPECT_CALL(auth_block_utility_, DeriveKeyBlobsWithAuthBlockAsync(
                                       AuthBlockType::kTpmBoundToPcr, _, _, _))
      .WillOnce([&kFakePerCredentialSecret](
                    AuthBlockType auth_block_type, const AuthInput& auth_input,
                    const AuthBlockState& auth_state,
                    AuthBlock::DeriveCallback derive_callback) {
        auto key_blobs = std::make_unique<KeyBlobs>();
        key_blobs->vkk_key = kFakePerCredentialSecret;
        std::move(derive_callback)
            .Run(OkStatus<CryptohomeCryptoError>(), std::move(key_blobs));
        return true;
      });

  // Calling AuthenticateAuthFactor.
  user_data_auth::AuthenticateAuthFactorRequest request;
  request.set_auth_session_id(auth_session.serialized_token());
  request.set_auth_factor_label(kFakeLabel);
  request.mutable_auth_input()->mutable_password_input()->set_secret(kFakePass);
  TestFuture<CryptohomeStatus> authenticate_future;
  EXPECT_TRUE(auth_session.AuthenticateAuthFactor(
      request, authenticate_future.GetCallback()));

  // Verify.
  EXPECT_THAT(authenticate_future.Get(), IsOk());
  EXPECT_EQ(auth_session.GetStatus(), AuthStatus::kAuthStatusAuthenticated);
  EXPECT_THAT(
      auth_session.authorized_intents(),
      UnorderedElementsAre(AuthIntent::kDecrypt, AuthIntent::kVerifyOnly));
  EXPECT_NE(auth_session.user_secret_stash_for_testing(), nullptr);
  EXPECT_NE(auth_session.user_secret_stash_main_key_for_testing(),
            std::nullopt);
  EXPECT_THAT(auth_session.TakeCredentialVerifier(),
              IsVerifierPtrForPassword(kFakePass));
}

// Test that an existing user with an existing password auth factor can be
// authenticated, using asynchronous key derivation.
TEST_F(AuthSessionWithUssExperimentTest,
       AuthenticatePasswordAuthFactorViaAsyncUss) {
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
  AuthSession auth_session(kFakeUsername, flags, AuthIntent::kDecrypt,
                           /*on_timeout=*/base::DoNothing(), &crypto_,
                           &platform_, &user_session_map_, &keyset_management_,
                           &auth_block_utility_, &auth_factor_manager_,
                           &user_secret_stash_storage_,
                           /*enable_create_backup_vk_with_uss =*/false);
  EXPECT_TRUE(auth_session.user_exists());

  // Test.
  // Setting the expectation that the auth block utility will derive key blobs.
  EXPECT_CALL(auth_block_utility_,
              GetAuthBlockTypeFromState(
                  AuthBlockStateTypeIs<TpmBoundToPcrAuthBlockState>()))
      .WillRepeatedly(Return(AuthBlockType::kTpmBoundToPcr));
  EXPECT_CALL(auth_block_utility_, DeriveKeyBlobsWithAuthBlockAsync(
                                       AuthBlockType::kTpmBoundToPcr, _, _, _))
      .WillOnce([this, &kFakePerCredentialSecret](
                    AuthBlockType auth_block_type, const AuthInput& auth_input,
                    const AuthBlockState& auth_state,
                    AuthBlock::DeriveCallback derive_callback) {
        auto key_blobs = std::make_unique<KeyBlobs>();
        key_blobs->vkk_key = kFakePerCredentialSecret;
        task_runner_->PostTask(FROM_HERE,
                               base::BindOnce(std::move(derive_callback),
                                              OkStatus<CryptohomeCryptoError>(),
                                              std::move(key_blobs)));
        return true;
      });

  // Calling AuthenticateAuthFactor.
  user_data_auth::AuthenticateAuthFactorRequest request;
  request.set_auth_session_id(auth_session.serialized_token());
  request.set_auth_factor_label(kFakeLabel);
  request.mutable_auth_input()->mutable_password_input()->set_secret(kFakePass);

  TestFuture<CryptohomeStatus> authenticate_future;
  EXPECT_TRUE(auth_session.AuthenticateAuthFactor(
      request, authenticate_future.GetCallback()));

  // Verify.
  EXPECT_THAT(authenticate_future.Get(), IsOk());
  EXPECT_EQ(auth_session.GetStatus(), AuthStatus::kAuthStatusAuthenticated);
  EXPECT_THAT(
      auth_session.authorized_intents(),
      UnorderedElementsAre(AuthIntent::kDecrypt, AuthIntent::kVerifyOnly));
  EXPECT_NE(auth_session.user_secret_stash_for_testing(), nullptr);
  EXPECT_NE(auth_session.user_secret_stash_main_key_for_testing(),
            std::nullopt);
  EXPECT_THAT(auth_session.TakeCredentialVerifier(),
              IsVerifierPtrForPassword(kFakePass));
}

// Test then failure path with an existing user with an existing password auth
// factor when the asynchronous derivation fails.
TEST_F(AuthSessionWithUssExperimentTest,
       AuthenticatePasswordAuthFactorViaAsyncUssFails) {
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
  AuthSession auth_session(kFakeUsername, flags, AuthIntent::kDecrypt,
                           /*on_timeout=*/base::DoNothing(), &crypto_,
                           &platform_, &user_session_map_, &keyset_management_,
                           &auth_block_utility_, &auth_factor_manager_,
                           &user_secret_stash_storage_,
                           /*enable_create_backup_vk_with_uss =*/false);
  EXPECT_TRUE(auth_session.user_exists());

  // Test.
  // Setting the expectation that the auth block utility will derive key blobs.
  EXPECT_CALL(auth_block_utility_,
              GetAuthBlockTypeFromState(
                  AuthBlockStateTypeIs<TpmBoundToPcrAuthBlockState>()))
      .WillRepeatedly(Return(AuthBlockType::kTpmBoundToPcr));
  EXPECT_CALL(auth_block_utility_, DeriveKeyBlobsWithAuthBlockAsync(
                                       AuthBlockType::kTpmBoundToPcr, _, _, _))
      .WillOnce([this](AuthBlockType auth_block_type,
                       const AuthInput& auth_input,
                       const AuthBlockState& auth_state,
                       AuthBlock::DeriveCallback derive_callback) {
        task_runner_->PostTask(
            FROM_HERE,
            base::BindOnce(
                std::move(derive_callback),
                MakeStatus<CryptohomeCryptoError>(
                    kErrorLocationForTestingAuthSession,
                    error::ErrorActionSet(
                        {error::ErrorAction::kDevCheckUnexpectedState}),
                    CryptoError::CE_OTHER_CRYPTO),
                nullptr));
        return true;
      });

  // Calling AuthenticateAuthFactor.
  user_data_auth::AuthenticateAuthFactorRequest request;
  request.set_auth_session_id(auth_session.serialized_token());
  request.set_auth_factor_label(kFakeLabel);
  request.mutable_auth_input()->mutable_password_input()->set_secret(kFakePass);

  TestFuture<CryptohomeStatus> authenticate_future;
  EXPECT_TRUE(auth_session.AuthenticateAuthFactor(
      request, authenticate_future.GetCallback()));

  // Verify.
  ASSERT_THAT(authenticate_future.Get(), NotOk());
  EXPECT_EQ(authenticate_future.Get()->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED);
  EXPECT_EQ(auth_session.TakeCredentialVerifier(), nullptr);
  EXPECT_EQ(auth_session.user_secret_stash_for_testing(), nullptr);
  EXPECT_EQ(auth_session.user_secret_stash_main_key_for_testing(),
            std::nullopt);
}

// Test that an existing user with an existing pin auth factor can be
// authenticated, in case the UserSecretStash experiment is on.
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
  AuthSession auth_session(kFakeUsername, flags, AuthIntent::kDecrypt,
                           /*on_timeout=*/base::DoNothing(), &crypto_,
                           &platform_, &user_session_map_, &keyset_management_,
                           &auth_block_utility_, &auth_factor_manager_,
                           &user_secret_stash_storage_,
                           /*enable_create_backup_vk_with_uss =*/false);
  EXPECT_TRUE(auth_session.user_exists());

  // Test.
  // Setting the expectation that the auth block utility will derive key blobs.
  EXPECT_CALL(auth_block_utility_,
              GetAuthBlockTypeFromState(
                  AuthBlockStateTypeIs<PinWeaverAuthBlockState>()))
      .WillRepeatedly(Return(AuthBlockType::kPinWeaver));
  EXPECT_CALL(auth_block_utility_, DeriveKeyBlobsWithAuthBlockAsync(
                                       AuthBlockType::kPinWeaver, _, _, _))
      .WillOnce([&kFakePerCredentialSecret](
                    AuthBlockType auth_block_type, const AuthInput& auth_input,
                    const AuthBlockState& auth_state,
                    AuthBlock::DeriveCallback derive_callback) {
        auto key_blobs = std::make_unique<KeyBlobs>();
        key_blobs->vkk_key = kFakePerCredentialSecret;
        std::move(derive_callback)
            .Run(OkStatus<CryptohomeCryptoError>(), std::move(key_blobs));
        return true;
      });
  // Calling AuthenticateAuthFactor.
  user_data_auth::AuthenticateAuthFactorRequest request;
  request.set_auth_session_id(auth_session.serialized_token());
  request.set_auth_factor_label(kFakePinLabel);
  request.mutable_auth_input()->mutable_pin_input()->set_secret(kFakePin);
  TestFuture<CryptohomeStatus> authenticate_future;
  EXPECT_TRUE(auth_session.AuthenticateAuthFactor(
      request, authenticate_future.GetCallback()));

  // Verify.
  EXPECT_THAT(authenticate_future.Get(), IsOk());
  EXPECT_EQ(auth_session.GetStatus(), AuthStatus::kAuthStatusAuthenticated);
  EXPECT_THAT(
      auth_session.authorized_intents(),
      UnorderedElementsAre(AuthIntent::kDecrypt, AuthIntent::kVerifyOnly));
  EXPECT_NE(auth_session.user_secret_stash_for_testing(), nullptr);
  EXPECT_NE(auth_session.user_secret_stash_main_key_for_testing(),
            std::nullopt);
}

TEST_F(AuthSessionWithUssExperimentTest, AddCryptohomeRecoveryAuthFactor) {
  // Setup.
  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;
  // Setting the expectation that the user does not exist.
  EXPECT_CALL(keyset_management_, UserExists(_)).WillRepeatedly(Return(false));
  AuthSession auth_session(kFakeUsername, flags, AuthIntent::kDecrypt,
                           /*on_timeout=*/base::DoNothing(), &crypto_,
                           &platform_, &user_session_map_, &keyset_management_,
                           &auth_block_utility_, &auth_factor_manager_,
                           &user_secret_stash_storage_,
                           /*enable_create_backup_vk_with_uss =*/false);
  // Creating the user.
  EXPECT_TRUE(auth_session.OnUserCreated().ok());
  EXPECT_NE(auth_session.user_secret_stash_for_testing(), nullptr);
  EXPECT_NE(auth_session.user_secret_stash_main_key_for_testing(),
            std::nullopt);
  // Setting the expectation that the auth block utility will create key blobs.
  EXPECT_CALL(auth_block_utility_,
              GetAuthBlockTypeForCreation(false, true, false))
      .WillRepeatedly(Return(AuthBlockType::kCryptohomeRecovery));
  EXPECT_CALL(auth_block_utility_,
              CreateKeyBlobsWithAuthBlockAsync(
                  AuthBlockType::kCryptohomeRecovery, _, _))
      .WillOnce([](AuthBlockType auth_block_type, const AuthInput& auth_input,
                   AuthBlock::CreateCallback create_callback) {
        // Make an arbitrary auth block state type can be used in this test.
        auto key_blobs = std::make_unique<KeyBlobs>();
        key_blobs->vkk_key = brillo::SecureBlob("fake vkk key");
        auto auth_block_state = std::make_unique<AuthBlockState>();
        auth_block_state->state = CryptohomeRecoveryAuthBlockState();
        std::move(create_callback)
            .Run(OkStatus<CryptohomeCryptoError>(), std::move(key_blobs),
                 std::move(auth_block_state));
        return true;
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
  // Test and Verify.
  TestFuture<CryptohomeStatus> add_future;
  auth_session.AddAuthFactor(request, add_future.GetCallback());

  // Verify.
  EXPECT_THAT(add_future.Get(), IsOk());
  std::map<std::string, AuthFactorType> stored_factors =
      auth_factor_manager_.ListAuthFactors(SanitizeUserName(kFakeUsername));
  EXPECT_THAT(
      stored_factors,
      ElementsAre(Pair(kFakeLabel, AuthFactorType::kCryptohomeRecovery)));
  // There should be no verifier for the recovery factor.
  EXPECT_EQ(auth_session.TakeCredentialVerifier(), nullptr);
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
  AuthSession auth_session(kFakeUsername, flags, AuthIntent::kDecrypt,
                           /*on_timeout=*/base::DoNothing(), &crypto_,
                           &platform_, &user_session_map_, &keyset_management_,
                           &auth_block_utility_, &auth_factor_manager_,
                           &user_secret_stash_storage_,
                           /*enable_create_backup_vk_with_uss =*/false);
  EXPECT_TRUE(auth_session.user_exists());

  // Test.
  // Setting the expectation that the auth block utility will generate recovery
  // request.
  EXPECT_CALL(auth_block_utility_, GenerateRecoveryRequest(_, _, _, _, _, _, _))
      .WillOnce([](const std::string& obfuscated_username,
                   const cryptorecovery::RequestMetadata& request_metadata,
                   const brillo::Blob& epoch_response,
                   const CryptohomeRecoveryAuthBlockState& state,
                   hwsec::RecoveryCryptoFrontend* recovery_hwsec,
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
  EXPECT_THAT(auth_session.authorized_intents(), IsEmpty());
  EXPECT_TRUE(auth_session.cryptohome_recovery_ephemeral_pub_key_for_testing()
                  .has_value());
  EXPECT_EQ(
      auth_session.cryptohome_recovery_ephemeral_pub_key_for_testing().value(),
      brillo::SecureBlob("test"));

  // Test.
  // Setting the expectation that the auth block utility will derive key blobs.
  EXPECT_CALL(auth_block_utility_,
              GetAuthBlockTypeFromState(
                  AuthBlockStateTypeIs<CryptohomeRecoveryAuthBlockState>()))
      .WillRepeatedly(Return(AuthBlockType::kCryptohomeRecovery));
  EXPECT_CALL(auth_block_utility_,
              DeriveKeyBlobsWithAuthBlockAsync(
                  AuthBlockType::kCryptohomeRecovery, _, _, _))
      .WillOnce([&kFakePerCredentialSecret](
                    AuthBlockType auth_block_type, const AuthInput& auth_input,
                    const AuthBlockState& auth_state,
                    AuthBlock::DeriveCallback derive_callback) {
        auto key_blobs = std::make_unique<KeyBlobs>();
        key_blobs->vkk_key = kFakePerCredentialSecret;
        std::move(derive_callback)
            .Run(OkStatus<CryptohomeCryptoError>(), std::move(key_blobs));
        return true;
      });

  // Calling AuthenticateAuthFactor.
  user_data_auth::AuthenticateAuthFactorRequest authenticate_request;
  authenticate_request.set_auth_session_id(auth_session.serialized_token());
  authenticate_request.set_auth_factor_label(kFakeLabel);
  authenticate_request.mutable_auth_input()
      ->mutable_cryptohome_recovery_input()
      ->mutable_recovery_response();
  TestFuture<CryptohomeStatus> authenticate_future;
  EXPECT_TRUE(auth_session.AuthenticateAuthFactor(
      authenticate_request, authenticate_future.GetCallback()));

  // Verify.
  EXPECT_THAT(authenticate_future.Get(), IsOk());
  EXPECT_EQ(auth_session.GetStatus(), AuthStatus::kAuthStatusAuthenticated);
  EXPECT_THAT(
      auth_session.authorized_intents(),
      UnorderedElementsAre(AuthIntent::kDecrypt, AuthIntent::kVerifyOnly));
  EXPECT_NE(auth_session.user_secret_stash_for_testing(), nullptr);
  EXPECT_NE(auth_session.user_secret_stash_main_key_for_testing(),
            std::nullopt);
  // There should be no verifier created for the recovery factor.
  EXPECT_EQ(auth_session.TakeCredentialVerifier(), nullptr);
}

// Test that AuthenticateAuthFactor succeeds for the `AuthIntent::kVerifyOnly`
// scenario, using a credential verifier.
TEST_F(AuthSessionWithUssExperimentTest, LightweightPasswordAuthentication) {
  // Setup.
  EXPECT_CALL(keyset_management_, UserExists(_)).WillRepeatedly(Return(true));
  // Add the user session. Configure the credential verifier mock to succeed.
  auto user_session = std::make_unique<MockUserSession>();
  EXPECT_CALL(*user_session, VerifyCredentials(_)).WillOnce(Return(true));
  EXPECT_TRUE(user_session_map_.Add(kFakeUsername, std::move(user_session)));
  // Create an AuthSession with a fake factor. No authentication mocks are set
  // up, because the lightweight authentication should be used in the test.
  AuthSession auth_session(
      kFakeUsername, user_data_auth::AUTH_SESSION_FLAGS_NONE,
      AuthIntent::kVerifyOnly,
      /*on_timeout=*/base::DoNothing(), &crypto_, &platform_,
      &user_session_map_, &keyset_management_, &auth_block_utility_,
      &auth_factor_manager_, &user_secret_stash_storage_,
      /*enable_create_backup_vk_with_uss =*/false);
  std::map<std::string, std::unique_ptr<AuthFactor>> auth_factor_map;
  auth_factor_map.emplace(
      kFakeLabel,
      std::make_unique<AuthFactor>(AuthFactorType::kPassword, kFakeLabel,
                                   AuthFactorMetadata(), AuthBlockState()));
  auth_session.set_label_to_auth_factor_for_testing(std::move(auth_factor_map));

  // Test.
  user_data_auth::AuthenticateAuthFactorRequest request;
  request.set_auth_session_id(auth_session.serialized_token());
  request.set_auth_factor_label(kFakeLabel);
  request.mutable_auth_input()->mutable_password_input()->set_secret(kFakePass);
  TestFuture<CryptohomeStatus> authenticate_future;
  EXPECT_TRUE(auth_session.AuthenticateAuthFactor(
      request, authenticate_future.GetCallback()));

  // Verify.
  EXPECT_THAT(authenticate_future.Get(), IsOk());
  EXPECT_THAT(auth_session.authorized_intents(),
              UnorderedElementsAre(AuthIntent::kVerifyOnly));
}

// Test that AuthenticateAuthFactor succeeds for the `AuthIntent::kVerifyOnly`
// scenario, using the legacy fingerprint.
TEST_F(AuthSessionWithUssExperimentTest, LightweightFingerprintAuthentication) {
  // Setup.
  EXPECT_CALL(keyset_management_, UserExists(_)).WillRepeatedly(Return(true));
  // Add the user session. Configure the credential verifier mock to succeed.
  auto user_session = std::make_unique<MockUserSession>();
  // Create an AuthSession and add a mock for a successful auth block verify.
  AuthSession auth_session(
      kFakeUsername, user_data_auth::AUTH_SESSION_FLAGS_NONE,
      AuthIntent::kVerifyOnly,
      /*on_timeout=*/base::DoNothing(), &crypto_, &platform_,
      &user_session_map_, &keyset_management_, &auth_block_utility_,
      &auth_factor_manager_, &user_secret_stash_storage_,
      /*enable_create_backup_vk_with_uss =*/false);
  EXPECT_CALL(auth_block_utility_, IsVerifyWithAuthFactorSupported(
                                       AuthFactorType::kLegacyFingerprint))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(
      auth_block_utility_,
      VerifyWithAuthFactorAsync(AuthFactorType::kLegacyFingerprint, _, _))
      .WillOnce([](AuthFactorType, const AuthInput&,
                   AuthBlockUtility::VerifyCallback callback) {
        std::move(callback).Run(OkStatus<CryptohomeError>());
      });

  // Test.
  user_data_auth::AuthenticateAuthFactorRequest request;
  request.set_auth_session_id(auth_session.serialized_token());
  request.set_auth_factor_label(kFakeLabel);
  request.mutable_auth_input()->mutable_legacy_fingerprint_input();
  TestFuture<CryptohomeStatus> authenticate_future;
  EXPECT_TRUE(auth_session.AuthenticateAuthFactor(
      request, authenticate_future.GetCallback()));

  // Verify.
  EXPECT_THAT(authenticate_future.Get(), IsOk());
  EXPECT_THAT(auth_session.authorized_intents(),
              UnorderedElementsAre(AuthIntent::kVerifyOnly));
}

// Test that AuthenticateAuthFactor succeeds and doesn't use the credential
// verifier in the `AuthIntent::kDecrypt` scenario.
TEST_F(AuthSessionWithUssExperimentTest, NoLightweightAuthForDecryption) {
  // Setup.
  EXPECT_CALL(keyset_management_, UserExists(_)).WillRepeatedly(Return(true));
  // Add the user session. Expect that no verification calls are made.
  auto user_session = std::make_unique<MockUserSession>();
  EXPECT_CALL(*user_session, VerifyCredentials(_)).Times(0);
  EXPECT_TRUE(user_session_map_.Add(kFakeUsername, std::move(user_session)));
  // Create an AuthSession with a fake factor.
  AuthSession auth_session(
      kFakeUsername, user_data_auth::AUTH_SESSION_FLAGS_NONE,
      AuthIntent::kDecrypt,
      /*on_timeout=*/base::DoNothing(), &crypto_, &platform_,
      &user_session_map_, &keyset_management_, &auth_block_utility_,
      &auth_factor_manager_, &user_secret_stash_storage_,
      /*enable_create_backup_vk_with_uss =*/false);
  std::map<std::string, std::unique_ptr<AuthFactor>> auth_factor_map;
  auth_factor_map.emplace(
      kFakeLabel,
      std::make_unique<AuthFactor>(AuthFactorType::kPassword, kFakeLabel,
                                   AuthFactorMetadata(), AuthBlockState()));
  auth_session.set_label_to_auth_factor_for_testing(std::move(auth_factor_map));
  // Set up VaultKeyset authentication mock.
  EXPECT_CALL(keyset_management_, GetVaultKeyset(_, kFakeLabel))
      .WillOnce(Return(ByMove(std::make_unique<VaultKeyset>())));
  EXPECT_CALL(auth_block_utility_, GetAuthBlockStateFromVaultKeyset(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(auth_block_utility_, GetAuthBlockTypeFromState(_))
      .WillRepeatedly(Return(AuthBlockType::kTpmBoundToPcr));
  EXPECT_CALL(auth_block_utility_, DeriveKeyBlobsWithAuthBlockAsync(_, _, _, _))
      .WillOnce([](AuthBlockType, const AuthInput&, const AuthBlockState&,
                   AuthBlock::DeriveCallback derive_callback) {
        std::move(derive_callback)
            .Run(OkStatus<CryptohomeCryptoError>(),
                 std::make_unique<KeyBlobs>());
        return true;
      });
  EXPECT_CALL(keyset_management_, GetValidKeysetWithKeyBlobs(_, _, _))
      .WillOnce(Return(ByMove(std::make_unique<VaultKeyset>())));

  // Test.
  user_data_auth::AuthenticateAuthFactorRequest request;
  request.set_auth_session_id(auth_session.serialized_token());
  request.set_auth_factor_label(kFakeLabel);
  request.mutable_auth_input()->mutable_password_input()->set_secret(kFakePass);
  TestFuture<CryptohomeStatus> authenticate_future;
  EXPECT_TRUE(auth_session.AuthenticateAuthFactor(
      request, authenticate_future.GetCallback()));

  // Verify.
  EXPECT_THAT(authenticate_future.Get(), IsOk());
  EXPECT_THAT(
      auth_session.authorized_intents(),
      UnorderedElementsAre(AuthIntent::kDecrypt, AuthIntent::kVerifyOnly));
}

TEST_F(AuthSessionWithUssExperimentTest, RemoveAuthFactor) {
  // Setup.
  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;
  // Setting the expectation that the user does not exist.
  EXPECT_CALL(keyset_management_, UserExists(_)).WillRepeatedly(Return(false));
  AuthSession auth_session(kFakeUsername, flags, AuthIntent::kDecrypt,
                           /*on_timeout=*/base::DoNothing(), &crypto_,
                           &platform_, &user_session_map_, &keyset_management_,
                           &auth_block_utility_, &auth_factor_manager_,
                           &user_secret_stash_storage_,
                           /*enable_create_backup_vk_with_uss =*/false);
  // Creating the user.
  EXPECT_TRUE(auth_session.OnUserCreated().ok());
  EXPECT_NE(auth_session.user_secret_stash_for_testing(), nullptr);
  EXPECT_NE(auth_session.user_secret_stash_main_key_for_testing(),
            std::nullopt);

  user_data_auth::CryptohomeErrorCode error =
      user_data_auth::CRYPTOHOME_ERROR_NOT_SET;

  error = AddPasswordAuthFactor(kFakePass, auth_session);
  EXPECT_EQ(error, user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  error = AddPinAuthFactor(kFakePin, auth_session);
  EXPECT_EQ(error, user_data_auth::CRYPTOHOME_ERROR_NOT_SET);

  // Both password and pin are available.
  std::map<std::string, AuthFactorType> stored_factors =
      auth_factor_manager_.ListAuthFactors(SanitizeUserName(kFakeUsername));
  EXPECT_THAT(stored_factors,
              ElementsAre(Pair(kFakeLabel, AuthFactorType::kPassword),
                          Pair(kFakePinLabel, AuthFactorType::kPin)));
  EXPECT_NE(auth_session.label_to_auth_factor_.find(kFakeLabel),
            auth_session.label_to_auth_factor_.end());
  EXPECT_NE(auth_session.label_to_auth_factor_.find(kFakePinLabel),
            auth_session.label_to_auth_factor_.end());

  // Test.

  // Calling RemoveAuthFactor for pin.
  user_data_auth::RemoveAuthFactorRequest request;
  request.set_auth_session_id(auth_session.serialized_token());
  request.set_auth_factor_label(kFakePinLabel);

  TestFuture<CryptohomeStatus> remove_future;
  auth_session.RemoveAuthFactor(request, remove_future.GetCallback());

  EXPECT_THAT(remove_future.Get(), IsOk());

  // Only password is available.
  std::map<std::string, AuthFactorType> stored_factors_1 =
      auth_factor_manager_.ListAuthFactors(SanitizeUserName(kFakeUsername));
  EXPECT_THAT(stored_factors_1,
              ElementsAre(Pair(kFakeLabel, AuthFactorType::kPassword)));
  EXPECT_NE(auth_session.label_to_auth_factor_.find(kFakeLabel),
            auth_session.label_to_auth_factor_.end());
  EXPECT_EQ(auth_session.label_to_auth_factor_.find(kFakePinLabel),
            auth_session.label_to_auth_factor_.end());

  // Calling AuthenticateAuthFactor for password succeeds.
  error = AuthenticatePasswordAuthFactor(kFakePass, auth_session);
  EXPECT_EQ(error, user_data_auth::CRYPTOHOME_ERROR_NOT_SET);

  // Calling AuthenticateAuthFactor for pin fails.
  user_data_auth::AuthenticateAuthFactorRequest auth_request;
  auth_request.set_auth_session_id(auth_session.serialized_token());
  auth_request.set_auth_factor_label(kFakePinLabel);
  auth_request.mutable_auth_input()->mutable_pin_input()->set_secret(kFakePin);
  TestFuture<CryptohomeStatus> authenticate_future;
  auth_session.AuthenticateAuthFactor(auth_request,
                                      authenticate_future.GetCallback());

  // Verify.
  ASSERT_THAT(authenticate_future.Get(), NotOk());
  EXPECT_EQ(authenticate_future.Get()->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_KEY_NOT_FOUND);
  // The verifier still uses the password.
  EXPECT_THAT(auth_session.TakeCredentialVerifier(),
              IsVerifierPtrForPassword(kFakePass));
}

// The test adds, removes and adds the same auth factor again.
TEST_F(AuthSessionWithUssExperimentTest, RemoveAndReAddAuthFactor) {
  // Setup.
  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;
  // Setting the expectation that the user does not exist.
  EXPECT_CALL(keyset_management_, UserExists(_)).WillRepeatedly(Return(false));
  AuthSession auth_session(kFakeUsername, flags, AuthIntent::kDecrypt,
                           /*on_timeout=*/base::DoNothing(), &crypto_,
                           &platform_, &user_session_map_, &keyset_management_,
                           &auth_block_utility_, &auth_factor_manager_,
                           &user_secret_stash_storage_,
                           /*enable_create_backup_vk_with_uss =*/false);
  // Creating the user.
  EXPECT_TRUE(auth_session.OnUserCreated().ok());
  EXPECT_NE(auth_session.user_secret_stash_for_testing(), nullptr);
  EXPECT_NE(auth_session.user_secret_stash_main_key_for_testing(),
            std::nullopt);

  user_data_auth::CryptohomeErrorCode error =
      user_data_auth::CRYPTOHOME_ERROR_NOT_SET;

  error = AddPasswordAuthFactor(kFakePass, auth_session);
  EXPECT_EQ(error, user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  error = AddPinAuthFactor(kFakePin, auth_session);
  EXPECT_EQ(error, user_data_auth::CRYPTOHOME_ERROR_NOT_SET);

  // Test.

  // Calling RemoveAuthFactor for pin.
  user_data_auth::RemoveAuthFactorRequest request;
  request.set_auth_session_id(auth_session.serialized_token());
  request.set_auth_factor_label(kFakePinLabel);

  TestFuture<CryptohomeStatus> remove_future;
  auth_session.RemoveAuthFactor(request, remove_future.GetCallback());

  EXPECT_THAT(remove_future.Get(), IsOk());

  // Add the same pin auth factor again.
  error = AddPinAuthFactor(kFakePin, auth_session);
  EXPECT_EQ(error, user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  // The verifier still uses the original password.
  EXPECT_THAT(auth_session.TakeCredentialVerifier(),
              IsVerifierPtrForPassword(kFakePass));
}

TEST_F(AuthSessionWithUssExperimentTest, RemoveAuthFactorFailsForLastFactor) {
  // Setup.
  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;
  // Setting the expectation that the user does not exist.
  EXPECT_CALL(keyset_management_, UserExists(_)).WillRepeatedly(Return(false));
  AuthSession auth_session(kFakeUsername, flags, AuthIntent::kDecrypt,
                           /*on_timeout=*/base::DoNothing(), &crypto_,
                           &platform_, &user_session_map_, &keyset_management_,
                           &auth_block_utility_, &auth_factor_manager_,
                           &user_secret_stash_storage_,
                           /*enable_create_backup_vk_with_uss =*/false);
  // Creating the user.
  EXPECT_TRUE(auth_session.OnUserCreated().ok());
  EXPECT_NE(auth_session.user_secret_stash_for_testing(), nullptr);
  EXPECT_NE(auth_session.user_secret_stash_main_key_for_testing(),
            std::nullopt);

  user_data_auth::CryptohomeErrorCode error =
      user_data_auth::CRYPTOHOME_ERROR_NOT_SET;

  error = AddPasswordAuthFactor(kFakePass, auth_session);
  EXPECT_EQ(error, user_data_auth::CRYPTOHOME_ERROR_NOT_SET);

  // Test.

  // Calling RemoveAuthFactor for password.
  user_data_auth::RemoveAuthFactorRequest request;
  request.set_auth_session_id(auth_session.serialized_token());
  request.set_auth_factor_label(kFakeLabel);

  TestFuture<CryptohomeStatus> remove_future;
  auth_session.RemoveAuthFactor(request, remove_future.GetCallback());

  // Verify.
  ASSERT_THAT(remove_future.Get(), NotOk());
  EXPECT_EQ(remove_future.Get()->local_legacy_error(),
            user_data_auth::CRYPTOHOME_REMOVE_CREDENTIALS_FAILED);
  // The verifier is still set after the removal failed.
  EXPECT_THAT(auth_session.TakeCredentialVerifier(),
              IsVerifierPtrForPassword(kFakePass));
}

TEST_F(AuthSessionTest, RemoveAuthFactorFailsForUnauthenticatedAuthSession) {
  // Setup.
  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;
  // Setting the expectation that the user does exist.
  EXPECT_CALL(keyset_management_, UserExists(_)).WillRepeatedly(Return(true));
  AuthSession auth_session(kFakeUsername, flags, AuthIntent::kDecrypt,
                           /*on_timeout=*/base::DoNothing(), &crypto_,
                           &platform_, &user_session_map_, &keyset_management_,
                           &auth_block_utility_, &auth_factor_manager_,
                           &user_secret_stash_storage_,
                           /*enable_create_backup_vk_with_uss =*/false);
  // Test.
  user_data_auth::RemoveAuthFactorRequest request;
  request.set_auth_session_id(auth_session.serialized_token());
  request.set_auth_factor_label(kFakeLabel);
  TestFuture<CryptohomeStatus> remove_future;
  auth_session.RemoveAuthFactor(request, remove_future.GetCallback());

  ASSERT_THAT(remove_future.Get(), NotOk());
  EXPECT_EQ(remove_future.Get()->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_UNAUTHENTICATED_AUTH_SESSION);
}

TEST_F(AuthSessionWithUssExperimentTest, UpdateAuthFactor) {
  // Setup.
  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;
  // Setting the expectation that the user does not exist.
  EXPECT_CALL(keyset_management_, UserExists(_)).WillRepeatedly(Return(false));
  AuthSession auth_session(kFakeUsername, flags, AuthIntent::kDecrypt,
                           /*on_timeout=*/base::DoNothing(), &crypto_,
                           &platform_, &user_session_map_, &keyset_management_,
                           &auth_block_utility_, &auth_factor_manager_,
                           &user_secret_stash_storage_,
                           /*enable_create_backup_vk_with_uss =*/false);
  // Creating the user.
  EXPECT_TRUE(auth_session.OnUserCreated().ok());
  EXPECT_NE(auth_session.user_secret_stash_for_testing(), nullptr);
  EXPECT_NE(auth_session.user_secret_stash_main_key_for_testing(),
            std::nullopt);

  user_data_auth::CryptohomeErrorCode error =
      user_data_auth::CRYPTOHOME_ERROR_NOT_SET;

  // Calling AddAuthFactor.
  error = AddPasswordAuthFactor(kFakePass, auth_session);
  EXPECT_EQ(error, user_data_auth::CRYPTOHOME_ERROR_NOT_SET);

  std::string new_pass = "update fake pass";

  // Test.

  // Calling UpdateAuthFactor.
  error = UpdatePasswordAuthFactor(new_pass, auth_session);
  EXPECT_EQ(error, user_data_auth::CRYPTOHOME_ERROR_NOT_SET);

  AuthSession new_auth_session(
      kFakeUsername, flags, AuthIntent::kDecrypt,
      /*on_timeout=*/base::DoNothing(), &crypto_, &platform_,
      &user_session_map_, &keyset_management_, &auth_block_utility_,
      &auth_factor_manager_, &user_secret_stash_storage_,
      /*enable_create_backup_vk_with_uss =*/false);
  EXPECT_EQ(new_auth_session.GetStatus(),
            AuthStatus::kAuthStatusFurtherFactorRequired);
  EXPECT_THAT(new_auth_session.authorized_intents(), IsEmpty());

  // Verify.
  // The credential verifier uses the new password.
  EXPECT_THAT(auth_session.TakeCredentialVerifier(),
              IsVerifierPtrForPassword(new_pass));
  // AuthenticateAuthFactor should succeed using the new password.
  error = AuthenticatePasswordAuthFactor(new_pass, new_auth_session);
  EXPECT_EQ(error, user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  EXPECT_EQ(new_auth_session.GetStatus(), AuthStatus::kAuthStatusAuthenticated);
  EXPECT_THAT(
      new_auth_session.authorized_intents(),
      UnorderedElementsAre(AuthIntent::kDecrypt, AuthIntent::kVerifyOnly));
}

TEST_F(AuthSessionWithUssExperimentTest, UpdateAuthFactorFailsForWrongLabel) {
  // Setup.
  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;
  // Setting the expectation that the user does not exist.
  EXPECT_CALL(keyset_management_, UserExists(_)).WillRepeatedly(Return(false));
  AuthSession auth_session(kFakeUsername, flags, AuthIntent::kDecrypt,
                           /*on_timeout=*/base::DoNothing(), &crypto_,
                           &platform_, &user_session_map_, &keyset_management_,
                           &auth_block_utility_, &auth_factor_manager_,
                           &user_secret_stash_storage_,
                           /*enable_create_backup_vk_with_uss =*/false);
  // Creating the user.
  EXPECT_TRUE(auth_session.OnUserCreated().ok());
  EXPECT_NE(auth_session.user_secret_stash_for_testing(), nullptr);
  EXPECT_NE(auth_session.user_secret_stash_main_key_for_testing(),
            std::nullopt);

  user_data_auth::CryptohomeErrorCode error =
      user_data_auth::CRYPTOHOME_ERROR_NOT_SET;

  // Calling AddAuthFactor.
  error = AddPasswordAuthFactor(kFakePass, auth_session);
  EXPECT_EQ(error, user_data_auth::CRYPTOHOME_ERROR_NOT_SET);

  std::string new_pass = "update fake pass";

  // Test.

  // Calling UpdateAuthFactor.
  user_data_auth::UpdateAuthFactorRequest request;
  request.set_auth_session_id(auth_session.serialized_token());
  request.set_auth_factor_label(kFakeLabel);
  request.mutable_auth_factor()->set_type(
      user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
  request.mutable_auth_factor()->set_label("different new label");
  request.mutable_auth_factor()->mutable_password_metadata();
  request.mutable_auth_input()->mutable_password_input()->set_secret(new_pass);

  TestFuture<CryptohomeStatus> update_future;
  auth_session.UpdateAuthFactor(request, update_future.GetCallback());

  // Verify.
  ASSERT_THAT(update_future.Get(), NotOk());
  EXPECT_EQ(update_future.Get()->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
  // The verifier still uses the original password.
  EXPECT_THAT(auth_session.TakeCredentialVerifier(),
              IsVerifierPtrForPassword(kFakePass));
}

TEST_F(AuthSessionWithUssExperimentTest, UpdateAuthFactorFailsForWrongType) {
  // Setup.
  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;
  // Setting the expectation that the user does not exist.
  EXPECT_CALL(keyset_management_, UserExists(_)).WillRepeatedly(Return(false));
  AuthSession auth_session(kFakeUsername, flags, AuthIntent::kDecrypt,
                           /*on_timeout=*/base::DoNothing(), &crypto_,
                           &platform_, &user_session_map_, &keyset_management_,
                           &auth_block_utility_, &auth_factor_manager_,
                           &user_secret_stash_storage_,
                           /*enable_create_backup_vk_with_uss =*/false);
  // Creating the user.
  EXPECT_TRUE(auth_session.OnUserCreated().ok());
  EXPECT_NE(auth_session.user_secret_stash_for_testing(), nullptr);
  EXPECT_NE(auth_session.user_secret_stash_main_key_for_testing(),
            std::nullopt);

  user_data_auth::CryptohomeErrorCode error =
      user_data_auth::CRYPTOHOME_ERROR_NOT_SET;

  // Calling AddAuthFactor.
  error = AddPasswordAuthFactor(kFakePass, auth_session);
  EXPECT_EQ(error, user_data_auth::CRYPTOHOME_ERROR_NOT_SET);

  // Test.

  // Calling UpdateAuthFactor.
  user_data_auth::UpdateAuthFactorRequest request;
  request.set_auth_session_id(auth_session.serialized_token());
  request.set_auth_factor_label(kFakeLabel);
  request.mutable_auth_factor()->set_type(user_data_auth::AUTH_FACTOR_TYPE_PIN);
  request.mutable_auth_factor()->set_label(kFakeLabel);
  request.mutable_auth_factor()->mutable_pin_metadata();
  request.mutable_auth_input()->mutable_pin_input()->set_secret(kFakePin);

  TestFuture<CryptohomeStatus> update_future;
  auth_session.UpdateAuthFactor(request, update_future.GetCallback());

  // Verify.
  ASSERT_THAT(update_future.Get(), NotOk());
  EXPECT_EQ(update_future.Get()->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
  // The verifier still uses the original password.
  EXPECT_THAT(auth_session.TakeCredentialVerifier(),
              IsVerifierPtrForPassword(kFakePass));
}

TEST_F(AuthSessionWithUssExperimentTest,
       UpdateAuthFactorFailsWhenLabelDoesntExist) {
  // Setup.
  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;
  // Setting the expectation that the user does not exist.
  EXPECT_CALL(keyset_management_, UserExists(_)).WillRepeatedly(Return(false));
  AuthSession auth_session(kFakeUsername, flags, AuthIntent::kDecrypt,
                           /*on_timeout=*/base::DoNothing(), &crypto_,
                           &platform_, &user_session_map_, &keyset_management_,
                           &auth_block_utility_, &auth_factor_manager_,
                           &user_secret_stash_storage_,
                           /*enable_create_backup_vk_with_uss =*/false);
  // Creating the user.
  EXPECT_TRUE(auth_session.OnUserCreated().ok());
  EXPECT_NE(auth_session.user_secret_stash_for_testing(), nullptr);
  EXPECT_NE(auth_session.user_secret_stash_main_key_for_testing(),
            std::nullopt);

  user_data_auth::CryptohomeErrorCode error =
      user_data_auth::CRYPTOHOME_ERROR_NOT_SET;

  // Calling AddAuthFactor.
  error = AddPasswordAuthFactor(kFakePass, auth_session);
  EXPECT_EQ(error, user_data_auth::CRYPTOHOME_ERROR_NOT_SET);

  // Test.

  // Calling UpdateAuthFactor.
  user_data_auth::UpdateAuthFactorRequest request;
  request.set_auth_session_id(auth_session.serialized_token());
  request.set_auth_factor_label("label doesn't exist");
  request.mutable_auth_factor()->set_type(
      user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
  request.mutable_auth_factor()->set_label(kFakeLabel);
  request.mutable_auth_factor()->mutable_password_metadata();
  request.mutable_auth_input()->mutable_password_input()->set_secret(kFakePass);

  TestFuture<CryptohomeStatus> update_future;
  auth_session.UpdateAuthFactor(request, update_future.GetCallback());

  // Verify.
  ASSERT_THAT(update_future.Get(), NotOk());
  EXPECT_EQ(update_future.Get()->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_KEY_NOT_FOUND);
  // The verifier still uses the original password.
  EXPECT_THAT(auth_session.TakeCredentialVerifier(),
              IsVerifierPtrForPassword(kFakePass));
}

// Test that AuthenticateAuthFactor succeeds in the `AuthIntent::kWebAuthn`
// scenario.
TEST_F(AuthSessionWithUssExperimentTest, AuthenticateAuthFactorWebAuthnIntent) {
  // Setup.
  EXPECT_CALL(keyset_management_, UserExists(_)).WillRepeatedly(Return(true));
  // Add the user session. Expect that no verification calls are made.
  auto user_session = std::make_unique<MockUserSession>();
  EXPECT_CALL(*user_session, PrepareWebAuthnSecret(_, _));
  EXPECT_TRUE(user_session_map_.Add(kFakeUsername, std::move(user_session)));
  // Create an AuthSession with a fake factor.
  AuthSession auth_session(
      kFakeUsername, user_data_auth::AUTH_SESSION_FLAGS_NONE,
      AuthIntent::kWebAuthn,
      /*on_timeout=*/base::DoNothing(), &crypto_, &platform_,
      &user_session_map_, &keyset_management_, &auth_block_utility_,
      &auth_factor_manager_, &user_secret_stash_storage_,
      /*enable_create_backup_vk_with_uss =*/false);
  std::map<std::string, std::unique_ptr<AuthFactor>> auth_factor_map;
  auth_factor_map.emplace(
      kFakeLabel,
      std::make_unique<AuthFactor>(AuthFactorType::kPassword, kFakeLabel,
                                   AuthFactorMetadata(), AuthBlockState()));
  auth_session.set_label_to_auth_factor_for_testing(std::move(auth_factor_map));
  // Set up VaultKeyset authentication mock.
  EXPECT_CALL(keyset_management_, GetVaultKeyset(_, kFakeLabel))
      .WillOnce(Return(ByMove(std::make_unique<VaultKeyset>())));
  EXPECT_CALL(auth_block_utility_, GetAuthBlockStateFromVaultKeyset(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(auth_block_utility_, GetAuthBlockTypeFromState(_))
      .WillRepeatedly(Return(AuthBlockType::kTpmBoundToPcr));
  EXPECT_CALL(auth_block_utility_, DeriveKeyBlobsWithAuthBlockAsync(_, _, _, _))
      .WillOnce([](AuthBlockType, const AuthInput&, const AuthBlockState&,
                   AuthBlock::DeriveCallback derive_callback) {
        std::move(derive_callback)
            .Run(OkStatus<CryptohomeCryptoError>(),
                 std::make_unique<KeyBlobs>());
        return true;
      });
  EXPECT_CALL(keyset_management_, GetValidKeysetWithKeyBlobs(_, _, _))
      .WillOnce(Return(ByMove(std::make_unique<VaultKeyset>())));

  // Test.
  user_data_auth::AuthenticateAuthFactorRequest request;
  request.set_auth_session_id(auth_session.serialized_token());
  request.set_auth_factor_label(kFakeLabel);
  request.mutable_auth_input()->mutable_password_input()->set_secret(kFakePass);
  TestFuture<CryptohomeStatus> authenticate_future;
  EXPECT_TRUE(auth_session.AuthenticateAuthFactor(
      request, authenticate_future.GetCallback()));

  // Verify.
  EXPECT_THAT(authenticate_future.Get(), IsOk());
  EXPECT_THAT(
      auth_session.authorized_intents(),
      UnorderedElementsAre(AuthIntent::kDecrypt, AuthIntent::kVerifyOnly,
                           AuthIntent::kWebAuthn));
}

}  // namespace cryptohome
