// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/userdataauth.h"

#include <memory>
#include <utility>

#include <base/containers/span.h>
#include <base/memory/scoped_refptr.h>
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

#include "cryptohome/auth_blocks/auth_block_utility_impl.h"
#include "cryptohome/auth_blocks/mock_auth_block_utility.h"
#include "cryptohome/auth_factor/auth_factor_manager.h"
#include "cryptohome/auth_session.h"
#include "cryptohome/auth_session_manager.h"
#include "cryptohome/cleanup/mock_user_oldest_activity_timestamp_manager.h"
#include "cryptohome/credentials.h"
#include "cryptohome/crypto.h"
#include "cryptohome/crypto_error.h"
#include "cryptohome/error/cryptohome_crypto_error.h"
#include "cryptohome/error/cryptohome_error.h"
#include "cryptohome/mock_cryptohome_keys_manager.h"
#include "cryptohome/mock_install_attributes.h"
#include "cryptohome/mock_keyset_management.h"
#include "cryptohome/mock_le_credential_manager.h"
#include "cryptohome/mock_platform.h"
#include "cryptohome/pkcs11/mock_pkcs11_token_factory.h"
#include "cryptohome/storage/error.h"
#include "cryptohome/storage/mock_homedirs.h"
#include "cryptohome/storage/mock_mount.h"
#include "cryptohome/user_secret_stash_storage.h"
#include "cryptohome/user_session/mock_user_session.h"
#include "cryptohome/user_session/mock_user_session_factory.h"
#include "cryptohome/user_session/real_user_session.h"
#include "cryptohome/user_session/user_session_map.h"
#include "cryptohome/vault_keyset.h"

namespace cryptohome {

using ::testing::_;
using ::testing::An;
using ::testing::ByMove;
using ::testing::DoAll;
using ::testing::Eq;
using ::testing::Invoke;
using ::testing::IsEmpty;
using ::testing::IsNull;
using ::testing::NiceMock;
using ::testing::NotNull;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::SetArgPointee;
using ::testing::UnorderedElementsAre;

using base::test::TaskEnvironment;
using base::test::TestFuture;
using brillo::cryptohome::home::kGuestUserName;
using brillo::cryptohome::home::SanitizeUserName;
using error::CryptohomeCryptoError;
using error::CryptohomeError;
using error::CryptohomeMountError;
using hwsec_foundation::error::testing::IsOk;
using hwsec_foundation::error::testing::NotOk;
using hwsec_foundation::error::testing::ReturnError;
using hwsec_foundation::error::testing::ReturnOk;
using hwsec_foundation::error::testing::ReturnValue;
using hwsec_foundation::status::MakeStatus;
using hwsec_foundation::status::OkStatus;
using user_data_auth::AUTH_INTENT_DECRYPT;
using user_data_auth::AUTH_INTENT_VERIFY_ONLY;
using user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_EPHEMERAL_USER;

using AuthenticateCallback = base::OnceCallback<void(
    const user_data_auth::AuthenticateAuthSessionReply&)>;
using AddCredentialCallback =
    base::OnceCallback<void(const user_data_auth::AddCredentialsReply&)>;

namespace {

constexpr char kUsername[] = "foo@example.com";
constexpr char kPassword[] = "password";
constexpr char kUsername2[] = "foo2@example.com";
constexpr char kPassword2[] = "password2";
constexpr char kUsername3[] = "foo3@example.com";
constexpr char kPassword3[] = "password3";
constexpr char kPasswordLabel[] = "fake-password-label";
constexpr char kPin[] = "1234";
constexpr char kPinLabel[] = "fake-pin-label";
// 300 seconds should be left right as we authenticate.
constexpr int time_left_after_authenticate = 300;
SerializedVaultKeyset CreateFakePasswordVk(const std::string& label) {
  SerializedVaultKeyset serialized_vk;
  serialized_vk.set_flags(SerializedVaultKeyset::TPM_WRAPPED |
                          SerializedVaultKeyset::SCRYPT_DERIVED |
                          SerializedVaultKeyset::PCR_BOUND |
                          SerializedVaultKeyset::ECC);
  serialized_vk.set_password_rounds(1);
  serialized_vk.set_tpm_key("tpm-key");
  serialized_vk.set_extended_tpm_key("tpm-extended-key");
  serialized_vk.set_vkk_iv("iv");
  serialized_vk.set_wrapped_reset_seed("wrapped-reset-seed");
  serialized_vk.mutable_key_data()->set_type(KeyData::KEY_TYPE_PASSWORD);
  serialized_vk.mutable_key_data()->set_label(label);
  return serialized_vk;
}

SerializedVaultKeyset CreateFakePinVk(const std::string& label) {
  SerializedVaultKeyset serialized_vk;
  serialized_vk.set_flags(SerializedVaultKeyset::LE_CREDENTIAL);
  serialized_vk.mutable_key_data()->set_type(KeyData::KEY_TYPE_PASSWORD);
  serialized_vk.mutable_key_data()->set_label(label);
  serialized_vk.mutable_key_data()
      ->mutable_policy()
      ->set_low_entropy_credential(true);
  serialized_vk.set_salt("salt");
  serialized_vk.set_le_chaps_iv("le-chaps-iv");
  serialized_vk.set_le_label(0);
  serialized_vk.set_le_fek_iv("le-fek-iv");
  return serialized_vk;
}

void MockLabelToKeyDataMapLoading(
    const std::string& obfuscated_username,
    const std::vector<SerializedVaultKeyset>& serialized_vks,
    MockKeysetManagement& keyset_management) {
  KeyLabelMap key_label_map;
  for (const auto& serialized_vk : serialized_vks) {
    key_label_map[serialized_vk.key_data().label()] = serialized_vk.key_data();
  }
  EXPECT_CALL(keyset_management,
              GetVaultKeysetLabelsAndData(obfuscated_username, _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(key_label_map), Return(true)));
}

void MockKeysetsLoading(
    const std::string& obfuscated_username,
    const std::vector<SerializedVaultKeyset>& serialized_vks,
    MockKeysetManagement& keyset_management) {
  std::vector<int> key_indices;
  for (size_t index = 0; index < serialized_vks.size(); ++index) {
    key_indices.push_back(index);
  }
  EXPECT_CALL(keyset_management, GetVaultKeysets(obfuscated_username, _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(key_indices), Return(true)));
}

void MockKeysetLoadingByIndex(const std::string& obfuscated_username,
                              int index,
                              const SerializedVaultKeyset& serialized_vk,
                              MockKeysetManagement& keyset_management) {
  EXPECT_CALL(keyset_management,
              LoadVaultKeysetForUser(obfuscated_username, index))
      .WillRepeatedly([=](const std::string&, int) {
        auto vk = std::make_unique<VaultKeyset>();
        vk->InitializeFromSerialized(serialized_vk);
        return vk;
      });
}

void MockKeysetLoadingByLabel(const std::string& obfuscated_username,
                              const SerializedVaultKeyset& serialized_vk,
                              MockKeysetManagement& keyset_management) {
  EXPECT_CALL(
      keyset_management,
      GetVaultKeyset(obfuscated_username, serialized_vk.key_data().label()))
      .WillRepeatedly([=](const std::string&, const std::string&) {
        auto vk = std::make_unique<VaultKeyset>();
        vk->InitializeFromSerialized(serialized_vk);
        return vk;
      });
}

void MockKeysetDerivation(const std::string& obfuscated_username,
                          const SerializedVaultKeyset& serialized_vk,
                          CryptoError derivation_error,
                          MockAuthBlockUtility& auth_block_utility) {
  EXPECT_CALL(auth_block_utility,
              GetAuthBlockStateFromVaultKeyset(serialized_vk.key_data().label(),
                                               obfuscated_username, _))
      .WillOnce(Return(true));

  // Return an arbitrary auth block type from the mock.
  EXPECT_CALL(auth_block_utility, GetAuthBlockTypeFromState(_))
      .WillOnce(Return(AuthBlockType::kTpmEcc));

  const CryptohomeError::ErrorLocationPair fake_error_location =
      CryptohomeError::ErrorLocationPair(
          static_cast<CryptohomeError::ErrorLocation>(1),
          std::string("FakeErrorLocation"));

  EXPECT_CALL(auth_block_utility, DeriveKeyBlobsWithAuthBlockAsync(_, _, _, _))
      .WillOnce([=](AuthBlockType, const AuthInput&, const AuthBlockState&,
                    AuthBlock::DeriveCallback derive_callback) {
        std::move(derive_callback)
            .Run(derivation_error == CryptoError::CE_NONE
                     ? OkStatus<CryptohomeCryptoError>()
                     : MakeStatus<CryptohomeCryptoError>(
                           fake_error_location, error::ErrorActionSet(),
                           derivation_error),
                 std::make_unique<KeyBlobs>());
        return true;
      });
}

void MockKeysetCreation(MockAuthBlockUtility& auth_block_utility) {
  // Return an arbitrary auth block type from the mock.
  EXPECT_CALL(auth_block_utility, GetAuthBlockTypeForCreation(_, _, _))
      .WillOnce(Return(AuthBlockType::kTpmEcc))
      .RetiresOnSaturation();

  EXPECT_CALL(auth_block_utility, CreateKeyBlobsWithAuthBlockAsync(_, _, _))
      .WillOnce([](AuthBlockType, const AuthInput&,
                   AuthBlock::CreateCallback create_callback) {
        std::move(create_callback)
            .Run(OkStatus<CryptohomeCryptoError>(),
                 std::make_unique<KeyBlobs>(),
                 std::make_unique<AuthBlockState>());
        return true;
      })
      .RetiresOnSaturation();
}

void MockInitialKeysetAdding(const std::string& obfuscated_username,
                             const SerializedVaultKeyset& serialized_vk,
                             MockKeysetManagement& keyset_management) {
  EXPECT_CALL(keyset_management,
              AddInitialKeysetWithKeyBlobs(obfuscated_username, _, _, _, _, _))
      .WillOnce([=](const std::string&, const KeyData&,
                    const std::optional<
                        SerializedVaultKeyset_SignatureChallengeInfo>&,
                    const FileSystemKeyset& file_system_keyset, KeyBlobs,
                    std::unique_ptr<AuthBlockState>) {
        // Populate the VK with both public and secret data (like reset seed).
        auto vk = std::make_unique<VaultKeyset>();
        vk->InitializeFromSerialized(serialized_vk);
        vk->CreateFromFileSystemKeyset(file_system_keyset);
        return vk;
      });
}

void MockKeysetLoadingViaBlobs(const std::string& obfuscated_username,
                               const SerializedVaultKeyset& serialized_vk,
                               MockKeysetManagement& keyset_management) {
  EXPECT_CALL(keyset_management,
              GetValidKeysetWithKeyBlobs(obfuscated_username, _, _))
      .WillOnce(
          [=](const std::string&, KeyBlobs, const std::optional<std::string>&) {
            auto vk = std::make_unique<VaultKeyset>();
            vk->InitializeFromSerialized(serialized_vk);
            return vk;
          });
}

void MockOwnerUser(const std::string& username, MockHomeDirs& homedirs) {
  EXPECT_CALL(homedirs, GetPlainOwner(_))
      .WillRepeatedly(DoAll(SetArgPointee<0>(username), Return(true)));
}

}  // namespace

class AuthSessionInterfaceTestBase : public ::testing::Test {
 public:
  AuthSessionInterfaceTestBase()
      : crypto_(&hwsec_, &pinweaver_, &cryptohome_keys_manager_, nullptr) {
    SetUpHWSecExpectations();
    MockLECredentialManager* le_cred_manager = new MockLECredentialManager();
    crypto_.set_le_manager_for_testing(
        std::unique_ptr<cryptohome::LECredentialManager>(le_cred_manager));
    crypto_.Init();

    userdataauth_.set_platform(&platform_);
    userdataauth_.set_homedirs(&homedirs_);
    userdataauth_.set_user_session_factory(&user_session_factory_);
    userdataauth_.set_keyset_management(&keyset_management_);
    userdataauth_.set_auth_factor_manager_for_testing(&auth_factor_manager_);
    userdataauth_.set_user_secret_stash_storage_for_testing(
        &user_secret_stash_storage_);
    userdataauth_.set_user_session_map_for_testing(&user_session_map_);
    userdataauth_.set_pkcs11_token_factory(&pkcs11_token_factory_);
    userdataauth_.set_user_activity_timestamp_manager(
        &user_activity_timestamp_manager_);
    userdataauth_.set_install_attrs(&install_attrs_);
    userdataauth_.set_mount_task_runner(
        task_environment.GetMainThreadTaskRunner());
    userdataauth_.set_current_thread_id_for_test(
        UserDataAuth::TestThreadId::kMountThread);
  }

  void SetUpHWSecExpectations() {
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
  }

  void CreateAuthSessionManager(AuthBlockUtility* auth_block_utility) {
    auth_session_manager_ = std::make_unique<AuthSessionManager>(
        &crypto_, &platform_, &user_session_map_, &keyset_management_,
        auth_block_utility, &auth_factor_manager_, &user_secret_stash_storage_);
    userdataauth_.set_auth_session_manager(auth_session_manager_.get());
  }

 protected:
  TaskEnvironment task_environment{
      TaskEnvironment::TimeSource::MOCK_TIME,
      TaskEnvironment::ThreadPoolExecutionMode::QUEUED};
  NiceMock<MockPlatform> platform_;
  UserSessionMap user_session_map_;
  NiceMock<MockHomeDirs> homedirs_;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager_;
  NiceMock<hwsec::MockCryptohomeFrontend> hwsec_;
  NiceMock<hwsec::MockPinWeaverFrontend> pinweaver_;
  Crypto crypto_;
  NiceMock<MockUserSessionFactory> user_session_factory_;
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

  void AddCredentials(
      user_data_auth::AddCredentialsRequest request,
      base::OnceCallback<void(const user_data_auth::AddCredentialsReply&)>
          on_done) {
    userdataauth_.AddCredentials(request, std::move(on_done));
  }

  void AuthenticateAuthSession(
      user_data_auth::AuthenticateAuthSessionRequest request,
      base::OnceCallback<
          void(const user_data_auth::AuthenticateAuthSessionReply&)> on_done) {
    userdataauth_.AuthenticateAuthSession(request, std::move(on_done));
  }

  void GetAuthSessionStatusImpl(
      AuthSession* auth_session,
      user_data_auth::GetAuthSessionStatusReply& reply) {
    userdataauth_.GetAuthSessionStatusImpl(auth_session, reply);
  }
};

class AuthSessionInterfaceTest : public AuthSessionInterfaceTestBase {
 protected:
  AuthSessionInterfaceTest() {
    auth_block_utility_impl_ = std::make_unique<AuthBlockUtilityImpl>(
        &keyset_management_, &crypto_, &platform_);
    CreateAuthSessionManager(auth_block_utility_impl_.get());
  }

  void SetAuthSessionAsAuthenticated(AuthSession* auth_session,
                                     base::span<const AuthIntent> intents) {
    auth_session->SetAuthSessionAsAuthenticated(intents);
  }

  AuthorizationRequest CreateAuthorization(const std::string& secret) {
    AuthorizationRequest req;
    req.mutable_key()->set_secret(secret);
    req.mutable_key()->mutable_data()->set_label("test-label");
    req.mutable_key()->mutable_data()->set_type(KeyData::KEY_TYPE_PASSWORD);
    return req;
  }

  void ExpectAuth(const std::string& username,
                  const brillo::SecureBlob& secret) {
    auto vk = std::make_unique<VaultKeyset>();
    Credentials creds(username, secret);
    EXPECT_CALL(keyset_management_, GetValidKeysetWithKeyBlobs(_, _, _))
        .WillOnce(Return(ByMove(std::move(vk))));
  }

  void ExpectVaultKeyset(int num_of_keysets) {
    // Assert parameter num_of_calls cannot be negative.
    DCHECK_GT(num_of_keysets, 0);

    // Setup expectations for GetVaultKeyset to return an initialized
    // VaultKeyset Construct the vault keyset with credentials for
    // AuthBlockType::kTpmNotBoundToPcrAuthBlockState.
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

    EXPECT_CALL(keyset_management_, GetVaultKeyset(_, _))
        .Times(num_of_keysets)
        .WillRepeatedly([=](const std::string& obfuscated_username,
                            const std::string& key_label) {
          auto vk = std::make_unique<VaultKeyset>();
          vk->InitializeFromSerialized(serialized);
          return vk;
        });
  }

  std::unique_ptr<AuthBlockUtilityImpl> auth_block_utility_impl_;
};

namespace {

TEST_F(AuthSessionInterfaceTest, PrepareGuestVault) {
  auto user_session = std::make_unique<MockUserSession>();
  EXPECT_CALL(*user_session, IsActive()).WillRepeatedly(Return(true));
  EXPECT_CALL(*user_session, MountGuest()).WillOnce(Invoke([]() {
    return OkStatus<CryptohomeMountError>();
  }));
  EXPECT_CALL(user_session_factory_, New(_, _, _))
      .WillOnce(Return(ByMove(std::move(user_session))));

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
  ExpectVaultKeyset(/*num_of_keysets=*/1);

  AuthSession* auth_session = auth_session_manager_->CreateAuthSession(
      kUsername, AUTH_SESSION_FLAGS_EPHEMERAL_USER, AuthIntent::kDecrypt,
      false);
  TestFuture<CryptohomeStatus> authenticate_future;
  auth_session->Authenticate(CreateAuthorization(kPassword),
                             authenticate_future.GetCallback());
  EXPECT_THAT(authenticate_future.Get(), IsOk());
  status = PrepareEphemeralVaultImpl(auth_session->serialized_token());
  ASSERT_FALSE(status.ok());
  ASSERT_EQ(status->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY);

  // ... or regular.

  auth_session = auth_session_manager_->CreateAuthSession(
      kUsername2, 0, AuthIntent::kDecrypt,
      /*enable_create_backup_vk_with_uss =*/false);
  TestFuture<CryptohomeStatus> authenticate_regular_future;
  auth_session->Authenticate(CreateAuthorization(kPassword2),
                             authenticate_regular_future.GetCallback());
  EXPECT_THAT(authenticate_regular_future.Get(), IsOk());
  status = PreparePersistentVaultImpl(auth_session->serialized_token(), {});
  ASSERT_FALSE(status.ok());
  ASSERT_EQ(status->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY);
}

TEST_F(AuthSessionInterfaceTest, PrepareEphemeralVault) {
  MockOwnerUser("whoever", homedirs_);

  // No auth session.
  CryptohomeStatus status = PrepareEphemeralVaultImpl("");
  ASSERT_FALSE(status.ok());
  ASSERT_EQ(status->local_legacy_error(),
            user_data_auth::CRYPTOHOME_INVALID_AUTH_SESSION_TOKEN);

  // Auth session is initially not authenticated for ephemeral users.
  AuthSession* auth_session = auth_session_manager_->CreateAuthSession(
      kUsername, AUTH_SESSION_FLAGS_EPHEMERAL_USER, AuthIntent::kDecrypt,
      /*enable_create_backup_vk_with_uss =*/false);
  EXPECT_THAT(auth_session->GetStatus(),
              AuthStatus::kAuthStatusFurtherFactorRequired);

  // User authed and exists.
  auto user_session = std::make_unique<MockUserSession>();
  EXPECT_CALL(*user_session, IsActive())
      .WillOnce(Return(false))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*user_session, SetCredentials(An<const Credentials&>()));
  EXPECT_CALL(*user_session, GetPkcs11Token()).WillRepeatedly(Return(nullptr));
  EXPECT_CALL(*user_session, IsEphemeral()).WillRepeatedly(Return(true));
  EXPECT_CALL(*user_session, MountEphemeral(kUsername))
      .WillOnce(ReturnError<CryptohomeMountError>());
  EXPECT_CALL(user_session_factory_, New(_, _, _))
      .WillOnce(Return(ByMove(std::move(user_session))));

  ASSERT_TRUE(PrepareEphemeralVaultImpl(auth_session->serialized_token()).ok());
  EXPECT_THAT(auth_session->GetStatus(), AuthStatus::kAuthStatusAuthenticated);
  EXPECT_EQ(auth_session->GetRemainingTime().InSeconds(),
            time_left_after_authenticate);

  // Set up expectation for add credential callback success.
  user_data_auth::AddCredentialsRequest request;
  request.set_auth_session_id(auth_session->serialized_token());
  *request.mutable_authorization() = CreateAuthorization(kPassword);

  user_data_auth::AddCredentialsReply reply;
  base::MockCallback<AddCredentialCallback> on_done;
  EXPECT_CALL(on_done, Run(_)).WillOnce(SaveArg<0>(&reply));
  AddCredentials(request, on_done.Get());

  // Evaluate error returned by callback.
  ASSERT_THAT(reply.error(), Eq(user_data_auth::CRYPTOHOME_ERROR_NOT_SET));

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

  // And so does ephemeral
  AuthSession* auth_session2 = auth_session_manager_->CreateAuthSession(
      kUsername2, AUTH_SESSION_FLAGS_EPHEMERAL_USER, AuthIntent::kDecrypt,
      false);
  status = PrepareEphemeralVaultImpl(auth_session2->serialized_token());
  ASSERT_FALSE(status.ok());
  ASSERT_EQ(status->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY);

  // But a different regular mount succeeds.
  auto user_session3 = std::make_unique<MockUserSession>();
  EXPECT_CALL(*user_session3, IsActive())
      .WillOnce(Return(false))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*user_session3, MountVault(kUsername3, _, _))
      .WillOnce(ReturnError<CryptohomeMountError>());
  EXPECT_CALL(user_session_factory_, New(_, _, _))
      .WillOnce(Return(ByMove(std::move(user_session3))));
  EXPECT_CALL(homedirs_, Exists(SanitizeUserName(kUsername3)))
      .WillRepeatedly(Return(true));
  ExpectAuth(kUsername3, brillo::SecureBlob(kPassword3));

  AuthSession* auth_session3 = auth_session_manager_->CreateAuthSession(
      kUsername3, 0, AuthIntent::kDecrypt,
      /*enable_create_backup_vk_with_uss =*/false);
  ExpectVaultKeyset(/*num_of_keysets=*/1);

  TestFuture<CryptohomeStatus> authenticate_third_future;
  auth_session3->Authenticate(CreateAuthorization(kPassword3),
                              authenticate_third_future.GetCallback());
  EXPECT_THAT(authenticate_third_future.Get(), IsOk());
  ASSERT_TRUE(
      PreparePersistentVaultImpl(auth_session3->serialized_token(), {}).ok());
}

// Test if PreparePersistentVaultImpl can succeed with invalid authSession. It
// should not.
TEST_F(AuthSessionInterfaceTest, PreparePersistentVaultWithInvalidAuthSession) {
  // No auth session.
  CryptohomeStatus status =
      PreparePersistentVaultImpl(/*auth_session_id=*/"", /*vault_options=*/{});
  ASSERT_FALSE(status.ok());
  ASSERT_EQ(status->local_legacy_error(),
            user_data_auth::CRYPTOHOME_INVALID_AUTH_SESSION_TOKEN);
}

// Test for checking if PreparePersistentVaultImpl will proceed with
// unauthenticated auth session.
TEST_F(AuthSessionInterfaceTest,
       PreparePersistentVaultWithUnAuthenticatedAuthSession) {
  AuthSession* auth_session = auth_session_manager_->CreateAuthSession(
      kUsername, 0, AuthIntent::kDecrypt,
      /*enable_create_backup_vk_with_uss =*/false);
  CryptohomeStatus status =
      PreparePersistentVaultImpl(auth_session->serialized_token(), {});
  ASSERT_FALSE(status.ok());
  ASSERT_EQ(status->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
}

// Test to check if PreparePersistentVaultImpl will succeed if user is not
// created.
TEST_F(AuthSessionInterfaceTest, PreparePersistentVaultNoShadowDir) {
  AuthSession* auth_session = auth_session_manager_->CreateAuthSession(
      kUsername, 0, AuthIntent::kDecrypt,
      /*enable_create_backup_vk_with_uss =*/false);
  SetAuthSessionAsAuthenticated(auth_session, kAllAuthIntents);

  // If no shadow homedir - we do not have a user.
  EXPECT_CALL(homedirs_, Exists(SanitizeUserName(kUsername)))
      .WillRepeatedly(Return(false));

  CryptohomeStatus status =
      PreparePersistentVaultImpl(auth_session->serialized_token(), {});

  ASSERT_FALSE(status.ok());
  ASSERT_EQ(status->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND);
}

// Test to check if PreparePersistentVaultImpl will succeed in happy case and
// calls the required functions.
TEST_F(AuthSessionInterfaceTest, PreparePersistentVaultRegularCase) {
  MockOwnerUser("whoever", homedirs_);

  AuthSession* auth_session = auth_session_manager_->CreateAuthSession(
      kUsername, 0, AuthIntent::kDecrypt,
      /*enable_create_backup_vk_with_uss =*/false);
  // Auth and prepare.
  auto user_session = std::make_unique<MockUserSession>();
  EXPECT_CALL(*user_session, IsActive())
      .WillOnce(Return(false))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*user_session, IsEphemeral()).WillRepeatedly(Return(false));
  EXPECT_CALL(*user_session, GetCredentialVerifier()).WillOnce(Return(nullptr));
  EXPECT_CALL(*user_session, SetCredentials(auth_session));
  EXPECT_CALL(*user_session, MountVault(kUsername, _, _))
      .WillOnce(ReturnError<CryptohomeMountError>());
  EXPECT_CALL(user_session_factory_, New(kUsername, _, _))
      .WillOnce(Return(ByMove(std::move(user_session))));

  ExpectVaultKeyset(/*num_of_keysets=*/1);
  ExpectAuth(kUsername, brillo::SecureBlob(kPassword));

  // Set up expectation for authenticate callback success.
  user_data_auth::AuthenticateAuthSessionRequest request;
  request.set_auth_session_id(auth_session->serialized_token());
  *request.mutable_authorization() = CreateAuthorization(kPassword);

  base::MockCallback<AuthenticateCallback> on_done;
  user_data_auth::AuthenticateAuthSessionReply reply;
  EXPECT_CALL(on_done, Run(testing::_)).WillOnce(testing::SaveArg<0>(&reply));

  AuthenticateAuthSession(request, on_done.Get());
  ASSERT_THAT(reply.error(), Eq(MOUNT_ERROR_NONE));

  // User authed and exists.
  EXPECT_CALL(homedirs_, Exists(SanitizeUserName(kUsername)))
      .WillRepeatedly(Return(true));
  ASSERT_TRUE(
      PreparePersistentVaultImpl(auth_session->serialized_token(), {}).ok());
}

// Test to check if PreparePersistentVaultImpl will succeed, call required
// functions and not succeed when PreparePersistentVault is called twice.
TEST_F(AuthSessionInterfaceTest, PreparePersistentVaultSecondMountPointBusy) {
  MockOwnerUser("whoever", homedirs_);

  AuthSession* auth_session = auth_session_manager_->CreateAuthSession(
      kUsername, 0, AuthIntent::kDecrypt,
      /*enable_create_backup_vk_with_uss =*/false);

  // Auth and prepare.
  auto user_session = std::make_unique<MockUserSession>();
  EXPECT_CALL(*user_session, IsActive())
      .WillOnce(Return(false))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*user_session, IsEphemeral()).WillRepeatedly(Return(false));
  EXPECT_CALL(*user_session, GetCredentialVerifier()).WillOnce(Return(nullptr));
  EXPECT_CALL(*user_session, SetCredentials(auth_session));
  EXPECT_CALL(*user_session, MountVault(kUsername, _, _))
      .WillOnce(ReturnError<CryptohomeMountError>());
  EXPECT_CALL(user_session_factory_, New(kUsername, _, _))
      .WillOnce(Return(ByMove(std::move(user_session))));

  ExpectVaultKeyset(/*num_of_keysets=*/1);
  ExpectAuth(kUsername, brillo::SecureBlob(kPassword));

  // Set up expectation for authenticate callback success.
  user_data_auth::AuthenticateAuthSessionRequest request;
  request.set_auth_session_id(auth_session->serialized_token());
  *request.mutable_authorization() = CreateAuthorization(kPassword);

  user_data_auth::AuthenticateAuthSessionReply reply;
  base::MockCallback<AuthenticateCallback> on_done;
  EXPECT_CALL(on_done, Run(_)).WillOnce(SaveArg<0>(&reply));

  AuthenticateAuthSession(request, on_done.Get());
  ASSERT_THAT(reply.error(), Eq(MOUNT_ERROR_NONE));

  // User authed and exists.
  EXPECT_CALL(homedirs_, Exists(SanitizeUserName(kUsername)))
      .WillRepeatedly(Return(true));
  ASSERT_TRUE(
      PreparePersistentVaultImpl(auth_session->serialized_token(), {}).ok());

  // Trying to mount again will yield busy.
  auto status =
      PreparePersistentVaultImpl(auth_session->serialized_token(), {});
  ASSERT_FALSE(status.ok());
  ASSERT_EQ(status->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY);
}

TEST_F(AuthSessionInterfaceTest, PreparePersistentVaultAndThenGuestFail) {
  // Test to check if PreparePersistentVaultImpl will succeed, call required
  // functions and mounting guest would not succeed.
  MockOwnerUser("whoever", homedirs_);
  AuthSession* auth_session = auth_session_manager_->CreateAuthSession(
      kUsername, 0, AuthIntent::kDecrypt,
      /*enable_create_backup_vk_with_uss =*/false);

  // Auth and prepare.
  auto user_session = std::make_unique<MockUserSession>();
  EXPECT_CALL(*user_session, IsActive())
      .WillOnce(Return(false))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*user_session, MountVault(kUsername, _, _))
      .WillOnce(ReturnError<CryptohomeMountError>());
  EXPECT_CALL(user_session_factory_, New(kUsername, _, _))
      .WillOnce(Return(ByMove(std::move(user_session))));
  EXPECT_CALL(homedirs_, Exists(SanitizeUserName(kUsername)))
      .WillRepeatedly(Return(true));

  // Set up expectations.
  ExpectVaultKeyset(/*num_of_keysets=*/1);
  ExpectAuth(kUsername, brillo::SecureBlob(kPassword));

  TestFuture<CryptohomeStatus> authenticate_future;
  auth_session->Authenticate(CreateAuthorization(kPassword),
                             authenticate_future.GetCallback());
  // Evaluate error returned by callback.
  EXPECT_THAT(authenticate_future.Get(), IsOk());

  // User authed and exists.
  EXPECT_CALL(homedirs_, Exists(SanitizeUserName(kUsername)))
      .WillRepeatedly(Return(true));
  ASSERT_TRUE(
      PreparePersistentVaultImpl(auth_session->serialized_token(), {}).ok());
  // Guest fails if other sessions present.
  auto status = PrepareGuestVaultImpl();
  ASSERT_FALSE(status.ok());
  ASSERT_EQ(status->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_MOUNT_FATAL);
}

TEST_F(AuthSessionInterfaceTest, PreparePersistentVaultAndEphemeral) {
  // Test to check if PreparePersistentVaultImpl will succeed, call required
  // functions and mounting ephemeral will succeed as we support multi mount for
  // that.
  MockOwnerUser("whoever", homedirs_);

  // Setup regular user.
  AuthSession* auth_session = auth_session_manager_->CreateAuthSession(
      kUsername, 0, AuthIntent::kDecrypt,
      /*enable_create_backup_vk_with_uss =*/false);

  // Auth and prepare.
  auto user_session = std::make_unique<MockUserSession>();
  EXPECT_CALL(*user_session, IsActive())
      .WillOnce(Return(false))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*user_session, IsEphemeral()).WillRepeatedly(Return(false));
  EXPECT_CALL(*user_session, GetCredentialVerifier()).WillOnce(Return(nullptr));
  EXPECT_CALL(*user_session, SetCredentials(auth_session));
  EXPECT_CALL(*user_session, MountVault(kUsername, _, _))
      .WillOnce(ReturnError<CryptohomeMountError>());
  EXPECT_CALL(user_session_factory_, New(kUsername, _, _))
      .WillOnce(Return(ByMove(std::move(user_session))));

  ExpectVaultKeyset(/*num_of_keysets=*/1);
  ExpectAuth(kUsername, brillo::SecureBlob(kPassword));

  // Set up expectation for authenticate callback success.
  user_data_auth::AuthenticateAuthSessionRequest request;
  request.set_auth_session_id(auth_session->serialized_token());
  *request.mutable_authorization() = CreateAuthorization(kPassword);

  user_data_auth::AuthenticateAuthSessionReply reply;
  base::MockCallback<AuthenticateCallback> on_done;
  EXPECT_CALL(on_done, Run(_)).WillOnce(SaveArg<0>(&reply));

  AuthenticateAuthSession(request, on_done.Get());
  ASSERT_THAT(reply.error(), Eq(MOUNT_ERROR_NONE));

  // User authed and exists.
  EXPECT_CALL(homedirs_, Exists(SanitizeUserName(kUsername)))
      .WillRepeatedly(Return(true));
  ASSERT_TRUE(
      PreparePersistentVaultImpl(auth_session->serialized_token(), {}).ok());

  // Setup ephemeral user. This should fail.
  AuthSession* auth_session2 = auth_session_manager_->CreateAuthSession(
      kUsername2, AUTH_SESSION_FLAGS_EPHEMERAL_USER, AuthIntent::kDecrypt,
      false);
  CryptohomeStatus status =
      PrepareEphemeralVaultImpl(auth_session2->serialized_token());
  ASSERT_FALSE(status.ok());
  ASSERT_EQ(status->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY);
}

// Test to check if PreparePersistentVaultImpl will succeed, call required
// functions and PreparePersistentVault will succeed for another user as we
// support multi mount.
TEST_F(AuthSessionInterfaceTest, PreparePersistentVaultMultiMount) {
  AuthSession* auth_session = auth_session_manager_->CreateAuthSession(
      kUsername, 0, AuthIntent::kDecrypt,
      /*enable_create_backup_vk_with_uss =*/false);

  // Auth and prepare.
  auto user_session = std::make_unique<MockUserSession>();
  EXPECT_CALL(*user_session, IsActive())
      .WillOnce(Return(false))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*user_session, IsEphemeral()).WillRepeatedly(Return(false));
  EXPECT_CALL(*user_session, GetCredentialVerifier()).WillOnce(Return(nullptr));
  EXPECT_CALL(*user_session, SetCredentials(auth_session));
  EXPECT_CALL(*user_session, MountVault(kUsername, _, _))
      .WillOnce(ReturnError<CryptohomeMountError>());
  EXPECT_CALL(user_session_factory_, New(kUsername, _, _))
      .WillOnce(Return(ByMove(std::move(user_session))));

  ExpectVaultKeyset(/*num_of_keysets=*/1);
  ExpectAuth(kUsername, brillo::SecureBlob(kPassword));

  // Set up expectation for authenticate callback success.
  user_data_auth::AuthenticateAuthSessionRequest request;
  request.set_auth_session_id(auth_session->serialized_token());
  *request.mutable_authorization() = CreateAuthorization(kPassword);

  user_data_auth::AuthenticateAuthSessionReply reply;
  base::MockCallback<AuthenticateCallback> on_done;
  EXPECT_CALL(on_done, Run(_)).WillOnce(SaveArg<0>(&reply));

  AuthenticateAuthSession(request, on_done.Get());
  ASSERT_THAT(reply.error(), Eq(MOUNT_ERROR_NONE));

  // User authed and exists.
  EXPECT_CALL(homedirs_, Exists(SanitizeUserName(kUsername)))
      .WillRepeatedly(Return(true));
  ASSERT_TRUE(
      PreparePersistentVaultImpl(auth_session->serialized_token(), {}).ok());

  // Second mount should also succeed.
  AuthSession* auth_session2 = auth_session_manager_->CreateAuthSession(
      kUsername2, 0, AuthIntent::kDecrypt,
      /*enable_create_backup_vk_with_uss =*/false);
  auto user_session2 = std::make_unique<MockUserSession>();
  EXPECT_CALL(*user_session2, IsActive())
      .WillOnce(Return(false))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*user_session2, IsEphemeral()).WillRepeatedly(Return(false));
  EXPECT_CALL(*user_session2, GetCredentialVerifier())
      .WillOnce(Return(nullptr));
  EXPECT_CALL(*user_session2, SetCredentials(auth_session2));
  EXPECT_CALL(*user_session2, MountVault(kUsername2, _, _))
      .WillOnce(ReturnError<CryptohomeMountError>());
  EXPECT_CALL(user_session_factory_, New(_, _, _))
      .WillOnce(Return(ByMove(std::move(user_session2))));
  EXPECT_CALL(homedirs_, Exists(SanitizeUserName(kUsername2)))
      .WillRepeatedly(Return(true));

  // Set up expectation for authenticate callback success.
  user_data_auth::AuthenticateAuthSessionRequest request2;
  user_data_auth::AuthenticateAuthSessionReply reply2;
  request2.set_auth_session_id(auth_session2->serialized_token());
  AuthorizationRequest auth_req2 = CreateAuthorization(kPassword2);
  request2.mutable_authorization()->Swap(&auth_req2);
  base::MockCallback<AuthenticateCallback> on_done2;
  EXPECT_CALL(on_done2, Run(_)).WillOnce(SaveArg<0>(&reply2));

  ExpectVaultKeyset(/*num_of_keysets=*/1);
  ExpectAuth(kUsername2, brillo::SecureBlob(kPassword2));

  AuthenticateAuthSession(request2, on_done2.Get());
  ASSERT_TRUE(
      PreparePersistentVaultImpl(auth_session2->serialized_token(), {}).ok());
  // Evaluate error returned by callback.
  ASSERT_THAT(reply2.error(), Eq(MOUNT_ERROR_NONE));
}

// Test CreatePersistentUserImpl with invalid auth_session.
TEST_F(AuthSessionInterfaceTest, CreatePersistentUserInvalidAuthSession) {
  // No auth session.
  ASSERT_THAT(CreatePersistentUserImpl("")->local_legacy_error().value(),
              Eq(user_data_auth::CRYPTOHOME_INVALID_AUTH_SESSION_TOKEN));
}

// Test CreatePersistentUserImpl with valid auth_session but user fails to
// create.
TEST_F(AuthSessionInterfaceTest, CreatePersistentUserFailedCreate) {
  EXPECT_CALL(homedirs_, CryptohomeExists(SanitizeUserName(kUsername)))
      .WillOnce(ReturnValue(false));
  AuthSession* auth_session = auth_session_manager_->CreateAuthSession(
      kUsername, 0, AuthIntent::kDecrypt,
      /*enable_create_backup_vk_with_uss =*/false);
  EXPECT_CALL(homedirs_, Exists(SanitizeUserName(kUsername)))
      .WillOnce(Return(false));
  EXPECT_CALL(homedirs_, Create(kUsername)).WillOnce(Return(false));
  auto status = CreatePersistentUserImpl(auth_session->serialized_token());
  ASSERT_FALSE(status.ok());
  ASSERT_THAT(status->local_legacy_error(),
              Eq(user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE));
}

// Test CreatePersistentUserImpl when Vault already exists.
TEST_F(AuthSessionInterfaceTest, CreatePersistentUserVaultExists) {
  AuthSession* auth_session = auth_session_manager_->CreateAuthSession(
      kUsername, 0, AuthIntent::kDecrypt,
      /*enable_create_backup_vk_with_uss =*/false);
  EXPECT_CALL(homedirs_, CryptohomeExists(SanitizeUserName(kUsername)))
      .WillOnce(ReturnValue(true));
  ASSERT_THAT(CreatePersistentUserImpl(auth_session->serialized_token())
                  ->local_legacy_error()
                  .value(),
              Eq(user_data_auth::CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY));
}

// Test CreatePersistentUserImpl with regular and expected case.
TEST_F(AuthSessionInterfaceTest, CreatePersistentUserRegular) {
  EXPECT_CALL(keyset_management_, UserExists(SanitizeUserName(kUsername)))
      .WillOnce(ReturnValue(false));
  AuthSession* auth_session = auth_session_manager_->CreateAuthSession(
      kUsername, 0, AuthIntent::kDecrypt,
      /*enable_create_backup_vk_with_uss =*/false);
  EXPECT_FALSE(auth_session->user_exists());
  // User doesn't exist and created.
  EXPECT_CALL(homedirs_, CryptohomeExists(SanitizeUserName(kUsername)))
      .WillOnce(ReturnValue(false));
  EXPECT_CALL(homedirs_, Exists(SanitizeUserName(kUsername)))
      .WillOnce(Return(false));
  EXPECT_CALL(homedirs_, Create(kUsername)).WillOnce(Return(true));
  ASSERT_TRUE(CreatePersistentUserImpl(auth_session->serialized_token()).ok());
  EXPECT_THAT(auth_session->GetStatus(), AuthStatus::kAuthStatusAuthenticated);
  EXPECT_EQ(auth_session->GetRemainingTime().InSeconds(),
            time_left_after_authenticate);

  // Set UserSession expectations for upcoming mount.
  // Auth and prepare.
  auto owned_user_session = std::make_unique<MockUserSession>();
  auto* const user_session = owned_user_session.get();
  EXPECT_CALL(user_session_factory_, New(kUsername, _, _))
      .WillOnce(Return(ByMove(std::move(owned_user_session))));
  EXPECT_CALL(*user_session, IsActive())
      .WillOnce(Return(false))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*user_session, MountVault(kUsername, _, _))
      .WillOnce(ReturnError<CryptohomeMountError>());

  // User authed and exists.
  EXPECT_CALL(homedirs_, Exists(SanitizeUserName(kUsername)))
      .WillRepeatedly(Return(true));
  ASSERT_TRUE(
      PreparePersistentVaultImpl(auth_session->serialized_token(), {}).ok());

  // Set expectations for credential verifier.
  EXPECT_CALL(*user_session, IsEphemeral()).WillRepeatedly(Return(false));
  EXPECT_CALL(*user_session, GetCredentialVerifier()).WillOnce(Return(nullptr));
  EXPECT_CALL(*user_session, SetCredentials(auth_session));
  // Set up expectation for add credential callback success.
  user_data_auth::AddCredentialsRequest request;
  user_data_auth::AddCredentialsReply reply;
  request.set_auth_session_id(auth_session->serialized_token());
  *request.mutable_authorization() = CreateAuthorization(kPassword);

  base::MockCallback<AddCredentialCallback> on_done;
  EXPECT_CALL(on_done, Run(testing::_)).WillOnce(testing::SaveArg<0>(&reply));
  EXPECT_CALL(keyset_management_,
              AddInitialKeysetWithKeyBlobs(_, _, _, _, _, _))
      .WillOnce(Return(ByMove(std::make_unique<VaultKeyset>())));

  AddCredentials(request, on_done.Get());

  // Evaluate error returned by callback.
  ASSERT_THAT(reply.error(), Eq(user_data_auth::CRYPTOHOME_ERROR_NOT_SET));
}

TEST_F(AuthSessionInterfaceTest, CreatePersistentUserRepeatCall) {
  EXPECT_CALL(keyset_management_, UserExists(SanitizeUserName(kUsername)))
      .WillOnce(ReturnValue(false));
  AuthSession* auth_session = auth_session_manager_->CreateAuthSession(
      kUsername, 0, AuthIntent::kDecrypt,
      /*enable_create_backup_vk_with_uss =*/false);

  EXPECT_CALL(homedirs_, CryptohomeExists(SanitizeUserName(kUsername)))
      .WillOnce(ReturnValue(false));
  EXPECT_CALL(homedirs_, Exists(SanitizeUserName(kUsername)))
      .WillOnce(Return(false));
  EXPECT_CALL(homedirs_, Create(kUsername)).WillOnce(Return(true));
  ASSERT_TRUE(CreatePersistentUserImpl(auth_session->serialized_token()).ok());
  EXPECT_THAT(auth_session->GetStatus(), AuthStatus::kAuthStatusAuthenticated);
  EXPECT_EQ(auth_session->GetRemainingTime().InSeconds(),
            time_left_after_authenticate);

  // Called again. User exists, vault should not be created again.
  EXPECT_CALL(homedirs_, CryptohomeExists(SanitizeUserName(kUsername)))
      .WillOnce(ReturnValue(false));
  EXPECT_CALL(homedirs_, Exists(SanitizeUserName(kUsername)))
      .WillOnce(Return(true));
  ASSERT_TRUE(CreatePersistentUserImpl(auth_session->serialized_token()).ok());
}

TEST_F(AuthSessionInterfaceTest, AuthenticateAuthSessionNoLabel) {
  // Auth session not authed.
  AuthSession* auth_session = auth_session_manager_->CreateAuthSession(
      kUsername, 0, AuthIntent::kDecrypt,
      /*enable_create_backup_vk_with_uss =*/false);

  // Pass no label in the request.
  AuthorizationRequest auth_req;
  auth_req.mutable_key()->set_secret(kPassword);
  auth_req.mutable_key()->mutable_data()->set_type(KeyData::KEY_TYPE_PASSWORD);
  TestFuture<CryptohomeStatus> authenticate_future;
  auth_session->Authenticate(auth_req, authenticate_future.GetCallback());

  // Evaluate error returned by callback.
  ASSERT_THAT(authenticate_future.Get(), NotOk());
  EXPECT_EQ(authenticate_future.Get()->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
}

TEST_F(AuthSessionInterfaceTest, GetAuthSessionStatus) {
  user_data_auth::GetAuthSessionStatusReply reply;
  AuthSession* auth_session = auth_session_manager_->CreateAuthSession(
      kUsername, 0, AuthIntent::kDecrypt,
      /*enable_create_backup_vk_with_uss =*/false);

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

TEST_F(AuthSessionInterfaceTest, GetHibernateSecretUnauthenticatedTest) {
  AuthSession* auth_session = auth_session_manager_->CreateAuthSession(
      kUsername, 0, AuthIntent::kDecrypt,
      /*enable_create_backup_vk_with_uss =*/false);

  // Verify an unauthenticated session fails in producing a hibernate secret.
  user_data_auth::GetHibernateSecretRequest request;
  request.set_auth_session_id(auth_session->serialized_token());
  user_data_auth::GetHibernateSecretReply hs_reply =
      userdataauth_.GetHibernateSecret(request);
  ASSERT_NE(hs_reply.error(), user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  ASSERT_FALSE(hs_reply.hibernate_secret().length());
}

TEST_F(AuthSessionInterfaceTest, GetHibernateSecretTest) {
  AuthSession* auth_session = auth_session_manager_->CreateAuthSession(
      kUsername, 0, AuthIntent::kDecrypt,
      /*enable_create_backup_vk_with_uss =*/false);
  ExpectAuth(kUsername, brillo::SecureBlob(kPassword));
  ExpectVaultKeyset(/*num_of_keysets=*/1);
  TestFuture<CryptohomeStatus> authenticate_future;
  auth_session->Authenticate(CreateAuthorization(kPassword),
                             authenticate_future.GetCallback());
  // Evaluate error returned by callback.
  EXPECT_THAT(authenticate_future.Get(), IsOk());

  // Verify that a successfully authenticated session produces a hibernate
  // secret.
  user_data_auth::GetHibernateSecretRequest request;
  request.set_auth_session_id(auth_session->serialized_token());
  user_data_auth::GetHibernateSecretReply hs_reply =
      userdataauth_.GetHibernateSecret(request);
  ASSERT_EQ(hs_reply.error(), user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  ASSERT_TRUE(hs_reply.hibernate_secret().length());
}

}  // namespace

class AuthSessionInterfaceMockAuthTest : public AuthSessionInterfaceTestBase {
 protected:
  AuthSessionInterfaceMockAuthTest() {
    userdataauth_.set_auth_block_utility(&mock_auth_block_utility_);
    CreateAuthSessionManager(&mock_auth_block_utility_);
  }

  user_data_auth::AddAuthFactorReply AddAuthFactor(
      const user_data_auth::AddAuthFactorRequest& request) {
    TestFuture<user_data_auth::AddAuthFactorReply> reply_future;
    userdataauth_.AddAuthFactor(
        request,
        reply_future.GetCallback<const user_data_auth::AddAuthFactorReply&>());
    return reply_future.Get();
  }

  user_data_auth::AddAuthFactorReply AddPasswordAuthFactor(
      const AuthSession& auth_session,
      const std::string& auth_factor_label,
      const std::string& password) {
    user_data_auth::AddAuthFactorRequest add_request;
    add_request.set_auth_session_id(auth_session.serialized_token());
    user_data_auth::AuthFactor& request_factor =
        *add_request.mutable_auth_factor();
    request_factor.set_type(user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
    request_factor.set_label(auth_factor_label);
    request_factor.mutable_password_metadata();
    add_request.mutable_auth_input()->mutable_password_input()->set_secret(
        password);
    return AddAuthFactor(add_request);
  }

  user_data_auth::AuthenticateAuthFactorReply AuthenticateAuthFactor(
      const user_data_auth::AuthenticateAuthFactorRequest& request) {
    TestFuture<user_data_auth::AuthenticateAuthFactorReply> reply_future;
    userdataauth_.AuthenticateAuthFactor(
        request,
        reply_future
            .GetCallback<const user_data_auth::AuthenticateAuthFactorReply&>());
    return reply_future.Get();
  }

  user_data_auth::AuthenticateAuthFactorReply AuthenticatePasswordAuthFactor(
      const AuthSession& auth_session,
      const std::string& auth_factor_label,
      const std::string& password) {
    user_data_auth::AuthenticateAuthFactorRequest request;
    request.set_auth_session_id(auth_session.serialized_token());
    request.set_auth_factor_label(auth_factor_label);
    request.mutable_auth_input()->mutable_password_input()->set_secret(
        password);
    return AuthenticateAuthFactor(request);
  }

  // Simulates a new user creation flow by running `CreatePersistentUser` and
  // `PreparePersistentVault`. Sets up all necessary mocks. Returns an
  // authenticated AuthSession, or null on failure.
  AuthSession* CreateAndPrepareUserVault() {
    EXPECT_CALL(keyset_management_, UserExists(SanitizeUserName(kUsername)))
        .WillRepeatedly(Return(false));

    AuthSession* auth_session = auth_session_manager_->CreateAuthSession(
        kUsername, /*flags=*/0, AuthIntent::kDecrypt,
        /*enable_create_backup_vk_with_uss =*/false);
    if (!auth_session)
      return nullptr;

    // Create the user.
    EXPECT_CALL(homedirs_, CryptohomeExists(SanitizeUserName(kUsername)))
        .WillOnce(ReturnValue(false));
    EXPECT_CALL(homedirs_, Create(kUsername)).WillOnce(Return(true));
    EXPECT_THAT(CreatePersistentUserImpl(auth_session->serialized_token()),
                IsOk());

    // Prepare the user vault. Use the real user session class to exercise
    // internal state transitions.
    EXPECT_CALL(homedirs_, Exists(SanitizeUserName(kUsername)))
        .WillRepeatedly(Return(true));
    auto mount = base::MakeRefCounted<MockMount>();
    EXPECT_CALL(*mount, IsMounted())
        .WillOnce(Return(false))
        .WillRepeatedly(Return(true));
    auto user_session = std::make_unique<RealUserSession>(
        kUsername, &homedirs_, &keyset_management_,
        &user_activity_timestamp_manager_, &pkcs11_token_factory_, mount);
    EXPECT_CALL(user_session_factory_, New(kUsername, _, _))
        .WillOnce(Return(ByMove(std::move(user_session))));
    EXPECT_THAT(PreparePersistentVaultImpl(auth_session->serialized_token(),
                                           /*vault_options=*/{}),
                IsOk());

    return auth_session;
  }

  AuthSession* PrepareEphemeralUser() {
    AuthSession* auth_session = auth_session_manager_->CreateAuthSession(
        kUsername, AUTH_SESSION_FLAGS_EPHEMERAL_USER, AuthIntent::kDecrypt,
        /*enable_create_backup_vk_with_uss =*/false);
    if (!auth_session)
      return nullptr;

    // Set up mocks for the user session creation. Use the real user session
    // class to exercise internal state transitions.
    auto mount = base::MakeRefCounted<MockMount>();
    EXPECT_CALL(*mount, IsMounted())
        .WillOnce(Return(false))
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*mount, MountEphemeralCryptohome(kUsername))
        .WillOnce(ReturnOk<StorageError>());
    EXPECT_CALL(*mount, IsEphemeral()).WillRepeatedly(Return(true));
    auto user_session = std::make_unique<RealUserSession>(
        kUsername, &homedirs_, &keyset_management_,
        &user_activity_timestamp_manager_, &pkcs11_token_factory_, mount);
    EXPECT_CALL(user_session_factory_, New(kUsername, _, _))
        .WillOnce(Return(ByMove(std::move(user_session))));

    EXPECT_THAT(PrepareEphemeralVaultImpl(auth_session->serialized_token()),
                IsOk());
    return auth_session;
  }

  MockAuthBlockUtility mock_auth_block_utility_;
};

namespace {

// Test that AddAuthFactor succeeds for a freshly created user.
TEST_F(AuthSessionInterfaceMockAuthTest, AddFactorNewUserVk) {
  const std::string obfuscated_username = SanitizeUserName(kUsername);

  // Arrange.
  AuthSession* const auth_session = CreateAndPrepareUserVault();
  ASSERT_TRUE(auth_session);
  const SerializedVaultKeyset serialized_vk =
      CreateFakePasswordVk(kPasswordLabel);
  MockKeysetCreation(mock_auth_block_utility_);
  MockInitialKeysetAdding(obfuscated_username, serialized_vk,
                          keyset_management_);
  MockKeysetLoadingByLabel(obfuscated_username, serialized_vk,
                           keyset_management_);

  // Act.
  user_data_auth::AddAuthFactorRequest request;
  request.set_auth_session_id(auth_session->serialized_token());
  user_data_auth::AuthFactor& request_factor = *request.mutable_auth_factor();
  request_factor.set_type(user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
  request_factor.set_label(kPasswordLabel);
  request_factor.mutable_password_metadata();
  request.mutable_auth_input()->mutable_password_input()->set_secret(kPassword);
  user_data_auth::AddAuthFactorReply reply = AddAuthFactor(request);

  // Assert.
  EXPECT_EQ(reply.error(), user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  UserSession* found_user_session =
      userdataauth_.FindUserSessionForTest(kUsername);
  ASSERT_TRUE(found_user_session);
  EXPECT_TRUE(found_user_session->IsActive());
  // Check the user session has a verifier for the given password.
  Credentials credentials(kUsername, brillo::SecureBlob(kPassword));
  EXPECT_TRUE(found_user_session->VerifyCredentials(credentials));
}

// Test that AddAuthFactor succeeds when adding a second factor for a freshly
// created user, but the credential verifier remains using the first credential.
TEST_F(AuthSessionInterfaceMockAuthTest, AddSecondFactorNewUserVk) {
  const std::string obfuscated_username = SanitizeUserName(kUsername);

  // Arrange.
  AuthSession* const auth_session = CreateAndPrepareUserVault();
  ASSERT_TRUE(auth_session);
  const SerializedVaultKeyset serialized_vk =
      CreateFakePasswordVk(kPasswordLabel);
  MockKeysetCreation(mock_auth_block_utility_);
  MockInitialKeysetAdding(obfuscated_username, serialized_vk,
                          keyset_management_);
  MockKeysetLoadingByLabel(obfuscated_username, serialized_vk,
                           keyset_management_);
  // Add the initial keyset.
  user_data_auth::AddAuthFactorRequest password_request;
  password_request.set_auth_session_id(auth_session->serialized_token());
  user_data_auth::AuthFactor& password_factor =
      *password_request.mutable_auth_factor();
  password_factor.set_type(user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
  password_factor.set_label(kPasswordLabel);
  password_factor.mutable_password_metadata();
  password_request.mutable_auth_input()->mutable_password_input()->set_secret(
      kPassword);
  EXPECT_EQ(AddAuthFactor(password_request).error(),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  // Set up mocks for adding the second keyset.
  MockKeysetCreation(mock_auth_block_utility_);
  MockKeysetLoadingByLabel(obfuscated_username, CreateFakePinVk(kPinLabel),
                           keyset_management_);

  // Act.
  user_data_auth::AddAuthFactorRequest pin_request;
  pin_request.set_auth_session_id(auth_session->serialized_token());
  user_data_auth::AuthFactor& pin_factor = *pin_request.mutable_auth_factor();
  pin_factor.set_type(user_data_auth::AUTH_FACTOR_TYPE_PIN);
  pin_factor.set_label(kPinLabel);
  pin_factor.mutable_pin_metadata();
  pin_request.mutable_auth_input()->mutable_pin_input()->set_secret(kPin);
  user_data_auth::AddAuthFactorReply reply = AddAuthFactor(pin_request);

  // Assert.
  EXPECT_EQ(reply.error(), user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  UserSession* found_user_session =
      userdataauth_.FindUserSessionForTest(kUsername);
  ASSERT_TRUE(found_user_session);
  EXPECT_TRUE(found_user_session->IsActive());
  // Check the user session has a verifier for the first keyset's password.
  Credentials credentials(kUsername, brillo::SecureBlob(kPassword));
  EXPECT_TRUE(found_user_session->VerifyCredentials(credentials));
}

// Test that AuthenticateAuthFactor succeeds for an existing user and a
// VautKeyset-based factor when using the correct credential.
TEST_F(AuthSessionInterfaceMockAuthTest, AuthenticateAuthFactorVkSuccess) {
  const std::string obfuscated_username = SanitizeUserName(kUsername);

  // Arrange.
  EXPECT_CALL(keyset_management_, UserExists(obfuscated_username))
      .WillRepeatedly(ReturnValue(true));
  const SerializedVaultKeyset serialized_vk =
      CreateFakePasswordVk(kPasswordLabel);
  MockLabelToKeyDataMapLoading(obfuscated_username, {serialized_vk},
                               keyset_management_);
  MockKeysetsLoading(obfuscated_username, {serialized_vk}, keyset_management_);
  MockKeysetLoadingByIndex(obfuscated_username, /*index=*/0, serialized_vk,
                           keyset_management_);
  MockKeysetLoadingByLabel(obfuscated_username, serialized_vk,
                           keyset_management_);
  MockKeysetDerivation(obfuscated_username, serialized_vk, CryptoError::CE_NONE,
                       mock_auth_block_utility_);
  MockKeysetLoadingViaBlobs(obfuscated_username, serialized_vk,
                            keyset_management_);
  AuthSession* auth_session = auth_session_manager_->CreateAuthSession(
      kUsername, /*flags=*/0, AuthIntent::kDecrypt,
      /*enable_create_backup_vk_with_uss =*/false);
  ASSERT_TRUE(auth_session);

  // Act.
  user_data_auth::AuthenticateAuthFactorRequest request;
  request.set_auth_session_id(auth_session->serialized_token());
  request.set_auth_factor_label(kPasswordLabel);
  request.mutable_auth_input()->mutable_password_input()->set_secret(kPassword);
  const user_data_auth::AuthenticateAuthFactorReply reply =
      AuthenticateAuthFactor(request);

  // Assert.
  EXPECT_EQ(reply.error(), user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  EXPECT_TRUE(reply.authenticated());
  EXPECT_EQ(auth_session->GetRemainingTime().InSeconds(),
            time_left_after_authenticate);
  EXPECT_THAT(
      reply.authorized_for(),
      UnorderedElementsAre(AUTH_INTENT_DECRYPT, AUTH_INTENT_VERIFY_ONLY));
  EXPECT_EQ(userdataauth_.FindUserSessionForTest(kUsername), nullptr);
}

// Test that AuthenticateAuthFactor fails in case the VaultKeyset decryption
// failed.
TEST_F(AuthSessionInterfaceMockAuthTest,
       AuthenticateAuthFactorVkDecryptionError) {
  const std::string obfuscated_username = SanitizeUserName(kUsername);

  // Arrange. Mock VK decryption to return a failure.
  EXPECT_CALL(keyset_management_, UserExists(obfuscated_username))
      .WillRepeatedly(ReturnValue(true));
  const SerializedVaultKeyset serialized_vk =
      CreateFakePasswordVk(kPasswordLabel);
  MockLabelToKeyDataMapLoading(obfuscated_username, {serialized_vk},
                               keyset_management_);
  MockKeysetsLoading(obfuscated_username, {serialized_vk}, keyset_management_);
  MockKeysetLoadingByIndex(obfuscated_username, /*index=*/0, serialized_vk,
                           keyset_management_);
  MockKeysetLoadingByLabel(obfuscated_username, serialized_vk,
                           keyset_management_);
  MockKeysetDerivation(obfuscated_username, serialized_vk,
                       CryptoError::CE_OTHER_CRYPTO, mock_auth_block_utility_);
  AuthSession* auth_session = auth_session_manager_->CreateAuthSession(
      kUsername, /*flags=*/0, AuthIntent::kDecrypt,
      /*enable_create_backup_vk_with_uss =*/false);
  ASSERT_TRUE(auth_session);

  // Act.
  user_data_auth::AuthenticateAuthFactorRequest request;
  request.set_auth_session_id(auth_session->serialized_token());
  request.set_auth_factor_label(kPasswordLabel);
  request.mutable_auth_input()->mutable_password_input()->set_secret(kPassword);
  const user_data_auth::AuthenticateAuthFactorReply reply =
      AuthenticateAuthFactor(request);

  // Assert.
  EXPECT_EQ(reply.error(),
            user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED);
  EXPECT_FALSE(reply.authenticated());
  EXPECT_FALSE(reply.has_seconds_left());
  EXPECT_THAT(reply.authorized_for(), IsEmpty());
  EXPECT_EQ(userdataauth_.FindUserSessionForTest(kUsername), nullptr);
}

// Test that AuthenticateAuthFactor succeeds using credential verifier based
// lightweight authentication when `AuthIntent::kVerifyOnly` is requested.
TEST_F(AuthSessionInterfaceMockAuthTest, AuthenticateAuthFactorLightweight) {
  const std::string obfuscated_username = SanitizeUserName(kUsername);

  // Arrange. Set up a fake VK without authentication mocks.
  EXPECT_CALL(keyset_management_, UserExists(obfuscated_username))
      .WillRepeatedly(ReturnValue(true));
  const SerializedVaultKeyset serialized_vk =
      CreateFakePasswordVk(kPasswordLabel);
  MockLabelToKeyDataMapLoading(obfuscated_username, {serialized_vk},
                               keyset_management_);
  MockKeysetsLoading(obfuscated_username, {serialized_vk}, keyset_management_);
  MockKeysetLoadingByIndex(obfuscated_username, /*index=*/0, serialized_vk,
                           keyset_management_);
  MockKeysetLoadingByLabel(obfuscated_username, serialized_vk,
                           keyset_management_);
  // Set up a user session with a mocked credential verifier.
  auto user_session = std::make_unique<MockUserSession>();
  EXPECT_CALL(*user_session, VerifyCredentials(_)).WillOnce(Return(true));
  EXPECT_TRUE(user_session_map_.Add(kUsername, std::move(user_session)));
  // Create an AuthSession.
  AuthSession* auth_session = auth_session_manager_->CreateAuthSession(
      kUsername, /*flags=*/0, AuthIntent::kVerifyOnly,
      /*enable_create_backup_vk_with_uss =*/false);
  ASSERT_TRUE(auth_session);

  // Act.
  user_data_auth::AuthenticateAuthFactorRequest request;
  request.set_auth_session_id(auth_session->serialized_token());
  request.set_auth_factor_label(kPasswordLabel);
  request.mutable_auth_input()->mutable_password_input()->set_secret(kPassword);
  const user_data_auth::AuthenticateAuthFactorReply reply =
      AuthenticateAuthFactor(request);

  // Assert. The legacy `authenticated` field stays false.
  EXPECT_EQ(reply.error(), user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  EXPECT_FALSE(reply.authenticated());
  EXPECT_FALSE(reply.has_seconds_left());
  EXPECT_THAT(reply.authorized_for(),
              UnorderedElementsAre(AUTH_INTENT_VERIFY_ONLY));
}

// Test that AuthenticateAuthFactor fails in case the AuthSession ID is missing.
TEST_F(AuthSessionInterfaceMockAuthTest, AuthenticateAuthFactorNoSessionId) {
  const std::string obfuscated_username = SanitizeUserName(kUsername);

  // Arrange.
  EXPECT_CALL(keyset_management_, UserExists(obfuscated_username))
      .WillRepeatedly(ReturnValue(false));

  // Act. Omit setting `auth_session_id` in the `request`.
  user_data_auth::AuthenticateAuthFactorRequest request;
  request.set_auth_factor_label(kPasswordLabel);
  request.mutable_auth_input()->mutable_password_input()->set_secret(kPassword);
  const user_data_auth::AuthenticateAuthFactorReply reply =
      AuthenticateAuthFactor(request);

  // Assert.
  EXPECT_EQ(reply.error(),
            user_data_auth::CRYPTOHOME_INVALID_AUTH_SESSION_TOKEN);
  EXPECT_FALSE(reply.authenticated());
  EXPECT_FALSE(reply.has_seconds_left());
  EXPECT_THAT(reply.authorized_for(), IsEmpty());
  EXPECT_EQ(userdataauth_.FindUserSessionForTest(kUsername), nullptr);
}

// Test that AuthenticateAuthFactor fails in case the AuthSession ID is invalid.
TEST_F(AuthSessionInterfaceMockAuthTest, AuthenticateAuthFactorBadSessionId) {
  const std::string obfuscated_username = SanitizeUserName(kUsername);

  // Arrange.
  EXPECT_CALL(keyset_management_, UserExists(obfuscated_username))
      .WillRepeatedly(ReturnValue(false));

  // Act.
  user_data_auth::AuthenticateAuthFactorRequest request;
  request.set_auth_session_id("bad-session-id");
  request.set_auth_factor_label(kPasswordLabel);
  request.mutable_auth_input()->mutable_password_input()->set_secret(kPassword);
  const user_data_auth::AuthenticateAuthFactorReply reply =
      AuthenticateAuthFactor(request);

  // Assert.
  EXPECT_EQ(reply.error(),
            user_data_auth::CRYPTOHOME_INVALID_AUTH_SESSION_TOKEN);
  EXPECT_FALSE(reply.authenticated());
  EXPECT_FALSE(reply.has_seconds_left());
  EXPECT_THAT(reply.authorized_for(), IsEmpty());
  EXPECT_EQ(userdataauth_.FindUserSessionForTest(kUsername), nullptr);
}

// Test that AuthenticateAuthFactor fails in case the AuthSession is expired.
TEST_F(AuthSessionInterfaceMockAuthTest, AuthenticateAuthFactorExpiredSession) {
  const std::string obfuscated_username = SanitizeUserName(kUsername);

  // Arrange.
  EXPECT_CALL(keyset_management_, UserExists(obfuscated_username))
      .WillRepeatedly(ReturnValue(false));
  AuthSession* auth_session = auth_session_manager_->CreateAuthSession(
      kUsername, /*flags=*/0, AuthIntent::kDecrypt,
      /*enable_create_backup_vk_with_uss =*/false);
  ASSERT_TRUE(auth_session);
  const auto auth_session_id = auth_session->serialized_token();
  EXPECT_TRUE(auth_session_manager_->RemoveAuthSession(auth_session_id));

  // Act.
  user_data_auth::AuthenticateAuthFactorRequest request;
  request.set_auth_session_id(auth_session_id);
  request.set_auth_factor_label(kPasswordLabel);
  request.mutable_auth_input()->mutable_password_input()->set_secret(kPassword);
  const user_data_auth::AuthenticateAuthFactorReply reply =
      AuthenticateAuthFactor(request);

  // Assert.
  EXPECT_EQ(reply.error(),
            user_data_auth::CRYPTOHOME_INVALID_AUTH_SESSION_TOKEN);
  EXPECT_FALSE(reply.authenticated());
  EXPECT_FALSE(reply.has_seconds_left());
  EXPECT_THAT(reply.authorized_for(), IsEmpty());
  EXPECT_EQ(userdataauth_.FindUserSessionForTest(kUsername), nullptr);
}

// Test that AuthenticateAuthFactor fails in case the user doesn't exist.
TEST_F(AuthSessionInterfaceMockAuthTest, AuthenticateAuthFactorNoUser) {
  const std::string obfuscated_username = SanitizeUserName(kUsername);

  // Arrange.
  EXPECT_CALL(keyset_management_, UserExists(obfuscated_username))
      .WillRepeatedly(ReturnValue(false));
  AuthSession* auth_session = auth_session_manager_->CreateAuthSession(
      kUsername, /*flags=*/0, AuthIntent::kDecrypt,
      /*enable_create_backup_vk_with_uss =*/false);
  ASSERT_TRUE(auth_session);

  // Act.
  user_data_auth::AuthenticateAuthFactorRequest request;
  request.set_auth_session_id(auth_session->serialized_token());
  request.set_auth_factor_label(kPasswordLabel);
  request.mutable_auth_input()->mutable_password_input()->set_secret(kPassword);
  const user_data_auth::AuthenticateAuthFactorReply reply =
      AuthenticateAuthFactor(request);

  // Assert.
  EXPECT_EQ(reply.error(), user_data_auth::CRYPTOHOME_ERROR_KEY_NOT_FOUND);
  EXPECT_FALSE(reply.authenticated());
  EXPECT_FALSE(reply.has_seconds_left());
  EXPECT_THAT(reply.authorized_for(), IsEmpty());
  EXPECT_EQ(userdataauth_.FindUserSessionForTest(kUsername), nullptr);
}

// Test that AuthenticateAuthFactor fails in case the user has no keys (because
// the user is just created). The AuthSession, however, stays authenticated.
TEST_F(AuthSessionInterfaceMockAuthTest, AuthenticateAuthFactorNoKeys) {
  const std::string obfuscated_username = SanitizeUserName(kUsername);

  // Arrange.
  EXPECT_CALL(keyset_management_, UserExists(obfuscated_username))
      .WillRepeatedly(ReturnValue(false));
  AuthSession* auth_session = auth_session_manager_->CreateAuthSession(
      kUsername, /*flags=*/0, AuthIntent::kDecrypt,
      /*enable_create_backup_vk_with_uss =*/false);
  ASSERT_TRUE(auth_session);
  EXPECT_THAT(auth_session->OnUserCreated(), IsOk());
  EXPECT_EQ(auth_session->GetStatus(), AuthStatus::kAuthStatusAuthenticated);
  EXPECT_EQ(auth_session->GetRemainingTime().InSeconds(),
            time_left_after_authenticate);
  EXPECT_THAT(
      auth_session->authorized_intents(),
      UnorderedElementsAre(AuthIntent::kDecrypt, AuthIntent::kVerifyOnly));

  // Act.
  user_data_auth::AuthenticateAuthFactorRequest request;
  request.set_auth_session_id(auth_session->serialized_token());
  request.set_auth_factor_label(kPasswordLabel);
  request.mutable_auth_input()->mutable_password_input()->set_secret(kPassword);
  const user_data_auth::AuthenticateAuthFactorReply reply =
      AuthenticateAuthFactor(request);

  // Assert.
  EXPECT_EQ(reply.error(), user_data_auth::CRYPTOHOME_ERROR_KEY_NOT_FOUND);
  EXPECT_TRUE(reply.authenticated());
  EXPECT_THAT(
      reply.authorized_for(),
      UnorderedElementsAre(AUTH_INTENT_DECRYPT, AUTH_INTENT_VERIFY_ONLY));
  EXPECT_EQ(userdataauth_.FindUserSessionForTest(kUsername), nullptr);
}

// Test that AuthenticateAuthFactor fails when a non-existing key label is
// specified.
TEST_F(AuthSessionInterfaceMockAuthTest, AuthenticateAuthFactorWrongVkLabel) {
  constexpr char kConfiguredKeyLabel[] = "fake-configured-label";
  constexpr char kRequestedKeyLabel[] = "fake-requested-label";
  const std::string obfuscated_username = SanitizeUserName(kUsername);

  // Arrange.
  EXPECT_CALL(keyset_management_, UserExists(obfuscated_username))
      .WillRepeatedly(ReturnValue(true));
  const SerializedVaultKeyset serialized_vk =
      CreateFakePasswordVk(kConfiguredKeyLabel);
  MockLabelToKeyDataMapLoading(obfuscated_username, {serialized_vk},
                               keyset_management_);
  MockKeysetsLoading(obfuscated_username, {serialized_vk}, keyset_management_);
  MockKeysetLoadingByIndex(obfuscated_username, /*index=*/0, serialized_vk,
                           keyset_management_);
  MockKeysetLoadingByLabel(obfuscated_username, serialized_vk,
                           keyset_management_);
  AuthSession* auth_session = auth_session_manager_->CreateAuthSession(
      kUsername, /*flags=*/0, AuthIntent::kDecrypt,
      /*enable_create_backup_vk_with_uss =*/false);
  ASSERT_TRUE(auth_session);

  // Act.
  user_data_auth::AuthenticateAuthFactorRequest request;
  request.set_auth_session_id(auth_session->serialized_token());
  request.set_auth_factor_label(kRequestedKeyLabel);
  request.mutable_auth_input()->mutable_password_input()->set_secret(kPassword);
  const user_data_auth::AuthenticateAuthFactorReply reply =
      AuthenticateAuthFactor(request);

  // Assert.
  EXPECT_EQ(reply.error(), user_data_auth::CRYPTOHOME_ERROR_KEY_NOT_FOUND);
  EXPECT_FALSE(reply.authenticated());
  EXPECT_FALSE(reply.has_seconds_left());
  EXPECT_THAT(reply.authorized_for(), IsEmpty());
  EXPECT_EQ(userdataauth_.FindUserSessionForTest(kUsername), nullptr);
}

// Test that AuthenticateAuthFactor fails when no AuthInput is provided.
TEST_F(AuthSessionInterfaceMockAuthTest, AuthenticateAuthFactorNoInput) {
  const std::string obfuscated_username = SanitizeUserName(kUsername);

  // Arrange.
  EXPECT_CALL(keyset_management_, UserExists(obfuscated_username))
      .WillRepeatedly(ReturnValue(true));
  const SerializedVaultKeyset serialized_vk =
      CreateFakePasswordVk(kPasswordLabel);
  MockLabelToKeyDataMapLoading(obfuscated_username, {serialized_vk},
                               keyset_management_);
  MockKeysetsLoading(obfuscated_username, {serialized_vk}, keyset_management_);
  MockKeysetLoadingByIndex(obfuscated_username, /*index=*/0, serialized_vk,
                           keyset_management_);
  MockKeysetLoadingByLabel(obfuscated_username, serialized_vk,
                           keyset_management_);
  AuthSession* auth_session = auth_session_manager_->CreateAuthSession(
      kUsername, /*flags=*/0, AuthIntent::kDecrypt,
      /*enable_create_backup_vk_with_uss =*/false);
  ASSERT_TRUE(auth_session);

  // Act. Omit setting `auth_input` in `request`.
  user_data_auth::AuthenticateAuthFactorRequest request;
  request.set_auth_session_id(auth_session->serialized_token());
  request.set_auth_factor_label(kPasswordLabel);
  const user_data_auth::AuthenticateAuthFactorReply reply =
      AuthenticateAuthFactor(request);

  // Assert.
  EXPECT_EQ(reply.error(), user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
  EXPECT_FALSE(reply.authenticated());
  EXPECT_FALSE(reply.has_seconds_left());
  EXPECT_THAT(reply.authorized_for(), IsEmpty());
  EXPECT_EQ(userdataauth_.FindUserSessionForTest(kUsername), nullptr);
}

// Test the PreparePersistentVault, when called after a successful
// AuthenticateAuthFactor, mounts the home dir and sets up the user session.
TEST_F(AuthSessionInterfaceMockAuthTest, PrepareVaultAfterFactorAuthVk) {
  const std::string obfuscated_username = SanitizeUserName(kUsername);

  // Arrange.
  EXPECT_CALL(keyset_management_, UserExists(obfuscated_username))
      .WillRepeatedly(Return(true));
  // Mock successful authentication via a VaultKeyset.
  const SerializedVaultKeyset serialized_vk =
      CreateFakePasswordVk(kPasswordLabel);
  MockLabelToKeyDataMapLoading(obfuscated_username, {serialized_vk},
                               keyset_management_);
  MockKeysetsLoading(obfuscated_username, {serialized_vk}, keyset_management_);
  MockKeysetLoadingByIndex(obfuscated_username, /*index=*/0, serialized_vk,
                           keyset_management_);
  MockKeysetLoadingByLabel(obfuscated_username, serialized_vk,
                           keyset_management_);
  MockKeysetDerivation(obfuscated_username, serialized_vk, CryptoError::CE_NONE,
                       mock_auth_block_utility_);
  MockKeysetLoadingViaBlobs(obfuscated_username, serialized_vk,
                            keyset_management_);
  // Prepare an AuthSession.
  AuthSession* auth_session = auth_session_manager_->CreateAuthSession(
      kUsername, /*flags=*/0, AuthIntent::kDecrypt,
      /*enable_create_backup_vk_with_uss =*/false);
  ASSERT_TRUE(auth_session);
  // Authenticate the AuthSession.
  user_data_auth::AuthenticateAuthFactorRequest request;
  request.set_auth_session_id(auth_session->serialized_token());
  request.set_auth_factor_label(kPasswordLabel);
  request.mutable_auth_input()->mutable_password_input()->set_secret(kPassword);
  EXPECT_EQ(AuthenticateAuthFactor(request).error(),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  // Mock user vault mounting. Use the real user session class in order to check
  // session state transitions.
  EXPECT_CALL(homedirs_, Exists(SanitizeUserName(kUsername)))
      .WillRepeatedly(Return(true));
  auto mount = base::MakeRefCounted<MockMount>();
  EXPECT_CALL(*mount, IsMounted())
      .WillOnce(Return(false))
      .WillRepeatedly(Return(true));
  auto user_session = std::make_unique<RealUserSession>(
      kUsername, &homedirs_, &keyset_management_,
      &user_activity_timestamp_manager_, &pkcs11_token_factory_, mount);
  EXPECT_CALL(user_session_factory_, New(kUsername, _, _))
      .WillOnce(Return(ByMove(std::move(user_session))));

  // Act.
  CryptohomeStatus prepare_status = PreparePersistentVaultImpl(
      auth_session->serialized_token(), /*vault_options=*/{});

  // Assert.
  EXPECT_THAT(prepare_status, IsOk());
  UserSession* found_user_session =
      userdataauth_.FindUserSessionForTest(kUsername);
  ASSERT_TRUE(found_user_session);
  EXPECT_TRUE(found_user_session->IsActive());
  // Check the user session has a verifier for the given password.
  Credentials credentials(kUsername, brillo::SecureBlob(kPassword));
  EXPECT_TRUE(found_user_session->VerifyCredentials(credentials));
}

// That that AddAuthFactor succeeds for a freshly prepared ephemeral user. The
// credential is stored in the user session as a verifier.
TEST_F(AuthSessionInterfaceMockAuthTest,
       AddPasswordFactorAfterPrepareEphemeral) {
  // Arrange.
  // Pretend to have a different owner user, because otherwise the ephemeral
  // login is disallowed.
  MockOwnerUser("whoever", homedirs_);
  // Prepare the ephemeral vault, which should also create the session.
  AuthSession* const auth_session = PrepareEphemeralUser();
  ASSERT_TRUE(auth_session);
  UserSession* found_user_session =
      userdataauth_.FindUserSessionForTest(kUsername);
  ASSERT_TRUE(found_user_session);
  EXPECT_TRUE(found_user_session->IsActive());
  EXPECT_THAT(found_user_session->GetCredentialVerifier(), IsNull());

  // Act.
  user_data_auth::AddAuthFactorReply reply =
      AddPasswordAuthFactor(*auth_session, kPasswordLabel, kPassword);

  // Assert.
  EXPECT_EQ(reply.error(), user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  // Check the user session has a verifier for the given password.
  EXPECT_THAT(found_user_session->GetCredentialVerifier(), NotNull());
  Credentials credentials(kUsername, brillo::SecureBlob(kPassword));
  EXPECT_TRUE(found_user_session->VerifyCredentials(credentials));
  EXPECT_THAT(
      auth_session->authorized_intents(),
      UnorderedElementsAre(AuthIntent::kDecrypt, AuthIntent::kVerifyOnly));
}

// Test that AuthenticateAuthFactor succeeds for a freshly prepared ephemeral
// user who has a password added.
TEST_F(AuthSessionInterfaceMockAuthTest,
       AuthenticatePasswordFactorForEphemeral) {
  // Arrange.
  // Pretend to have a different owner user, because otherwise the ephemeral
  // login is disallowed.
  MockOwnerUser("whoever", homedirs_);
  AuthSession* const first_auth_session = PrepareEphemeralUser();
  ASSERT_TRUE(first_auth_session);
  EXPECT_EQ(
      AddPasswordAuthFactor(*first_auth_session, kPasswordLabel, kPassword)
          .error(),
      user_data_auth::CRYPTOHOME_ERROR_NOT_SET);

  // Act.
  AuthSession* const second_auth_session =
      auth_session_manager_->CreateAuthSession(
          kUsername, AUTH_SESSION_FLAGS_EPHEMERAL_USER, AuthIntent::kVerifyOnly,
          /*enable_create_backup_vk_with_uss =*/false);
  ASSERT_TRUE(second_auth_session);
  user_data_auth::AuthenticateAuthFactorReply reply =
      AuthenticatePasswordAuthFactor(*second_auth_session, kPasswordLabel,
                                     kPassword);

  // Assert.
  EXPECT_EQ(reply.error(), user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  EXPECT_THAT(second_auth_session->authorized_intents(),
              UnorderedElementsAre(AuthIntent::kVerifyOnly));
}

// Test that AuthenticateAuthFactor fails for a freshly prepared ephemeral user
// if a wrong password is provided.
TEST_F(AuthSessionInterfaceMockAuthTest,
       AuthenticatePasswordFactorForEphemeralWrongPassword) {
  // Arrange.
  // Pretend to have a different owner user, because otherwise the ephemeral
  // login is disallowed.
  MockOwnerUser("whoever", homedirs_);
  // Prepare the ephemeral user with a password configured.
  AuthSession* const first_auth_session = PrepareEphemeralUser();
  ASSERT_TRUE(first_auth_session);
  EXPECT_EQ(
      AddPasswordAuthFactor(*first_auth_session, kPasswordLabel, kPassword)
          .error(),
      user_data_auth::CRYPTOHOME_ERROR_NOT_SET);

  // Act.
  AuthSession* const second_auth_session =
      auth_session_manager_->CreateAuthSession(
          kUsername, AUTH_SESSION_FLAGS_EPHEMERAL_USER, AuthIntent::kVerifyOnly,
          /*enable_create_backup_vk_with_uss =*/false);
  ASSERT_TRUE(second_auth_session);
  user_data_auth::AuthenticateAuthFactorReply reply =
      AuthenticatePasswordAuthFactor(*second_auth_session, kPasswordLabel,
                                     kPassword2);

  // Assert. The error code is such because AuthSession falls back to checking
  // persistent auth factors.
  EXPECT_EQ(reply.error(), user_data_auth::CRYPTOHOME_ERROR_KEY_NOT_FOUND);
  EXPECT_THAT(second_auth_session->authorized_intents(), IsEmpty());
}

// Test that AuthenticateAuthFactor fails for a freshly prepared ephemeral user
// if no password was configured.
TEST_F(AuthSessionInterfaceMockAuthTest,
       AuthenticatePasswordFactorForEphemeralNoPassword) {
  // Arrange.
  // Pretend to have a different owner user, because otherwise the ephemeral
  // login is disallowed.
  MockOwnerUser("whoever", homedirs_);
  // Prepare the ephemeral user without any factor configured.
  EXPECT_TRUE(PrepareEphemeralUser());

  // Act.
  AuthSession* const auth_session = auth_session_manager_->CreateAuthSession(
      kUsername, AUTH_SESSION_FLAGS_EPHEMERAL_USER, AuthIntent::kVerifyOnly,
      /*enable_create_backup_vk_with_uss =*/false);
  ASSERT_TRUE(auth_session);
  user_data_auth::AuthenticateAuthFactorReply reply =
      AuthenticatePasswordAuthFactor(*auth_session, kPasswordLabel, kPassword);

  // Assert. The error code is such because AuthSession falls back to checking
  // persistent auth factors.
  EXPECT_EQ(reply.error(), user_data_auth::CRYPTOHOME_ERROR_KEY_NOT_FOUND);
  EXPECT_THAT(auth_session->authorized_intents(), IsEmpty());
}

}  // namespace

}  // namespace cryptohome
