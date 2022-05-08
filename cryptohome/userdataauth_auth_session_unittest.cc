// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/userdataauth.h"

#include <memory>
#include <utility>

#include <base/test/task_environment.h>
#include <brillo/cryptohome.h>
#include <brillo/secure_blob.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libhwsec-foundation/error/testing_helper.h>

#include "cryptohome/auth_blocks/mock_auth_block_utility.h"
#include "cryptohome/auth_factor/auth_factor_manager.h"
#include "cryptohome/auth_session_manager.h"
#include "cryptohome/cleanup/mock_user_oldest_activity_timestamp_manager.h"
#include "cryptohome/credentials.h"
#include "cryptohome/credentials_test_util.h"
#include "cryptohome/crypto.h"
#include "cryptohome/filesystem_layout.h"
#include "cryptohome/mock_install_attributes.h"
#include "cryptohome/mock_keyset_management.h"
#include "cryptohome/mock_pkcs11_init.h"
#include "cryptohome/mock_platform.h"
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
    auth_session_manager_ = std::make_unique<AuthSessionManager>(
        &crypto_, &keyset_management_, &auth_block_utility_,
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
  NiceMock<MockUserSessionFactory> user_session_factory_;
  NiceMock<MockAuthBlockUtility> auth_block_utility_;
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

  user_data_auth::CryptohomeErrorCode PrepareGuestVaultImpl() {
    return userdataauth_.PrepareGuestVaultImpl();
  }

  user_data_auth::CryptohomeErrorCode PrepareEphemeralVaultImpl(
      const std::string& auth_session_id) {
    return userdataauth_.PrepareEphemeralVaultImpl(auth_session_id);
  }

  user_data_auth::CryptohomeErrorCode PreparePersistentVaultImpl(
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
    req.mutable_key()->mutable_data()->set_type(KeyData::KEY_TYPE_PASSWORD);
    return req;
  }

  void ExpectAuth(const std::string& username,
                  const brillo::SecureBlob& secret) {
    auto vk = std::make_unique<VaultKeyset>();
    Credentials creds(username, secret);
    EXPECT_CALL(keyset_management_, GetValidKeyset(CredentialsMatcher(creds)))
        .WillOnce(Return(ByMove(std::move(vk))));
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

  ASSERT_THAT(PrepareGuestVaultImpl(),
              Eq(user_data_auth::CRYPTOHOME_ERROR_NOT_SET));

  // Trying to prepare another session should fail, whether it is guest, ...
  ASSERT_THAT(PrepareGuestVaultImpl(),
              Eq(user_data_auth::CRYPTOHOME_ERROR_MOUNT_FATAL));

  // ... ephemeral, ...
  AuthSession* auth_session = auth_session_manager_->CreateAuthSession(
      kUsername, AUTH_SESSION_FLAGS_EPHEMERAL_USER);
  ASSERT_TRUE(auth_session->Authenticate(CreateAuthorization(kPassword)).ok());
  ASSERT_THAT(PrepareEphemeralVaultImpl(auth_session->serialized_token()),
              Eq(user_data_auth::CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY));

  // ... or regular.
  auth_session = auth_session_manager_->CreateAuthSession(kUsername2, 0);
  ASSERT_TRUE(auth_session->Authenticate(CreateAuthorization(kPassword2)).ok());
  ASSERT_THAT(PreparePersistentVaultImpl(auth_session->serialized_token(), {}),
              Eq(user_data_auth::CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY));
}

TEST_F(AuthSessionInterfaceTest, PrepareEphemeralVault) {
  EXPECT_CALL(homedirs_, GetPlainOwner(_))
      .WillRepeatedly(
          DoAll(SetArgPointee<0>(std::string("whoever")), Return(true)));

  // No auth session.
  ASSERT_THAT(PrepareEphemeralVaultImpl(""),
              Eq(user_data_auth::CRYPTOHOME_INVALID_AUTH_SESSION_TOKEN));

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

  ASSERT_THAT(PrepareEphemeralVaultImpl(auth_session->serialized_token()),
              Eq(user_data_auth::CRYPTOHOME_ERROR_NOT_SET));

  // Trying to mount again will yield busy.
  ASSERT_THAT(PrepareEphemeralVaultImpl(auth_session->serialized_token()),
              Eq(user_data_auth::CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY));

  // Guest fails if other sessions present.
  ASSERT_THAT(PrepareGuestVaultImpl(),
              Eq(user_data_auth::CRYPTOHOME_ERROR_MOUNT_FATAL));

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
  ASSERT_THAT(PrepareEphemeralVaultImpl(auth_session2->serialized_token()),
              Eq(user_data_auth::CRYPTOHOME_ERROR_NOT_SET));
  ASSERT_THAT(HandleAddCredentialForEphemeralVault(
                  CreateAuthorization(kPassword3), auth_session2),
              Eq(user_data_auth::CRYPTOHOME_ERROR_NOT_SET));

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
  ASSERT_TRUE(
      auth_session3->Authenticate(CreateAuthorization(kPassword3)).ok());
  ASSERT_THAT(PreparePersistentVaultImpl(auth_session3->serialized_token(), {}),
              Eq(user_data_auth::CRYPTOHOME_ERROR_NOT_SET));
}

TEST_F(AuthSessionInterfaceTest, PreparePersistentVault) {
  EXPECT_CALL(homedirs_, GetPlainOwner(_))
      .WillRepeatedly(
          DoAll(SetArgPointee<0>(std::string("whoever")), Return(true)));

  // No auth session.
  ASSERT_THAT(PreparePersistentVaultImpl("", {}),
              Eq(user_data_auth::CRYPTOHOME_INVALID_AUTH_SESSION_TOKEN));

  // Auth session not authed.
  AuthSession* auth_session =
      auth_session_manager_->CreateAuthSession(kUsername, 0);
  ASSERT_THAT(PreparePersistentVaultImpl(auth_session->serialized_token(), {}),
              Eq(user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));

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

  ASSERT_TRUE(auth_session->Authenticate(CreateAuthorization(kPassword)).ok());

  // If no shadow homedir - we do not have a user.
  EXPECT_CALL(homedirs_, Exists(SanitizeUserName(kUsername)))
      .WillRepeatedly(Return(false));
  ASSERT_THAT(PreparePersistentVaultImpl(auth_session->serialized_token(), {}),
              Eq(user_data_auth::CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND));

  // User authed and exists.
  EXPECT_CALL(homedirs_, Exists(SanitizeUserName(kUsername)))
      .WillRepeatedly(Return(true));
  ASSERT_THAT(PreparePersistentVaultImpl(auth_session->serialized_token(), {}),
              Eq(user_data_auth::CRYPTOHOME_ERROR_NOT_SET));

  // Trying to mount again will yield busy.
  ASSERT_THAT(PreparePersistentVaultImpl(auth_session->serialized_token(), {}),
              Eq(user_data_auth::CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY));

  // Guest fails if other sessions present.
  ASSERT_THAT(PrepareGuestVaultImpl(),
              Eq(user_data_auth::CRYPTOHOME_ERROR_MOUNT_FATAL));

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
  ASSERT_TRUE(
      auth_session2->Authenticate(CreateAuthorization(kPassword2)).ok());
  ASSERT_THAT(PrepareEphemeralVaultImpl(auth_session2->serialized_token()),
              Eq(user_data_auth::CRYPTOHOME_ERROR_NOT_SET));

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
  ASSERT_TRUE(
      auth_session3->Authenticate(CreateAuthorization(kPassword3)).ok());
  ASSERT_THAT(PreparePersistentVaultImpl(auth_session3->serialized_token(), {}),
              Eq(user_data_auth::CRYPTOHOME_ERROR_NOT_SET));
}

TEST_F(AuthSessionInterfaceTest, CreatePersistentUser) {
  // No auth session.
  ASSERT_THAT(CreatePersistentUserImpl("")->local_legacy_error().value(),
              Eq(user_data_auth::CRYPTOHOME_INVALID_AUTH_SESSION_TOKEN));

  // Auth session not authed.
  AuthSession* auth_session =
      auth_session_manager_->CreateAuthSession(kUsername, 0);
  ExpectAuth(kUsername, brillo::SecureBlob(kPassword));
  ASSERT_TRUE(auth_session->Authenticate(CreateAuthorization(kPassword)).ok());

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
