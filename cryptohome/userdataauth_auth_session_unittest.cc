// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/userdataauth.h"

#include <memory>
#include <utility>

#include <base/containers/span.h>
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
#include "cryptohome/credentials_test_util.h"
#include "cryptohome/crypto.h"
#include "cryptohome/crypto_error.h"
#include "cryptohome/error/cryptohome_crypto_error.h"
#include "cryptohome/error/cryptohome_error.h"
#include "cryptohome/filesystem_layout.h"
#include "cryptohome/mock_cryptohome_keys_manager.h"
#include "cryptohome/mock_install_attributes.h"
#include "cryptohome/mock_keyset_management.h"
#include "cryptohome/mock_le_credential_manager.h"
#include "cryptohome/mock_pkcs11_init.h"
#include "cryptohome/mock_platform.h"
#include "cryptohome/mock_tpm.h"
#include "cryptohome/pkcs11/mock_pkcs11_token_factory.h"
#include "cryptohome/storage/mock_homedirs.h"
#include "cryptohome/user_secret_stash_storage.h"
#include "cryptohome/user_session/mock_user_session.h"
#include "cryptohome/user_session/mock_user_session_factory.h"
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
using ::testing::NiceMock;
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
  serialized_vk.mutable_key_data()->set_type(KeyData::KEY_TYPE_PASSWORD);
  serialized_vk.mutable_key_data()->set_label(label);
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
      kUsername, AUTH_SESSION_FLAGS_EPHEMERAL_USER, AuthIntent::kDecrypt);
  TestFuture<CryptohomeStatus> authenticate_future;
  auth_session->Authenticate(CreateAuthorization(kPassword),
                             authenticate_future.GetCallback());
  EXPECT_THAT(authenticate_future.Get(), IsOk());
  status = PrepareEphemeralVaultImpl(auth_session->serialized_token());
  ASSERT_FALSE(status.ok());
  ASSERT_EQ(status->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY);

  // ... or regular.

  auth_session = auth_session_manager_->CreateAuthSession(kUsername2, 0,
                                                          AuthIntent::kDecrypt);
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
  EXPECT_CALL(homedirs_, GetPlainOwner(_))
      .WillRepeatedly(
          DoAll(SetArgPointee<0>(std::string("whoever")), Return(true)));

  // No auth session.
  CryptohomeStatus status = PrepareEphemeralVaultImpl("");
  ASSERT_FALSE(status.ok());
  ASSERT_EQ(status->local_legacy_error(),
            user_data_auth::CRYPTOHOME_INVALID_AUTH_SESSION_TOKEN);

  // Auth session is authed for ephemeral users.
  AuthSession* auth_session = auth_session_manager_->CreateAuthSession(
      kUsername, AUTH_SESSION_FLAGS_EPHEMERAL_USER, AuthIntent::kDecrypt);
  EXPECT_THAT(auth_session->GetStatus(), AuthStatus::kAuthStatusAuthenticated);

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
      kUsername2, AUTH_SESSION_FLAGS_EPHEMERAL_USER, AuthIntent::kDecrypt);
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
      kUsername3, 0, AuthIntent::kDecrypt);
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
      kUsername, 0, AuthIntent::kDecrypt);
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
      kUsername, 0, AuthIntent::kDecrypt);
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
  EXPECT_CALL(homedirs_, GetPlainOwner(_))
      .WillRepeatedly(
          DoAll(SetArgPointee<0>(std::string("whoever")), Return(true)));

  AuthSession* auth_session = auth_session_manager_->CreateAuthSession(
      kUsername, 0, AuthIntent::kDecrypt);
  // Auth and prepare.
  auto user_session = std::make_unique<MockUserSession>();
  EXPECT_CALL(*user_session, IsActive())
      .WillOnce(Return(false))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*user_session, IsEphemeral()).WillRepeatedly(Return(false));
  EXPECT_CALL(*user_session, HasCredentialVerifier()).WillOnce(Return(false));
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
  EXPECT_CALL(homedirs_, GetPlainOwner(_))
      .WillRepeatedly(
          DoAll(SetArgPointee<0>(std::string("whoever")), Return(true)));

  AuthSession* auth_session = auth_session_manager_->CreateAuthSession(
      kUsername, 0, AuthIntent::kDecrypt);

  // Auth and prepare.
  auto user_session = std::make_unique<MockUserSession>();
  EXPECT_CALL(*user_session, IsActive())
      .WillOnce(Return(false))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*user_session, IsEphemeral()).WillRepeatedly(Return(false));
  EXPECT_CALL(*user_session, HasCredentialVerifier()).WillOnce(Return(false));
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
  EXPECT_CALL(homedirs_, GetPlainOwner(_))
      .WillRepeatedly(
          DoAll(SetArgPointee<0>(std::string("whoever")), Return(true)));
  AuthSession* auth_session = auth_session_manager_->CreateAuthSession(
      kUsername, 0, AuthIntent::kDecrypt);

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
  EXPECT_CALL(homedirs_, GetPlainOwner(_))
      .WillRepeatedly(
          DoAll(SetArgPointee<0>(std::string("whoever")), Return(true)));

  // Setup regular user.
  AuthSession* auth_session = auth_session_manager_->CreateAuthSession(
      kUsername, 0, AuthIntent::kDecrypt);

  // Auth and prepare.
  auto user_session = std::make_unique<MockUserSession>();
  EXPECT_CALL(*user_session, IsActive())
      .WillOnce(Return(false))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*user_session, IsEphemeral()).WillRepeatedly(Return(false));
  EXPECT_CALL(*user_session, HasCredentialVerifier()).WillOnce(Return(false));
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
      kUsername2, AUTH_SESSION_FLAGS_EPHEMERAL_USER, AuthIntent::kDecrypt);
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
      kUsername, 0, AuthIntent::kDecrypt);

  // Auth and prepare.
  auto user_session = std::make_unique<MockUserSession>();
  EXPECT_CALL(*user_session, IsActive())
      .WillOnce(Return(false))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*user_session, IsEphemeral()).WillRepeatedly(Return(false));
  EXPECT_CALL(*user_session, HasCredentialVerifier()).WillOnce(Return(false));
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
      kUsername2, 0, AuthIntent::kDecrypt);
  auto user_session2 = std::make_unique<MockUserSession>();
  EXPECT_CALL(*user_session2, IsActive())
      .WillOnce(Return(false))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*user_session2, IsEphemeral()).WillRepeatedly(Return(false));
  EXPECT_CALL(*user_session2, HasCredentialVerifier()).WillOnce(Return(false));
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
      kUsername, 0, AuthIntent::kDecrypt);
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
      kUsername, 0, AuthIntent::kDecrypt);
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
      kUsername, 0, AuthIntent::kDecrypt);
  EXPECT_FALSE(auth_session->user_exists());
  // User doesn't exist and created.
  EXPECT_CALL(homedirs_, CryptohomeExists(SanitizeUserName(kUsername)))
      .WillOnce(ReturnValue(false));
  EXPECT_CALL(homedirs_, Exists(SanitizeUserName(kUsername)))
      .WillOnce(Return(false));
  EXPECT_CALL(homedirs_, Create(kUsername)).WillOnce(Return(true));
  ASSERT_TRUE(CreatePersistentUserImpl(auth_session->serialized_token()).ok());
  EXPECT_THAT(auth_session->GetStatus(), AuthStatus::kAuthStatusAuthenticated);

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
  EXPECT_CALL(*user_session, HasCredentialVerifier()).WillOnce(Return(false));
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
      kUsername, 0, AuthIntent::kDecrypt);

  EXPECT_CALL(homedirs_, CryptohomeExists(SanitizeUserName(kUsername)))
      .WillOnce(ReturnValue(false));
  EXPECT_CALL(homedirs_, Exists(SanitizeUserName(kUsername)))
      .WillOnce(Return(false));
  EXPECT_CALL(homedirs_, Create(kUsername)).WillOnce(Return(true));
  ASSERT_TRUE(CreatePersistentUserImpl(auth_session->serialized_token()).ok());
  EXPECT_THAT(auth_session->GetStatus(), AuthStatus::kAuthStatusAuthenticated);

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
      kUsername, 0, AuthIntent::kDecrypt);

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
      kUsername, 0, AuthIntent::kDecrypt);

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
      kUsername, 0, AuthIntent::kDecrypt);

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
      kUsername, 0, AuthIntent::kDecrypt);
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

  user_data_auth::AuthenticateAuthFactorReply AuthenticateAuthFactor(
      const user_data_auth::AuthenticateAuthFactorRequest& request) {
    TestFuture<user_data_auth::AuthenticateAuthFactorReply> reply_future;
    userdataauth_.AuthenticateAuthFactor(
        request,
        reply_future
            .GetCallback<const user_data_auth::AuthenticateAuthFactorReply&>());
    return reply_future.Get();
  }

  MockAuthBlockUtility mock_auth_block_utility_;
};

namespace {

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
      kUsername, /*flags=*/0, AuthIntent::kDecrypt);
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
  EXPECT_THAT(
      reply.authorized_for(),
      UnorderedElementsAre(AUTH_INTENT_DECRYPT, AUTH_INTENT_VERIFY_ONLY));
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
      kUsername, /*flags=*/0, AuthIntent::kDecrypt);
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
  EXPECT_THAT(reply.authorized_for(), IsEmpty());
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
      kUsername, /*flags=*/0, AuthIntent::kVerifyOnly);
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
  EXPECT_THAT(reply.authorized_for(), IsEmpty());
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
  EXPECT_THAT(reply.authorized_for(), IsEmpty());
}

// Test that AuthenticateAuthFactor fails in case the AuthSession is expired.
TEST_F(AuthSessionInterfaceMockAuthTest, AuthenticateAuthFactorExpiredSession) {
  const std::string obfuscated_username = SanitizeUserName(kUsername);

  // Arrange.
  EXPECT_CALL(keyset_management_, UserExists(obfuscated_username))
      .WillRepeatedly(ReturnValue(false));
  AuthSession* auth_session = auth_session_manager_->CreateAuthSession(
      kUsername, /*flags=*/0, AuthIntent::kDecrypt);
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
  EXPECT_THAT(reply.authorized_for(), IsEmpty());
}

// Test that AuthenticateAuthFactor fails in case the user doesn't exist.
TEST_F(AuthSessionInterfaceMockAuthTest, AuthenticateAuthFactorNoUser) {
  const std::string obfuscated_username = SanitizeUserName(kUsername);

  // Arrange.
  EXPECT_CALL(keyset_management_, UserExists(obfuscated_username))
      .WillRepeatedly(ReturnValue(false));
  AuthSession* auth_session = auth_session_manager_->CreateAuthSession(
      kUsername, /*flags=*/0, AuthIntent::kDecrypt);
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
  EXPECT_THAT(reply.authorized_for(), IsEmpty());
}

// Test that AuthenticateAuthFactor fails in case the user has no keys (because
// the user is just created). The AuthSession, however, stays authenticated.
TEST_F(AuthSessionInterfaceMockAuthTest, AuthenticateAuthFactorNoKeys) {
  const std::string obfuscated_username = SanitizeUserName(kUsername);

  // Arrange.
  EXPECT_CALL(keyset_management_, UserExists(obfuscated_username))
      .WillRepeatedly(ReturnValue(false));
  AuthSession* auth_session = auth_session_manager_->CreateAuthSession(
      kUsername, /*flags=*/0, AuthIntent::kDecrypt);
  ASSERT_TRUE(auth_session);
  EXPECT_THAT(auth_session->OnUserCreated(), IsOk());
  EXPECT_EQ(auth_session->GetStatus(), AuthStatus::kAuthStatusAuthenticated);
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
      kUsername, /*flags=*/0, AuthIntent::kDecrypt);
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
  EXPECT_THAT(reply.authorized_for(), IsEmpty());
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
      kUsername, /*flags=*/0, AuthIntent::kDecrypt);
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
  EXPECT_THAT(reply.authorized_for(), IsEmpty());
}

}  // namespace

}  // namespace cryptohome
