// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Unit tests for UserSession.

#include "cryptohome/user_session.h"

#include <string>
#include <vector>

#include <base/memory/ref_counted.h>
#include <base/test/task_environment.h>
#include <brillo/cryptohome.h>
#include <brillo/secure_blob.h>
#include <gtest/gtest.h>
#include <policy/libpolicy.h>
#include <policy/mock_device_policy.h>

#include "cryptohome/credentials.h"
#include "cryptohome/crypto.h"
#include "cryptohome/crypto/hmac.h"
#include "cryptohome/crypto/secure_blob_util.h"
#include "cryptohome/filesystem_layout.h"
#include "cryptohome/keyset_management.h"
#include "cryptohome/mock_platform.h"
#include "cryptohome/pkcs11/fake_pkcs11_token.h"
#include "cryptohome/pkcs11/mock_pkcs11_token_factory.h"
#include "cryptohome/storage/homedirs.h"
#include "cryptohome/storage/mock_mount.h"

using brillo::SecureBlob;

using ::testing::_;
using ::testing::ByRef;
using ::testing::DoAll;
using ::testing::Eq;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SetArgPointee;

namespace cryptohome {

namespace {

constexpr char kUser0[] = "First User";
constexpr char kUserPassword0[] = "user0_pass";
constexpr char kWebAuthnSecretHmacMessage[] = "AuthTimeWebAuthnSecret";

}  // namespace

class UserSessionTest : public ::testing::Test {
 public:
  UserSessionTest() : crypto_(&platform_) {}
  ~UserSessionTest() override {}

  // Not copyable or movable
  UserSessionTest(const UserSessionTest&) = delete;
  UserSessionTest& operator=(const UserSessionTest&) = delete;
  UserSessionTest(UserSessionTest&&) = delete;
  UserSessionTest& operator=(UserSessionTest&&) = delete;

  void SetUp() override {
    InitializeFilesystemLayout(&platform_, &crypto_, &system_salt_);
    keyset_management_ = std::make_unique<KeysetManagement>(
        &platform_, &crypto_, system_salt_,
        std::make_unique<VaultKeysetFactory>());
    user_activity_timestamp_manager_ =
        std::make_unique<UserOldestActivityTimestampManager>(&platform_);
    HomeDirs::RemoveCallback remove_callback;
    mock_device_policy_ = new policy::MockDevicePolicy();
    homedirs_ = std::make_unique<HomeDirs>(
        &platform_,
        std::make_unique<policy::PolicyProvider>(
            std::unique_ptr<policy::MockDevicePolicy>(mock_device_policy_)),
        remove_callback);

    platform_.GetFake()->SetSystemSaltForLibbrillo(system_salt_);

    AddUser(kUser0, kUserPassword0);

    PrepareDirectoryStructure();

    mount_ = new NiceMock<MockMount>();

    ON_CALL(pkcs11_token_factory_, New(_, _, _))
        .WillByDefault(Invoke([](const std::string& username,
                                 const base::FilePath& token_dir,
                                 const brillo::SecureBlob& auth_data) {
          return std::make_unique<FakePkcs11Token>();
        }));

    session_ = new UserSession(homedirs_.get(), keyset_management_.get(),
                               user_activity_timestamp_manager_.get(),
                               &pkcs11_token_factory_, system_salt_, mount_);
  }

  void TearDown() override {
    platform_.GetFake()->RemoveSystemSaltForLibbrillo();
  }

 protected:
  struct UserInfo {
    std::string name;
    std::string obfuscated;
    brillo::SecureBlob passkey;
    Credentials credentials;
    base::FilePath homedir_path;
    base::FilePath user_path;
  };

  // Information about users' homedirs. The order of users is equal to kUsers.
  std::vector<UserInfo> users_;
  NiceMock<MockPlatform> platform_;
  NiceMock<MockPkcs11TokenFactory> pkcs11_token_factory_;
  Crypto crypto_;
  brillo::SecureBlob system_salt_;
  std::unique_ptr<KeysetManagement> keyset_management_;
  policy::MockDevicePolicy* mock_device_policy_;  // owned by homedirs_
  std::unique_ptr<UserOldestActivityTimestampManager>
      user_activity_timestamp_manager_;
  std::unique_ptr<HomeDirs> homedirs_;
  scoped_refptr<UserSession> session_;
  // TODO(dlunev): Replace with real mount when FakePlatform is mature enough
  // to support it mock-less.
  scoped_refptr<MockMount> mount_;
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};

  void AddUser(const char* name, const char* password) {
    std::string obfuscated =
        brillo::cryptohome::home::SanitizeUserNameWithSalt(name, system_salt_);
    brillo::SecureBlob passkey;
    cryptohome::Crypto::PasswordToPasskey(password, system_salt_, &passkey);
    Credentials credentials(name, passkey);

    UserInfo info = {name,
                     obfuscated,
                     passkey,
                     credentials,
                     ShadowRoot().Append(obfuscated),
                     brillo::cryptohome::home::GetHashedUserPath(obfuscated)};
    users_.push_back(info);
  }

  void PrepareDirectoryStructure() {
    ASSERT_TRUE(platform_.CreateDirectory(ShadowRoot()));
    ASSERT_TRUE(platform_.CreateDirectory(
        brillo::cryptohome::home::GetUserPathPrefix()));
  }

  void PreparePolicy(bool enterprise_owned, const std::string& owner) {
    homedirs_->set_enterprise_owned(enterprise_owned);
    EXPECT_CALL(*mock_device_policy_, LoadPolicy())
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*mock_device_policy_, GetOwner(_))
        .WillRepeatedly(DoAll(SetArgPointee<0>(owner), Return(!owner.empty())));
  }
};

MATCHER_P(VaultOptionsEqual, options, "") {
  return memcmp(&options, &arg, sizeof(options)) == 0;
}

// Mount twice: first time with create, and the second time for the existing
// one.
TEST_F(UserSessionTest, MountVaultOk) {
  // SETUP

  const base::Time kTs1 = base::Time::FromInternalValue(42);
  const base::Time kTs2 = base::Time::FromInternalValue(43);
  const base::Time kTs3 = base::Time::FromInternalValue(44);

  // Test with ecryptfs since it has a simpler existence check.
  CryptohomeVault::Options options = {
      .force_type = EncryptedContainerType::kEcryptfs,
  };

  EXPECT_CALL(*mount_, MountCryptohome(users_[0].name, _,
                                       VaultOptionsEqual(options), true))
      .WillOnce(Return(MOUNT_ERROR_NONE));
  EXPECT_CALL(platform_, GetCurrentTime())
      .Times(2)  // Initial set and update on mount.
      .WillRepeatedly(Return(kTs1));

  // TEST

  ASSERT_EQ(MOUNT_ERROR_NONE,
            session_->MountVault(users_[0].credentials, options,
                                 /*is_pristine=*/true));

  // VERIFY
  // Vault created.

  EXPECT_TRUE(platform_.DirectoryExists(users_[0].homedir_path));
  EXPECT_TRUE(session_->VerifyCredentials(users_[0].credentials));
  EXPECT_TRUE(keyset_management_->AreCredentialsValid(users_[0].credentials));

  EXPECT_THAT(user_activity_timestamp_manager_->GetLastUserActivityTimestamp(
                  users_[0].obfuscated),
              Eq(kTs1));

  EXPECT_NE(session_->GetWebAuthnSecret(), nullptr);
  EXPECT_FALSE(session_->GetWebAuthnSecretHash().empty());

  EXPECT_NE(session_->GetPkcs11Token(), nullptr);
  ASSERT_FALSE(session_->GetPkcs11Token()->IsReady());
  ASSERT_TRUE(session_->GetPkcs11Token()->Insert());
  ASSERT_TRUE(session_->GetPkcs11Token()->IsReady());

  // SETUP

  // TODO(dlunev): this is required to mimic a real Mount::PrepareCryptohome
  // call. Remove it when we are not mocking mount.
  platform_.CreateDirectory(GetEcryptfsUserVaultPath(users_[0].obfuscated));

  options = {};

  EXPECT_CALL(*mount_, MountCryptohome(users_[0].name, _,
                                       VaultOptionsEqual(options), false))
      .WillOnce(Return(MOUNT_ERROR_NONE));
  EXPECT_CALL(platform_, GetCurrentTime()).WillOnce(Return(kTs2));

  // TEST

  ASSERT_EQ(MOUNT_ERROR_NONE,
            session_->MountVault(users_[0].credentials, options,
                                 /*can_create=*/false));

  // VERIFY
  // Vault still exists when tried to remount with no create.
  // ts updated on mount

  EXPECT_TRUE(platform_.DirectoryExists(users_[0].homedir_path));
  EXPECT_TRUE(session_->VerifyCredentials(users_[0].credentials));
  EXPECT_TRUE(keyset_management_->AreCredentialsValid(users_[0].credentials));

  EXPECT_THAT(user_activity_timestamp_manager_->GetLastUserActivityTimestamp(
                  users_[0].obfuscated),
              Eq(kTs2));

  ASSERT_FALSE(session_->GetPkcs11Token()->IsReady());
  ASSERT_TRUE(session_->GetPkcs11Token()->Insert());
  ASSERT_TRUE(session_->GetPkcs11Token()->IsReady());

  // SETUP

  EXPECT_CALL(*mount_, IsNonEphemeralMounted()).WillOnce(Return(true));
  EXPECT_CALL(platform_, GetCurrentTime()).WillOnce(Return(kTs3));
  EXPECT_CALL(*mount_, UnmountCryptohome()).WillOnce(Return(true));

  // TEST

  ASSERT_TRUE(session_->Unmount());

  // VERIFY
  // ts updated on unmount

  EXPECT_THAT(user_activity_timestamp_manager_->GetLastUserActivityTimestamp(
                  users_[0].obfuscated),
              Eq(kTs3));

  EXPECT_EQ(session_->GetPkcs11Token(), nullptr);
}

TEST_F(UserSessionTest, MountVaultWrongCreds) {
  // SETUP

  const base::Time kTs1 = base::Time::FromInternalValue(42);

  // Test with ecryptfs since it has a simpler existence check.
  CryptohomeVault::Options options = {
      .force_type = EncryptedContainerType::kEcryptfs,
  };

  EXPECT_CALL(*mount_, MountCryptohome(users_[0].name, _,
                                       VaultOptionsEqual(options), true))
      .WillOnce(Return(MOUNT_ERROR_NONE));
  EXPECT_CALL(platform_, GetCurrentTime())
      .Times(2)  // Initial set and update on mount.
      .WillRepeatedly(Return(kTs1));

  ASSERT_EQ(MOUNT_ERROR_NONE,
            session_->MountVault(users_[0].credentials, options,
                                 /*can_create=*/true));

  EXPECT_THAT(user_activity_timestamp_manager_->GetLastUserActivityTimestamp(
                  users_[0].obfuscated),
              Eq(kTs1));

  EXPECT_CALL(*mount_, IsNonEphemeralMounted()).WillOnce(Return(true));
  EXPECT_CALL(platform_, GetCurrentTime()).WillOnce(Return(kTs1));
  EXPECT_CALL(*mount_, UnmountCryptohome()).WillOnce(Return(true));

  ASSERT_TRUE(session_->Unmount());

  // TODO(dlunev): this is required to mimic a real Mount::PrepareCryptohome
  // call. Remove it when we are not mocking mount.
  platform_.CreateDirectory(GetEcryptfsUserVaultPath(users_[0].obfuscated));

  options = {};

  EXPECT_CALL(*mount_, MountCryptohome(users_[0].name, _,
                                       VaultOptionsEqual(options), false))
      .Times(0);

  Credentials wrong_creds(users_[0].name, brillo::SecureBlob("wrong"));

  // TEST

  ASSERT_EQ(MOUNT_ERROR_KEY_FAILURE,
            session_->MountVault(wrong_creds, options, /*can_create=*/false));
  EXPECT_EQ(session_->GetPkcs11Token(), nullptr);

  // VERIFY
  // Failed to remount with wrong credentials.

  EXPECT_TRUE(platform_.DirectoryExists(users_[0].homedir_path));
  EXPECT_TRUE(session_->VerifyCredentials(users_[0].credentials));
  EXPECT_TRUE(keyset_management_->AreCredentialsValid(users_[0].credentials));

  // No mount, no ts update.
  EXPECT_THAT(user_activity_timestamp_manager_->GetLastUserActivityTimestamp(
                  users_[0].obfuscated),
              Eq(kTs1));

  // SETUP

  EXPECT_CALL(*mount_, IsNonEphemeralMounted()).WillOnce(Return(false));
  EXPECT_CALL(*mount_, UnmountCryptohome()).WillOnce(Return(true));

  // TEST

  ASSERT_TRUE(session_->Unmount());

  // VERIFY
  // No unmount, no ts update.
  EXPECT_THAT(user_activity_timestamp_manager_->GetLastUserActivityTimestamp(
                  users_[0].obfuscated),
              Eq(kTs1));
  EXPECT_NE(session_->GetWebAuthnSecret(), nullptr);
  EXPECT_FALSE(session_->GetWebAuthnSecretHash().empty());
}

TEST_F(UserSessionTest, EphemeralMountPolicyTest) {
  EXPECT_CALL(*mount_, MountEphemeralCryptohome(_))
      .WillRepeatedly(Return(MOUNT_ERROR_NONE));

  struct PolicyTestCase {
    std::string name;
    bool is_enterprise;
    std::string owner;
    std::string user;
    MountError expected_result;
  };

  std::vector<PolicyTestCase> test_cases{
      {
          .name = "NotEnterprise_NoOwner_UserLogin_OK",
          .is_enterprise = false,
          .owner = "",
          .user = "some_user",
          .expected_result = MOUNT_ERROR_EPHEMERAL_MOUNT_BY_OWNER,
      },
      {
          .name = "NotEnterprise_Owner_UserLogin_OK",
          .is_enterprise = false,
          .owner = "owner",
          .user = "some_user",
          .expected_result = MOUNT_ERROR_NONE,
      },
      {
          .name = "NotEnterprise_Owner_OwnerLogin_Error",
          .is_enterprise = false,
          .owner = "owner",
          .user = "owner",
          .expected_result = MOUNT_ERROR_EPHEMERAL_MOUNT_BY_OWNER,
      },
      {
          .name = "Enterprise_NoOwner_UserLogin_OK",
          .is_enterprise = true,
          .owner = "",
          .user = "some_user",
          .expected_result = MOUNT_ERROR_NONE,
      },
      {
          .name = "Enterprise_Owner_UserLogin_OK",
          .is_enterprise = true,
          .owner = "owner",
          .user = "some_user",
          .expected_result = MOUNT_ERROR_NONE,
      },
      {
          .name = "Enterprise_Owner_OwnerLogin_OK",
          .is_enterprise = true,
          .owner = "owner",
          .user = "owner",
          .expected_result = MOUNT_ERROR_NONE,
      },
  };

  for (const auto& test_case : test_cases) {
    PreparePolicy(test_case.is_enterprise, test_case.owner);
    ASSERT_THAT(session_->MountEphemeral(Credentials(
                    test_case.user, brillo::SecureBlob("doesn't matter"))),
                test_case.expected_result)
        << "Test case: " << test_case.name;
  }
}

// WebAuthn secret is cleared after read once.
TEST_F(UserSessionTest, WebAuthnSecretReadTwice) {
  // SETUP

  // Test with ecryptfs since it has a simpler existence check.
  CryptohomeVault::Options options = {
      .force_type = EncryptedContainerType::kEcryptfs,
  };

  EXPECT_CALL(*mount_, MountCryptohome(users_[0].name, _,
                                       VaultOptionsEqual(options), true))
      .WillOnce(Return(MOUNT_ERROR_NONE));

  ASSERT_EQ(MOUNT_ERROR_NONE,
            session_->MountVault(users_[0].credentials, options,
                                 /*can_create=*/true));

  MountError code = MOUNT_ERROR_NONE;
  std::unique_ptr<VaultKeyset> vk =
      keyset_management_->LoadUnwrappedKeyset(users_[0].credentials, &code);
  EXPECT_EQ(code, MOUNT_ERROR_NONE);
  EXPECT_NE(vk, nullptr);
  FileSystemKeyset fs_keyset(*vk);
  const std::string message(kWebAuthnSecretHmacMessage);
  auto expected_webauthn_secret = std::make_unique<brillo::SecureBlob>(
      HmacSha256(brillo::SecureBlob::Combine(fs_keyset.Key().fnek,
                                             fs_keyset.Key().fek),
                 brillo::Blob(message.cbegin(), message.cend())));
  EXPECT_NE(expected_webauthn_secret, nullptr);

  // TEST

  std::unique_ptr<brillo::SecureBlob> actual_webauthn_secret =
      session_->GetWebAuthnSecret();
  EXPECT_NE(actual_webauthn_secret, nullptr);
  EXPECT_EQ(*actual_webauthn_secret, *expected_webauthn_secret);
  EXPECT_FALSE(session_->GetWebAuthnSecretHash().empty());
  // VERIFY

  // The second read should get nothing.
  EXPECT_EQ(session_->GetWebAuthnSecret(), nullptr);

  // The second read of the WebAuthn secret hash should still get the hash.
  EXPECT_FALSE(session_->GetWebAuthnSecretHash().empty());
}

// WebAuthn secret is cleared after timeout.
TEST_F(UserSessionTest, WebAuthnSecretTimeout) {
  // SETUP

  // Test with ecryptfs since it has a simpler existence check.
  CryptohomeVault::Options options = {
      .force_type = EncryptedContainerType::kEcryptfs,
  };

  EXPECT_CALL(*mount_, MountCryptohome(users_[0].name, _,
                                       VaultOptionsEqual(options), true))
      .WillOnce(Return(MOUNT_ERROR_NONE));

  ASSERT_EQ(MOUNT_ERROR_NONE,
            session_->MountVault(users_[0].credentials, options,
                                 /*can_create=*/true));

  // TEST

  // TODO(b/184393647): Since GetWebAuthnSecret is not currently used yet,
  // the timer for clearing WebAuthn secret for security is minimized. It will
  // be set to appropriate duration after secret enforcement.
  task_environment_.FastForwardBy(base::TimeDelta::FromSeconds(0));

  // VERIFY

  EXPECT_EQ(session_->GetWebAuthnSecret(), nullptr);

  // The WebAuthn secret hash will not be cleared after timeout.
  EXPECT_FALSE(session_->GetWebAuthnSecretHash().empty());
}

class UserSessionReAuthTest : public ::testing::Test {
 public:
  UserSessionReAuthTest() : salt() {}
  virtual ~UserSessionReAuthTest() {}

  // Not copyable or movable
  UserSessionReAuthTest(const UserSessionReAuthTest&) = delete;
  UserSessionReAuthTest& operator=(const UserSessionReAuthTest&) = delete;
  UserSessionReAuthTest(UserSessionReAuthTest&&) = delete;
  UserSessionReAuthTest& operator=(UserSessionReAuthTest&&) = delete;

  void SetUp() {
    salt.resize(16);
    GetSecureRandom(salt.data(), salt.size());
  }

 protected:
  SecureBlob salt;
};

TEST_F(UserSessionReAuthTest, VerifyUser) {
  Credentials credentials("username", SecureBlob("password"));
  scoped_refptr<UserSession> session =
      new UserSession(nullptr, nullptr, nullptr, nullptr, salt, nullptr);
  EXPECT_TRUE(session->SetCredentials(credentials, 0));

  EXPECT_TRUE(session->VerifyUser(credentials.GetObfuscatedUsername(salt)));
  EXPECT_FALSE(session->VerifyUser("other"));
}

TEST_F(UserSessionReAuthTest, VerifyCredentials) {
  Credentials credentials_1("username", SecureBlob("password"));
  Credentials credentials_2("username", SecureBlob("password2"));
  Credentials credentials_3("username2", SecureBlob("password2"));

  scoped_refptr<UserSession> session =
      new UserSession(nullptr, nullptr, nullptr, nullptr, salt, nullptr);
  EXPECT_TRUE(session->SetCredentials(credentials_1, 0));
  EXPECT_TRUE(session->VerifyCredentials(credentials_1));
  EXPECT_FALSE(session->VerifyCredentials(credentials_2));
  EXPECT_FALSE(session->VerifyCredentials(credentials_3));

  EXPECT_TRUE(session->SetCredentials(credentials_2, 0));
  EXPECT_FALSE(session->VerifyCredentials(credentials_1));
  EXPECT_TRUE(session->VerifyCredentials(credentials_2));
  EXPECT_FALSE(session->VerifyCredentials(credentials_3));

  EXPECT_TRUE(session->SetCredentials(credentials_3, 0));
  EXPECT_FALSE(session->VerifyCredentials(credentials_1));
  EXPECT_FALSE(session->VerifyCredentials(credentials_2));
  EXPECT_TRUE(session->VerifyCredentials(credentials_3));
}

}  // namespace cryptohome
