// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>

#include <base/test/mock_callback.h>
#include <base/test/task_environment.h>
#include <brillo/cryptohome.h>
#include <brillo/secure_blob.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libhwsec/frontend/cryptohome/mock_frontend.h>
#include <libhwsec/frontend/pinweaver/mock_frontend.h>
#include <libhwsec-foundation/error/testing_helper.h>

#include "cryptohome/auth_blocks/auth_block.h"
#include "cryptohome/auth_blocks/auth_block_utility_impl.h"
#include "cryptohome/auth_blocks/libscrypt_compat_auth_block.h"
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
#include "cryptohome/mock_vault_keyset_factory.h"
#include "cryptohome/pkcs11/mock_pkcs11_token_factory.h"
#include "cryptohome/storage/mock_homedirs.h"
#include "cryptohome/user_secret_stash_storage.h"
#include "cryptohome/user_session/mock_user_session.h"
#include "cryptohome/user_session/mock_user_session_factory.h"
#include "cryptohome/userdataauth.h"
#include "cryptohome/vault_keyset.h"

namespace cryptohome {

using ::testing::_;
using ::testing::NiceMock;
using ::testing::NotNull;
using ::testing::Return;

using base::test::TaskEnvironment;
using brillo::cryptohome::home::SanitizeUserName;
using hwsec_foundation::error::testing::ReturnValue;
using hwsec_foundation::status::OkStatus;

namespace {
constexpr char kUsername[] = "foo@example.com";
constexpr char kPassword[] = "password";
}  // namespace

namespace {
// TODO(b/233700483): Replace this with the mock auth block.
class FallbackVaultKeyset : public VaultKeyset {
 protected:
  std::unique_ptr<cryptohome::SyncAuthBlock> GetAuthBlockForCreation()
      const override {
    auto auth_block_for_creation = VaultKeyset::GetAuthBlockForCreation();
    if (!auth_block_for_creation) {
      return std::make_unique<LibScryptCompatAuthBlock>();
    }

    return auth_block_for_creation;
  }
};
}  // namespace

class AuthSessionTestWithKeysetManagement : public ::testing::Test {
 public:
  AuthSessionTestWithKeysetManagement()
      : crypto_(&hwsec_,
                &pinweaver_,
                &cryptohome_keys_manager_,
                /*recovery_backend=*/nullptr) {
    // Setting HWSec Expectations.
    EXPECT_CALL(hwsec_, IsEnabled()).WillRepeatedly(ReturnValue(true));
    EXPECT_CALL(hwsec_, IsReady()).WillRepeatedly(ReturnValue(true));
    EXPECT_CALL(hwsec_, IsDAMitigationReady())
        .WillRepeatedly(ReturnValue(true));
    EXPECT_CALL(hwsec_, GetManufacturer())
        .WillRepeatedly(ReturnValue(0x43524f53));
    EXPECT_CALL(hwsec_, GetAuthValue(_, _))
        .WillRepeatedly(ReturnValue(brillo::SecureBlob()));
    EXPECT_CALL(hwsec_, SealWithCurrentUser(_, _, _))
        .WillRepeatedly(ReturnValue(brillo::Blob()));
    EXPECT_CALL(hwsec_, GetPubkeyHash(_))
        .WillRepeatedly(ReturnValue(brillo::Blob()));

    crypto_.set_le_manager_for_testing(
        std::make_unique<MockLECredentialManager>());
    DCHECK(crypto_.Init());

    mock_vault_keyset_factory_ = new NiceMock<MockVaultKeysetFactory>();
    ON_CALL(*mock_vault_keyset_factory_, New(&platform_, &crypto_))
        .WillByDefault([this](auto, auto) {
          auto* vk = new FallbackVaultKeyset();
          vk->Initialize(&platform_, &crypto_);
          return vk;
        });
    keyset_management_ = std::make_unique<KeysetManagement>(
        &platform_, &crypto_, base::WrapUnique(mock_vault_keyset_factory_));
    auth_block_utility_ = std::make_unique<AuthBlockUtilityImpl>(
        keyset_management_.get(), &crypto_, &platform_);
    auth_session_manager_ = std::make_unique<AuthSessionManager>(
        &crypto_, keyset_management_.get(), auth_block_utility_.get(),
        &auth_factor_manager_, &user_secret_stash_storage_);

    // Initializing UserData class.
    userdataauth_.set_platform(&platform_);
    userdataauth_.set_homedirs(&homedirs_);
    userdataauth_.set_user_session_factory(&user_session_factory_);
    userdataauth_.set_keyset_management(keyset_management_.get());
    userdataauth_.set_auth_factor_manager_for_testing(&auth_factor_manager_);
    userdataauth_.set_user_secret_stash_storage_for_testing(
        &user_secret_stash_storage_);
    userdataauth_.set_auth_session_manager(auth_session_manager_.get());
    userdataauth_.set_pkcs11_token_factory(&pkcs11_token_factory_);
    userdataauth_.set_user_activity_timestamp_manager(
        &user_activity_timestamp_manager_);
    userdataauth_.set_install_attrs(&install_attrs_);
    userdataauth_.set_mount_task_runner(
        task_environment_.GetMainThreadTaskRunner());
    userdataauth_.set_current_thread_id_for_test(
        UserDataAuth::TestThreadId::kMountThread);
    file_system_keyset_ = FileSystemKeyset::CreateRandom();
    PrepareDirectoryStructure();
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

  void AddUser(const std::string& name, const std::string& password) {
    std::string obfuscated = SanitizeUserName(name);
    brillo::SecureBlob passkey(password);
    Credentials credentials(name, passkey);

    UserInfo info = {name,
                     obfuscated,
                     passkey,
                     credentials,
                     UserPath(obfuscated),
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

  void KeysetSetUpWithoutKeyData() {
    for (UserInfo& user : users_) {
      FallbackVaultKeyset vk;
      vk.Initialize(&platform_, &crypto_);
      vk.CreateFromFileSystemKeyset(file_system_keyset_);
      ASSERT_TRUE(vk.Encrypt(user.passkey, user.obfuscated).ok());
      ASSERT_TRUE(
          vk.Save(user.homedir_path.Append(kKeyFile).AddExtension("0")));
      ASSERT_FALSE(vk.HasKeyData());
    }
  }

  base::test::TaskEnvironment task_environment_;
  NiceMock<MockPlatform> platform_;
  NiceMock<hwsec::MockCryptohomeFrontend> hwsec_;
  NiceMock<hwsec::MockPinWeaverFrontend> pinweaver_;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager_;
  Crypto crypto_;

  FileSystemKeyset file_system_keyset_;
  MockVaultKeysetFactory* mock_vault_keyset_factory_;
  std::unique_ptr<KeysetManagement> keyset_management_;
  NiceMock<MockHomeDirs> homedirs_;
  NiceMock<MockUserSessionFactory> user_session_factory_;
  std::unique_ptr<AuthBlockUtilityImpl> auth_block_utility_;
  AuthFactorManager auth_factor_manager_{&platform_};
  UserSecretStashStorage user_secret_stash_storage_{&platform_};
  std::unique_ptr<AuthSessionManager> auth_session_manager_;

  NiceMock<MockPkcs11TokenFactory> pkcs11_token_factory_;
  NiceMock<MockUserOldestActivityTimestampManager>
      user_activity_timestamp_manager_;
  NiceMock<MockInstallAttributes> install_attrs_;
  UserDataAuth userdataauth_;

  // Store user info for users that will be setup.
  std::vector<UserInfo> users_;
};

// This test checks if StartAuthSession can return keydataless keysets
// correctly.
TEST_F(AuthSessionTestWithKeysetManagement, StartAuthSessionWithoutKeyData) {
  AddUser(kUsername, kPassword);
  KeysetSetUpWithoutKeyData();

  user_data_auth::StartAuthSessionRequest start_auth_session_req;
  start_auth_session_req.mutable_account_id()->set_account_id(users_[0].name);
  user_data_auth::StartAuthSessionReply auth_session_reply;

  userdataauth_.StartAuthSession(
      start_auth_session_req,
      base::BindOnce(
          [](user_data_auth::StartAuthSessionReply* auth_reply_ptr,
             const user_data_auth::StartAuthSessionReply& reply) {
            *auth_reply_ptr = reply;
          },
          base::Unretained(&auth_session_reply)));

  EXPECT_EQ(auth_session_reply.error(),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  std::optional<base::UnguessableToken> auth_session_id =
      AuthSession::GetTokenFromSerializedString(
          auth_session_reply.auth_session_id());
  EXPECT_TRUE(auth_session_id.has_value());
  EXPECT_THAT(userdataauth_.auth_session_manager_->FindAuthSession(
                  auth_session_id.value()),
              NotNull());
  EXPECT_FALSE(auth_session_reply.key_label_data().empty());

  const auto& key_label_data = auth_session_reply.key_label_data();
  ASSERT_THAT(key_label_data, testing::SizeIs(1));
  EXPECT_THAT(key_label_data.begin()->first, "legacy-0");
}
}  // namespace cryptohome
