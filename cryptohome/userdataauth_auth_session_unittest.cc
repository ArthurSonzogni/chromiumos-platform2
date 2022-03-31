// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/userdataauth.h"

#include <memory>
#include <utility>

#include <base/test/mock_callback.h>
#include <base/test/task_environment.h>
#include <brillo/cryptohome.h>
#include <brillo/secure_blob.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libhwsec-foundation/error/testing_helper.h>

#include "cryptohome/auth_blocks/auth_block_utility_impl.h"
#include "cryptohome/auth_factor/auth_factor_manager.h"
#include "cryptohome/auth_session_manager.h"
#include "cryptohome/cleanup/mock_user_oldest_activity_timestamp_manager.h"
#include "cryptohome/credentials.h"
#include "cryptohome/credentials_test_util.h"
#include "cryptohome/crypto.h"
#include "cryptohome/filesystem_layout.h"
#include "cryptohome/mock_cryptohome_keys_manager.h"
#include "cryptohome/mock_install_attributes.h"
#include "cryptohome/mock_keyset_management.h"
#include "cryptohome/mock_le_credential_manager.h"
#include "cryptohome/mock_pkcs11_init.h"
#include "cryptohome/mock_platform.h"
#include "cryptohome/mock_tpm.h"
#include "cryptohome/pkcs11/mock_pkcs11_token_factory.h"
#include "cryptohome/storage/mock_homedirs.h"
#include "cryptohome/user_secret_stash_storage.h"
#include "cryptohome/user_session/mock_user_session.h"
#include "cryptohome/user_session/mock_user_session_factory.h"
#include "cryptohome/vault_keyset.h"

namespace cryptohome {

using ::testing::_;
using ::testing::ByMove;
using ::testing::DoAll;
using ::testing::Eq;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SetArgPointee;

using base::test::TaskEnvironment;
using brillo::cryptohome::home::kGuestUserName;
using brillo::cryptohome::home::SanitizeUserName;
using error::CryptohomeMountError;
using hwsec_foundation::error::testing::ReturnError;
using hwsec_foundation::status::OkStatus;
using user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_EPHEMERAL_USER;

using AuthenticateCallback = base::OnceCallback<void(
    const user_data_auth::AuthenticateAuthSessionReply&)>;

namespace {
constexpr char kUsername[] = "foo@example.com";
constexpr char kPassword[] = "password";
constexpr char kUsername2[] = "foo2@example.com";
constexpr char kPassword2[] = "password2";
constexpr char kUsername3[] = "foo3@example.com";
constexpr char kPassword3[] = "password3";

}  // namespace

class AuthSessionInterfaceTest : public ::testing::Test {
 public:
  AuthSessionInterfaceTest() : crypto_(&platform_) {}
  ~AuthSessionInterfaceTest() override = default;
  AuthSessionInterfaceTest(const AuthSessionInterfaceTest&) = delete;
  AuthSessionInterfaceTest& operator=(const AuthSessionInterfaceTest&) = delete;

  void SetUp() override {
    MockLECredentialManager* le_cred_manager = new MockLECredentialManager();
    crypto_.set_le_manager_for_testing(
        std::unique_ptr<cryptohome::LECredentialManager>(le_cred_manager));
    crypto_.Init(&tpm_, &cryptohome_keys_manager_);
    auth_block_utility_ = std::make_unique<AuthBlockUtilityImpl>(
        &keyset_management_, &crypto_, &platform_);
    auth_session_manager_ = std::make_unique<AuthSessionManager>(
        &crypto_, &keyset_management_, auth_block_utility_.get(),
        &auth_factor_manager_, &user_secret_stash_storage_);

    userdataauth_.set_platform(&platform_);
    userdataauth_.set_homedirs(&homedirs_);
    userdataauth_.set_user_session_factory(&user_session_factory_);
    userdataauth_.set_keyset_management(&keyset_management_);
    userdataauth_.set_auth_factor_manager_for_testing(&auth_factor_manager_);
    userdataauth_.set_user_secret_stash_storage_for_testing(
        &user_secret_stash_storage_);
    userdataauth_.set_auth_session_manager(auth_session_manager_.get());
    userdataauth_.set_pkcs11_token_factory(&pkcs11_token_factory_);
    userdataauth_.set_user_activity_timestamp_manager(
        &user_activity_timestamp_manager_);
    userdataauth_.set_install_attrs(&install_attrs_);
    userdataauth_.set_mount_task_runner(
        task_environment.GetMainThreadTaskRunner());
    userdataauth_.set_current_thread_id_for_test(
        UserDataAuth::TestThreadId::kMountThread);
  }

 protected:
  TaskEnvironment task_environment{
      TaskEnvironment::ThreadPoolExecutionMode::QUEUED};
  NiceMock<MockPlatform> platform_;
  Crypto crypto_;
  NiceMock<MockHomeDirs> homedirs_;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager_;
  NiceMock<MockTpm> tpm_;
  NiceMock<MockUserSessionFactory> user_session_factory_;
  std::unique_ptr<AuthBlockUtilityImpl> auth_block_utility_;
  AuthFactorManager auth_factor_manager_{&platform_};
  UserSecretStashStorage user_secret_stash_storage_{&platform_};
  NiceMock<MockKeysetManagement> keyset_management_;
  NiceMock<MockPkcs11TokenFactory> pkcs11_token_factory_;
  NiceMock<MockUserOldestActivityTimestampManager>
      user_activity_timestamp_manager_;
  NiceMock<MockInstallAttributes> install_attrs_;
  std::unique_ptr<AuthSessionManager> auth_session_manager_;
  UserDataAuth userdataauth_;

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

  user_data_auth::CryptohomeErrorCode HandleAddCredentialForEphemeralVault(
      AuthorizationRequest request, const AuthSession* auth_session) {
    return userdataauth_.HandleAddCredentialForEphemeralVault(request,
                                                              auth_session);
  }

  void GetAuthSessionStatusImpl(
      AuthSession* auth_session,
      user_data_auth::GetAuthSessionStatusReply& reply) {
    userdataauth_.GetAuthSessionStatusImpl(auth_session, reply);
  }

  AuthorizationRequest CreateAuthorization(const std::string& secret) {
    AuthorizationRequest req;
    req.mutable_key()->set_secret(secret);
    req.mutable_key()->mutable_data()->set_label("test-label");
    req.mutable_key()->mutable_data()->set_type(KeyData::KEY_TYPE_PASSWORD);
    req.mutable_key()
        ->mutable_data()
        ->mutable_policy()
        ->set_low_entropy_credential(true);
    return req;
  }

  void ExpectAuth(const std::string& username,
                  const brillo::SecureBlob& secret) {
    auto vk = std::make_unique<VaultKeyset>();
    Credentials creds(username, secret);
    EXPECT_CALL(keyset_management_, GetValidKeysetWithKeyBlobs(_, _, _))
        .WillOnce(Return(ByMove(std::move(vk))));
  }

  void ExpectVaultKeyset() {
    // Setup expectations for GetVaultKeyset to return an initialized
    // VaultKeyset Construct the vault keyset with credentials for
    // AuthBlockType::kTpmNotBoundToPcrAuthBlockState.
    auto vk = std::make_unique<VaultKeyset>();
    auto vk2 = std::make_unique<VaultKeyset>();

    const brillo::SecureBlob blob16(16, 'A');

    brillo::SecureBlob passkey(20, 'A');
    Credentials credentials("Test User", passkey);

    brillo::SecureBlob system_salt_ =
        brillo::SecureBlob(*brillo::cryptohome::home::GetSystemSalt());

    SerializedVaultKeyset serialized;
    serialized.set_flags(SerializedVaultKeyset::LE_CREDENTIAL);
    serialized.set_salt(system_salt_.data(), system_salt_.size());
    serialized.set_le_chaps_iv(blob16.data(), blob16.size());
    serialized.set_le_label(0);
    serialized.set_le_fek_iv(blob16.data(), blob16.size());
    vk->InitializeFromSerialized(serialized);
    vk2->InitializeFromSerialized(serialized);

    EXPECT_CALL(keyset_management_, GetVaultKeyset(_, _))
        .WillOnce(Return(ByMove(std::move(vk))))
        .WillOnce(Return(ByMove(std::move(vk2))));
  }
};

namespace {

TEST_F(AuthSessionInterfaceTest, PrepareGuestVault) {
  scoped_refptr<MockUserSession> user_session =
      base::MakeRefCounted<MockUserSession>();
  EXPECT_CALL(user_session_factory_, New(_, _)).WillOnce(Return(user_session));
  EXPECT_CALL(*user_session, IsActive()).WillRepeatedly(Return(true));
  EXPECT_CALL(*user_session, MountGuest()).WillOnce(Invoke([]() {
    return OkStatus<CryptohomeMountError>();
  }));

  // Expect auth and existing cryptohome-dir only for non-ephemeral
  ExpectAuth(kUsername2, brillo::SecureBlob(kPassword2));
  EXPECT_CALL(homedirs_, Exists(SanitizeUserName(kUsername2)))
      .WillRepeatedly(Return(true));

  ASSERT_TRUE(PrepareGuestVaultImpl().ok());

  // Trying to prepare another session should fail, whether it is guest, ...
  CryptohomeStatus status = PrepareGuestVaultImpl();
  ASSERT_FALSE(status.ok());
  ASSERT_EQ(status->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_MOUNT_FATAL);

  // ... ephemeral, ...
  // Set up expectation in callback for success.
  ExpectVaultKeyset();
  user_data_auth::AuthenticateAuthSessionReply reply;
  base::MockCallback<AuthenticateCallback> on_done_ephemeral;
  EXPECT_CALL(on_done_ephemeral, Run(testing::_))
      .WillOnce(testing::SaveArg<0>(&reply));

  AuthSession* auth_session = auth_session_manager_->CreateAuthSession(
      kUsername, AUTH_SESSION_FLAGS_EPHEMERAL_USER);
  auth_session->Authenticate(CreateAuthorization(kPassword),
                             on_done_ephemeral.Get());
  status = PrepareEphemeralVaultImpl(auth_session->serialized_token());
  ASSERT_FALSE(status.ok());
  ASSERT_EQ(status->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY);
  ASSERT_THAT(reply.error(), Eq(MOUNT_ERROR_NONE));

  // ... or regular.
  // Set up expectation in callback for success.
  base::MockCallback<AuthenticateCallback> on_done_regular;
  EXPECT_CALL(on_done_regular, Run(testing::_))
      .WillOnce(testing::SaveArg<0>(&reply));

  auth_session = auth_session_manager_->CreateAuthSession(kUsername2, 0);
  auth_session->Authenticate(CreateAuthorization(kPassword2),
                             on_done_regular.Get());
  status = PreparePersistentVaultImpl(auth_session->serialized_token(), {});
  ASSERT_FALSE(status.ok());
  ASSERT_EQ(status->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY);
  ASSERT_THAT(reply.error(), Eq(MOUNT_ERROR_NONE));
}

TEST_F(AuthSessionInterfaceTest, PrepareEphemeralVault) {
  EXPECT_CALL(homedirs_, GetPlainOwner(_))
      .WillRepeatedly(
          DoAll(SetArgPointee<0>(std::string("whoever")), Return(true)));

  // No auth session.
  CryptohomeStatus status = PrepareEphemeralVaultImpl("");
  ASSERT_FALSE(status.ok());
  ASSERT_EQ(status->local_legacy_error(),
            user_data_auth::CRYPTOHOME_INVALID_AUTH_SESSION_TOKEN);

  // Auth session is authed for ephemeral users.
  AuthSession* auth_session = auth_session_manager_->CreateAuthSession(
      kUsername, AUTH_SESSION_FLAGS_EPHEMERAL_USER);
  // User authed and exists.
  scoped_refptr<MockUserSession> user_session =
      base::MakeRefCounted<MockUserSession>();
  EXPECT_CALL(user_session_factory_, New(_, _)).WillOnce(Return(user_session));
  EXPECT_CALL(*user_session, IsActive())
      .WillOnce(Return(false))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*user_session, MountEphemeral(kUsername))
      .WillOnce(ReturnError<CryptohomeMountError>());

  ASSERT_TRUE(PrepareEphemeralVaultImpl(auth_session->serialized_token()).ok());
  ExpectVaultKeyset();

  // Set up expectation for Authenticate callback success.
  user_data_auth::AuthenticateAuthSessionReply reply;
  base::MockCallback<AuthenticateCallback> on_done;
  EXPECT_CALL(on_done, Run(testing::_)).WillOnce(testing::SaveArg<0>(&reply));
  auth_session->Authenticate(CreateAuthorization(kPassword), on_done.Get());

  // Evaluate error returned by callback.
  ASSERT_THAT(reply.error(), Eq(MOUNT_ERROR_NONE));

  // Trying to mount again will yield busy.
  status = PrepareEphemeralVaultImpl(auth_session->serialized_token());
  ASSERT_FALSE(status.ok());
  ASSERT_EQ(status->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY);

  // Guest fails if other sessions present.
  status = PrepareGuestVaultImpl();
  ASSERT_FALSE(status.ok());
  ASSERT_EQ(status->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_MOUNT_FATAL);

  // But ephemeral succeeds ...
  scoped_refptr<MockUserSession> user_session2 =
      base::MakeRefCounted<MockUserSession>();
  EXPECT_CALL(user_session_factory_, New(_, _)).WillOnce(Return(user_session2));
  EXPECT_CALL(*user_session2, IsActive())
      .WillOnce(Return(false))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*user_session2, IsEphemeral()).WillRepeatedly(Return(true));
  EXPECT_CALL(*user_session2, MountEphemeral(kUsername2))
      .WillOnce(ReturnError<CryptohomeMountError>());

  AuthSession* auth_session2 = auth_session_manager_->CreateAuthSession(
      kUsername2, AUTH_SESSION_FLAGS_EPHEMERAL_USER);
  ASSERT_TRUE(
      PrepareEphemeralVaultImpl(auth_session2->serialized_token()).ok());
  // Set up expectation in callback for success.
  base::MockCallback<AuthenticateCallback> on_done_second;
  EXPECT_CALL(on_done_second, Run(testing::_))
      .WillOnce(testing::SaveArg<0>(&reply));

  auth_session2->Authenticate(CreateAuthorization(kPassword2),
                              on_done_second.Get());
  ASSERT_THAT(HandleAddCredentialForEphemeralVault(
                  CreateAuthorization(kPassword3), auth_session2),
              Eq(user_data_auth::CRYPTOHOME_ERROR_NOT_SET));
  // Evaluate error returned by callback.
  ASSERT_THAT(reply.error(), Eq(MOUNT_ERROR_NONE));

  // ... and so regular.
  scoped_refptr<MockUserSession> user_session3 =
      base::MakeRefCounted<MockUserSession>();
  EXPECT_CALL(user_session_factory_, New(_, _)).WillOnce(Return(user_session3));
  EXPECT_CALL(*user_session3, IsActive())
      .WillOnce(Return(false))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*user_session3, MountVault(kUsername3, _, _))
      .WillOnce(ReturnError<CryptohomeMountError>());
  EXPECT_CALL(homedirs_, Exists(SanitizeUserName(kUsername3)))
      .WillRepeatedly(Return(true));
  ExpectAuth(kUsername3, brillo::SecureBlob(kPassword3));

  AuthSession* auth_session3 =
      auth_session_manager_->CreateAuthSession(kUsername3, 0);

  // Set up expectation in callback for success.
  base::MockCallback<AuthenticateCallback> on_done_third;
  EXPECT_CALL(on_done_third, Run(testing::_))
      .WillOnce(testing::SaveArg<0>(&reply));
  auth_session3->Authenticate(CreateAuthorization(kPassword3),
                              on_done_third.Get());
  ASSERT_TRUE(
      PreparePersistentVaultImpl(auth_session3->serialized_token(), {}).ok());
  // Evaluate error returned by callback.
  ASSERT_THAT(reply.error(), Eq(MOUNT_ERROR_NONE));
}

TEST_F(AuthSessionInterfaceTest, PreparePersistentVault) {
  EXPECT_CALL(homedirs_, GetPlainOwner(_))
      .WillRepeatedly(
          DoAll(SetArgPointee<0>(std::string("whoever")), Return(true)));

  // No auth session.
  CryptohomeStatus status = PreparePersistentVaultImpl("", {});
  ASSERT_FALSE(status.ok());
  ASSERT_EQ(status->local_legacy_error(),
            user_data_auth::CRYPTOHOME_INVALID_AUTH_SESSION_TOKEN);

  // Auth session not authed.
  AuthSession* auth_session =
      auth_session_manager_->CreateAuthSession(kUsername, 0);
  status = PreparePersistentVaultImpl(auth_session->serialized_token(), {});
  ASSERT_FALSE(status.ok());
  ASSERT_EQ(status->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);

  // Auth and prepare.
  scoped_refptr<MockUserSession> user_session =
      base::MakeRefCounted<MockUserSession>();
  EXPECT_CALL(user_session_factory_, New(_, _)).WillOnce(Return(user_session));
  EXPECT_CALL(*user_session, IsActive())
      .WillOnce(Return(false))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*user_session, MountVault(kUsername, _, _))
      .WillOnce(ReturnError<CryptohomeMountError>());
  EXPECT_CALL(homedirs_, Exists(SanitizeUserName(kUsername)))
      .WillRepeatedly(Return(true));
  ExpectAuth(kUsername, brillo::SecureBlob(kPassword));

  ExpectVaultKeyset();

  // Set up expectation in callback for success.
  user_data_auth::AuthenticateAuthSessionReply reply;
  base::MockCallback<AuthenticateCallback> on_done;
  EXPECT_CALL(on_done, Run(testing::_)).WillOnce(testing::SaveArg<0>(&reply));

  auth_session->Authenticate(CreateAuthorization(kPassword), on_done.Get());
  // Evaluate error returned by callback.
  ASSERT_THAT(reply.error(), Eq(MOUNT_ERROR_NONE));

  // If no shadow homedir - we do not have a user.
  EXPECT_CALL(homedirs_, Exists(SanitizeUserName(kUsername)))
      .WillRepeatedly(Return(false));
  status = PreparePersistentVaultImpl(auth_session->serialized_token(), {});
  ASSERT_FALSE(status.ok());
  ASSERT_EQ(status->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND);

  // User authed and exists.
  EXPECT_CALL(homedirs_, Exists(SanitizeUserName(kUsername)))
      .WillRepeatedly(Return(true));
  ASSERT_TRUE(
      PreparePersistentVaultImpl(auth_session->serialized_token(), {}).ok());

  // Trying to mount again will yield busy.
  status = PreparePersistentVaultImpl(auth_session->serialized_token(), {});
  ASSERT_FALSE(status.ok());
  ASSERT_EQ(status->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY);

  // Guest fails if other sessions present.
  status = PrepareGuestVaultImpl();
  ASSERT_FALSE(status.ok());
  ASSERT_EQ(status->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_MOUNT_FATAL);

  // But ephemeral succeeds ...
  scoped_refptr<MockUserSession> user_session2 =
      base::MakeRefCounted<MockUserSession>();
  EXPECT_CALL(user_session_factory_, New(_, _)).WillOnce(Return(user_session2));
  EXPECT_CALL(*user_session2, IsActive())
      .WillOnce(Return(false))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*user_session2, MountEphemeral(kUsername2))
      .WillOnce(ReturnError<CryptohomeMountError>());

  AuthSession* auth_session2 = auth_session_manager_->CreateAuthSession(
      kUsername2, AUTH_SESSION_FLAGS_EPHEMERAL_USER);

  ExpectVaultKeyset();

  // Set up expectation in callback for success.
  // Evaluate error returned by callback.
  base::MockCallback<AuthenticateCallback> on_done_second;
  EXPECT_CALL(on_done_second, Run(testing::_))
      .WillOnce(testing::SaveArg<0>(&reply));
  auth_session2->Authenticate(CreateAuthorization(kPassword2),
                              on_done_second.Get());
  ASSERT_TRUE(
      PrepareEphemeralVaultImpl(auth_session2->serialized_token()).ok());
  // Evaluate error returned by callback.
  ASSERT_THAT(reply.error(), Eq(MOUNT_ERROR_NONE));

  // ... and so regular.
  scoped_refptr<MockUserSession> user_session3 =
      base::MakeRefCounted<MockUserSession>();
  EXPECT_CALL(user_session_factory_, New(_, _)).WillOnce(Return(user_session3));
  EXPECT_CALL(*user_session3, IsActive())
      .WillOnce(Return(false))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*user_session3, MountVault(kUsername3, _, _))
      .WillOnce(ReturnError<CryptohomeMountError>());
  EXPECT_CALL(homedirs_, Exists(SanitizeUserName(kUsername3)))
      .WillRepeatedly(Return(true));
  ExpectAuth(kUsername3, brillo::SecureBlob(kPassword3));

  AuthSession* auth_session3 =
      auth_session_manager_->CreateAuthSession(kUsername3, 0);

  // Set up expectation in callback for success.
  base::MockCallback<AuthenticateCallback> on_done_third;
  EXPECT_CALL(on_done_third, Run(testing::_))
      .WillOnce(testing::SaveArg<0>(&reply));

  auth_session3->Authenticate(CreateAuthorization(kPassword3),
                              on_done_third.Get());
  ASSERT_TRUE(
      PreparePersistentVaultImpl(auth_session3->serialized_token(), {}).ok());
  // Evaluate error returned by callback.
  ASSERT_THAT(reply.error(), Eq(MOUNT_ERROR_NONE));
}

TEST_F(AuthSessionInterfaceTest, CreatePersistentUser) {
  // No auth session.
  ASSERT_THAT(CreatePersistentUserImpl("")->local_legacy_error().value(),
              Eq(user_data_auth::CRYPTOHOME_INVALID_AUTH_SESSION_TOKEN));

  // Auth session not authed.
  AuthSession* auth_session =
      auth_session_manager_->CreateAuthSession(kUsername, 0);
  ExpectAuth(kUsername, brillo::SecureBlob(kPassword));

  ExpectVaultKeyset();

  // Set up expectation in callback for success.
  user_data_auth::AuthenticateAuthSessionReply reply;
  base::MockCallback<AuthenticateCallback> on_done;
  EXPECT_CALL(on_done, Run(testing::_)).WillOnce(testing::SaveArg<0>(&reply));

  auth_session->Authenticate(CreateAuthorization(kPassword), on_done.Get());
  // Evaluate error returned by callback.
  ASSERT_THAT(reply.error(), Eq(MOUNT_ERROR_NONE));

  // Vault already exists.
  EXPECT_CALL(homedirs_, CryptohomeExists(SanitizeUserName(kUsername), _))
      .WillOnce(Return(true));
  ASSERT_THAT(CreatePersistentUserImpl(auth_session->serialized_token())
                  ->local_legacy_error()
                  .value(),
              Eq(user_data_auth::CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY));

  // User doesn't exist and failed to create.
  EXPECT_CALL(homedirs_, CryptohomeExists(SanitizeUserName(kUsername), _))
      .WillOnce(Return(false));
  EXPECT_CALL(homedirs_, Exists(SanitizeUserName(kUsername)))
      .WillOnce(Return(false));
  EXPECT_CALL(homedirs_, Create(kUsername)).WillOnce(Return(false));
  ASSERT_THAT(CreatePersistentUserImpl(auth_session->serialized_token())
                  ->local_legacy_error()
                  .value(),
              Eq(user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE));

  // User doesn't exist and created.
  EXPECT_CALL(homedirs_, CryptohomeExists(SanitizeUserName(kUsername), _))
      .WillOnce(Return(false));
  EXPECT_CALL(homedirs_, Exists(SanitizeUserName(kUsername)))
      .WillOnce(Return(false));
  EXPECT_CALL(homedirs_, Create(kUsername)).WillOnce(Return(true));
  ASSERT_TRUE(CreatePersistentUserImpl(auth_session->serialized_token()).ok());

  // User exists but vault doesn't.
  EXPECT_CALL(homedirs_, CryptohomeExists(SanitizeUserName(kUsername), _))
      .WillOnce(Return(false));
  EXPECT_CALL(homedirs_, Exists(SanitizeUserName(kUsername)))
      .WillOnce(Return(true));
  ASSERT_TRUE(CreatePersistentUserImpl(auth_session->serialized_token()).ok());
}

TEST_F(AuthSessionInterfaceTest, CreatePersistentUserFailNoLabel) {
  // No auth session.
  ASSERT_THAT(CreatePersistentUserImpl("")->local_legacy_error().value(),
              Eq(user_data_auth::CRYPTOHOME_INVALID_AUTH_SESSION_TOKEN));

  // Auth session not authed.
  AuthSession* auth_session =
      auth_session_manager_->CreateAuthSession(kUsername, 0);

  // Set up expectation in callback for failure, no label with the
  // AuthorizationRequest.
  user_data_auth::AuthenticateAuthSessionReply reply;
  base::MockCallback<AuthenticateCallback> on_done;
  EXPECT_CALL(on_done, Run(testing::_)).WillOnce(testing::SaveArg<0>(&reply));

  AuthorizationRequest auth_req;
  auth_req.mutable_key()->set_secret(kPassword);
  auth_req.mutable_key()->mutable_data()->set_type(KeyData::KEY_TYPE_PASSWORD);
  auth_session->Authenticate(auth_req, on_done.Get());

  // Evaluate error returned by callback.
  ASSERT_THAT(reply.error(),
              Eq(user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
}

TEST_F(AuthSessionInterfaceTest, GetAuthSessionStatus) {
  user_data_auth::GetAuthSessionStatusReply reply;
  AuthSession* auth_session =
      auth_session_manager_->CreateAuthSession(kUsername, 0);

  // Test 1.
  auth_session->SetStatus(AuthStatus::kAuthStatusFurtherFactorRequired);
  GetAuthSessionStatusImpl(auth_session, reply);
  ASSERT_THAT(reply.status(),
              Eq(user_data_auth::AUTH_SESSION_STATUS_FURTHER_FACTOR_REQUIRED));

  // Test 2.
  auth_session->SetStatus(AuthStatus::kAuthStatusTimedOut);
  GetAuthSessionStatusImpl(auth_session, reply);
  ASSERT_THAT(reply.status(),
              Eq(user_data_auth::AUTH_SESSION_STATUS_INVALID_AUTH_SESSION));
}

}  // namespace

}  // namespace cryptohome
