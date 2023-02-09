// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>

#include <base/files/file_path.h>
#include <base/test/mock_callback.h>
#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <brillo/cryptohome.h>
#include <brillo/secure_blob.h>
#include <cryptohome/proto_bindings/auth_factor.pb.h>
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
#include "cryptohome/auth_factor/auth_factor_storage_type.h"
#include "cryptohome/auth_session_manager.h"
#include "cryptohome/challenge_credentials/mock_challenge_credentials_helper.h"
#include "cryptohome/cleanup/mock_user_oldest_activity_timestamp_manager.h"
#include "cryptohome/credentials.h"
#include "cryptohome/crypto.h"
#include "cryptohome/filesystem_layout.h"
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
using cryptohome::error::CryptohomeCryptoError;
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
constexpr char kPublicHash2[] = "public key hash2";
constexpr int kAuthValueRounds = 5;

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

// Helper function to create a mock vault keyset with some useful default
// functions to create basic minimal VKs.
std::unique_ptr<VaultKeysetFactory> CreateMockVaultKeysetFactory() {
  auto factory = std::make_unique<MockVaultKeysetFactory>();
  ON_CALL(*factory, New(_, _))
      .WillByDefault([](Platform* platform, Crypto* crypto) {
        auto* vk = new FallbackVaultKeyset();
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
    file_system_keyset_ = FileSystemKeyset::CreateRandom();
    AddUser(kUsername, kPassword);
    PrepareDirectoryStructure();
  }

  ~AuthSessionTestWithKeysetManagement() override {
    // Reset USS experiment test flag.
    ResetUserSecretStashExperimentFlagForTesting();
  }

 protected:
  struct UserInfo {
    Username name;
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
      FallbackVaultKeyset vk;
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

  void AddFactorWithMockAuthBlockUtility(AuthSession& auth_session,
                                         const std::string& label,
                                         const std::string& secret) {
    EXPECT_CALL(mock_auth_block_utility_, GetAuthBlockTypeForCreation(_, _, _))
        .WillOnce(ReturnValue(AuthBlockType::kTpmEcc));
    auto key_blobs = std::make_unique<KeyBlobs>(kKeyBlobs);
    auto auth_block_state = std::make_unique<AuthBlockState>();
    auth_block_state->state = kTpmState;

    EXPECT_CALL(mock_auth_block_utility_,
                CreateKeyBlobsWithAuthBlockAsync(_, _, _))
        .WillOnce([&key_blobs, &auth_block_state](
                      AuthBlockType auth_block_type,
                      const AuthInput& auth_input,
                      AuthBlock::CreateCallback create_callback) {
          std::move(create_callback)
              .Run(OkStatus<CryptohomeCryptoError>(), std::move(key_blobs),
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
                DeriveKeyBlobsWithAuthBlockAsync(_, _, _, _))
        .WillOnce([&key_blobs2](AuthBlockType auth_block_type,
                                const AuthInput& auth_input,
                                const AuthBlockState& auth_state,
                                AuthBlock::DeriveCallback derive_callback) {
          std::move(derive_callback)
              .Run(OkStatus<CryptohomeCryptoError>(), std::move(key_blobs2));
          return true;
        });
    auto key_blobs = std::make_unique<KeyBlobs>(kKeyBlobs);
    auto auth_block_state = std::make_unique<AuthBlockState>();
    auth_block_state->state = kTpmState;

    EXPECT_CALL(mock_auth_block_utility_,
                CreateKeyBlobsWithAuthBlockAsync(_, _, _))
        .WillRepeatedly([&key_blobs, &auth_block_state](
                            AuthBlockType auth_block_type,
                            const AuthInput& auth_input,
                            AuthBlock::CreateCallback create_callback) {
          std::move(create_callback)
              .Run(OkStatus<CryptohomeCryptoError>(), std::move(key_blobs),
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
  AuthBlockUtilityImpl auth_block_utility_{
      &keyset_management_, &crypto_, &platform_,
      FingerprintAuthBlockService::MakeNullService()};
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
  start_auth_session_req.mutable_account_id()->set_account_id(*users_[0].name);
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
TEST_F(AuthSessionTestWithKeysetManagement, MigrationToUssWithNoKeyData) {
  if (!USE_USS_MIGRATION) {
    GTEST_SKIP() << "Skipped because this test is valid only when USS "
                    "migration is enabled.";
  }
  // Setup
  // UserSecretStash is not enabled, setup VaultKeysets for the user.
  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;
  SetUserSecretStashExperimentForTesting(/*enabled=*/false);
  KeysetSetUpWithoutKeyDataAndKeyBlobs();
  // Set the UserSecretStash experiment for testing to enable USS
  // migration with the authentication.
  SetUserSecretStashExperimentForTesting(/*enabled=*/true);

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

  // Test that authenticating the password should migrate VaultKeyset to
  // UserSecretStash, converting the VaultKeyset to a backup VaultKeyset.
  EXPECT_TRUE(auth_session->auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kVaultKeyset));
  EXPECT_FALSE(auth_session->auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kUserSecretStash));
  AuthenticateAndMigrate(*auth_session, kDefaultLabel, kPassword);

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
  ASSERT_EQ(auth_session->auth_factor_map().Find(kDefaultLabel)->storage_type(),
            AuthFactorStorageType::kUserSecretStash);

  // Verify that the authentication succeeds after migration.
  CryptohomeStatusOr<InUseAuthSession> auth_session2_status =
      auth_session_manager_impl_->CreateAuthSession(Username(kUsername), flags,
                                                    AuthIntent::kDecrypt);
  EXPECT_TRUE(auth_session2_status.ok());
  AuthSession* auth_session2 = auth_session2_status.value().Get();
  EXPECT_THAT(AuthStatus::kAuthStatusFurtherFactorRequired,
              auth_session2->status());

  // Test that authenticating the password should migrate VaultKeyset to
  // UserSecretStash, converting the VaultKeyset to a backup VaultKeyset.
  EXPECT_FALSE(auth_session2->auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kVaultKeyset));
  EXPECT_TRUE(auth_session2->auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kUserSecretStash));
  AuthenticateAndMigrate(*auth_session2, kDefaultLabel, kPassword);

  // Test that adding a new keyset succeeds
  AddFactorWithMockAuthBlockUtility(*auth_session2, kPasswordLabel, kPassword);
}

// Test that creating user with USS and adding AuthFactors adds backup
// VautlKeyset
TEST_F(AuthSessionTestWithKeysetManagement, USSEnabledCreatesBackupVKs) {
  // Setup
  // Set the UserSecretStash experiment for testing to enable USS path in the
  // test
  SetUserSecretStashExperimentForTesting(/*enabled=*/true);

  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;

  CryptohomeStatusOr<InUseAuthSession> auth_session_status =
      auth_session_manager_->CreateAuthSession(Username(kUsername), flags,
                                               AuthIntent::kDecrypt);
  EXPECT_TRUE(auth_session_status.ok());
  AuthSession* auth_session = auth_session_status.value().Get();

  // Test.
  EXPECT_THAT(AuthStatus::kAuthStatusFurtherFactorRequired,
              auth_session->status());
  EXPECT_TRUE(auth_session->OnUserCreated().ok());
  EXPECT_EQ(auth_session->status(), AuthStatus::kAuthStatusAuthenticated);

  // Add an initial and an additional factor
  AddFactor(*auth_session, kPasswordLabel, kPassword);
  AddFactor(*auth_session, kPasswordLabel2, kPassword2);

  // Verify
  std::unique_ptr<VaultKeyset> vk1 =
      keyset_management_.GetVaultKeyset(users_[0].obfuscated, kPasswordLabel);
  EXPECT_NE(vk1, nullptr);
  EXPECT_TRUE(vk1->IsForBackup());
  std::unique_ptr<VaultKeyset> vk2 =
      keyset_management_.GetVaultKeyset(users_[0].obfuscated, kPasswordLabel2);
  EXPECT_NE(vk2, nullptr);
  EXPECT_TRUE(vk2->IsForBackup());

  EXPECT_TRUE(auth_session->auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kUserSecretStash));
  EXPECT_FALSE(auth_session->auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kVaultKeyset));

  SetUserSecretStashExperimentForTesting(/*enabled=*/false);

  // Verify that AuthSession lists the backup VaultKeysets
  // as the current AuthFactors on start, if USS is disabled.
  CryptohomeStatusOr<InUseAuthSession> auth_session2_status =
      auth_session_manager_->CreateAuthSession(Username(kUsername), flags,
                                               AuthIntent::kDecrypt);
  EXPECT_TRUE(auth_session2_status.ok());
  AuthSession* auth_session2 = auth_session2_status.value().Get();
  EXPECT_FALSE(auth_session2->auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kUserSecretStash));
  EXPECT_TRUE(auth_session2->auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kVaultKeyset));
}

// Test that creating user and adding AuthFactors adds regular non-backup
// VautlKeysets if USS is not enabled
TEST_F(AuthSessionTestWithKeysetManagement, USSDisabledNotCreatesBackupVKs) {
  // Setup
  SetUserSecretStashExperimentForTesting(/*enabled=*/false);

  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;

  CryptohomeStatusOr<InUseAuthSession> auth_session_status =
      auth_session_manager_->CreateAuthSession(Username(kUsername), flags,
                                               AuthIntent::kDecrypt);
  EXPECT_TRUE(auth_session_status.ok());
  AuthSession* auth_session = auth_session_status.value().Get();

  // Test.
  EXPECT_THAT(AuthStatus::kAuthStatusFurtherFactorRequired,
              auth_session->status());
  EXPECT_TRUE(auth_session->OnUserCreated().ok());
  EXPECT_EQ(auth_session->status(), AuthStatus::kAuthStatusAuthenticated);

  // Add an initial and an additional factor
  AddFactor(*auth_session, kPasswordLabel, kPassword);
  AddFactor(*auth_session, kPasswordLabel2, kPassword2);

  // Verify
  std::unique_ptr<VaultKeyset> vk1 =
      keyset_management_.GetVaultKeyset(users_[0].obfuscated, kPasswordLabel);
  EXPECT_NE(vk1, nullptr);
  EXPECT_FALSE(vk1->IsForBackup());
  std::unique_ptr<VaultKeyset> vk2 =
      keyset_management_.GetVaultKeyset(users_[0].obfuscated, kPasswordLabel2);
  EXPECT_NE(vk2, nullptr);
  EXPECT_FALSE(vk2->IsForBackup());
  EXPECT_TRUE(auth_session->auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kVaultKeyset));

  // Verify that on AuthSession start it lists the VaultKeysetsas the current
  // AuthFactors.
  CryptohomeStatusOr<InUseAuthSession> auth_session2_status =
      auth_session_manager_->CreateAuthSession(Username(kUsername), flags,
                                               AuthIntent::kDecrypt);
  EXPECT_TRUE(auth_session2_status.ok());
  AuthSession* auth_session2 = auth_session2_status.value().Get();
  EXPECT_FALSE(auth_session2->auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kUserSecretStash));
  EXPECT_TRUE(auth_session2->auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kVaultKeyset));
}

// Test that backup VaultKeysets are removed together with the AuthFactor.
TEST_F(AuthSessionTestWithKeysetManagement, USSEnabledRemovesBackupVKs) {
  // Setup
  // Set the UserSecretStash experiment for testing to enable USS path in the
  // test
  SetUserSecretStashExperimentForTesting(/*enabled=*/true);

  AuthSession auth_session(
      {.username = Username(kUsername),
       .obfuscated_username = SanitizeUserName(Username(kUsername)),
       .is_ephemeral_user = false,
       .intent = AuthIntent::kDecrypt,
       .on_timeout = base::DoNothing(),
       .user_exists = false,
       .auth_factor_map = AuthFactorMap(),
       .migrate_to_user_secret_stash = false},
      backing_apis_);

  EXPECT_THAT(AuthStatus::kAuthStatusFurtherFactorRequired,
              auth_session.status());
  EXPECT_TRUE(auth_session.OnUserCreated().ok());
  EXPECT_EQ(auth_session.status(), AuthStatus::kAuthStatusAuthenticated);
  // Add factors and see backup VaultKeysets are also added.
  AddFactor(auth_session, kPasswordLabel, kPassword);
  AddFactor(auth_session, kPasswordLabel2, kPassword2);
  std::unique_ptr<VaultKeyset> vk1 =
      keyset_management_.GetVaultKeyset(users_[0].obfuscated, kPasswordLabel);
  EXPECT_NE(vk1, nullptr);
  EXPECT_TRUE(vk1->IsForBackup());
  std::unique_ptr<VaultKeyset> vk2 =
      keyset_management_.GetVaultKeyset(users_[0].obfuscated, kPasswordLabel2);
  EXPECT_NE(vk2, nullptr);
  EXPECT_TRUE(vk2->IsForBackup());
  EXPECT_TRUE(auth_session.auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kUserSecretStash));
  EXPECT_FALSE(auth_session.auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kVaultKeyset));

  // Test
  RemoveFactor(auth_session, kPasswordLabel2, kPassword2);

  // Verify that only the backup VaultKeyset for the removed label is deleted.
  std::unique_ptr<VaultKeyset> vk3 =
      keyset_management_.GetVaultKeyset(users_[0].obfuscated, kPasswordLabel2);
  EXPECT_EQ(vk3, nullptr);
  std::unique_ptr<VaultKeyset> vk4 =
      keyset_management_.GetVaultKeyset(users_[0].obfuscated, kPasswordLabel);
  EXPECT_NE(vk4, nullptr);
}

// Test that when user updates their credentials with USS backup VautlKeysets
// are kept as a backup.
TEST_F(AuthSessionTestWithKeysetManagement, USSEnabledUpdateBackupVKs) {
  // Setup
  // Set the UserSecretStash experiment for testing to enable USS path in the
  // test
  SetUserSecretStashExperimentForTesting(/*enabled=*/true);

  AuthSession auth_session(
      {.username = Username(kUsername),
       .obfuscated_username = SanitizeUserName(Username(kUsername)),
       .is_ephemeral_user = false,
       .intent = AuthIntent::kDecrypt,
       .on_timeout = base::DoNothing(),
       .user_exists = false,
       .auth_factor_map = AuthFactorMap(),
       .migrate_to_user_secret_stash = false},
      backing_apis_);

  EXPECT_THAT(AuthStatus::kAuthStatusFurtherFactorRequired,
              auth_session.status());
  EXPECT_TRUE(auth_session.OnUserCreated().ok());
  EXPECT_EQ(auth_session.status(), AuthStatus::kAuthStatusAuthenticated);

  // Add an initial factor to USS and backup VK
  AddFactor(auth_session, kPasswordLabel, kPassword);
  std::unique_ptr<VaultKeyset> vk1 =
      keyset_management_.GetVaultKeyset(users_[0].obfuscated, kPasswordLabel);
  EXPECT_NE(vk1, nullptr);
  EXPECT_TRUE(vk1->IsForBackup());
  EXPECT_TRUE(auth_session.auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kUserSecretStash));
  EXPECT_FALSE(auth_session.auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kVaultKeyset));

  // Test
  // Update the auth factor and see the backup VaultKeyset is still a backup.
  UpdateFactor(auth_session, kPasswordLabel, kPassword2);

  // Verify
  std::unique_ptr<VaultKeyset> vk2 =
      keyset_management_.GetVaultKeyset(users_[0].obfuscated, kPasswordLabel);
  EXPECT_NE(vk2, nullptr);
  EXPECT_TRUE(vk2->IsForBackup());
  EXPECT_TRUE(auth_session.auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kUserSecretStash));
  EXPECT_FALSE(auth_session.auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kVaultKeyset));

  // Verify that on AuthSession start it lists the USS-AuthFactors.
  CryptohomeStatusOr<InUseAuthSession> auth_session2_status =
      auth_session_manager_->CreateAuthSession(
          Username(kUsername),
          user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE,
          AuthIntent::kDecrypt);
  EXPECT_TRUE(auth_session2_status.ok());
  AuthSession* auth_session2 = auth_session2_status.value().Get();
  EXPECT_TRUE(auth_session2->auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kUserSecretStash));
  EXPECT_FALSE(auth_session2->auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kVaultKeyset));
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

  EXPECT_CALL(mock_auth_block_utility_, GetAuthBlockTypeForCreation(_, _, _))
      .WillRepeatedly(ReturnValue(AuthBlockType::kTpmEcc));

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
  AddFactor(*auth_session, kPasswordLabel, kPassword);

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
  UpdateFactor(*auth_session, kPasswordLabel, kPassword2);

  std::unique_ptr<VaultKeyset> vk1 =
      keyset_management_.GetVaultKeyset(users_[0].obfuscated, kPasswordLabel);
  EXPECT_NE(vk1, nullptr);
  EXPECT_TRUE(vk1->IsForBackup());
  EXPECT_TRUE(auth_session->auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kUserSecretStash));
  EXPECT_FALSE(auth_session->auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kVaultKeyset));

  // Test
  // See that authentication with the backup password succeeds
  // if USS is disabled after the update.
  SetUserSecretStashExperimentForTesting(/*enabled=*/false);
  CryptohomeStatusOr<InUseAuthSession> auth_session2_status =
      auth_session_manager_impl_->CreateAuthSession(Username(kUsername), flags,
                                                    AuthIntent::kDecrypt);
  EXPECT_TRUE(auth_session2_status.ok());
  AuthSession* auth_session2 = auth_session2_status.value().Get();
  EXPECT_TRUE(auth_session2->auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kVaultKeyset));
  EXPECT_FALSE(auth_session2->auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kUserSecretStash));

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

  AuthenticateFactor(*auth_session2, kPasswordLabel, kPassword2);

  // Verify
  EXPECT_EQ(auth_session2->status(), AuthStatus::kAuthStatusAuthenticated);
}

// Test that authentication with backup VK succeeds when USS is rolled back.
TEST_F(AuthSessionTestWithKeysetManagement,
       USSRollbackAuthWithBackupVKSuccess) {
  // Setup
  // Set the UserSecretStash experiment for testing to enable USS path in the
  // test
  SetUserSecretStashExperimentForTesting(/*enabled=*/true);

  AuthSession::BackingApis backing_apis = backing_apis_;
  backing_apis.auth_block_utility = &mock_auth_block_utility_;
  AuthSession auth_session(
      {.username = Username(kUsername),
       .obfuscated_username = SanitizeUserName(Username(kUsername)),
       .is_ephemeral_user = false,
       .intent = AuthIntent::kDecrypt,
       .on_timeout = base::DoNothing(),
       .user_exists = false,
       .auth_factor_map = AuthFactorMap(),
       .migrate_to_user_secret_stash = false},
      backing_apis);

  EXPECT_THAT(AuthStatus::kAuthStatusFurtherFactorRequired,
              auth_session.status());
  EXPECT_TRUE(auth_session.OnUserCreated().ok());
  EXPECT_EQ(auth_session.status(), AuthStatus::kAuthStatusAuthenticated);

  EXPECT_CALL(mock_auth_block_utility_, GetAuthBlockTypeForCreation(_, _, _))
      .WillOnce(ReturnValue(AuthBlockType::kTpmEcc));

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
      keyset_management_.GetVaultKeyset(users_[0].obfuscated, kPasswordLabel);
  EXPECT_NE(vk1, nullptr);
  EXPECT_TRUE(vk1->IsForBackup());
  EXPECT_TRUE(auth_session.auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kUserSecretStash));
  EXPECT_FALSE(auth_session.auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kVaultKeyset));

  // Test
  // See that authentication with the backup password succeeds if USS is
  // disabled.
  SetUserSecretStashExperimentForTesting(/*enabled=*/false);
  // Attach the mock_auth_block_utility to our AuthSessionManager and created
  // AuthSession.
  auto auth_session_manager_impl_ = std::make_unique<AuthSessionManager>(
      &crypto_, &platform_, &user_session_map_, &keyset_management_,
      &mock_auth_block_utility_, &auth_factor_manager_,
      &user_secret_stash_storage_);
  CryptohomeStatusOr<InUseAuthSession> auth_session2_status =
      auth_session_manager_impl_->CreateAuthSession(
          Username(kUsername),
          user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE,
          AuthIntent::kDecrypt);
  EXPECT_TRUE(auth_session2_status.ok());
  AuthSession* auth_session2 = auth_session2_status.value().Get();
  EXPECT_TRUE(auth_session2->auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kVaultKeyset));
  EXPECT_FALSE(auth_session2->auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kUserSecretStash));

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

  AuthenticateFactor(*auth_session2, kPasswordLabel, kPassword);

  // Verify
  EXPECT_EQ(auth_session2->status(), AuthStatus::kAuthStatusAuthenticated);
}

// Test that AuthSession list the non-backup VKs on session start
TEST_F(AuthSessionTestWithKeysetManagement, USSDisableddNotListBackupVKs) {
  // Setup
  SetUserSecretStashExperimentForTesting(/*enabled=*/false);

  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;
  CryptohomeStatusOr<InUseAuthSession> auth_session_status =
      auth_session_manager_->CreateAuthSession(Username(kUsername), flags,
                                               AuthIntent::kDecrypt);
  EXPECT_TRUE(auth_session_status.ok());
  AuthSession* auth_session = auth_session_status.value().Get();
  EXPECT_THAT(AuthStatus::kAuthStatusFurtherFactorRequired,
              auth_session->status());
  EXPECT_TRUE(auth_session->OnUserCreated().ok());
  EXPECT_EQ(auth_session->status(), AuthStatus::kAuthStatusAuthenticated);
  // Add factors
  AddFactor(*auth_session, kPasswordLabel, kPassword);
  AddFactor(*auth_session, kPasswordLabel2, kPassword2);
  std::unique_ptr<VaultKeyset> vk1 =
      keyset_management_.GetVaultKeyset(users_[0].obfuscated, kPasswordLabel);
  EXPECT_NE(vk1, nullptr);
  std::unique_ptr<VaultKeyset> vk2 =
      keyset_management_.GetVaultKeyset(users_[0].obfuscated, kPasswordLabel2);
  EXPECT_NE(vk2, nullptr);

  // Test
  CryptohomeStatusOr<InUseAuthSession> auth_session2_status =
      auth_session_manager_->CreateAuthSession(Username(kUsername), flags,
                                               AuthIntent::kDecrypt);
  EXPECT_TRUE(auth_session2_status.ok());
  AuthSession* auth_session2 = auth_session2_status.value().Get();

  // Verify
  EXPECT_FALSE(vk1->IsForBackup());
  EXPECT_FALSE(vk2->IsForBackup());
  EXPECT_TRUE(auth_session2->auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kVaultKeyset));
  EXPECT_THAT(auth_session2->auth_factor_map().Find(kPasswordLabel),
              Optional(_));
  EXPECT_THAT(auth_session2->auth_factor_map().Find(kPasswordLabel2),
              Optional(_));
}

// Test that AuthSession list the backup VKs on session start if USS is disabled
// after being enabled.
TEST_F(AuthSessionTestWithKeysetManagement, USSRollbackListBackupVKs) {
  // Setup
  SetUserSecretStashExperimentForTesting(/*enabled=*/true);

  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;
  CryptohomeStatusOr<InUseAuthSession> auth_session_status =
      auth_session_manager_->CreateAuthSession(Username(kUsername), flags,
                                               AuthIntent::kDecrypt);
  EXPECT_TRUE(auth_session_status.ok());
  AuthSession* auth_session = auth_session_status.value().Get();
  EXPECT_THAT(AuthStatus::kAuthStatusFurtherFactorRequired,
              auth_session->status());
  EXPECT_TRUE(auth_session->OnUserCreated().ok());
  EXPECT_EQ(auth_session->status(), AuthStatus::kAuthStatusAuthenticated);
  // Add factors
  AddFactor(*auth_session, kPasswordLabel, kPassword);
  AddFactor(*auth_session, kPasswordLabel2, kPassword2);
  std::unique_ptr<VaultKeyset> vk1 =
      keyset_management_.GetVaultKeyset(users_[0].obfuscated, kPasswordLabel);
  EXPECT_NE(vk1, nullptr);
  std::unique_ptr<VaultKeyset> vk2 =
      keyset_management_.GetVaultKeyset(users_[0].obfuscated, kPasswordLabel2);
  EXPECT_NE(vk2, nullptr);
  EXPECT_TRUE(auth_session->auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kUserSecretStash));
  EXPECT_FALSE(auth_session->auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kVaultKeyset));

  // Test
  SetUserSecretStashExperimentForTesting(/*enabled=*/false);
  CryptohomeStatusOr<InUseAuthSession> auth_session2_status =
      auth_session_manager_->CreateAuthSession(Username(kUsername), flags,
                                               AuthIntent::kDecrypt);
  EXPECT_TRUE(auth_session2_status.ok());
  AuthSession* auth_session2 = auth_session2_status.value().Get();

  // Verify
  EXPECT_TRUE(vk1->IsForBackup());
  EXPECT_TRUE(vk2->IsForBackup());
  EXPECT_TRUE(auth_session2->auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kVaultKeyset));

  EXPECT_THAT(auth_session2->auth_factor_map().Find(kPasswordLabel),
              Optional(_));
  EXPECT_THAT(auth_session2->auth_factor_map().Find(kPasswordLabel2),
              Optional(_));
}

// Test that VaultKeysets are migrated to UserSecretStash when migration is
// enabled, converting the existing VaultKeysets to migrated VaultKeysets.
TEST_F(AuthSessionTestWithKeysetManagement, MigrationEnabledMigratesToUss) {
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

  CryptohomeStatusOr<InUseAuthSession> auth_session3_status =
      auth_session_manager_impl_->CreateAuthSession(Username(kUsername), flags,
                                                    AuthIntent::kDecrypt);
  EXPECT_TRUE(auth_session3_status.ok());
  AuthSession* auth_session3 = auth_session3_status.value().Get();
  AuthenticateAndMigrate(*auth_session3, kPasswordLabel2, kPassword2);

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
      auth_session3->auth_factor_map().Find(kPasswordLabel)->storage_type(),
      AuthFactorStorageType::kUserSecretStash);
  ASSERT_EQ(
      auth_session3->auth_factor_map().Find(kPasswordLabel2)->storage_type(),
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
  ResetUserSecretStashExperimentFlagForTesting();
}

// Test that after a VaultKeyset is migrated to UserSecretStash the next
// factor is added as migrated VaultKeysets.
TEST_F(AuthSessionTestWithKeysetManagement,
       MigrationEnabledAddNextFactorsToUss) {
  if (!USE_USS_MIGRATION) {
    GTEST_SKIP() << "Skipped because this test is valid only when USS "
                    "migration is enabled.";
  }
  // Setup
  // UserSecretStash is not enabled, setup VaultKeysets for the user.
  int flags = user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_NONE;
  SetUserSecretStashExperimentForTesting(/*enabled=*/false);

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
  // Add the first factor with VaultKeyset backing.
  AddFactorWithMockAuthBlockUtility(*auth_session, kPasswordLabel, kPassword);

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

  // Verify that the vault_keysets still exist and converted to backup and
  // migrated VaultKeysets.
  std::unique_ptr<VaultKeyset> vk1 =
      keyset_management_.GetVaultKeyset(users_[0].obfuscated, kPasswordLabel);
  ASSERT_NE(vk1, nullptr);
  ASSERT_TRUE(vk1->IsForBackup());
  ASSERT_TRUE(vk1->IsMigrated());

  // Test that adding a second factor adds as a USS AuthFactor with a backup &
  // migrated VK.
  AddFactorWithMockAuthBlockUtility(*auth_session2, kPasswordLabel2,
                                    kPassword2);
  // Added vault_keyset should be a backup and migrated VaultKeyset.
  std::unique_ptr<VaultKeyset> vk2 =
      keyset_management_.GetVaultKeyset(users_[0].obfuscated, kPasswordLabel2);
  ASSERT_NE(vk2, nullptr);
  ASSERT_TRUE(vk2->IsForBackup());
  ASSERT_TRUE(vk2->IsMigrated());

  // Verify
  // Create a new AuthSession for verifications.
  CryptohomeStatusOr<InUseAuthSession> auth_session3_status =
      auth_session_manager_impl_->CreateAuthSession(Username(kUsername), flags,
                                                    AuthIntent::kDecrypt);
  EXPECT_TRUE(auth_session3_status.ok());
  AuthSession* auth_session3 = auth_session3_status.value().Get();
  AuthenticateAndMigrate(*auth_session3, kPasswordLabel2, kPassword2);

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
      auth_session2->auth_factor_map().Find(kPasswordLabel)->storage_type(),
      AuthFactorStorageType::kUserSecretStash);
  ASSERT_EQ(
      auth_session2->auth_factor_map().Find(kPasswordLabel2)->storage_type(),
      AuthFactorStorageType::kUserSecretStash);
}

// Test that AuthSession's auth factor map lists the factor from right backing
// store during the migration.
TEST_F(AuthSessionTestWithKeysetManagement,
       AuthFactorMapStatusDuringMigration) {
  if (!USE_USS_MIGRATION) {
    GTEST_SKIP() << "Skipped because this test is valid only when USS "
                    "migration is enabled.";
  }
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

  AddFactorWithMockAuthBlockUtility(*auth_session, kPasswordLabel, kPassword);
  AddFactorWithMockAuthBlockUtility(*auth_session, kPasswordLabel2, kPassword2);
  EXPECT_TRUE(auth_session->auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kVaultKeyset));
  EXPECT_FALSE(auth_session->auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kUserSecretStash));
  ASSERT_EQ(
      auth_session->auth_factor_map().Find(kPasswordLabel)->storage_type(),
      AuthFactorStorageType::kVaultKeyset);
  ASSERT_EQ(
      auth_session->auth_factor_map().Find(kPasswordLabel2)->storage_type(),
      AuthFactorStorageType::kVaultKeyset);

  // Tests

  // 1-Test that enabling UserSecretStash doesn't change the AuthFactorMap when
  // there are only regular VaultKeysets.
  SetUserSecretStashExperimentForTesting(/*enabled=*/true);
  // Start a new AuthSession to test with a fresh session.
  CryptohomeStatusOr<InUseAuthSession> auth_session2_status =
      auth_session_manager_impl_->CreateAuthSession(Username(kUsername), flags,
                                                    AuthIntent::kDecrypt);
  EXPECT_TRUE(auth_session2_status.ok());
  AuthSession* auth_session2 = auth_session2_status.value().Get();
  EXPECT_TRUE(auth_session2->auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kVaultKeyset));
  EXPECT_FALSE(auth_session2->auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kUserSecretStash));
  ASSERT_EQ(
      auth_session->auth_factor_map().Find(kPasswordLabel)->storage_type(),
      AuthFactorStorageType::kVaultKeyset);
  ASSERT_EQ(
      auth_session->auth_factor_map().Find(kPasswordLabel2)->storage_type(),
      AuthFactorStorageType::kVaultKeyset);

  // 2- Test migration of the first factor on auth_session2. Storage type for
  // the migrated factor should be KUserSecretStash and non-migrated factor
  // should be kVaultKeyset.
  AuthenticateAndMigrate(*auth_session2, kPasswordLabel, kPassword);
  // auth_session3 should list both the migrated factor and the not migrated VK
  CryptohomeStatusOr<InUseAuthSession> auth_session3_status =
      auth_session_manager_impl_->CreateAuthSession(Username(kUsername), flags,
                                                    AuthIntent::kDecrypt);
  EXPECT_TRUE(auth_session3_status.ok());
  AuthSession* auth_session3 = auth_session3_status.value().Get();
  EXPECT_TRUE(auth_session3->auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kVaultKeyset));
  EXPECT_TRUE(auth_session3->auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kUserSecretStash));
  ASSERT_EQ(
      auth_session3->auth_factor_map().Find(kPasswordLabel)->storage_type(),
      AuthFactorStorageType::kUserSecretStash);
  ASSERT_EQ(
      auth_session3->auth_factor_map().Find(kPasswordLabel2)->storage_type(),
      AuthFactorStorageType::kVaultKeyset);

  // 3- Test migration of the second factor on auth_session3. Storage type for
  // the migrated factors should be KUserSecretStash.
  AuthenticateAndMigrate(*auth_session3, kPasswordLabel2, kPassword2);
  EXPECT_FALSE(auth_session3->auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kVaultKeyset));
  EXPECT_TRUE(auth_session3->auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kUserSecretStash));
  ASSERT_EQ(
      auth_session3->auth_factor_map().Find(kPasswordLabel)->storage_type(),
      AuthFactorStorageType::kUserSecretStash);
  ASSERT_EQ(
      auth_session3->auth_factor_map().Find(kPasswordLabel2)->storage_type(),
      AuthFactorStorageType::kUserSecretStash);

  // 4- Test that when UserSecretStash is disabled AuthSession lists the backup
  // VaultKeysets on the map.
  SetUserSecretStashExperimentForTesting(/*enabled=*/false);
  CryptohomeStatusOr<InUseAuthSession> auth_session4_status =
      auth_session_manager_impl_->CreateAuthSession(Username(kUsername), flags,
                                                    AuthIntent::kDecrypt);
  EXPECT_TRUE(auth_session4_status.ok());
  AuthSession* auth_session4 = auth_session4_status.value().Get();
  EXPECT_TRUE(auth_session4->auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kVaultKeyset));
  EXPECT_FALSE(auth_session4->auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kUserSecretStash));
  ASSERT_EQ(
      auth_session4->auth_factor_map().Find(kPasswordLabel)->storage_type(),
      AuthFactorStorageType::kVaultKeyset);
  ASSERT_EQ(
      auth_session4->auth_factor_map().Find(kPasswordLabel2)->storage_type(),
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

  // When UserSecretStash is disabled |auth_factor_map| lists the backup
  // VaultKeysets.
  SetUserSecretStashExperimentForTesting(/*enabled=*/false);
  CryptohomeStatusOr<InUseAuthSession> auth_session2_status =
      auth_session_manager_impl_->CreateAuthSession(Username(kUsername), flags,
                                                    AuthIntent::kDecrypt);
  EXPECT_TRUE(auth_session2_status.ok());
  AuthSession* auth_session2 = auth_session2_status.value().Get();
  EXPECT_TRUE(auth_session2->auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kVaultKeyset));
  EXPECT_FALSE(auth_session2->auth_factor_map().HasFactorWithStorage(
      AuthFactorStorageType::kUserSecretStash));
  ASSERT_EQ(
      auth_session2->auth_factor_map().Find(kPasswordLabel)->storage_type(),
      AuthFactorStorageType::kVaultKeyset);
  ASSERT_EQ(
      auth_session2->auth_factor_map().Find(kPasswordLabel2)->storage_type(),
      AuthFactorStorageType::kVaultKeyset);
}

// Test the scenario of adding a new factor when the authenticated factor's
// backup VaultKeyset was corrupted. The operation fails, but it's a regression
// test for a crash.
TEST_F(AuthSessionTestWithKeysetManagement, AddFactorAfterBackupVkCorruption) {
  // Setup.
  SetUserSecretStashExperimentForTesting(/*enabled=*/true);
  SetUpHwsecAuthenticationMocks();
  // Creating the user with a password factor.
  {
    CryptohomeStatusOr<InUseAuthSession> auth_session_status =
        auth_session_manager_->CreateAuthSession(
            Username(kUsername), user_data_auth::AUTH_SESSION_FLAGS_NONE,
            AuthIntent::kDecrypt);
    ASSERT_THAT(auth_session_status, IsOk());
    AuthSession& auth_session = *auth_session_status.value().Get();
    EXPECT_THAT(auth_session.OnUserCreated(), IsOk());
    AddFactor(auth_session, kPasswordLabel, kPassword);
  }
  // Corrupt the backup VK (it's the user's only VK) by truncating it.
  const base::FilePath vk_path =
      VaultKeysetPath(SanitizeUserName(Username(kUsername)), /*index=*/0);
  EXPECT_TRUE(platform_.FileExists(vk_path));
  EXPECT_TRUE(platform_.WriteFile(vk_path, brillo::Blob()));
  // Creating a new AuthSession for authentication.
  CryptohomeStatusOr<InUseAuthSession> auth_session_status =
      auth_session_manager_->CreateAuthSession(
          Username(kUsername), user_data_auth::AUTH_SESSION_FLAGS_NONE,
          AuthIntent::kDecrypt);
  ASSERT_THAT(auth_session_status, IsOk());
  AuthSession& auth_session = *auth_session_status.value().Get();
  // Authenticating the AuthSession via the password.
  std::string auth_factor_labels[] = {kPasswordLabel};
  user_data_auth::AuthInput auth_input_proto;
  auth_input_proto.mutable_password_input()->set_secret(kPassword);
  TestFuture<CryptohomeStatus> auth_future;
  auth_session.AuthenticateAuthFactor(auth_factor_labels, auth_input_proto,
                                      auth_future.GetCallback());
  EXPECT_THAT(auth_future.Get(), IsOk());

  // Test.
  user_data_auth::AddAuthFactorRequest add_request;
  add_request.set_auth_session_id(auth_session.serialized_token());
  add_request.mutable_auth_factor()->set_type(
      user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
  add_request.mutable_auth_factor()->set_label(kPasswordLabel2);
  add_request.mutable_auth_factor()->mutable_password_metadata();
  add_request.mutable_auth_input()->mutable_password_input()->set_secret(
      kPassword2);
  TestFuture<CryptohomeStatus> add_future;
  auth_session.AddAuthFactor(add_request, add_future.GetCallback());

  // Verify.
  EXPECT_THAT(add_future.Get(), NotOk());
}

}  // namespace cryptohome
