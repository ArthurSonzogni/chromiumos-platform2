// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>

#include <base/test/mock_callback.h>
#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <brillo/cryptohome.h>
#include <brillo/secure_blob.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libhwsec/frontend/cryptohome/mock_frontend.h>
#include <libhwsec/frontend/pinweaver/mock_frontend.h>
#include <libhwsec-foundation/error/testing_helper.h>

#include "cryptohome/auth_blocks/auth_block.h"
#include "cryptohome/auth_blocks/auth_block_utility_impl.h"
#include "cryptohome/auth_blocks/mock_auth_block_utility.h"
#include "cryptohome/auth_blocks/scrypt_auth_block.h"
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
#include "cryptohome/mock_vault_keyset_factory.h"
#include "cryptohome/pkcs11/mock_pkcs11_token_factory.h"
#include "cryptohome/storage/mock_homedirs.h"
#include "cryptohome/user_secret_stash_storage.h"
#include "cryptohome/user_session/mock_user_session.h"
#include "cryptohome/user_session/mock_user_session_factory.h"
#include "cryptohome/user_session/user_session_map.h"
#include "cryptohome/userdataauth.h"
#include "cryptohome/vault_keyset.h"

namespace cryptohome {

using ::testing::_;
using ::testing::NiceMock;
using ::testing::NotNull;
using ::testing::Return;

using base::test::TaskEnvironment;
using base::test::TestFuture;
using brillo::cryptohome::home::SanitizeUserName;
using cryptohome::error::CryptohomeCryptoError;
using hwsec_foundation::error::testing::IsOk;
using hwsec_foundation::error::testing::NotOk;
using hwsec_foundation::error::testing::ReturnValue;
using hwsec_foundation::status::OkStatus;

namespace {
constexpr char kUsername[] = "foo@example.com";
constexpr char kPassword[] = "password";
constexpr char kPasswordLabel[] = "label";
constexpr char kPassword2[] = "password2";
constexpr char kPasswordLabel2[] = "label2";
constexpr char kSalt[] = "salt";
constexpr char kPublicHash[] = "public key hash";
constexpr char kPublicHash2[] = "public key hash2";
constexpr int kAuthValueRounds = 5;
}  // namespace

namespace {
// TODO(b/233700483): Replace this with the mock auth block.
class FallbackVaultKeyset : public VaultKeyset {
 protected:
  std::unique_ptr<cryptohome::SyncAuthBlock> GetAuthBlockForCreation()
      const override {
    auto auth_block_for_creation = VaultKeyset::GetAuthBlockForCreation();
    if (!auth_block_for_creation) {
      return std::make_unique<ScryptAuthBlock>();
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
                /*recovery_hwsec=*/nullptr) {
    // Setting HWSec Expectations.
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

    ON_CALL(*mock_vault_keyset_factory_, NewBackup(&platform_, &crypto_))
        .WillByDefault([this](auto...) {
          auto* vk = new VaultKeyset();
          vk->InitializeAsBackup(&platform_, &crypto_);
          return vk;
        });

    keyset_management_ = std::make_unique<KeysetManagement>(
        &platform_, &crypto_, base::WrapUnique(mock_vault_keyset_factory_));
    auth_block_utility_ = std::make_unique<AuthBlockUtilityImpl>(
        keyset_management_.get(), &crypto_, &platform_,
        FingerprintAuthBlockService::MakeNullService());
    auth_session_manager_ = std::make_unique<AuthSessionManager>(
        &crypto_, &platform_, &user_session_map_, keyset_management_.get(),
        auth_block_utility_.get(), &auth_factor_manager_,
        &user_secret_stash_storage_);

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
    userdataauth_.set_auth_block_utility(auth_block_utility_.get());
    file_system_keyset_ = FileSystemKeyset::CreateRandom();
    AddUser(kUsername, kPassword);
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

  void AddFactor(AuthSession& auth_session,
                 const std::string& label,
                 const std::string& secret) {
    user_data_auth::AddAuthFactorRequest request;
    request.mutable_auth_factor()->set_type(
        user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
    request.mutable_auth_factor()->set_label(label);
    request.mutable_auth_factor()->mutable_password_metadata();
    request.mutable_auth_input()->mutable_password_input()->set_secret(secret);
    request.set_auth_session_id(auth_session.serialized_token());
    TestFuture<CryptohomeStatus> add_future;
    auth_session.AddAuthFactor(request, add_future.GetCallback());
    EXPECT_THAT(add_future.Get(), IsOk());
  }

  void UpdateFactor(AuthSession& auth_session,
                    const std::string& label,
                    const std::string& secret) {
    user_data_auth::UpdateAuthFactorRequest request;
    request.set_auth_session_id(auth_session.serialized_token());
    request.set_auth_factor_label(label);
    request.mutable_auth_factor()->set_type(
        user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
    request.mutable_auth_factor()->set_label(label);
    request.mutable_auth_factor()->mutable_password_metadata();
    request.mutable_auth_input()->mutable_password_input()->set_secret(secret);
    TestFuture<CryptohomeStatus> update_future;
    auth_session.UpdateAuthFactor(request, update_future.GetCallback());
    EXPECT_THAT(update_future.Get(), IsOk());
  }

  void AuthenticateFactor(AuthSession& auth_session,
                          const std::string& label,
                          const std::string& secret) {
    user_data_auth::AuthenticateAuthFactorRequest request;
    request.set_auth_factor_label(label);
    request.mutable_auth_input()->mutable_password_input()->set_secret(secret);
    request.set_auth_session_id(auth_session.serialized_token());
    TestFuture<CryptohomeStatus> authenticate_future;
    auth_session.AuthenticateAuthFactor(request,
                                        authenticate_future.GetCallback());
    EXPECT_THAT(authenticate_future.Get(), IsOk());
  }

  base::test::TaskEnvironment task_environment_;
  NiceMock<MockPlatform> platform_;
  NiceMock<hwsec::MockCryptohomeFrontend> hwsec_;
  NiceMock<hwsec::MockPinWeaverFrontend> pinweaver_;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager_;
  Crypto crypto_;
  UserSessionMap user_session_map_;

  FileSystemKeyset file_system_keyset_;
  MockVaultKeysetFactory* mock_vault_keyset_factory_;
  std::unique_ptr<KeysetManagement> keyset_management_;
  NiceMock<MockHomeDirs> homedirs_;
  NiceMock<MockUserSessionFactory> user_session_factory_;
  std::unique_ptr<AuthBlockUtilityImpl> auth_block_utility_;
  NiceMock<MockAuthBlockUtility> mock_auth_block_utility_;
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

// Test that creating user with USS and adding AuthFactors adds backup
// VautlKeyset
TEST_F(AuthSessionTestWithKeysetManagement, USSEnabledCreatesBackupVKs) {
  // Setup
  // Set the UserSecretStash experiment for testing to enable USS path in the
  // test
  SetUserSecretStashExperimentForTesting(/*enabled=*/true);

  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;

  AuthSession auth_session(
      kUsername, flags, AuthIntent::kDecrypt, /*on_timeout=*/base::DoNothing(),
      &crypto_, &platform_, &user_session_map_, keyset_management_.get(),
      auth_block_utility_.get(), &auth_factor_manager_,
      &user_secret_stash_storage_, /*enable_create_backup_vk_with_uss =*/true);

  // Test.
  EXPECT_THAT(AuthStatus::kAuthStatusFurtherFactorRequired,
              auth_session.GetStatus());
  EXPECT_TRUE(auth_session.OnUserCreated().ok());
  EXPECT_EQ(auth_session.GetStatus(), AuthStatus::kAuthStatusAuthenticated);

  // Add an initial and an additional factor
  AddFactor(auth_session, kPasswordLabel, kPassword);
  AddFactor(auth_session, kPasswordLabel2, kPassword2);

  // Verify
  std::unique_ptr<VaultKeyset> vk1 =
      keyset_management_->GetVaultKeyset(users_[0].obfuscated, kPasswordLabel);
  EXPECT_NE(vk1, nullptr);
  EXPECT_TRUE(vk1->IsForBackup());
  std::unique_ptr<VaultKeyset> vk2 =
      keyset_management_->GetVaultKeyset(users_[0].obfuscated, kPasswordLabel2);
  EXPECT_NE(vk2, nullptr);
  EXPECT_TRUE(vk2->IsForBackup());

  EXPECT_TRUE(auth_session.user_has_configured_auth_factor());
  EXPECT_FALSE(auth_session.user_has_configured_credential());

  SetUserSecretStashExperimentForTesting(/*enabled=*/false);
}

// Test that creating user and adding AuthFactors adds regular non-backup
// VautlKeysets if USS is not enabled
TEST_F(AuthSessionTestWithKeysetManagement, USSDisabledNotCreatesBackupVKs) {
  // Setup
  SetUserSecretStashExperimentForTesting(/*enabled=*/false);

  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;

  AuthSession auth_session(
      kUsername, flags, AuthIntent::kDecrypt, /*on_timeout=*/base::DoNothing(),
      &crypto_, &platform_, &user_session_map_, keyset_management_.get(),
      auth_block_utility_.get(), &auth_factor_manager_,
      &user_secret_stash_storage_, /*enable_create_backup_vk_with_uss =*/true);

  // Test.
  EXPECT_THAT(AuthStatus::kAuthStatusFurtherFactorRequired,
              auth_session.GetStatus());
  EXPECT_TRUE(auth_session.OnUserCreated().ok());
  EXPECT_EQ(auth_session.GetStatus(), AuthStatus::kAuthStatusAuthenticated);

  // Add an initial and an additional factor
  AddFactor(auth_session, kPasswordLabel, kPassword);
  AddFactor(auth_session, kPasswordLabel2, kPassword2);

  // Verify
  std::unique_ptr<VaultKeyset> vk1 =
      keyset_management_->GetVaultKeyset(users_[0].obfuscated, kPasswordLabel);
  EXPECT_NE(vk1, nullptr);
  EXPECT_FALSE(vk1->IsForBackup());
  std::unique_ptr<VaultKeyset> vk2 =
      keyset_management_->GetVaultKeyset(users_[0].obfuscated, kPasswordLabel2);
  EXPECT_NE(vk2, nullptr);
  EXPECT_FALSE(vk2->IsForBackup());
  EXPECT_TRUE(auth_session.user_has_configured_credential());
}

// Test that when user updates their credentials with USS backup VautlKeysets
// are kept as a backup.
TEST_F(AuthSessionTestWithKeysetManagement, USSEnabledUpdateBackupVKs) {
  // Setup
  // Set the UserSecretStash experiment for testing to enable USS path in the
  // test
  SetUserSecretStashExperimentForTesting(/*enabled=*/true);

  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;

  AuthSession auth_session(
      kUsername, flags, AuthIntent::kDecrypt, /*on_timeout=*/base::DoNothing(),
      &crypto_, &platform_, &user_session_map_, keyset_management_.get(),
      auth_block_utility_.get(), &auth_factor_manager_,
      &user_secret_stash_storage_, /*enable_create_backup_vk_with_uss =*/true);

  EXPECT_THAT(AuthStatus::kAuthStatusFurtherFactorRequired,
              auth_session.GetStatus());
  EXPECT_TRUE(auth_session.OnUserCreated().ok());
  EXPECT_EQ(auth_session.GetStatus(), AuthStatus::kAuthStatusAuthenticated);

  // Add an initial factor to USS and backup VK
  AddFactor(auth_session, kPasswordLabel, kPassword);
  std::unique_ptr<VaultKeyset> vk1 =
      keyset_management_->GetVaultKeyset(users_[0].obfuscated, kPasswordLabel);
  EXPECT_NE(vk1, nullptr);
  EXPECT_TRUE(vk1->IsForBackup());
  EXPECT_TRUE(auth_session.user_has_configured_auth_factor());
  EXPECT_FALSE(auth_session.user_has_configured_credential());

  // Test
  // Update the auth factor and see the backup VaultKeyset is still a backup.
  UpdateFactor(auth_session, kPasswordLabel, kPassword2);

  // Verify
  std::unique_ptr<VaultKeyset> vk2 =
      keyset_management_->GetVaultKeyset(users_[0].obfuscated, kPasswordLabel);
  EXPECT_NE(vk2, nullptr);
  EXPECT_TRUE(vk2->IsForBackup());
  EXPECT_TRUE(auth_session.user_has_configured_auth_factor());
  EXPECT_FALSE(auth_session.user_has_configured_credential());
}

// Test that authentication with backup VK succeeds when USS is rolled back
// after UpdateAuthFactor.
TEST_F(AuthSessionTestWithKeysetManagement,
       USSRollbackAuthWithUpdatedBackupVKSuccess) {
  // Setup
  // Set the UserSecretStash experiment for testing to enable USS path in the
  // test
  SetUserSecretStashExperimentForTesting(/*enabled=*/true);

  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;

  AuthSession auth_session(
      kUsername, flags, AuthIntent::kDecrypt, /*on_timeout=*/base::DoNothing(),
      &crypto_, &platform_, &user_session_map_, keyset_management_.get(),
      &mock_auth_block_utility_, &auth_factor_manager_,
      &user_secret_stash_storage_, /*enable_create_backup_vk_with_uss =*/true);

  EXPECT_THAT(AuthStatus::kAuthStatusFurtherFactorRequired,
              auth_session.GetStatus());
  EXPECT_TRUE(auth_session.OnUserCreated().ok());
  EXPECT_EQ(auth_session.GetStatus(), AuthStatus::kAuthStatusAuthenticated);

  EXPECT_CALL(mock_auth_block_utility_, GetAuthBlockTypeForCreation(_, _, _))
      .WillRepeatedly(Return(AuthBlockType::kTpmEcc));

  // Add an initial factor to USS and backup VK and update password.
  auto key_blobs = std::make_unique<KeyBlobs>();
  const brillo::SecureBlob kBlob32(32, 'A');
  const brillo::SecureBlob kBlob16(16, 'C');
  key_blobs->vkk_key = kBlob32;
  key_blobs->vkk_iv = kBlob16;
  key_blobs->chaps_iv = kBlob16;
  TpmEccAuthBlockState tpm_state = {
      .salt = brillo::SecureBlob(kSalt),
      .vkk_iv = kBlob32,
      .auth_value_rounds = kAuthValueRounds,
      .sealed_hvkkm = kBlob32,
      .extended_sealed_hvkkm = kBlob32,
      .tpm_public_key_hash = brillo::SecureBlob(kPublicHash),
  };
  auto auth_block_state = std::make_unique<AuthBlockState>();
  auth_block_state->state = tpm_state;
  EXPECT_CALL(mock_auth_block_utility_,
              CreateKeyBlobsWithAuthBlockAsync(_, _, _))
      .WillOnce([&key_blobs, &auth_block_state](
                    AuthBlockType auth_block_type, const AuthInput& auth_input,
                    AuthBlock::CreateCallback create_callback) {
        std::move(create_callback)
            .Run(OkStatus<CryptohomeCryptoError>(), std::move(key_blobs),
                 std::move(auth_block_state));
        return true;
      });
  AddFactor(auth_session, kPasswordLabel, kPassword);

  // KeyBlobs associated with second password.
  auto key_blobs2 = std::make_unique<KeyBlobs>();
  const brillo::SecureBlob kNewBlob32(32, 'B');
  const brillo::SecureBlob kNewBlob16(16, 'D');
  key_blobs2->vkk_key = kNewBlob32;
  key_blobs2->vkk_iv = kNewBlob16;
  key_blobs2->chaps_iv = kNewBlob16;
  TpmEccAuthBlockState tpm_state2 = {
      .salt = brillo::SecureBlob(kSalt),
      .vkk_iv = kNewBlob32,
      .auth_value_rounds = kAuthValueRounds,
      .sealed_hvkkm = kNewBlob32,
      .extended_sealed_hvkkm = kNewBlob32,
      .tpm_public_key_hash = brillo::SecureBlob(kPublicHash2),
  };
  auto auth_block_state2 = std::make_unique<AuthBlockState>();
  auth_block_state2->state = tpm_state2;
  EXPECT_CALL(mock_auth_block_utility_,
              CreateKeyBlobsWithAuthBlockAsync(_, _, _))
      .WillOnce([&key_blobs2, &auth_block_state2](
                    AuthBlockType auth_block_type, const AuthInput& auth_input,
                    AuthBlock::CreateCallback create_callback) {
        std::move(create_callback)
            .Run(OkStatus<CryptohomeCryptoError>(), std::move(key_blobs2),
                 std::move(auth_block_state2));
        return true;
      });
  UpdateFactor(auth_session, kPasswordLabel, kPassword2);

  std::unique_ptr<VaultKeyset> vk1 =
      keyset_management_->GetVaultKeyset(users_[0].obfuscated, kPasswordLabel);
  EXPECT_NE(vk1, nullptr);
  EXPECT_TRUE(vk1->IsForBackup());
  EXPECT_TRUE(auth_session.user_has_configured_auth_factor());
  EXPECT_FALSE(auth_session.user_has_configured_credential());

  // Test
  // See that authentication with the backup password succeeds
  // if USS is disabled after the update.
  SetUserSecretStashExperimentForTesting(/*enabled=*/false);
  AuthSession auth_session2(kUsername, flags, AuthIntent::kDecrypt,
                            /*on_timeout=*/base::DoNothing(), &crypto_,
                            &platform_, &user_session_map_,
                            keyset_management_.get(), &mock_auth_block_utility_,
                            &auth_factor_manager_, &user_secret_stash_storage_,
                            /*enable_create_backup_vk_with_uss =*/true);
  EXPECT_TRUE(auth_session2.user_has_configured_credential());
  EXPECT_FALSE(auth_session2.user_has_configured_auth_factor());

  EXPECT_CALL(mock_auth_block_utility_,
              GetAuthBlockStateFromVaultKeyset(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(mock_auth_block_utility_, GetAuthBlockTypeFromState(_))
      .WillRepeatedly(Return(AuthBlockType::kTpmEcc));

  // The same KeyBlobs associated with the second password.
  auto key_blobs3 = std::make_unique<KeyBlobs>();
  key_blobs3->vkk_key = kNewBlob32;
  key_blobs3->vkk_iv = kNewBlob16;
  key_blobs3->chaps_iv = kNewBlob16;
  EXPECT_CALL(mock_auth_block_utility_,
              DeriveKeyBlobsWithAuthBlockAsync(_, _, _, _))
      .WillOnce([&key_blobs3](AuthBlockType auth_block_type,
                              const AuthInput& auth_input,
                              const AuthBlockState& auth_state,
                              AuthBlock::DeriveCallback derive_callback) {
        std::move(derive_callback)
            .Run(OkStatus<CryptohomeCryptoError>(), std::move(key_blobs3));
        return true;
      });

  AuthenticateFactor(auth_session2, kPasswordLabel, kPassword2);

  // Verify
  EXPECT_EQ(auth_session2.GetStatus(), AuthStatus::kAuthStatusAuthenticated);
}

// Test that authentication with backup VK succeeds when USS is rolled back.
TEST_F(AuthSessionTestWithKeysetManagement,
       USSRollbackAuthWithBackupVKSuccess) {
  // Setup
  // Set the UserSecretStash experiment for testing to enable USS path in the
  // test
  SetUserSecretStashExperimentForTesting(/*enabled=*/true);

  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;

  AuthSession auth_session(
      kUsername, flags, AuthIntent::kDecrypt, /*on_timeout=*/base::DoNothing(),
      &crypto_, &platform_, &user_session_map_, keyset_management_.get(),
      &mock_auth_block_utility_, &auth_factor_manager_,
      &user_secret_stash_storage_, /*enable_create_backup_vk_with_uss =*/true);

  EXPECT_THAT(AuthStatus::kAuthStatusFurtherFactorRequired,
              auth_session.GetStatus());
  EXPECT_TRUE(auth_session.OnUserCreated().ok());
  EXPECT_EQ(auth_session.GetStatus(), AuthStatus::kAuthStatusAuthenticated);

  EXPECT_CALL(mock_auth_block_utility_, GetAuthBlockTypeForCreation(_, _, _))
      .WillOnce(Return(AuthBlockType::kTpmEcc));

  auto key_blobs = std::make_unique<KeyBlobs>();
  const brillo::SecureBlob kBlob32(32, 'A');
  const brillo::SecureBlob kBlob16(16, 'C');
  key_blobs->vkk_key = kBlob32;
  key_blobs->vkk_iv = kBlob16;
  key_blobs->chaps_iv = kBlob16;
  TpmEccAuthBlockState tpm_state = {
      .salt = brillo::SecureBlob(kSalt),
      .vkk_iv = kBlob32,
      .auth_value_rounds = kAuthValueRounds,
      .sealed_hvkkm = kBlob32,
      .extended_sealed_hvkkm = kBlob32,
      .tpm_public_key_hash = brillo::SecureBlob(kPublicHash),
  };
  auto auth_block_state = std::make_unique<AuthBlockState>();
  auth_block_state->state = tpm_state;
  EXPECT_CALL(mock_auth_block_utility_,
              CreateKeyBlobsWithAuthBlockAsync(_, _, _))
      .WillOnce([&key_blobs, &auth_block_state](
                    AuthBlockType auth_block_type, const AuthInput& auth_input,
                    AuthBlock::CreateCallback create_callback) {
        std::move(create_callback)
            .Run(OkStatus<CryptohomeCryptoError>(), std::move(key_blobs),
                 std::move(auth_block_state));
        return true;
      });
  // Add an initial factor to USS and backup VK
  AddFactor(auth_session, kPasswordLabel, kPassword);

  std::unique_ptr<VaultKeyset> vk1 =
      keyset_management_->GetVaultKeyset(users_[0].obfuscated, kPasswordLabel);
  EXPECT_NE(vk1, nullptr);
  EXPECT_TRUE(vk1->IsForBackup());
  EXPECT_TRUE(auth_session.user_has_configured_auth_factor());
  EXPECT_FALSE(auth_session.user_has_configured_credential());

  // Test
  // See that authentication with the backup password succeeds if USS is
  // disabled.
  SetUserSecretStashExperimentForTesting(/*enabled=*/false);
  AuthSession auth_session2(kUsername, flags, AuthIntent::kDecrypt,
                            /*on_timeout=*/base::DoNothing(), &crypto_,
                            &platform_, &user_session_map_,
                            keyset_management_.get(), &mock_auth_block_utility_,
                            &auth_factor_manager_, &user_secret_stash_storage_,
                            /*enable_create_backup_vk_with_uss =*/true);
  EXPECT_TRUE(auth_session2.user_has_configured_credential());
  EXPECT_FALSE(auth_session2.user_has_configured_auth_factor());

  EXPECT_CALL(mock_auth_block_utility_,
              GetAuthBlockStateFromVaultKeyset(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(mock_auth_block_utility_, GetAuthBlockTypeFromState(_))
      .WillRepeatedly(Return(AuthBlockType::kTpmEcc));

  auto key_blobs2 = std::make_unique<KeyBlobs>();
  key_blobs2->vkk_key = kBlob32;
  key_blobs2->vkk_iv = kBlob16;
  key_blobs2->chaps_iv = kBlob16;
  EXPECT_CALL(mock_auth_block_utility_,
              DeriveKeyBlobsWithAuthBlockAsync(_, _, _, _))
      .WillOnce([&key_blobs2](AuthBlockType auth_block_type,
                              const AuthInput& auth_input,
                              const AuthBlockState& auth_state,
                              AuthBlock::DeriveCallback derive_callback) {
        std::move(derive_callback)
            .Run(OkStatus<CryptohomeCryptoError>(), std::move(key_blobs2));
        return true;
      });

  AuthenticateFactor(auth_session2, kPasswordLabel, kPassword);

  // Verify
  EXPECT_EQ(auth_session2.GetStatus(), AuthStatus::kAuthStatusAuthenticated);
}

// Test that AuthSession list the non-backup VKs on session start
TEST_F(AuthSessionTestWithKeysetManagement, USSDisableddNotListBackupVKs) {
  // Setup
  SetUserSecretStashExperimentForTesting(/*enabled=*/false);

  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;
  AuthSession auth_session(
      kUsername, flags, AuthIntent::kDecrypt, /*on_timeout=*/base::DoNothing(),
      &crypto_, &platform_, &user_session_map_, keyset_management_.get(),
      auth_block_utility_.get(), &auth_factor_manager_,
      &user_secret_stash_storage_, /*enable_create_backup_vk_with_uss =*/true);
  EXPECT_THAT(AuthStatus::kAuthStatusFurtherFactorRequired,
              auth_session.GetStatus());
  EXPECT_TRUE(auth_session.OnUserCreated().ok());
  EXPECT_EQ(auth_session.GetStatus(), AuthStatus::kAuthStatusAuthenticated);
  // Add factors
  AddFactor(auth_session, kPasswordLabel, kPassword);
  AddFactor(auth_session, kPasswordLabel2, kPassword2);
  std::unique_ptr<VaultKeyset> vk1 =
      keyset_management_->GetVaultKeyset(users_[0].obfuscated, kPasswordLabel);
  EXPECT_NE(vk1, nullptr);
  std::unique_ptr<VaultKeyset> vk2 =
      keyset_management_->GetVaultKeyset(users_[0].obfuscated, kPasswordLabel2);
  EXPECT_NE(vk2, nullptr);

  // Test
  AuthSession auth_session2(
      kUsername, flags, AuthIntent::kDecrypt, /*on_timeout=*/base::DoNothing(),
      &crypto_, &platform_, &user_session_map_, keyset_management_.get(),
      auth_block_utility_.get(), &auth_factor_manager_,
      &user_secret_stash_storage_, /*enable_create_backup_vk_with_uss =*/true);

  // Verify
  EXPECT_FALSE(vk1->IsForBackup());
  EXPECT_FALSE(vk2->IsForBackup());
  EXPECT_TRUE(auth_session2.user_has_configured_credential());
  // Map contains the password, but when user_has_configured_credential is
  // false, this means factor are not backed by a non-backup VK
  EXPECT_NE(auth_session2.label_to_auth_factor().find(kPasswordLabel),
            auth_session2.label_to_auth_factor().end());
  EXPECT_NE(auth_session2.label_to_auth_factor().find(kPasswordLabel2),
            auth_session2.label_to_auth_factor().end());
}

// Test that AuthSession list the backup VKs on session start if USS is disabled
// after being enabled.
TEST_F(AuthSessionTestWithKeysetManagement, USSRollbackListBackupVKs) {
  // Setup
  SetUserSecretStashExperimentForTesting(/*enabled=*/true);

  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;
  AuthSession auth_session(
      kUsername, flags, AuthIntent::kDecrypt, /*on_timeout=*/base::DoNothing(),
      &crypto_, &platform_, &user_session_map_, keyset_management_.get(),
      auth_block_utility_.get(), &auth_factor_manager_,
      &user_secret_stash_storage_, /*enable_create_backup_vk_with_uss =*/true);
  EXPECT_THAT(AuthStatus::kAuthStatusFurtherFactorRequired,
              auth_session.GetStatus());
  EXPECT_TRUE(auth_session.OnUserCreated().ok());
  EXPECT_EQ(auth_session.GetStatus(), AuthStatus::kAuthStatusAuthenticated);
  // Add factors
  AddFactor(auth_session, kPasswordLabel, kPassword);
  AddFactor(auth_session, kPasswordLabel2, kPassword2);
  std::unique_ptr<VaultKeyset> vk1 =
      keyset_management_->GetVaultKeyset(users_[0].obfuscated, kPasswordLabel);
  EXPECT_NE(vk1, nullptr);
  std::unique_ptr<VaultKeyset> vk2 =
      keyset_management_->GetVaultKeyset(users_[0].obfuscated, kPasswordLabel2);
  EXPECT_NE(vk2, nullptr);
  EXPECT_TRUE(auth_session.user_has_configured_auth_factor());
  EXPECT_FALSE(auth_session.user_has_configured_credential());

  // Test
  SetUserSecretStashExperimentForTesting(/*enabled=*/false);
  AuthSession auth_session2(
      kUsername, flags, AuthIntent::kDecrypt, /*on_timeout=*/base::DoNothing(),
      &crypto_, &platform_, &user_session_map_, keyset_management_.get(),
      auth_block_utility_.get(), &auth_factor_manager_,
      &user_secret_stash_storage_, /*enable_create_backup_vk_with_uss =*/true);

  // Verify
  EXPECT_TRUE(vk1->IsForBackup());
  EXPECT_TRUE(vk2->IsForBackup());
  EXPECT_TRUE(auth_session2.user_has_configured_credential());

  EXPECT_NE(auth_session2.label_to_auth_factor().find(kPasswordLabel),
            auth_session2.label_to_auth_factor().end());
  EXPECT_NE(auth_session2.label_to_auth_factor().find(kPasswordLabel2),
            auth_session2.label_to_auth_factor().end());
}

}  // namespace cryptohome
