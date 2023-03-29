// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <base/files/file_path.h>
#include <base/functional/bind.h>
#include <base/memory/scoped_refptr.h>
#include <base/test/bind.h>
#include <base/test/mock_callback.h>
#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <brillo/cryptohome.h>
#include <brillo/secure_blob.h>
#include <cryptohome/proto_bindings/auth_factor.pb.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libhwsec/factory/factory_impl.h>
#include <libhwsec/frontend/cryptohome/mock_frontend.h>
#include <libhwsec/frontend/pinweaver/mock_frontend.h>
#include <libhwsec-foundation/error/testing_helper.h>

#include "cryptohome/auth_blocks/auth_block.h"
#include "cryptohome/auth_blocks/auth_block_utility_impl.h"
#include "cryptohome/auth_blocks/mock_auth_block_utility.h"
#include "cryptohome/auth_blocks/scrypt_auth_block.h"
#include "cryptohome/auth_factor/auth_factor_manager.h"
#include "cryptohome/auth_factor/auth_factor_storage_type.h"
#include "cryptohome/auth_factor/auth_factor_utils.h"
#include "cryptohome/auth_session_manager.h"
#include "cryptohome/challenge_credentials/mock_challenge_credentials_helper.h"
#include "cryptohome/cleanup/mock_user_oldest_activity_timestamp_manager.h"
#include "cryptohome/credentials.h"
#include "cryptohome/crypto.h"
#include "cryptohome/fake_features.h"
#include "cryptohome/features.h"
#include "cryptohome/filesystem_layout.h"
#include "cryptohome/key_objects.h"
#include "cryptohome/mock_cryptohome_keys_manager.h"
#include "cryptohome/mock_install_attributes.h"
#include "cryptohome/mock_key_challenge_service_factory.h"
#include "cryptohome/mock_le_credential_manager.h"
#include "cryptohome/mock_platform.h"
#include "cryptohome/mock_vault_keyset_factory.h"
#include "cryptohome/pkcs11/mock_pkcs11_token_factory.h"
#include "cryptohome/storage/mock_homedirs.h"
#include "cryptohome/user_secret_stash.h"
#include "cryptohome/user_secret_stash_storage.h"
#include "cryptohome/user_session/mock_user_session_factory.h"
#include "cryptohome/user_session/user_session_map.h"
#include "cryptohome/userdataauth.h"
#include "cryptohome/vault_keyset.h"

namespace cryptohome {
namespace {

using ::testing::_;
using ::testing::NiceMock;
using ::testing::NotNull;
using ::testing::Optional;
using ::testing::Return;

using base::test::TaskEnvironment;
using base::test::TestFuture;
using brillo::cryptohome::home::SanitizeUserName;
using cryptohome::error::CryptohomeError;
using hwsec_foundation::error::testing::IsOk;
using hwsec_foundation::error::testing::NotOk;
using hwsec_foundation::error::testing::ReturnValue;
using hwsec_foundation::status::OkStatus;

constexpr char kUsername[] = "foo@example.com";
constexpr char kPassword[] = "password";
constexpr char kPasswordLabel[] = "label";
constexpr char kPassword2[] = "password2";
constexpr char kPasswordLabel2[] = "label2";
constexpr char kDefaultLabel[] = "legacy-0";
constexpr char kSalt[] = "salt";
constexpr char kPublicHash[] = "public key hash";
constexpr int kAuthValueRounds = 5;

// Helper function to create a mock vault keyset with some useful default
// functions to create basic minimal VKs.
std::unique_ptr<VaultKeysetFactory> CreateMockVaultKeysetFactory() {
  auto factory = std::make_unique<MockVaultKeysetFactory>();
  ON_CALL(*factory, New(_, _))
      .WillByDefault([](Platform* platform, Crypto* crypto) {
        auto* vk = new VaultKeyset();
        vk->Initialize(platform, crypto);
        return vk;
      });
  ON_CALL(*factory, NewBackup(_, _))
      .WillByDefault([](Platform* platform, Crypto* crypto) {
        auto* vk = new VaultKeyset();
        vk->InitializeAsBackup(platform, crypto);
        return vk;
      });
  return factory;
}

}  // namespace

class AuthSessionTestWithKeysetManagement : public ::testing::Test {
 public:
  AuthSessionTestWithKeysetManagement() {
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
    crypto_.Init();

    auth_block_utility_.InitializeChallengeCredentialsHelper(
        &challenge_credentials_helper_, &key_challenge_service_factory_);
    auth_session_manager_ = std::make_unique<AuthSessionManager>(
        &crypto_, &platform_, &user_session_map_, &keyset_management_,
        &auth_block_utility_, &auth_factor_manager_,
        &user_secret_stash_storage_);
    // Initializing UserData class.
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
        task_environment_.GetMainThreadTaskRunner());
    userdataauth_.set_auth_block_utility(&auth_block_utility_);
    userdataauth_.set_features(&features_.object);
    file_system_keyset_ = FileSystemKeyset::CreateRandom();
    AddUser(kUsername, kPassword);
    PrepareDirectoryStructure();
  }

  ~AuthSessionTestWithKeysetManagement() override {
    // Reset USS experiment test flag.
    ResetUserSecretStashExperimentForTesting();
  }

 protected:
  struct UserInfo {
    Username username;
    ObfuscatedUsername obfuscated;
    brillo::SecureBlob passkey;
    Credentials credentials;
    base::FilePath homedir_path;
    base::FilePath user_path;
  };

  void AddUser(const std::string& name, const std::string& password) {
    Username username(name);
    ObfuscatedUsername obfuscated = SanitizeUserName(username);
    brillo::SecureBlob passkey(password);
    Credentials credentials(username, passkey);

    UserInfo info = {username,
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

  // Configures the mock Hwsec to simulate correct replies for authentication
  // (unsealing) requests.
  void SetUpHwsecAuthenticationMocks() {
    // When sealing, remember the secret and configure the unseal mock to return
    // it.
    EXPECT_CALL(hwsec_, SealWithCurrentUser(_, _, _))
        .WillRepeatedly([this](auto, auto, auto unsealed_value) {
          EXPECT_CALL(hwsec_, UnsealWithCurrentUser(_, _, _))
              .WillRepeatedly(ReturnValue(unsealed_value));
          return brillo::Blob();
        });
    EXPECT_CALL(hwsec_, PreloadSealedData(_))
        .WillRepeatedly(ReturnValue(std::nullopt));
  }

  void RemoveFactor(AuthSession& auth_session,
                    const std::string& label,
                    const std::string& secret) {
    user_data_auth::RemoveAuthFactorRequest request;
    request.set_auth_factor_label(label);
    request.set_auth_session_id(auth_session.serialized_token());
    TestFuture<CryptohomeStatus> remove_future;
    auth_session.RemoveAuthFactor(request, remove_future.GetCallback());
    EXPECT_THAT(remove_future.Get(), IsOk());
  }

  void KeysetSetUpWithoutKeyDataAndKeyBlobs() {
    for (auto& user : users_) {
      VaultKeyset vk;
      vk.Initialize(&platform_, &crypto_);
      vk.CreateFromFileSystemKeyset(file_system_keyset_);
      AuthBlockState auth_block_state = {
          .state = kTpmState,
      };

      ASSERT_THAT(vk.EncryptEx(kKeyBlobs, auth_block_state), IsOk());
      ASSERT_TRUE(
          vk.Save(user.homedir_path.Append(kKeyFile).AddExtension("0")));
    }
  }

  AuthSession StartAuthSessionWithMockAuthBlockUtility(
      bool enable_uss_migration) {
    AuthFactorVaultKeysetConverter converter{&keyset_management_};

    AuthFactorMap auth_factor_map =
        LoadAuthFactorMap(enable_uss_migration, users_[0].obfuscated, platform_,
                          converter, auth_factor_manager_);
    AuthSession::Params auth_session_params{
        .username = users_[0].username,
        .is_ephemeral_user = false,
        .intent = AuthIntent::kDecrypt,
        .timeout_timer = std::make_unique<base::WallClockTimer>(),
        .user_exists = true,
        .auth_factor_map = std::move(auth_factor_map),
        .migrate_to_user_secret_stash = enable_uss_migration};
    backing_apis_.auth_block_utility = &mock_auth_block_utility_;
    return AuthSession(std::move(auth_session_params), backing_apis_);
  }

  AuthSession StartAuthSession(bool enable_uss_migration) {
    AuthFactorVaultKeysetConverter converter{&keyset_management_};

    AuthFactorMap auth_factor_map =
        LoadAuthFactorMap(enable_uss_migration, users_[0].obfuscated, platform_,
                          converter, auth_factor_manager_);
    AuthSession::Params auth_session_params{
        .username = users_[0].username,
        .is_ephemeral_user = false,
        .intent = AuthIntent::kDecrypt,
        .timeout_timer = std::make_unique<base::WallClockTimer>(),
        .user_exists = true,
        .auth_factor_map = std::move(auth_factor_map),
        .migrate_to_user_secret_stash = enable_uss_migration};
    return AuthSession(std::move(auth_session_params), backing_apis_);
  }

  void AddFactorWithMockAuthBlockUtility(AuthSession& auth_session,
                                         const std::string& label,
                                         const std::string& secret) {
    EXPECT_CALL(mock_auth_block_utility_, GetAuthBlockTypeForCreation(_))
        .WillOnce(ReturnValue(AuthBlockType::kTpmEcc));
    auto key_blobs = std::make_unique<KeyBlobs>(kKeyBlobs);
    auto auth_block_state = std::make_unique<AuthBlockState>();
    auth_block_state->state = kTpmState;

    EXPECT_CALL(mock_auth_block_utility_, CreateKeyBlobsWithAuthBlock(_, _, _))
        .WillOnce([&key_blobs, &auth_block_state](
                      AuthBlockType auth_block_type,
                      const AuthInput& auth_input,
                      AuthBlock::CreateCallback create_callback) {
          std::move(create_callback)
              .Run(OkStatus<CryptohomeError>(), std::move(key_blobs),
                   std::move(auth_block_state));
          return true;
        });
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

  void AuthenticateAndMigrate(AuthSession& auth_session,
                              const std::string& label,
                              const std::string& secret) {
    EXPECT_CALL(mock_auth_block_utility_,
                GetAuthBlockStateFromVaultKeyset(_, _, _))
        .WillRepeatedly(Return(true));
    EXPECT_CALL(mock_auth_block_utility_, GetAuthBlockTypeFromState(_))
        .WillRepeatedly(Return(AuthBlockType::kTpmEcc));

    auto key_blobs2 = std::make_unique<KeyBlobs>(kKeyBlobs);
    EXPECT_CALL(mock_auth_block_utility_,
                DeriveKeyBlobsWithAuthBlock(_, _, _, _))
        .WillOnce([&key_blobs2](AuthBlockType auth_block_type,
                                const AuthInput& auth_input,
                                const AuthBlockState& auth_state,
                                AuthBlock::DeriveCallback derive_callback) {
          std::move(derive_callback)
              .Run(OkStatus<CryptohomeError>(), std::move(key_blobs2));
          return true;
        });
    auto key_blobs = std::make_unique<KeyBlobs>(kKeyBlobs);
    auto auth_block_state = std::make_unique<AuthBlockState>();
    auth_block_state->state = kTpmState;

    EXPECT_CALL(mock_auth_block_utility_, CreateKeyBlobsWithAuthBlock(_, _, _))
        .WillRepeatedly([&key_blobs, &auth_block_state](
                            AuthBlockType auth_block_type,
                            const AuthInput& auth_input,
                            AuthBlock::CreateCallback create_callback) {
          std::move(create_callback)
              .Run(OkStatus<CryptohomeError>(), std::move(key_blobs),
                   std::move(auth_block_state));
          return true;
        });
    std::string auth_factor_labels[] = {label};
    user_data_auth::AuthInput auth_input_proto;
    auth_input_proto.mutable_password_input()->set_secret(secret);
    TestFuture<CryptohomeStatus> authenticate_future;
    auth_session.AuthenticateAuthFactor(auth_factor_labels, auth_input_proto,
                                        authenticate_future.GetCallback());
    EXPECT_THAT(authenticate_future.Get(), IsOk());
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
    std::string auth_factor_labels[] = {label};
    user_data_auth::AuthInput auth_input_proto;
    auth_input_proto.mutable_password_input()->set_secret(secret);
    TestFuture<CryptohomeStatus> authenticate_future;
    auth_session.AuthenticateAuthFactor(auth_factor_labels, auth_input_proto,
                                        authenticate_future.GetCallback());
    EXPECT_THAT(authenticate_future.Get(), IsOk());
  }

  // Standard key blob and TPM state objects to use in testing.
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

  base::test::TaskEnvironment task_environment_;

  // Mocks and fakes for the test AuthSessions to use.
  NiceMock<hwsec::MockCryptohomeFrontend> hwsec_;
  NiceMock<hwsec::MockPinWeaverFrontend> pinweaver_;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager_;
  Crypto crypto_{&hwsec_, &pinweaver_, &cryptohome_keys_manager_,
                 /*recovery_hwsec=*/nullptr};
  NiceMock<MockPlatform> platform_;
  UserSessionMap user_session_map_;
  KeysetManagement keyset_management_{&platform_, &crypto_,
                                      CreateMockVaultKeysetFactory()};
  FakeFeaturesForTesting features_;
  AuthBlockUtilityImpl auth_block_utility_{
      &keyset_management_,
      &crypto_,
      &platform_,
      &features_.async,
      FingerprintAuthBlockService::MakeNullService(),
      BiometricsAuthBlockService::NullGetter()};
  NiceMock<MockAuthBlockUtility> mock_auth_block_utility_;
  AuthFactorManager auth_factor_manager_{&platform_};
  UserSecretStashStorage user_secret_stash_storage_{&platform_};
  AuthSession::BackingApis backing_apis_{&crypto_,
                                         &platform_,
                                         &user_session_map_,
                                         &keyset_management_,
                                         &auth_block_utility_,
                                         &auth_factor_manager_,
                                         &user_secret_stash_storage_};

  // An AuthSession manager for testing managed creation.
  std::unique_ptr<AuthSessionManager> auth_session_manager_;

  FileSystemKeyset file_system_keyset_;
  MockVaultKeysetFactory* mock_vault_keyset_factory_;
  NiceMock<MockHomeDirs> homedirs_;
  NiceMock<MockUserSessionFactory> user_session_factory_;
  NiceMock<MockChallengeCredentialsHelper> challenge_credentials_helper_;
  NiceMock<MockKeyChallengeServiceFactory> key_challenge_service_factory_;

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
  KeysetSetUpWithoutKeyDataAndKeyBlobs();

  user_data_auth::StartAuthSessionRequest start_auth_session_req;
  start_auth_session_req.mutable_account_id()->set_account_id(
      *users_[0].username);
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
  InUseAuthSession auth_session =
      userdataauth_.auth_session_manager_->FindAuthSession(
          auth_session_id.value());
  EXPECT_TRUE(auth_session.AuthSessionStatus().ok());
}

// Test that a VaultKeyset without KeyData migration succeeds during login.
TEST_F(AuthSessionTestWithKeysetManagement,
       MigrationToUssWithNoKeyDataAndNewFactor) {
  // Setup
  // UserSecretStash is not enabled, setup VaultKeysets for the user.
  SetUserSecretStashExperimentForTesting(/*enabled=*/false);
  SetUpHwsecAuthenticationMocks();
  AuthInput auth_input = {
      .user_input = brillo::SecureBlob(kPassword),
      .locked_to_single_user = std::nullopt,
      .username = users_[0].username,
      .obfuscated_username = users_[0].obfuscated,
  };
  auth_block_utility_.CreateKeyBlobsWithAuthBlock(
      AuthBlockType::kTpmEcc, auth_input,
      base::BindLambdaForTesting(
          [&](CryptohomeStatus error, std::unique_ptr<KeyBlobs> key_blobs,
              std::unique_ptr<AuthBlockState> auth_block_state) {
            ASSERT_THAT(error, IsOk());
            VaultKeyset vk;
            vk.Initialize(&platform_, &crypto_);
            KeyData key_data;
            key_data.set_label(kDefaultLabel);
            vk.SetKeyData(key_data);
            vk.CreateFromFileSystemKeyset(file_system_keyset_);
            ASSERT_THAT(vk.EncryptEx(*key_blobs, *auth_block_state), IsOk());
            ASSERT_TRUE(vk.Save(
                users_[0].homedir_path.Append(kKeyFile).AddExtension("0")));
          }));
  // Set the UserSecretStash experiment for testing to enable USS
  // migration with the authentication.
  SetUserSecretStashExperimentForTesting(/*enabled=*/true);
  AuthSession auth_session1 = StartAuthSession(/*enable_uss_migration=*/true);
  ASSERT_EQ(auth_session1.auth_factor_map().Find(kDefaultLabel)->storage_type(),
            AuthFactorStorageType::kVaultKeyset);
  // Test that authenticating the password should migrate VaultKeyset to
  // UserSecretStash, converting the VaultKeyset to a backup VaultKeyset.
  AuthenticateFactor(auth_session1, kDefaultLabel, kPassword);
  ASSERT_EQ(auth_session1.auth_factor_map().Find(kDefaultLabel)->storage_type(),
            AuthFactorStorageType::kUserSecretStash);

  // Verify that the vault_keysets still exist and converted to backup and
  // migrated VaultKeysets.
  std::unique_ptr<VaultKeyset> vk1 =
      keyset_management_.GetVaultKeyset(users_[0].obfuscated, kDefaultLabel);
  ASSERT_NE(vk1, nullptr);
  ASSERT_TRUE(vk1->IsForBackup());
  ASSERT_TRUE(vk1->IsMigrated());

  // Verify that migrator created the user_secret_stash and uss_main_key.
  ASSERT_TRUE(auth_session1.has_user_secret_stash());

  // Verify that the authentication succeeds after migration.
  AuthSession auth_session2 = StartAuthSession(/*enable_uss_migration=*/true);
  EXPECT_THAT(AuthStatus::kAuthStatusFurtherFactorRequired,
              auth_session2.status());
  ASSERT_EQ(auth_session2.auth_factor_map().Find(kDefaultLabel)->storage_type(),
            AuthFactorStorageType::kUserSecretStash);
  AuthenticateFactor(auth_session2, kDefaultLabel, kPassword);

  // Turn on the migration again and test that adding a new keyset succeeds.
  AuthSession auth_session4 = StartAuthSession(/*enable_uss_migration=*/true);
  ASSERT_EQ(auth_session4.auth_factor_map().Find(kDefaultLabel)->storage_type(),
            AuthFactorStorageType::kUserSecretStash);
  AuthenticateFactor(auth_session4, kDefaultLabel, kPassword);
  AddFactor(auth_session4, kPasswordLabel2, kPassword2);
  ASSERT_EQ(
      auth_session4.auth_factor_map().Find(kPasswordLabel2)->storage_type(),
      AuthFactorStorageType::kUserSecretStash);
  // Verify authentication works with the added keyset.
  AuthenticateFactor(auth_session4, kPasswordLabel2, kPassword2);
}

// Test that a VaultKeyset without KeyData migration succeeds during login.
TEST_F(AuthSessionTestWithKeysetManagement, MigrationToUssWithNoKeyData) {
  // Setup
  // UserSecretStash is not enabled, setup VaultKeysets for the user.
  SetUserSecretStashExperimentForTesting(/*enabled=*/false);
  KeysetSetUpWithoutKeyDataAndKeyBlobs();

  // Create an AuthSession and set the USS migration enabled.
  SetUserSecretStashExperimentForTesting(/*enabled=*/true);
  AuthSession auth_session =
      StartAuthSessionWithMockAuthBlockUtility(/*enable_uss_migration=*/true);
  EXPECT_THAT(AuthStatus::kAuthStatusFurtherFactorRequired,
              auth_session.status());

  // Test that authenticating the password should migrate VaultKeyset to
  // UserSecretStash, converting the VaultKeyset to a backup VaultKeyset.
  EXPECT_TRUE(auth_session.auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kVaultKeyset));
  EXPECT_FALSE(auth_session.auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kUserSecretStash));
  AuthenticateAndMigrate(auth_session, kDefaultLabel, kPassword);

  // Verify that the vault_keysets still exist and converted to backup and
  // migrated VaultKeysets.
  std::unique_ptr<VaultKeyset> vk1 =
      keyset_management_.GetVaultKeyset(users_[0].obfuscated, kDefaultLabel);
  ASSERT_NE(vk1, nullptr);
  ASSERT_TRUE(vk1->IsForBackup());
  ASSERT_TRUE(vk1->IsMigrated());

  // Verify that migrator created the user_secret_stash and uss_main_key.
  UserSecretStashStorage uss_storage(&platform_);
  CryptohomeStatusOr<brillo::Blob> uss_serialized_container_status =
      uss_storage.LoadPersisted(users_[0].obfuscated);
  ASSERT_TRUE(uss_serialized_container_status.ok());
  std::optional<brillo::SecureBlob> uss_credential_secret =
      kKeyBlobs.DeriveUssCredentialSecret();
  ASSERT_TRUE(uss_credential_secret.has_value());
  brillo::SecureBlob decrypted_main_key;
  CryptohomeStatusOr<std::unique_ptr<UserSecretStash>>
      user_secret_stash_status =
          UserSecretStash::FromEncryptedContainerWithWrappingKey(
              *uss_serialized_container_status, kDefaultLabel,
              *uss_credential_secret, &decrypted_main_key);
  ASSERT_TRUE(user_secret_stash_status.ok());
  // Verify that the user_secret_stash has the wrapped_key_block for
  // the default label.
  ASSERT_TRUE(
      user_secret_stash_status.value()->HasWrappedMainKey(kDefaultLabel));
  //  Verify that the AuthFactors are created for the AuthFactor labels and
  //  storage type is updated in the AuthFactor map for each of them.
  std::map<std::string, std::unique_ptr<AuthFactor>> factor_map =
      auth_factor_manager_.LoadAllAuthFactors(users_[0].obfuscated);
  ASSERT_NE(factor_map.find(kDefaultLabel), factor_map.end());
  ASSERT_EQ(auth_session.auth_factor_map().Find(kDefaultLabel)->storage_type(),
            AuthFactorStorageType::kUserSecretStash);

  // Verify that the authentication succeeds after migration.
  AuthSession auth_session2 =
      StartAuthSessionWithMockAuthBlockUtility(/*enable_uss_migration=*/true);
  EXPECT_THAT(AuthStatus::kAuthStatusFurtherFactorRequired,
              auth_session2.status());

  // Test that authenticating the password should migrate VaultKeyset to
  // UserSecretStash, converting the VaultKeyset to a backup VaultKeyset.
  EXPECT_FALSE(auth_session2.auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kVaultKeyset));
  EXPECT_TRUE(auth_session2.auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kUserSecretStash));
  AuthenticateAndMigrate(auth_session2, kDefaultLabel, kPassword);
}

// Test that when UpdateAuthFactor is called, both VK and AuthFactor are removed
// for partially migrated users.
TEST_F(AuthSessionTestWithKeysetManagement, MigrationEnabledUpdateBackup) {
  if (!USE_USS_MIGRATION) {
    GTEST_SKIP() << "Skipped because this test is valid only when USS "
                    "migration is enabled.";
  }
  // Setup
  // UserSecretStash is not enabled, setup VaultKeysets for the user.
  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;
  // Attach the mock_auth_block_utility to our AuthSessionManager and created
  // AuthSession.
  SetUserSecretStashExperimentForTesting(/*enabled=*/false);
  auto auth_session_manager_impl_ = std::make_unique<AuthSessionManager>(
      &crypto_, &platform_, &user_session_map_, &keyset_management_,
      &mock_auth_block_utility_, &auth_factor_manager_,
      &user_secret_stash_storage_);
  CryptohomeStatusOr<InUseAuthSession> auth_session_status =
      auth_session_manager_impl_->CreateAuthSession(Username(kUsername), flags,
                                                    AuthIntent::kDecrypt);
  EXPECT_TRUE(auth_session_status.ok());
  AuthSession* auth_session = auth_session_status.value().Get();
  EXPECT_THAT(AuthStatus::kAuthStatusFurtherFactorRequired,
              auth_session->status());
  EXPECT_TRUE(auth_session->OnUserCreated().ok());
  EXPECT_EQ(auth_session->status(), AuthStatus::kAuthStatusAuthenticated);
  // Add the first factor with VaultKeyset backing.
  AddFactorWithMockAuthBlockUtility(*auth_session, kPasswordLabel, kPassword);
  AddFactorWithMockAuthBlockUtility(*auth_session, kPasswordLabel2, kPassword2);
  // Set the UserSecretStash experiment for testing to enable USS
  // migration with the authentication.
  SetUserSecretStashExperimentForTesting(/*enabled=*/true);

  // Test that authenticating the password should migrate VaultKeyset to
  // UserSecretStash, converting the VaultKeyset to a backup VaultKeyset.
  CryptohomeStatusOr<InUseAuthSession> auth_session2_status =
      auth_session_manager_impl_->CreateAuthSession(Username(kUsername), flags,
                                                    AuthIntent::kDecrypt);
  EXPECT_TRUE(auth_session2_status.ok());
  AuthSession* auth_session2 = auth_session2_status.value().Get();
  EXPECT_TRUE(auth_session2->auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kVaultKeyset));
  EXPECT_FALSE(auth_session2->auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kUserSecretStash));
  AuthenticateAndMigrate(*auth_session2, kPasswordLabel, kPassword);

  // Verify that migrator loaded the user_secret_stash and uss_main_key.
  UserSecretStashStorage uss_storage(&platform_);
  CryptohomeStatusOr<brillo::Blob> uss_serialized_container_status =
      uss_storage.LoadPersisted(users_[0].obfuscated);
  ASSERT_TRUE(uss_serialized_container_status.ok());
  std::optional<brillo::SecureBlob> uss_credential_secret =
      kKeyBlobs.DeriveUssCredentialSecret();
  ASSERT_TRUE(uss_credential_secret.has_value());
  brillo::SecureBlob decrypted_main_key;
  CryptohomeStatusOr<std::unique_ptr<UserSecretStash>>
      user_secret_stash_status =
          UserSecretStash::FromEncryptedContainerWithWrappingKey(
              *uss_serialized_container_status, kPasswordLabel,
              *uss_credential_secret, &decrypted_main_key);
  ASSERT_TRUE(user_secret_stash_status.ok());

  // Verify that the user_secret_stash has the wrapped_key_blocks for the
  // AuthFactor label.
  ASSERT_TRUE(
      user_secret_stash_status.value()->HasWrappedMainKey(kPasswordLabel));
  //  Verify that the AuthFactors are created for the AuthFactor labels and
  //  storage type is updated in the AuthFactor map for each of them.
  std::map<std::string, std::unique_ptr<AuthFactor>> factor_map =
      auth_factor_manager_.LoadAllAuthFactors(users_[0].obfuscated);
  ASSERT_NE(factor_map.find(kPasswordLabel), factor_map.end());
  ASSERT_NE(factor_map.find(kPasswordLabel2), factor_map.end());
  ASSERT_EQ(
      auth_session2->auth_factor_map().Find(kPasswordLabel)->storage_type(),
      AuthFactorStorageType::kUserSecretStash);
  ASSERT_EQ(
      auth_session2->auth_factor_map().Find(kPasswordLabel2)->storage_type(),
      AuthFactorStorageType::kVaultKeyset);
  // Verify that the vault_keysets still exist and converted to migrated
  // VaultKeysets.
  std::unique_ptr<VaultKeyset> vk1 =
      keyset_management_.GetVaultKeyset(users_[0].obfuscated, kPasswordLabel);
  EXPECT_NE(vk1, nullptr);
  EXPECT_TRUE(vk1->IsForBackup());
  EXPECT_TRUE(vk1->IsMigrated());
  std::unique_ptr<VaultKeyset> vk2 =
      keyset_management_.GetVaultKeyset(users_[0].obfuscated, kPasswordLabel2);
  EXPECT_NE(vk2, nullptr);
  EXPECT_FALSE(vk2->IsForBackup());
  EXPECT_FALSE(vk2->IsMigrated());

  // Test
  CryptohomeStatusOr<InUseAuthSession> auth_session3_status =
      auth_session_manager_impl_->CreateAuthSession(Username(kUsername), flags,
                                                    AuthIntent::kDecrypt);
  EXPECT_TRUE(auth_session3_status.ok());
  AuthSession* auth_session3 = auth_session3_status.value().Get();

  // Update the auth factor and see the backup VaultKeyset is still a backup.
  UpdateFactor(*auth_session3, kPasswordLabel, kPassword2);

  // Verify
  std::unique_ptr<VaultKeyset> vk3 =
      keyset_management_.GetVaultKeyset(users_[0].obfuscated, kPasswordLabel);
  EXPECT_NE(vk3, nullptr);
  EXPECT_TRUE(vk3->IsForBackup());
  EXPECT_TRUE(auth_session3->auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kUserSecretStash));
  EXPECT_FALSE(auth_session3->auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kVaultKeyset));

  // Verify that on AuthSession start it lists the USS-AuthFactors.
  CryptohomeStatusOr<InUseAuthSession> auth_session4_status =
      auth_session_manager_->CreateAuthSession(
          Username(kUsername),
          user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE,
          AuthIntent::kDecrypt);
  EXPECT_TRUE(auth_session4_status.ok());
  AuthSession* auth_session4 = auth_session4_status.value().Get();
  EXPECT_TRUE(auth_session4->auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kUserSecretStash));
  EXPECT_FALSE(auth_session4->auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kVaultKeyset));
}

// Test that VaultKeysets are migrated to UserSecretStash when migration is
// enabled, converting the existing VaultKeysets to migrated VaultKeysets.
TEST_F(AuthSessionTestWithKeysetManagement, MigrationEnabledMigratesToUss) {
  // Setup
  // UserSecretStash is not enabled, setup VaultKeysets for the user.
  SetUserSecretStashExperimentForTesting(/*enabled=*/false);

  // Attach the mock_auth_block_utility to our AuthSessionManager and created
  // AuthSession.
  AuthSession auth_session =
      StartAuthSessionWithMockAuthBlockUtility(/*enable_uss_migration=*/false);
  EXPECT_THAT(AuthStatus::kAuthStatusFurtherFactorRequired,
              auth_session.status());
  EXPECT_TRUE(auth_session.OnUserCreated().ok());
  EXPECT_EQ(auth_session.status(), AuthStatus::kAuthStatusAuthenticated);
  // Add the first factor with VaultKeyset backing.
  AddFactorWithMockAuthBlockUtility(auth_session, kPasswordLabel, kPassword);
  AddFactorWithMockAuthBlockUtility(auth_session, kPasswordLabel2, kPassword2);

  // Set the UserSecretStash and USS migration for testing to enable USS
  // migration with the authentication.
  SetUserSecretStashExperimentForTesting(/*enabled=*/true);
  AuthSession auth_session2 =
      StartAuthSessionWithMockAuthBlockUtility(/*enable_uss_migration=*/true);
  EXPECT_TRUE(auth_session2.auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kVaultKeyset));
  EXPECT_FALSE(auth_session2.auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kUserSecretStash));
  // Test that authenticating the password should migrate VaultKeyset to
  // UserSecretStash, converting the VaultKeyset to a backup VaultKeyset.
  AuthenticateAndMigrate(auth_session2, kPasswordLabel, kPassword);
  AuthSession auth_session3 =
      StartAuthSessionWithMockAuthBlockUtility(/*enable_uss_migration=*/true);
  AuthenticateAndMigrate(auth_session3, kPasswordLabel2, kPassword2);

  // Verify
  // Verify that migrator loaded the user_secret_stash and uss_main_key.
  UserSecretStashStorage uss_storage(&platform_);
  CryptohomeStatusOr<brillo::Blob> uss_serialized_container_status =
      uss_storage.LoadPersisted(users_[0].obfuscated);
  ASSERT_TRUE(uss_serialized_container_status.ok());
  std::optional<brillo::SecureBlob> uss_credential_secret =
      kKeyBlobs.DeriveUssCredentialSecret();
  ASSERT_TRUE(uss_credential_secret.has_value());
  brillo::SecureBlob decrypted_main_key;
  CryptohomeStatusOr<std::unique_ptr<UserSecretStash>>
      user_secret_stash_status =
          UserSecretStash::FromEncryptedContainerWithWrappingKey(
              *uss_serialized_container_status, kPasswordLabel,
              *uss_credential_secret, &decrypted_main_key);
  ASSERT_TRUE(user_secret_stash_status.ok());

  // Verify that the user_secret_stash has the wrapped_key_blocks for the
  // AuthFactor labels.
  ASSERT_TRUE(
      user_secret_stash_status.value()->HasWrappedMainKey(kPasswordLabel));
  ASSERT_TRUE(
      user_secret_stash_status.value()->HasWrappedMainKey(kPasswordLabel2));
  //  Verify that the AuthFactors are created for the AuthFactor labels and
  //  storage type is updated in the AuthFactor map for each of them.
  std::map<std::string, std::unique_ptr<AuthFactor>> factor_map =
      auth_factor_manager_.LoadAllAuthFactors(users_[0].obfuscated);
  ASSERT_NE(factor_map.find(kPasswordLabel), factor_map.end());
  ASSERT_NE(factor_map.find(kPasswordLabel2), factor_map.end());
  ASSERT_EQ(
      auth_session3.auth_factor_map().Find(kPasswordLabel)->storage_type(),
      AuthFactorStorageType::kUserSecretStash);
  ASSERT_EQ(
      auth_session3.auth_factor_map().Find(kPasswordLabel2)->storage_type(),
      AuthFactorStorageType::kUserSecretStash);
  // Verify that the vault_keysets still exist and converted to migrated
  // VaultKeysets.
  std::unique_ptr<VaultKeyset> vk1 =
      keyset_management_.GetVaultKeyset(users_[0].obfuscated, kPasswordLabel);
  EXPECT_NE(vk1, nullptr);
  EXPECT_TRUE(vk1->IsForBackup());
  EXPECT_TRUE(vk1->IsMigrated());
  std::unique_ptr<VaultKeyset> vk2 =
      keyset_management_.GetVaultKeyset(users_[0].obfuscated, kPasswordLabel2);
  EXPECT_NE(vk2, nullptr);
  EXPECT_TRUE(vk2->IsForBackup());
  EXPECT_TRUE(vk2->IsMigrated());
}

// Test that after a VaultKeyset is migrated to UserSecretStash the next
// factor is added as migrated VaultKeysets.
TEST_F(AuthSessionTestWithKeysetManagement,
       MigrationEnabledAddNextFactorsToUss) {
  // Setup
  // UserSecretStash is not enabled, setup VaultKeysets for the user.
  SetUserSecretStashExperimentForTesting(/*enabled=*/false);
  AuthSession auth_session =
      StartAuthSessionWithMockAuthBlockUtility(/*enable_uss_migration=*/false);
  EXPECT_THAT(AuthStatus::kAuthStatusFurtherFactorRequired,
              auth_session.status());
  EXPECT_TRUE(auth_session.OnUserCreated().ok());
  EXPECT_EQ(auth_session.status(), AuthStatus::kAuthStatusAuthenticated);
  // Add the first factor with VaultKeyset backing.
  AddFactorWithMockAuthBlockUtility(auth_session, kPasswordLabel, kPassword);

  // Set the UserSecretStash and USS migration for testing to enable USS
  // migration with the authentication.
  SetUserSecretStashExperimentForTesting(/*enabled=*/true);
  AuthSession auth_session2 =
      StartAuthSessionWithMockAuthBlockUtility(/*enable_uss_migration=*/true);
  EXPECT_TRUE(auth_session2.auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kVaultKeyset));
  EXPECT_FALSE(auth_session2.auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kUserSecretStash));
  // Test that authenticating the password should migrate VaultKeyset to
  // UserSecretStash, converting the VaultKeyset to a backup VaultKeyset.
  AuthenticateAndMigrate(auth_session2, kPasswordLabel, kPassword);

  // Verify that the vault_keysets still exist and converted to backup and
  // migrated VaultKeysets.
  std::unique_ptr<VaultKeyset> vk1 =
      keyset_management_.GetVaultKeyset(users_[0].obfuscated, kPasswordLabel);
  ASSERT_NE(vk1, nullptr);
  ASSERT_TRUE(vk1->IsForBackup());
  ASSERT_TRUE(vk1->IsMigrated());

  // Test that adding a second factor adds as a USS AuthFactor.
  AddFactorWithMockAuthBlockUtility(auth_session2, kPasswordLabel2, kPassword2);

  // Verify
  // Create a new AuthSession for verifications.
  AuthSession auth_session3 =
      StartAuthSessionWithMockAuthBlockUtility(/*enable_uss_migration=*/true);
  AuthenticateAndMigrate(auth_session3, kPasswordLabel2, kPassword2);

  // Verify that migrator created the user_secret_stash and uss_main_key.
  UserSecretStashStorage uss_storage(&platform_);
  CryptohomeStatusOr<brillo::Blob> uss_serialized_container_status =
      uss_storage.LoadPersisted(users_[0].obfuscated);
  ASSERT_TRUE(uss_serialized_container_status.ok());
  std::optional<brillo::SecureBlob> uss_credential_secret =
      kKeyBlobs.DeriveUssCredentialSecret();
  ASSERT_TRUE(uss_credential_secret.has_value());
  brillo::SecureBlob decrypted_main_key;
  CryptohomeStatusOr<std::unique_ptr<UserSecretStash>>
      user_secret_stash_status =
          UserSecretStash::FromEncryptedContainerWithWrappingKey(
              *uss_serialized_container_status, kPasswordLabel,
              *uss_credential_secret, &decrypted_main_key);
  ASSERT_TRUE(user_secret_stash_status.ok());
  // Verify that the user_secret_stash has the wrapped_key_blocks for both
  // AuthFactor labels.
  ASSERT_TRUE(
      user_secret_stash_status.value()->HasWrappedMainKey(kPasswordLabel));
  ASSERT_TRUE(
      user_secret_stash_status.value()->HasWrappedMainKey(kPasswordLabel2));
  //  Verify that the AuthFactors are created for the AuthFactor labels and
  //  storage type is updated in the AuthFactor map for each of them.
  std::map<std::string, std::unique_ptr<AuthFactor>> factor_map =
      auth_factor_manager_.LoadAllAuthFactors(users_[0].obfuscated);
  ASSERT_NE(factor_map.find(kPasswordLabel), factor_map.end());
  ASSERT_NE(factor_map.find(kPasswordLabel2), factor_map.end());
  ASSERT_EQ(
      auth_session3.auth_factor_map().Find(kPasswordLabel)->storage_type(),
      AuthFactorStorageType::kUserSecretStash);
  ASSERT_EQ(
      auth_session3.auth_factor_map().Find(kPasswordLabel2)->storage_type(),
      AuthFactorStorageType::kUserSecretStash);
}

// Test that AuthSession's auth factor map lists the factor from right backing
// store during the migration.
TEST_F(AuthSessionTestWithKeysetManagement,
       AuthFactorMapStatusDuringMigration) {
  // Setup
  SetUserSecretStashExperimentForTesting(/*enabled=*/false);
  AuthSession auth_session =
      StartAuthSessionWithMockAuthBlockUtility(/*enable_uss_migration=*/false);
  EXPECT_THAT(AuthStatus::kAuthStatusFurtherFactorRequired,
              auth_session.status());
  EXPECT_TRUE(auth_session.OnUserCreated().ok());
  EXPECT_EQ(auth_session.status(), AuthStatus::kAuthStatusAuthenticated);
  EXPECT_FALSE(auth_session.auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kVaultKeyset));
  EXPECT_FALSE(auth_session.auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kUserSecretStash));

  AddFactorWithMockAuthBlockUtility(auth_session, kPasswordLabel, kPassword);
  AddFactorWithMockAuthBlockUtility(auth_session, kPasswordLabel2, kPassword2);
  EXPECT_TRUE(auth_session.auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kVaultKeyset));
  EXPECT_FALSE(auth_session.auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kUserSecretStash));
  ASSERT_EQ(auth_session.auth_factor_map().Find(kPasswordLabel)->storage_type(),
            AuthFactorStorageType::kVaultKeyset);
  ASSERT_EQ(
      auth_session.auth_factor_map().Find(kPasswordLabel2)->storage_type(),
      AuthFactorStorageType::kVaultKeyset);

  // Tests

  // 1-Test that enabling USS and migration doesn't change the AuthFactorMap
  // when there are only regular VaultKeysets.
  SetUserSecretStashExperimentForTesting(/*enabled=*/true);
  AuthSession auth_session2 =
      StartAuthSessionWithMockAuthBlockUtility(/*enable_uss_migration=*/true);
  EXPECT_TRUE(auth_session2.auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kVaultKeyset));
  EXPECT_FALSE(auth_session2.auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kUserSecretStash));
  ASSERT_EQ(
      auth_session2.auth_factor_map().Find(kPasswordLabel)->storage_type(),
      AuthFactorStorageType::kVaultKeyset);
  ASSERT_EQ(
      auth_session2.auth_factor_map().Find(kPasswordLabel2)->storage_type(),
      AuthFactorStorageType::kVaultKeyset);

  // 2- Test migration of the first factor on auth_session2. Storage type for
  // the migrated factor should be KUserSecretStash and non-migrated factor
  // should be kVaultKeyset.
  AuthenticateAndMigrate(auth_session2, kPasswordLabel, kPassword);
  // auth_session3 should list both the migrated factor and the not migrated VK
  AuthSession auth_session3 =
      StartAuthSessionWithMockAuthBlockUtility(/*enable_uss_migration=*/true);
  EXPECT_TRUE(auth_session3.auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kVaultKeyset));
  EXPECT_TRUE(auth_session3.auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kUserSecretStash));
  ASSERT_EQ(
      auth_session3.auth_factor_map().Find(kPasswordLabel)->storage_type(),
      AuthFactorStorageType::kUserSecretStash);
  ASSERT_EQ(
      auth_session3.auth_factor_map().Find(kPasswordLabel2)->storage_type(),
      AuthFactorStorageType::kVaultKeyset);

  // 3- Test migration of the second factor on auth_session3. Storage type for
  // the migrated factors should be KUserSecretStash.
  AuthenticateAndMigrate(auth_session3, kPasswordLabel2, kPassword2);
  EXPECT_FALSE(auth_session3.auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kVaultKeyset));
  EXPECT_TRUE(auth_session3.auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kUserSecretStash));
  ASSERT_EQ(
      auth_session3.auth_factor_map().Find(kPasswordLabel)->storage_type(),
      AuthFactorStorageType::kUserSecretStash);
  ASSERT_EQ(
      auth_session3.auth_factor_map().Find(kPasswordLabel2)->storage_type(),
      AuthFactorStorageType::kUserSecretStash);

  // 4- Test that when migration is disabled AuthSession lists the migrated
  // VaultKeysets on the map.
  AuthSession auth_session4 =
      StartAuthSessionWithMockAuthBlockUtility(/*enable_uss_migration=*/false);
  EXPECT_TRUE(auth_session4.auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kVaultKeyset));
  EXPECT_FALSE(auth_session4.auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kUserSecretStash));
  ASSERT_EQ(
      auth_session4.auth_factor_map().Find(kPasswordLabel)->storage_type(),
      AuthFactorStorageType::kVaultKeyset);
  ASSERT_EQ(
      auth_session4.auth_factor_map().Find(kPasswordLabel2)->storage_type(),
      AuthFactorStorageType::kVaultKeyset);
}

// Test that AuthSession's auth factor map lists the factor from right backing
// store on session start.
TEST_F(AuthSessionTestWithKeysetManagement, AuthFactorMapRegularVaultKeysets) {
  // Setup
  SetUserSecretStashExperimentForTesting(/*enabled=*/false);
  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;
  // Attach the mock_auth_block_utility to our AuthSessionManager and created
  // AuthSession.
  auto auth_session_manager_impl_ = std::make_unique<AuthSessionManager>(
      &crypto_, &platform_, &user_session_map_, &keyset_management_,
      &mock_auth_block_utility_, &auth_factor_manager_,
      &user_secret_stash_storage_);
  CryptohomeStatusOr<InUseAuthSession> auth_session_status =
      auth_session_manager_impl_->CreateAuthSession(Username(kUsername), flags,
                                                    AuthIntent::kDecrypt);
  EXPECT_TRUE(auth_session_status.ok());
  AuthSession* auth_session = auth_session_status.value().Get();
  EXPECT_THAT(AuthStatus::kAuthStatusFurtherFactorRequired,
              auth_session->status());
  EXPECT_TRUE(auth_session->OnUserCreated().ok());
  EXPECT_EQ(auth_session->status(), AuthStatus::kAuthStatusAuthenticated);
  EXPECT_FALSE(auth_session->auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kVaultKeyset));
  EXPECT_FALSE(auth_session->auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kUserSecretStash));

  // Test
  // Test that adding regular VaultKeysets update the map to contain
  // VaultKeysets.
  AddFactorWithMockAuthBlockUtility(*auth_session, kPasswordLabel, kPassword);
  EXPECT_TRUE(auth_session->auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kVaultKeyset));
  EXPECT_FALSE(auth_session->auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kUserSecretStash));
  AddFactorWithMockAuthBlockUtility(*auth_session, kPasswordLabel2, kPassword2);
  EXPECT_TRUE(auth_session->auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kVaultKeyset));
  EXPECT_FALSE(auth_session->auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kUserSecretStash));

  // Verify that the |auth_factor_map| contains the two VaultKeyset factor.
  ASSERT_EQ(
      auth_session->auth_factor_map().Find(kPasswordLabel)->storage_type(),
      AuthFactorStorageType::kVaultKeyset);
  ASSERT_EQ(
      auth_session->auth_factor_map().Find(kPasswordLabel2)->storage_type(),
      AuthFactorStorageType::kVaultKeyset);
}

// Test that AuthSession's auth factor map lists the factor from right backing
// store on session start when USS is enabled.
TEST_F(AuthSessionTestWithKeysetManagement, AuthFactorMapUserSecretStash) {
  // Setup
  SetUserSecretStashExperimentForTesting(/*enabled=*/true);
  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;
  // Attach the mock_auth_block_utility to our AuthSessionManager and created
  // AuthSession.
  auto auth_session_manager_impl_ = std::make_unique<AuthSessionManager>(
      &crypto_, &platform_, &user_session_map_, &keyset_management_,
      &mock_auth_block_utility_, &auth_factor_manager_,
      &user_secret_stash_storage_);
  CryptohomeStatusOr<InUseAuthSession> auth_session_status =
      auth_session_manager_impl_->CreateAuthSession(Username(kUsername), flags,
                                                    AuthIntent::kDecrypt);
  EXPECT_TRUE(auth_session_status.ok());
  AuthSession* auth_session = auth_session_status.value().Get();
  EXPECT_THAT(AuthStatus::kAuthStatusFurtherFactorRequired,
              auth_session->status());
  EXPECT_TRUE(auth_session->OnUserCreated().ok());
  EXPECT_EQ(auth_session->status(), AuthStatus::kAuthStatusAuthenticated);
  EXPECT_FALSE(auth_session->auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kVaultKeyset));
  EXPECT_FALSE(auth_session->auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kUserSecretStash));

  // Test
  // Test that adding AuthFactors update the map to contain
  // these AuthFactors with kUserSecretStash backing store.
  AddFactorWithMockAuthBlockUtility(*auth_session, kPasswordLabel, kPassword);
  EXPECT_FALSE(auth_session->auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kVaultKeyset));
  EXPECT_TRUE(auth_session->auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kUserSecretStash));
  AddFactorWithMockAuthBlockUtility(*auth_session, kPasswordLabel2, kPassword2);
  EXPECT_FALSE(auth_session->auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kVaultKeyset));
  EXPECT_TRUE(auth_session->auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kUserSecretStash));

  // Verify that the |auth_factor_map| contains the two labels with
  // kUserSecretStash backing store.
  ASSERT_EQ(
      auth_session->auth_factor_map().Find(kPasswordLabel)->storage_type(),
      AuthFactorStorageType::kUserSecretStash);
  ASSERT_EQ(
      auth_session->auth_factor_map().Find(kPasswordLabel2)->storage_type(),
      AuthFactorStorageType::kUserSecretStash);
}

}  // namespace cryptohome
