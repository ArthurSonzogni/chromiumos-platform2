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

#include "cryptohome/auth_factor/auth_factor_manager.h"
#include "cryptohome/auth_session_manager.h"
#include "cryptohome/cleanup/mock_user_oldest_activity_timestamp_manager.h"
#include "cryptohome/credentials.h"
#include "cryptohome/crypto.h"
#include "cryptohome/filesystem_layout.h"
#include "cryptohome/mock_install_attributes.h"
#include "cryptohome/mock_keyset_management.h"
#include "cryptohome/mock_pkcs11_init.h"
#include "cryptohome/mock_platform.h"
#include "cryptohome/pkcs11/mock_pkcs11_token_factory.h"
#include "cryptohome/storage/mock_homedirs.h"
#include "cryptohome/storage/mock_mount.h"
#include "cryptohome/storage/mock_mount_factory.h"
#include "cryptohome/user_secret_stash_storage.h"
#include "cryptohome/vault_keyset.h"

namespace cryptohome {

using ::testing::_;
using ::testing::ByMove;
using ::testing::DoAll;
using ::testing::Eq;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SetArgPointee;

using base::test::TaskEnvironment;
using brillo::cryptohome::home::kGuestUserName;
using brillo::cryptohome::home::SanitizeUserName;
using user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_EPHEMERAL_USER;

namespace {
constexpr char kUsername[] = "foo@example.com";
constexpr char kPassword[] = "password";
constexpr char kUsername2[] = "foo2@example.com";
constexpr char kPassword2[] = "password2";
constexpr char kUsername3[] = "foo3@example.com";
constexpr char kPassword3[] = "password3";

MATCHER_P(CredentialsMatcher, creds, "") {
  if (creds.username() != arg.username()) {
    return false;
  }
  if (creds.passkey() != arg.passkey()) {
    return false;
  }
  return true;
}

}  // namespace

class AuthSessionInterfaceTest : public ::testing::Test {
 public:
  AuthSessionInterfaceTest() : crypto_(&platform_) {}
  ~AuthSessionInterfaceTest() override = default;
  AuthSessionInterfaceTest(const AuthSessionInterfaceTest&) = delete;
  AuthSessionInterfaceTest& operator=(const AuthSessionInterfaceTest&) = delete;

  void SetUp() override {
    auth_session_manager_ = std::make_unique<AuthSessionManager>(
        &keyset_management_, &auth_factor_manager_,
        &user_secret_stash_storage_);
    brillo::SecureBlob system_salt;
    InitializeFilesystemLayout(&platform_, &crypto_, &system_salt);
    platform_.GetFake()->SetSystemSaltForLibbrillo(system_salt);

    userdataauth_.set_platform(&platform_);
    userdataauth_.set_homedirs(&homedirs_);
    userdataauth_.set_mount_factory(&mount_factory_);
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

  void TearDown() override {
    platform_.GetFake()->RemoveSystemSaltForLibbrillo();
  }

 protected:
  TaskEnvironment task_environment{
      TaskEnvironment::ThreadPoolExecutionMode::QUEUED};
  NiceMock<MockPlatform> platform_;
  Crypto crypto_;
  NiceMock<MockHomeDirs> homedirs_;
  NiceMock<MockMountFactory> mount_factory_;
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

  user_data_auth::CryptohomeErrorCode CreatePersistentUserImpl(
      const std::string& auth_session_id) {
    return userdataauth_.CreatePersistentUserImpl(auth_session_id);
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
    EXPECT_CALL(keyset_management_,
                GetValidKeyset(CredentialsMatcher(creds), _))
        .WillOnce(DoAll(SetArgPointee<1>(MOUNT_ERROR_NONE),
                        Return(ByMove(std::move(vk)))));
  }
};

namespace {

TEST_F(AuthSessionInterfaceTest, PrepareGuestVault) {
  // Mount factory returns a raw pointer, which caller wraps into unique_ptr.
  MockMount* mount = new MockMount();
  EXPECT_CALL(mount_factory_, New(_, _, _, _, _)).WillOnce(Return(mount));
  EXPECT_CALL(*mount, IsMounted()).WillRepeatedly(Return(true));
  EXPECT_CALL(*mount, MountEphemeralCryptohome(kGuestUserName))
      .WillOnce(Return(MOUNT_ERROR_NONE));

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
  ASSERT_THAT(auth_session->Authenticate(CreateAuthorization(kPassword)),
              Eq(MOUNT_ERROR_NONE));
  ASSERT_THAT(PrepareEphemeralVaultImpl(auth_session->serialized_token()),
              Eq(user_data_auth::CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY));

  // ... or regular.
  auth_session = auth_session_manager_->CreateAuthSession(kUsername2, 0);
  ASSERT_THAT(auth_session->Authenticate(CreateAuthorization(kPassword2)),
              Eq(MOUNT_ERROR_NONE));
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

  // Auth session not authed.
  AuthSession* auth_session = auth_session_manager_->CreateAuthSession(
      kUsername, AUTH_SESSION_FLAGS_EPHEMERAL_USER);
  ASSERT_THAT(PrepareEphemeralVaultImpl(auth_session->serialized_token()),
              Eq(user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));

  // User authed and exists.
  // Mount factory returns a raw pointer, which caller wraps into unique_ptr.
  MockMount* mount = new MockMount();
  EXPECT_CALL(mount_factory_, New(_, _, _, _, _)).WillOnce(Return(mount));
  EXPECT_CALL(*mount, IsMounted())
      .WillOnce(Return(false))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mount, MountEphemeralCryptohome(kUsername))
      .WillOnce(Return(MOUNT_ERROR_NONE));

  ASSERT_THAT(auth_session->Authenticate(CreateAuthorization(kPassword)),
              Eq(MOUNT_ERROR_NONE));
  ASSERT_THAT(PrepareEphemeralVaultImpl(auth_session->serialized_token()),
              Eq(user_data_auth::CRYPTOHOME_ERROR_NOT_SET));

  // Trying to mount again will yield busy.
  ASSERT_THAT(PrepareEphemeralVaultImpl(auth_session->serialized_token()),
              Eq(user_data_auth::CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY));

  // Guest fails if other sessions present.
  ASSERT_THAT(PrepareGuestVaultImpl(),
              Eq(user_data_auth::CRYPTOHOME_ERROR_MOUNT_FATAL));

  // But ephemeral succeeds ...
  // Mount factory returns a raw pointer, which caller wraps into unique_ptr.
  MockMount* mount2 = new MockMount();
  EXPECT_CALL(mount_factory_, New(_, _, _, _, _)).WillOnce(Return(mount2));
  EXPECT_CALL(*mount2, IsMounted())
      .WillOnce(Return(false))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mount2, MountEphemeralCryptohome(kUsername2))
      .WillOnce(Return(MOUNT_ERROR_NONE));

  AuthSession* auth_session2 = auth_session_manager_->CreateAuthSession(
      kUsername2, AUTH_SESSION_FLAGS_EPHEMERAL_USER);
  ASSERT_THAT(auth_session2->Authenticate(CreateAuthorization(kPassword2)),
              Eq(MOUNT_ERROR_NONE));
  ASSERT_THAT(PrepareEphemeralVaultImpl(auth_session2->serialized_token()),
              Eq(user_data_auth::CRYPTOHOME_ERROR_NOT_SET));

  // ... and so regular.
  // Mount factory returns a raw pointer, which caller wraps into unique_ptr.
  MockMount* mount3 = new MockMount();
  EXPECT_CALL(mount_factory_, New(_, _, _, _, _)).WillOnce(Return(mount3));
  EXPECT_CALL(*mount3, IsMounted())
      .WillOnce(Return(false))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mount3, MountCryptohome(kUsername3, _, _))
      .WillOnce(Return(MOUNT_ERROR_NONE));
  EXPECT_CALL(homedirs_, Exists(SanitizeUserName(kUsername3)))
      .WillRepeatedly(Return(true));
  ExpectAuth(kUsername3, brillo::SecureBlob(kPassword3));

  AuthSession* auth_session3 =
      auth_session_manager_->CreateAuthSession(kUsername3, 0);
  ASSERT_THAT(auth_session3->Authenticate(CreateAuthorization(kPassword3)),
              Eq(MOUNT_ERROR_NONE));
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
  // Mount factory returns a raw pointer, which caller wraps into unique_ptr.
  MockMount* mount = new MockMount();
  EXPECT_CALL(mount_factory_, New(_, _, _, _, _)).WillOnce(Return(mount));
  EXPECT_CALL(*mount, IsMounted())
      .WillOnce(Return(false))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mount, MountCryptohome(kUsername, _, _))
      .WillOnce(Return(MOUNT_ERROR_NONE));
  ExpectAuth(kUsername, brillo::SecureBlob(kPassword));

  ASSERT_THAT(auth_session->Authenticate(CreateAuthorization(kPassword)),
              Eq(MOUNT_ERROR_NONE));

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
  // Mount factory returns a raw pointer, which caller wraps into unique_ptr.
  MockMount* mount2 = new MockMount();
  EXPECT_CALL(mount_factory_, New(_, _, _, _, _)).WillOnce(Return(mount2));
  EXPECT_CALL(*mount2, IsMounted())
      .WillOnce(Return(false))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mount2, MountEphemeralCryptohome(kUsername2))
      .WillOnce(Return(MOUNT_ERROR_NONE));

  AuthSession* auth_session2 = auth_session_manager_->CreateAuthSession(
      kUsername2, AUTH_SESSION_FLAGS_EPHEMERAL_USER);
  ASSERT_THAT(auth_session2->Authenticate(CreateAuthorization(kPassword2)),
              Eq(MOUNT_ERROR_NONE));
  ASSERT_THAT(PrepareEphemeralVaultImpl(auth_session2->serialized_token()),
              Eq(user_data_auth::CRYPTOHOME_ERROR_NOT_SET));

  // ... and so regular.
  MockMount* mount3 = new MockMount();
  // Mount factory returns a raw pointer, which caller wraps into unique_ptr.
  EXPECT_CALL(mount_factory_, New(_, _, _, _, _)).WillOnce(Return(mount3));
  EXPECT_CALL(*mount3, IsMounted())
      .WillOnce(Return(false))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mount3, MountCryptohome(kUsername3, _, _))
      .WillOnce(Return(MOUNT_ERROR_NONE));
  EXPECT_CALL(homedirs_, Exists(SanitizeUserName(kUsername3)))
      .WillRepeatedly(Return(true));
  ExpectAuth(kUsername3, brillo::SecureBlob(kPassword3));

  AuthSession* auth_session3 =
      auth_session_manager_->CreateAuthSession(kUsername3, 0);
  ASSERT_THAT(auth_session3->Authenticate(CreateAuthorization(kPassword3)),
              Eq(MOUNT_ERROR_NONE));
  ASSERT_THAT(PreparePersistentVaultImpl(auth_session3->serialized_token(), {}),
              Eq(user_data_auth::CRYPTOHOME_ERROR_NOT_SET));
}

TEST_F(AuthSessionInterfaceTest, CreatePersistentUser) {
  // No auth session.
  ASSERT_THAT(CreatePersistentUserImpl(""),
              Eq(user_data_auth::CRYPTOHOME_INVALID_AUTH_SESSION_TOKEN));

  // Auth session not authed.
  AuthSession* auth_session =
      auth_session_manager_->CreateAuthSession(kUsername, 0);
  ExpectAuth(kUsername, brillo::SecureBlob(kPassword));
  ASSERT_THAT(auth_session->Authenticate(CreateAuthorization(kPassword)),
              Eq(MOUNT_ERROR_NONE));

  // Vault already exists.
  EXPECT_CALL(homedirs_, CryptohomeExists(SanitizeUserName(kUsername), _))
      .WillOnce(Return(true));
  ASSERT_THAT(CreatePersistentUserImpl(auth_session->serialized_token()),
              Eq(user_data_auth::CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY));

  // User doesn't exist and failed to create.
  EXPECT_CALL(homedirs_, CryptohomeExists(SanitizeUserName(kUsername), _))
      .WillOnce(Return(false));
  EXPECT_CALL(homedirs_, Exists(SanitizeUserName(kUsername)))
      .WillOnce(Return(false));
  EXPECT_CALL(homedirs_, Create(kUsername)).WillOnce(Return(false));
  ASSERT_THAT(CreatePersistentUserImpl(auth_session->serialized_token()),
              Eq(user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE));

  // User doesn't exist and created.
  EXPECT_CALL(homedirs_, CryptohomeExists(SanitizeUserName(kUsername), _))
      .WillOnce(Return(false));
  EXPECT_CALL(homedirs_, Exists(SanitizeUserName(kUsername)))
      .WillOnce(Return(false));
  EXPECT_CALL(homedirs_, Create(kUsername)).WillOnce(Return(true));
  ASSERT_THAT(CreatePersistentUserImpl(auth_session->serialized_token()),
              Eq(user_data_auth::CRYPTOHOME_ERROR_NOT_SET));

  // User exists but vault doesn't.
  EXPECT_CALL(homedirs_, CryptohomeExists(SanitizeUserName(kUsername), _))
      .WillOnce(Return(false));
  EXPECT_CALL(homedirs_, Exists(SanitizeUserName(kUsername)))
      .WillOnce(Return(true));
  ASSERT_THAT(CreatePersistentUserImpl(auth_session->serialized_token()),
              Eq(user_data_auth::CRYPTOHOME_ERROR_NOT_SET));
}

}  // namespace

}  // namespace cryptohome
