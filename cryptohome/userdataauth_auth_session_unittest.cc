// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/userdataauth.h"

#include <memory>
#include <string>
#include <utility>

#include <base/containers/span.h>
#include <base/memory/scoped_refptr.h>
#include <base/test/bind.h>
#include <base/test/mock_callback.h>
#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <base/time/time.h>
#include <brillo/cryptohome.h>
#include <brillo/secure_blob.h>
#include <cryptohome/proto_bindings/auth_factor.pb.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>
#include <gmock/gmock-matchers.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libhwsec/frontend/cryptohome/mock_frontend.h>
#include <libhwsec/frontend/pinweaver/mock_frontend.h>
#include <libhwsec/frontend/pinweaver_manager/mock_frontend.h>
#include <libhwsec-foundation/error/testing_helper.h>

#include "cryptohome/auth_blocks/auth_block_utility_impl.h"
#include "cryptohome/auth_blocks/fp_service.h"
#include "cryptohome/auth_blocks/mock_auth_block_utility.h"
#include "cryptohome/auth_factor/auth_factor_manager.h"
#include "cryptohome/auth_factor/types/manager.h"
#include "cryptohome/auth_session.h"
#include "cryptohome/auth_session_manager.h"
#include "cryptohome/cleanup/mock_user_oldest_activity_timestamp_manager.h"
#include "cryptohome/crypto.h"
#include "cryptohome/crypto_error.h"
#include "cryptohome/error/cryptohome_crypto_error.h"
#include "cryptohome/error/cryptohome_error.h"
#include "cryptohome/fake_features.h"
#include "cryptohome/filesystem_layout.h"
#include "cryptohome/mock_credential_verifier.h"
#include "cryptohome/mock_cryptohome_keys_manager.h"
#include "cryptohome/mock_install_attributes.h"
#include "cryptohome/mock_keyset_management.h"
#include "cryptohome/mock_platform.h"
#include "cryptohome/pinweaver_manager/mock_le_credential_manager.h"
#include "cryptohome/pkcs11/mock_pkcs11_token_factory.h"
#include "cryptohome/storage/error.h"
#include "cryptohome/storage/mock_homedirs.h"
#include "cryptohome/storage/mock_mount.h"
#include "cryptohome/user_secret_stash/storage.h"
#include "cryptohome/user_secret_stash/user_secret_stash.h"
#include "cryptohome/user_session/mock_user_session.h"
#include "cryptohome/user_session/mock_user_session_factory.h"
#include "cryptohome/user_session/real_user_session.h"
#include "cryptohome/user_session/user_session_map.h"
#include "cryptohome/username.h"
#include "cryptohome/vault_keyset.h"

namespace cryptohome {

using ::testing::_;
using ::testing::AllOf;
using ::testing::An;
using ::testing::ByMove;
using ::testing::DoAll;
using ::testing::Eq;
using ::testing::Gt;
using ::testing::Invoke;
using ::testing::IsEmpty;
using ::testing::IsNull;
using ::testing::Le;
using ::testing::NiceMock;
using ::testing::NotNull;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::SetArgPointee;
using ::testing::UnorderedElementsAre;

using base::test::TaskEnvironment;
using base::test::TestFuture;
using brillo::cryptohome::home::SanitizeUserName;
using error::CryptohomeCryptoError;
using error::CryptohomeError;
using error::CryptohomeMountError;
using hwsec::TPMError;
using hwsec_foundation::error::testing::IsOk;
using hwsec_foundation::error::testing::NotOk;
using hwsec_foundation::error::testing::ReturnError;
using hwsec_foundation::error::testing::ReturnOk;
using hwsec_foundation::error::testing::ReturnValue;
using hwsec_foundation::status::MakeStatus;
using hwsec_foundation::status::OkStatus;
using user_data_auth::AUTH_INTENT_DECRYPT;
using user_data_auth::AUTH_INTENT_VERIFY_ONLY;
using user_data_auth::AUTH_INTENT_WEBAUTHN;
using user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_EPHEMERAL_USER;

namespace {

using AuthenticateAuthFactorCallback = base::OnceCallback<void(
    const user_data_auth::AuthenticateAuthFactorReply&)>;
using AddAuthFactorCallback =
    base::OnceCallback<void(const user_data_auth::AddAuthFactorReply&)>;

// Time in seconds.
constexpr char kPassword[] = "password";
constexpr char kPassword2[] = "password2";
constexpr char kPasswordLabel[] = "fake-password-label";
constexpr char kPasswordLabel2[] = "fake-password-label2";
constexpr char kUsernameString[] = "foo@example.com";
constexpr char kUsername2String[] = "foo2@example.com";
constexpr char kUsername3String[] = "foo3@example.com";
constexpr char kSalt[] = "salt";
constexpr char kPublicHash[] = "public key hash";
constexpr int kAuthValueRounds = 5;
// 300 seconds should be left right as we authenticate.
constexpr base::TimeDelta kDefaultTimeAfterAuthenticate = base::Seconds(300);
constexpr base::TimeDelta kDefaultExtensionDuration = base::Seconds(60);

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
  serialized_vk.set_wrapped_reset_seed("wrapped-reset-seed");
  serialized_vk.mutable_key_data()->set_type(KeyData::KEY_TYPE_PASSWORD);
  serialized_vk.mutable_key_data()->set_label(label);
  return serialized_vk;
}

void MockVKToAuthFactorMapLoading(
    const ObfuscatedUsername& obfuscated_username,
    const std::vector<SerializedVaultKeyset>& serialized_vks,
    MockKeysetManagement& keyset_management) {
  std::vector<int> key_indices;
  for (size_t index = 0; index < serialized_vks.size(); ++index) {
    key_indices.push_back(index);
  }
  EXPECT_CALL(keyset_management, GetVaultKeysets(obfuscated_username, _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(key_indices), Return(true)));

  for (size_t index = 0; index < serialized_vks.size(); ++index) {
    const auto& serialized_vk = serialized_vks[index];
    EXPECT_CALL(keyset_management,
                LoadVaultKeysetForUser(obfuscated_username, index))
        .WillRepeatedly([=](const ObfuscatedUsername&, int) {
          auto vk = std::make_unique<VaultKeyset>();
          vk->InitializeFromSerialized(serialized_vk);
          return vk;
        });
  }
}

void MockKeysetLoadingByLabel(const ObfuscatedUsername& obfuscated_username,
                              const SerializedVaultKeyset& serialized_vk,
                              MockKeysetManagement& keyset_management) {
  EXPECT_CALL(
      keyset_management,
      GetVaultKeyset(obfuscated_username, serialized_vk.key_data().label()))
      .WillRepeatedly([=](const ObfuscatedUsername&, const std::string&) {
        auto vk = std::make_unique<VaultKeyset>();
        vk->InitializeFromSerialized(serialized_vk);
        return vk;
      });
}

void MockOwnerUser(const std::string& username, MockHomeDirs& homedirs) {
  EXPECT_CALL(homedirs, GetPlainOwner(_))
      .WillRepeatedly(
          DoAll(SetArgPointee<0>(Username(username)), Return(true)));
}

}  // namespace

class AuthSessionInterfaceTestBase : public ::testing::Test {
 public:
  AuthSessionInterfaceTestBase()
      : crypto_(&hwsec_,
                &pinweaver_,
                &hwsec_pw_manager_,
                &cryptohome_keys_manager_,
                nullptr) {
    SetUpHWSecExpectations();
    MockLECredentialManager* le_cred_manager = new MockLECredentialManager();
    crypto_.set_le_manager_for_testing(
        std::unique_ptr<LECredentialManager>(le_cred_manager));
    crypto_.Init();
    auth_block_utility_impl_ = std::make_unique<AuthBlockUtilityImpl>(
        &keyset_management_, &crypto_, &platform_, &features_.async,
        AsyncInitPtr<ChallengeCredentialsHelper>(nullptr), nullptr,
        AsyncInitPtr<BiometricsAuthBlockService>(nullptr));

    userdataauth_.set_platform(&platform_);
    userdataauth_.set_homedirs(&homedirs_);
    userdataauth_.set_user_session_factory(&user_session_factory_);
    userdataauth_.set_keyset_management(&keyset_management_);
    userdataauth_.set_auth_factor_driver_manager_for_testing(
        &auth_factor_driver_manager_);
    userdataauth_.set_auth_factor_manager_for_testing(&auth_factor_manager_);
    userdataauth_.set_uss_storage_for_testing(&uss_storage_);
    userdataauth_.set_user_session_map_for_testing(&user_session_map_);
    userdataauth_.set_pkcs11_token_factory(&pkcs11_token_factory_);
    userdataauth_.set_user_activity_timestamp_manager(
        &user_activity_timestamp_manager_);
    userdataauth_.set_install_attrs(&install_attrs_);
    userdataauth_.set_mount_task_runner(
        task_environment_.GetMainThreadTaskRunner());
    userdataauth_.set_pinweaver(&pinweaver_);
  }

  void SetUpHWSecExpectations() {
    EXPECT_CALL(hwsec_, IsEnabled()).WillRepeatedly(ReturnValue(true));
    EXPECT_CALL(hwsec_, IsReady()).WillRepeatedly(ReturnValue(true));
    EXPECT_CALL(hwsec_, IsSealingSupported()).WillRepeatedly(ReturnValue(true));
    EXPECT_CALL(hwsec_, IsPinWeaverEnabled()).WillRepeatedly(ReturnValue(true));
    EXPECT_CALL(hwsec_, GetManufacturer())
        .WillRepeatedly(ReturnValue(0x43524f53));
    EXPECT_CALL(hwsec_, GetAuthValue(_, _))
        .WillRepeatedly(ReturnValue(brillo::SecureBlob()));
    EXPECT_CALL(hwsec_, SealWithCurrentUser(_, _, _))
        .WillRepeatedly(ReturnValue(brillo::Blob()));
    ON_CALL(hwsec_, PreloadSealedData(_))
        .WillByDefault(ReturnValue(std::nullopt));
    ON_CALL(hwsec_, UnsealWithCurrentUser(_, _, _))
        .WillByDefault(ReturnValue(brillo::SecureBlob()));
    EXPECT_CALL(hwsec_, GetPubkeyHash(_))
        .WillRepeatedly(ReturnValue(brillo::Blob()));
    EXPECT_CALL(pinweaver_, IsEnabled()).WillRepeatedly(ReturnValue(true));
    EXPECT_CALL(pinweaver_, GetVersion()).WillRepeatedly(ReturnValue(2));
    EXPECT_CALL(pinweaver_, BlockGeneratePk())
        .WillRepeatedly(ReturnOk<TPMError>());
  }

  void CreateAuthSessionManager(AuthBlockUtility* auth_block_utility) {
    auth_session_manager_ =
        std::make_unique<AuthSessionManager>(AuthSession::BackingApis{
            &crypto_, &platform_, &user_session_map_, &keyset_management_,
            auth_block_utility, &auth_factor_driver_manager_,
            &auth_factor_manager_, &uss_storage_, &features_.async});
    userdataauth_.set_auth_session_manager(auth_session_manager_.get());
  }

 protected:
  // Accessors functions to avoid making each test a friend.

  CryptohomeStatus PrepareGuestVaultImpl() {
    return userdataauth_.PrepareGuestVaultImpl();
  }

  CryptohomeStatus PrepareEphemeralVaultImpl(
      const std::string& auth_session_id) {
    return userdataauth_.PrepareEphemeralVaultImpl(auth_session_id);
  }

  CryptohomeStatus PreparePersistentVaultImpl(
      const std::string& auth_session_id,
      const CryptohomeVault::Options& vault_options) {
    return userdataauth_.PreparePersistentVaultImpl(auth_session_id,
                                                    vault_options);
  }

  CryptohomeStatus CreatePersistentUserImpl(
      const std::string& auth_session_id) {
    return userdataauth_.CreatePersistentUserImpl(auth_session_id);
  }

  void GetAuthSessionStatusImpl(
      InUseAuthSession& auth_session,
      user_data_auth::GetAuthSessionStatusReply& reply) {
    userdataauth_.GetAuthSessionStatusImpl(auth_session, reply);
  }

  user_data_auth::ExtendAuthSessionReply ExtendAuthSession(
      const user_data_auth::ExtendAuthSessionRequest& request) {
    TestFuture<user_data_auth::ExtendAuthSessionReply> reply_future;
    userdataauth_.ExtendAuthSession(
        request,
        reply_future
            .GetCallback<const user_data_auth::ExtendAuthSessionReply&>());
    return reply_future.Get();
  }

  AuthSession* FindAuthSession(const std::string& serialized_token) {
    InUseAuthSession auth_session =
        auth_session_manager_->FindAuthSession(serialized_token);
    return auth_session.Get();
  }

  user_data_auth::StartAuthSessionReply StartAuthSession(
      user_data_auth::StartAuthSessionRequest start_session_req) {
    TestFuture<user_data_auth::StartAuthSessionReply> reply_future;
    userdataauth_.StartAuthSession(
        start_session_req,
        reply_future
            .GetCallback<const user_data_auth::StartAuthSessionReply&>());
    return reply_future.Get();
  }

  // Common functions for both interface and mock_auth_interface tests.

  std::string StartAuthenticatedAuthSession(const std::string& username,
                                            user_data_auth::AuthIntent intent) {
    user_data_auth::StartAuthSessionRequest start_session_req;
    start_session_req.mutable_account_id()->set_account_id(username);
    start_session_req.set_flags(
        user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE);
    start_session_req.set_intent(intent);
    user_data_auth::StartAuthSessionReply reply =
        StartAuthSession(start_session_req);

    EXPECT_EQ(reply.error(), user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
    std::optional<base::UnguessableToken> auth_session_id =
        AuthSession::GetTokenFromSerializedString(reply.auth_session_id());
    EXPECT_TRUE(auth_session_id.has_value());

    // Get the session into an authenticated state by treating it as if we just
    // freshly created the user.
    InUseAuthSession auth_session =
        userdataauth_.auth_session_manager_->FindAuthSession(
            auth_session_id.value());
    EXPECT_THAT(auth_session.AuthSessionStatus(), IsOk());
    EXPECT_THAT(auth_session->OnUserCreated(), IsOk());
    EXPECT_TRUE(auth_session->has_user_secret_stash());

    return auth_session->serialized_token();
  }

  user_data_auth::AddAuthFactorReply AddAuthFactor(
      const user_data_auth::AddAuthFactorRequest& request) {
    TestFuture<user_data_auth::AddAuthFactorReply> reply_future;
    userdataauth_.AddAuthFactor(
        request,
        reply_future.GetCallback<const user_data_auth::AddAuthFactorReply&>());
    return reply_future.Get();
  }

  user_data_auth::AddAuthFactorReply AddPasswordAuthFactor(
      const AuthSession& auth_session,
      const std::string& auth_factor_label,
      const std::string& password) {
    user_data_auth::AddAuthFactorRequest add_request;
    add_request.set_auth_session_id(auth_session.serialized_token());
    user_data_auth::AuthFactor& request_factor =
        *add_request.mutable_auth_factor();
    request_factor.set_type(user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
    request_factor.set_label(auth_factor_label);
    request_factor.mutable_password_metadata();
    add_request.mutable_auth_input()->mutable_password_input()->set_secret(
        password);
    return AddAuthFactor(add_request);
  }

  user_data_auth::AuthenticateAuthFactorReply AuthenticateAuthFactor(
      const user_data_auth::AuthenticateAuthFactorRequest& request) {
    TestFuture<user_data_auth::AuthenticateAuthFactorReply> reply_future;
    userdataauth_.AuthenticateAuthFactor(
        request,
        reply_future
            .GetCallback<const user_data_auth::AuthenticateAuthFactorReply&>());
    return reply_future.Get();
  }

  const Username kUsername{kUsernameString};
  const Username kUsername2{kUsername2String};
  const Username kUsername3{kUsername3String};

  TaskEnvironment task_environment_{
      TaskEnvironment::TimeSource::MOCK_TIME,
      TaskEnvironment::ThreadPoolExecutionMode::QUEUED};
  NiceMock<MockPlatform> platform_;
  UserSessionMap user_session_map_;
  NiceMock<MockHomeDirs> homedirs_;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager_;
  NiceMock<hwsec::MockCryptohomeFrontend> hwsec_;
  NiceMock<hwsec::MockPinWeaverFrontend> pinweaver_;
  NiceMock<hwsec::MockPinWeaverManagerFrontend> hwsec_pw_manager_;
  Crypto crypto_;
  UssStorage uss_storage_{&platform_};
  NiceMock<MockUserSessionFactory> user_session_factory_;
  std::unique_ptr<FingerprintAuthBlockService> fp_service_{
      FingerprintAuthBlockService::MakeNullService()};
  AuthFactorDriverManager auth_factor_driver_manager_{
      &platform_,
      &crypto_,
      &uss_storage_,
      AsyncInitPtr<ChallengeCredentialsHelper>(nullptr),
      nullptr,
      fp_service_.get(),
      AsyncInitPtr<BiometricsAuthBlockService>(nullptr)};
  AuthFactorManager auth_factor_manager_{&platform_};
  NiceMock<MockKeysetManagement> keyset_management_;
  NiceMock<MockPkcs11TokenFactory> pkcs11_token_factory_;
  NiceMock<MockUserOldestActivityTimestampManager>
      user_activity_timestamp_manager_;
  NiceMock<MockInstallAttributes> install_attrs_;
  std::unique_ptr<AuthSessionManager> auth_session_manager_;
  UserDataAuth userdataauth_;
  std::unique_ptr<AuthBlockUtilityImpl> auth_block_utility_impl_;
  FakeFeaturesForTesting features_;
};

class AuthSessionInterfaceTest : public AuthSessionInterfaceTestBase {
 protected:
  AuthSessionInterfaceTest() {
    CreateAuthSessionManager(auth_block_utility_impl_.get());
  }

  AuthorizationRequest CreateAuthorization(const std::string& secret) {
    AuthorizationRequest req;
    req.mutable_key()->set_secret(secret);
    req.mutable_key()->mutable_data()->set_label("test-label");
    req.mutable_key()->mutable_data()->set_type(KeyData::KEY_TYPE_PASSWORD);
    return req;
  }

  void ExpectAuth(const Username& username, const brillo::SecureBlob& secret) {
    auto vk = std::make_unique<VaultKeyset>();
    EXPECT_CALL(keyset_management_, GetValidKeyset(_, _, _))
        .WillOnce(Return(ByMove(std::move(vk))));
    ON_CALL(platform_, DirectoryExists(UserPath(SanitizeUserName(username))))
        .WillByDefault(Return(true));
  }
};

namespace {

TEST_F(AuthSessionInterfaceTest,
       PrepareEphemeralVaultWithNonEphemeralAuthSession) {
  MockOwnerUser("whoever", homedirs_);
  std::string serialized_token;
  // Auth session is initially not authenticated.
  {
    CryptohomeStatusOr<InUseAuthSession> auth_session_status =
        auth_session_manager_->CreateAuthSession(kUsername, 0,
                                                 AuthIntent::kDecrypt);
    EXPECT_THAT(auth_session_status, IsOk());
    AuthSession* auth_session = auth_session_status.value().Get();
    EXPECT_THAT(auth_session->authorized_intents(), IsEmpty());
    serialized_token = auth_session->serialized_token();
  }

  // User authed and exists.
  auto user_session = std::make_unique<MockUserSession>();
  CryptohomeStatus status = PrepareEphemeralVaultImpl(serialized_token);
  EXPECT_THAT(status, NotOk());
  ASSERT_EQ(status->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
}

// Test if PreparePersistentVaultImpl can succeed with invalid authSession. It
// should not.
TEST_F(AuthSessionInterfaceTest, PreparePersistentVaultWithInvalidAuthSession) {
  // No auth session.

  CryptohomeStatus status =
      PreparePersistentVaultImpl(/*auth_session_id=*/"", /*vault_options=*/{});
  EXPECT_THAT(status, NotOk());
  ASSERT_EQ(status->local_legacy_error(),
            user_data_auth::CRYPTOHOME_INVALID_AUTH_SESSION_TOKEN);
}

// Test for checking if PreparePersistentVaultImpl will proceed when given the
// broadcast ID of a session.
TEST_F(AuthSessionInterfaceTest, PreparePersistentVaultWithBroadcastId) {
  std::string serialized_token;
  {
    CryptohomeStatusOr<InUseAuthSession> auth_session_status =
        auth_session_manager_->CreateAuthSession(kUsername, 0,
                                                 AuthIntent::kDecrypt);
    EXPECT_THAT(auth_session_status, IsOk());
    AuthSession* auth_session = auth_session_status.value().Get();
    serialized_token = auth_session->serialized_public_token();
  }

  CryptohomeStatus status = PreparePersistentVaultImpl(serialized_token, {});
  EXPECT_THAT(status, NotOk());
  ASSERT_EQ(status->local_legacy_error(),
            user_data_auth::CRYPTOHOME_INVALID_AUTH_SESSION_TOKEN);
}

// Test for checking if PreparePersistentVaultImpl will proceed with
// unauthenticated auth session.
TEST_F(AuthSessionInterfaceTest,
       PreparePersistentVaultWithUnAuthenticatedAuthSession) {
  std::string serialized_token;
  {
    CryptohomeStatusOr<InUseAuthSession> auth_session_status =
        auth_session_manager_->CreateAuthSession(kUsername, 0,
                                                 AuthIntent::kDecrypt);
    EXPECT_THAT(auth_session_status, IsOk());
    AuthSession* auth_session = auth_session_status.value().Get();
    serialized_token = auth_session->serialized_token();
  }

  CryptohomeStatus status = PreparePersistentVaultImpl(serialized_token, {});
  EXPECT_THAT(status, NotOk());
  ASSERT_EQ(status->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
}

// Test for checking if PreparePersistentVaultImpl will proceed with
// ephemeral auth session.
TEST_F(AuthSessionInterfaceTest,
       PreparePersistentVaultWithEphemeralAuthSession) {
  std::string serialized_token;
  {
    CryptohomeStatusOr<InUseAuthSession> auth_session_status =
        auth_session_manager_->CreateAuthSession(
            kUsername, AUTH_SESSION_FLAGS_EPHEMERAL_USER, AuthIntent::kDecrypt);
    EXPECT_THAT(auth_session_status, IsOk());
    AuthSession* auth_session = auth_session_status.value().Get();
    serialized_token = auth_session->serialized_token();
  }

  CryptohomeStatus status = PreparePersistentVaultImpl(serialized_token, {});
  EXPECT_THAT(status, NotOk());
  ASSERT_EQ(status->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
}

// Test to check if PreparePersistentVaultImpl will succeed if user is not
// created.
TEST_F(AuthSessionInterfaceTest, PreparePersistentVaultNoShadowDir) {
  std::string serialized_token;
  {
    CryptohomeStatusOr<InUseAuthSession> auth_session_status =
        auth_session_manager_->CreateAuthSession(kUsername, 0,
                                                 AuthIntent::kDecrypt);
    EXPECT_THAT(auth_session_status, IsOk());
    AuthSession* auth_session = auth_session_status.value().Get();

    // Say that the user was created and the session is authenticated, without
    // actually creating the user.
    EXPECT_THAT(auth_session->OnUserCreated(), IsOk());
    serialized_token = auth_session->serialized_token();
  }

  // If no shadow homedir - we do not have a user.
  EXPECT_CALL(homedirs_, Exists(SanitizeUserName(kUsername)))
      .WillRepeatedly(Return(false));

  CryptohomeStatus status = PreparePersistentVaultImpl(serialized_token, {});

  EXPECT_THAT(status, NotOk());
  ASSERT_EQ(status->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND);
}

// Test CreatePersistentUserImpl with invalid auth_session.
TEST_F(AuthSessionInterfaceTest, CreatePersistentUserInvalidAuthSession) {
  // No auth session.

  ASSERT_THAT(CreatePersistentUserImpl("")->local_legacy_error().value(),
              Eq(user_data_auth::CRYPTOHOME_INVALID_AUTH_SESSION_TOKEN));
}

// Test CreatePersistentUserImpl fails when a forbidden auth_session token
// (all-zeroes) is specified.
TEST_F(AuthSessionInterfaceTest,
       CreatePersistentUserInvalidAllZeroesAuthSession) {
  std::string all_zeroes_token;
  {
    // Setup. To avoid hardcoding the length of the string in the test, first
    // serialize an arbitrary token and then replace its contents with zeroes.
    CryptohomeStatusOr<InUseAuthSession> auth_session_status =
        auth_session_manager_->CreateAuthSession(kUsername, 0,
                                                 AuthIntent::kDecrypt);
    ASSERT_THAT(auth_session_status, IsOk());
    all_zeroes_token = std::string(
        auth_session_status.value()->serialized_token().length(), '\0');
  }
  // Test.
  CryptohomeStatus status = CreatePersistentUserImpl(all_zeroes_token);

  // Verify.
  ASSERT_THAT(status, NotOk());
  EXPECT_EQ(status->local_legacy_error(),
            user_data_auth::CRYPTOHOME_INVALID_AUTH_SESSION_TOKEN);
}

// Test CreatePersistentUserImpl with valid auth_session but user fails to
// create.
TEST_F(AuthSessionInterfaceTest, CreatePersistentUserFailedCreate) {
  EXPECT_CALL(homedirs_, CryptohomeExists(SanitizeUserName(kUsername)))
      .WillOnce(ReturnValue(false));

  std::string serialized_token;
  {
    CryptohomeStatusOr<InUseAuthSession> auth_session_status =
        auth_session_manager_->CreateAuthSession(kUsername, 0,
                                                 AuthIntent::kDecrypt);
    EXPECT_THAT(auth_session_status, IsOk());
    AuthSession* auth_session = auth_session_status.value().Get();
    serialized_token = auth_session->serialized_token();
  }

  EXPECT_CALL(homedirs_, Exists(SanitizeUserName(kUsername)))
      .WillOnce(Return(false));
  EXPECT_CALL(homedirs_, Create(kUsername)).WillOnce(Return(false));
  auto status = CreatePersistentUserImpl(serialized_token);
  EXPECT_THAT(status, NotOk());
  ASSERT_THAT(status->local_legacy_error(),
              Eq(user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE));
}

// Test CreatePersistentUserImpl when Vault already exists.
TEST_F(AuthSessionInterfaceTest, CreatePersistentUserVaultExists) {
  std::string serialized_token;
  {
    CryptohomeStatusOr<InUseAuthSession> auth_session_status =
        auth_session_manager_->CreateAuthSession(kUsername, 0,
                                                 AuthIntent::kDecrypt);
    EXPECT_THAT(auth_session_status, IsOk());
    AuthSession* auth_session = auth_session_status.value().Get();
    serialized_token = auth_session->serialized_token();
  }

  EXPECT_CALL(homedirs_, CryptohomeExists(SanitizeUserName(kUsername)))
      .WillOnce(ReturnValue(true));
  ASSERT_THAT(
      CreatePersistentUserImpl(serialized_token)->local_legacy_error().value(),
      Eq(user_data_auth::CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY));
}

// Test CreatePersistentUserImpl with Ephemeral AuthSession.
TEST_F(AuthSessionInterfaceTest, CreatePersistentUserWithEphemeralAuthSession) {
  std::string serialized_token;
  {
    CryptohomeStatusOr<InUseAuthSession> auth_session_status =
        auth_session_manager_->CreateAuthSession(
            kUsername, AUTH_SESSION_FLAGS_EPHEMERAL_USER, AuthIntent::kDecrypt);
    EXPECT_THAT(auth_session_status, IsOk());
    AuthSession* auth_session = auth_session_status.value().Get();
    serialized_token = auth_session->serialized_token();
  }

  ASSERT_THAT(
      CreatePersistentUserImpl(serialized_token)->local_legacy_error().value(),
      Eq(user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
}

// Test CreatePersistentUserImpl with a session broadcast ID.
TEST_F(AuthSessionInterfaceTest, CreatePersistentUserWithBroadcastId) {
  std::string serialized_token;
  {
    CryptohomeStatusOr<InUseAuthSession> auth_session_status =
        auth_session_manager_->CreateAuthSession(kUsername, 0,
                                                 AuthIntent::kDecrypt);
    EXPECT_THAT(auth_session_status, IsOk());
    AuthSession* auth_session = auth_session_status.value().Get();
    serialized_token = auth_session->serialized_public_token();
  }

  ASSERT_THAT(
      CreatePersistentUserImpl(serialized_token)->local_legacy_error().value(),
      Eq(user_data_auth::CRYPTOHOME_INVALID_AUTH_SESSION_TOKEN));
}

TEST_F(AuthSessionInterfaceTest, GetAuthSessionStatus) {
  std::string auth_session_id;
  {
    CryptohomeStatusOr<InUseAuthSession> auth_session_status =
        auth_session_manager_->CreateAuthSession(kUsername, 0,
                                                 AuthIntent::kDecrypt);
    EXPECT_THAT(auth_session_status, IsOk());
    auth_session_id = auth_session_status.value()->serialized_token();
  }

  {
    user_data_auth::GetAuthSessionStatusRequest request;
    request.set_auth_session_id(auth_session_id);
    TestFuture<user_data_auth::GetAuthSessionStatusReply> reply_future;

    userdataauth_.GetAuthSessionStatus(
        request,
        reply_future
            .GetCallback<const user_data_auth::GetAuthSessionStatusReply&>());
    user_data_auth::GetAuthSessionStatusReply reply = reply_future.Get();

    // First verify that auth is required is the status.
    ASSERT_THAT(reply.auth_properties().authorized_for(), IsEmpty());
  }

  {
    {
      CryptohomeStatusOr<InUseAuthSession> auth_session_status =
          auth_session_manager_->FindAuthSession(auth_session_id);
      EXPECT_THAT(auth_session_status, IsOk());
      ASSERT_TRUE(auth_session_status.value()->OnUserCreated().ok());
    }
    user_data_auth::GetAuthSessionStatusRequest request;
    request.set_auth_session_id(auth_session_id);
    TestFuture<user_data_auth::GetAuthSessionStatusReply> reply_future;

    userdataauth_.GetAuthSessionStatus(
        request,
        reply_future
            .GetCallback<const user_data_auth::GetAuthSessionStatusReply&>());
    user_data_auth::GetAuthSessionStatusReply reply = reply_future.Get();

    // Then create the user which should authenticate the session.
    ASSERT_THAT(
        reply.auth_properties().authorized_for(),
        UnorderedElementsAre(AUTH_INTENT_DECRYPT, AUTH_INTENT_VERIFY_ONLY));
  }

  // Finally move time forward to time out the session.
  task_environment_.FastForwardBy(base::Minutes(5));
  {
    user_data_auth::GetAuthSessionStatusRequest request;
    request.set_auth_session_id(auth_session_id);
    TestFuture<user_data_auth::GetAuthSessionStatusReply> reply_future;

    userdataauth_.GetAuthSessionStatus(
        request,
        reply_future
            .GetCallback<const user_data_auth::GetAuthSessionStatusReply&>());
    user_data_auth::GetAuthSessionStatusReply reply = reply_future.Get();

    // First verify that auth is required is the status.
    ASSERT_EQ(reply.error(),
              user_data_auth::CRYPTOHOME_INVALID_AUTH_SESSION_TOKEN);
  }
}

TEST_F(AuthSessionInterfaceTest, GetHibernateSecretUnauthenticatedTest) {
  CryptohomeStatusOr<InUseAuthSession> auth_session_status =
      auth_session_manager_->CreateAuthSession(kUsername, 0,
                                               AuthIntent::kDecrypt);
  EXPECT_THAT(auth_session_status, IsOk());
  AuthSession* auth_session = auth_session_status.value().Get();

  // Verify an unauthenticated session fails in producing a hibernate secret.
  user_data_auth::GetHibernateSecretRequest request;
  request.set_auth_session_id(auth_session->serialized_token());
  user_data_auth::GetHibernateSecretReply hs_reply =
      userdataauth_.GetHibernateSecret(request);
  ASSERT_NE(hs_reply.error(), user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  ASSERT_FALSE(hs_reply.hibernate_secret().length());
}

TEST_F(AuthSessionInterfaceTest, ExtendAuthSessionDefaultValue) {
  // Setup.
  std::string token;
  {
    CryptohomeStatusOr<InUseAuthSession> auth_session_status =
        auth_session_manager_->CreateAuthSession(kUsername, 0,
                                                 AuthIntent::kDecrypt);
    EXPECT_THAT(auth_session_status, IsOk());

    // Get the session into an authenticated state by treating it as if we just
    // freshly created the user.
    // Then create the user which should authenticate the session.
    ASSERT_TRUE(auth_session_status.value()->OnUserCreated().ok());
    token = auth_session_status.value().Get()->serialized_token();
  }
  // Fast forward by four minutes and thirty seconds to see effect of default
  // value.
  task_environment_.FastForwardBy(base::Seconds(270));

  // Test 0 value.
  {
    user_data_auth::ExtendAuthSessionRequest ext_auth_session_req;
    ext_auth_session_req.set_auth_session_id(token);
    ext_auth_session_req.set_extension_duration(0);

    // Extend the AuthSession.
    user_data_auth::ExtendAuthSessionReply reply =
        ExtendAuthSession(ext_auth_session_req);
    EXPECT_EQ(reply.error(), user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
    EXPECT_TRUE(reply.has_seconds_left());
    EXPECT_LE(base::Seconds(reply.seconds_left()), kDefaultExtensionDuration);

    // Verify that timer has changed, within a resaonsable degree of error.
    InUseAuthSession auth_session =
        auth_session_manager_->FindAuthSession(token);
    EXPECT_THAT(
        auth_session.GetRemainingTime(),
        AllOf(Gt(base::TimeDelta(base::Seconds(30))), Le(base::Minutes(1))));
  }

  // Fast forward by thirty seconds to see effect of default value when no value
  // is set.
  task_environment_.FastForwardBy(base::Seconds(30));

  // Test no value.
  {
    user_data_auth::ExtendAuthSessionRequest ext_auth_session_req;
    ext_auth_session_req.set_auth_session_id(token);
    // The following line should be set, but for this test it is intentionally
    // ext_auth_session_req.set_extension_duration(0);

    // Extend the AuthSession.
    user_data_auth::ExtendAuthSessionReply reply =
        ExtendAuthSession(ext_auth_session_req);
    EXPECT_EQ(reply.error(), user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
    EXPECT_TRUE(reply.has_seconds_left());
    EXPECT_LE(base::Seconds(reply.seconds_left()), kDefaultExtensionDuration);

    // Verify that timer has changed, within a resaonsable degree of error.
    InUseAuthSession auth_session =
        auth_session_manager_->FindAuthSession(token);
    EXPECT_THAT(
        auth_session.GetRemainingTime(),
        AllOf(Gt(base::TimeDelta(base::Seconds(30))), Le(base::Minutes(1))));
  }
}

TEST_F(AuthSessionInterfaceTest, PrepareGuestVault) {
  // Setup a password user.
  auto user_session = std::make_unique<MockUserSession>();
  EXPECT_CALL(*user_session, IsActive()).WillRepeatedly(Return(true));
  EXPECT_CALL(*user_session, MountGuest()).WillOnce(Invoke([]() {
    return OkStatus<CryptohomeMountError>();
  }));
  EXPECT_CALL(user_session_factory_, New(_, _, _))
      .WillOnce(Return(ByMove(std::move(user_session))));
  EXPECT_THAT(PrepareGuestVaultImpl(), IsOk());

  // Trying to prepare another session should fail, whether it is guest, ...
  CryptohomeStatus status = PrepareGuestVaultImpl();
  EXPECT_THAT(status, NotOk());
  ASSERT_EQ(status->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_MOUNT_FATAL);

  // ... ephemeral, ...
  std::string serialized_token;
  {
    CryptohomeStatusOr<InUseAuthSession> auth_session_status =
        auth_session_manager_->CreateAuthSession(
            kUsername, AUTH_SESSION_FLAGS_EPHEMERAL_USER, AuthIntent::kDecrypt);
    EXPECT_THAT(auth_session_status, IsOk());
    AuthSession* auth_session = auth_session_status.value().Get();
    serialized_token = auth_session->serialized_token();
  }

  status = PrepareEphemeralVaultImpl(serialized_token);
  EXPECT_THAT(status, NotOk());
  ASSERT_EQ(status->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY);
  auth_session_manager_->RemoveAllAuthSessions();

  // ... or regular.
  serialized_token = StartAuthenticatedAuthSession(
      kUsername2String, user_data_auth::AuthIntent::AUTH_INTENT_DECRYPT);
  const ObfuscatedUsername obfuscated_username = SanitizeUserName(kUsername2);
  EXPECT_CALL(platform_, DirectoryExists(UserPath(obfuscated_username)))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(homedirs_, Exists(obfuscated_username))
      .WillRepeatedly(Return(true));
  status = PreparePersistentVaultImpl(serialized_token, {});
  EXPECT_THAT(status, NotOk());
  ASSERT_EQ(status->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY);
}

TEST_F(AuthSessionInterfaceTest, PrepareGuestVaultAfterFailedGuest) {
  auto user_session = std::make_unique<MockUserSession>();
  const CryptohomeError::ErrorLocationPair fake_error_location =
      CryptohomeError::ErrorLocationPair(
          static_cast<CryptohomeError::ErrorLocation>(1),
          std::string("FakeErrorLocation"));

  EXPECT_CALL(*user_session, IsActive()).WillRepeatedly(Return(false));
  EXPECT_CALL(*user_session, MountGuest()).WillOnce(Invoke([&]() {
    return MakeStatus<CryptohomeMountError>(
        fake_error_location,
        error::ErrorActionSet({error::PossibleAction::kReboot}),
        MOUNT_ERROR_FATAL, std::nullopt);
  }));

  auto user_session2 = std::make_unique<MockUserSession>();
  EXPECT_CALL(*user_session2, IsActive()).WillRepeatedly(Return(true));
  EXPECT_CALL(*user_session2, MountGuest()).WillOnce(Invoke([]() {
    return OkStatus<CryptohomeMountError>();
  }));

  EXPECT_CALL(user_session_factory_, New(_, _, _))
      .WillOnce(Return(ByMove(std::move(user_session))))
      .WillOnce(Return(ByMove(std::move(user_session2))));

  // We set first invocation to fail, but the second should succeed.
  ASSERT_THAT(PrepareGuestVaultImpl(), NotOk());
  ASSERT_THAT(PrepareGuestVaultImpl(), IsOk());
}

TEST_F(AuthSessionInterfaceTest, PrepareGuestVaultAfterFailedPersistent) {
  const ObfuscatedUsername obfuscated_username = SanitizeUserName(kUsername);

  // Arrange user created state.
  std::string serialized_token = StartAuthenticatedAuthSession(
      kUsernameString, user_data_auth::AuthIntent::AUTH_INTENT_DECRYPT);

  EXPECT_CALL(platform_, DirectoryExists(UserPath(obfuscated_username)))
      .WillRepeatedly(Return(true));

  // Arrange the vault operations: user exists, not active.
  auto user_session = std::make_unique<MockUserSession>();
  EXPECT_CALL(*user_session, IsActive()).WillRepeatedly(Return(false));
  const CryptohomeError::ErrorLocationPair fake_error_location =
      CryptohomeError::ErrorLocationPair(
          static_cast<CryptohomeError::ErrorLocation>(1),
          std::string("FakeErrorLocation"));
  EXPECT_CALL(*user_session, MountVault(kUsername, _, _))
      .WillOnce(Invoke([&](const Username&, const FileSystemKeyset&,
                           const CryptohomeVault::Options&) {
        return MakeStatus<CryptohomeMountError>(
            fake_error_location,
            error::ErrorActionSet({error::PossibleAction::kReboot}),
            MOUNT_ERROR_FATAL, std::nullopt);
      }));
  EXPECT_CALL(homedirs_, Exists(SanitizeUserName(kUsername)))
      .WillRepeatedly(Return(true));

  auto user_session2 = std::make_unique<MockUserSession>();
  EXPECT_CALL(*user_session2, IsActive()).WillRepeatedly(Return(true));
  EXPECT_CALL(*user_session2, MountGuest()).WillOnce(Invoke([]() {
    return OkStatus<CryptohomeMountError>();
  }));

  EXPECT_CALL(user_session_factory_, New(_, _, _))
      .WillOnce(Return(ByMove(std::move(user_session))))
      .WillOnce(Return(ByMove(std::move(user_session2))));
  ASSERT_THAT(PreparePersistentVaultImpl(serialized_token, {}), NotOk());
  ASSERT_THAT(PrepareGuestVaultImpl(), IsOk());
}

TEST_F(AuthSessionInterfaceTest, PrepareGuestVaultAfterFailedEphemeral) {
  // Auth session is initially not authenticated for ephemeral users.
  std::string serialized_token;
  {
    CryptohomeStatusOr<InUseAuthSession> auth_session_status =
        auth_session_manager_->CreateAuthSession(
            kUsername, AUTH_SESSION_FLAGS_EPHEMERAL_USER, AuthIntent::kDecrypt);
    EXPECT_TRUE(auth_session_status.ok());
    AuthSession* auth_session = auth_session_status.value().Get();
    serialized_token = auth_session->serialized_token();
  }

  auto user_session = std::make_unique<MockUserSession>();
  const CryptohomeError::ErrorLocationPair fake_error_location =
      CryptohomeError::ErrorLocationPair(
          static_cast<CryptohomeError::ErrorLocation>(1),
          std::string("FakeErrorLocation"));
  EXPECT_CALL(*user_session, IsActive())
      .WillOnce(Return(false))
      .WillOnce(Return(false));
  EXPECT_CALL(*user_session, MountEphemeral(kUsername))
      .WillOnce(Invoke([&](const Username&) {
        return MakeStatus<CryptohomeMountError>(
            fake_error_location,
            error::ErrorActionSet({error::PossibleAction::kReboot}),
            MOUNT_ERROR_FATAL, std::nullopt);
      }));

  auto user_session2 = std::make_unique<MockUserSession>();
  EXPECT_CALL(*user_session2, IsActive()).WillRepeatedly(Return(true));
  EXPECT_CALL(*user_session2, MountGuest()).WillOnce(Invoke([]() {
    return OkStatus<CryptohomeMountError>();
  }));

  EXPECT_CALL(user_session_factory_, New(_, _, _))
      .WillOnce(Return(ByMove(std::move(user_session))))
      .WillOnce(Return(ByMove(std::move(user_session2))));

  // We set first invocation to fail, but the second should succeed.
  ASSERT_THAT(PrepareEphemeralVaultImpl(serialized_token), NotOk());
  ASSERT_THAT(PrepareGuestVaultImpl(), IsOk());
}

TEST_F(AuthSessionInterfaceTest, PrepareEphemeralVault) {
  MockOwnerUser("whoever", homedirs_);

  // No auth session.
  CryptohomeStatus status = PrepareEphemeralVaultImpl("");
  EXPECT_THAT(status, NotOk());
  ASSERT_EQ(status->local_legacy_error(),
            user_data_auth::CRYPTOHOME_INVALID_AUTH_SESSION_TOKEN);

  // Auth session is initially not authenticated for ephemeral users.
  std::string serialized_token;
  std::string serialized_public_token;
  {
    CryptohomeStatusOr<InUseAuthSession> auth_session_status =
        auth_session_manager_->CreateAuthSession(
            kUsername, AUTH_SESSION_FLAGS_EPHEMERAL_USER, AuthIntent::kDecrypt);
    EXPECT_THAT(auth_session_status, IsOk());
    AuthSession* auth_session = auth_session_status.value().Get();
    EXPECT_THAT(auth_session->authorized_intents(), IsEmpty());
    serialized_token = auth_session->serialized_token();
    serialized_public_token = auth_session->serialized_public_token();
  }

  // Using the broadcast ID as the session ID should fail.
  status = PrepareEphemeralVaultImpl(serialized_public_token);
  EXPECT_THAT(status, NotOk());
  ASSERT_EQ(status->local_legacy_error(),
            user_data_auth::CRYPTOHOME_INVALID_AUTH_SESSION_TOKEN);

  // User authed and exists.
  auto user_session = std::make_unique<MockUserSession>();
  EXPECT_CALL(*user_session, IsActive())
      .WillOnce(Return(false))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*user_session, GetPkcs11Token()).WillRepeatedly(Return(nullptr));
  EXPECT_CALL(*user_session, IsEphemeral()).WillRepeatedly(Return(true));
  EXPECT_CALL(*user_session, MountEphemeral(kUsername))
      .WillOnce(ReturnError<CryptohomeMountError>());
  EXPECT_CALL(user_session_factory_, New(_, _, _))
      .WillOnce(Return(ByMove(std::move(user_session))));

  EXPECT_THAT(PrepareEphemeralVaultImpl(serialized_token), IsOk());
  {
    InUseAuthSession auth_session =
        auth_session_manager_->FindAuthSession(serialized_token);
    EXPECT_THAT(
        auth_session->authorized_intents(),
        UnorderedElementsAre(AuthIntent::kDecrypt, AuthIntent::kVerifyOnly));
    EXPECT_EQ(auth_session.GetRemainingTime(), kDefaultTimeAfterAuthenticate);
  }

  // Set up expectation for add credential callback success.
  user_data_auth::AddAuthFactorRequest request;
  request.set_auth_session_id(serialized_token);
  user_data_auth::AuthFactor& request_factor = *request.mutable_auth_factor();
  request_factor.set_type(user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
  request_factor.set_label(kPasswordLabel);
  request_factor.mutable_password_metadata();
  request.mutable_auth_input()->mutable_password_input()->set_secret(kPassword);

  user_data_auth::AddAuthFactorReply reply = AddAuthFactor(request);

  // Evaluate error returned by callback.
  ASSERT_THAT(reply.error(), Eq(user_data_auth::CRYPTOHOME_ERROR_NOT_SET));

  // Trying to mount again will yield busy.
  status = PrepareEphemeralVaultImpl(serialized_token);
  EXPECT_THAT(status, NotOk());
  ASSERT_EQ(status->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY);

  // Guest fails if other sessions present.
  status = PrepareGuestVaultImpl();
  EXPECT_THAT(status, NotOk());
  ASSERT_EQ(status->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_MOUNT_FATAL);

  // And so does ephemeral
  {
    CryptohomeStatusOr<InUseAuthSession> auth_session2_status =
        auth_session_manager_->CreateAuthSession(
            kUsername2, AUTH_SESSION_FLAGS_EPHEMERAL_USER,
            AuthIntent::kDecrypt);
    EXPECT_THAT(auth_session2_status, IsOk());
    AuthSession* auth_session2 = auth_session2_status.value().Get();
    serialized_token = auth_session2->serialized_token();
  }
  status = PrepareEphemeralVaultImpl(serialized_token);
  EXPECT_THAT(status, NotOk());
  ASSERT_EQ(status->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY);

  // But a different regular mount succeeds.
  const ObfuscatedUsername obfuscated_username = SanitizeUserName(kUsername3);
  serialized_token = StartAuthenticatedAuthSession(
      kUsername3String, user_data_auth::AuthIntent::AUTH_INTENT_DECRYPT);
  EXPECT_CALL(platform_, DirectoryExists(UserPath(obfuscated_username)))
      .WillRepeatedly(Return(true));

  auto user_session3 = std::make_unique<MockUserSession>();
  EXPECT_CALL(*user_session3, IsActive())
      .WillOnce(Return(false))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*user_session3, MountVault(kUsername3, _, _))
      .WillOnce(ReturnError<CryptohomeMountError>());
  EXPECT_CALL(user_session_factory_, New(_, _, _))
      .WillOnce(Return(ByMove(std::move(user_session3))));
  EXPECT_CALL(homedirs_, Exists(obfuscated_username))
      .WillRepeatedly(Return(true));

  EXPECT_THAT(PreparePersistentVaultImpl(serialized_token, {}), IsOk());
}

TEST_F(AuthSessionInterfaceTest, PreparePersistentVaultAndThenGuestFail) {
  const ObfuscatedUsername obfuscated_username = SanitizeUserName(kUsername);

  // Arrange.
  std::string serialized_token =
      StartAuthenticatedAuthSession(kUsernameString, AUTH_INTENT_DECRYPT);

  EXPECT_CALL(platform_, DirectoryExists(UserPath(obfuscated_username)))
      .WillRepeatedly(Return(true));

  // Arrange the vault operations.
  auto user_session = std::make_unique<MockUserSession>();
  EXPECT_CALL(*user_session, IsActive())
      .WillOnce(Return(false))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*user_session, MountVault(kUsername, _, _))
      .WillOnce(ReturnError<CryptohomeMountError>());
  EXPECT_CALL(user_session_factory_, New(kUsername, _, _))
      .WillOnce(Return(ByMove(std::move(user_session))));
  EXPECT_CALL(homedirs_, Exists(SanitizeUserName(kUsername)))
      .WillRepeatedly(Return(true));

  // User authed and exists.
  EXPECT_CALL(homedirs_, Exists(SanitizeUserName(kUsername)))
      .WillRepeatedly(Return(true));
  EXPECT_THAT(PreparePersistentVaultImpl(serialized_token, {}), IsOk());

  // Guest fails if other sessions present.
  auto status = PrepareGuestVaultImpl();
  EXPECT_THAT(status, NotOk());
  ASSERT_EQ(status->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_MOUNT_FATAL);
}

// Test that RemoveAuthFactor successfully removes the password factor with the
// given label.
TEST_F(AuthSessionInterfaceTest, RemoveAuthFactorSuccess) {
  // Arrange.
  std::string serialized_token =
      StartAuthenticatedAuthSession(kUsernameString, AUTH_INTENT_DECRYPT);
  AuthSession* auth_session = FindAuthSession(serialized_token);
  AddPasswordAuthFactor(*auth_session, kPasswordLabel, kPassword);
  AddPasswordAuthFactor(*auth_session, kPasswordLabel2, kPassword2);

  // Act.
  // Test that RemoveAuthFactor removes the password factor.
  user_data_auth::RemoveAuthFactorRequest remove_request;
  remove_request.set_auth_session_id(serialized_token);
  remove_request.set_auth_factor_label(kPasswordLabel);
  TestFuture<user_data_auth::RemoveAuthFactorReply> remove_reply_future;
  userdataauth_.RemoveAuthFactor(
      remove_request,
      remove_reply_future
          .GetCallback<const user_data_auth::RemoveAuthFactorReply&>());

  // Assert.
  EXPECT_EQ(remove_reply_future.Get().error(),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
}

// Test that RemoveAuthFactor returns failure from remove request for the wrong
// label.
TEST_F(AuthSessionInterfaceTest, RemoveAuthFactorFailsNonExitingLabel) {
  // Arrange.
  std::string serialized_token =
      StartAuthenticatedAuthSession(kUsernameString, AUTH_INTENT_DECRYPT);
  AuthSession* auth_session = FindAuthSession(serialized_token);
  AddPasswordAuthFactor(*auth_session, kPasswordLabel, kPassword);

  // Act.
  // Test that RemoveAuthFactor fails to remove the non-existing factor.
  user_data_auth::RemoveAuthFactorRequest remove_request;
  remove_request.set_auth_session_id(serialized_token);
  remove_request.set_auth_factor_label(kPasswordLabel2);
  TestFuture<user_data_auth::RemoveAuthFactorReply> remove_reply_future;
  userdataauth_.RemoveAuthFactor(
      remove_request,
      remove_reply_future
          .GetCallback<const user_data_auth::RemoveAuthFactorReply&>());

  // Assert.
  EXPECT_EQ(remove_reply_future.Get().error(),
            user_data_auth::CRYPTOHOME_ERROR_KEY_NOT_FOUND);
}

// Test that RemoveAuthFactor fails to remove the only factor.
TEST_F(AuthSessionInterfaceTest, RemoveAuthFactorFailsLastFactor) {
  // Arrange.
  std::string serialized_token =
      StartAuthenticatedAuthSession(kUsernameString, AUTH_INTENT_DECRYPT);
  AuthSession* auth_session = FindAuthSession(serialized_token);
  AddPasswordAuthFactor(*auth_session, kPasswordLabel, kPassword);

  // Act.
  // Test that RemoveAuthFactor fails to remove the non-existing VK.
  user_data_auth::RemoveAuthFactorRequest remove_request;
  remove_request.set_auth_session_id(serialized_token);
  remove_request.set_auth_factor_label(kPasswordLabel);
  TestFuture<user_data_auth::RemoveAuthFactorReply> remove_reply_future;
  userdataauth_.RemoveAuthFactor(
      remove_request,
      remove_reply_future
          .GetCallback<const user_data_auth::RemoveAuthFactorReply&>());

  // Assert.
  EXPECT_EQ(remove_reply_future.Get().error(),
            user_data_auth::CRYPTOHOME_REMOVE_CREDENTIALS_FAILED);
}

// Test that RemoveAuthFactor fails to remove the authenticated VaultKeyset.
TEST_F(AuthSessionInterfaceTest, RemoveAuthFactorFailsToRemoveSameFactor) {
  // Arrange.
  std::string serialized_token =
      StartAuthenticatedAuthSession(kUsernameString, AUTH_INTENT_DECRYPT);
  AuthSession* auth_session = FindAuthSession(serialized_token);
  AddPasswordAuthFactor(*auth_session, kPasswordLabel, kPassword);
  AddPasswordAuthFactor(*auth_session, kPasswordLabel2, kPassword2);

  // Act.
  user_data_auth::RemoveAuthFactorRequest remove_request;
  remove_request.set_auth_session_id(serialized_token);
  remove_request.set_auth_factor_label(kPasswordLabel);
  TestFuture<user_data_auth::RemoveAuthFactorReply> remove_reply_future;
  userdataauth_.RemoveAuthFactor(
      remove_request,
      remove_reply_future
          .GetCallback<const user_data_auth::RemoveAuthFactorReply&>());
  // Test that RemoveAuthFactor fails to remove the non-existing VK.
  user_data_auth::RemoveAuthFactorRequest remove_request2;
  remove_request2.set_auth_session_id(serialized_token);
  remove_request2.set_auth_factor_label(kPasswordLabel);
  TestFuture<user_data_auth::RemoveAuthFactorReply> remove_reply_future2;
  userdataauth_.RemoveAuthFactor(
      remove_request2,
      remove_reply_future2
          .GetCallback<const user_data_auth::RemoveAuthFactorReply&>());

  // Assert.
  EXPECT_EQ(remove_reply_future.Get().error(),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  EXPECT_EQ(remove_reply_future2.Get().error(),
            user_data_auth::CRYPTOHOME_ERROR_KEY_NOT_FOUND);
}

// Test the PreparePersistentVault, when called after a successful
// AuthenticateAuthFactor, mounts the home dir and sets up the user session.
TEST_F(AuthSessionInterfaceTest, PrepareVaultAfterFactorAuth) {
  const ObfuscatedUsername obfuscated_username = SanitizeUserName(kUsername);

  // Arrange.
  std::string serialized_token =
      StartAuthenticatedAuthSession(kUsernameString, AUTH_INTENT_DECRYPT);
  EXPECT_CALL(platform_, DirectoryExists(UserPath(obfuscated_username)))
      .WillRepeatedly(Return(true));

  // Mock user vault mounting. Use the real user session class in order to check
  // session state transitions.
  EXPECT_CALL(homedirs_, Exists(obfuscated_username))
      .WillRepeatedly(Return(true));
  auto mount = base::MakeRefCounted<MockMount>();
  EXPECT_CALL(*mount, IsMounted())
      .WillOnce(Return(false))
      .WillRepeatedly(Return(true));
  auto user_session = std::make_unique<RealUserSession>(
      kUsername, &homedirs_, &keyset_management_,
      &user_activity_timestamp_manager_, &pkcs11_token_factory_, mount);
  EXPECT_CALL(user_session_factory_, New(kUsername, _, _))
      .WillOnce(Return(ByMove(std::move(user_session))));

  // Act.
  CryptohomeStatus prepare_status =
      PreparePersistentVaultImpl(serialized_token, /*vault_options=*/{});

  // Assert.
  EXPECT_THAT(prepare_status, IsOk());
  UserSession* found_user_session =
      userdataauth_.FindUserSessionForTest(kUsername);
  ASSERT_TRUE(found_user_session);
  EXPECT_TRUE(found_user_session->IsActive());

  AuthInput auth_input = {.user_input = brillo::SecureBlob(kPassword),
                          .obfuscated_username = obfuscated_username};
}

// Test the PreparePersistentVault, when called after a successful
// AuthenticateAuthFactor, mounts the home dir and sets up the user session.
// Following that, second call should fail.
TEST_F(AuthSessionInterfaceTest, PrepareVaultAfterFactorAuthMountPointBusy) {
  const ObfuscatedUsername obfuscated_username = SanitizeUserName(kUsername);

  // Arrange.
  std::string serialized_token =
      StartAuthenticatedAuthSession(kUsernameString, AUTH_INTENT_DECRYPT);
  EXPECT_CALL(platform_, DirectoryExists(UserPath(obfuscated_username)))
      .WillRepeatedly(Return(true));

  // Mock user vault mounting. Use the real user session class in order to check
  // session state transitions.
  EXPECT_CALL(homedirs_, Exists(obfuscated_username))
      .WillRepeatedly(Return(true));
  auto mount = base::MakeRefCounted<MockMount>();
  EXPECT_CALL(*mount, IsMounted())
      .WillOnce(Return(false))
      .WillRepeatedly(Return(true));
  auto user_session = std::make_unique<RealUserSession>(
      kUsername, &homedirs_, &keyset_management_,
      &user_activity_timestamp_manager_, &pkcs11_token_factory_, mount);
  EXPECT_CALL(user_session_factory_, New(kUsername, _, _))
      .WillOnce(Return(ByMove(std::move(user_session))));

  // Act.
  CryptohomeStatus prepare_status =
      PreparePersistentVaultImpl(serialized_token, /*vault_options=*/{});

  // Assert.
  EXPECT_THAT(prepare_status, IsOk());
  UserSession* found_user_session =
      userdataauth_.FindUserSessionForTest(kUsername);
  ASSERT_TRUE(found_user_session);
  EXPECT_TRUE(found_user_session->IsActive());

  // Trying to mount again will yield busy.
  prepare_status = PreparePersistentVaultImpl(serialized_token,
                                              /*vault_options=*/{});
  EXPECT_THAT(prepare_status, NotOk());
  ASSERT_EQ(prepare_status->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY);
}

// Test the PreparePersistentVault, when called after a successful
// AuthenticateAuthFactor, mounts the home dir and sets up the user session.
// Following that, a call to prepare ephemeral mount should fail.
TEST_F(AuthSessionInterfaceTest, PreparePersistentVaultAndEphemeral) {
  const ObfuscatedUsername obfuscated_username = SanitizeUserName(kUsername);

  // Arrange.
  std::string serialized_token =
      StartAuthenticatedAuthSession(kUsernameString, AUTH_INTENT_DECRYPT);
  EXPECT_CALL(platform_, DirectoryExists(UserPath(obfuscated_username)))
      .WillRepeatedly(Return(true));
  // Mock user vault mounting. Use the real user session class in order to check
  // session state transitions.
  EXPECT_CALL(homedirs_, Exists(obfuscated_username))
      .WillRepeatedly(Return(true));
  auto mount = base::MakeRefCounted<MockMount>();
  EXPECT_CALL(*mount, IsMounted())
      .WillOnce(Return(false))
      .WillRepeatedly(Return(true));
  auto user_session = std::make_unique<RealUserSession>(
      kUsername, &homedirs_, &keyset_management_,
      &user_activity_timestamp_manager_, &pkcs11_token_factory_, mount);
  EXPECT_CALL(user_session_factory_, New(kUsername, _, _))
      .WillOnce(Return(ByMove(std::move(user_session))));

  // Act.
  CryptohomeStatus prepare_status =
      PreparePersistentVaultImpl(serialized_token, /*vault_options=*/{});

  // Assert.
  EXPECT_THAT(prepare_status, IsOk());
  UserSession* found_user_session =
      userdataauth_.FindUserSessionForTest(kUsername);
  ASSERT_TRUE(found_user_session);
  EXPECT_TRUE(found_user_session->IsActive());

  // Trying to mount again will yield busy.
  prepare_status = PrepareEphemeralVaultImpl(serialized_token);
  EXPECT_THAT(prepare_status, NotOk());
  ASSERT_EQ(prepare_status->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY);
}

}  // namespace

class AuthSessionInterfaceMockAuthTest : public AuthSessionInterfaceTestBase {
 protected:
  AuthSessionInterfaceMockAuthTest() {
    userdataauth_.set_auth_block_utility(&mock_auth_block_utility_);
    CreateAuthSessionManager(&mock_auth_block_utility_);
  }

  user_data_auth::AuthenticateAuthFactorReply
  LegacyAuthenticatePasswordAuthFactor(const AuthSession& auth_session,
                                       const std::string& auth_factor_label,
                                       const std::string& password) {
    user_data_auth::AuthenticateAuthFactorRequest request;
    request.set_auth_session_id(auth_session.serialized_token());
    request.set_auth_factor_label(auth_factor_label);
    request.mutable_auth_input()->mutable_password_input()->set_secret(
        password);
    return AuthenticateAuthFactor(request);
  }

  user_data_auth::AuthenticateAuthFactorReply AuthenticatePasswordAuthFactor(
      const AuthSession& auth_session,
      const std::string& auth_factor_label,
      const std::string& password) {
    user_data_auth::AuthenticateAuthFactorRequest request;
    request.set_auth_session_id(auth_session.serialized_token());
    request.add_auth_factor_labels(auth_factor_label);
    request.mutable_auth_input()->mutable_password_input()->set_secret(
        password);
    return AuthenticateAuthFactor(request);
  }

  // Simulates a new user creation flow by running `CreatePersistentUser` and
  // `PreparePersistentVault`. Sets up all necessary mocks. Returns an
  // authenticated AuthSession, or null on failure.
  AuthSession* CreateAndPrepareUserVault(const Username username) {
    ObfuscatedUsername obfuscated_username = SanitizeUserName(username);

    EXPECT_CALL(platform_, DirectoryExists(UserPath(obfuscated_username)))
        .WillRepeatedly(Return(false));

    std::string serialized_token;
    {
      CryptohomeStatusOr<InUseAuthSession> auth_session_status =
          auth_session_manager_->CreateAuthSession(username, /*flags=*/0,
                                                   AuthIntent::kDecrypt);
      EXPECT_THAT(auth_session_status, IsOk());
      AuthSession* auth_session = auth_session_status.value().Get();

      if (!auth_session)
        return nullptr;

      serialized_token = auth_session->serialized_token();
    }

    // Create the user.
    EXPECT_CALL(homedirs_, CryptohomeExists(obfuscated_username))
        .WillOnce(ReturnValue(false));
    EXPECT_CALL(homedirs_, Create(username)).WillRepeatedly(Return(true));
    EXPECT_THAT(CreatePersistentUserImpl(serialized_token), IsOk());

    // Prepare the user vault. Use the real user session class to exercise
    // internal state transitions.
    EXPECT_CALL(homedirs_, Exists(_)).WillRepeatedly(Return(true));
    auto mount = base::MakeRefCounted<MockMount>();
    EXPECT_CALL(*mount, IsMounted())
        .WillOnce(Return(false))
        .WillRepeatedly(Return(true));
    auto user_session = std::make_unique<RealUserSession>(
        username, &homedirs_, &keyset_management_,
        &user_activity_timestamp_manager_, &pkcs11_token_factory_, mount);
    EXPECT_CALL(user_session_factory_, New(username, _, _))
        .WillOnce(Return(ByMove(std::move(user_session))));
    EXPECT_THAT(PreparePersistentVaultImpl(serialized_token,
                                           /*vault_options=*/{}),
                IsOk());
    InUseAuthSession auth_session =
        auth_session_manager_->FindAuthSession(serialized_token);
    return auth_session.Get();
  }

  AuthSession* PrepareEphemeralUser() {
    std::string serialized_token;
    {
      CryptohomeStatusOr<InUseAuthSession> auth_session_status =
          auth_session_manager_->CreateAuthSession(
              kUsername, AUTH_SESSION_FLAGS_EPHEMERAL_USER,
              AuthIntent::kDecrypt);
      EXPECT_THAT(auth_session_status, IsOk());
      AuthSession* auth_session = auth_session_status.value().Get();
      if (!auth_session)
        return nullptr;
      serialized_token = auth_session->serialized_token();
    }

    // Set up mocks for the user session creation. Use the real user session
    // class to exercise internal state transitions.
    auto mount = base::MakeRefCounted<MockMount>();
    EXPECT_CALL(*mount, IsMounted())
        .WillOnce(Return(false))
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*mount, MountEphemeralCryptohome(kUsername))
        .WillOnce(ReturnOk<StorageError>());
    EXPECT_CALL(*mount, IsEphemeral()).WillRepeatedly(Return(true));
    auto user_session = std::make_unique<RealUserSession>(
        kUsername, &homedirs_, &keyset_management_,
        &user_activity_timestamp_manager_, &pkcs11_token_factory_, mount);
    EXPECT_CALL(user_session_factory_, New(kUsername, _, _))
        .WillOnce(Return(ByMove(std::move(user_session))));

    EXPECT_THAT(PrepareEphemeralVaultImpl(serialized_token), IsOk());
    InUseAuthSession auth_session =
        auth_session_manager_->FindAuthSession(serialized_token);
    return auth_session.Get();
  }

  FakeFeaturesForTesting features_;
  MockAuthBlockUtility mock_auth_block_utility_;
};

namespace {

TEST_F(AuthSessionInterfaceMockAuthTest,
       AuthenticateAuthFactorWithBroadcastId) {
  const ObfuscatedUsername obfuscated_username = SanitizeUserName(kUsername);

  // Arrange.
  std::string serialized_token =
      StartAuthenticatedAuthSession(kUsernameString, AUTH_INTENT_DECRYPT);
  AuthSession* auth_session = FindAuthSession(serialized_token);
  ASSERT_TRUE(auth_session);

  std::string serialized_public_token = auth_session->serialized_public_token();

  // Act.
  user_data_auth::AuthenticateAuthFactorRequest request;
  request.set_auth_session_id(serialized_public_token);
  request.mutable_auth_input()->mutable_password_input()->set_secret(kPassword);
  const user_data_auth::AuthenticateAuthFactorReply reply =
      AuthenticateAuthFactor(request);

  // Verify
  ASSERT_EQ(reply.error(),
            user_data_auth::CRYPTOHOME_INVALID_AUTH_SESSION_TOKEN);
  ASSERT_THAT(reply.authorized_for(), IsEmpty());
}

TEST_F(AuthSessionInterfaceMockAuthTest, AuthenticateAuthFactorNoLabel) {
  const ObfuscatedUsername obfuscated_username = SanitizeUserName(kUsername);

  // Arrange.
  // Auth session is initially not authenticated.
  std::string serialized_token;
  {
    CryptohomeStatusOr<InUseAuthSession> auth_session_status =
        auth_session_manager_->CreateAuthSession(kUsername, 0,
                                                 AuthIntent::kDecrypt);
    EXPECT_THAT(auth_session_status, IsOk());
    AuthSession* auth_session = auth_session_status.value().Get();
    EXPECT_THAT(auth_session->authorized_intents(), IsEmpty());
    serialized_token = auth_session->serialized_token();
  }

  // Act.
  user_data_auth::AuthenticateAuthFactorRequest request;
  request.set_auth_session_id(serialized_token);
  request.mutable_auth_input()->mutable_password_input()->set_secret(kPassword);
  const user_data_auth::AuthenticateAuthFactorReply reply =
      AuthenticateAuthFactor(request);

  // Verify
  ASSERT_NE(reply.error(), user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  ASSERT_THAT(reply.authorized_for(), IsEmpty());
}

TEST_F(AuthSessionInterfaceMockAuthTest, GetHibernateSecretTest) {
  const ObfuscatedUsername obfuscated_username = SanitizeUserName(kUsername);

  // Arrange.
  std::string serialized_token =
      StartAuthenticatedAuthSession(kUsernameString, AUTH_INTENT_DECRYPT);
  EXPECT_CALL(platform_, DirectoryExists(UserPath(obfuscated_username)))
      .WillRepeatedly(Return(true));

  user_data_auth::GetHibernateSecretRequest hs_request;
  hs_request.set_auth_session_id(serialized_token);
  user_data_auth::GetHibernateSecretReply hs_reply =
      userdataauth_.GetHibernateSecret(hs_request);

  // Assert.
  EXPECT_EQ(hs_reply.error(), user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  EXPECT_TRUE(hs_reply.hibernate_secret().length());
}

TEST_F(AuthSessionInterfaceMockAuthTest, GetHibernateSecretWithBroadcastId) {
  const ObfuscatedUsername obfuscated_username = SanitizeUserName(kUsername);

  // Arrange.
  std::string serialized_token =
      StartAuthenticatedAuthSession(kUsernameString, AUTH_INTENT_DECRYPT);
  AuthSession* auth_session = FindAuthSession(serialized_token);
  ASSERT_TRUE(auth_session);

  std::string serialized_public_token = auth_session->serialized_public_token();

  // Act.
  user_data_auth::GetHibernateSecretRequest hs_request;
  hs_request.set_auth_session_id(serialized_public_token);
  user_data_auth::GetHibernateSecretReply hs_reply =
      userdataauth_.GetHibernateSecret(hs_request);

  // Assert.
  EXPECT_EQ(hs_reply.error(),
            user_data_auth::CRYPTOHOME_INVALID_AUTH_SESSION_TOKEN);
}

// Test that AuthenticateAuthFactor succeeds using credential verifier based
// lightweight authentication when `AuthIntent::kVerifyOnly` is requested.
TEST_F(AuthSessionInterfaceMockAuthTest, AuthenticateAuthFactorLightweight) {
  const ObfuscatedUsername obfuscated_username = SanitizeUserName(kUsername);

  // Arrange. Set up a fake VK without authentication mocks.
  EXPECT_CALL(platform_, DirectoryExists(UserPath(obfuscated_username)))
      .WillRepeatedly(Return(true));
  const SerializedVaultKeyset serialized_vk =
      CreateFakePasswordVk(kPasswordLabel);
  MockVKToAuthFactorMapLoading(obfuscated_username, {serialized_vk},
                               keyset_management_);
  MockKeysetLoadingByLabel(obfuscated_username, serialized_vk,
                           keyset_management_);
  // Set up a user session with a mocked credential verifier.
  auto user_session = std::make_unique<MockUserSession>();
  EXPECT_CALL(*user_session, VerifyUser(SanitizeUserName(kUsername)))
      .WillOnce(Return(true));
  auto verifier = std::make_unique<MockCredentialVerifier>(
      AuthFactorType::kPassword, kPasswordLabel,
      AuthFactorMetadata{.metadata = auth_factor::PasswordMetadata()});
  EXPECT_CALL(*verifier, VerifySync(_)).WillOnce(ReturnOk<CryptohomeError>());
  user_session->AddCredentialVerifier(std::move(verifier));
  EXPECT_TRUE(user_session_map_.Add(kUsername, std::move(user_session)));

  // Create an AuthSession.
  std::string serialized_token;
  {
    CryptohomeStatusOr<InUseAuthSession> auth_session_status =
        auth_session_manager_->CreateAuthSession(kUsername, /*flags=*/0,
                                                 AuthIntent::kVerifyOnly);
    EXPECT_THAT(auth_session_status, IsOk());
    AuthSession* auth_session = auth_session_status.value().Get();

    ASSERT_TRUE(auth_session);
    serialized_token = auth_session->serialized_token();
  }

  // Act.
  user_data_auth::AuthenticateAuthFactorRequest request;
  request.set_auth_session_id(serialized_token);
  request.set_auth_factor_label(kPasswordLabel);
  request.mutable_auth_input()->mutable_password_input()->set_secret(kPassword);
  const user_data_auth::AuthenticateAuthFactorReply reply =
      AuthenticateAuthFactor(request);

  EXPECT_EQ(reply.error(), user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  EXPECT_FALSE(reply.auth_properties().has_seconds_left());
  EXPECT_FALSE(reply.has_seconds_left());
  EXPECT_THAT(reply.auth_properties().authorized_for(),
              UnorderedElementsAre(AUTH_INTENT_VERIFY_ONLY));
  EXPECT_THAT(reply.authorized_for(),
              UnorderedElementsAre(AUTH_INTENT_VERIFY_ONLY));
}

// Test that AuthenticateAuthFactor fails in case the AuthSession ID is missing.
TEST_F(AuthSessionInterfaceMockAuthTest, AuthenticateAuthFactorNoSessionId) {
  const ObfuscatedUsername obfuscated_username = SanitizeUserName(kUsername);

  // Arrange.
  EXPECT_CALL(platform_, DirectoryExists(UserPath(obfuscated_username)))
      .WillRepeatedly(Return(true));

  // Act. Omit setting `auth_session_id` in the `request`.
  user_data_auth::AuthenticateAuthFactorRequest request;
  request.set_auth_factor_label(kPasswordLabel);
  request.mutable_auth_input()->mutable_password_input()->set_secret(kPassword);
  const user_data_auth::AuthenticateAuthFactorReply reply =
      AuthenticateAuthFactor(request);

  // Assert.
  EXPECT_EQ(reply.error(),
            user_data_auth::CRYPTOHOME_INVALID_AUTH_SESSION_TOKEN);
  EXPECT_FALSE(reply.has_seconds_left());
  EXPECT_THAT(reply.authorized_for(), IsEmpty());
  EXPECT_EQ(userdataauth_.FindUserSessionForTest(kUsername), nullptr);
}

// Test that AuthenticateAuthFactor fails in case the AuthSession ID is invalid.
TEST_F(AuthSessionInterfaceMockAuthTest, AuthenticateAuthFactorBadSessionId) {
  const ObfuscatedUsername obfuscated_username = SanitizeUserName(kUsername);

  // Arrange.
  EXPECT_CALL(platform_, DirectoryExists(UserPath(obfuscated_username)))
      .WillRepeatedly(Return(false));

  // Act.
  user_data_auth::AuthenticateAuthFactorRequest request;
  request.set_auth_session_id("bad-session-id");
  request.set_auth_factor_label(kPasswordLabel);
  request.mutable_auth_input()->mutable_password_input()->set_secret(kPassword);
  const user_data_auth::AuthenticateAuthFactorReply reply =
      AuthenticateAuthFactor(request);

  // Assert.
  EXPECT_EQ(reply.error(),
            user_data_auth::CRYPTOHOME_INVALID_AUTH_SESSION_TOKEN);
  EXPECT_FALSE(reply.has_seconds_left());
  EXPECT_THAT(reply.authorized_for(), IsEmpty());
  EXPECT_EQ(userdataauth_.FindUserSessionForTest(kUsername), nullptr);
}

// Test that AuthenticateAuthFactor fails in case the AuthSession is expired.
TEST_F(AuthSessionInterfaceMockAuthTest, AuthenticateAuthFactorExpiredSession) {
  const ObfuscatedUsername obfuscated_username = SanitizeUserName(kUsername);

  // Arrange.
  EXPECT_CALL(platform_, DirectoryExists(UserPath(obfuscated_username)))
      .WillRepeatedly(Return(false));
  std::string auth_session_id;
  {
    CryptohomeStatusOr<InUseAuthSession> auth_session_status =
        auth_session_manager_->CreateAuthSession(kUsername, /*flags=*/0,
                                                 AuthIntent::kDecrypt);
    EXPECT_THAT(auth_session_status, IsOk());
    AuthSession* auth_session = auth_session_status.value().Get();

    ASSERT_TRUE(auth_session);
    auth_session_id = auth_session->serialized_token();
  }

  EXPECT_TRUE(auth_session_manager_->RemoveAuthSession(auth_session_id));

  // Act.
  user_data_auth::AuthenticateAuthFactorRequest request;
  request.set_auth_session_id(auth_session_id);
  request.set_auth_factor_label(kPasswordLabel);
  request.mutable_auth_input()->mutable_password_input()->set_secret(kPassword);
  const user_data_auth::AuthenticateAuthFactorReply reply =
      AuthenticateAuthFactor(request);

  // Assert.
  EXPECT_EQ(reply.error(),
            user_data_auth::CRYPTOHOME_INVALID_AUTH_SESSION_TOKEN);
  EXPECT_FALSE(reply.has_seconds_left());
  EXPECT_THAT(reply.authorized_for(), IsEmpty());
  EXPECT_EQ(userdataauth_.FindUserSessionForTest(kUsername), nullptr);
}

// Test that AuthenticateAuthFactor fails in case the user doesn't exist.
TEST_F(AuthSessionInterfaceMockAuthTest, AuthenticateAuthFactorNoUser) {
  const ObfuscatedUsername obfuscated_username = SanitizeUserName(kUsername);

  // Arrange.
  EXPECT_CALL(platform_, DirectoryExists(UserPath(obfuscated_username)))
      .WillRepeatedly(Return(false));
  std::string serialized_token;
  {
    CryptohomeStatusOr<InUseAuthSession> auth_session_status =
        auth_session_manager_->CreateAuthSession(kUsername, /*flags=*/0,
                                                 AuthIntent::kDecrypt);
    EXPECT_THAT(auth_session_status, IsOk());
    AuthSession* auth_session = auth_session_status.value().Get();

    ASSERT_TRUE(auth_session);

    serialized_token = auth_session->serialized_token();
  }

  // Act.
  user_data_auth::AuthenticateAuthFactorRequest request;
  request.set_auth_session_id(serialized_token);
  request.set_auth_factor_label(kPasswordLabel);
  request.mutable_auth_input()->mutable_password_input()->set_secret(kPassword);
  const user_data_auth::AuthenticateAuthFactorReply reply =
      AuthenticateAuthFactor(request);

  // Assert.
  EXPECT_EQ(reply.error(), user_data_auth::CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND);
  EXPECT_FALSE(reply.has_seconds_left());
  EXPECT_THAT(reply.authorized_for(), IsEmpty());
  EXPECT_EQ(userdataauth_.FindUserSessionForTest(kUsername), nullptr);
}

// Test that AuthenticateAuthFactor fails in case the user has no keys (because
// the user is just created). The AuthSession, however, stays authenticated.
TEST_F(AuthSessionInterfaceMockAuthTest, AuthenticateAuthFactorNoKeys) {
  const ObfuscatedUsername obfuscated_username = SanitizeUserName(kUsername);

  // Arrange.
  EXPECT_CALL(platform_, DirectoryExists(UserPath(obfuscated_username)))
      .WillRepeatedly(Return(false));
  std::string serialized_token;
  {
    CryptohomeStatusOr<InUseAuthSession> auth_session_status =
        auth_session_manager_->CreateAuthSession(kUsername, /*flags=*/0,
                                                 AuthIntent::kDecrypt);
    EXPECT_THAT(auth_session_status, IsOk());
    InUseAuthSession& auth_session = *auth_session_status;

    EXPECT_THAT(auth_session->OnUserCreated(), IsOk());
    EXPECT_THAT(
        auth_session->authorized_intents(),
        UnorderedElementsAre(AuthIntent::kDecrypt, AuthIntent::kVerifyOnly));
    EXPECT_EQ(auth_session.GetRemainingTime(), kDefaultTimeAfterAuthenticate);
    EXPECT_THAT(
        auth_session->authorized_intents(),
        UnorderedElementsAre(AuthIntent::kDecrypt, AuthIntent::kVerifyOnly));

    serialized_token = auth_session->serialized_token();
  }

  // Act.
  user_data_auth::AuthenticateAuthFactorRequest request;
  request.set_auth_session_id(serialized_token);
  request.set_auth_factor_label(kPasswordLabel);
  request.mutable_auth_input()->mutable_password_input()->set_secret(kPassword);
  const user_data_auth::AuthenticateAuthFactorReply reply =
      AuthenticateAuthFactor(request);

  // Assert.
  EXPECT_EQ(reply.error(), user_data_auth::CRYPTOHOME_ERROR_KEY_NOT_FOUND);
  EXPECT_THAT(
      reply.auth_properties().authorized_for(),
      UnorderedElementsAre(AUTH_INTENT_DECRYPT, AUTH_INTENT_VERIFY_ONLY));
  EXPECT_THAT(
      reply.authorized_for(),
      UnorderedElementsAre(AUTH_INTENT_DECRYPT, AUTH_INTENT_VERIFY_ONLY));
  EXPECT_EQ(userdataauth_.FindUserSessionForTest(kUsername), nullptr);
}

// Test that AuthenticateAuthFactor fails when no AuthInput is provided.
TEST_F(AuthSessionInterfaceMockAuthTest, AuthenticateAuthFactorNoInput) {
  const ObfuscatedUsername obfuscated_username = SanitizeUserName(kUsername);

  // Arrange.
  EXPECT_CALL(platform_, DirectoryExists(UserPath(obfuscated_username)))
      .WillRepeatedly(Return(true));
  const SerializedVaultKeyset serialized_vk =
      CreateFakePasswordVk(kPasswordLabel);
  MockVKToAuthFactorMapLoading(obfuscated_username, {serialized_vk},
                               keyset_management_);

  MockKeysetLoadingByLabel(obfuscated_username, serialized_vk,
                           keyset_management_);
  std::string serialized_token;
  {
    CryptohomeStatusOr<InUseAuthSession> auth_session_status =
        auth_session_manager_->CreateAuthSession(kUsername, /*flags=*/0,
                                                 AuthIntent::kDecrypt);
    EXPECT_THAT(auth_session_status, IsOk());
    AuthSession* auth_session = auth_session_status.value().Get();

    ASSERT_TRUE(auth_session);
    serialized_token = auth_session->serialized_token();
  }

  // Act. Omit setting `auth_input` in `request`.
  user_data_auth::AuthenticateAuthFactorRequest request;
  request.set_auth_session_id(serialized_token);
  request.set_auth_factor_label(kPasswordLabel);
  const user_data_auth::AuthenticateAuthFactorReply reply =
      AuthenticateAuthFactor(request);

  // Assert.
  EXPECT_EQ(reply.error(), user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
  EXPECT_FALSE(reply.has_seconds_left());
  EXPECT_THAT(reply.authorized_for(), IsEmpty());
  EXPECT_EQ(userdataauth_.FindUserSessionForTest(kUsername), nullptr);
}

// Test that AuthenticateAuthFactor fails when both |auth_factor_label| and
// |auth_factor_labels| are specified.
TEST_F(AuthSessionInterfaceMockAuthTest, AuthenticateAuthFactorLabelConflicts) {
  const ObfuscatedUsername obfuscated_username = SanitizeUserName(kUsername);

  // Arrange.
  EXPECT_CALL(platform_, DirectoryExists(UserPath(obfuscated_username)))
      .WillRepeatedly(Return(true));
  const SerializedVaultKeyset serialized_vk =
      CreateFakePasswordVk(kPasswordLabel);
  MockVKToAuthFactorMapLoading(obfuscated_username, {serialized_vk},
                               keyset_management_);

  MockKeysetLoadingByLabel(obfuscated_username, serialized_vk,
                           keyset_management_);

  std::string serialized_token;
  {
    CryptohomeStatusOr<InUseAuthSession> auth_session_status =
        auth_session_manager_->CreateAuthSession(kUsername, /*flags=*/0,
                                                 AuthIntent::kDecrypt);
    EXPECT_TRUE(auth_session_status.ok());
    AuthSession* auth_session = auth_session_status.value().Get();

    ASSERT_TRUE(auth_session);
    serialized_token = auth_session->serialized_token();
  }

  // Act.
  user_data_auth::AuthenticateAuthFactorRequest request;
  request.set_auth_session_id(serialized_token);
  request.mutable_auth_input()->mutable_password_input()->set_secret(kPassword);
  request.set_auth_factor_label(kPasswordLabel);
  request.add_auth_factor_labels(kPasswordLabel2);
  const user_data_auth::AuthenticateAuthFactorReply reply =
      AuthenticateAuthFactor(request);

  // Assert.
  EXPECT_EQ(reply.error(), user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
  EXPECT_FALSE(reply.has_seconds_left());
  EXPECT_THAT(reply.authorized_for(), IsEmpty());
  EXPECT_EQ(userdataauth_.FindUserSessionForTest(kUsername), nullptr);
}

// Test multi mount with two users.
TEST_F(AuthSessionInterfaceMockAuthTest, PreparePersistentVaultMultiMount) {
  ASSERT_THAT(CreateAndPrepareUserVault(kUsername), Not(IsNull()));
  ASSERT_THAT(CreateAndPrepareUserVault(kUsername2), Not(IsNull()));
}

// Test that AddAuthFactor succeeds for a freshly prepared ephemeral user.
TEST_F(AuthSessionInterfaceMockAuthTest,
       AddPasswordFactorAfterPrepareEphemeral) {
  // Arrange.
  // Pretend to have a different owner user, because otherwise the ephemeral
  // login is disallowed.
  MockOwnerUser("whoever", homedirs_);
  // Prepare the ephemeral vault, which should also create the session.
  AuthSession* const auth_session = PrepareEphemeralUser();
  ASSERT_TRUE(auth_session);
  UserSession* found_user_session =
      userdataauth_.FindUserSessionForTest(kUsername);
  ASSERT_TRUE(found_user_session);
  EXPECT_TRUE(found_user_session->IsActive());
  EXPECT_THAT(found_user_session->GetCredentialVerifiers(), IsEmpty());

  // Act.
  user_data_auth::AddAuthFactorReply reply =
      AddPasswordAuthFactor(*auth_session, kPasswordLabel, kPassword);

  // Assert.
  EXPECT_EQ(reply.error(), user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  EXPECT_TRUE(reply.has_added_auth_factor());
  EXPECT_EQ(reply.added_auth_factor().auth_factor().label(), kPasswordLabel);
  EXPECT_THAT(reply.added_auth_factor().available_for_intents(),
              UnorderedElementsAre(user_data_auth::AUTH_INTENT_VERIFY_ONLY));
  EXPECT_TRUE(reply.added_auth_factor().auth_factor().has_password_metadata());
  // Check the user session has a verifier for the given password.
  const CredentialVerifier* verifier =
      found_user_session->FindCredentialVerifier(kPasswordLabel);
  ASSERT_THAT(verifier, NotNull());
  AuthInput auth_input = {.user_input = brillo::SecureBlob(kPassword),
                          .obfuscated_username = SanitizeUserName(kUsername)};
  EXPECT_TRUE(verifier->Verify(auth_input));
  EXPECT_THAT(
      auth_session->authorized_intents(),
      UnorderedElementsAre(AuthIntent::kDecrypt, AuthIntent::kVerifyOnly));
}

// Test that AuthenticateAuthFactor succeeds for a freshly prepared ephemeral
// user who has a password added.
TEST_F(AuthSessionInterfaceMockAuthTest,
       AuthenticatePasswordFactorForEphemeral) {
  // Arrange.
  // Pretend to have a different owner user, because otherwise the ephemeral
  // login is disallowed.
  MockOwnerUser("whoever", homedirs_);
  AuthSession* const first_auth_session = PrepareEphemeralUser();
  ASSERT_TRUE(first_auth_session);
  user_data_auth::AddAuthFactorReply add_reply =
      AddPasswordAuthFactor(*first_auth_session, kPasswordLabel, kPassword);

  EXPECT_EQ(add_reply.error(), user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  EXPECT_TRUE(add_reply.has_added_auth_factor());
  EXPECT_EQ(add_reply.added_auth_factor().auth_factor().label(),
            kPasswordLabel);
  EXPECT_THAT(add_reply.added_auth_factor().available_for_intents(),
              UnorderedElementsAre(user_data_auth::AUTH_INTENT_VERIFY_ONLY));
  EXPECT_TRUE(
      add_reply.added_auth_factor().auth_factor().has_password_metadata());

  // Act.
  AuthSession* second_auth_session;
  {
    CryptohomeStatusOr<InUseAuthSession> auth_session_status =
        auth_session_manager_->CreateAuthSession(
            kUsername, AUTH_SESSION_FLAGS_EPHEMERAL_USER,
            AuthIntent::kVerifyOnly);
    EXPECT_THAT(auth_session_status, IsOk());
    second_auth_session = auth_session_status.value().Get();
    ASSERT_TRUE(second_auth_session);
  }

  user_data_auth::AuthenticateAuthFactorReply reply =
      AuthenticatePasswordAuthFactor(*second_auth_session, kPasswordLabel,
                                     kPassword);

  // Assert.
  EXPECT_EQ(reply.error(), user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  EXPECT_THAT(second_auth_session->authorized_intents(),
              UnorderedElementsAre(AuthIntent::kVerifyOnly));
}

// Test that AuthenticateAuthFactor succeeds for a freshly prepared ephemeral
// user who has a password added. Test the same functionality as
// AuthenticatePassworFactorForEphermeral. Use a different helper method to
// construct the request with legacy |auth_factor_label| to ensure backward
// compatibility.
TEST_F(AuthSessionInterfaceMockAuthTest,
       LegacyAuthenticatePasswordFactorForEphemeral) {
  // Arrange.
  // Pretend to have a different owner user, because otherwise the ephemeral
  // login is disallowed.
  MockOwnerUser("whoever", homedirs_);
  AuthSession* const first_auth_session = PrepareEphemeralUser();
  ASSERT_TRUE(first_auth_session);
  auto add_reply =
      AddPasswordAuthFactor(*first_auth_session, kPasswordLabel, kPassword);

  EXPECT_EQ(add_reply.error(), user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  EXPECT_TRUE(add_reply.has_added_auth_factor());
  EXPECT_EQ(add_reply.added_auth_factor().auth_factor().label(),
            kPasswordLabel);
  EXPECT_THAT(add_reply.added_auth_factor().available_for_intents(),
              UnorderedElementsAre(user_data_auth::AUTH_INTENT_VERIFY_ONLY));
  EXPECT_TRUE(
      add_reply.added_auth_factor().auth_factor().has_password_metadata());

  // Act.
  AuthSession* second_auth_session;
  {
    CryptohomeStatusOr<InUseAuthSession> auth_session_status =
        auth_session_manager_->CreateAuthSession(
            kUsername, AUTH_SESSION_FLAGS_EPHEMERAL_USER,
            AuthIntent::kVerifyOnly);
    EXPECT_TRUE(auth_session_status.ok());
    second_auth_session = auth_session_status.value().Get();
    ASSERT_TRUE(second_auth_session);
  }
  user_data_auth::AuthenticateAuthFactorReply reply =
      LegacyAuthenticatePasswordAuthFactor(*second_auth_session, kPasswordLabel,
                                           kPassword);

  // Assert.
  EXPECT_EQ(reply.error(), user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  EXPECT_THAT(second_auth_session->authorized_intents(),
              UnorderedElementsAre(AuthIntent::kVerifyOnly));
}

// Test that AuthenticateAuthFactor fails for a freshly prepared ephemeral user
// if a wrong password is provided.
TEST_F(AuthSessionInterfaceMockAuthTest,
       AuthenticatePasswordFactorForEphemeralWrongPassword) {
  // Arrange.
  // Pretend to have a different owner user, because otherwise the ephemeral
  // login is disallowed.
  MockOwnerUser("whoever", homedirs_);
  // Prepare the ephemeral user with a password configured.
  AuthSession* const first_auth_session = PrepareEphemeralUser();
  ASSERT_TRUE(first_auth_session);
  EXPECT_EQ(
      AddPasswordAuthFactor(*first_auth_session, kPasswordLabel, kPassword)
          .error(),
      user_data_auth::CRYPTOHOME_ERROR_NOT_SET);

  // Act.
  AuthSession* second_auth_session;
  {
    CryptohomeStatusOr<InUseAuthSession> auth_session_status =
        auth_session_manager_->CreateAuthSession(
            kUsername, AUTH_SESSION_FLAGS_EPHEMERAL_USER,
            AuthIntent::kVerifyOnly);
    EXPECT_THAT(auth_session_status, IsOk());
    second_auth_session = auth_session_status.value().Get();
    ASSERT_TRUE(second_auth_session);
  }
  user_data_auth::AuthenticateAuthFactorReply reply =
      AuthenticatePasswordAuthFactor(*second_auth_session, kPasswordLabel,
                                     kPassword2);

  // Assert.
  EXPECT_EQ(reply.error(),
            user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED);
  EXPECT_THAT(second_auth_session->authorized_intents(), IsEmpty());
}

// Test that AuthenticateAuthFactor fails for a freshly prepared ephemeral user
// if no password was configured.
TEST_F(AuthSessionInterfaceMockAuthTest,
       AuthenticatePasswordFactorForEphemeralNoPassword) {
  // Arrange.
  // Pretend to have a different owner user, because otherwise the ephemeral
  // login is disallowed.
  MockOwnerUser("whoever", homedirs_);
  // Prepare the ephemeral user without any factor configured.
  EXPECT_TRUE(PrepareEphemeralUser());

  // Act.
  AuthSession* auth_session;
  {
    CryptohomeStatusOr<InUseAuthSession> auth_session_status =
        auth_session_manager_->CreateAuthSession(
            kUsername, AUTH_SESSION_FLAGS_EPHEMERAL_USER,
            AuthIntent::kVerifyOnly);
    EXPECT_THAT(auth_session_status, IsOk());
    auth_session = auth_session_status.value().Get();
    ASSERT_TRUE(auth_session);
  }
  user_data_auth::AuthenticateAuthFactorReply reply =
      AuthenticatePasswordAuthFactor(*auth_session, kPasswordLabel, kPassword);

  // Assert. The error code is such because AuthSession falls back to checking
  // persistent auth factors.
  EXPECT_EQ(reply.error(), user_data_auth::CRYPTOHOME_ERROR_KEY_NOT_FOUND);
  EXPECT_THAT(auth_session->authorized_intents(), IsEmpty());
}

// Test that AuthenticateAuthFactor succeeds for an existing user and a
// VautKeyset-based factor when using the correct credential, and that the
// WebAuthn secret is prepared when `AuthIntent::kWebAuthn` is requested.
TEST_F(AuthSessionInterfaceMockAuthTest, AuthenticateAuthFactorWebAuthnIntent) {
  const ObfuscatedUsername obfuscated_username = SanitizeUserName(kUsername);
  const brillo::SecureBlob kBlob32{std::string(32, 'A')};
  const brillo::SecureBlob kBlob16{std::string(16, 'C')};
  const KeyBlobs kKeyBlobs{
      .vkk_key = kBlob32, .vkk_iv = kBlob16, .chaps_iv = kBlob16};
  const TpmEccAuthBlockState kTpmState = {
      .salt = brillo::SecureBlob(kSalt),
      .vkk_iv = kBlob32,
      .auth_value_rounds = kAuthValueRounds,
      .sealed_hvkkm = kBlob32,
      .extended_sealed_hvkkm = kBlob32,
      .tpm_public_key_hash = brillo::SecureBlob(kPublicHash),
  };
  // Arrange.
  std::string serialized_token = StartAuthenticatedAuthSession(
      kUsernameString, user_data_auth::AUTH_INTENT_WEBAUTHN);
  auto user_session = std::make_unique<MockUserSession>();
  EXPECT_CALL(*user_session, PrepareWebAuthnSecret(_, _));
  EXPECT_TRUE(user_session_map_.Add(kUsername, std::move(user_session)));
  AuthSession* auth_session = FindAuthSession(serialized_token);

  EXPECT_CALL(mock_auth_block_utility_, SelectAuthBlockTypeForCreation(_))
      .WillOnce(ReturnValue(AuthBlockType::kTpmEcc));

  auto key_blobs = std::make_unique<KeyBlobs>(kKeyBlobs);
  auto auth_block_state = std::make_unique<AuthBlockState>();
  auth_block_state->state = kTpmState;
  EXPECT_CALL(mock_auth_block_utility_, CreateKeyBlobsWithAuthBlock(_, _, _))
      .WillOnce([&key_blobs, &auth_block_state](
                    AuthBlockType auth_block_type, const AuthInput& auth_input,
                    AuthBlock::CreateCallback create_callback) {
        std::move(create_callback)
            .Run(OkStatus<CryptohomeError>(), std::move(key_blobs),
                 std::move(auth_block_state));
        return true;
      });
  EXPECT_EQ(
      AddPasswordAuthFactor(*auth_session, kPasswordLabel, kPassword).error(),
      user_data_auth::CRYPTOHOME_ERROR_NOT_SET);

  // Act.
  EXPECT_CALL(mock_auth_block_utility_, GetAuthBlockTypeFromState(_))
      .WillRepeatedly(Return(AuthBlockType::kTpmEcc));

  auto key_blobs2 = std::make_unique<KeyBlobs>(kKeyBlobs);
  EXPECT_CALL(mock_auth_block_utility_, DeriveKeyBlobsWithAuthBlock(_, _, _, _))
      .WillOnce([&key_blobs2](AuthBlockType auth_block_type,
                              const AuthInput& auth_input,
                              const AuthBlockState& auth_state,
                              AuthBlock::DeriveCallback derive_callback) {
        std::move(derive_callback)
            .Run(OkStatus<CryptohomeError>(), std::move(key_blobs2),
                 std::nullopt);
        return true;
      });
  user_data_auth::AuthenticateAuthFactorRequest request;
  request.set_auth_session_id(serialized_token);
  request.set_auth_factor_label(kPasswordLabel);
  request.mutable_auth_input()->mutable_password_input()->set_secret(kPassword);
  const user_data_auth::AuthenticateAuthFactorReply reply =
      AuthenticateAuthFactor(request);

  // Assert.
  EXPECT_EQ(reply.error(), user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  EXPECT_THAT(reply.auth_properties().authorized_for(),
              UnorderedElementsAre(AUTH_INTENT_DECRYPT, AUTH_INTENT_VERIFY_ONLY,
                                   AUTH_INTENT_WEBAUTHN));
  EXPECT_THAT(reply.authorized_for(),
              UnorderedElementsAre(AUTH_INTENT_DECRYPT, AUTH_INTENT_VERIFY_ONLY,
                                   AUTH_INTENT_WEBAUTHN));
}

TEST_F(AuthSessionInterfaceMockAuthTest, AuthenticateAuthFactorCheckSignal) {
  const ObfuscatedUsername obfuscated_username = SanitizeUserName(kUsername);

  // Arrange.
  EXPECT_CALL(platform_, DirectoryExists(UserPath(obfuscated_username)))
      .WillRepeatedly(Return(true));
  const SerializedVaultKeyset serialized_vk =
      CreateFakePasswordVk(kPasswordLabel);
  MockVKToAuthFactorMapLoading(obfuscated_username, {serialized_vk},
                               keyset_management_);

  MockKeysetLoadingByLabel(obfuscated_username, serialized_vk,
                           keyset_management_);
  std::string serialized_token;
  {
    CryptohomeStatusOr<InUseAuthSession> auth_session_status =
        auth_session_manager_->CreateAuthSession(kUsername, /*flags=*/0,
                                                 AuthIntent::kDecrypt);
    EXPECT_THAT(auth_session_status, IsOk());
    AuthSession* auth_session = auth_session_status.value().Get();

    ASSERT_TRUE(auth_session);
    serialized_token = auth_session->serialized_token();
  }

  // Copy results from callback.
  user_data_auth::AuthenticateAuthFactorCompleted result_proto;
  userdataauth_.SetAuthenticateAuthFactorCompletedCallback(base::BindRepeating(
      [](user_data_auth::AuthenticateAuthFactorCompleted* proto_copy,
         const user_data_auth::AuthenticateAuthFactorCompleted proto) {
        proto_copy->CopyFrom(proto);
      },
      &result_proto));

  // Act.
  user_data_auth::AuthenticateAuthFactorRequest request;
  request.set_auth_session_id(serialized_token);
  request.add_auth_factor_labels("password");
  request.mutable_auth_input()->mutable_password_input()->set_secret(kPassword);
  const user_data_auth::AuthenticateAuthFactorReply reply =
      AuthenticateAuthFactor(request);

  // Verify
  ASSERT_TRUE(result_proto.has_error_info());
  EXPECT_EQ(user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_KEY_NOT_FOUND,
            result_proto.error());
}

}  // namespace

}  // namespace cryptohome
