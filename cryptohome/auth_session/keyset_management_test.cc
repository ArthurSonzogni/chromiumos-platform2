// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <absl/container/flat_hash_map.h>
#include <base/files/file_path.h>
#include <base/functional/bind.h>
#include <base/functional/callback_helpers.h>
#include <base/memory/scoped_refptr.h>
#include <base/test/bind.h>
#include <base/test/mock_callback.h>
#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <base/unguessable_token.h>
#include <brillo/cryptohome.h>
#include <brillo/secure_blob.h>
#include <cryptohome/proto_bindings/auth_factor.pb.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libhwsec-foundation/error/testing_helper.h>
#include <libhwsec-foundation/status/status_chain.h>
#include <libhwsec/factory/factory_impl.h>
#include <libhwsec/factory/tpm2_simulator_factory_for_test.h>
#include <libhwsec/frontend/cryptohome/mock_frontend.h>
#include <libhwsec/frontend/pinweaver_manager/mock_frontend.h>
#include <libhwsec/frontend/recovery_crypto/mock_frontend.h>
#include <libstorage/platform/mock_platform.h>

#include "cryptohome/auth_blocks/auth_block.h"
#include "cryptohome/auth_blocks/auth_block_type.h"
#include "cryptohome/auth_blocks/auth_block_utility_impl.h"
#include "cryptohome/auth_blocks/fp_service.h"
#include "cryptohome/auth_blocks/mock_auth_block_utility.h"
#include "cryptohome/auth_factor/manager.h"
#include "cryptohome/auth_factor/storage_type.h"
#include "cryptohome/auth_factor/types/manager.h"
#include "cryptohome/auth_io/auth_input.h"
#include "cryptohome/auth_session/manager.h"
#include "cryptohome/challenge_credentials/mock_challenge_credentials_helper.h"
#include "cryptohome/crypto.h"
#include "cryptohome/fake_features.h"
#include "cryptohome/filesystem_layout.h"
#include "cryptohome/flatbuffer_schemas/auth_block_state.h"
#include "cryptohome/key_objects.h"
#include "cryptohome/mock_key_challenge_service_factory.h"
#include "cryptohome/mock_keyset_management.h"
#include "cryptohome/mock_vault_keyset_factory.h"
#include "cryptohome/pkcs11/mock_pkcs11_token_factory.h"
#include "cryptohome/storage/mock_homedirs.h"
#include "cryptohome/user_secret_stash/decrypted.h"
#include "cryptohome/user_secret_stash/storage.h"
#include "cryptohome/user_session/mock_user_session_factory.h"
#include "cryptohome/user_session/user_session_map.h"
#include "cryptohome/userdataauth.h"
#include "cryptohome/userdataauth_test_utils.h"
#include "cryptohome/vault_keyset.h"
#include "cryptohome/vault_keyset_factory.h"

namespace cryptohome {
namespace {

using ::base::test::TaskEnvironment;
using ::base::test::TestFuture;
using ::brillo::cryptohome::home::SanitizeUserName;
using ::cryptohome::error::CryptohomeError;
using ::hwsec_foundation::error::testing::IsOk;
using ::hwsec_foundation::error::testing::NotOk;
using ::hwsec_foundation::error::testing::ReturnValue;
using ::hwsec_foundation::status::OkStatus;
using ::testing::_;
using ::testing::Contains;
using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::NiceMock;
using ::testing::NotNull;
using ::testing::Optional;
using ::testing::Return;
using ::testing::UnorderedElementsAre;
using ::testing::VariantWith;

using AuthenticateTestFuture =
    TestFuture<const AuthSession::PostAuthAction&, CryptohomeStatus>;

constexpr char kUsername[] = "foo@example.com";
constexpr char kPassword[] = "password";
constexpr char kPin[] = "1234";
constexpr char kWrongPin[] = "4321";
constexpr char kPasswordLabel[] = "password_label";
constexpr char kPassword2[] = "password2";
constexpr char kPasswordLabel2[] = "password_label2";
constexpr char kDefaultLabel[] = "legacy-0";
constexpr char kPinLabel[] = "pin_label";
constexpr char kSalt[] = "salt";
constexpr char kPublicHash[] = "public key hash";
constexpr int kAuthValueRounds = 5;

const brillo::SecureBlob kInitialBlob64(64, 'A');
const brillo::SecureBlob kInitialBlob32(32, 'A');
const brillo::SecureBlob kAdditionalBlob32(32, 'B');
const brillo::SecureBlob kInitialBlob16(16, 'C');
const brillo::SecureBlob kAdditionalBlob16(16, 'D');

AuthSession::AuthenticateAuthFactorRequest ToAuthenticateRequest(
    std::vector<std::string> labels, user_data_auth::AuthInput auth_input) {
  return AuthSession::AuthenticateAuthFactorRequest{
      .auth_factor_labels = std::move(labels),
      .auth_input_proto = std::move(auth_input),
      .flags = {.force_full_auth = AuthSession::ForceFullAuthFlag::kNone},
  };
}

}  // namespace

class AuthSessionTestWithKeysetManagement : public ::testing::Test {
 public:
  AuthSessionTestWithKeysetManagement() {
    // Setting HWSec Expectations.
    EXPECT_CALL(system_apis_.hwsec, IsEnabled())
        .WillRepeatedly(ReturnValue(true));
    EXPECT_CALL(system_apis_.hwsec, IsReady())
        .WillRepeatedly(ReturnValue(true));
    EXPECT_CALL(system_apis_.hwsec, IsSealingSupported())
        .WillRepeatedly(ReturnValue(true));
    EXPECT_CALL(system_apis_.hwsec, GetManufacturer())
        .WillRepeatedly(ReturnValue(0x43524f53));
    EXPECT_CALL(system_apis_.hwsec, GetAuthValue(_, _))
        .WillRepeatedly(ReturnValue(brillo::SecureBlob()));
    EXPECT_CALL(system_apis_.hwsec, SealWithCurrentUser(_, _, _))
        .WillRepeatedly(ReturnValue(brillo::Blob()));
    EXPECT_CALL(system_apis_.hwsec, GetPubkeyHash(_))
        .WillRepeatedly(ReturnValue(brillo::Blob()));
    ON_CALL(system_apis_.hwsec, IsPinWeaverEnabled())
        .WillByDefault(ReturnValue(false));

    // Mock the VK factory with some useful default functions to create basic
    // minimal keysets.
    ON_CALL(*system_apis_.vault_keyset_factory, New(_, _))
        .WillByDefault([](libstorage::Platform* platform, Crypto* crypto) {
          auto* vk = new VaultKeyset();
          vk->Initialize(platform, crypto);
          return vk;
        });
    ON_CALL(*system_apis_.vault_keyset_factory, NewBackup(_, _))
        .WillByDefault([](libstorage::Platform* platform, Crypto* crypto) {
          auto* vk = new VaultKeyset();
          vk->InitializeAsBackup(platform, crypto);
          return vk;
        });

    system_apis_.crypto.Init();

    auth_session_manager_ = std::make_unique<AuthSessionManager>(
        AuthSession::BackingApis{
            &system_apis_.crypto, &system_apis_.platform, &user_session_map_,
            &system_apis_.keyset_management, &auth_block_utility_,
            &auth_factor_driver_manager_, &system_apis_.auth_factor_manager,
            &fp_migration_utility_, &system_apis_.uss_storage,
            &system_apis_.uss_manager, &features_.async},
        task_environment_.GetMainThreadTaskRunner().get());
    // Initializing UserData class.
    userdataauth_.set_homedirs(&homedirs_);
    userdataauth_.set_user_session_factory(&user_session_factory_);
    userdataauth_.set_auth_factor_driver_manager_for_testing(
        &auth_factor_driver_manager_);
    userdataauth_.set_auth_session_manager(auth_session_manager_.get());
    userdataauth_.set_pkcs11_token_factory(&pkcs11_token_factory_);
    userdataauth_.set_mount_task_runner(
        task_environment_.GetMainThreadTaskRunner());
    userdataauth_.set_auth_block_utility(&auth_block_utility_);
    userdataauth_.set_features(&features_.object);
    file_system_keyset_ = FileSystemKeyset::CreateRandom();
    AddUser(kUsername, kPassword);
    PrepareDirectoryStructure();
  }

 protected:
  struct UserInfo {
    Username username;
    ObfuscatedUsername obfuscated;
    brillo::SecureBlob passkey;
    base::FilePath homedir_path;
    base::FilePath user_path;
  };

  void AddUser(const std::string& name, const std::string& password) {
    Username username(name);
    ObfuscatedUsername obfuscated = SanitizeUserName(username);
    brillo::SecureBlob passkey(password);

    UserInfo info = {username, obfuscated, passkey, UserPath(obfuscated),
                     brillo::cryptohome::home::GetUserPath(obfuscated)};
    users_.push_back(info);
  }

  void PrepareDirectoryStructure() {
    ASSERT_TRUE(system_apis_.platform.CreateDirectory(ShadowRoot()));
    ASSERT_TRUE(system_apis_.platform.CreateDirectory(
        brillo::cryptohome::home::GetUserPathPrefix()));
    // We only need the homedir path, not the vault/mount paths.
    for (const auto& user : users_) {
      ASSERT_TRUE(system_apis_.platform.CreateDirectory(user.homedir_path));
    }
  }

  // Configures the mock Hwsec to simulate correct replies for authentication
  // (unsealing) requests.
  void SetUpHwsecAuthenticationMocks() {
    // When sealing, remember the secret and configure the unseal mock to return
    // it.
    EXPECT_CALL(system_apis_.hwsec, SealWithCurrentUser(_, _, _))
        .WillRepeatedly([this](auto, auto, auto unsealed_value) {
          EXPECT_CALL(system_apis_.hwsec, UnsealWithCurrentUser(_, _, _))
              .WillRepeatedly(ReturnValue(unsealed_value));
          return brillo::Blob();
        });
    EXPECT_CALL(system_apis_.hwsec, PreloadSealedData(_))
        .WillRepeatedly(ReturnValue(std::nullopt));
  }

  void RemoveFactor(AuthSession& auth_session,
                    const std::string& label,
                    const std::string& secret) {
    user_data_auth::RemoveAuthFactorRequest request;
    request.set_auth_factor_label(label);
    request.set_auth_session_id(auth_session.serialized_token());
    TestFuture<CryptohomeStatus> remove_future;
    auth_session.GetAuthForDecrypt()->RemoveAuthFactor(
        request, remove_future.GetCallback());
    EXPECT_THAT(remove_future.Get(), IsOk());
  }

  KeyData DefaultKeyData() {
    KeyData key_data;
    key_data.set_label(kPasswordLabel);
    return key_data;
  }

  void KeysetSetUpWithKeyDataAndKeyBlobs(const KeyData& key_data,
                                         int index = 0) {
    for (auto& user : users_) {
      VaultKeyset vk;
      vk.Initialize(&system_apis_.platform, &system_apis_.crypto);
      vk.CreateFromFileSystemKeyset(file_system_keyset_);
      vk.SetKeyData(key_data);
      AuthBlockState auth_block_state;
      auth_block_state.state = kTpmState;

      ASSERT_THAT(vk.EncryptEx(kKeyBlobs, auth_block_state), IsOk());
      ASSERT_TRUE(vk.Save(user.homedir_path.Append(kKeyFile).AddExtension(
          std::to_string(index))));
    }
  }

  void BackupKeysetSetUpWithKeyDataAndKeyBlobs(const KeyData& key_data,
                                               int index = 0) {
    for (auto& user : users_) {
      VaultKeyset vk;
      vk.InitializeAsBackup(&system_apis_.platform, &system_apis_.crypto);
      vk.CreateFromFileSystemKeyset(file_system_keyset_);
      vk.SetKeyData(key_data);
      AuthBlockState auth_block_state;
      auth_block_state.state = kTpmState;

      ASSERT_THAT(vk.EncryptEx(kKeyBlobs, auth_block_state), IsOk());
      ASSERT_TRUE(vk.Save(user.homedir_path.Append(kKeyFile).AddExtension(
          std::to_string(index))));
    }
  }

  void KeysetSetUpWithoutKeyDataAndKeyBlobs() {
    for (auto& user : users_) {
      VaultKeyset vk;
      vk.Initialize(&system_apis_.platform, &system_apis_.crypto);
      vk.CreateFromFileSystemKeyset(file_system_keyset_);
      AuthBlockState auth_block_state = {
          .state = kTpmState,
      };

      ASSERT_THAT(vk.EncryptEx(kKeyBlobs, auth_block_state), IsOk());
      ASSERT_TRUE(
          vk.Save(user.homedir_path.Append(kKeyFile).AddExtension("0")));
    }
  }

  VaultKeyset KeysetSetupWithAuthInput(bool is_migrated,
                                       bool is_backup,
                                       const AuthInput& auth_input,
                                       KeyData& key_data,
                                       const std::string& file_indice) {
    VaultKeyset vk;
    AuthBlockType auth_block_type = AuthBlockType::kTpmEcc;
    AuthFactorMetadata metadata = {.metadata = PasswordMetadata()};
    if (key_data.has_policy() && key_data.policy().low_entropy_credential()) {
      auth_block_type = AuthBlockType::kPinWeaver;
      metadata = {.metadata = PinMetadata()};
    }
    auth_block_utility_.CreateKeyBlobsWithAuthBlock(
        auth_block_type, auth_input, metadata,
        base::BindLambdaForTesting(
            [&](CryptohomeStatus error, std::unique_ptr<KeyBlobs> key_blobs,
                std::unique_ptr<AuthBlockState> auth_block_state) {
              ASSERT_THAT(error, IsOk());
              vk.Initialize(&system_apis_.platform, &system_apis_.crypto);
              vk.SetKeyData(key_data);
              vk.set_backup_vk_for_testing(is_backup);

              vk.set_migrated_vk_for_testing(is_migrated);
              vk.CreateFromFileSystemKeyset(file_system_keyset_);
              ASSERT_THAT(vk.EncryptEx(*key_blobs, *auth_block_state), IsOk());
              ASSERT_TRUE(
                  vk.Save(users_[0].homedir_path.Append(kKeyFile).AddExtension(
                      file_indice)));
            }));
    return vk;
  }

  AuthSession StartAuthSessionWithMockAuthBlockUtility() {
    AuthSession::Params auth_session_params{
        .username = users_[0].username,
        .is_ephemeral_user = false,
        .intent = AuthIntent::kDecrypt,
        .auth_factor_status_update_timer =
            std::make_unique<base::WallClockTimer>(),
        .user_exists = true};
    backing_apis_.auth_block_utility = &mock_auth_block_utility_;
    return AuthSession(std::move(auth_session_params), backing_apis_);
  }

  AuthSession StartAuthSession() {
    AuthSession::Params auth_session_params{
        .username = users_[0].username,
        .is_ephemeral_user = false,
        .intent = AuthIntent::kDecrypt,
        .auth_factor_status_update_timer =
            std::make_unique<base::WallClockTimer>(),
        .user_exists = true};
    return AuthSession(std::move(auth_session_params), backing_apis_);
  }

  const AuthFactorMap& GetAuthFactorMap() {
    return system_apis_.auth_factor_manager.GetAuthFactorMap(
        users_[0].obfuscated);
  }

  void AddFactorWithMockAuthBlockUtility(AuthSession& auth_session,
                                         const std::string& label,
                                         const std::string& secret) {
    EXPECT_CALL(mock_auth_block_utility_, SelectAuthBlockTypeForCreation(_))
        .WillOnce(ReturnValue(AuthBlockType::kTpmEcc));
    auto key_blobs = std::make_unique<KeyBlobs>(kKeyBlobs);
    auto auth_block_state = std::make_unique<AuthBlockState>();
    auth_block_state->state = kTpmState;

    EXPECT_CALL(mock_auth_block_utility_,
                CreateKeyBlobsWithAuthBlock(_, _, _, _))
        .WillOnce([&key_blobs, &auth_block_state](
                      AuthBlockType auth_block_type,
                      const AuthInput& auth_input,
                      const AuthFactorMetadata& auth_factor_metadata,
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
    auth_session.GetAuthForDecrypt()->AddAuthFactor(request,
                                                    add_future.GetCallback());
    EXPECT_THAT(add_future.Get(), IsOk());
  }

  void AuthenticateAndMigrate(AuthSession& auth_session,
                              const std::string& label,
                              const std::string& secret) {
    EXPECT_CALL(mock_auth_block_utility_, GetAuthBlockTypeFromState(_))
        .WillRepeatedly(Return(AuthBlockType::kTpmEcc));

    auto key_blobs2 = std::make_unique<KeyBlobs>(kKeyBlobs);
    EXPECT_CALL(mock_auth_block_utility_,
                DeriveKeyBlobsWithAuthBlock(_, _, _, _, _))
        .WillOnce([&key_blobs2](AuthBlockType auth_block_type,
                                const AuthInput& auth_input,
                                const AuthFactorMetadata& auth_factor_metadata,
                                const AuthBlockState& auth_state,
                                AuthBlock::DeriveCallback derive_callback) {
          std::move(derive_callback)
              .Run(OkStatus<CryptohomeError>(), std::move(key_blobs2),
                   std::nullopt);
          return true;
        });
    auto key_blobs = std::make_unique<KeyBlobs>(kKeyBlobs);
    auto auth_block_state = std::make_unique<AuthBlockState>();
    auth_block_state->state = kTpmState;

    EXPECT_CALL(mock_auth_block_utility_,
                CreateKeyBlobsWithAuthBlock(_, _, _, _))
        .WillRepeatedly([&key_blobs, &auth_block_state](
                            AuthBlockType auth_block_type,
                            const AuthInput& auth_input,
                            const AuthFactorMetadata& metadata,
                            AuthBlock::CreateCallback create_callback) {
          std::move(create_callback)
              .Run(OkStatus<CryptohomeError>(), std::move(key_blobs),
                   std::move(auth_block_state));
          return true;
        });
    std::vector<std::string> auth_factor_labels{label};
    user_data_auth::AuthInput auth_input_proto;
    auth_input_proto.mutable_password_input()->set_secret(secret);
    auto auth_factor_type_policy = GetEmptyAuthFactorTypePolicy(
        *DetermineFactorTypeFromAuthInput(auth_input_proto));

    AuthenticateTestFuture authenticate_future;
    auth_session.AuthenticateAuthFactor(
        ToAuthenticateRequest(auth_factor_labels, auth_input_proto),
        auth_factor_type_policy, authenticate_future.GetCallback());
    auto& [action, status] = authenticate_future.Get();
    EXPECT_THAT(status, IsOk());
    EXPECT_EQ(action.action_type, AuthSession::PostAuthActionType::kNone);
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
    auth_session.GetAuthForDecrypt()->AddAuthFactor(request,
                                                    add_future.GetCallback());
    EXPECT_THAT(add_future.Get(), IsOk());
  }

  void UpdateFactor(AuthSession& auth_session,
                    const std::string& label,
                    const std::string& secret) {
    EXPECT_CALL(mock_auth_block_utility_, SelectAuthBlockTypeForCreation(_))
        .WillOnce(ReturnValue(AuthBlockType::kTpmEcc));
    auto key_blobs = std::make_unique<KeyBlobs>(kKeyBlobs);
    auto auth_block_state = std::make_unique<AuthBlockState>();
    auth_block_state->state = kTpmState;

    EXPECT_CALL(mock_auth_block_utility_,
                CreateKeyBlobsWithAuthBlock(_, _, _, _))
        .WillOnce([&key_blobs, &auth_block_state](
                      AuthBlockType auth_block_type,
                      const AuthInput& auth_input,
                      const AuthFactorMetadata& auth_factor_metadata,
                      AuthBlock::CreateCallback create_callback) {
          std::move(create_callback)
              .Run(OkStatus<CryptohomeError>(), std::move(key_blobs),
                   std::move(auth_block_state));
          return true;
        });
    user_data_auth::UpdateAuthFactorRequest request;
    request.set_auth_session_id(auth_session.serialized_token());
    request.set_auth_factor_label(label);
    request.mutable_auth_factor()->set_type(
        user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
    request.mutable_auth_factor()->set_label(label);
    request.mutable_auth_factor()->mutable_password_metadata();
    request.mutable_auth_input()->mutable_password_input()->set_secret(secret);
    TestFuture<CryptohomeStatus> update_future;
    auth_session.GetAuthForDecrypt()->UpdateAuthFactor(
        request, update_future.GetCallback());
    EXPECT_THAT(update_future.Get(), IsOk());
  }

  void AuthenticatePasswordFactor(AuthSession& auth_session,
                                  const std::string& label,
                                  const std::string& secret) {
    std::vector<std::string> auth_factor_labels{label};
    user_data_auth::AuthInput auth_input_proto;
    auth_input_proto.mutable_password_input()->set_secret(secret);
    AuthenticateTestFuture authenticate_future;
    auto auth_factor_type_policy =
        GetEmptyAuthFactorTypePolicy(AuthFactorType::kPassword);
    auth_session.AuthenticateAuthFactor(
        ToAuthenticateRequest(auth_factor_labels, auth_input_proto),
        auth_factor_type_policy, authenticate_future.GetCallback());
    auto& [action, status] = authenticate_future.Get();
    EXPECT_THAT(status, IsOk());
    EXPECT_EQ(action.action_type, AuthSession::PostAuthActionType::kNone);
  }

  user_data_auth::CryptohomeErrorCode AttemptAuthWithPinFactor(
      AuthSession& auth_session,
      const std::string& label,
      const std::string& secret) {
    std::vector<std::string> auth_factor_labels{label};
    user_data_auth::AuthInput auth_input_proto;
    auth_input_proto.mutable_pin_input()->set_secret(secret);
    AuthenticateTestFuture authenticate_future;
    auto auth_factor_type_policy =
        GetEmptyAuthFactorTypePolicy(AuthFactorType::kPin);
    auth_session.AuthenticateAuthFactor(
        ToAuthenticateRequest(auth_factor_labels, auth_input_proto),
        auth_factor_type_policy, authenticate_future.GetCallback());

    auto& [unused_action, status] = authenticate_future.Get();
    if (status.ok() || !status->local_legacy_error().has_value()) {
      return user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
    }
    return status->local_legacy_error().value();
  }

  // Standard key blob and TPM state objects to use in testing.
  const brillo::SecureBlob kSecureBlob32 = brillo::SecureBlob(32, 'A');
  const brillo::Blob kBlob32 = brillo::Blob(32, 'B');
  const brillo::Blob kBlob16 = brillo::Blob(16, 'C');
  const KeyBlobs kKeyBlobs{
      .vkk_key = kSecureBlob32, .vkk_iv = kBlob16, .chaps_iv = kBlob16};
  const TpmEccAuthBlockState kTpmState = {
      .salt = brillo::BlobFromString(kSalt),
      .vkk_iv = kBlob32,
      .auth_value_rounds = kAuthValueRounds,
      .sealed_hvkkm = kBlob32,
      .extended_sealed_hvkkm = kBlob32,
      .tpm_public_key_hash = brillo::BlobFromString(kPublicHash),
  };

  base::test::TaskEnvironment task_environment_;
  TestScryptThread scrypt_thread_;

  // Mocks and fakes for the test AuthSessions to use.
  MockSystemApis<WithMockVaultKeysetFactory> system_apis_;
  UserSessionMap user_session_map_;
  FakeFeaturesForTesting features_;
  NiceMock<MockChallengeCredentialsHelper> challenge_credentials_helper_;
  NiceMock<MockKeyChallengeServiceFactory> key_challenge_service_factory_;
  std::unique_ptr<FingerprintAuthBlockService> fp_service_{
      FingerprintAuthBlockService::MakeNullService()};
  AuthBlockUtilityImpl auth_block_utility_{
      &system_apis_.keyset_management,
      &system_apis_.crypto,
      &system_apis_.platform,
      &features_.async,
      scrypt_thread_.task_runner.get(),
      AsyncInitPtr<ChallengeCredentialsHelper>(&challenge_credentials_helper_),
      &key_challenge_service_factory_,
      AsyncInitPtr<BiometricsAuthBlockService>(nullptr)};
  NiceMock<MockAuthBlockUtility> mock_auth_block_utility_;
  AuthFactorDriverManager auth_factor_driver_manager_{
      &system_apis_.platform,
      &system_apis_.crypto,
      &system_apis_.uss_manager,
      AsyncInitPtr<ChallengeCredentialsHelper>(nullptr),
      nullptr,
      &system_apis_.recovery_ab_service,
      fp_service_.get(),
      AsyncInitPtr<BiometricsAuthBlockService>(nullptr),
      &features_.async};
  FpMigrationUtility fp_migration_utility_{
      &system_apis_.crypto, AsyncInitPtr<BiometricsAuthBlockService>(nullptr),
      &features_.async};
  AuthSession::BackingApis backing_apis_{&system_apis_.crypto,
                                         &system_apis_.platform,
                                         &user_session_map_,
                                         &system_apis_.keyset_management,
                                         &auth_block_utility_,
                                         &auth_factor_driver_manager_,
                                         &system_apis_.auth_factor_manager,
                                         &fp_migration_utility_,
                                         &system_apis_.uss_storage,
                                         &system_apis_.uss_manager,
                                         &features_.async};

  // An AuthSession manager for testing managed creation.
  std::unique_ptr<AuthSessionManager> auth_session_manager_;

  FileSystemKeyset file_system_keyset_;
  MockVaultKeysetFactory* mock_vault_keyset_factory_;
  NiceMock<MockHomeDirs> homedirs_;
  NiceMock<MockUserSessionFactory> user_session_factory_;

  NiceMock<MockPkcs11TokenFactory> pkcs11_token_factory_;
  UserDataAuth userdataauth_{system_apis_.ToBackingApis()};

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
  start_auth_session_req.set_intent(user_data_auth::AUTH_INTENT_DECRYPT);
  user_data_auth::StartAuthSessionReply auth_session_reply;

  userdataauth_.StartAuthSession(
      start_auth_session_req,
      base::BindOnce(
          [](user_data_auth::StartAuthSessionReply* auth_reply_ptr,
             const user_data_auth::StartAuthSessionReply& reply) {
            *auth_reply_ptr = reply;
          },
          base::Unretained(&auth_session_reply)));
  task_environment_.RunUntilIdle();

  EXPECT_EQ(auth_session_reply.error(),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  std::optional<base::UnguessableToken> auth_session_id =
      AuthSession::GetTokenFromSerializedString(
          auth_session_reply.auth_session_id());
  EXPECT_TRUE(auth_session_id.has_value());
  userdataauth_.auth_session_manager_->RunWhenAvailable(
      *auth_session_id, base::BindOnce([](InUseAuthSession auth_session) {
        ASSERT_THAT(auth_session.AuthSessionStatus(), IsOk());
      }));
  task_environment_.RunUntilIdle();
}

// Test that a VaultKeyset without KeyData migration succeeds during login.
TEST_F(AuthSessionTestWithKeysetManagement,
       MigrationToUssWithNoKeyDataAndNewFactor) {
  // Setup
  // Setup legacy VaultKeysets to test USS migration. On AuthSession start
  // legacy keyset should be migrated to USS following successful
  // authentication.
  SetUpHwsecAuthenticationMocks();
  AuthInput auth_input = {
      .user_input = brillo::SecureBlob(kPassword),
      .locked_to_single_user = std::nullopt,
      .username = users_[0].username,
      .obfuscated_username = users_[0].obfuscated,
  };
  auth_block_utility_.CreateKeyBlobsWithAuthBlock(
      AuthBlockType::kTpmEcc, auth_input, {},
      base::BindLambdaForTesting(
          [&](CryptohomeStatus error, std::unique_ptr<KeyBlobs> key_blobs,
              std::unique_ptr<AuthBlockState> auth_block_state) {
            ASSERT_THAT(error, IsOk());
            VaultKeyset vk;
            vk.Initialize(&system_apis_.platform, &system_apis_.crypto);
            vk.CreateFromFileSystemKeyset(file_system_keyset_);
            ASSERT_THAT(vk.EncryptEx(*key_blobs, *auth_block_state), IsOk());
            ASSERT_TRUE(vk.Save(
                users_[0].homedir_path.Append(kKeyFile).AddExtension("0")));
          }));

  AuthSession auth_session1 = StartAuthSession();
  ASSERT_EQ(GetAuthFactorMap().Find(kDefaultLabel)->storage_type(),
            AuthFactorStorageType::kVaultKeyset);
  // Test that authenticating the password migrates VaultKeyset to
  // UserSecretStash.
  AuthenticatePasswordFactor(auth_session1, kDefaultLabel, kPassword);
  ASSERT_EQ(GetAuthFactorMap().Find(kDefaultLabel)->storage_type(),
            AuthFactorStorageType::kUserSecretStash);

  // Verify that migrator created the user_secret_stash and uss_main_key.
  ASSERT_TRUE(auth_session1.has_user_secret_stash());

  // Verify that the authentication succeeds after migration.
  AuthSession auth_session2 = StartAuthSession();
  EXPECT_THAT(auth_session2.authorized_intents(), IsEmpty());
  ASSERT_EQ(GetAuthFactorMap().Find(kDefaultLabel)->storage_type(),
            AuthFactorStorageType::kUserSecretStash);
  AuthenticatePasswordFactor(auth_session2, kDefaultLabel, kPassword);

  // Test that adding a new factor succeeds.
  AuthSession auth_session4 = StartAuthSession();
  ASSERT_EQ(GetAuthFactorMap().Find(kDefaultLabel)->storage_type(),
            AuthFactorStorageType::kUserSecretStash);
  AuthenticatePasswordFactor(auth_session4, kDefaultLabel, kPassword);
  AddFactor(auth_session4, kPasswordLabel2, kPassword2);
  ASSERT_EQ(GetAuthFactorMap().Find(kPasswordLabel2)->storage_type(),
            AuthFactorStorageType::kUserSecretStash);
  // Verify authentication works with the added factor.
  AuthenticatePasswordFactor(auth_session4, kPasswordLabel2, kPassword2);
}

// This test tests successful removal of the backup keysets.
// Initial USS migration code converted the migrated VaultKeysets to backup
// keysets rather than removing them.
// USS migration code is then updated to remove the migrated VaultKeysets since
// the rollback is no longer a possibility. Updated USS migration code also
// removes the leftover backup keysets from initial USS migration.
// This test tests the removal of the leftover backup keyset is successful and
// removing the backup keyset doesn't break the PIN lock/unlock mechanism.
TEST_F(AuthSessionTestWithKeysetManagement,
       RemoveBackupKeysetFromMigratedKeyset) {
  // SETUP
  constexpr int kMaxWrongAttempts = 5;

  // Setup pinweaver manager.
  hwsec::Tpm2SimulatorFactoryForTest factory;
  auto pw_manager = factory.GetPinWeaverManagerFrontend();
  system_apis_.crypto.set_pinweaver_manager_for_testing(pw_manager.get());
  system_apis_.crypto.Init();
  EXPECT_CALL(system_apis_.hwsec, IsPinWeaverEnabled())
      .WillRepeatedly(ReturnValue(false));

  SetUpHwsecAuthenticationMocks();
  AuthInput auth_input = {
      .user_input = brillo::SecureBlob(kPassword),
      .locked_to_single_user = std::nullopt,
      .username = users_[0].username,
      .obfuscated_username = users_[0].obfuscated,
  };

  // Setup keyset files.

  KeyData key_data = DefaultKeyData();

  // Setup keyset file to be used as backup keyset simulator.
  key_data.set_label(kDefaultLabel);
  auto backup_vk = KeysetSetupWithAuthInput(
      /*is_migrated=*/true, /*is_backup=*/true, auth_input, key_data, "1");
  brillo::SecureBlob reset_seed = backup_vk.GetResetSeed();

  // Setup original keyset.
  key_data.set_label(kPasswordLabel);
  KeysetSetupWithAuthInput(
      /*is_migrated=*/false, /*is_backup=*/false, auth_input, key_data, "0");

  // Test authenticate migrates to UserSecretStash.

  // AuthenticateAuthFactor also removes the original keyset but not the backup
  // keyset simulator, since it has a different label.
  {
    AuthSession auth_session = StartAuthSession();
    EXPECT_THAT(auth_session.authorized_intents(), IsEmpty());
    EXPECT_TRUE(GetAuthFactorMap().HasFactorWithStorage(
        AuthFactorStorageType::kVaultKeyset));
    EXPECT_FALSE(GetAuthFactorMap().HasFactorWithStorage(
        AuthFactorStorageType::kUserSecretStash));

    AuthenticatePasswordFactor(auth_session, kPasswordLabel, kPassword);
    EXPECT_EQ(nullptr, system_apis_.keyset_management.GetVaultKeyset(
                           users_[0].obfuscated, kPasswordLabel));
    EXPECT_NE(nullptr, system_apis_.keyset_management.GetVaultKeyset(
                           users_[0].obfuscated, kDefaultLabel));
  }
  system_apis_.auth_factor_manager.DiscardAllAuthFactorMaps();

  // Simulate backup keyset.

  // Setup backup keyset to simulate the state when migrated factors
  // had backup keysets. Restore original label since now the regular
  // VaultKeyset with the original label is migrated to USS and the regular
  // VaultKeyset is deleted.
  std::unique_ptr<VaultKeyset> vk_backup =
      system_apis_.keyset_management.GetVaultKeyset(users_[0].obfuscated,
                                                    kDefaultLabel);
  EXPECT_NE(nullptr, vk_backup);
  vk_backup->SetKeyDataLabel(kPasswordLabel);
  ASSERT_TRUE(vk_backup->Save(
      users_[0].homedir_path.Append(kKeyFile).AddExtension("0")));
  EXPECT_NE(nullptr, system_apis_.keyset_management.GetVaultKeyset(
                         users_[0].obfuscated, kPasswordLabel));

  // Simulate the mixed configuration.

  // Setup a PIN keyset to simulate a mixed configuration of VaultKeyset and USS
  // backed factors.
  auth_input.user_input = brillo::SecureBlob(kPin);
  auth_input.reset_seed = reset_seed;
  EXPECT_CALL(system_apis_.hwsec, IsPinWeaverEnabled())
      .WillRepeatedly(ReturnValue(true));
  KeyData pin_data;
  pin_data.set_label(kPinLabel);
  pin_data.mutable_policy()->set_low_entropy_credential(true);
  KeysetSetupWithAuthInput(
      /*is_migrated=*/false, /*is_backup=*/false, auth_input, pin_data, "1");
  // Verify mixed configuration state.
  std::unique_ptr<VaultKeyset> vk_password =
      system_apis_.keyset_management.GetVaultKeyset(users_[0].obfuscated,
                                                    kPasswordLabel);
  std::unique_ptr<VaultKeyset> vk_pin =
      system_apis_.keyset_management.GetVaultKeyset(users_[0].obfuscated,
                                                    kPinLabel);

  EXPECT_NE(nullptr, vk_password);
  EXPECT_NE(nullptr, vk_pin);
  EXPECT_TRUE(vk_password->IsForBackup());
  EXPECT_TRUE(vk_password->IsMigrated());
  EXPECT_FALSE(vk_pin->IsForBackup());

  // Test that AuthenticateAuthFactor removes the backup keyset.

  // We need to mock the KeysetManagement. Encryption key of the USS
  // key_block and the VaultKeyset are different since the backup is not
  // generated during the migration flow. Hence VaultKeyset can't be decrypted
  // by the same authentication.
  auto original_backing_apis = std::move(backing_apis_);
  NiceMock<MockKeysetManagement> mock_keyset_management;
  AuthSession::BackingApis backing_api_with_mock_km{
      &system_apis_.crypto,
      &system_apis_.platform,
      &user_session_map_,
      &mock_keyset_management,
      &auth_block_utility_,
      &auth_factor_driver_manager_,
      &system_apis_.auth_factor_manager,
      &fp_migration_utility_,
      &system_apis_.uss_storage,
      &system_apis_.uss_manager,
      &features_.async};
  backing_apis_ = std::move(backing_api_with_mock_km);
  EXPECT_CALL(system_apis_.hwsec, IsPinWeaverEnabled())
      .WillRepeatedly(ReturnValue(false));

  {
    AuthSession auth_session = StartAuthSession();
    EXPECT_TRUE(GetAuthFactorMap().HasFactorWithStorage(
        AuthFactorStorageType::kUserSecretStash));
    EXPECT_TRUE(GetAuthFactorMap().HasFactorWithStorage(
        AuthFactorStorageType::kVaultKeyset));
    EXPECT_CALL(mock_keyset_management, GetVaultKeyset(users_[0].obfuscated, _))
        .WillRepeatedly(
            [&](const ObfuscatedUsername& obfuscated, const std::string&) {
              std::unique_ptr<VaultKeyset> vk_to_mock =
                  system_apis_.keyset_management.GetVaultKeyset(obfuscated,
                                                                kPinLabel);
              vk_to_mock->SetResetSalt(vk_pin->GetResetSalt());
              vk_to_mock->set_backup_vk_for_testing(true);
              return vk_to_mock;
            });
    EXPECT_CALL(mock_keyset_management, RemoveKeysetFile(_))
        .WillOnce(Return(OkStatus<CryptohomeError>()));
    // We need to explicitly add the reset_seed for testing since |vk_password|
    // is not decrypted.
    vk_password->SetResetSeed(reset_seed);
    EXPECT_CALL(mock_keyset_management, GetValidKeyset(_, _, _))
        .WillOnce(Return(std::move(vk_password)));
    AuthenticatePasswordFactor(auth_session, kPasswordLabel, kPassword);
  }

  // Verify PIN reset mechanism.

  // Verify that wrong PINs lock the PIN counter and password authentication
  // reset the PIN counter after the removal of the backup password.

  backing_apis_ = std::move(original_backing_apis);
  {
    AuthSession auth_session = StartAuthSession();
    EXPECT_TRUE(GetAuthFactorMap().HasFactorWithStorage(
        AuthFactorStorageType::kUserSecretStash));
    EXPECT_TRUE(GetAuthFactorMap().HasFactorWithStorage(
        AuthFactorStorageType::kVaultKeyset));
    AuthenticatePasswordFactor(auth_session, kPasswordLabel, kPassword);

    // Attempting too many wrong PINs, but don't lock yet.
    EXPECT_CALL(system_apis_.hwsec, IsPinWeaverEnabled())
        .WillRepeatedly(ReturnValue(true));
    for (int i = 0; i < kMaxWrongAttempts - 2; i++) {
      EXPECT_EQ(user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED,
                AttemptAuthWithPinFactor(auth_session, kPinLabel, kWrongPin));
    }
    // One more wrong PIN attempt will lockout the PIN.
    EXPECT_EQ(user_data_auth::CRYPTOHOME_ERROR_CREDENTIAL_LOCKED,
              AttemptAuthWithPinFactor(auth_session, kPinLabel, kWrongPin));
    // Pin should be locked and correct PIN should fail.
    EXPECT_EQ(user_data_auth::CRYPTOHOME_ERROR_TPM_DEFEND_LOCK,
              AttemptAuthWithPinFactor(auth_session, kPinLabel, kPin));
    // Reset the PIN counter with correct password.
    AuthenticatePasswordFactor(auth_session, kPasswordLabel, kPassword);
    auth_session.ResetLECredentials();
    // After resetting with password correct PIn should now work.
    EXPECT_EQ(user_data_auth::CRYPTOHOME_ERROR_NOT_SET,
              AttemptAuthWithPinFactor(auth_session, kPinLabel, kPin));
  }
}

// Test that we can authenticate an old-style kiosk VK, and migrate it to USS
// correctly. These old VKs show up as password VKs and so we need the
// authenticate to successfully convert it to a kiosk based on the input.
TEST_F(AuthSessionTestWithKeysetManagement, AuthenticatePasswordVkToKioskUss) {
  // Setup
  // Setup legacy kiosk VaultKeyset to test USS migration.
  SetUpHwsecAuthenticationMocks();
  user_data_auth::AuthInput proto;
  proto.mutable_kiosk_input();
  AuthFactorMetadata auth_factor_metadata;
  std::optional<AuthInput> auth_input = CreateAuthInput(
      &system_apis_.platform, proto, users_[0].username, users_[0].obfuscated,
      /*locked_to_single_user=*/true,
      /*cryptohome_recovery_ephemeral_pub_key=*/nullptr);
  auth_block_utility_.CreateKeyBlobsWithAuthBlock(
      AuthBlockType::kTpmEcc, auth_input.value(), {},
      base::BindLambdaForTesting(
          [&](CryptohomeStatus error, std::unique_ptr<KeyBlobs> key_blobs,
              std::unique_ptr<AuthBlockState> auth_block_state) {
            ASSERT_THAT(error, IsOk());
            VaultKeyset vk;
            vk.Initialize(&system_apis_.platform, &system_apis_.crypto);
            vk.CreateFromFileSystemKeyset(file_system_keyset_);
            ASSERT_THAT(vk.EncryptEx(*key_blobs, *auth_block_state), IsOk());
            ASSERT_TRUE(vk.Save(
                users_[0].homedir_path.Append(kKeyFile).AddExtension("0")));
          }));
  AuthSession auth_session = StartAuthSession();
  ASSERT_EQ(GetAuthFactorMap().Find(kDefaultLabel)->storage_type(),
            AuthFactorStorageType::kVaultKeyset);

  // Test that authenticating the legacy kiosk migrates VaultKeyset to
  // UserSecretStash as a kKiosk type.
  std::vector<std::string> auth_factor_labels{kDefaultLabel};
  user_data_auth::AuthInput auth_input_proto;
  auth_input_proto.mutable_kiosk_input();
  AuthenticateTestFuture authenticate_future;
  auto auth_factor_type_policy = GetEmptyAuthFactorTypePolicy(
      *DetermineFactorTypeFromAuthInput(auth_input_proto));
  auth_session.AuthenticateAuthFactor(
      ToAuthenticateRequest(auth_factor_labels, auth_input_proto),
      auth_factor_type_policy, authenticate_future.GetCallback());
  auto& [action, status] = authenticate_future.Get();
  EXPECT_THAT(status, IsOk());
  EXPECT_EQ(action.action_type, AuthSession::PostAuthActionType::kNone);

  ASSERT_EQ(GetAuthFactorMap().Find(kDefaultLabel)->storage_type(),
            AuthFactorStorageType::kUserSecretStash);

  // Verify.
  ASSERT_TRUE(auth_session.has_user_secret_stash());
  ASSERT_THAT(GetAuthFactorMap().size(), Eq(1));
  AuthFactorMap::ValueView stored_auth_factor = *GetAuthFactorMap().begin();
  const AuthFactor& auth_factor = stored_auth_factor.auth_factor();
  EXPECT_THAT(stored_auth_factor.storage_type(),
              Eq(AuthFactorStorageType::kUserSecretStash));
  EXPECT_THAT(auth_factor.type(), Eq(AuthFactorType::kKiosk));
  EXPECT_THAT(auth_factor.metadata().metadata, VariantWith<KioskMetadata>(_));
}

// Test if AuthenticateAuthFactor authenticates existing credentials for a
// user with VK and resaves it.
TEST_F(AuthSessionTestWithKeysetManagement,
       AuthenticateAuthFactorExistingVKAndResaves) {
  // Setup
  // Setup legacy VaultKeyset with no chaps key so that AuthenticateAuthFactor
  // generates a chaps key and saves it before migrating to USS.
  SetUpHwsecAuthenticationMocks();
  AuthInput auth_input = {
      .user_input = brillo::SecureBlob(kPassword),
      .locked_to_single_user = std::nullopt,
      .username = users_[0].username,
      .obfuscated_username = users_[0].obfuscated,
  };
  VaultKeyset vk;
  auth_block_utility_.CreateKeyBlobsWithAuthBlock(
      AuthBlockType::kTpmEcc, auth_input, {},
      base::BindLambdaForTesting(
          [&](CryptohomeStatus error, std::unique_ptr<KeyBlobs> key_blobs,
              std::unique_ptr<AuthBlockState> auth_block_state) {
            ASSERT_THAT(error, IsOk());
            vk.Initialize(&system_apis_.platform, &system_apis_.crypto);
            KeyData key_data;
            key_data.set_label(kDefaultLabel);
            vk.SetKeyData(key_data);
            vk.CreateFromFileSystemKeyset(file_system_keyset_);
            ASSERT_THAT(vk.EncryptEx(*key_blobs, *auth_block_state), IsOk());
            vk.ClearWrappedChapsKey();
            ASSERT_TRUE(vk.Save(
                users_[0].homedir_path.Append(kKeyFile).AddExtension("0")));
          }));

  AuthSession auth_session = StartAuthSession();
  ASSERT_EQ(GetAuthFactorMap().Find(kDefaultLabel)->storage_type(),
            AuthFactorStorageType::kVaultKeyset);
  ASSERT_FALSE(vk.HasWrappedChapsKey());

  // Test that authenticating the VaultKeyset with missing chaps key still
  // migrates to UserSecretStash after regenerating the chaps key. Note
  // AuthenticateAuthFactor() returning success shows that chaps key has been
  // generated on VK and resaved. Otherwise USS creation during migration would
  // fail.
  AuthenticatePasswordFactor(auth_session, kDefaultLabel, kPassword);
  ASSERT_EQ(GetAuthFactorMap().Find(kDefaultLabel)->storage_type(),
            AuthFactorStorageType::kUserSecretStash);

  // Verify that migrator created the user_secret_stash and uss_main_key.
  ASSERT_TRUE(auth_session.has_user_secret_stash());
}

// Test that a VaultKeyset without KeyData migration succeeds during login.
TEST_F(AuthSessionTestWithKeysetManagement, MigrationToUssWithNoKeyData) {
  // Setup
  // Setup legacy VaultKeysets to test USS migration. On AuthSession start
  // legacy keyset should be migrated to USS following successful
  // authentication.
  KeysetSetUpWithoutKeyDataAndKeyBlobs();

  AuthSession auth_session = StartAuthSessionWithMockAuthBlockUtility();
  EXPECT_THAT(auth_session.authorized_intents(), IsEmpty());

  // Test that authenticating the password migrates VaultKeyset to
  // UserSecretStash, converting the VaultKeyset to a backup VaultKeyset.
  EXPECT_TRUE(GetAuthFactorMap().HasFactorWithStorage(
      AuthFactorStorageType::kVaultKeyset));
  EXPECT_FALSE(GetAuthFactorMap().HasFactorWithStorage(
      AuthFactorStorageType::kUserSecretStash));
  AuthenticateAndMigrate(auth_session, kDefaultLabel, kPassword);

  // Verify that migrator created the user_secret_stash and uss_main_key.
  UssStorage uss_storage(&system_apis_.platform);
  UserUssStorage user_uss_storage(uss_storage, users_[0].obfuscated);
  CryptohomeStatusOr<brillo::SecureBlob> uss_credential_secret =
      kKeyBlobs.DeriveUssCredentialSecret();
  ASSERT_THAT(uss_credential_secret, IsOk());
  CryptohomeStatusOr<DecryptedUss> decrypted_uss =
      DecryptedUss::FromStorageUsingWrappedKey(user_uss_storage, kDefaultLabel,
                                               *uss_credential_secret);
  ASSERT_THAT(decrypted_uss, IsOk());

  // Verify that the user_secret_stash has the wrapped_key_block for
  // the default label.
  ASSERT_THAT(decrypted_uss->encrypted().WrappedMainKeyIds(),
              Contains(kDefaultLabel));
  //  Verify that the AuthFactors are created for the AuthFactor labels and
  //  storage type is updated in the AuthFactor map for each of them.
  absl::flat_hash_map<std::string, AuthFactorType> factor_map =
      system_apis_.auth_factor_manager.ListAuthFactors(users_[0].obfuscated);
  ASSERT_NE(factor_map.find(kDefaultLabel), factor_map.end());
  ASSERT_EQ(GetAuthFactorMap().Find(kDefaultLabel)->storage_type(),
            AuthFactorStorageType::kUserSecretStash);

  // Verify that the authentication succeeds after migration.
  AuthSession auth_session2 = StartAuthSessionWithMockAuthBlockUtility();
  EXPECT_THAT(auth_session2.authorized_intents(), IsEmpty());

  // Test that authenticating the password should migrate VaultKeyset to
  // UserSecretStash, converting the VaultKeyset to a backup VaultKeyset.
  EXPECT_FALSE(GetAuthFactorMap().HasFactorWithStorage(
      AuthFactorStorageType::kVaultKeyset));
  EXPECT_TRUE(GetAuthFactorMap().HasFactorWithStorage(
      AuthFactorStorageType::kUserSecretStash));
  AuthenticateAndMigrate(auth_session2, kDefaultLabel, kPassword);
}

// Test UpdateAuthFactor for partially migrated users.
TEST_F(AuthSessionTestWithKeysetManagement, MigrationEnabledUpdateBackup) {
  // Setup
  // Setup legacy VaultKeysets to test USS migration. On AuthSession start
  // legacy keyset should be migrated to USS following successful
  // authentication.
  KeysetSetUpWithKeyDataAndKeyBlobs(DefaultKeyData());
  KeyData key_data = DefaultKeyData();
  key_data.set_label(kPasswordLabel2);
  KeysetSetUpWithKeyDataAndKeyBlobs(key_data, 1);

  // Test that authenticating the password should migrate VaultKeyset to
  // UserSecretStash.
  AuthSession auth_session2 = StartAuthSessionWithMockAuthBlockUtility();
  EXPECT_TRUE(GetAuthFactorMap().HasFactorWithStorage(
      AuthFactorStorageType::kVaultKeyset));
  EXPECT_FALSE(GetAuthFactorMap().HasFactorWithStorage(
      AuthFactorStorageType::kUserSecretStash));
  AuthenticateAndMigrate(auth_session2, kPasswordLabel, kPassword);

  // Verify that migrator loaded the user_secret_stash and uss_main_key.
  UssStorage uss_storage(&system_apis_.platform);
  UserUssStorage user_uss_storage(uss_storage, users_[0].obfuscated);
  CryptohomeStatusOr<brillo::SecureBlob> uss_credential_secret =
      kKeyBlobs.DeriveUssCredentialSecret();
  ASSERT_THAT(uss_credential_secret, IsOk());
  CryptohomeStatusOr<DecryptedUss> decrypted_uss =
      DecryptedUss::FromStorageUsingWrappedKey(user_uss_storage, kPasswordLabel,
                                               *uss_credential_secret);
  ASSERT_THAT(decrypted_uss, IsOk());

  // Verify that the user_secret_stash has the wrapped_key_blocks for the
  // AuthFactor label.
  ASSERT_THAT(decrypted_uss->encrypted().WrappedMainKeyIds(),
              Contains(kPasswordLabel));
  //  Verify that the AuthFactors are created for the AuthFactor labels and
  //  storage type is updated in the AuthFactor map for each of them.
  ASSERT_EQ(GetAuthFactorMap().Find(kPasswordLabel)->storage_type(),
            AuthFactorStorageType::kUserSecretStash);
  ASSERT_EQ(GetAuthFactorMap().Find(kPasswordLabel2)->storage_type(),
            AuthFactorStorageType::kVaultKeyset);

  // Test
  UpdateFactor(auth_session2, kPasswordLabel2, kPassword2);

  // Verify AuthFactors listing. All factors are migrated.
  AuthSession auth_session3 = StartAuthSessionWithMockAuthBlockUtility();
  EXPECT_TRUE(GetAuthFactorMap().HasFactorWithStorage(
      AuthFactorStorageType::kUserSecretStash));
  EXPECT_FALSE(GetAuthFactorMap().HasFactorWithStorage(
      AuthFactorStorageType::kVaultKeyset));
  ASSERT_EQ(GetAuthFactorMap().Find(kPasswordLabel)->storage_type(),
            AuthFactorStorageType::kUserSecretStash);
  ASSERT_EQ(GetAuthFactorMap().Find(kPasswordLabel2)->storage_type(),
            AuthFactorStorageType::kUserSecretStash);
}

// Test that VaultKeysets are migrated to UserSecretStash when migration is
// enabled, converting the existing VaultKeysets to migrated VaultKeysets.
TEST_F(AuthSessionTestWithKeysetManagement, MigrationEnabledMigratesToUss) {
  // Setup
  // Setup legacy VaultKeysets to test USS migration. On AuthSession start
  // legacy keyset should be migrated to USS following successful
  // authentication.
  KeysetSetUpWithKeyDataAndKeyBlobs(DefaultKeyData());
  KeyData key_data = DefaultKeyData();
  key_data.set_label(kPasswordLabel2);
  KeysetSetUpWithKeyDataAndKeyBlobs(key_data, 1);

  AuthSession auth_session2 = StartAuthSessionWithMockAuthBlockUtility();
  EXPECT_TRUE(GetAuthFactorMap().HasFactorWithStorage(
      AuthFactorStorageType::kVaultKeyset));
  EXPECT_FALSE(GetAuthFactorMap().HasFactorWithStorage(
      AuthFactorStorageType::kUserSecretStash));
  // Test that authenticating the password should migrate VaultKeyset to
  // UserSecretStash, converting the VaultKeyset to a backup VaultKeyset.
  AuthenticateAndMigrate(auth_session2, kPasswordLabel, kPassword);
  AuthSession auth_session3 = StartAuthSessionWithMockAuthBlockUtility();
  AuthenticateAndMigrate(auth_session3, kPasswordLabel2, kPassword2);

  // Verify
  // Verify that migrator loaded the user_secret_stash and uss_main_key.
  UssStorage uss_storage(&system_apis_.platform);
  UserUssStorage user_uss_storage(uss_storage, users_[0].obfuscated);
  CryptohomeStatusOr<brillo::SecureBlob> uss_credential_secret =
      kKeyBlobs.DeriveUssCredentialSecret();
  ASSERT_THAT(uss_credential_secret, IsOk());
  CryptohomeStatusOr<DecryptedUss> decrypted_uss =
      DecryptedUss::FromStorageUsingWrappedKey(user_uss_storage, kPasswordLabel,
                                               *uss_credential_secret);
  ASSERT_THAT(decrypted_uss, IsOk());

  // Verify that the user_secret_stash has the wrapped_key_blocks for the
  // AuthFactor labels.
  ASSERT_THAT(decrypted_uss->encrypted().WrappedMainKeyIds(),
              Contains(kPasswordLabel));
  ASSERT_THAT(decrypted_uss->encrypted().WrappedMainKeyIds(),
              Contains(kPasswordLabel2));
  //  Verify that the AuthFactors are created for the AuthFactor labels and
  //  storage type is updated in the AuthFactor map for each of them.
  ASSERT_EQ(GetAuthFactorMap().Find(kPasswordLabel)->storage_type(),
            AuthFactorStorageType::kUserSecretStash);
  ASSERT_EQ(GetAuthFactorMap().Find(kPasswordLabel2)->storage_type(),
            AuthFactorStorageType::kUserSecretStash);
}

// Test that after a VaultKeyset is migrated to UserSecretStash the next
// factor is added as USS factor.
TEST_F(AuthSessionTestWithKeysetManagement,
       MigrationEnabledAddNextFactorsToUss) {
  // Setup
  // Setup legacy VaultKeysets to test USS migration. On AuthSession start
  // legacy keyset should be migrated to USS following successful
  // authentication.
  KeysetSetUpWithKeyDataAndKeyBlobs(DefaultKeyData());

  AuthSession auth_session2 = StartAuthSessionWithMockAuthBlockUtility();
  EXPECT_TRUE(GetAuthFactorMap().HasFactorWithStorage(
      AuthFactorStorageType::kVaultKeyset));
  EXPECT_FALSE(GetAuthFactorMap().HasFactorWithStorage(
      AuthFactorStorageType::kUserSecretStash));
  // Test that authenticating the password should migrate VaultKeyset to
  // UserSecretStash, converting the VaultKeyset to a backup VaultKeyset.
  AuthenticateAndMigrate(auth_session2, kPasswordLabel, kPassword);

  // Test that adding a second factor adds as a USS AuthFactor.
  AddFactorWithMockAuthBlockUtility(auth_session2, kPasswordLabel2, kPassword2);

  // Verify
  // Create a new AuthSession for verifications.
  AuthSession auth_session3 = StartAuthSessionWithMockAuthBlockUtility();
  AuthenticateAndMigrate(auth_session3, kPasswordLabel2, kPassword2);

  // Verify that migrator created the user_secret_stash and uss_main_key.
  UssStorage uss_storage(&system_apis_.platform);
  UserUssStorage user_uss_storage(uss_storage, users_[0].obfuscated);
  CryptohomeStatusOr<brillo::SecureBlob> uss_credential_secret =
      kKeyBlobs.DeriveUssCredentialSecret();
  ASSERT_THAT(uss_credential_secret, IsOk());
  CryptohomeStatusOr<DecryptedUss> decrypted_uss =
      DecryptedUss::FromStorageUsingWrappedKey(user_uss_storage, kPasswordLabel,
                                               *uss_credential_secret);
  ASSERT_THAT(decrypted_uss, IsOk());

  // Verify that the user_secret_stash has the wrapped_key_blocks for both
  // AuthFactor labels.
  ASSERT_THAT(decrypted_uss->encrypted().WrappedMainKeyIds(),
              Contains(kPasswordLabel));
  ASSERT_THAT(decrypted_uss->encrypted().WrappedMainKeyIds(),
              Contains(kPasswordLabel2));
  //  Verify that the AuthFactors are created for the AuthFactor labels and
  //  storage type is updated in the AuthFactor map for each of them.
  absl::flat_hash_map<std::string, AuthFactorType> factor_map =
      system_apis_.auth_factor_manager.ListAuthFactors(users_[0].obfuscated);
  ASSERT_NE(factor_map.find(kPasswordLabel), factor_map.end());
  ASSERT_NE(factor_map.find(kPasswordLabel2), factor_map.end());
  ASSERT_EQ(GetAuthFactorMap().Find(kPasswordLabel)->storage_type(),
            AuthFactorStorageType::kUserSecretStash);
  ASSERT_EQ(GetAuthFactorMap().Find(kPasswordLabel2)->storage_type(),
            AuthFactorStorageType::kUserSecretStash);
}

// Test that AuthSession's auth factor map lists the factor from right backing
// store during the migration.
TEST_F(AuthSessionTestWithKeysetManagement,
       AuthFactorMapStatusDuringMigration) {
  // Setup
  // Setup legacy VaultKeysets to test USS migration. On AuthSession start
  // legacy keyset should be migrated to USS following successful
  // authentication.
  KeysetSetUpWithKeyDataAndKeyBlobs(DefaultKeyData());
  KeyData key_data = DefaultKeyData();
  key_data.set_label(kPasswordLabel2);
  KeysetSetUpWithKeyDataAndKeyBlobs(key_data, 1);

  AuthSession auth_session = StartAuthSessionWithMockAuthBlockUtility();
  EXPECT_THAT(auth_session.authorized_intents(), IsEmpty());
  EXPECT_TRUE(GetAuthFactorMap().HasFactorWithStorage(
      AuthFactorStorageType::kVaultKeyset));
  EXPECT_FALSE(GetAuthFactorMap().HasFactorWithStorage(
      AuthFactorStorageType::kUserSecretStash));
  ASSERT_EQ(GetAuthFactorMap().Find(kPasswordLabel)->storage_type(),
            AuthFactorStorageType::kVaultKeyset);
  ASSERT_EQ(GetAuthFactorMap().Find(kPasswordLabel2)->storage_type(),
            AuthFactorStorageType::kVaultKeyset);

  // Tests

  // 1- Test migration of the first factor. Storage type for the migrated factor
  // should be KUserSecretStash and non-migrated factor should be kVaultKeyset.
  AuthSession auth_session2 = StartAuthSessionWithMockAuthBlockUtility();
  AuthenticateAndMigrate(auth_session2, kPasswordLabel, kPassword);
  // auth_session3 should list both the migrated factor and the not migrated
  // VK
  AuthSession auth_session3 = StartAuthSessionWithMockAuthBlockUtility();
  EXPECT_TRUE(GetAuthFactorMap().HasFactorWithStorage(
      AuthFactorStorageType::kVaultKeyset));
  EXPECT_TRUE(GetAuthFactorMap().HasFactorWithStorage(
      AuthFactorStorageType::kUserSecretStash));
  ASSERT_EQ(GetAuthFactorMap().Find(kPasswordLabel)->storage_type(),
            AuthFactorStorageType::kUserSecretStash);
  ASSERT_EQ(GetAuthFactorMap().Find(kPasswordLabel2)->storage_type(),
            AuthFactorStorageType::kVaultKeyset);

  // 2- Test migration of the second factor on auth_session3. Storage type for
  // the migrated factors should be KUserSecretStash.
  AuthenticateAndMigrate(auth_session3, kPasswordLabel2, kPassword2);
  EXPECT_FALSE(GetAuthFactorMap().HasFactorWithStorage(
      AuthFactorStorageType::kVaultKeyset));
  EXPECT_TRUE(GetAuthFactorMap().HasFactorWithStorage(
      AuthFactorStorageType::kUserSecretStash));
  ASSERT_EQ(GetAuthFactorMap().Find(kPasswordLabel)->storage_type(),
            AuthFactorStorageType::kUserSecretStash);
  ASSERT_EQ(GetAuthFactorMap().Find(kPasswordLabel2)->storage_type(),
            AuthFactorStorageType::kUserSecretStash);
}

// Test that AuthSession's auth factor map lists the factor from right backing
// store on session start when USS is enabled.
TEST_F(AuthSessionTestWithKeysetManagement, AuthFactorMapUserSecretStash) {
  // Attach the mock_auth_block_utility to our AuthSessionManager and created
  // AuthSession.
  auto auth_session_manager_mock = std::make_unique<AuthSessionManager>(
      AuthSession::BackingApis{
          &system_apis_.crypto, &system_apis_.platform, &user_session_map_,
          &system_apis_.keyset_management, &mock_auth_block_utility_,
          &auth_factor_driver_manager_, &system_apis_.auth_factor_manager,
          &fp_migration_utility_, &system_apis_.uss_storage,
          &system_apis_.uss_manager, &features_.async},
      task_environment_.GetMainThreadTaskRunner().get());

  base::UnguessableToken token = auth_session_manager_mock->CreateAuthSession(
      Username(kUsername),
      {.is_ephemeral_user = false, .intent = AuthIntent::kDecrypt});

  TestFuture<InUseAuthSession> session_future;
  auth_session_manager_mock->RunWhenAvailable(token,
                                              session_future.GetCallback());
  InUseAuthSession auth_session = session_future.Take();
  EXPECT_THAT(auth_session.AuthSessionStatus(), IsOk());

  EXPECT_THAT(auth_session->authorized_intents(), IsEmpty());
  EXPECT_TRUE(auth_session->OnUserCreated().ok());
  EXPECT_THAT(
      auth_session->authorized_intents(),
      UnorderedElementsAre(AuthIntent::kDecrypt, AuthIntent::kVerifyOnly));
  EXPECT_FALSE(GetAuthFactorMap().HasFactorWithStorage(
      AuthFactorStorageType::kVaultKeyset));
  EXPECT_FALSE(GetAuthFactorMap().HasFactorWithStorage(
      AuthFactorStorageType::kUserSecretStash));

  // Test
  // Test that adding AuthFactors update the map to contain
  // these AuthFactors with kUserSecretStash backing store.
  AddFactorWithMockAuthBlockUtility(*auth_session, kPasswordLabel, kPassword);
  EXPECT_FALSE(GetAuthFactorMap().HasFactorWithStorage(
      AuthFactorStorageType::kVaultKeyset));
  EXPECT_TRUE(GetAuthFactorMap().HasFactorWithStorage(
      AuthFactorStorageType::kUserSecretStash));
  AddFactorWithMockAuthBlockUtility(*auth_session, kPasswordLabel2, kPassword2);
  EXPECT_FALSE(GetAuthFactorMap().HasFactorWithStorage(
      AuthFactorStorageType::kVaultKeyset));
  EXPECT_TRUE(GetAuthFactorMap().HasFactorWithStorage(
      AuthFactorStorageType::kUserSecretStash));

  // Verify that the |auth_factor_map| contains the two labels with
  // kUserSecretStash backing store.
  ASSERT_EQ(GetAuthFactorMap().Find(kPasswordLabel)->storage_type(),
            AuthFactorStorageType::kUserSecretStash);
  ASSERT_EQ(GetAuthFactorMap().Find(kPasswordLabel2)->storage_type(),
            AuthFactorStorageType::kUserSecretStash);
}

}  // namespace cryptohome
