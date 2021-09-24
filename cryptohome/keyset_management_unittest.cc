// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/keyset_management.h"

#include <algorithm>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/scoped_temp_dir.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/stringprintf.h>
#include <brillo/cryptohome.h>
#include <brillo/data_encoding.h>
#include <brillo/secure_blob.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "cryptohome/cleanup/mock_user_oldest_activity_timestamp_cache.h"
#include "cryptohome/credentials.h"
#include "cryptohome/crypto.h"
#include "cryptohome/crypto/hmac.h"
#include "cryptohome/crypto/secure_blob_util.h"
#include "cryptohome/fake_le_credential_backend.h"
#include "cryptohome/filesystem_layout.h"
#include "cryptohome/le_credential_manager_impl.h"
#include "cryptohome/mock_crypto.h"
#include "cryptohome/mock_cryptohome_key_loader.h"
#include "cryptohome/mock_cryptohome_keys_manager.h"
#include "cryptohome/mock_keyset_management.h"
#include "cryptohome/mock_le_credential_manager.h"
#include "cryptohome/mock_platform.h"
#include "cryptohome/mock_tpm.h"
#include "cryptohome/mock_vault_keyset.h"
#include "cryptohome/mock_vault_keyset_factory.h"
#include "cryptohome/signed_secret.pb.h"
#include "cryptohome/timestamp.pb.h"
#include "cryptohome/vault_keyset.h"

using ::testing::_;
using ::testing::ContainerEq;
using ::testing::DoAll;
using ::testing::ElementsAre;
using ::testing::EndsWith;
using ::testing::MatchesRegex;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::SetArgPointee;
using ::testing::StrEq;
using ::testing::UnorderedElementsAre;

namespace cryptohome {

namespace {

struct UserPassword {
  const char* name;
  const char* password;
};

constexpr char kUser0[] = "First User";
constexpr char kUserPassword0[] = "user0_pass";

constexpr char kCredDirName[] = "low_entropy_creds";
constexpr char kPasswordLabel[] = "password";
constexpr char kPinLabel[] = "lecred1";
constexpr char kAltPasswordLabel[] = "alt_password";

constexpr char kWrongPasskey[] = "wrong pass";
constexpr char kNewPasskey[] = "new pass";

constexpr int kWrongAuthAttempts = 6;

void GetKeysetBlob(const brillo::SecureBlob& wrapped_keyset,
                   brillo::SecureBlob* blob) {
  *blob = wrapped_keyset;
}

}  // namespace

class KeysetManagementTest : public ::testing::Test {
 public:
  KeysetManagementTest() : crypto_(&platform_) {
    CHECK(temp_dir_.CreateUniqueTempDir());
  }

  ~KeysetManagementTest() override {}

  // Not copyable or movable
  KeysetManagementTest(const KeysetManagementTest&) = delete;
  KeysetManagementTest& operator=(const KeysetManagementTest&) = delete;
  KeysetManagementTest(KeysetManagementTest&&) = delete;
  KeysetManagementTest& operator=(KeysetManagementTest&&) = delete;

  void SetUp() override {
    InitializeFilesystemLayout(&platform_, &crypto_, &system_salt_);
    keyset_management_ = std::make_unique<KeysetManagement>(
        &platform_, &crypto_, system_salt_, &timestamp_cache_,
        std::make_unique<VaultKeysetFactory>());
    mock_vault_keyset_factory_ = new MockVaultKeysetFactory();
    keyset_management_mock_vk_ = std::make_unique<KeysetManagement>(
        &platform_, &crypto_, system_salt_, &timestamp_cache_,
        std::unique_ptr<VaultKeysetFactory>(mock_vault_keyset_factory_));
    platform_.GetFake()->SetSystemSaltForLibbrillo(system_salt_);

    AddUser(kUser0, kUserPassword0);

    PrepareDirectoryStructure();
  }

  void TearDown() override {
    platform_.GetFake()->RemoveSystemSaltForLibbrillo();
  }

  // Returns location of on-disk hash tree directory.
  base::FilePath CredDirPath() {
    return temp_dir_.GetPath().Append(kCredDirName);
  }

 protected:
  NiceMock<MockPlatform> platform_;
  NiceMock<MockTpm> tpm_;
  NiceMock<MockUserOldestActivityTimestampCache> timestamp_cache_;
  Crypto crypto_;
  brillo::SecureBlob system_salt_;
  std::unique_ptr<KeysetManagement> keyset_management_;
  MockVaultKeysetFactory* mock_vault_keyset_factory_;
  std::unique_ptr<KeysetManagement> keyset_management_mock_vk_;
  base::ScopedTempDir temp_dir_;
  struct UserInfo {
    std::string name;
    std::string obfuscated;
    brillo::SecureBlob passkey;
    Credentials credentials;
    base::FilePath homedir_path;
    base::FilePath user_path;
  };

  // SETUPers

  // Information about users' keyset_management. The order of users is equal to
  // kUsers.
  std::vector<UserInfo> users_;

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
    // We only need the homedir path, not the vault/mount paths.
    for (const auto& user : users_) {
      ASSERT_TRUE(platform_.CreateDirectory(user.homedir_path));
    }
  }

  KeyData DefaultKeyData() {
    KeyData key_data;
    key_data.set_label(kPasswordLabel);
    return key_data;
  }

  KeyData DefaultLEKeyData() {
    KeyData key_data;
    key_data.set_label(kPinLabel);
    key_data.mutable_policy()->set_low_entropy_credential(true);
    return key_data;
  }

  Credentials CredsForUpdate(const brillo::SecureBlob& passkey) {
    Credentials credentials(users_[0].name, passkey);
    KeyData key_data;
    key_data.set_label(kAltPasswordLabel);
    credentials.set_key_data(key_data);
    return credentials;
  }

  Key KeyForUpdate(const Credentials& creds, int revision) {
    Key key;
    std::string secret_str;
    secret_str.resize(creds.passkey().size());
    secret_str.assign(reinterpret_cast<const char*>(creds.passkey().data()),
                      creds.passkey().size());
    key.set_secret(secret_str);
    key.mutable_data()->set_label(creds.key_data().label());
    key.mutable_data()->set_revision(revision);

    return key;
  }

  std::string SignatureForUpdate(const Key& key,
                                 const std::string& signing_key) {
    std::string changes_str;
    ac::chrome::managedaccounts::account::Secret secret;
    secret.set_revision(key.data().revision());
    secret.set_secret(key.secret());
    secret.SerializeToString(&changes_str);

    brillo::SecureBlob hmac_key(signing_key);
    brillo::SecureBlob hmac_data(changes_str.begin(), changes_str.end());
    brillo::SecureBlob hmac = HmacSha256(hmac_key, hmac_data);

    return hmac.to_string();
  }

  void KeysetSetUpWithKeyData(const KeyData& key_data) {
    for (auto& user : users_) {
      VaultKeyset vk;
      vk.Initialize(&platform_, &crypto_);
      vk.CreateRandom();
      vk.SetKeyData(key_data);
      user.credentials.set_key_data(key_data);
      ASSERT_TRUE(vk.Encrypt(user.passkey, user.obfuscated));
      ASSERT_TRUE(
          vk.Save(user.homedir_path.Append(kKeyFile).AddExtension("0")));
    }
  }

  void KeysetSetUpWithoutKeyData() {
    for (auto& user : users_) {
      VaultKeyset vk;
      vk.Initialize(&platform_, &crypto_);
      vk.CreateRandom();
      ASSERT_TRUE(vk.Encrypt(user.passkey, user.obfuscated));
      ASSERT_TRUE(
          vk.Save(user.homedir_path.Append(kKeyFile).AddExtension("0")));
    }
  }

  // TESTers

  void VerifyKeysetIndicies(const std::vector<int>& expected) {
    std::vector<int> indicies;
    ASSERT_TRUE(
        keyset_management_->GetVaultKeysets(users_[0].obfuscated, &indicies));
    EXPECT_THAT(indicies, ContainerEq(expected));
  }

  void VerifyKeysetNotPresentWithCreds(const Credentials& creds) {
    std::unique_ptr<VaultKeyset> vk =
        keyset_management_->GetValidKeyset(creds, /* error */ nullptr);
    ASSERT_EQ(vk.get(), nullptr);
  }

  void VerifyKeysetPresentWithCredsAtIndex(const Credentials& creds,
                                           int index) {
    std::unique_ptr<VaultKeyset> vk =
        keyset_management_->GetValidKeyset(creds, /* error */ nullptr);
    ASSERT_NE(vk.get(), nullptr);
    EXPECT_EQ(vk->GetLegacyIndex(), index);
    EXPECT_TRUE(vk->HasWrappedChapsKey());
    EXPECT_TRUE(vk->HasWrappedResetSeed());
  }

  void VerifyKeysetPresentWithCredsAtIndexAndRevision(const Credentials& creds,
                                                      int index,
                                                      int revision) {
    std::unique_ptr<VaultKeyset> vk =
        keyset_management_->GetValidKeyset(creds, /* error */ nullptr);
    ASSERT_NE(vk.get(), nullptr);
    EXPECT_EQ(vk->GetLegacyIndex(), index);
    EXPECT_EQ(vk->GetKeyData().revision(), revision);
    EXPECT_TRUE(vk->HasWrappedChapsKey());
    EXPECT_TRUE(vk->HasWrappedResetSeed());
  }
};

TEST_F(KeysetManagementTest, AddUserTimestampToCache) {
  VaultKeyset vk;
  vk.Initialize(&platform_, &crypto_);
  // Populate and encrypt keyset to satisfy confirmation check within |Save|.
  vk.CreateRandom();
  constexpr char kKeyFileIndexSuffix[] = "0";
  constexpr char kKeyFileTimestampSuffix[] = "0.timestamp";
  constexpr int kTime = 499;
  const base::Time t = base::Time::FromInternalValue(kTime);
  Timestamp timestamp;
  timestamp.set_timestamp(kTime);
  std::string timestamp_str;
  ASSERT_TRUE(timestamp.SerializeToString(&timestamp_str));
  ASSERT_TRUE(platform_.WriteStringToFileAtomicDurable(
      users_[0].homedir_path.Append(kKeyFile).AddExtension(
          kKeyFileTimestampSuffix),
      timestamp_str, 0600));
  ASSERT_TRUE(vk.Encrypt(brillo::SecureBlob("random"), users_[0].obfuscated));
  ASSERT_TRUE(vk.Save(users_[0].homedir_path.Append(kKeyFile).AddExtension(
      kKeyFileIndexSuffix)));

  // TS from an external file
  EXPECT_CALL(timestamp_cache_, AddExistingUser(users_[0].obfuscated, t))
      .Times(1);
  keyset_management_->AddUserTimestampToCache(users_[0].obfuscated);
}

TEST_F(KeysetManagementTest, AddUserTimestampToCacheEmpty) {
  VaultKeyset vk;
  vk.Initialize(&platform_, &crypto_);
  // Populate and encrypt keyset to satisfy confirmation check within |Save|.
  vk.CreateRandom();
  ASSERT_TRUE(vk.Encrypt(brillo::SecureBlob("random"), users_[0].obfuscated));
  ASSERT_TRUE(
      vk.Save(users_[0].homedir_path.Append(kKeyFile).AddExtension("0")));

  // No user ts is added.
  EXPECT_CALL(timestamp_cache_, AddExistingUser(users_[0].obfuscated, _))
      .Times(0);
  keyset_management_->AddUserTimestampToCache(users_[0].obfuscated);
}

TEST_F(KeysetManagementTest, AreCredentialsValid) {
  // SETUP

  KeysetSetUpWithoutKeyData();
  Credentials wrong_credentials(users_[0].name,
                                brillo::SecureBlob(kWrongPasskey));

  // TEST
  ASSERT_TRUE(keyset_management_->AreCredentialsValid(users_[0].credentials));
  ASSERT_FALSE(keyset_management_->AreCredentialsValid(wrong_credentials));
}

// Successfully adds initial keyset
TEST_F(KeysetManagementTest, AddInitialKeyset) {
  // SETUP

  users_[0].credentials.set_key_data(DefaultKeyData());

  // TEST

  EXPECT_TRUE(keyset_management_->AddInitialKeyset(users_[0].credentials));

  // VERIFY
  // Initial keyset is added, readable, has "new-er" fields correctly
  // populated and the initial index is "0".

  VerifyKeysetPresentWithCredsAtIndex(users_[0].credentials,
                                      kInitialKeysetIndex);

  std::unique_ptr<VaultKeyset> vk = keyset_management_->GetValidKeyset(
      users_[0].credentials, /* error */ nullptr);

  SerializedVaultKeyset svk = vk->ToSerialized();
  LOG(INFO) << svk.DebugString();
}

// Successfully adds new keyset
TEST_F(KeysetManagementTest, AddKeysetSuccess) {
  // SETUP

  KeysetSetUpWithKeyData(DefaultKeyData());

  brillo::SecureBlob new_passkey(kNewPasskey);
  Credentials new_credentials(users_[0].name, new_passkey);

  // TEST

  int index = -1;
  EXPECT_EQ(CRYPTOHOME_ERROR_NOT_SET,
            keyset_management_->AddKeyset(users_[0].credentials, new_passkey,
                                          nullptr, false, &index));
  EXPECT_NE(index, -1);

  // VERIFY
  // After we add an additional keyset, we can list and read both of them.

  VerifyKeysetIndicies({kInitialKeysetIndex, index});

  VerifyKeysetPresentWithCredsAtIndex(users_[0].credentials,
                                      kInitialKeysetIndex);
  VerifyKeysetPresentWithCredsAtIndex(new_credentials, index);
}

// Overrides existing keyset on label collision when "clobber" flag is present.
TEST_F(KeysetManagementTest, AddKeysetClobberSuccess) {
  // SETUP

  KeysetSetUpWithKeyData(DefaultKeyData());

  brillo::SecureBlob new_passkey(kNewPasskey);
  Credentials new_credentials(users_[0].name, new_passkey);
  // Re-use key data from existing credentials to cause label collision.
  KeyData key_data = users_[0].credentials.key_data();
  new_credentials.set_key_data(key_data);

  // TEST

  int index = -1;
  EXPECT_EQ(CRYPTOHOME_ERROR_NOT_SET,
            keyset_management_->AddKeyset(users_[0].credentials, new_passkey,
                                          &key_data, true, &index));
  EXPECT_EQ(index, 0);

  // VERIFY
  // When adding new keyset with an "existing" label and the clobber is on, we
  // expect it to override the keyset with the same label. Thus we shall have
  // a keyset readable with new_credentials under the index of the old keyset.
  // The old keyset shall be removed.

  VerifyKeysetIndicies({kInitialKeysetIndex});

  VerifyKeysetNotPresentWithCreds(users_[0].credentials);
  VerifyKeysetPresentWithCredsAtIndex(new_credentials, kInitialKeysetIndex);
}

// Return error on label collision when no "clobber".
TEST_F(KeysetManagementTest, AddKeysetNoClobber) {
  // SETUP

  KeysetSetUpWithKeyData(DefaultKeyData());

  brillo::SecureBlob new_passkey(kNewPasskey);
  Credentials new_credentials(users_[0].name, new_passkey);
  // Re-use key data from existing credentials to cause label collision.
  KeyData key_data = users_[0].credentials.key_data();
  new_credentials.set_key_data(key_data);

  // TEST

  int index = -1;
  EXPECT_EQ(CRYPTOHOME_ERROR_KEY_LABEL_EXISTS,
            keyset_management_->AddKeyset(users_[0].credentials, new_passkey,
                                          &key_data, false, &index));
  EXPECT_EQ(index, -1);

  // VERIFY
  // Label collision without "clobber" causes an addition error. Old keyset
  // shall still be readable with old credentials, and the new one shall not
  // exist.

  VerifyKeysetIndicies({kInitialKeysetIndex});

  VerifyKeysetPresentWithCredsAtIndex(users_[0].credentials,
                                      kInitialKeysetIndex);
  VerifyKeysetNotPresentWithCreds(new_credentials);
}

// Fail to add new keyset due to invalid label.
TEST_F(KeysetManagementTest, AddKeysetNonExistentLabel) {
  // SETUP

  KeysetSetUpWithKeyData(DefaultKeyData());

  brillo::SecureBlob new_passkey(kNewPasskey);
  Credentials new_credentials(users_[0].name, new_passkey);

  Credentials not_existing_label_credentials = users_[0].credentials;
  KeyData key_data = users_[0].credentials.key_data();
  key_data.set_label("i do not exist");
  not_existing_label_credentials.set_key_data(key_data);

  // TEST

  int index = -1;
  ASSERT_EQ(CRYPTOHOME_ERROR_AUTHORIZATION_KEY_NOT_FOUND,
            keyset_management_->AddKeyset(not_existing_label_credentials,
                                          new_passkey, nullptr, false, &index));
  EXPECT_EQ(index, -1);

  // VERIFY
  // Invalid label causes an addition error. Old keyset shall still be
  // readable with old credentials, and the new one shall not  exist.

  VerifyKeysetIndicies({kInitialKeysetIndex});

  VerifyKeysetPresentWithCredsAtIndex(users_[0].credentials,
                                      kInitialKeysetIndex);
  VerifyKeysetNotPresentWithCreds(new_credentials);
}

// Fail to add new keyset due to invalid credentials.
TEST_F(KeysetManagementTest, AddKeysetInvalidCreds) {
  // SETUP

  KeysetSetUpWithKeyData(DefaultKeyData());

  brillo::SecureBlob new_passkey(kNewPasskey);
  Credentials new_credentials(users_[0].name, new_passkey);

  brillo::SecureBlob wrong_passkey(kWrongPasskey);
  Credentials wrong_credentials(users_[0].name, wrong_passkey);

  // TEST

  int index = -1;
  ASSERT_EQ(CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED,
            keyset_management_->AddKeyset(wrong_credentials, new_passkey,
                                          nullptr, false, &index));
  EXPECT_EQ(index, -1);

  // VERIFY
  // Invalid credentials cause an addition error. Old keyset shall still be
  // readable with old credentials, and the new one shall not  exist.

  VerifyKeysetIndicies({kInitialKeysetIndex});

  VerifyKeysetPresentWithCredsAtIndex(users_[0].credentials,
                                      kInitialKeysetIndex);
  VerifyKeysetNotPresentWithCreds(new_credentials);
}

// Fail to add new keyset due to index pool exhaustion.
TEST_F(KeysetManagementTest, AddKeysetNoFreeIndices) {
  // SETUP

  KeysetSetUpWithKeyData(DefaultKeyData());

  brillo::SecureBlob new_passkey(kNewPasskey);
  Credentials new_credentials(users_[0].name, new_passkey);

  // Use mock not to literally create a hundread files.
  EXPECT_CALL(platform_,
              OpenFile(Property(&base::FilePath::value,
                                MatchesRegex(".*/master\\..*$")),  // nocheck
                       StrEq("wx")))
      .WillRepeatedly(Return(nullptr));

  // TEST

  int index = -1;
  EXPECT_EQ(CRYPTOHOME_ERROR_KEY_QUOTA_EXCEEDED,
            keyset_management_->AddKeyset(users_[0].credentials, new_passkey,
                                          nullptr, false, &index));
  EXPECT_EQ(index, -1);

  // VERIFY
  // Nothing should change if we were not able to add keyset due to a lack of
  // free slots. Since we mocked the "slot" check, we should still have only
  // initial keyset index, adn the keyset is readable with the old credentials.

  VerifyKeysetIndicies({kInitialKeysetIndex});

  VerifyKeysetPresentWithCredsAtIndex(users_[0].credentials,
                                      kInitialKeysetIndex);
  VerifyKeysetNotPresentWithCreds(new_credentials);
}

// Fail to add new keyset due to failed encryption.
TEST_F(KeysetManagementTest, AddKeysetEncryptFail) {
  // SETUP

  KeysetSetUpWithoutKeyData();

  brillo::SecureBlob new_passkey(kNewPasskey);
  Credentials new_credentials(users_[0].name, new_passkey);

  // Mock vk to inject encryption failure on new keyset.
  auto mock_vk_to_add = new NiceMock<MockVaultKeyset>();
  // Mock vk for existing keyset.
  auto mock_vk = new NiceMock<MockVaultKeyset>();
  mock_vk->CreateRandomResetSeed();
  mock_vk->SetWrappedResetSeed(brillo::SecureBlob("reset_seed"));
  EXPECT_CALL(*mock_vault_keyset_factory_, New(_, _))
      .Times(2)
      .WillOnce(Return(mock_vk))
      .WillOnce(Return(mock_vk_to_add));
  EXPECT_CALL(*mock_vk, Load(_)).WillOnce(Return(true));
  EXPECT_CALL(*mock_vk, Decrypt(_, _, _)).WillOnce(Return(true));
  EXPECT_CALL(*mock_vk_to_add, Encrypt(new_passkey, _)).WillOnce(Return(false));

  // TEST

  int index = -1;
  ASSERT_EQ(CRYPTOHOME_ERROR_BACKING_STORE_FAILURE,
            keyset_management_mock_vk_->AddKeyset(
                users_[0].credentials, new_passkey, nullptr, false, &index));
  EXPECT_EQ(index, -1);

  // VERIFY
  // If we failed to save the added keyset due to encryption failure, the old
  // keyset should still exist and be readable with the old credentials.

  VerifyKeysetIndicies({kInitialKeysetIndex});

  VerifyKeysetPresentWithCredsAtIndex(users_[0].credentials,
                                      kInitialKeysetIndex);
  VerifyKeysetNotPresentWithCreds(new_credentials);
}

// Fail to add new keyset due to failed disk write.
TEST_F(KeysetManagementTest, AddKeysetSaveFail) {
  // SETUP

  KeysetSetUpWithoutKeyData();

  brillo::SecureBlob new_passkey(kNewPasskey);
  Credentials new_credentials(users_[0].name, new_passkey);

  // Mock vk to inject encryption failure on new keyset.
  auto mock_vk_to_add = new NiceMock<MockVaultKeyset>();
  // Mock vk for existing keyset.
  auto mock_vk = new NiceMock<MockVaultKeyset>();
  mock_vk->CreateRandomResetSeed();
  mock_vk->SetWrappedResetSeed(brillo::SecureBlob("reset_seed"));
  EXPECT_CALL(*mock_vault_keyset_factory_, New(_, _))
      .Times(2)
      .WillOnce(Return(mock_vk))
      .WillOnce(Return(mock_vk_to_add));
  EXPECT_CALL(*mock_vk, Load(_)).WillOnce(Return(true));
  EXPECT_CALL(*mock_vk, Decrypt(_, _, _)).WillOnce(Return(true));
  EXPECT_CALL(*mock_vk_to_add, Encrypt(new_passkey, _)).WillOnce(Return(true));
  EXPECT_CALL(*mock_vk_to_add, Save(_)).WillOnce(Return(false));

  // TEST

  int index = -1;
  ASSERT_EQ(CRYPTOHOME_ERROR_BACKING_STORE_FAILURE,
            keyset_management_mock_vk_->AddKeyset(
                users_[0].credentials, new_passkey, nullptr, false, &index));
  EXPECT_EQ(index, -1);

  // VERIFY
  // If we failed to save the added keyset due to disk failure, the old
  // keyset should still exist and be readable with the old credentials.

  VerifyKeysetIndicies({kInitialKeysetIndex});

  VerifyKeysetPresentWithCredsAtIndex(users_[0].credentials,
                                      kInitialKeysetIndex);
  VerifyKeysetNotPresentWithCreds(new_credentials);
}

// Successfully removes keyset.
TEST_F(KeysetManagementTest, RemoveKeysetSuccess) {
  // SETUP

  KeysetSetUpWithKeyData(DefaultKeyData());

  brillo::SecureBlob new_passkey(kNewPasskey);
  Credentials new_credentials(users_[0].name, new_passkey);

  int index = -1;
  EXPECT_EQ(CRYPTOHOME_ERROR_NOT_SET,
            keyset_management_->AddKeyset(users_[0].credentials, new_passkey,
                                          nullptr, false, &index));

  // TEST

  EXPECT_EQ(CRYPTOHOME_ERROR_NOT_SET,
            keyset_management_->RemoveKeyset(users_[0].credentials,
                                             users_[0].credentials.key_data()));

  // VERIFY
  // We had one initial keyset and one added one. After deleting the initial
  // one, only the new one shoulde be available.

  VerifyKeysetIndicies({index});

  VerifyKeysetNotPresentWithCreds(users_[0].credentials);
  VerifyKeysetPresentWithCredsAtIndex(new_credentials, index);
}

// Fails to remove due to missing the desired key.
TEST_F(KeysetManagementTest, RemoveKeysetNotFound) {
  // SETUP

  KeysetSetUpWithKeyData(DefaultKeyData());

  KeyData key_data = users_[0].credentials.key_data();
  key_data.set_label("i do not exist");

  // TEST

  EXPECT_EQ(CRYPTOHOME_ERROR_KEY_NOT_FOUND,
            keyset_management_->RemoveKeyset(users_[0].credentials, key_data));

  // VERIFY
  // Trying to delete keyset with non-existing label. Nothing changes, initial
  // keyset still available with old credentials.

  VerifyKeysetIndicies({kInitialKeysetIndex});

  VerifyKeysetPresentWithCredsAtIndex(users_[0].credentials,
                                      kInitialKeysetIndex);
}

// Fails to remove due to not existing label.
TEST_F(KeysetManagementTest, RemoveKeysetNonExistentLabel) {
  // SETUP

  KeysetSetUpWithKeyData(DefaultKeyData());

  Credentials not_existing_label_credentials = users_[0].credentials;
  KeyData key_data = users_[0].credentials.key_data();
  key_data.set_label("i do not exist");
  not_existing_label_credentials.set_key_data(key_data);

  // TEST

  EXPECT_EQ(CRYPTOHOME_ERROR_AUTHORIZATION_KEY_NOT_FOUND,
            keyset_management_->RemoveKeyset(not_existing_label_credentials,
                                             users_[0].credentials.key_data()));

  // VERIFY
  // Wrong label on authorization credentials. Nothing changes, initial
  // keyset still available with old credentials.

  VerifyKeysetIndicies({kInitialKeysetIndex});

  VerifyKeysetPresentWithCredsAtIndex(users_[0].credentials,
                                      kInitialKeysetIndex);
}

// Fails to remove due to invalid credentials.
TEST_F(KeysetManagementTest, RemoveKeysetInvalidCreds) {
  // SETUP

  KeysetSetUpWithKeyData(DefaultKeyData());

  brillo::SecureBlob wrong_passkey(kWrongPasskey);
  Credentials wrong_credentials(users_[0].name, wrong_passkey);

  // TEST

  EXPECT_EQ(CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED,
            keyset_management_->RemoveKeyset(wrong_credentials,
                                             users_[0].credentials.key_data()));

  // VERIFY
  // Wrong credentials. Nothing changes, initial keyset still available
  // with old credentials.

  VerifyKeysetIndicies({kInitialKeysetIndex});

  VerifyKeysetPresentWithCredsAtIndex(users_[0].credentials,
                                      kInitialKeysetIndex);
}

// List labels.
TEST_F(KeysetManagementTest, GetVaultKeysetLabels) {
  // SETUP

  KeysetSetUpWithKeyData(DefaultKeyData());

  brillo::SecureBlob new_passkey(kNewPasskey);
  KeyData key_data;
  key_data.set_label(kAltPasswordLabel);

  int index = -1;
  EXPECT_EQ(CRYPTOHOME_ERROR_NOT_SET,
            keyset_management_->AddKeyset(users_[0].credentials, new_passkey,
                                          &key_data, false, &index));

  // TEST

  std::vector<std::string> labels;
  EXPECT_TRUE(
      keyset_management_->GetVaultKeysetLabels(users_[0].obfuscated, &labels));

  // VERIFY
  // Labels of the initial and newly added keysets are returned.

  ASSERT_EQ(2, labels.size());
  EXPECT_THAT(labels, UnorderedElementsAre(kPasswordLabel, kAltPasswordLabel));
}

// List labels for legacy keyset.
TEST_F(KeysetManagementTest, GetVaultKeysetLabelsOneLegacyLabeled) {
  // SETUP

  KeysetSetUpWithoutKeyData();
  std::vector<std::string> labels;

  // TEST

  EXPECT_TRUE(
      keyset_management_->GetVaultKeysetLabels(users_[0].obfuscated, &labels));

  // VERIFY
  // Initial keyset has no key data thus shall provide "legacy" label.

  ASSERT_EQ(1, labels.size());
  EXPECT_EQ(base::StringPrintf("%s%d", kKeyLegacyPrefix, kInitialKeysetIndex),
            labels[0]);
}

// Successfully force removes keyset.
TEST_F(KeysetManagementTest, ForceRemoveKeysetSuccess) {
  // SETUP

  KeysetSetUpWithKeyData(DefaultKeyData());

  brillo::SecureBlob new_passkey(kNewPasskey);
  Credentials new_credentials(users_[0].name, new_passkey);
  brillo::SecureBlob new_passkey2("new pass2");
  Credentials new_credentials2(users_[0].name, new_passkey2);

  int index = -1;
  EXPECT_EQ(CRYPTOHOME_ERROR_NOT_SET,
            keyset_management_->AddKeyset(users_[0].credentials, new_passkey,
                                          nullptr, false, &index));
  int index2 = -1;
  EXPECT_EQ(CRYPTOHOME_ERROR_NOT_SET,
            keyset_management_->AddKeyset(users_[0].credentials, new_passkey2,
                                          nullptr, false, &index2));

  // TEST

  EXPECT_TRUE(
      keyset_management_->ForceRemoveKeyset(users_[0].obfuscated, index));
  // Remove a non-existing keyset is a success.
  EXPECT_TRUE(
      keyset_management_->ForceRemoveKeyset(users_[0].obfuscated, index));

  // VERIFY
  // We added two new keysets and force removed on of them. Only initial and the
  // second added shall remain.

  VerifyKeysetIndicies({kInitialKeysetIndex, index2});

  VerifyKeysetPresentWithCredsAtIndex(users_[0].credentials,
                                      kInitialKeysetIndex);
  VerifyKeysetNotPresentWithCreds(new_credentials);
  VerifyKeysetPresentWithCredsAtIndex(new_credentials2, index2);
}

// Fails to remove keyset due to invalid index.
TEST_F(KeysetManagementTest, ForceRemoveKeysetInvalidIndex) {
  // SETUP

  KeysetSetUpWithKeyData(DefaultKeyData());

  // TEST

  ASSERT_FALSE(keyset_management_->ForceRemoveKeyset(users_[0].obfuscated, -1));
  ASSERT_FALSE(
      keyset_management_->ForceRemoveKeyset(users_[0].obfuscated, kKeyFileMax));

  // VERIFY
  // Trying to delete keyset with out-of-bound index id. Nothing changes,
  // initial keyset still available with old creds.

  VerifyKeysetIndicies({kInitialKeysetIndex});

  VerifyKeysetPresentWithCredsAtIndex(users_[0].credentials,
                                      kInitialKeysetIndex);
}

// Fails to remove keyset due to injected error.
TEST_F(KeysetManagementTest, ForceRemoveKeysetFailedDelete) {
  // SETUP

  KeysetSetUpWithKeyData(DefaultKeyData());
  EXPECT_CALL(platform_, DeleteFile(Property(&base::FilePath::value,
                                             EndsWith("master.0"))))  // nocheck
      .WillOnce(Return(false));

  // TEST

  ASSERT_FALSE(keyset_management_->ForceRemoveKeyset(users_[0].obfuscated, 0));

  // VERIFY
  // Deletion fails, Nothing changes, initial keyset still available with old
  // creds.

  VerifyKeysetIndicies({kInitialKeysetIndex});

  VerifyKeysetPresentWithCredsAtIndex(users_[0].credentials,
                                      kInitialKeysetIndex);
}

// Successfully moves keyset.
TEST_F(KeysetManagementTest, MoveKeysetSuccess) {
  // SETUP

  constexpr int kFirstMoveIndex = 17;
  constexpr int kSecondMoveIndex = 22;

  KeysetSetUpWithKeyData(DefaultKeyData());

  // TEST

  // Move twice to test move from the initial position and from a non-initial
  // position.
  ASSERT_TRUE(keyset_management_->MoveKeyset(
      users_[0].obfuscated, kInitialKeysetIndex, kFirstMoveIndex));
  ASSERT_TRUE(keyset_management_->MoveKeyset(
      users_[0].obfuscated, kFirstMoveIndex, kSecondMoveIndex));

  // VERIFY
  // Move initial keyset twice, expect it to be accessible with old creds on the
  // new index slot.

  VerifyKeysetIndicies({kSecondMoveIndex});

  VerifyKeysetPresentWithCredsAtIndex(users_[0].credentials, kSecondMoveIndex);
}

// Fails to move keyset.
TEST_F(KeysetManagementTest, MoveKeysetFail) {
  // SETUP

  KeysetSetUpWithKeyData(DefaultKeyData());

  brillo::SecureBlob new_passkey(kNewPasskey);
  Credentials new_credentials(users_[0].name, new_passkey);

  int index = -1;
  EXPECT_EQ(CRYPTOHOME_ERROR_NOT_SET,
            keyset_management_->AddKeyset(users_[0].credentials, new_passkey,
                                          nullptr, false, &index));

  const std::string kInitialFile =
      base::StringPrintf("master.%d", kInitialKeysetIndex);  // nocheck
  const std::string kIndexPlus2File =
      base::StringPrintf("master.%d", index + 2);  // nocheck
  const std::string kIndexPlus3File =
      base::StringPrintf("master.%d", index + 3);  // nocheck

  // Inject open failure for the slot 2.
  ON_CALL(platform_,
          OpenFile(Property(&base::FilePath::value, EndsWith(kIndexPlus2File)),
                   StrEq("wx")))
      .WillByDefault(Return(nullptr));

  // Inject rename failure for the slot 3.
  ON_CALL(platform_,
          Rename(Property(&base::FilePath::value, EndsWith(kInitialFile)),
                 Property(&base::FilePath::value, EndsWith(kIndexPlus3File))))
      .WillByDefault(Return(false));

  // TEST

  // Out of bound indexes
  ASSERT_FALSE(keyset_management_->MoveKeyset(users_[0].obfuscated, -1, index));
  ASSERT_FALSE(keyset_management_->MoveKeyset(users_[0].obfuscated,
                                              kInitialKeysetIndex, -1));
  ASSERT_FALSE(
      keyset_management_->MoveKeyset(users_[0].obfuscated, kKeyFileMax, index));
  ASSERT_FALSE(keyset_management_->MoveKeyset(
      users_[0].obfuscated, kInitialKeysetIndex, kKeyFileMax));

  // Not existing source
  ASSERT_FALSE(keyset_management_->MoveKeyset(users_[0].obfuscated, index + 4,
                                              index + 5));

  // Destination exists
  ASSERT_FALSE(keyset_management_->MoveKeyset(users_[0].obfuscated,
                                              kInitialKeysetIndex, index));

  // Destination file error-injected.
  ASSERT_FALSE(keyset_management_->MoveKeyset(users_[0].obfuscated,
                                              kInitialKeysetIndex, index + 2));
  ASSERT_FALSE(keyset_management_->MoveKeyset(users_[0].obfuscated,
                                              kInitialKeysetIndex, index + 3));

  // VERIFY

  // TODO(chromium:1141301, dlunev): the fact we have keyset index+3 is a bug -
  // MoveKeyset will not cleanup created file if Rename fails. Not addressing it
  // now durign test refactor, but will in the coming CLs.
  VerifyKeysetIndicies({kInitialKeysetIndex, index, index + 3});

  VerifyKeysetPresentWithCredsAtIndex(users_[0].credentials,
                                      kInitialKeysetIndex);
  VerifyKeysetPresentWithCredsAtIndex(new_credentials, index);
}

TEST_F(KeysetManagementTest, ReSaveKeysetNoReSave) {
  // SETUP

  KeysetSetUpWithKeyData(DefaultKeyData());

  std::unique_ptr<VaultKeyset> vk0 = keyset_management_->GetValidKeyset(
      users_[0].credentials, /* error */ nullptr);
  ASSERT_NE(vk0.get(), nullptr);

  // TEST

  MountError code;
  std::unique_ptr<VaultKeyset> vk_load =
      keyset_management_->LoadUnwrappedKeyset(users_[0].credentials, &code);
  EXPECT_EQ(MOUNT_ERROR_NONE, code);

  // VERIFY

  std::unique_ptr<VaultKeyset> vk0_new(keyset_management_->GetValidKeyset(
      users_[0].credentials, /* error */ nullptr));
  ASSERT_NE(vk0_new.get(), nullptr);

  brillo::SecureBlob lhs, rhs;
  GetKeysetBlob(vk0->GetWrappedKeyset(), &lhs);
  GetKeysetBlob(vk0_new->GetWrappedKeyset(), &rhs);
  ASSERT_EQ(lhs.size(), rhs.size());
  ASSERT_EQ(0, brillo::SecureMemcmp(lhs.data(), rhs.data(), lhs.size()));
}

TEST_F(KeysetManagementTest, ReSaveKeysetChapsRepopulation) {
  // SETUP

  KeysetSetUpWithKeyData(DefaultKeyData());

  std::unique_ptr<VaultKeyset> vk0 =
      keyset_management_->LoadVaultKeysetForUser(users_[0].obfuscated, 0);
  ASSERT_NE(vk0.get(), nullptr);
  vk0->ClearWrappedChapsKey();
  EXPECT_FALSE(vk0->HasWrappedChapsKey());
  ASSERT_TRUE(vk0->Save(vk0->GetSourceFile()));

  // TEST

  MountError code;
  std::unique_ptr<VaultKeyset> vk_load =
      keyset_management_->LoadUnwrappedKeyset(users_[0].credentials, &code);
  EXPECT_EQ(MOUNT_ERROR_NONE, code);
  EXPECT_TRUE(vk_load->HasWrappedChapsKey());

  // VERIFY

  std::unique_ptr<VaultKeyset> vk0_new = keyset_management_->GetValidKeyset(
      users_[0].credentials, /* error */ nullptr);
  ASSERT_NE(vk0_new.get(), nullptr);
  EXPECT_TRUE(vk0_new->HasWrappedChapsKey());

  ASSERT_EQ(vk0_new->GetChapsKey().size(), vk_load->GetChapsKey().size());
  ASSERT_EQ(0, brillo::SecureMemcmp(vk0_new->GetChapsKey().data(),
                                    vk_load->GetChapsKey().data(),
                                    vk0_new->GetChapsKey().size()));
}

TEST_F(KeysetManagementTest, ReSaveOnLoadNoReSave) {
  // SETUP

  KeysetSetUpWithKeyData(DefaultKeyData());

  std::unique_ptr<VaultKeyset> vk0 = keyset_management_->GetValidKeyset(
      users_[0].credentials, /* error */ nullptr);
  ASSERT_NE(vk0.get(), nullptr);

  // TEST

  EXPECT_FALSE(keyset_management_->ShouldReSaveKeyset(vk0.get()));
}

// The following tests use MOCKs for TpmState and hand-crafted vault keyset
// state. Ideally we shall have a fake tpm, but that is not feasible ATM.

TEST_F(KeysetManagementTest, ReSaveOnLoadTestRegularCreds) {
  // SETUP

  KeysetSetUpWithKeyData(DefaultKeyData());

  std::unique_ptr<VaultKeyset> vk0 = keyset_management_->GetValidKeyset(
      users_[0].credentials, /* error */ nullptr);
  ASSERT_NE(vk0.get(), nullptr);

  NiceMock<MockCryptohomeKeysManager> mock_cryptohome_keys_manager;
  EXPECT_CALL(mock_cryptohome_keys_manager, HasAnyCryptohomeKey())
      .WillRepeatedly(Return(true));
  EXPECT_CALL(mock_cryptohome_keys_manager, Init()).WillRepeatedly(Return());

  EXPECT_CALL(tpm_, IsEnabled()).WillRepeatedly(Return(true));
  EXPECT_CALL(tpm_, IsOwned()).WillRepeatedly(Return(true));

  crypto_.Init(&tpm_, &mock_cryptohome_keys_manager);

  // TEST

  // Scrypt wrapped shall be resaved when tpm present.
  EXPECT_TRUE(keyset_management_->ShouldReSaveKeyset(vk0.get()));

  // Tpm wrapped not pcr bound, but no public hash - resave.
  vk0->SetFlags(SerializedVaultKeyset::TPM_WRAPPED |
                SerializedVaultKeyset::SCRYPT_DERIVED);
  EXPECT_TRUE(keyset_management_->ShouldReSaveKeyset(vk0.get()));

  // Tpm wrapped pcr bound, but no public hash - resave.
  vk0->SetFlags(SerializedVaultKeyset::TPM_WRAPPED |
                SerializedVaultKeyset::SCRYPT_DERIVED |
                SerializedVaultKeyset::PCR_BOUND);
  EXPECT_TRUE(keyset_management_->ShouldReSaveKeyset(vk0.get()));

  // Tpm wrapped not pcr bound, public hash - resave.
  vk0->SetTpmPublicKeyHash(brillo::SecureBlob("public hash"));
  vk0->SetFlags(SerializedVaultKeyset::TPM_WRAPPED |
                SerializedVaultKeyset::SCRYPT_DERIVED);
  EXPECT_TRUE(keyset_management_->ShouldReSaveKeyset(vk0.get()));

  // Tpm wrapped pcr bound, public hash - no resave.
  vk0->SetTpmPublicKeyHash(brillo::SecureBlob("public hash"));
  vk0->SetFlags(SerializedVaultKeyset::TPM_WRAPPED |
                SerializedVaultKeyset::SCRYPT_DERIVED |
                SerializedVaultKeyset::PCR_BOUND);
  EXPECT_FALSE(keyset_management_->ShouldReSaveKeyset(vk0.get()));

  // Tpm wrapped pcr bound and ECC key, public hash - no resave.
  vk0->SetTpmPublicKeyHash(brillo::SecureBlob("public hash"));
  vk0->SetFlags(SerializedVaultKeyset::TPM_WRAPPED |
                SerializedVaultKeyset::SCRYPT_DERIVED |
                SerializedVaultKeyset::PCR_BOUND | SerializedVaultKeyset::ECC);
  EXPECT_FALSE(keyset_management_->ShouldReSaveKeyset(vk0.get()));
}

TEST_F(KeysetManagementTest, ReSaveOnLoadTestLeCreds) {
  // SETUP
  NiceMock<MockCryptohomeKeysManager> mock_cryptohome_keys_manager;
  FakeLECredentialBackend fake_backend_;
  auto le_cred_manager =
      std::make_unique<LECredentialManagerImpl>(&fake_backend_, CredDirPath());
  crypto_.set_le_manager_for_testing(std::move(le_cred_manager));
  crypto_.Init(&tpm_, &mock_cryptohome_keys_manager);

  KeysetSetUpWithKeyData(DefaultLEKeyData());

  std::unique_ptr<VaultKeyset> vk0 = keyset_management_->GetValidKeyset(
      users_[0].credentials, /* error */ nullptr);
  ASSERT_NE(vk0.get(), nullptr);

  EXPECT_CALL(mock_cryptohome_keys_manager, HasAnyCryptohomeKey())
      .WillRepeatedly(Return(true));
  EXPECT_CALL(mock_cryptohome_keys_manager, Init()).WillRepeatedly(Return());

  EXPECT_CALL(tpm_, IsEnabled()).WillRepeatedly(Return(true));
  EXPECT_CALL(tpm_, IsOwned()).WillRepeatedly(Return(true));

  fake_backend_.set_needs_pcr_binding(false);
  EXPECT_FALSE(keyset_management_->ShouldReSaveKeyset(vk0.get()));

  fake_backend_.set_needs_pcr_binding(true);
  EXPECT_TRUE(keyset_management_->ShouldReSaveKeyset(vk0.get()));
  // LE Credentials cannot be re-encrypted if the keyset does not have a
  // reset_seed. This should fail because the keyset_management tries to
  // re-encrypt the keyset here.
  EXPECT_FALSE(
      keyset_management_->ReSaveKeyset(users_[0].credentials, vk0.get()));
}

TEST_F(KeysetManagementTest, RemoveLECredentials) {
  // SETUP
  NiceMock<MockCryptohomeKeysManager> mock_cryptohome_keys_manager;
  FakeLECredentialBackend fake_backend_;
  auto le_cred_manager =
      std::make_unique<LECredentialManagerImpl>(&fake_backend_, CredDirPath());
  crypto_.set_le_manager_for_testing(std::move(le_cred_manager));
  crypto_.Init(&tpm_, &mock_cryptohome_keys_manager);

  // Setup initial user.
  KeysetSetUpWithKeyData(DefaultKeyData());

  // Setup pin credentials.
  brillo::SecureBlob new_passkey(kNewPasskey);
  Credentials new_credentials(users_[0].name, new_passkey);
  KeyData key_data = DefaultLEKeyData();
  new_credentials.set_key_data(key_data);

  // Add Pin Credentials
  int index = -1;
  EXPECT_EQ(CRYPTOHOME_ERROR_NOT_SET,
            keyset_management_->AddKeyset(users_[0].credentials, new_passkey,
                                          &key_data, true, &index));
  EXPECT_EQ(index, 1);

  // When adding new keyset with an new label we expect it to have another
  // keyset.
  VerifyKeysetIndicies({kInitialKeysetIndex, kInitialKeysetIndex + 1});

  // Ensure Pin keyset was added.
  std::unique_ptr<VaultKeyset> vk =
      keyset_management_->GetValidKeyset(new_credentials, /* error */ nullptr);
  ASSERT_NE(vk.get(), nullptr);

  // TEST
  keyset_management_->RemoveLECredentials(users_[0].obfuscated);

  // Verify
  vk = keyset_management_->GetValidKeyset(new_credentials, /* error */ nullptr);
  ASSERT_EQ(vk.get(), nullptr);

  // Make sure that the password credentials are still valid.
  vk = keyset_management_->GetValidKeyset(users_[0].credentials,
                                          /* error */ nullptr);
  ASSERT_NE(vk.get(), nullptr);
}

TEST_F(KeysetManagementTest, GetPublicMountPassKey) {
  // SETUP
  // Generate a valid passkey from the users id and public salt.
  std::string account_id(kUser0);

  brillo::SecureBlob public_mount_salt;
  // Fetches or creates a salt from a saltfile. Setting the force
  // parameter to false only creates a new saltfile if one doesn't
  // already exist.
  crypto_.GetPublicMountSalt(&public_mount_salt);

  brillo::SecureBlob passkey;
  Crypto::PasswordToPasskey(account_id.c_str(), public_mount_salt, &passkey);

  // TEST
  EXPECT_EQ(keyset_management_->GetPublicMountPassKey(account_id), passkey);
}

TEST_F(KeysetManagementTest, GetPublicMountPassKeyFail) {
  // SETUP
  std::string account_id(kUser0);

  NiceMock<MockCrypto> mock_crypto;
  std::unique_ptr<KeysetManagement> keyset_management_mock_crypto;
  keyset_management_mock_crypto = std::make_unique<KeysetManagement>(
      &platform_, &mock_crypto, system_salt_, &timestamp_cache_,
      std::make_unique<VaultKeysetFactory>());

  EXPECT_CALL(mock_crypto, GetPublicMountSalt).WillOnce(Return(false));

  // Compare the SecureBlob with an empty and non-empty SecureBlob.
  brillo::SecureBlob public_mount_passkey =
      keyset_management_mock_crypto->GetPublicMountPassKey(account_id);
  EXPECT_TRUE(public_mount_passkey.empty());
}

TEST_F(KeysetManagementTest, ResetLECredentialsAuthLocked) {
  // Setup
  NiceMock<MockCryptohomeKeysManager> mock_cryptohome_keys_manager;
  FakeLECredentialBackend fake_backend_;
  auto le_cred_manager =
      std::make_unique<LECredentialManagerImpl>(&fake_backend_, CredDirPath());
  crypto_.set_le_manager_for_testing(std::move(le_cred_manager));
  crypto_.Init(&tpm_, &mock_cryptohome_keys_manager);

  KeysetSetUpWithKeyData(DefaultKeyData());

  // Create an LECredential.
  brillo::SecureBlob new_passkey(kNewPasskey);
  Credentials new_credentials(users_[0].name, new_passkey);
  KeyData key_data = DefaultLEKeyData();
  new_credentials.set_key_data(key_data);

  // Add Pin Keyset to keyset_mangement_.
  int index = -1;
  EXPECT_EQ(CRYPTOHOME_ERROR_NOT_SET,
            keyset_management_->AddKeyset(users_[0].credentials, new_passkey,
                                          &key_data, true, &index));
  EXPECT_EQ(index, 1);

  std::unique_ptr<VaultKeyset> le_vk =
      keyset_management_->GetVaultKeyset(users_[0].obfuscated, kPinLabel);
  EXPECT_TRUE(le_vk->GetFlags() & SerializedVaultKeyset::LE_CREDENTIAL);

  // Test
  // Manually trigger attempts to set auth_locked to true.
  // Note: Yes there are 6 wrong attempts, on the 6th attempt
  // wrong_auth_attempts stops incrementing and sets auth_locked to true.
  brillo::SecureBlob wrong_key(kWrongPasskey);
  for (int iter = 0; iter < kWrongAuthAttempts; iter++) {
    EXPECT_FALSE(le_vk->Decrypt(wrong_key, false, nullptr));
  }

  EXPECT_EQ(crypto_.GetWrongAuthAttempts(le_vk->GetLELabel()),
            (kWrongAuthAttempts - 1));
  EXPECT_TRUE(le_vk->GetAuthLocked());

  // Have a correct attempt that will reset the credentials.
  keyset_management_->ResetLECredentials(users_[0].credentials);
  EXPECT_EQ(crypto_.GetWrongAuthAttempts(le_vk->GetLELabel()), 0);
  le_vk = keyset_management_->GetVaultKeyset(users_[0].obfuscated, kPinLabel);
  EXPECT_TRUE(le_vk->GetFlags() & SerializedVaultKeyset::LE_CREDENTIAL);
  EXPECT_FALSE(le_vk->GetAuthLocked());
}

TEST_F(KeysetManagementTest, ResetLECredentialsNotAuthLocked) {
  // Ensure the wrong_auth_counter is reset to 0 after a correct attempt,
  // even if auth_locked is false.
  // Setup
  NiceMock<MockCryptohomeKeysManager> mock_cryptohome_keys_manager;
  FakeLECredentialBackend fake_backend_;
  auto le_cred_manager =
      std::make_unique<LECredentialManagerImpl>(&fake_backend_, CredDirPath());
  crypto_.set_le_manager_for_testing(std::move(le_cred_manager));
  crypto_.Init(&tpm_, &mock_cryptohome_keys_manager);

  KeysetSetUpWithKeyData(DefaultKeyData());

  // Create an LECredential and add to keyset_mangement_.
  // Setup pin credentials.
  brillo::SecureBlob new_passkey(kNewPasskey);
  Credentials new_credentials(users_[0].name, new_passkey);
  KeyData key_data = DefaultLEKeyData();
  new_credentials.set_key_data(key_data);

  // Add Pin Keyset.
  int index = -1;
  EXPECT_EQ(CRYPTOHOME_ERROR_NOT_SET,
            keyset_management_->AddKeyset(users_[0].credentials, new_passkey,
                                          &key_data, true, &index));
  EXPECT_EQ(index, 1);

  std::unique_ptr<VaultKeyset> le_vk =
      keyset_management_->GetVaultKeyset(users_[0].obfuscated, kPinLabel);
  EXPECT_TRUE(le_vk->GetFlags() & SerializedVaultKeyset::LE_CREDENTIAL);

  // Manually trigger attempts, but not enough to set auth_locked to true.
  brillo::SecureBlob wrong_key(kWrongPasskey);
  for (int iter = 0; iter < (kWrongAuthAttempts - 1); iter++) {
    EXPECT_FALSE(le_vk->Decrypt(wrong_key, false, nullptr));
  }

  EXPECT_EQ(crypto_.GetWrongAuthAttempts(le_vk->GetLELabel()),
            (kWrongAuthAttempts - 1));
  EXPECT_FALSE(le_vk->GetAuthLocked());

  // Have a correct attempt that will reset the credentials.
  keyset_management_->ResetLECredentials(users_[0].credentials);
  EXPECT_EQ(crypto_.GetWrongAuthAttempts(le_vk->GetLELabel()), 0);
  le_vk = keyset_management_->GetVaultKeyset(users_[0].obfuscated, kPinLabel);
  EXPECT_TRUE(le_vk->GetFlags() & SerializedVaultKeyset::LE_CREDENTIAL);
  EXPECT_FALSE(le_vk->GetAuthLocked());
}

TEST_F(KeysetManagementTest, ResetLECredentialsWrongCredential) {
  // Setup
  NiceMock<MockCryptohomeKeysManager> mock_cryptohome_keys_manager;
  FakeLECredentialBackend fake_backend_;
  auto le_cred_manager =
      std::make_unique<LECredentialManagerImpl>(&fake_backend_, CredDirPath());
  crypto_.set_le_manager_for_testing(std::move(le_cred_manager));
  crypto_.Init(&tpm_, &mock_cryptohome_keys_manager);

  KeysetSetUpWithKeyData(DefaultKeyData());

  // Create an LECredential and add to keyset_mangement_.
  // Setup pin credentials.
  brillo::SecureBlob new_passkey(kNewPasskey);
  Credentials new_credentials(users_[0].name, new_passkey);
  KeyData key_data = DefaultLEKeyData();
  new_credentials.set_key_data(key_data);

  // Add Pin Keyset.
  int index = -1;
  EXPECT_EQ(CRYPTOHOME_ERROR_NOT_SET,
            keyset_management_->AddKeyset(users_[0].credentials, new_passkey,
                                          &key_data, true, &index));
  EXPECT_EQ(index, 1);

  std::unique_ptr<VaultKeyset> le_vk =
      keyset_management_->GetVaultKeyset(users_[0].obfuscated, kPinLabel);
  EXPECT_TRUE(le_vk->GetFlags() & SerializedVaultKeyset::LE_CREDENTIAL);

  // Manually trigger attempts to set auth_locked to true.
  // Note: Yes there are 6 wrong attempts, on the 6th attempt
  // wrong_auth_attempts stops incrementing and sets auth_locked to true.
  brillo::SecureBlob wrong_key(kWrongPasskey);
  for (int iter = 0; iter < kWrongAuthAttempts; iter++) {
    EXPECT_FALSE(le_vk->Decrypt(wrong_key, false, nullptr));
  }

  EXPECT_EQ(crypto_.GetWrongAuthAttempts(le_vk->GetLELabel()),
            (kWrongAuthAttempts - 1));
  EXPECT_TRUE(le_vk->GetAuthLocked());

  // Have an attempt that will fail to reset the credentials.
  Credentials wrong_credentials(users_[0].name, wrong_key);
  keyset_management_->ResetLECredentials(wrong_credentials);
  EXPECT_EQ(crypto_.GetWrongAuthAttempts(le_vk->GetLELabel()),
            (kWrongAuthAttempts - 1));
  le_vk = keyset_management_->GetVaultKeyset(users_[0].obfuscated, kPinLabel);
  EXPECT_TRUE(le_vk->GetFlags() & SerializedVaultKeyset::LE_CREDENTIAL);
  EXPECT_TRUE(le_vk->GetAuthLocked());
}

TEST_F(KeysetManagementTest, AddKeysetResetSeedGeneration) {
  // This existing keyset is used as a basis to add a new credential for a user.
  // Setup
  VaultKeyset vk;
  vk.Initialize(&platform_, &crypto_);
  vk.CreateRandom();
  vk.SetKeyData(DefaultKeyData());
  users_[0].credentials.set_key_data(DefaultKeyData());

  // Explicitly set reset_seed to be empty.
  vk.reset_seed_.clear();
  ASSERT_TRUE(vk.Encrypt(users_[0].passkey, users_[0].obfuscated));
  ASSERT_TRUE(
      vk.Save(users_[0].homedir_path.Append(kKeyFile).AddExtension("0")));

  // Reset seed should be empty for the VaultKeyset in keyset_management_.
  // There is no real code flow in cryptohome that should produce a keyset like
  // this - i.e a high entropy, password/labeled credential but with no
  // reset_seed. AddKeyset generates a new reset_seed and populates the field
  // if it's empty for any reason.
  std::unique_ptr<VaultKeyset> init_vk =
      keyset_management_->GetValidKeyset(users_[0].credentials, nullptr);
  EXPECT_FALSE(init_vk->HasWrappedResetSeed());

  // Create an Credential
  brillo::SecureBlob new_passkey(kNewPasskey);
  Credentials new_credentials(users_[0].name, new_passkey);

  // Add Credentials to keyset_mangement_
  int index = -1;
  EXPECT_EQ(CRYPTOHOME_ERROR_NOT_SET,
            keyset_management_->AddKeyset(users_[0].credentials, new_passkey,
                                          nullptr, true, &index));
  EXPECT_EQ(index, 1);

  // Test
  std::unique_ptr<VaultKeyset> add_vk =
      keyset_management_->LoadVaultKeysetForUser(users_[0].obfuscated, index);
  EXPECT_TRUE(add_vk->HasWrappedResetSeed());
}

TEST_F(KeysetManagementTest, GetValidKeysetNoValidKeyset) {
  // No valid keyset for GetValidKeyset to load.
  // Test
  MountError mount_error;
  EXPECT_EQ(nullptr, keyset_management_->GetValidKeyset(users_[0].credentials,
                                                        &mount_error));
  EXPECT_EQ(mount_error, MOUNT_ERROR_VAULT_UNRECOVERABLE);
}

TEST_F(KeysetManagementTest, GetValidKeysetNoParsableKeyset) {
  // KeysetManagement has a valid keyset, but is unable to parse due to read
  // failure.
  KeysetSetUpWithKeyData(DefaultKeyData());

  EXPECT_CALL(platform_, ReadFile(_, _)).WillOnce(Return(false));
  MountError mount_error;
  EXPECT_EQ(nullptr, keyset_management_->GetValidKeyset(users_[0].credentials,
                                                        &mount_error));
  EXPECT_EQ(mount_error, MOUNT_ERROR_VAULT_UNRECOVERABLE);
}

TEST_F(KeysetManagementTest, GetValidKeysetCryptoError) {
  // Map's all the relevant CryptoError's to their equivalent MountError
  // as per the conversion in GetValidKeyset.
  const std::map<CryptoError, MountError> kErrorMap = {
      {CryptoError::CE_TPM_FATAL, MOUNT_ERROR_VAULT_UNRECOVERABLE},
      {CryptoError::CE_OTHER_FATAL, MOUNT_ERROR_VAULT_UNRECOVERABLE},
      {CryptoError::CE_TPM_COMM_ERROR, MOUNT_ERROR_TPM_COMM_ERROR},
      {CryptoError::CE_TPM_DEFEND_LOCK, MOUNT_ERROR_TPM_DEFEND_LOCK},
      {CryptoError::CE_TPM_REBOOT, MOUNT_ERROR_TPM_NEEDS_REBOOT},
      {CryptoError::CE_OTHER_CRYPTO, MOUNT_ERROR_KEY_FAILURE},
  };

  for (const auto& [key, value] : kErrorMap) {
    // Setup
    KeysetSetUpWithoutKeyData();

    // Mock vk to inject decryption failure on GetValidKeyset
    auto mock_vk = new NiceMock<MockVaultKeyset>();
    EXPECT_CALL(*mock_vault_keyset_factory_, New(_, _))
        .WillOnce(Return(mock_vk));
    EXPECT_CALL(*mock_vk, Load(_)).WillOnce(Return(true));
    EXPECT_CALL(*mock_vk, Decrypt(_, _, _))
        .WillOnce(DoAll(SetArgPointee<2>(key), Return(false)));

    MountError mount_error;
    EXPECT_EQ(nullptr, keyset_management_mock_vk_->GetValidKeyset(
                           users_[0].credentials, &mount_error));
    EXPECT_EQ(mount_error, value);
  }
}

TEST_F(KeysetManagementTest, AddKeysetNoFile) {
  // Test for file not found.
  // Setup
  VaultKeyset vk;
  vk.Initialize(&platform_, &crypto_);
  vk.CreateRandom();

  EXPECT_CALL(platform_, OpenFile(_, StrEq("wx")))
      .WillRepeatedly(Return(nullptr));

  // Test
  // VaultKeysetPath returns no valid paths.
  EXPECT_EQ(keyset_management_->AddKeyset(users_[0].credentials, vk),
            user_data_auth::CRYPTOHOME_ERROR_KEY_QUOTA_EXCEEDED);
}

TEST_F(KeysetManagementTest, AddKeysetNewLabel) {
  // Suitable file path is found, test for first time entering a new label.
  // Setup
  VaultKeyset vk;
  vk.Initialize(&platform_, &crypto_);
  vk.CreateRandom();

  // Test
  EXPECT_EQ(keyset_management_->AddKeyset(users_[0].credentials, vk),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
}

TEST_F(KeysetManagementTest, AddKeysetLabelExists) {
  // Suitable file path is found, but label already exists.
  // Setup
  // Saves DefaultKeyData() as primary label.
  KeysetSetUpWithKeyData(DefaultKeyData());
  VaultKeyset vk;
  vk.Initialize(&platform_, &crypto_);
  vk.CreateRandom();

  // Test
  // AddKeyset creates a file at index 1, but deletes the file
  // after KeysetManagement finds a duplicate label at index 0.
  // The original label is overwritten when adding the new keyset.
  EXPECT_EQ(keyset_management_->AddKeyset(users_[0].credentials, vk),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);

  // Verify
  base::FilePath vk_path = VaultKeysetPath(users_[0].obfuscated, 1);
  EXPECT_FALSE(platform_.FileExists(vk_path));
}

TEST_F(KeysetManagementTest, AddKeysetLabelExistsFail) {
  // Suitable file path is found, label already exists,
  // but AddKeyset fails to overwrite the existing file.
  // Setup
  KeysetSetUpWithKeyData(DefaultKeyData());
  VaultKeyset vk;
  vk.Initialize(&platform_, &crypto_);
  vk.CreateRandom();

  auto mock_vk = new NiceMock<MockVaultKeyset>();
  auto match_vk = new VaultKeyset();
  match_vk->Initialize(&platform_, &crypto_);
  EXPECT_CALL(*mock_vault_keyset_factory_, New(_, _))
      .WillOnce(Return(match_vk))  // Return duplicate label in AddKeyset.
      .WillOnce(Return(mock_vk));  // mock_vk injects the encryption failure.

  // AddKeyset creates a file at index 1, but deletes the file
  // after KeysetManagement finds a duplicate label at index 0.
  // AddKeyset tries to overwrite at index 0, but test forces encrypt to fail.
  EXPECT_CALL(*mock_vk, Encrypt(_, _)).WillOnce(Return(false));

  // Test
  EXPECT_EQ(user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE,
            keyset_management_mock_vk_->AddKeyset(users_[0].credentials, vk));

  // Verify that AddKeyset deleted the file at index 1.
  base::FilePath vk_path = VaultKeysetPath(users_[0].obfuscated, 1);
  EXPECT_FALSE(platform_.FileExists(vk_path));

  // Verify original label still exists after encryption failure.
  std::unique_ptr<VaultKeyset> test_vk = keyset_management_->GetVaultKeyset(
      users_[0].obfuscated, users_[0].credentials.key_data().label());
  EXPECT_NE(nullptr, test_vk.get());
}

TEST_F(KeysetManagementTest, AddKeysetSaveFailAuthSessions) {
  // Test of AddKeyset overloaded to work with AuthSessions.
  // Suitable file path is found, but save fails.
  // Setup
  VaultKeyset vk;
  vk.Initialize(&platform_, &crypto_);
  vk.CreateRandom();

  auto mock_vk = new NiceMock<MockVaultKeyset>();
  EXPECT_CALL(*mock_vault_keyset_factory_, New(_, _)).WillOnce(Return(mock_vk));
  // Because of conditional or short-circuiting, Encrypt must
  // return true for Save() to run.
  EXPECT_CALL(*mock_vk, Encrypt(_, _)).WillOnce(Return(true));
  EXPECT_CALL(*mock_vk, Save(_)).WillOnce(Return(false));

  // Test
  // The file path created by AddKeyset is deleted after save fails.
  EXPECT_EQ(user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE,
            keyset_management_mock_vk_->AddKeyset(users_[0].credentials, vk));

  // Verify
  base::FilePath vk_path = VaultKeysetPath(users_[0].obfuscated, 0);
  EXPECT_FALSE(platform_.FileExists(vk_path));
}

TEST_F(KeysetManagementTest, AddKeysetEncryptFailAuthSessions) {
  // Test of AddKeyset overloaded to work with AuthSessions.
  // A suitable file path is found, encyrpt fails,
  // and the created VaultKeyset file is deleted.
  // Setup
  VaultKeyset vk;
  vk.Initialize(&platform_, &crypto_);
  vk.CreateRandom();

  auto mock_vk = new NiceMock<MockVaultKeyset>();
  EXPECT_CALL(*mock_vault_keyset_factory_, New(_, _)).WillOnce(Return(mock_vk));
  EXPECT_CALL(*mock_vk, Encrypt(_, _)).WillOnce(Return(false));

  // Test
  // The file path created by AddKeyset is deleted after encyrption fails.
  EXPECT_EQ(user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE,
            keyset_management_mock_vk_->AddKeyset(users_[0].credentials, vk));

  // Verify that the file was deleted.
  base::FilePath vk_path = VaultKeysetPath(users_[0].obfuscated, 0);
  EXPECT_FALSE(platform_.FileExists(vk_path));
}

TEST_F(KeysetManagementTest, GetVaultKeysetLabelsAndData) {
  // Test to load key labels data as normal.
  // Setup
  KeysetSetUpWithKeyData(DefaultKeyData());

  VaultKeyset vk;
  vk.Initialize(&platform_, &crypto_);
  vk.CreateRandom();

  brillo::SecureBlob new_passkey(kNewPasskey);
  Credentials new_credentials(users_[0].name, new_passkey);

  KeyData key_data;
  key_data.set_label(kAltPasswordLabel);
  new_credentials.set_key_data(key_data);

  EXPECT_EQ(keyset_management_->AddKeyset(new_credentials, vk),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);

  std::map<std::string, KeyData> labels_and_data_map;
  std::pair<std::string, int> answer_map[] = {
      {kAltPasswordLabel, KeyData::KEY_TYPE_PASSWORD},
      {"password", KeyData::KEY_TYPE_PASSWORD}};

  // Test
  EXPECT_TRUE(keyset_management_->GetVaultKeysetLabelsAndData(
      users_[0].obfuscated, &labels_and_data_map));
  int answer_iter = 0;
  for (const auto& [key, value] : labels_and_data_map) {
    EXPECT_EQ(key, answer_map[answer_iter].first);
    EXPECT_EQ(value.type(), answer_map[answer_iter].second);
    answer_iter++;
  }
}

TEST_F(KeysetManagementTest, GetVaultKeysetLabelsAndDataInvalidFileExtension) {
  // File extension on keyset is not equal to kKeyFile, shouldn't be read.
  // Setup
  KeysetSetUpWithKeyData(DefaultKeyData());

  VaultKeyset vk;
  vk.Initialize(&platform_, &crypto_);
  vk.CreateRandom();

  brillo::SecureBlob new_passkey(kNewPasskey);
  Credentials new_credentials(users_[0].name, new_passkey);

  KeyData key_data;
  key_data.set_label(kAltPasswordLabel);
  new_credentials.set_key_data(key_data);
  vk.SetKeyData(new_credentials.key_data());

  std::string obfuscated_username =
      new_credentials.GetObfuscatedUsername(system_salt_);
  ASSERT_TRUE(vk.Encrypt(new_credentials.passkey(), obfuscated_username));
  ASSERT_TRUE(
      vk.Save(users_[0].homedir_path.Append("wrong_ext").AddExtension("1")));

  std::map<std::string, KeyData> labels_and_data_map;
  std::pair<std::string, int> answer_map[] = {
      // "alt_password" is not fetched below, file extension is wrong.
      // {"alt_password", KeyData::KEY_TYPE_PASSWORD}
      {"password", KeyData::KEY_TYPE_PASSWORD},
  };

  // Test
  EXPECT_TRUE(keyset_management_->GetVaultKeysetLabelsAndData(
      obfuscated_username, &labels_and_data_map));
  int answer_iter = 0;
  for (const auto& [key, value] : labels_and_data_map) {
    EXPECT_EQ(key, answer_map[answer_iter].first);
    EXPECT_EQ(value.type(), answer_map[answer_iter].second);
    answer_iter++;
  }
}

TEST_F(KeysetManagementTest, GetVaultKeysetLabelsAndDataInvalidFileIndex) {
  // Test for invalid key file range,
  // i.e. AddExtension appends a string that isn't a number.
  // Setup
  KeysetSetUpWithKeyData(DefaultKeyData());

  VaultKeyset vk;
  vk.Initialize(&platform_, &crypto_);
  vk.CreateRandom();

  brillo::SecureBlob new_passkey(kNewPasskey);
  Credentials new_credentials(users_[0].name, new_passkey);

  KeyData key_data;
  key_data.set_label(kAltPasswordLabel);
  new_credentials.set_key_data(key_data);
  vk.SetKeyData(new_credentials.key_data());

  std::string obfuscated_username =
      new_credentials.GetObfuscatedUsername(system_salt_);
  ASSERT_TRUE(vk.Encrypt(new_credentials.passkey(), obfuscated_username));
  // GetVaultKeysetLabelsAndData will skip over any file with an exentsion
  // that is not a number (NAN), but in this case we use the string NAN to
  // represent this.
  ASSERT_TRUE(
      vk.Save(users_[0].homedir_path.Append(kKeyFile).AddExtension("NAN")));

  std::map<std::string, KeyData> labels_and_data_map;
  std::pair<std::string, int> answer_map[] = {
      // "alt_password" is not fetched, invalid file index.
      // {"alt_password", KeyData::KEY_TYPE_PASSWORD}
      {"password", KeyData::KEY_TYPE_PASSWORD},
  };

  // Test
  EXPECT_TRUE(keyset_management_->GetVaultKeysetLabelsAndData(
      obfuscated_username, &labels_and_data_map));
  int answer_iter = 0;
  for (const auto& [key, value] : labels_and_data_map) {
    EXPECT_EQ(key, answer_map[answer_iter].first);
    EXPECT_EQ(value.type(), answer_map[answer_iter].second);
    answer_iter++;
  }
}

TEST_F(KeysetManagementTest, GetVaultKeysetLabelsAndDataDuplicateLabel) {
  // Test for duplicate label.
  // Setup
  KeysetSetUpWithKeyData(DefaultKeyData());

  VaultKeyset vk;
  vk.Initialize(&platform_, &crypto_);
  vk.CreateRandom();

  brillo::SecureBlob new_passkey(kNewPasskey);
  Credentials new_credentials(users_[0].name, new_passkey);

  KeyData key_data;
  // Setting label to be the duplicate of original.
  key_data.set_label(kPasswordLabel);
  new_credentials.set_key_data(key_data);
  vk.SetKeyData(new_credentials.key_data());

  std::string obfuscated_username =
      new_credentials.GetObfuscatedUsername(system_salt_);
  ASSERT_TRUE(vk.Encrypt(new_credentials.passkey(), obfuscated_username));
  ASSERT_TRUE(
      vk.Save(users_[0].homedir_path.Append(kKeyFile).AddExtension("1")));

  std::map<std::string, KeyData> labels_and_data_map;
  std::pair<std::string, int> answer_map[] = {
      // Not fetched, label is duplicate.
      // {"password", KeyData::KEY_TYPE_PASSWORD}
      {"password", KeyData::KEY_TYPE_PASSWORD},
  };

  // Test
  EXPECT_TRUE(keyset_management_->GetVaultKeysetLabelsAndData(
      obfuscated_username, &labels_and_data_map));
  int answer_iter = 0;
  for (const auto& [key, value] : labels_and_data_map) {
    EXPECT_EQ(key, answer_map[answer_iter].first);
    EXPECT_EQ(value.type(), answer_map[answer_iter].second);
    answer_iter++;
  }
}

TEST_F(KeysetManagementTest, GetVaultKeysetLabelsAndDataLoadFail) {
  // LoadVaultKeysetForUser within function fails to load the VaultKeyset.
  // Setup
  VaultKeyset vk;
  vk.Initialize(&platform_, &crypto_);
  vk.CreateRandom();
  vk.SetKeyData(DefaultKeyData());

  EXPECT_EQ(keyset_management_->AddKeyset(users_[0].credentials, vk),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);

  auto mock_vk = new NiceMock<MockVaultKeyset>();
  EXPECT_CALL(*mock_vault_keyset_factory_, New(_, _)).WillOnce(Return(mock_vk));
  EXPECT_CALL(*mock_vk, Load(_)).WillOnce(Return(false));

  // Test
  std::map<std::string, KeyData> labels_and_data_map;
  EXPECT_FALSE(keyset_management_mock_vk_->GetVaultKeysetLabelsAndData(
      users_[0].obfuscated, &labels_and_data_map));
}

}  // namespace cryptohome
