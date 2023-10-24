// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Unit tests for AuthSession.

#include "cryptohome/auth_session.h"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <base/functional/callback_helpers.h>
#include <base/run_loop.h>
#include <base/task/sequenced_task_runner.h>
#include <base/test/bind.h>
#include <base/test/simple_test_clock.h>
#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <base/timer/mock_timer.h>
#include <base/unguessable_token.h>
#include <brillo/cryptohome.h>
#include <brillo/secure_blob.h>
#include <cryptohome/proto_bindings/auth_factor.pb.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libhwsec/frontend/cryptohome/mock_frontend.h>
#include <libhwsec/frontend/pinweaver/mock_frontend.h>
#include <libhwsec/frontend/pinweaver_manager/frontend.h>
#include <libhwsec/frontend/pinweaver_manager/mock_frontend.h>
#include <libhwsec-foundation/crypto/aes.h>
#include <libhwsec-foundation/error/testing_helper.h>

#include "cryptohome/auth_blocks/auth_block_utility_impl.h"
#include "cryptohome/auth_blocks/biometrics_auth_block_service.h"
#include "cryptohome/auth_blocks/mock_auth_block_utility.h"
#include "cryptohome/auth_blocks/mock_biometrics_command_processor.h"
#include "cryptohome/auth_blocks/tpm_bound_to_pcr_auth_block.h"
#include "cryptohome/auth_factor/auth_factor.h"
#include "cryptohome/auth_factor/auth_factor_manager.h"
#include "cryptohome/auth_factor/auth_factor_metadata.h"
#include "cryptohome/auth_factor/auth_factor_storage_type.h"
#include "cryptohome/auth_factor/auth_factor_type.h"
#include "cryptohome/auth_factor/flatbuffer.h"
#include "cryptohome/auth_input_utils.h"
#include "cryptohome/challenge_credentials/challenge_credentials_helper.h"
#include "cryptohome/challenge_credentials/mock_challenge_credentials_helper.h"
#include "cryptohome/credential_verifier_test_utils.h"
#include "cryptohome/crypto_error.h"
#include "cryptohome/error/cryptohome_error.h"
#include "cryptohome/fake_features.h"
#include "cryptohome/filesystem_layout.h"
#include "cryptohome/flatbuffer_schemas/auth_block_state.h"
#include "cryptohome/flatbuffer_schemas/auth_factor.h"
#include "cryptohome/key_objects.h"
#include "cryptohome/mock_credential_verifier.h"
#include "cryptohome/mock_cryptohome_keys_manager.h"
#include "cryptohome/mock_key_challenge_service_factory.h"
#include "cryptohome/mock_keyset_management.h"
#include "cryptohome/mock_platform.h"
#include "cryptohome/pinweaver_manager/mock_le_credential_manager.h"
#include "cryptohome/pkcs11/mock_pkcs11_token_factory.h"
#include "cryptohome/storage/homedirs.h"
#include "cryptohome/storage/mock_mount.h"
#include "cryptohome/user_secret_stash/storage.h"
#include "cryptohome/user_session/mock_user_session.h"
#include "cryptohome/user_session/real_user_session.h"
#include "cryptohome/user_session/user_session_map.h"
#include "cryptohome/username.h"

namespace cryptohome {
namespace {

using base::test::TestFuture;
using brillo::cryptohome::home::SanitizeUserName;
using cryptohome::error::CryptohomeCryptoError;
using cryptohome::error::CryptohomeError;
using cryptohome::error::CryptohomeMountError;
using hwsec_foundation::error::testing::IsOk;
using hwsec_foundation::error::testing::NotOk;
using hwsec_foundation::error::testing::ReturnError;
using hwsec_foundation::error::testing::ReturnOk;
using hwsec_foundation::error::testing::ReturnValue;
using hwsec_foundation::status::MakeStatus;
using hwsec_foundation::status::OkStatus;
using hwsec_foundation::status::StatusChain;
using ::testing::_;
using ::testing::AtLeast;
using ::testing::ByMove;
using ::testing::DoAll;
using ::testing::DoDefault;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Field;
using ::testing::IsEmpty;
using ::testing::IsNull;
using ::testing::Matcher;
using ::testing::NiceMock;
using ::testing::NotNull;
using ::testing::Optional;
using ::testing::Pair;
using ::testing::Return;
using ::testing::UnorderedElementsAre;
using ::testing::VariantWith;

using AuthenticateTestFuture =
    TestFuture<const AuthSession::PostAuthAction&, CryptohomeStatus>;

// Fake labels to be in used in this test suite.
constexpr char kFakeLabel[] = "test_label";
constexpr char kFakeOtherLabel[] = "test_other_label";
constexpr char kFakePinLabel[] = "test_pin_label";
constexpr char kRecoveryLabel[] = "recovery";
constexpr char kFakeFingerprintLabel[] = "test_fp_label";
constexpr char kFakeSecondFingerprintLabel[] = "test_second_fp_label";

// Fake passwords to be in used in this test suite.
constexpr char kFakePass[] = "test_pass";
constexpr char kFakePin[] = "123456";
constexpr char kFakeOtherPass[] = "test_other_pass";
constexpr char kFakeRecoverySecret[] = "test_recovery_secret";

// Fingerprint-related constants to be used in this test suite.
const uint64_t kFakeRateLimiterLabel = 100;
const uint64_t kFakeFpLabel = 200;
const uint64_t kFakeSecondFpLabel = 300;
constexpr char kFakeVkkKey[] = "fake_vkk_key";
constexpr char kFakeSecondVkkKey[] = "fake_second_vkk_key";
constexpr char kFakeRecordId[] = "fake_record_id";
constexpr char kFakeSecondRecordId[] = "fake_second_record_id";

// Upper limit of the Size of user specified name.
constexpr int kUserSpecifiedNameSizeLimit = 256;

// Returns a blob "derived" from provided blob to generate fake vkk_key from
// user secret in tests.
brillo::SecureBlob GetFakeDerivedSecret(const brillo::SecureBlob& blob) {
  return brillo::SecureBlob::Combine(blob,
                                     brillo::SecureBlob(" derived secret"));
}

// A matcher that checks if an auth block state has a particular type.
template <typename StateType>
Matcher<const AuthBlockState&> AuthBlockStateTypeIs() {
  return Field(&AuthBlockState::state, VariantWith<StateType>(_));
}

AuthSession::AuthenticateAuthFactorRequest ToAuthenticateRequest(
    std::vector<std::string> labels, user_data_auth::AuthInput auth_input) {
  return AuthSession::AuthenticateAuthFactorRequest{
      .auth_factor_labels = std::move(labels),
      .auth_input_proto = std::move(auth_input),
      .flags = {.force_full_auth = AuthSession::ForceFullAuthFlag::kNone},
  };
}

// A helpful utility for setting up AuthFactorMaps for testing. This provides a
// very concise way to construct them with a variety of configurable options.
// The way you use this is something like:
//
// auto auth_factor_map = AfMapBuilder().WithUss().AddPin("label").Consume();
//
// The end result of this will a map that contains a USS-backed PIN.
class AfMapBuilder {
 public:
  AfMapBuilder() = default;

  AfMapBuilder(const AfMapBuilder&) = delete;
  AfMapBuilder& operator=(const AfMapBuilder&) = delete;

  // Set the storage type of any subsequent factors.
  AfMapBuilder& WithVk() {
    storage_type_ = AuthFactorStorageType::kVaultKeyset;
    return *this;
  }
  AfMapBuilder& WithUss() {
    storage_type_ = AuthFactorStorageType::kUserSecretStash;
    return *this;
  }

  // Helpers to add different kinds of auth factors.
  template <typename StateType>
  AfMapBuilder& AddPassword(std::string label) {
    return AddFactor<StateType>(label, AuthFactorType::kPassword);
  }
  AfMapBuilder& AddPin(std::string label) {
    return AddFactor<PinWeaverAuthBlockState>(label, AuthFactorType::kPin);
  }
  AfMapBuilder& AddRecovery(std::string label) {
    return AddFactor<CryptohomeRecoveryAuthBlockState>(
        label, AuthFactorType::kCryptohomeRecovery);
  }

  // Helper to add copies of factors from an existing AuthFactorMap.
  AfMapBuilder& AddCopiesFromMap(const AuthFactorMap& af_map) {
    for (AuthFactorMap::ValueView entry : af_map) {
      map_.Add(entry.auth_factor(), storage_type_);
    }
    return *this;
  }

  // Consume the map.
  AuthFactorMap Consume() { return std::move(map_); }

 private:
  // Generic add factor implementation. The template parameter specifies the
  // type of auth block state to use, or void for none.
  template <typename StateType>
  AfMapBuilder& AddFactor(std::string label, AuthFactorType auth_factor_type) {
    AuthBlockState auth_block_state;
    if constexpr (!std::is_void_v<StateType>) {
      auth_block_state.state = StateType();
    }
    map_.Add(AuthFactor(auth_factor_type, std::move(label),
                        AuthFactorMetadata(), auth_block_state),
             storage_type_);
    return *this;
  }

  AuthFactorStorageType storage_type_ = AuthFactorStorageType::kUserSecretStash;

  AuthFactorMap map_;
};

}  // namespace

class AuthSessionTest : public ::testing::Test {
 public:
  AuthSessionTest() {
    auto mock_processor =
        std::make_unique<NiceMock<MockBiometricsCommandProcessor>>();
    bio_processor_ = mock_processor.get();
    bio_service_ = std::make_unique<BiometricsAuthBlockService>(
        std::move(mock_processor),
        /*enroll_signal_sender=*/base::DoNothing(),
        /*auth_signal_sender=*/base::DoNothing());
  }

  void SetUp() override {
    EXPECT_CALL(hwsec_, IsEnabled()).WillRepeatedly(ReturnValue(true));
    EXPECT_CALL(hwsec_, IsReady()).WillRepeatedly(ReturnValue(true));
    EXPECT_CALL(hwsec_, IsPinWeaverEnabled()).WillRepeatedly(ReturnValue(true));
    EXPECT_CALL(hwsec_, IsSealingSupported()).WillRepeatedly(ReturnValue(true));
    EXPECT_CALL(hwsec_, GetManufacturer())
        .WillRepeatedly(ReturnValue(0x43524f53));
    EXPECT_CALL(hwsec_, GetAuthValue(_, _))
        .WillRepeatedly(ReturnValue(brillo::SecureBlob()));
    EXPECT_CALL(hwsec_, SealWithCurrentUser(_, _, _))
        .WillRepeatedly(ReturnValue(brillo::Blob()));
    EXPECT_CALL(hwsec_, GetPubkeyHash(_))
        .WillRepeatedly(ReturnValue(brillo::Blob()));
    EXPECT_CALL(pinweaver_, IsEnabled()).WillRepeatedly(ReturnValue(true));
    EXPECT_CALL(hwsec_pw_manager_, IsEnabled())
        .WillRepeatedly(ReturnValue(true));
    crypto_.Init();
  }

 protected:
  // Fake username to be used in this test suite.
  const Username kFakeUsername{"test_username"};
  // Get a UserSession for the given user, creating a minimal stub one if
  // necessary.
  UserSession* FindOrCreateUserSession(const Username& username) {
    if (UserSession* session = user_session_map_.Find(username)) {
      return session;
    }
    user_session_map_.Add(
        username, std::make_unique<RealUserSession>(
                      username, &homedirs_, &keyset_management_,
                      &user_activity_timestamp_manager_, &pkcs11_token_factory_,
                      new NiceMock<MockMount>()));
    return user_session_map_.Find(username);
  }

  base::test::SingleThreadTaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  base::SimpleTestClock clock_;
  scoped_refptr<base::SequencedTaskRunner> task_runner_ =
      base::SequencedTaskRunner::GetCurrentDefault();

  // Mocks and fakes for the test AuthSessions to use.
  NiceMock<MockPlatform> platform_;
  NiceMock<hwsec::MockCryptohomeFrontend> hwsec_;
  NiceMock<hwsec::MockPinWeaverFrontend> pinweaver_;
  NiceMock<hwsec::MockPinWeaverManagerFrontend> hwsec_pw_manager_;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager_;
  Crypto crypto_{&hwsec_, &pinweaver_, &hwsec_pw_manager_,
                 &cryptohome_keys_manager_, nullptr};
  UssStorage uss_storage_{&platform_};
  UserUssStorage user_uss_storage_{uss_storage_,
                                   SanitizeUserName(kFakeUsername)};
  UserSessionMap user_session_map_;
  NiceMock<MockKeysetManagement> keyset_management_;
  NiceMock<MockAuthBlockUtility> auth_block_utility_;
  std::unique_ptr<FingerprintAuthBlockService> fp_service_{
      FingerprintAuthBlockService::MakeNullService()};
  NiceMock<MockChallengeCredentialsHelper> challenge_credentials_helper_;
  NiceMock<MockKeyChallengeServiceFactory> key_challenge_service_factory_;
  NiceMock<MockBiometricsCommandProcessor>* bio_processor_;
  std::unique_ptr<BiometricsAuthBlockService> bio_service_;
  AuthFactorDriverManager auth_factor_driver_manager_{
      &platform_,
      &crypto_,
      &uss_storage_,
      AsyncInitPtr<ChallengeCredentialsHelper>(&challenge_credentials_helper_),
      &key_challenge_service_factory_,
      fp_service_.get(),
      AsyncInitPtr<BiometricsAuthBlockService>(base::BindRepeating(
          [](AuthSessionTest* test) { return test->bio_service_.get(); },
          base::Unretained(this)))};
  AuthFactorManager auth_factor_manager_{&platform_};
  FakeFeaturesForTesting fake_features_;
  AuthSession::BackingApis backing_apis_{&crypto_,
                                         &platform_,
                                         &user_session_map_,
                                         &keyset_management_,
                                         &auth_block_utility_,
                                         &auth_factor_driver_manager_,
                                         &auth_factor_manager_,
                                         &uss_storage_,
                                         &fake_features_.async};

  // Mocks and fakes for UserSession to use.
  HomeDirs homedirs_{&platform_,
                     std::make_unique<policy::PolicyProvider>(nullptr),
                     HomeDirs::RemoveCallback(),
                     /*vault_factory=*/nullptr};
  UserOldestActivityTimestampManager user_activity_timestamp_manager_{
      &platform_};
  NiceMock<MockPkcs11TokenFactory> pkcs11_token_factory_;
};

const CryptohomeError::ErrorLocationPair kErrorLocationForTestingAuthSession =
    CryptohomeError::ErrorLocationPair(
        static_cast<::cryptohome::error::CryptohomeError::ErrorLocation>(1),
        std::string("MockErrorLocationAuthSession"));

TEST_F(AuthSessionTest, TokensAreValid) {
  AuthSession auth_session({.username = kFakeUsername,
                            .is_ephemeral_user = false,
                            .intent = AuthIntent::kDecrypt,
                            .auth_factor_status_update_timer =
                                std::make_unique<base::WallClockTimer>(),
                            .user_exists = false,
                            .auth_factor_map = AuthFactorMap()},
                           backing_apis_);

  EXPECT_FALSE(auth_session.token().is_empty());
  EXPECT_FALSE(auth_session.public_token().is_empty());
  EXPECT_NE(auth_session.token(), auth_session.public_token());

  EXPECT_FALSE(auth_session.serialized_token().empty());
  EXPECT_FALSE(auth_session.serialized_public_token().empty());
  EXPECT_NE(auth_session.serialized_token(),
            auth_session.serialized_public_token());
}

TEST_F(AuthSessionTest, InitiallyNotAuthenticated) {
  AuthSession auth_session({.username = kFakeUsername,
                            .is_ephemeral_user = false,
                            .intent = AuthIntent::kDecrypt,
                            .auth_factor_status_update_timer =
                                std::make_unique<base::WallClockTimer>(),
                            .user_exists = false,
                            .auth_factor_map = AuthFactorMap()},
                           backing_apis_);

  EXPECT_THAT(auth_session.authorized_intents(), IsEmpty());
  EXPECT_THAT(auth_session.GetAuthForDecrypt(), IsNull());
  EXPECT_THAT(auth_session.GetAuthForVerifyOnly(), IsNull());
  EXPECT_THAT(auth_session.GetAuthForDecrypt(), IsNull());
}

TEST_F(AuthSessionTest, InitiallyNotAuthenticatedForExistingUser) {
  AuthSession auth_session({.username = kFakeUsername,
                            .is_ephemeral_user = false,
                            .intent = AuthIntent::kDecrypt,
                            .auth_factor_status_update_timer =
                                std::make_unique<base::WallClockTimer>(),
                            .user_exists = true,
                            .auth_factor_map = AuthFactorMap()},
                           backing_apis_);

  EXPECT_THAT(auth_session.authorized_intents(), IsEmpty());
  EXPECT_THAT(auth_session.GetAuthForDecrypt(), IsNull());
  EXPECT_THAT(auth_session.GetAuthForVerifyOnly(), IsNull());
  EXPECT_THAT(auth_session.GetAuthForWebAuthn(), IsNull());
}

TEST_F(AuthSessionTest, Username) {
  AuthSession auth_session({.username = kFakeUsername,
                            .is_ephemeral_user = false,
                            .intent = AuthIntent::kDecrypt,
                            .auth_factor_status_update_timer =
                                std::make_unique<base::WallClockTimer>(),
                            .user_exists = false,
                            .auth_factor_map = AuthFactorMap()},
                           backing_apis_);

  EXPECT_EQ(auth_session.username(), kFakeUsername);
  EXPECT_EQ(auth_session.obfuscated_username(),
            SanitizeUserName(kFakeUsername));
}

TEST_F(AuthSessionTest, DecryptionIntent) {
  AuthSession auth_session({.username = kFakeUsername,
                            .is_ephemeral_user = false,
                            .intent = AuthIntent::kDecrypt,
                            .auth_factor_status_update_timer =
                                std::make_unique<base::WallClockTimer>(),
                            .user_exists = false,
                            .auth_factor_map = AuthFactorMap()},
                           backing_apis_);

  EXPECT_EQ(auth_session.auth_intent(), AuthIntent::kDecrypt);
}

TEST_F(AuthSessionTest, VerfyIntent) {
  AuthSession auth_session({.username = kFakeUsername,
                            .is_ephemeral_user = false,
                            .intent = AuthIntent::kVerifyOnly,
                            .auth_factor_status_update_timer =
                                std::make_unique<base::WallClockTimer>(),
                            .user_exists = false,
                            .auth_factor_map = AuthFactorMap()},
                           backing_apis_);

  EXPECT_EQ(auth_session.auth_intent(), AuthIntent::kVerifyOnly);
}

TEST_F(AuthSessionTest, WebAuthnIntent) {
  AuthSession auth_session({.username = kFakeUsername,
                            .is_ephemeral_user = false,
                            .intent = AuthIntent::kWebAuthn,
                            .auth_factor_status_update_timer =
                                std::make_unique<base::WallClockTimer>(),
                            .user_exists = false,
                            .auth_factor_map = AuthFactorMap()},
                           backing_apis_);

  EXPECT_EQ(auth_session.auth_intent(), AuthIntent::kWebAuthn);
}

TEST_F(AuthSessionTest, SerializedStringFromNullToken) {
  base::UnguessableToken token = base::UnguessableToken::Null();
  std::optional<std::string> serialized_token =
      AuthSession::GetSerializedStringFromToken(token);
  EXPECT_FALSE(serialized_token.has_value());
}

TEST_F(AuthSessionTest, TokenFromEmptyString) {
  std::string serialized_string = "";
  std::optional<base::UnguessableToken> unguessable_token =
      AuthSession::GetTokenFromSerializedString(serialized_string);
  EXPECT_FALSE(unguessable_token.has_value());
}

TEST_F(AuthSessionTest, TokenFromUnexpectedSize) {
  std::string serialized_string = "unexpected_sized_string";
  std::optional<base::UnguessableToken> unguessable_token =
      AuthSession::GetTokenFromSerializedString(serialized_string);
  EXPECT_FALSE(unguessable_token.has_value());
}

TEST_F(AuthSessionTest, TokenFromString) {
  base::UnguessableToken original_token = platform_.CreateUnguessableToken();
  std::optional<std::string> serialized_token =
      AuthSession::GetSerializedStringFromToken(original_token);
  EXPECT_TRUE(serialized_token.has_value());
  std::optional<base::UnguessableToken> deserialized_token =
      AuthSession::GetTokenFromSerializedString(serialized_token.value());
  EXPECT_TRUE(deserialized_token.has_value());
  EXPECT_EQ(deserialized_token.value(), original_token);
}

// Test that `GetSerializedStringFromToken()` refuses a string containing only
// zero bytes (but doesn't crash). Note: such a string would've corresponded to
// `base::UnguessableToken::Null()` if the latter would be allowed.
TEST_F(AuthSessionTest, TokenFromAllZeroesString) {
  // Setup. To avoid hardcoding the length of the string in the test, first
  // serialize an arbitrary token and then replace its contents with zeroes.
  const base::UnguessableToken some_token = base::UnguessableToken::Create();
  const std::optional<std::string> serialized_some_token =
      AuthSession::GetSerializedStringFromToken(some_token);
  ASSERT_TRUE(serialized_some_token.has_value());
  const std::string all_zeroes_token(serialized_some_token->length(), '\0');

  // Test.
  std::optional<base::UnguessableToken> deserialized_token =
      AuthSession::GetTokenFromSerializedString(all_zeroes_token);

  // Verify.
  EXPECT_EQ(deserialized_token, std::nullopt);
}

// Test if AuthSession reports the correct attributes on an already-existing
// ephemeral user.
TEST_F(AuthSessionTest, ExistingEphemeralUser) {
  // Setup.
  int flags =
      user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_EPHEMERAL_USER;

  // Setting the expectation that there is no persistent user but there is an
  // active ephemeral one.
  EXPECT_CALL(platform_, DirectoryExists(_)).WillRepeatedly(Return(false));
  auto user_session = std::make_unique<MockUserSession>();
  EXPECT_CALL(*user_session, IsActive()).WillRepeatedly(Return(true));
  user_session_map_.Add(kFakeUsername, std::move(user_session));

  // Test.
  std::unique_ptr<AuthSession> auth_session = AuthSession::Create(
      kFakeUsername, flags, AuthIntent::kDecrypt, backing_apis_);

  // Verify.
  EXPECT_TRUE(auth_session->user_exists());
}

// Test that AuthenticateAuthFactor returns an error when supplied label and
// type mismatch.
TEST_F(AuthSessionTest, AuthenticateAuthFactorMismatchLabelAndType) {
  // Setup AuthSession.
  AuthSession auth_session(
      {.username = kFakeUsername,
       .is_ephemeral_user = false,
       .intent = AuthIntent::kDecrypt,
       .auth_factor_status_update_timer =
           std::make_unique<base::WallClockTimer>(),
       .user_exists = true,
       .auth_factor_map = AfMapBuilder().AddPin(kFakePinLabel).Consume()},
      backing_apis_);
  EXPECT_THAT(auth_session.authorized_intents(), IsEmpty());
  EXPECT_THAT(auth_session.GetAuthForDecrypt(), IsNull());
  EXPECT_THAT(auth_session.GetAuthForVerifyOnly(), IsNull());
  EXPECT_THAT(auth_session.GetAuthForWebAuthn(), IsNull());

  EXPECT_TRUE(auth_session.user_exists());

  // Test
  // Calling AuthenticateAuthFactor.
  AuthenticateTestFuture authenticate_future;
  std::vector<std::string> auth_factor_labels{kFakePinLabel};
  user_data_auth::AuthInput auth_input_proto;
  auth_input_proto.mutable_password_input()->set_secret(kFakePin);
  SerializedUserAuthFactorTypePolicy auth_factor_type_policy(
      {.type = *SerializeAuthFactorType(
           *DetermineFactorTypeFromAuthInput(auth_input_proto)),
       .enabled_intents = {},
       .disabled_intents = {}});
  auth_session.AuthenticateAuthFactor(
      ToAuthenticateRequest(auth_factor_labels, auth_input_proto),
      auth_factor_type_policy, authenticate_future.GetCallback());

  // Verify.
  auto& [action, status] = authenticate_future.Get();
  EXPECT_EQ(action.action_type, AuthSession::PostAuthActionType::kNone);
  EXPECT_THAT(status, NotOk());
  EXPECT_EQ(status->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
  EXPECT_THAT(auth_session.authorized_intents(), IsEmpty());
  EXPECT_THAT(auth_session.GetAuthForDecrypt(), IsNull());
  EXPECT_THAT(auth_session.GetAuthForVerifyOnly(), IsNull());
  EXPECT_THAT(auth_session.GetAuthForWebAuthn(), IsNull());
}

// Test that AddAuthFactor succeeds for an ephemeral user and creates a
// credential verifier.
TEST_F(AuthSessionTest, AddPasswordFactorToEphemeral) {
  // Setup.
  AuthSession auth_session({.username = kFakeUsername,
                            .is_ephemeral_user = true,
                            .intent = AuthIntent::kDecrypt,
                            .auth_factor_status_update_timer =
                                std::make_unique<base::WallClockTimer>(),
                            .user_exists = false,
                            .auth_factor_map = AuthFactorMap()},
                           backing_apis_);
  EXPECT_THAT(auth_session.OnUserCreated(), IsOk());
  EXPECT_THAT(
      auth_session.authorized_intents(),
      UnorderedElementsAre(AuthIntent::kDecrypt, AuthIntent::kVerifyOnly));
  EXPECT_THAT(auth_session.GetAuthForDecrypt(), NotNull());
  EXPECT_THAT(auth_session.GetAuthForVerifyOnly(), NotNull());
  EXPECT_THAT(auth_session.GetAuthForWebAuthn(), IsNull());

  // Test.
  user_data_auth::AddAuthFactorRequest request;
  request.set_auth_session_id(auth_session.serialized_token());
  user_data_auth::AuthFactor& request_factor = *request.mutable_auth_factor();
  request_factor.set_type(user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
  request_factor.set_label(kFakeLabel);
  request_factor.mutable_password_metadata();
  request.mutable_auth_input()->mutable_password_input()->set_secret(kFakePass);

  TestFuture<CryptohomeStatus> add_future;
  auth_session.GetAuthForDecrypt()->AddAuthFactor(request,
                                                  add_future.GetCallback());

  // Verify.
  EXPECT_THAT(add_future.Get(), IsOk());

  UserSession* user_session = FindOrCreateUserSession(kFakeUsername);
  EXPECT_THAT(user_session->GetCredentialVerifiers(),
              UnorderedElementsAre(
                  IsVerifierPtrWithLabelAndPassword(kFakeLabel, kFakePass)));
}

// Test that AddAuthFactor fails for an ephemeral user when PIN is added.
TEST_F(AuthSessionTest, AddPinFactorToEphemeralFails) {
  // Setup.
  AuthSession auth_session({.username = kFakeUsername,
                            .is_ephemeral_user = true,
                            .intent = AuthIntent::kDecrypt,
                            .auth_factor_status_update_timer =
                                std::make_unique<base::WallClockTimer>(),
                            .user_exists = false,
                            .auth_factor_map = AuthFactorMap()},
                           backing_apis_);
  EXPECT_THAT(auth_session.OnUserCreated(), IsOk());
  EXPECT_THAT(
      auth_session.authorized_intents(),
      UnorderedElementsAre(AuthIntent::kDecrypt, AuthIntent::kVerifyOnly));
  EXPECT_THAT(auth_session.GetAuthForDecrypt(), NotNull());
  EXPECT_THAT(auth_session.GetAuthForVerifyOnly(), NotNull());
  EXPECT_THAT(auth_session.GetAuthForWebAuthn(), IsNull());

  // Test.
  user_data_auth::AddAuthFactorRequest request;
  request.set_auth_session_id(auth_session.serialized_token());
  user_data_auth::AuthFactor& request_factor = *request.mutable_auth_factor();
  request_factor.set_type(user_data_auth::AUTH_FACTOR_TYPE_PIN);
  request_factor.set_label(kFakePinLabel);
  request_factor.mutable_pin_metadata();
  request.mutable_auth_input()->mutable_pin_input()->set_secret(kFakePin);

  TestFuture<CryptohomeStatus> add_future;
  auth_session.GetAuthForDecrypt()->AddAuthFactor(request,
                                                  add_future.GetCallback());

  // Verify.
  ASSERT_THAT(add_future.Get(), NotOk());
  EXPECT_EQ(add_future.Get()->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);

  UserSession* user_session = FindOrCreateUserSession(kFakeUsername);
  EXPECT_THAT(user_session->GetCredentialVerifiers(), IsEmpty());
}

TEST_F(AuthSessionTest, AddSecondPasswordFactorToEphemeral) {
  // Setup.
  AuthSession auth_session({.username = kFakeUsername,
                            .is_ephemeral_user = true,
                            .intent = AuthIntent::kDecrypt,
                            .auth_factor_status_update_timer =
                                std::make_unique<base::WallClockTimer>(),
                            .user_exists = false,
                            .auth_factor_map = AuthFactorMap()},
                           backing_apis_);
  EXPECT_THAT(auth_session.OnUserCreated(), IsOk());
  EXPECT_THAT(
      auth_session.authorized_intents(),
      UnorderedElementsAre(AuthIntent::kDecrypt, AuthIntent::kVerifyOnly));
  EXPECT_THAT(auth_session.GetAuthForDecrypt(), NotNull());
  EXPECT_THAT(auth_session.GetAuthForVerifyOnly(), NotNull());
  EXPECT_THAT(auth_session.GetAuthForWebAuthn(), IsNull());
  // Add the first password.
  user_data_auth::AddAuthFactorRequest request;
  request.set_auth_session_id(auth_session.serialized_token());
  user_data_auth::AuthFactor& request_factor = *request.mutable_auth_factor();
  request_factor.set_type(user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
  request_factor.set_label(kFakeLabel);
  request_factor.mutable_password_metadata();
  request.mutable_auth_input()->mutable_password_input()->set_secret(kFakePass);
  TestFuture<CryptohomeStatus> first_add_future;
  auth_session.GetAuthForDecrypt()->AddAuthFactor(
      request, first_add_future.GetCallback());
  EXPECT_THAT(first_add_future.Get(), IsOk());

  // Test.
  request_factor.set_label(kFakeOtherLabel);
  request.mutable_auth_input()->mutable_password_input()->set_secret(
      kFakeOtherPass);
  TestFuture<CryptohomeStatus> second_add_future;
  auth_session.GetAuthForDecrypt()->AddAuthFactor(
      request, second_add_future.GetCallback());

  // Verify.
  ASSERT_THAT(second_add_future.Get(), IsOk());
  // There should be two verifiers.
  UserSession* user_session = FindOrCreateUserSession(kFakeUsername);
  EXPECT_THAT(
      user_session->GetCredentialVerifiers(),
      UnorderedElementsAre(
          IsVerifierPtrWithLabelAndPassword(kFakeLabel, kFakePass),
          IsVerifierPtrWithLabelAndPassword(kFakeOtherLabel, kFakeOtherPass)));
}

// UpdateAuthFactor fails if label doesn't exist.
TEST_F(AuthSessionTest, UpdateAuthFactorFailsLabelNotMatchInAFMap) {
  // Setup.
  AuthSession auth_session(
      {.username = kFakeUsername,
       .is_ephemeral_user = false,
       .intent = AuthIntent::kDecrypt,
       .auth_factor_status_update_timer =
           std::make_unique<base::WallClockTimer>(),
       .user_exists = true,
       .auth_factor_map =
           AfMapBuilder()
               .AddPassword<TpmBoundToPcrAuthBlockState>(kFakeLabel)
               .Consume()},
      backing_apis_);
  EXPECT_THAT(auth_session.authorized_intents(), IsEmpty());
  EXPECT_THAT(auth_session.GetAuthForDecrypt(), IsNull());
  EXPECT_THAT(auth_session.GetAuthForVerifyOnly(), IsNull());
  EXPECT_THAT(auth_session.GetAuthForWebAuthn(), IsNull());

  EXPECT_TRUE(auth_session.user_exists());

  // Creating the user.
  EXPECT_TRUE(auth_session.OnUserCreated().ok());
  EXPECT_THAT(
      auth_session.authorized_intents(),
      UnorderedElementsAre(AuthIntent::kDecrypt, AuthIntent::kVerifyOnly));
  EXPECT_THAT(auth_session.GetAuthForDecrypt(), NotNull());
  EXPECT_THAT(auth_session.GetAuthForVerifyOnly(), NotNull());
  EXPECT_THAT(auth_session.GetAuthForWebAuthn(), IsNull());
  EXPECT_TRUE(auth_session.user_exists());

  user_data_auth::UpdateAuthFactorRequest request;
  request.set_auth_session_id(auth_session.serialized_token());
  request.set_auth_factor_label(kFakeLabel);
  request.mutable_auth_factor()->set_type(
      user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
  request.mutable_auth_factor()->set_label(kFakeOtherLabel);
  request.mutable_auth_factor()->mutable_password_metadata();
  request.mutable_auth_input()->mutable_password_input()->set_secret(
      kFakeOtherPass);

  TestFuture<CryptohomeStatus> update_future;
  auth_session.GetAuthForDecrypt()->UpdateAuthFactor(
      request, update_future.GetCallback());

  // Verify.
  ASSERT_THAT(update_future.Get(), NotOk());
  // Verify that the credential_verifier is not updated on failure.
  UserSession* user_session = FindOrCreateUserSession(kFakeUsername);
  EXPECT_THAT(user_session->GetCredentialVerifiers(), IsEmpty());
}

// UpdateAuthFactor fails if label doesn't exist in the existing factors.
TEST_F(AuthSessionTest, UpdateAuthFactorFailsLabelNotFoundInAFMap) {
  // Setup.
  AuthSession auth_session(
      {.username = kFakeUsername,
       .is_ephemeral_user = false,
       .intent = AuthIntent::kDecrypt,
       .auth_factor_status_update_timer =
           std::make_unique<base::WallClockTimer>(),
       .user_exists = true,
       .auth_factor_map =
           AfMapBuilder()
               .AddPassword<TpmBoundToPcrAuthBlockState>(kFakeLabel)
               .Consume()},
      backing_apis_);
  EXPECT_THAT(auth_session.authorized_intents(), IsEmpty());
  EXPECT_THAT(auth_session.GetAuthForDecrypt(), IsNull());
  EXPECT_THAT(auth_session.GetAuthForVerifyOnly(), IsNull());
  EXPECT_THAT(auth_session.GetAuthForWebAuthn(), IsNull());

  EXPECT_TRUE(auth_session.user_exists());

  // Creating the user.
  EXPECT_TRUE(auth_session.OnUserCreated().ok());
  EXPECT_THAT(
      auth_session.authorized_intents(),
      UnorderedElementsAre(AuthIntent::kDecrypt, AuthIntent::kVerifyOnly));
  EXPECT_THAT(auth_session.GetAuthForDecrypt(), NotNull());
  EXPECT_THAT(auth_session.GetAuthForVerifyOnly(), NotNull());
  EXPECT_THAT(auth_session.GetAuthForWebAuthn(), IsNull());
  EXPECT_TRUE(auth_session.user_exists());

  user_data_auth::UpdateAuthFactorRequest request;
  request.set_auth_session_id(auth_session.serialized_token());
  request.set_auth_factor_label(kFakeOtherLabel);
  request.mutable_auth_factor()->set_type(
      user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
  request.mutable_auth_factor()->set_label(kFakeOtherLabel);
  request.mutable_auth_factor()->mutable_password_metadata();
  request.mutable_auth_input()->mutable_password_input()->set_secret(
      kFakeOtherPass);

  TestFuture<CryptohomeStatus> update_future;
  auth_session.GetAuthForDecrypt()->UpdateAuthFactor(
      request, update_future.GetCallback());

  // Verify.
  ASSERT_THAT(update_future.Get(), NotOk());
  // Verify that the credential_verifier is not updated on failure.
  UserSession* user_session = FindOrCreateUserSession(kFakeUsername);
  EXPECT_THAT(user_session->GetCredentialVerifiers(), IsEmpty());
}

// A variant of the auth session test that tests AuthFactor APIs with the
// UserSecretStash.
class AuthSessionWithUssTest : public AuthSessionTest {
 protected:
  struct ReplyToVerifyKey {
    void operator()(const Username& account_id,
                    const SerializedChallengePublicKeyInfo& public_key_info,
                    std::unique_ptr<KeyChallengeService> key_challenge_service,
                    ChallengeCredentialsHelper::VerifyKeyCallback callback) {
      if (is_key_valid) {
        std::move(callback).Run(OkStatus<error::CryptohomeCryptoError>());
      } else {
        const error::CryptohomeError::ErrorLocationPair
            kErrorLocationPlaceholder =
                error::CryptohomeError::ErrorLocationPair(
                    static_cast<
                        ::cryptohome::error::CryptohomeError::ErrorLocation>(1),
                    "Testing1");

        std::move(callback).Run(MakeStatus<error::CryptohomeCryptoError>(
            kErrorLocationPlaceholder,
            error::ErrorActionSet(error::PrimaryAction::kIncorrectAuth),
            CryptoError::CE_OTHER_CRYPTO));
      }
    }
    bool is_key_valid = false;
  };

  user_data_auth::CryptohomeErrorCode AddRecoveryAuthFactor(
      const std::string& label,
      const std::string& secret,
      AuthSession& auth_session) {
    EXPECT_CALL(auth_block_utility_, SelectAuthBlockTypeForCreation(_))
        .WillRepeatedly(ReturnValue(AuthBlockType::kCryptohomeRecovery));
    EXPECT_CALL(
        auth_block_utility_,
        CreateKeyBlobsWithAuthBlock(AuthBlockType::kCryptohomeRecovery, _, _))
        .WillOnce([&secret](auto auth_block_type, auto auth_input,
                            auto create_callback) {
          auto key_blobs = std::make_unique<KeyBlobs>();
          key_blobs->vkk_key = brillo::SecureBlob(secret);
          auto auth_block_state = std::make_unique<AuthBlockState>();
          auth_block_state->state = CryptohomeRecoveryAuthBlockState();
          std::move(create_callback)
              .Run(OkStatus<CryptohomeCryptoError>(), std::move(key_blobs),
                   std::move(auth_block_state));
        });
    // Prepare recovery add request.
    user_data_auth::AddAuthFactorRequest request;
    request.set_auth_session_id(auth_session.serialized_token());
    request.mutable_auth_factor()->set_type(
        user_data_auth::AUTH_FACTOR_TYPE_CRYPTOHOME_RECOVERY);
    request.mutable_auth_factor()->set_label(label);
    request.mutable_auth_factor()->mutable_cryptohome_recovery_metadata();
    request.mutable_auth_input()
        ->mutable_cryptohome_recovery_input()
        ->set_mediator_pub_key("mediator pub key");
    // Add recovery AuthFactor.
    TestFuture<CryptohomeStatus> add_future;
    auth_session.GetAuthForDecrypt()->AddAuthFactor(request,
                                                    add_future.GetCallback());

    if (add_future.Get().ok() ||
        !add_future.Get()->local_legacy_error().has_value()) {
      return user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
    }

    return add_future.Get()->local_legacy_error().value();
  }

  user_data_auth::CryptohomeErrorCode AddPasswordAuthFactor(
      const std::string& label,
      const std::string& password,
      bool first_factor,
      AuthSession& auth_session) {
    EXPECT_CALL(auth_block_utility_, SelectAuthBlockTypeForCreation(_))
        .WillRepeatedly(ReturnValue(AuthBlockType::kTpmBoundToPcr));
    EXPECT_CALL(auth_block_utility_, CreateKeyBlobsWithAuthBlock(
                                         AuthBlockType::kTpmBoundToPcr, _, _))
        .WillOnce([](AuthBlockType auth_block_type, const AuthInput& auth_input,
                     AuthBlock::CreateCallback create_callback) {
          // Make an arbitrary auth block state type can be used in this test.
          auto key_blobs = std::make_unique<KeyBlobs>();
          key_blobs->vkk_key =
              GetFakeDerivedSecret(auth_input.user_input.value());
          auto auth_block_state = std::make_unique<AuthBlockState>();
          auth_block_state->state = TpmBoundToPcrAuthBlockState();
          std::move(create_callback)
              .Run(OkStatus<CryptohomeCryptoError>(), std::move(key_blobs),
                   std::move(auth_block_state));
        });
    user_data_auth::AddAuthFactorRequest request;
    request.mutable_auth_factor()->set_type(
        user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
    request.mutable_auth_factor()->set_label(label);
    request.mutable_auth_factor()->mutable_password_metadata();
    request.mutable_auth_input()->mutable_password_input()->set_secret(
        password);
    request.set_auth_session_id(auth_session.serialized_token());

    TestFuture<CryptohomeStatus> add_future;
    auth_session.GetAuthForDecrypt()->AddAuthFactor(request,
                                                    add_future.GetCallback());

    if (add_future.Get().ok() ||
        !add_future.Get()->local_legacy_error().has_value()) {
      return user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
    }

    return add_future.Get()->local_legacy_error().value();
  }

  user_data_auth::CryptohomeErrorCode AuthenticateRecoveryAuthFactor(
      const std::string& auth_factor_label,
      const std::string& secret,
      AuthSession& auth_session) {
    EXPECT_CALL(auth_block_utility_,
                GetAuthBlockTypeFromState(
                    AuthBlockStateTypeIs<CryptohomeRecoveryAuthBlockState>()))
        .WillRepeatedly(Return(AuthBlockType::kCryptohomeRecovery));
    EXPECT_CALL(auth_block_utility_,
                DeriveKeyBlobsWithAuthBlock(AuthBlockType::kCryptohomeRecovery,
                                            _, _, _))
        .WillOnce([&secret](auto auth_block_type, auto auth_input,
                            auto auth_state, auto derive_callback) {
          auto key_blobs = std::make_unique<KeyBlobs>();
          key_blobs->vkk_key = brillo::SecureBlob(secret);
          std::move(derive_callback)
              .Run(OkStatus<CryptohomeCryptoError>(), std::move(key_blobs),
                   std::nullopt);
        });
    // Prepare recovery authentication request.
    std::vector<std::string> auth_factor_labels{auth_factor_label};
    user_data_auth::AuthInput auth_input_proto;
    auth_input_proto.mutable_cryptohome_recovery_input()
        ->mutable_recovery_response();
    AuthenticateTestFuture authenticate_future;
    SerializedUserAuthFactorTypePolicy auth_factor_type_policy(
        {.type = *SerializeAuthFactorType(
             *DetermineFactorTypeFromAuthInput(auth_input_proto)),
         .enabled_intents = {},
         .disabled_intents = {}});
    // Authenticate using recovery.
    auth_session.AuthenticateAuthFactor(
        ToAuthenticateRequest(auth_factor_labels, auth_input_proto),
        auth_factor_type_policy, authenticate_future.GetCallback());
    // Verify.
    auto& [unused_action, status] = authenticate_future.Get();
    if (status.ok() || !status->local_legacy_error().has_value()) {
      return user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
    }
    return status->local_legacy_error().value();
  }

  user_data_auth::CryptohomeErrorCode AuthenticatePasswordAuthFactor(
      const std::string& label,
      const std::string& password,
      AuthSession& auth_session) {
    EXPECT_CALL(auth_block_utility_,
                GetAuthBlockTypeFromState(
                    AuthBlockStateTypeIs<TpmBoundToPcrAuthBlockState>()))
        .WillRepeatedly(Return(AuthBlockType::kTpmBoundToPcr));
    EXPECT_CALL(
        auth_block_utility_,
        DeriveKeyBlobsWithAuthBlock(AuthBlockType::kTpmBoundToPcr, _, _, _))
        .WillOnce([](AuthBlockType auth_block_type, const AuthInput& auth_input,
                     const AuthBlockState& auth_state,
                     AuthBlock::DeriveCallback derive_callback) {
          auto key_blobs = std::make_unique<KeyBlobs>();
          key_blobs->vkk_key =
              GetFakeDerivedSecret(auth_input.user_input.value());
          std::move(derive_callback)
              .Run(OkStatus<CryptohomeCryptoError>(), std::move(key_blobs),
                   std::nullopt);
        });

    AuthenticateTestFuture authenticate_future;
    std::vector<std::string> auth_factor_labels{label};
    user_data_auth::AuthInput auth_input_proto;
    auth_input_proto.mutable_password_input()->set_secret(password);
    SerializedUserAuthFactorTypePolicy auth_factor_type_policy(
        {.type = *SerializeAuthFactorType(
             *DetermineFactorTypeFromAuthInput(auth_input_proto)),
         .enabled_intents = {},
         .disabled_intents = {}});
    auth_session.AuthenticateAuthFactor(
        ToAuthenticateRequest(auth_factor_labels, auth_input_proto),
        auth_factor_type_policy, authenticate_future.GetCallback());

    // Verify.
    auto& [unused_action, status] = authenticate_future.Get();
    if (status.ok() || !status->local_legacy_error().has_value()) {
      return user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
    }
    return status->local_legacy_error().value();
  }

  user_data_auth::CryptohomeErrorCode UpdatePasswordAuthFactor(
      const std::string& new_password, AuthSession& auth_session) {
    EXPECT_CALL(auth_block_utility_, SelectAuthBlockTypeForCreation(_))
        .WillRepeatedly(ReturnValue(AuthBlockType::kTpmBoundToPcr));
    EXPECT_CALL(auth_block_utility_, CreateKeyBlobsWithAuthBlock(
                                         AuthBlockType::kTpmBoundToPcr, _, _))
        .WillOnce([](AuthBlockType auth_block_type, const AuthInput& auth_input,
                     AuthBlock::CreateCallback create_callback) {
          // Make an arbitrary auth block state type can be used in this test.
          auto key_blobs = std::make_unique<KeyBlobs>();
          key_blobs->vkk_key =
              GetFakeDerivedSecret(auth_input.user_input.value());
          auto auth_block_state = std::make_unique<AuthBlockState>();
          auth_block_state->state = TpmBoundToPcrAuthBlockState();
          std::move(create_callback)
              .Run(OkStatus<CryptohomeCryptoError>(), std::move(key_blobs),
                   std::move(auth_block_state));
        });

    user_data_auth::UpdateAuthFactorRequest request;
    request.set_auth_session_id(auth_session.serialized_token());
    request.set_auth_factor_label(kFakeLabel);
    request.mutable_auth_factor()->set_type(
        user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
    request.mutable_auth_factor()->set_label(kFakeLabel);
    request.mutable_auth_factor()->mutable_password_metadata();
    request.mutable_auth_input()->mutable_password_input()->set_secret(
        new_password);

    TestFuture<CryptohomeStatus> update_future;
    auth_session.GetAuthForDecrypt()->UpdateAuthFactor(
        request, update_future.GetCallback());

    if (update_future.Get().ok() ||
        !update_future.Get()->local_legacy_error().has_value()) {
      return user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
    }

    return update_future.Get()->local_legacy_error().value();
  }

  user_data_auth::CryptohomeErrorCode UpdateAuthFactorMetadata(
      user_data_auth::AuthFactor& auth_factor_proto,
      AuthSession& auth_session) {
    user_data_auth::UpdateAuthFactorMetadataRequest request;
    request.set_auth_session_id(auth_session.serialized_token());
    request.set_auth_factor_label(auth_factor_proto.label());
    *request.mutable_auth_factor() = std::move(auth_factor_proto);

    TestFuture<CryptohomeStatus> update_future;
    auth_session.UpdateAuthFactorMetadata(request, update_future.GetCallback());

    if (update_future.Get().ok() ||
        !update_future.Get().status()->local_legacy_error().has_value()) {
      return user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
    }

    return update_future.Get().status()->local_legacy_error().value();
  }

  user_data_auth::CryptohomeErrorCode RelabelAuthFactor(
      const std::string& old_label,
      const std::string& new_label,
      AuthSession& auth_session) {
    user_data_auth::RelabelAuthFactorRequest request;
    request.set_auth_session_id(auth_session.serialized_token());
    request.set_auth_factor_label(old_label);
    request.set_new_auth_factor_label(new_label);

    TestFuture<CryptohomeStatus> relabel_future;
    auth_session.GetAuthForDecrypt()->RelabelAuthFactor(
        request, relabel_future.GetCallback());

    if (relabel_future.Get().ok() ||
        !relabel_future.Get()->local_legacy_error().has_value()) {
      return user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
    }

    return relabel_future.Get()->local_legacy_error().value();
  }

  user_data_auth::CryptohomeErrorCode AddPinAuthFactor(
      const std::string& pin, AuthSession& auth_session) {
    EXPECT_CALL(auth_block_utility_, SelectAuthBlockTypeForCreation(_))
        .WillRepeatedly(ReturnValue(AuthBlockType::kPinWeaver));
    EXPECT_CALL(auth_block_utility_,
                CreateKeyBlobsWithAuthBlock(AuthBlockType::kPinWeaver, _, _))
        .WillOnce([](AuthBlockType auth_block_type, const AuthInput& auth_input,
                     AuthBlock::CreateCallback create_callback) {
          // Make an arbitrary auth block state type can be used in this test.
          auto key_blobs = std::make_unique<KeyBlobs>();
          key_blobs->vkk_key =
              GetFakeDerivedSecret(auth_input.user_input.value());
          key_blobs->reset_secret = auth_input.reset_secret;
          auto auth_block_state = std::make_unique<AuthBlockState>();
          auth_block_state->state = PinWeaverAuthBlockState();
          std::move(create_callback)
              .Run(OkStatus<CryptohomeCryptoError>(), std::move(key_blobs),
                   std::move(auth_block_state));
        });
    // Calling AddAuthFactor.
    user_data_auth::AddAuthFactorRequest add_pin_request;
    add_pin_request.set_auth_session_id(auth_session.serialized_token());
    add_pin_request.mutable_auth_factor()->set_type(
        user_data_auth::AUTH_FACTOR_TYPE_PIN);
    add_pin_request.mutable_auth_factor()->set_label(kFakePinLabel);
    add_pin_request.mutable_auth_factor()->mutable_pin_metadata();
    add_pin_request.mutable_auth_input()->mutable_pin_input()->set_secret(pin);
    TestFuture<CryptohomeStatus> add_future;
    auth_session.GetAuthForDecrypt()->AddAuthFactor(add_pin_request,
                                                    add_future.GetCallback());

    if (add_future.Get().ok() ||
        !add_future.Get()->local_legacy_error().has_value()) {
      return user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
    }
    return add_future.Get()->local_legacy_error().value();
  }

  user_data_auth::CryptohomeErrorCode AddFingerprintAuthFactor(
      AuthSession& auth_session,
      const std::string& label,
      const brillo::SecureBlob& vkk_key,
      const std::string& record_id,
      uint64_t leaf_label) {
    EXPECT_CALL(auth_block_utility_, SelectAuthBlockTypeForCreation(_))
        .WillOnce(ReturnValue(AuthBlockType::kFingerprint));
    EXPECT_CALL(auth_block_utility_,
                CreateKeyBlobsWithAuthBlock(AuthBlockType::kFingerprint, _, _))
        .WillOnce([&](AuthBlockType auth_block_type,
                      const AuthInput& auth_input,
                      AuthBlock::CreateCallback create_callback) {
          EXPECT_TRUE(auth_input.rate_limiter_label.has_value());
          EXPECT_TRUE(auth_input.reset_secret.has_value());
          // Make an arbitrary auth block state type that can be used in the
          // tests.
          auto key_blobs = std::make_unique<KeyBlobs>();
          key_blobs->vkk_key = vkk_key;
          key_blobs->reset_secret = auth_input.reset_secret;
          auto auth_block_state = std::make_unique<AuthBlockState>();
          FingerprintAuthBlockState fingerprint_state =
              FingerprintAuthBlockState();
          fingerprint_state.template_id = record_id;
          fingerprint_state.gsc_secret_label = leaf_label;
          auth_block_state->state = fingerprint_state;
          std::move(create_callback)
              .Run(OkStatus<CryptohomeCryptoError>(), std::move(key_blobs),
                   std::move(auth_block_state));
        });
    // Calling AddAuthFactor.
    user_data_auth::AddAuthFactorRequest request;
    request.set_auth_session_id(auth_session.serialized_token());
    request.mutable_auth_factor()->set_type(
        user_data_auth::AUTH_FACTOR_TYPE_FINGERPRINT);
    request.mutable_auth_factor()->set_label(label);
    request.mutable_auth_factor()->mutable_fingerprint_metadata();
    request.mutable_auth_input()->mutable_fingerprint_input();

    TestFuture<CryptohomeStatus> add_future;
    auth_session.GetAuthForDecrypt()->AddAuthFactor(request,
                                                    add_future.GetCallback());

    if (add_future.Get().ok() ||
        !add_future.Get()->local_legacy_error().has_value()) {
      return user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
    }

    return add_future.Get()->local_legacy_error().value();
  }

  user_data_auth::CryptohomeErrorCode AddFirstFingerprintAuthFactor(
      AuthSession& auth_session) {
    return AddFingerprintAuthFactor(auth_session, kFakeFingerprintLabel,
                                    brillo::SecureBlob(kFakeVkkKey),
                                    kFakeRecordId, kFakeFpLabel);
  }

  user_data_auth::CryptohomeErrorCode AddSecondFingerprintAuthFactor(
      AuthSession& auth_session) {
    return AddFingerprintAuthFactor(auth_session, kFakeSecondFingerprintLabel,
                                    brillo::SecureBlob(kFakeSecondVkkKey),
                                    kFakeSecondRecordId, kFakeSecondFpLabel);
  }
};

// Test that the USS is created on the user creation.
TEST_F(AuthSessionWithUssTest, UssCreation) {
  // Setup.
  AuthSession auth_session({.username = kFakeUsername,
                            .is_ephemeral_user = false,
                            .intent = AuthIntent::kDecrypt,
                            .auth_factor_status_update_timer =
                                std::make_unique<base::WallClockTimer>(),
                            .user_exists = false,
                            .auth_factor_map = AuthFactorMap()},
                           backing_apis_);

  // Test.
  EXPECT_FALSE(auth_session.has_user_secret_stash());
  EXPECT_TRUE(auth_session.OnUserCreated().ok());

  // Verify.
  EXPECT_TRUE(auth_session.has_user_secret_stash());
  UserSession* user_session = FindOrCreateUserSession(kFakeUsername);
  EXPECT_THAT(user_session->GetCredentialVerifiers(), IsEmpty());
}

// Test that no USS is created for an ephemeral user.
TEST_F(AuthSessionWithUssTest, NoUssForEphemeral) {
  // Setup.
  AuthSession auth_session({.username = kFakeUsername,
                            .is_ephemeral_user = true,
                            .intent = AuthIntent::kDecrypt,
                            .auth_factor_status_update_timer =
                                std::make_unique<base::WallClockTimer>(),
                            .user_exists = false,
                            .auth_factor_map = AuthFactorMap()},
                           backing_apis_);

  // Test.
  EXPECT_TRUE(auth_session.OnUserCreated().ok());

  // Verify.
  EXPECT_FALSE(auth_session.has_user_secret_stash());
}

// Test that a new auth factor can be added to the newly created user.
TEST_F(AuthSessionWithUssTest, AddPasswordAuthFactorViaUss) {
  // Setup.
  AuthSession auth_session({.username = kFakeUsername,
                            .is_ephemeral_user = false,
                            .intent = AuthIntent::kDecrypt,
                            .auth_factor_status_update_timer =
                                std::make_unique<base::WallClockTimer>(),
                            .user_exists = false,
                            .auth_factor_map = AuthFactorMap()},
                           backing_apis_);
  // Creating the user.
  EXPECT_TRUE(auth_session.OnUserCreated().ok());
  EXPECT_TRUE(auth_session.has_user_secret_stash());

  // Test.
  // Setting the expectation that the auth block utility will create key
  // blobs.
  EXPECT_CALL(auth_block_utility_, SelectAuthBlockTypeForCreation(_))
      .WillRepeatedly(ReturnValue(AuthBlockType::kTpmBoundToPcr));
  EXPECT_CALL(auth_block_utility_,
              CreateKeyBlobsWithAuthBlock(AuthBlockType::kTpmBoundToPcr, _, _))
      .WillOnce([](AuthBlockType auth_block_type, const AuthInput& auth_input,
                   AuthBlock::CreateCallback create_callback) {
        // Make an arbitrary auth block state type can be used in this test.
        auto key_blobs = std::make_unique<KeyBlobs>();
        key_blobs->vkk_key = brillo::SecureBlob("fake vkk key");
        auto auth_block_state = std::make_unique<AuthBlockState>();
        auth_block_state->state = TpmBoundToPcrAuthBlockState();
        std::move(create_callback)
            .Run(OkStatus<CryptohomeCryptoError>(), std::move(key_blobs),
                 std::move(auth_block_state));
      });
  // Calling AddAuthFactor.
  user_data_auth::AddAuthFactorRequest request;
  request.set_auth_session_id(auth_session.serialized_token());
  request.mutable_auth_factor()->set_type(
      user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
  request.mutable_auth_factor()->set_label(kFakeLabel);
  request.mutable_auth_factor()->mutable_password_metadata();
  request.mutable_auth_input()->mutable_password_input()->set_secret(kFakePass);

  TestFuture<CryptohomeStatus> add_future;
  auth_session.GetAuthForDecrypt()->AddAuthFactor(request,
                                                  add_future.GetCallback());

  // Verify
  EXPECT_THAT(add_future.Get(), IsOk());
  UserSession* user_session = FindOrCreateUserSession(kFakeUsername);
  EXPECT_THAT(user_session->GetCredentialVerifiers(),
              UnorderedElementsAre(
                  IsVerifierPtrWithLabelAndPassword(kFakeLabel, kFakePass)));

  std::map<std::string, AuthFactorType> stored_factors =
      auth_factor_manager_.ListAuthFactors(SanitizeUserName(kFakeUsername));
  EXPECT_THAT(stored_factors,
              ElementsAre(Pair(kFakeLabel, AuthFactorType::kPassword)));
  EXPECT_THAT(auth_session.auth_factor_map().Find(kFakeLabel), Optional(_));
}

// TODO(betuls) : migrate to uss test
// Test that AuthenticateAuthFactor succeeds in the `AuthIntent::kWebAuthn`
// scenario.
TEST_F(AuthSessionWithUssTest, AuthenticateAuthFactorWebAuthnIntent) {
  // Setup.
  AuthSession auth_session({.username = kFakeUsername,
                            .is_ephemeral_user = false,
                            .intent = AuthIntent::kWebAuthn,
                            .auth_factor_status_update_timer =
                                std::make_unique<base::WallClockTimer>(),
                            .user_exists = false,
                            .auth_factor_map = AuthFactorMap()},
                           backing_apis_);
  // Creating the user.
  EXPECT_TRUE(auth_session.OnUserCreated().ok());
  EXPECT_TRUE(auth_session.has_user_secret_stash());

  EXPECT_EQ(user_data_auth::CRYPTOHOME_ERROR_NOT_SET,
            AddPasswordAuthFactor(kFakeLabel, kFakePass,
                                  /*first_factor=*/true, auth_session));
  // Add the user session. Expect that no verification calls are made.
  auto user_session = std::make_unique<MockUserSession>();
  EXPECT_CALL(*user_session, PrepareWebAuthnSecret(_, _));
  EXPECT_TRUE(user_session_map_.Add(kFakeUsername, std::move(user_session)));
  // Calling AuthenticateAuthFactor.
  EXPECT_EQ(
      user_data_auth::CRYPTOHOME_ERROR_NOT_SET,
      AuthenticatePasswordAuthFactor(kFakeLabel, kFakePass, auth_session));

  // Verify.
  EXPECT_THAT(
      auth_session.authorized_intents(),
      UnorderedElementsAre(AuthIntent::kDecrypt, AuthIntent::kVerifyOnly,
                           AuthIntent::kWebAuthn));
}

// Test that a new auth factor can be added to the newly created user using
// asynchronous key creation.
TEST_F(AuthSessionWithUssTest, AddPasswordAuthFactorViaAsyncUss) {
  // Setup.
  AuthSession auth_session({.username = kFakeUsername,
                            .is_ephemeral_user = false,
                            .intent = AuthIntent::kDecrypt,
                            .auth_factor_status_update_timer =
                                std::make_unique<base::WallClockTimer>(),
                            .user_exists = false,
                            .auth_factor_map = AuthFactorMap()},
                           backing_apis_);

  // Creating the user.
  EXPECT_TRUE(auth_session.OnUserCreated().ok());
  EXPECT_TRUE(auth_session.has_user_secret_stash());

  // Test.
  // Setting the expectation that the auth block utility will create key blobs.
  EXPECT_CALL(auth_block_utility_, SelectAuthBlockTypeForCreation(_))
      .WillRepeatedly(ReturnValue(AuthBlockType::kTpmBoundToPcr));
  EXPECT_CALL(auth_block_utility_,
              CreateKeyBlobsWithAuthBlock(AuthBlockType::kTpmBoundToPcr, _, _))
      .WillOnce([this](AuthBlockType, const AuthInput&,
                       AuthBlock::CreateCallback create_callback) {
        // Make an arbitrary auth block state, but schedule it to run later to
        // simulate an proper async key creation.
        auto key_blobs = std::make_unique<KeyBlobs>();
        key_blobs->vkk_key = brillo::SecureBlob("fake vkk key");
        auto auth_block_state = std::make_unique<AuthBlockState>();
        auth_block_state->state = TpmBoundToPcrAuthBlockState();
        task_runner_->PostTask(
            FROM_HERE,
            base::BindOnce(std::move(create_callback),
                           OkStatus<CryptohomeCryptoError>(),
                           std::move(key_blobs), std::move(auth_block_state)));
      });
  // Calling AddAuthFactor.
  user_data_auth::AddAuthFactorRequest request;
  request.set_auth_session_id(auth_session.serialized_token());
  request.mutable_auth_factor()->set_type(
      user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
  request.mutable_auth_factor()->set_label(kFakeLabel);
  request.mutable_auth_factor()->mutable_password_metadata();
  request.mutable_auth_input()->mutable_password_input()->set_secret(kFakePass);

  TestFuture<CryptohomeStatus> add_future;
  auth_session.GetAuthForDecrypt()->AddAuthFactor(request,
                                                  add_future.GetCallback());

  // Verify.
  EXPECT_THAT(add_future.Get(), IsOk());
  UserSession* user_session = FindOrCreateUserSession(kFakeUsername);
  EXPECT_THAT(user_session->GetCredentialVerifiers(),
              UnorderedElementsAre(
                  IsVerifierPtrWithLabelAndPassword(kFakeLabel, kFakePass)));

  std::map<std::string, AuthFactorType> stored_factors =
      auth_factor_manager_.ListAuthFactors(SanitizeUserName(kFakeUsername));
  EXPECT_THAT(stored_factors,
              ElementsAre(Pair(kFakeLabel, AuthFactorType::kPassword)));
  EXPECT_THAT(auth_session.auth_factor_map().Find(kFakeLabel), Optional(_));
}

// Test the new auth factor failure path when asynchronous key creation fails.
TEST_F(AuthSessionWithUssTest, AddPasswordAuthFactorViaAsyncUssFails) {
  // Setup.
  AuthSession auth_session({.username = kFakeUsername,
                            .is_ephemeral_user = false,
                            .intent = AuthIntent::kDecrypt,
                            .auth_factor_status_update_timer =
                                std::make_unique<base::WallClockTimer>(),
                            .user_exists = false,
                            .auth_factor_map = AuthFactorMap()},
                           backing_apis_);

  // Creating the user.
  EXPECT_TRUE(auth_session.OnUserCreated().ok());
  EXPECT_TRUE(auth_session.has_user_secret_stash());

  // Test.
  // Setting the expectation that the auth block utility will be called an that
  // key blob creation will fail.
  EXPECT_CALL(auth_block_utility_, SelectAuthBlockTypeForCreation(_))
      .WillRepeatedly(ReturnValue(AuthBlockType::kTpmBoundToPcr));
  EXPECT_CALL(auth_block_utility_,
              CreateKeyBlobsWithAuthBlock(AuthBlockType::kTpmBoundToPcr, _, _))
      .WillOnce([this](AuthBlockType, const AuthInput&,
                       AuthBlock::CreateCallback create_callback) {
        // Have the creation callback report an error.
        task_runner_->PostTask(
            FROM_HERE,
            base::BindOnce(
                std::move(create_callback),
                MakeStatus<CryptohomeCryptoError>(
                    kErrorLocationForTestingAuthSession,
                    error::ErrorActionSet(
                        {error::PossibleAction::kDevCheckUnexpectedState}),
                    CryptoError::CE_OTHER_CRYPTO),
                nullptr, nullptr));
      });
  // Calling AddAuthFactor.
  user_data_auth::AddAuthFactorRequest request;
  request.set_auth_session_id(auth_session.serialized_token());
  request.mutable_auth_factor()->set_type(
      user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
  request.mutable_auth_factor()->set_label(kFakeLabel);
  request.mutable_auth_factor()->mutable_password_metadata();
  request.mutable_auth_input()->mutable_password_input()->set_secret(kFakePass);

  TestFuture<CryptohomeStatus> add_future;
  auth_session.GetAuthForDecrypt()->AddAuthFactor(request,
                                                  add_future.GetCallback());

  // Verify.
  ASSERT_THAT(add_future.Get(), NotOk());
  UserSession* user_session = FindOrCreateUserSession(kFakeUsername);
  EXPECT_THAT(user_session->GetCredentialVerifiers(), IsEmpty());
  ASSERT_EQ(add_future.Get()->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ADD_CREDENTIALS_FAILED);
  std::map<std::string, AuthFactorType> stored_factors =
      auth_factor_manager_.ListAuthFactors(SanitizeUserName(kFakeUsername));
  EXPECT_THAT(stored_factors, IsEmpty());
}

// Test the new auth factor failure path when asynchronous key creation succeeds
// but when writing to USS fails.
TEST_F(AuthSessionWithUssTest,
       AddPasswordAuthFactorViaAsyncUssFailsOnWriteFailure) {
  // Setup.
  AuthSession auth_session({.username = kFakeUsername,
                            .is_ephemeral_user = false,
                            .intent = AuthIntent::kDecrypt,
                            .auth_factor_status_update_timer =
                                std::make_unique<base::WallClockTimer>(),
                            .user_exists = false,
                            .auth_factor_map = AuthFactorMap()},
                           backing_apis_);

  // Creating the user.
  EXPECT_TRUE(auth_session.OnUserCreated().ok());
  EXPECT_TRUE(auth_session.has_user_secret_stash());

  // Test.
  // Setting the expectation that the auth block utility will create key blobs
  // but then writing to USS will fail.
  EXPECT_CALL(auth_block_utility_, SelectAuthBlockTypeForCreation(_))
      .WillRepeatedly(ReturnValue(AuthBlockType::kTpmBoundToPcr));
  EXPECT_CALL(auth_block_utility_,
              CreateKeyBlobsWithAuthBlock(AuthBlockType::kTpmBoundToPcr, _, _))
      .WillOnce([this](AuthBlockType, const AuthInput&,
                       AuthBlock::CreateCallback create_callback) {
        // Make an arbitrary auth block state, but schedule it to run later to
        // simulate an proper async key creation.
        auto key_blobs = std::make_unique<KeyBlobs>();
        key_blobs->vkk_key = brillo::SecureBlob("fake vkk key");
        auto auth_block_state = std::make_unique<AuthBlockState>();
        auth_block_state->state = TpmBoundToPcrAuthBlockState();
        task_runner_->PostTask(
            FROM_HERE,
            base::BindOnce(std::move(create_callback),
                           OkStatus<CryptohomeCryptoError>(),
                           std::move(key_blobs), std::move(auth_block_state)));
      });
  EXPECT_CALL(platform_, WriteFileAtomicDurable(_, _, _))
      .WillRepeatedly(DoDefault());
  EXPECT_CALL(platform_,
              WriteFileAtomicDurable(
                  UserSecretStashPath(SanitizeUserName(kFakeUsername),
                                      kUserSecretStashDefaultSlot),
                  _, _))
      .Times(AtLeast(1))
      .WillRepeatedly(Return(false));
  // Calling AddAuthFactor.
  user_data_auth::AddAuthFactorRequest request;
  request.set_auth_session_id(auth_session.serialized_token());
  request.mutable_auth_factor()->set_type(
      user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
  request.mutable_auth_factor()->set_label(kFakeLabel);
  request.mutable_auth_factor()->mutable_password_metadata();
  request.mutable_auth_input()->mutable_password_input()->set_secret(kFakePass);

  TestFuture<CryptohomeStatus> add_future;
  auth_session.GetAuthForDecrypt()->AddAuthFactor(request,
                                                  add_future.GetCallback());

  // Verify.
  ASSERT_THAT(add_future.Get(), NotOk());
  UserSession* user_session = FindOrCreateUserSession(kFakeUsername);
  EXPECT_THAT(user_session->GetCredentialVerifiers(), IsEmpty());
  ASSERT_EQ(add_future.Get()->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ADD_CREDENTIALS_FAILED);
}

// Test that a new auth factor and a pin can be added to the newly created user,
// in case the USS experiment is on.
TEST_F(AuthSessionWithUssTest, AddPasswordAndPinAuthFactorViaUss) {
  // Setup.
  AuthSession auth_session({.username = kFakeUsername,
                            .is_ephemeral_user = false,
                            .intent = AuthIntent::kDecrypt,
                            .auth_factor_status_update_timer =
                                std::make_unique<base::WallClockTimer>(),
                            .user_exists = false,
                            .auth_factor_map = AuthFactorMap()},
                           backing_apis_);

  // Creating the user.
  EXPECT_TRUE(auth_session.OnUserCreated().ok());
  EXPECT_TRUE(auth_session.has_user_secret_stash());
  // Add a password first.
  // Setting the expectation that the auth block utility will create key blobs.
  EXPECT_CALL(auth_block_utility_, SelectAuthBlockTypeForCreation(_))
      .WillRepeatedly(ReturnValue(AuthBlockType::kTpmBoundToPcr));
  EXPECT_CALL(auth_block_utility_,
              CreateKeyBlobsWithAuthBlock(AuthBlockType::kTpmBoundToPcr, _, _))
      .WillOnce([](AuthBlockType auth_block_type, const AuthInput& auth_input,
                   AuthBlock::CreateCallback create_callback) {
        // Make an arbitrary auth block state type can be used in this test.
        auto key_blobs = std::make_unique<KeyBlobs>();
        key_blobs->vkk_key = brillo::SecureBlob("fake vkk key");
        auto auth_block_state = std::make_unique<AuthBlockState>();
        auth_block_state->state = TpmBoundToPcrAuthBlockState();
        std::move(create_callback)
            .Run(OkStatus<CryptohomeCryptoError>(), std::move(key_blobs),
                 std::move(auth_block_state));
      });
  // Calling AddAuthFactor.
  user_data_auth::AddAuthFactorRequest request;
  request.set_auth_session_id(auth_session.serialized_token());
  request.mutable_auth_factor()->set_type(
      user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
  request.mutable_auth_factor()->set_label(kFakeLabel);
  request.mutable_auth_factor()->mutable_password_metadata();
  request.mutable_auth_input()->mutable_password_input()->set_secret(kFakePass);

  // Test and Verify.
  TestFuture<CryptohomeStatus> add_future;
  auth_session.GetAuthForDecrypt()->AddAuthFactor(request,
                                                  add_future.GetCallback());

  // Verify.
  EXPECT_THAT(add_future.Get(), IsOk());

  // Setting the expectation that the auth block utility will create key blobs.
  EXPECT_CALL(auth_block_utility_, SelectAuthBlockTypeForCreation(_))
      .WillRepeatedly(ReturnValue(AuthBlockType::kPinWeaver));
  EXPECT_CALL(auth_block_utility_,
              CreateKeyBlobsWithAuthBlock(AuthBlockType::kPinWeaver, _, _))
      .WillOnce([](AuthBlockType auth_block_type, const AuthInput& auth_input,
                   AuthBlock::CreateCallback create_callback) {
        // Make an arbitrary auth block state type can be used in this test.
        auto key_blobs = std::make_unique<KeyBlobs>();
        key_blobs->vkk_key = brillo::SecureBlob("fake vkk key");
        auto auth_block_state = std::make_unique<AuthBlockState>();
        auth_block_state->state = PinWeaverAuthBlockState();
        std::move(create_callback)
            .Run(OkStatus<CryptohomeCryptoError>(), std::move(key_blobs),
                 std::move(auth_block_state));
      });
  // Calling AddAuthFactor.
  user_data_auth::AddAuthFactorRequest add_pin_request;
  add_pin_request.set_auth_session_id(auth_session.serialized_token());
  add_pin_request.mutable_auth_factor()->set_type(
      user_data_auth::AUTH_FACTOR_TYPE_PIN);
  add_pin_request.mutable_auth_factor()->set_label(kFakePinLabel);
  add_pin_request.mutable_auth_factor()->mutable_pin_metadata();
  add_pin_request.mutable_auth_input()->mutable_pin_input()->set_secret(
      kFakePin);
  // Test and Verify.
  TestFuture<CryptohomeStatus> add_pin_future;
  auth_session.GetAuthForDecrypt()->AddAuthFactor(add_pin_request,
                                                  add_pin_future.GetCallback());

  // Verify.
  ASSERT_THAT(add_pin_future.Get(), IsOk());
  std::map<std::string, AuthFactorType> stored_factors =
      auth_factor_manager_.ListAuthFactors(SanitizeUserName(kFakeUsername));
  EXPECT_THAT(stored_factors,
              ElementsAre(Pair(kFakeLabel, AuthFactorType::kPassword),
                          Pair(kFakePinLabel, AuthFactorType::kPin)));
  UserSession* user_session = FindOrCreateUserSession(kFakeUsername);
  EXPECT_THAT(user_session->GetCredentialVerifiers(),
              UnorderedElementsAre(
                  IsVerifierPtrWithLabelAndPassword(kFakeLabel, kFakePass)));
}

// Test that an existing user with an existing password auth factor can be
// authenticated.
TEST_F(AuthSessionWithUssTest, AuthenticatePasswordAuthFactorViaUss) {
  // Setup.
  const ObfuscatedUsername obfuscated_username =
      SanitizeUserName(kFakeUsername);
  const brillo::SecureBlob kFakePerCredentialSecret("fake-vkk");
  // Setting the expectation that the user exists.
  EXPECT_CALL(platform_, DirectoryExists(_)).WillRepeatedly(Return(true));
  // Generating the USS.
  CryptohomeStatusOr<DecryptedUss> uss = DecryptedUss::CreateWithRandomMainKey(
      user_uss_storage_, FileSystemKeyset::CreateRandom());
  ASSERT_THAT(uss, IsOk());
  // Creating the auth factor. An arbitrary auth block state is used in this
  // test.
  AuthFactor auth_factor(
      AuthFactorType::kPassword, kFakeLabel,
      AuthFactorMetadata{.metadata = PasswordMetadata()},
      AuthBlockState{.state = TpmBoundToPcrAuthBlockState()});
  EXPECT_TRUE(
      auth_factor_manager_.SaveAuthFactorFile(obfuscated_username, auth_factor)
          .ok());
  AuthFactorMap auth_factor_map;
  auth_factor_map.Add(std::move(auth_factor),
                      AuthFactorStorageType::kUserSecretStash);
  // Adding the auth factor into the USS and persisting the latter.
  const KeyBlobs key_blobs = {.vkk_key = kFakePerCredentialSecret};
  std::optional<brillo::SecureBlob> wrapping_key =
      key_blobs.DeriveUssCredentialSecret();
  ASSERT_TRUE(wrapping_key.has_value());
  {
    auto transaction = uss->StartTransaction();
    ASSERT_THAT(transaction.InsertWrappedMainKey(kFakeLabel, *wrapping_key),
                IsOk());
    ASSERT_THAT(std::move(transaction).Commit(), IsOk());
  }
  // Creating the auth session.
  AuthSession auth_session({.username = kFakeUsername,
                            .is_ephemeral_user = false,
                            .intent = AuthIntent::kDecrypt,
                            .auth_factor_status_update_timer =
                                std::make_unique<base::WallClockTimer>(),
                            .user_exists = true,
                            .auth_factor_map = std::move(auth_factor_map)},
                           backing_apis_);
  EXPECT_TRUE(auth_session.user_exists());

  // Test.
  // Setting the expectation that the auth block utility will derive key
  // blobs.
  EXPECT_CALL(auth_block_utility_,
              GetAuthBlockTypeFromState(
                  AuthBlockStateTypeIs<TpmBoundToPcrAuthBlockState>()))
      .WillRepeatedly(Return(AuthBlockType::kTpmBoundToPcr));
  EXPECT_CALL(auth_block_utility_, DeriveKeyBlobsWithAuthBlock(
                                       AuthBlockType::kTpmBoundToPcr, _, _, _))
      .WillOnce([&kFakePerCredentialSecret](
                    AuthBlockType auth_block_type, const AuthInput& auth_input,
                    const AuthBlockState& auth_state,
                    AuthBlock::DeriveCallback derive_callback) {
        auto key_blobs = std::make_unique<KeyBlobs>();
        key_blobs->vkk_key = kFakePerCredentialSecret;
        std::move(derive_callback)
            .Run(OkStatus<CryptohomeCryptoError>(), std::move(key_blobs),
                 std::nullopt);
      });
  // Calling AuthenticateAuthFactor.
  AuthenticateTestFuture authenticate_future;
  std::vector<std::string> auth_factor_labels{kFakeLabel};
  user_data_auth::AuthInput auth_input_proto;
  auth_input_proto.mutable_password_input()->set_secret(kFakePass);
  SerializedUserAuthFactorTypePolicy auth_factor_type_policy(
      {.type = *SerializeAuthFactorType(
           *DetermineFactorTypeFromAuthInput(auth_input_proto)),
       .enabled_intents = {},
       .disabled_intents = {}});
  auth_session.AuthenticateAuthFactor(
      ToAuthenticateRequest(auth_factor_labels, auth_input_proto),
      auth_factor_type_policy, authenticate_future.GetCallback());

  // Verify.
  auto& [action, status] = authenticate_future.Get();
  EXPECT_THAT(status, IsOk());
  EXPECT_EQ(action.action_type, AuthSession::PostAuthActionType::kNone);
  EXPECT_THAT(
      auth_session.authorized_intents(),
      UnorderedElementsAre(AuthIntent::kDecrypt, AuthIntent::kVerifyOnly));
  EXPECT_THAT(auth_session.GetAuthForDecrypt(), NotNull());
  EXPECT_THAT(auth_session.GetAuthForVerifyOnly(), NotNull());
  EXPECT_THAT(auth_session.GetAuthForWebAuthn(), IsNull());
  EXPECT_TRUE(auth_session.has_user_secret_stash());

  UserSession* user_session = FindOrCreateUserSession(kFakeUsername);
  EXPECT_THAT(user_session->GetCredentialVerifiers(),
              UnorderedElementsAre(
                  IsVerifierPtrWithLabelAndPassword(kFakeLabel, kFakePass)));
}

// Test that an existing user with an existing password auth factor can be
// authenticated, using asynchronous key derivation.
TEST_F(AuthSessionWithUssTest, AuthenticatePasswordAuthFactorViaAsyncUss) {
  // Setup.
  const ObfuscatedUsername obfuscated_username =
      SanitizeUserName(kFakeUsername);
  const brillo::SecureBlob kFakePerCredentialSecret("fake-vkk");
  // Setting the expectation that the user exists.
  EXPECT_CALL(platform_, DirectoryExists(_)).WillRepeatedly(Return(true));
  // Generating the USS.
  CryptohomeStatusOr<DecryptedUss> uss = DecryptedUss::CreateWithRandomMainKey(
      user_uss_storage_, FileSystemKeyset::CreateRandom());
  ASSERT_THAT(uss, IsOk());
  // Creating the auth factor. An arbitrary auth block state is used in this
  // test.
  AuthFactor auth_factor(
      AuthFactorType::kPassword, kFakeLabel,
      AuthFactorMetadata{.metadata = PasswordMetadata()},
      AuthBlockState{.state = TpmBoundToPcrAuthBlockState()});
  EXPECT_TRUE(
      auth_factor_manager_.SaveAuthFactorFile(obfuscated_username, auth_factor)
          .ok());
  AuthFactorMap auth_factor_map;
  auth_factor_map.Add(std::move(auth_factor),
                      AuthFactorStorageType::kUserSecretStash);
  // Adding the auth factor into the USS and persisting the latter.
  const KeyBlobs key_blobs = {.vkk_key = kFakePerCredentialSecret};
  std::optional<brillo::SecureBlob> wrapping_key =
      key_blobs.DeriveUssCredentialSecret();
  ASSERT_TRUE(wrapping_key.has_value());
  {
    auto transaction = uss->StartTransaction();
    ASSERT_THAT(transaction.InsertWrappedMainKey(kFakeLabel, *wrapping_key),
                IsOk());
    ASSERT_THAT(std::move(transaction).Commit(), IsOk());
  }
  // Creating the auth session.
  AuthSession auth_session({.username = kFakeUsername,
                            .is_ephemeral_user = false,
                            .intent = AuthIntent::kDecrypt,
                            .auth_factor_status_update_timer =
                                std::make_unique<base::WallClockTimer>(),
                            .user_exists = true,
                            .auth_factor_map = std::move(auth_factor_map)},
                           backing_apis_);
  EXPECT_TRUE(auth_session.user_exists());

  // Test.
  // Setting the expectation that the auth block utility will derive key
  // blobs.
  EXPECT_CALL(auth_block_utility_,
              GetAuthBlockTypeFromState(
                  AuthBlockStateTypeIs<TpmBoundToPcrAuthBlockState>()))
      .WillRepeatedly(Return(AuthBlockType::kTpmBoundToPcr));
  EXPECT_CALL(auth_block_utility_, DeriveKeyBlobsWithAuthBlock(
                                       AuthBlockType::kTpmBoundToPcr, _, _, _))
      .WillOnce([this, &kFakePerCredentialSecret](
                    AuthBlockType auth_block_type, const AuthInput& auth_input,
                    const AuthBlockState& auth_state,
                    AuthBlock::DeriveCallback derive_callback) {
        auto key_blobs = std::make_unique<KeyBlobs>();
        key_blobs->vkk_key = kFakePerCredentialSecret;
        task_runner_->PostTask(
            FROM_HERE, base::BindOnce(std::move(derive_callback),
                                      OkStatus<CryptohomeCryptoError>(),
                                      std::move(key_blobs), std::nullopt));
      });
  // Calling AuthenticateAuthFactor.
  std::vector<std::string> auth_factor_labels{kFakeLabel};
  user_data_auth::AuthInput auth_input_proto;
  auth_input_proto.mutable_password_input()->set_secret(kFakePass);
  AuthenticateTestFuture authenticate_future;
  SerializedUserAuthFactorTypePolicy auth_factor_type_policy(
      {.type = *SerializeAuthFactorType(
           *DetermineFactorTypeFromAuthInput(auth_input_proto)),
       .enabled_intents = {},
       .disabled_intents = {}});
  auth_session.AuthenticateAuthFactor(
      ToAuthenticateRequest(auth_factor_labels, auth_input_proto),
      auth_factor_type_policy, authenticate_future.GetCallback());

  // Verify.
  auto& [action, status] = authenticate_future.Get();
  EXPECT_THAT(status, IsOk());
  EXPECT_EQ(action.action_type, AuthSession::PostAuthActionType::kNone);
  EXPECT_THAT(
      auth_session.authorized_intents(),
      UnorderedElementsAre(AuthIntent::kDecrypt, AuthIntent::kVerifyOnly));
  EXPECT_THAT(auth_session.GetAuthForDecrypt(), NotNull());
  EXPECT_THAT(auth_session.GetAuthForVerifyOnly(), NotNull());
  EXPECT_THAT(auth_session.GetAuthForWebAuthn(), IsNull());
  EXPECT_TRUE(auth_session.has_user_secret_stash());

  UserSession* user_session = FindOrCreateUserSession(kFakeUsername);
  EXPECT_THAT(user_session->GetCredentialVerifiers(),
              UnorderedElementsAre(
                  IsVerifierPtrWithLabelAndPassword(kFakeLabel, kFakePass)));
}

// Test then failure path with an existing user with an existing password
// auth factor when the asynchronous derivation fails.
TEST_F(AuthSessionWithUssTest, AuthenticatePasswordAuthFactorViaAsyncUssFails) {
  // Setup.
  const ObfuscatedUsername obfuscated_username =
      SanitizeUserName(kFakeUsername);
  const brillo::SecureBlob kFakePerCredentialSecret("fake-vkk");
  // Setting the expectation that the user exists.
  EXPECT_CALL(platform_, DirectoryExists(_)).WillRepeatedly(Return(true));
  // Generating the USS.
  CryptohomeStatusOr<DecryptedUss> uss = DecryptedUss::CreateWithRandomMainKey(
      user_uss_storage_, FileSystemKeyset::CreateRandom());
  ASSERT_THAT(uss, IsOk());
  // Creating the auth factor. An arbitrary auth block state is used in this
  // test.
  AuthFactor auth_factor(
      AuthFactorType::kPassword, kFakeLabel,
      AuthFactorMetadata{.metadata = PasswordMetadata()},
      AuthBlockState{.state = TpmBoundToPcrAuthBlockState()});
  EXPECT_TRUE(
      auth_factor_manager_.SaveAuthFactorFile(obfuscated_username, auth_factor)
          .ok());
  AuthFactorMap auth_factor_map;
  auth_factor_map.Add(std::move(auth_factor),
                      AuthFactorStorageType::kUserSecretStash);
  // Adding the auth factor into the USS and persisting the latter.
  const KeyBlobs key_blobs = {.vkk_key = kFakePerCredentialSecret};
  std::optional<brillo::SecureBlob> wrapping_key =
      key_blobs.DeriveUssCredentialSecret();
  ASSERT_TRUE(wrapping_key.has_value());
  {
    auto transaction = uss->StartTransaction();
    ASSERT_THAT(transaction.InsertWrappedMainKey(kFakeLabel, *wrapping_key),
                IsOk());
    ASSERT_THAT(std::move(transaction).Commit(), IsOk());
  }
  // Creating the auth session.
  AuthSession auth_session({.username = kFakeUsername,
                            .is_ephemeral_user = false,
                            .intent = AuthIntent::kDecrypt,
                            .auth_factor_status_update_timer =
                                std::make_unique<base::WallClockTimer>(),
                            .user_exists = true,
                            .auth_factor_map = std::move(auth_factor_map)},
                           backing_apis_);
  EXPECT_TRUE(auth_session.user_exists());

  // Test.
  // Setting the expectation that the auth block utility will derive key
  // blobs.
  EXPECT_CALL(auth_block_utility_,
              GetAuthBlockTypeFromState(
                  AuthBlockStateTypeIs<TpmBoundToPcrAuthBlockState>()))
      .WillRepeatedly(Return(AuthBlockType::kTpmBoundToPcr));
  EXPECT_CALL(auth_block_utility_, DeriveKeyBlobsWithAuthBlock(
                                       AuthBlockType::kTpmBoundToPcr, _, _, _))
      .WillOnce([this](AuthBlockType auth_block_type,
                       const AuthInput& auth_input,
                       const AuthBlockState& auth_state,
                       AuthBlock::DeriveCallback derive_callback) {
        task_runner_->PostTask(
            FROM_HERE,
            base::BindOnce(
                std::move(derive_callback),
                MakeStatus<CryptohomeCryptoError>(
                    kErrorLocationForTestingAuthSession,
                    error::ErrorActionSet(
                        {error::PossibleAction::kDevCheckUnexpectedState}),
                    CryptoError::CE_OTHER_CRYPTO),
                nullptr, std::nullopt));
      });

  // Calling AuthenticateAuthFactor.
  std::vector<std::string> auth_factor_labels{kFakeLabel};
  user_data_auth::AuthInput auth_input_proto;
  auth_input_proto.mutable_password_input()->set_secret(kFakePass);
  AuthenticateTestFuture authenticate_future;
  SerializedUserAuthFactorTypePolicy auth_factor_type_policy(
      {.type = *SerializeAuthFactorType(
           *DetermineFactorTypeFromAuthInput(auth_input_proto)),
       .enabled_intents = {},
       .disabled_intents = {}});
  auth_session.AuthenticateAuthFactor(
      ToAuthenticateRequest(auth_factor_labels, auth_input_proto),
      auth_factor_type_policy, authenticate_future.GetCallback());

  // Verify.
  auto& [action, status] = authenticate_future.Get();
  EXPECT_EQ(action.action_type, AuthSession::PostAuthActionType::kNone);
  EXPECT_THAT(status, NotOk());
  EXPECT_EQ(status->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED);
  UserSession* user_session = FindOrCreateUserSession(kFakeUsername);
  EXPECT_THAT(user_session->GetCredentialVerifiers(), IsEmpty());
  EXPECT_FALSE(auth_session.has_user_secret_stash());
}

// Test that an existing user with an existing pin auth factor can be
// authenticated.
TEST_F(AuthSessionWithUssTest, AuthenticatePinAuthFactorViaUss) {
  // Setup.
  const ObfuscatedUsername obfuscated_username =
      SanitizeUserName(kFakeUsername);
  const brillo::SecureBlob kFakePerCredentialSecret("fake-vkk");
  // Setting the expectation that the user exists.
  EXPECT_CALL(platform_, DirectoryExists(_)).WillRepeatedly(Return(true));
  // Generating the USS.
  CryptohomeStatusOr<DecryptedUss> uss = DecryptedUss::CreateWithRandomMainKey(
      user_uss_storage_, FileSystemKeyset::CreateRandom());
  ASSERT_THAT(uss, IsOk());
  // Creating the auth factor. An arbitrary auth block state is used in this
  // test.
  AuthFactor auth_factor(AuthFactorType::kPin, kFakePinLabel,
                         AuthFactorMetadata{.metadata = PinMetadata()},
                         AuthBlockState{.state = PinWeaverAuthBlockState()});
  EXPECT_TRUE(
      auth_factor_manager_.SaveAuthFactorFile(obfuscated_username, auth_factor)
          .ok());
  AuthFactorMap auth_factor_map;
  auth_factor_map.Add(std::move(auth_factor),
                      AuthFactorStorageType::kUserSecretStash);
  // Adding the auth factor into the USS and persisting the latter.
  const KeyBlobs key_blobs = {.vkk_key = kFakePerCredentialSecret};
  std::optional<brillo::SecureBlob> wrapping_key =
      key_blobs.DeriveUssCredentialSecret();
  ASSERT_TRUE(wrapping_key.has_value());
  {
    auto transaction = uss->StartTransaction();
    ASSERT_THAT(transaction.InsertWrappedMainKey(kFakePinLabel, *wrapping_key),
                IsOk());
    ASSERT_THAT(std::move(transaction).Commit(), IsOk());
  }
  // Creating the auth session.
  AuthSession auth_session({.username = kFakeUsername,
                            .is_ephemeral_user = false,
                            .intent = AuthIntent::kDecrypt,
                            .auth_factor_status_update_timer =
                                std::make_unique<base::WallClockTimer>(),
                            .user_exists = true,
                            .auth_factor_map = std::move(auth_factor_map)},
                           backing_apis_);
  EXPECT_TRUE(auth_session.user_exists());

  // Test.
  // Setting the expectation that the auth block utility will derive key
  // blobs.
  EXPECT_CALL(auth_block_utility_,
              GetAuthBlockTypeFromState(
                  AuthBlockStateTypeIs<PinWeaverAuthBlockState>()))
      .WillRepeatedly(Return(AuthBlockType::kPinWeaver));
  EXPECT_CALL(auth_block_utility_,
              DeriveKeyBlobsWithAuthBlock(AuthBlockType::kPinWeaver, _, _, _))
      .WillOnce([&kFakePerCredentialSecret](
                    AuthBlockType auth_block_type, const AuthInput& auth_input,
                    const AuthBlockState& auth_state,
                    AuthBlock::DeriveCallback derive_callback) {
        auto key_blobs = std::make_unique<KeyBlobs>();
        key_blobs->vkk_key = kFakePerCredentialSecret;
        std::move(derive_callback)
            .Run(OkStatus<CryptohomeCryptoError>(), std::move(key_blobs),
                 std::nullopt);
      });
  // Calling AuthenticateAuthFactor.
  std::vector<std::string> auth_factor_labels{kFakePinLabel};
  user_data_auth::AuthInput auth_input_proto;
  auth_input_proto.mutable_pin_input()->set_secret(kFakePin);
  AuthenticateTestFuture authenticate_future;
  SerializedUserAuthFactorTypePolicy auth_factor_type_policy(
      {.type = *SerializeAuthFactorType(
           *DetermineFactorTypeFromAuthInput(auth_input_proto)),
       .enabled_intents = {},
       .disabled_intents = {}});
  auth_session.AuthenticateAuthFactor(
      ToAuthenticateRequest(auth_factor_labels, auth_input_proto),
      auth_factor_type_policy, authenticate_future.GetCallback());

  // Verify.
  auto& [action, status] = authenticate_future.Get();
  EXPECT_THAT(status, IsOk());
  EXPECT_EQ(action.action_type, AuthSession::PostAuthActionType::kNone);
  EXPECT_THAT(
      auth_session.authorized_intents(),
      UnorderedElementsAre(AuthIntent::kDecrypt, AuthIntent::kVerifyOnly));
  EXPECT_THAT(auth_session.GetAuthForDecrypt(), NotNull());
  EXPECT_THAT(auth_session.GetAuthForVerifyOnly(), NotNull());
  EXPECT_THAT(auth_session.GetAuthForWebAuthn(), IsNull());
  EXPECT_TRUE(auth_session.has_user_secret_stash());
}

// Test that an existing user with an existing pin auth factor can be
// authenticated and then re-created if the derive suggests it.
TEST_F(AuthSessionWithUssTest, AuthenticatePinAuthFactorViaUssWithRecreate) {
  // Setup.
  const ObfuscatedUsername obfuscated_username =
      SanitizeUserName(kFakeUsername);
  const brillo::SecureBlob kFakePerCredentialSecret("fake-vkk");
  // Setting the expectation that the user exists.
  EXPECT_CALL(platform_, DirectoryExists(_)).WillRepeatedly(Return(true));
  // Generating the USS.
  CryptohomeStatusOr<DecryptedUss> uss = DecryptedUss::CreateWithRandomMainKey(
      user_uss_storage_, FileSystemKeyset::CreateRandom());
  ASSERT_THAT(uss, IsOk());
  // Creating the auth factor. An arbitrary auth block state is used in this
  // test.
  AuthFactor auth_factor(AuthFactorType::kPin, kFakePinLabel,
                         AuthFactorMetadata{.metadata = PinMetadata()},
                         AuthBlockState{.state = PinWeaverAuthBlockState()});
  EXPECT_TRUE(
      auth_factor_manager_.SaveAuthFactorFile(obfuscated_username, auth_factor)
          .ok());
  AuthFactorMap auth_factor_map;
  auth_factor_map.Add(std::move(auth_factor),
                      AuthFactorStorageType::kUserSecretStash);
  // Adding the auth factor into the USS and persisting the latter.
  const KeyBlobs key_blobs = {.vkk_key = kFakePerCredentialSecret};
  std::optional<brillo::SecureBlob> wrapping_key =
      key_blobs.DeriveUssCredentialSecret();
  ASSERT_TRUE(wrapping_key.has_value());
  {
    auto transaction = uss->StartTransaction();
    ASSERT_THAT(transaction.InsertWrappedMainKey(kFakePinLabel, *wrapping_key),
                IsOk());
    ASSERT_THAT(std::move(transaction).Commit(), IsOk());
  }

  // Creating the auth session.
  AuthSession auth_session({.username = kFakeUsername,
                            .is_ephemeral_user = false,
                            .intent = AuthIntent::kDecrypt,
                            .auth_factor_status_update_timer =
                                std::make_unique<base::WallClockTimer>(),
                            .user_exists = true,
                            .auth_factor_map = std::move(auth_factor_map)},
                           backing_apis_);
  EXPECT_TRUE(auth_session.user_exists());

  // Test.
  // Setting the expectation that the auth block utility will derive key
  // blobs, and then that there will be additional calls to re-create
  // them.
  EXPECT_CALL(auth_block_utility_,
              GetAuthBlockTypeFromState(
                  AuthBlockStateTypeIs<PinWeaverAuthBlockState>()))
      .WillRepeatedly(Return(AuthBlockType::kPinWeaver));
  EXPECT_CALL(auth_block_utility_,
              DeriveKeyBlobsWithAuthBlock(AuthBlockType::kPinWeaver, _, _, _))
      .WillOnce([&kFakePerCredentialSecret](
                    AuthBlockType auth_block_type, const AuthInput& auth_input,
                    const AuthBlockState& auth_state,
                    AuthBlock::DeriveCallback derive_callback) {
        auto key_blobs = std::make_unique<KeyBlobs>();
        key_blobs->vkk_key = kFakePerCredentialSecret;
        std::move(derive_callback)
            .Run(OkStatus<CryptohomeCryptoError>(), std::move(key_blobs),
                 AuthBlock::SuggestedAction::kRecreate);
      });
  EXPECT_CALL(auth_block_utility_, SelectAuthBlockTypeForCreation(_))
      .WillRepeatedly(ReturnValue(AuthBlockType::kPinWeaver));
  EXPECT_CALL(auth_block_utility_,
              CreateKeyBlobsWithAuthBlock(AuthBlockType::kPinWeaver, _, _))
      .WillOnce([](AuthBlockType auth_block_type, const AuthInput& auth_input,
                   AuthBlock::CreateCallback create_callback) {
        // Make an arbitrary auth block state type can be used in this
        // test.
        auto key_blobs = std::make_unique<KeyBlobs>();
        key_blobs->vkk_key = brillo::SecureBlob("fake vkk key");
        auto auth_block_state = std::make_unique<AuthBlockState>();
        auth_block_state->state = PinWeaverAuthBlockState();
        std::move(create_callback)
            .Run(OkStatus<CryptohomeCryptoError>(), std::move(key_blobs),
                 std::move(auth_block_state));
      });
  // Calling AuthenticateAuthFactor.
  std::vector<std::string> auth_factor_labels{kFakePinLabel};
  user_data_auth::AuthInput auth_input_proto;
  auth_input_proto.mutable_pin_input()->set_secret(kFakePin);
  AuthenticateTestFuture authenticate_future;
  SerializedUserAuthFactorTypePolicy auth_factor_type_policy(
      {.type = *SerializeAuthFactorType(
           *DetermineFactorTypeFromAuthInput(auth_input_proto)),
       .enabled_intents = {},
       .disabled_intents = {}});
  auth_session.AuthenticateAuthFactor(
      ToAuthenticateRequest(auth_factor_labels, auth_input_proto),
      auth_factor_type_policy, authenticate_future.GetCallback());

  // Verify.
  auto& [action, status] = authenticate_future.Get();
  EXPECT_THAT(status, IsOk());
  EXPECT_EQ(action.action_type, AuthSession::PostAuthActionType::kNone);
  EXPECT_THAT(
      auth_session.authorized_intents(),
      UnorderedElementsAre(AuthIntent::kDecrypt, AuthIntent::kVerifyOnly));
  EXPECT_THAT(auth_session.GetAuthForDecrypt(), NotNull());
  EXPECT_THAT(auth_session.GetAuthForVerifyOnly(), NotNull());
  EXPECT_THAT(auth_session.GetAuthForWebAuthn(), IsNull());
  EXPECT_TRUE(auth_session.has_user_secret_stash());
}

// Test that an existing user with an existing pin auth factor can be
// authenticated and then re-created if the derive suggests it. This test
// verifies that the authenticate still works even if the re-create fails.
TEST_F(AuthSessionWithUssTest,
       AuthenticatePinAuthFactorViaUssWithRecreateThatFails) {
  // Setup.
  const ObfuscatedUsername obfuscated_username =
      SanitizeUserName(kFakeUsername);
  const brillo::SecureBlob kFakePerCredentialSecret("fake-vkk");
  // Setting the expectation that the user exists.
  EXPECT_CALL(platform_, DirectoryExists(_)).WillRepeatedly(Return(true));
  // Generating the USS.
  CryptohomeStatusOr<DecryptedUss> uss = DecryptedUss::CreateWithRandomMainKey(
      user_uss_storage_, FileSystemKeyset::CreateRandom());
  ASSERT_THAT(uss, IsOk());
  // Creating the auth factor. An arbitrary auth block state is used in this
  // test.
  AuthFactor auth_factor(AuthFactorType::kPin, kFakePinLabel,
                         AuthFactorMetadata{.metadata = PinMetadata()},
                         AuthBlockState{.state = PinWeaverAuthBlockState()});
  EXPECT_TRUE(
      auth_factor_manager_.SaveAuthFactorFile(obfuscated_username, auth_factor)
          .ok());
  AuthFactorMap auth_factor_map;
  auth_factor_map.Add(std::move(auth_factor),
                      AuthFactorStorageType::kUserSecretStash);
  // Adding the auth factor into the USS and persisting the latter.
  const KeyBlobs key_blobs = {.vkk_key = kFakePerCredentialSecret};
  std::optional<brillo::SecureBlob> wrapping_key =
      key_blobs.DeriveUssCredentialSecret();
  ASSERT_TRUE(wrapping_key.has_value());
  {
    auto transaction = uss->StartTransaction();
    ASSERT_THAT(transaction.InsertWrappedMainKey(kFakePinLabel, *wrapping_key),
                IsOk());
    ASSERT_THAT(std::move(transaction).Commit(), IsOk());
  }
  // Creating the auth session.
  AuthSession auth_session({.username = kFakeUsername,
                            .is_ephemeral_user = false,
                            .intent = AuthIntent::kDecrypt,
                            .auth_factor_status_update_timer =
                                std::make_unique<base::WallClockTimer>(),
                            .user_exists = true,
                            .auth_factor_map = std::move(auth_factor_map)},
                           backing_apis_);
  EXPECT_TRUE(auth_session.user_exists());

  // Test.
  // Setting the expectation that the auth block utility will derive key
  // blobs, and then that there will be additional calls to re-create
  // them.
  EXPECT_CALL(auth_block_utility_,
              GetAuthBlockTypeFromState(
                  AuthBlockStateTypeIs<PinWeaverAuthBlockState>()))
      .WillRepeatedly(Return(AuthBlockType::kPinWeaver));
  EXPECT_CALL(auth_block_utility_,
              DeriveKeyBlobsWithAuthBlock(AuthBlockType::kPinWeaver, _, _, _))
      .WillOnce([&kFakePerCredentialSecret](
                    AuthBlockType auth_block_type, const AuthInput& auth_input,
                    const AuthBlockState& auth_state,
                    AuthBlock::DeriveCallback derive_callback) {
        auto key_blobs = std::make_unique<KeyBlobs>();
        key_blobs->vkk_key = kFakePerCredentialSecret;
        std::move(derive_callback)
            .Run(OkStatus<CryptohomeCryptoError>(), std::move(key_blobs),
                 AuthBlock::SuggestedAction::kRecreate);
      });
  EXPECT_CALL(auth_block_utility_, SelectAuthBlockTypeForCreation(_))
      .WillRepeatedly([](auto...) -> CryptoStatusOr<AuthBlockType> {
        return MakeStatus<CryptohomeCryptoError>(
            kErrorLocationForTestingAuthSession,
            error::ErrorActionSet(
                {error::PossibleAction::kDevCheckUnexpectedState}),
            CryptoError::CE_OTHER_CRYPTO);
      });
  // Calling AuthenticateAuthFactor.
  std::vector<std::string> auth_factor_labels{kFakePinLabel};
  user_data_auth::AuthInput auth_input_proto;
  auth_input_proto.mutable_pin_input()->set_secret(kFakePin);
  AuthenticateTestFuture authenticate_future;
  SerializedUserAuthFactorTypePolicy auth_factor_type_policy(
      {.type = *SerializeAuthFactorType(
           *DetermineFactorTypeFromAuthInput(auth_input_proto)),
       .enabled_intents = {},
       .disabled_intents = {}});
  auth_session.AuthenticateAuthFactor(
      ToAuthenticateRequest(auth_factor_labels, auth_input_proto),
      auth_factor_type_policy, authenticate_future.GetCallback());

  // Verify.
  auto& [action, status] = authenticate_future.Get();
  EXPECT_THAT(status, IsOk());
  EXPECT_EQ(action.action_type, AuthSession::PostAuthActionType::kNone);
  EXPECT_THAT(
      auth_session.authorized_intents(),
      UnorderedElementsAre(AuthIntent::kDecrypt, AuthIntent::kVerifyOnly));
  EXPECT_THAT(auth_session.GetAuthForDecrypt(), NotNull());
  EXPECT_THAT(auth_session.GetAuthForVerifyOnly(), NotNull());
  EXPECT_THAT(auth_session.GetAuthForWebAuthn(), IsNull());
  EXPECT_TRUE(auth_session.has_user_secret_stash());
}

// Test that if a user gets locked out, the AuthFactorStatusUpdate timer
// is set and called periodically.
TEST_F(AuthSessionTest, AuthFactorStatusUpdateTimerTest) {
  // Setup.
  const ObfuscatedUsername obfuscated_username =
      SanitizeUserName(kFakeUsername);
  const brillo::SecureBlob kFakePerCredentialSecret("fake-vkk");
  // Setting the expectation that the user exists.
  EXPECT_CALL(platform_, DirectoryExists(_)).WillRepeatedly(Return(true));
  // Generating the USS.
  CryptohomeStatusOr<DecryptedUss> uss = DecryptedUss::CreateWithRandomMainKey(
      user_uss_storage_, FileSystemKeyset::CreateRandom());
  ASSERT_THAT(uss, IsOk());
  // Creating the auth factor. An arbitrary auth block state is used in this
  // test.
  AuthFactor auth_factor(
      AuthFactorType::kPin, kFakePinLabel,
      AuthFactorMetadata{.metadata = PinMetadata()},
      AuthBlockState{.state = PinWeaverAuthBlockState{.le_label = 0xbaadf00d}});
  EXPECT_TRUE(
      auth_factor_manager_.SaveAuthFactorFile(obfuscated_username, auth_factor)
          .ok());
  AuthFactorMap auth_factor_map;
  auth_factor_map.Add(std::move(auth_factor),
                      AuthFactorStorageType::kUserSecretStash);
  // Adding the auth factor into the USS and persisting the latter.
  const KeyBlobs key_blobs = {.vkk_key = kFakePerCredentialSecret};
  std::optional<brillo::SecureBlob> wrapping_key =
      key_blobs.DeriveUssCredentialSecret();
  ASSERT_TRUE(wrapping_key.has_value());
  {
    auto transaction = uss->StartTransaction();
    ASSERT_THAT(transaction.InsertWrappedMainKey(kFakePinLabel, *wrapping_key),
                IsOk());
    ASSERT_THAT(std::move(transaction).Commit(), IsOk());
  }
  // Creating the auth session.
  AuthSession auth_session({.username = kFakeUsername,
                            .is_ephemeral_user = false,
                            .intent = AuthIntent::kDecrypt,
                            .auth_factor_status_update_timer =
                                std::make_unique<base::WallClockTimer>(),
                            .user_exists = true,
                            .auth_factor_map = std::move(auth_factor_map)},
                           backing_apis_);
  EXPECT_TRUE(auth_session.user_exists());

  auto mock_le_manager = std::make_unique<MockLECredentialManager>();
  crypto_.set_le_manager_for_testing(std::move(mock_le_manager));
  auth_session.SetAuthFactorStatusUpdateCallback(base::BindRepeating(
      [](user_data_auth::AuthFactorWithStatus, const std::string&) {}));
  // Test.
  // Setting the expectation that the auth block utility will derive key
  // blobs.
  EXPECT_CALL(auth_block_utility_,
              GetAuthBlockTypeFromState(
                  AuthBlockStateTypeIs<PinWeaverAuthBlockState>()))
      .WillRepeatedly(Return(AuthBlockType::kPinWeaver));
  EXPECT_CALL(auth_block_utility_,
              DeriveKeyBlobsWithAuthBlock(AuthBlockType::kPinWeaver, _, _, _))
      .WillOnce([&kFakePerCredentialSecret](
                    AuthBlockType auth_block_type, const AuthInput& auth_input,
                    const AuthBlockState& auth_state,
                    AuthBlock::DeriveCallback derive_callback) {
        auto key_blobs = std::make_unique<KeyBlobs>();
        key_blobs->vkk_key = kFakePerCredentialSecret;
        std::move(derive_callback)
            .Run(MakeStatus<error::CryptohomeCryptoError>(
                     kErrorLocationForTestingAuthSession,
                     error::ErrorActionSet(
                         {error::PrimaryAction::kIncorrectAuth}),
                     CryptoError::CE_CREDENTIAL_LOCKED),
                 nullptr, std::nullopt);
      });
  // Calling AuthenticateAuthFactor.
  std::vector<std::string> auth_factor_labels{kFakePinLabel};
  user_data_auth::AuthInput auth_input_proto;
  // The wrong pin needs to be sent multiple times. |wrong_pin| is set to
  // be different than |kFakePin|.
  std::string wrong_pin = "232323";
  auth_input_proto.mutable_pin_input()->set_secret(wrong_pin);
  EXPECT_CALL(hwsec_pw_manager_, GetDelayInSeconds(_))
      .WillRepeatedly(ReturnValue(UINT32_MAX));
  AuthenticateTestFuture authenticate_future;
  SerializedUserAuthFactorTypePolicy auth_factor_type_policy(
      {.type = *SerializeAuthFactorType(
           *DetermineFactorTypeFromAuthInput(auth_input_proto)),
       .enabled_intents = {},
       .disabled_intents = {}});
  auth_session.AuthenticateAuthFactor(
      ToAuthenticateRequest(auth_factor_labels, auth_input_proto),
      auth_factor_type_policy, authenticate_future.GetCallback());
  auto& [action, status] = authenticate_future.Get();
  EXPECT_EQ(action.action_type, AuthSession::PostAuthActionType::kNone);
  EXPECT_THAT(status, NotOk());
  // As currently the user is locked out until they log in via password,
  // the delay policy does not matter, but once the passwordless policy is
  // set, this timing should change to reflect that.
  task_environment_.FastForwardBy(base::Seconds(30));
}

TEST_F(AuthSessionWithUssTest, AddCryptohomeRecoveryAuthFactor) {
  // Setup.
  AuthSession auth_session({.username = kFakeUsername,
                            .is_ephemeral_user = false,
                            .intent = AuthIntent::kDecrypt,
                            .auth_factor_status_update_timer =
                                std::make_unique<base::WallClockTimer>(),
                            .user_exists = false,
                            .auth_factor_map = AuthFactorMap()},
                           backing_apis_);
  // Creating the user.
  EXPECT_TRUE(auth_session.OnUserCreated().ok());
  EXPECT_TRUE(auth_session.has_user_secret_stash());
  // Setting the expectation that the auth block utility will create key
  // blobs.
  EXPECT_CALL(auth_block_utility_, SelectAuthBlockTypeForCreation(_))
      .WillRepeatedly(ReturnValue(AuthBlockType::kCryptohomeRecovery));
  EXPECT_CALL(
      auth_block_utility_,
      CreateKeyBlobsWithAuthBlock(AuthBlockType::kCryptohomeRecovery, _, _))
      .WillOnce([](AuthBlockType auth_block_type, const AuthInput& auth_input,
                   AuthBlock::CreateCallback create_callback) {
        // Make an arbitrary auth block state type can be used in this
        // test.
        auto key_blobs = std::make_unique<KeyBlobs>();
        key_blobs->vkk_key = brillo::SecureBlob("fake vkk key");
        auto auth_block_state = std::make_unique<AuthBlockState>();
        auth_block_state->state = CryptohomeRecoveryAuthBlockState();
        std::move(create_callback)
            .Run(OkStatus<CryptohomeCryptoError>(), std::move(key_blobs),
                 std::move(auth_block_state));
      });
  // Calling AddAuthFactor.
  user_data_auth::AddAuthFactorRequest request;
  request.set_auth_session_id(auth_session.serialized_token());
  request.mutable_auth_factor()->set_type(
      user_data_auth::AUTH_FACTOR_TYPE_CRYPTOHOME_RECOVERY);
  request.mutable_auth_factor()->set_label(kFakeLabel);
  request.mutable_auth_factor()->mutable_cryptohome_recovery_metadata();
  request.mutable_auth_input()
      ->mutable_cryptohome_recovery_input()
      ->set_mediator_pub_key("mediator pub key");
  // Test and Verify.
  TestFuture<CryptohomeStatus> add_future;
  auth_session.GetAuthForDecrypt()->AddAuthFactor(request,
                                                  add_future.GetCallback());

  // Verify.
  EXPECT_THAT(add_future.Get(), IsOk());
  std::map<std::string, AuthFactorType> stored_factors =
      auth_factor_manager_.ListAuthFactors(SanitizeUserName(kFakeUsername));
  EXPECT_THAT(
      stored_factors,
      ElementsAre(Pair(kFakeLabel, AuthFactorType::kCryptohomeRecovery)));
  // There should be no verifier for the recovery factor.
  UserSession* user_session = FindOrCreateUserSession(kFakeUsername);
  EXPECT_THAT(user_session->GetCredentialVerifiers(), IsEmpty());
}

TEST_F(AuthSessionWithUssTest, AuthenticateCryptohomeRecoveryAuthFactor) {
  // Setup.
  const ObfuscatedUsername obfuscated_username =
      SanitizeUserName(kFakeUsername);
  const brillo::SecureBlob kFakePerCredentialSecret("fake-vkk");
  // Setting the expectation that the user exists.
  EXPECT_CALL(platform_, DirectoryExists(_)).WillRepeatedly(Return(true));
  // Generating the USS.
  CryptohomeStatusOr<DecryptedUss> uss = DecryptedUss::CreateWithRandomMainKey(
      user_uss_storage_, FileSystemKeyset::CreateRandom());
  ASSERT_THAT(uss, IsOk());
  // Creating the auth factor.
  AuthFactor auth_factor(
      AuthFactorType::kCryptohomeRecovery, kFakeLabel,
      AuthFactorMetadata{.metadata = CryptohomeRecoveryMetadata()},
      AuthBlockState{.state = CryptohomeRecoveryAuthBlockState()});
  EXPECT_TRUE(
      auth_factor_manager_.SaveAuthFactorFile(obfuscated_username, auth_factor)
          .ok());
  AuthFactorMap auth_factor_map;
  auth_factor_map.Add(std::move(auth_factor),
                      AuthFactorStorageType::kUserSecretStash);

  // Adding the auth factor into the USS and persisting the latter.
  const KeyBlobs key_blobs = {.vkk_key = kFakePerCredentialSecret};
  std::optional<brillo::SecureBlob> wrapping_key =
      key_blobs.DeriveUssCredentialSecret();
  ASSERT_TRUE(wrapping_key.has_value());
  {
    auto transaction = uss->StartTransaction();
    ASSERT_THAT(transaction.InsertWrappedMainKey(kFakeLabel, *wrapping_key),
                IsOk());
    ASSERT_THAT(std::move(transaction).Commit(), IsOk());
  }
  // Creating the auth session.
  AuthSession auth_session({.username = kFakeUsername,
                            .is_ephemeral_user = false,
                            .intent = AuthIntent::kDecrypt,
                            .auth_factor_status_update_timer =
                                std::make_unique<base::WallClockTimer>(),
                            .user_exists = true,
                            .auth_factor_map = std::move(auth_factor_map)},
                           backing_apis_);
  EXPECT_TRUE(auth_session.user_exists());

  // Test.
  // Setting the expectation that the auth block utility will generate
  // recovery request.
  EXPECT_CALL(auth_block_utility_, GenerateRecoveryRequest(_, _, _, _, _, _, _))
      .WillOnce([](const ObfuscatedUsername& obfuscated_username,
                   const cryptorecovery::RequestMetadata& request_metadata,
                   const brillo::Blob& epoch_response,
                   const CryptohomeRecoveryAuthBlockState& state,
                   const hwsec::RecoveryCryptoFrontend* recovery_hwsec,
                   brillo::SecureBlob* out_recovery_request,
                   brillo::SecureBlob* out_ephemeral_pub_key) {
        *out_ephemeral_pub_key = brillo::SecureBlob("test");
        return OkStatus<CryptohomeCryptoError>();
      });
  EXPECT_FALSE(auth_session.has_user_secret_stash());

  // Calling GetRecoveryRequest.
  user_data_auth::GetRecoveryRequestRequest request;
  request.set_auth_session_id(auth_session.serialized_token());
  request.set_auth_factor_label(kFakeLabel);
  TestFuture<user_data_auth::GetRecoveryRequestReply> reply_future;
  auth_session.GetRecoveryRequest(
      request,
      reply_future
          .GetCallback<const user_data_auth::GetRecoveryRequestReply&>());

  // Verify.
  EXPECT_EQ(reply_future.Get().error(),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  EXPECT_THAT(auth_session.authorized_intents(), IsEmpty());
  EXPECT_THAT(auth_session.GetAuthForDecrypt(), IsNull());
  EXPECT_THAT(auth_session.GetAuthForVerifyOnly(), IsNull());
  EXPECT_THAT(auth_session.GetAuthForWebAuthn(), IsNull());

  // Test.
  // Setting the expectation that the auth block utility will derive key
  // blobs.
  EXPECT_CALL(auth_block_utility_,
              GetAuthBlockTypeFromState(
                  AuthBlockStateTypeIs<CryptohomeRecoveryAuthBlockState>()))
      .WillRepeatedly(Return(AuthBlockType::kCryptohomeRecovery));
  EXPECT_CALL(
      auth_block_utility_,
      DeriveKeyBlobsWithAuthBlock(AuthBlockType::kCryptohomeRecovery, _, _, _))
      .WillOnce([&kFakePerCredentialSecret](
                    AuthBlockType auth_block_type, const AuthInput& auth_input,
                    const AuthBlockState& auth_state,
                    AuthBlock::DeriveCallback derive_callback) {
        EXPECT_THAT(
            auth_input.cryptohome_recovery_auth_input->ephemeral_pub_key,
            Optional(brillo::SecureBlob("test")));
        auto key_blobs = std::make_unique<KeyBlobs>();
        key_blobs->vkk_key = kFakePerCredentialSecret;
        std::move(derive_callback)
            .Run(OkStatus<CryptohomeCryptoError>(), std::move(key_blobs),
                 std::nullopt);
      });

  // Calling AuthenticateAuthFactor.
  std::vector<std::string> auth_factor_labels{kFakeLabel};
  user_data_auth::AuthInput auth_input_proto;
  auth_input_proto.mutable_cryptohome_recovery_input()
      ->mutable_recovery_response();
  AuthenticateTestFuture authenticate_future;
  SerializedUserAuthFactorTypePolicy auth_factor_type_policy(
      {.type = *SerializeAuthFactorType(
           *DetermineFactorTypeFromAuthInput(auth_input_proto)),
       .enabled_intents = {},
       .disabled_intents = {}});
  auth_session.AuthenticateAuthFactor(
      ToAuthenticateRequest(auth_factor_labels, auth_input_proto),
      auth_factor_type_policy, authenticate_future.GetCallback());

  // Verify.
  auto& [action, status] = authenticate_future.Get();
  EXPECT_THAT(status, IsOk());
  EXPECT_EQ(action.action_type, AuthSession::PostAuthActionType::kNone);
  EXPECT_THAT(
      auth_session.authorized_intents(),
      UnorderedElementsAre(AuthIntent::kDecrypt, AuthIntent::kVerifyOnly));
  EXPECT_THAT(auth_session.GetAuthForDecrypt(), NotNull());
  EXPECT_THAT(auth_session.GetAuthForVerifyOnly(), NotNull());
  EXPECT_THAT(auth_session.GetAuthForWebAuthn(), IsNull());
  EXPECT_TRUE(auth_session.has_user_secret_stash());
  // There should be no verifier created for the recovery factor.
  UserSession* user_session = FindOrCreateUserSession(kFakeUsername);
  EXPECT_THAT(user_session->GetCredentialVerifiers(), IsEmpty());
}

// Test scenario where we add a Smart Card/Challenge Response credential,
// and go through the authentication flow twice. On the second
// authentication, AuthSession should use the lightweight verify check.
TEST_F(AuthSessionWithUssTest, AuthenticateSmartCardAuthFactor) {
  // Setup.
  brillo::Blob public_key_spki_der = brillo::BlobFromString("public_key");
  const ObfuscatedUsername obfuscated_username =
      SanitizeUserName(kFakeUsername);
  const brillo::SecureBlob kFakePerCredentialSecret("fake-vkk");
  // Setting the expectation that the user exists.
  EXPECT_CALL(platform_, DirectoryExists(_)).WillRepeatedly(Return(true));
  // Generating the USS.
  CryptohomeStatusOr<DecryptedUss> uss = DecryptedUss::CreateWithRandomMainKey(
      user_uss_storage_, FileSystemKeyset::CreateRandom());
  ASSERT_THAT(uss, IsOk());
  // Creating the auth factor.
  AuthFactor auth_factor(
      AuthFactorType::kSmartCard, kFakeLabel,
      AuthFactorMetadata{
          .metadata =
              SmartCardMetadata{.public_key_spki_der = public_key_spki_der}},
      AuthBlockState{.state = ChallengeCredentialAuthBlockState()});
  EXPECT_TRUE(
      auth_factor_manager_.SaveAuthFactorFile(obfuscated_username, auth_factor)
          .ok());
  AuthFactorMap auth_factor_map;
  auth_factor_map.Add(auth_factor, AuthFactorStorageType::kUserSecretStash);
  // Adding the auth factor into the USS and persisting the latter.
  const KeyBlobs key_blobs = {.vkk_key = kFakePerCredentialSecret};
  std::optional<brillo::SecureBlob> wrapping_key =
      key_blobs.DeriveUssCredentialSecret();
  ASSERT_TRUE(wrapping_key.has_value());
  {
    auto transaction = uss->StartTransaction();
    ASSERT_THAT(transaction.InsertWrappedMainKey(kFakeLabel, *wrapping_key),
                IsOk());
    ASSERT_THAT(std::move(transaction).Commit(), IsOk());
  }
  // Creating the auth session.
  AuthSession auth_session({.username = kFakeUsername,
                            .is_ephemeral_user = false,
                            .intent = AuthIntent::kDecrypt,
                            .auth_factor_status_update_timer =
                                std::make_unique<base::WallClockTimer>(),
                            .user_exists = true,
                            .auth_factor_map = std::move(auth_factor_map)},
                           backing_apis_);
  EXPECT_TRUE(auth_session.user_exists());
  EXPECT_FALSE(auth_session.has_user_secret_stash());

  // Verify.
  EXPECT_THAT(auth_session.authorized_intents(), IsEmpty());
  EXPECT_THAT(auth_session.GetAuthForDecrypt(), IsNull());
  EXPECT_THAT(auth_session.GetAuthForVerifyOnly(), IsNull());
  EXPECT_THAT(auth_session.GetAuthForWebAuthn(), IsNull());

  // Test.
  // Setting the expectation that the auth block utility will derive key
  // blobs.
  EXPECT_CALL(auth_block_utility_,
              GetAuthBlockTypeFromState(
                  AuthBlockStateTypeIs<ChallengeCredentialAuthBlockState>()))
      .WillRepeatedly(Return(AuthBlockType::kChallengeCredential));
  EXPECT_CALL(
      auth_block_utility_,
      DeriveKeyBlobsWithAuthBlock(AuthBlockType::kChallengeCredential, _, _, _))
      .WillOnce([&kFakePerCredentialSecret](
                    AuthBlockType auth_block_type, const AuthInput& auth_input,
                    const AuthBlockState& auth_state,
                    AuthBlock::DeriveCallback derive_callback) {
        auto key_blobs = std::make_unique<KeyBlobs>();
        key_blobs->vkk_key = kFakePerCredentialSecret;
        std::move(derive_callback)
            .Run(OkStatus<CryptohomeCryptoError>(), std::move(key_blobs),
                 std::nullopt);
      });

  // Calling AuthenticateAuthFactor.
  std::vector<std::string> auth_factor_labels{kFakeLabel};
  user_data_auth::AuthInput auth_input_proto;
  auth_input_proto.mutable_smart_card_input()->add_signature_algorithms(
      user_data_auth::CHALLENGE_RSASSA_PKCS1_V1_5_SHA256);
  auth_input_proto.mutable_smart_card_input()
      ->set_key_delegate_dbus_service_name("test_cc_dbus");
  AuthenticateTestFuture authenticate_future;
  SerializedUserAuthFactorTypePolicy auth_factor_type_policy(
      {.type = *SerializeAuthFactorType(
           *DetermineFactorTypeFromAuthInput(auth_input_proto)),
       .enabled_intents = {},
       .disabled_intents = {}});
  auth_session.AuthenticateAuthFactor(
      ToAuthenticateRequest(auth_factor_labels, auth_input_proto),
      auth_factor_type_policy, authenticate_future.GetCallback());

  // Verify.
  auto& [action, status] = authenticate_future.Get();
  EXPECT_THAT(status, IsOk());
  EXPECT_EQ(action.action_type, AuthSession::PostAuthActionType::kNone);
  EXPECT_THAT(
      auth_session.authorized_intents(),
      UnorderedElementsAre(AuthIntent::kDecrypt, AuthIntent::kVerifyOnly));
  EXPECT_THAT(auth_session.GetAuthForDecrypt(), NotNull());
  EXPECT_THAT(auth_session.GetAuthForVerifyOnly(), NotNull());
  EXPECT_THAT(auth_session.GetAuthForWebAuthn(), IsNull());
  EXPECT_TRUE(auth_session.has_user_secret_stash());

  // There should be a verifier created for the smart card factor.
  UserSession* user_session = FindOrCreateUserSession(kFakeUsername);
  EXPECT_THAT(user_session->GetCredentialVerifiers(),
              UnorderedElementsAre(IsVerifierPtrWithLabel(kFakeLabel)));

  AuthFactorMap verify_auth_factor_map;
  auth_factor_map.Add(auth_factor, AuthFactorStorageType::kUserSecretStash);
  AuthSession verify_auth_session(
      {.username = kFakeUsername,
       .is_ephemeral_user = false,
       .intent = AuthIntent::kVerifyOnly,
       .auth_factor_status_update_timer =
           std::make_unique<base::WallClockTimer>(),
       .user_exists = true,
       .auth_factor_map = std::move(verify_auth_factor_map)},
      backing_apis_);

  // Simulate a successful key verification.
  EXPECT_CALL(challenge_credentials_helper_, VerifyKey(_, _, _, _))
      .WillOnce(ReplyToVerifyKey{/*is_key_valid=*/true});

  // Call AuthenticateAuthFactor again.
  AuthenticateTestFuture verify_authenticate_future;
  auth_factor_type_policy = SerializedUserAuthFactorTypePolicy(
      {.type = *SerializeAuthFactorType(
           *DetermineFactorTypeFromAuthInput(auth_input_proto)),
       .enabled_intents = {},
       .disabled_intents = {}});
  verify_auth_session.AuthenticateAuthFactor(
      ToAuthenticateRequest(auth_factor_labels, auth_input_proto),
      auth_factor_type_policy, verify_authenticate_future.GetCallback());
  EXPECT_THAT(verify_auth_session.authorized_intents(),
              UnorderedElementsAre(AuthIntent::kVerifyOnly));
}

// Test that AuthenticateAuthFactor succeeds for the
// `AuthIntent::kVerifyOnly` scenario, using a credential verifier.
TEST_F(AuthSessionWithUssTest, LightweightPasswordAuthentication) {
  // Setup.
  // Add the user session along with a verifier that's configured to pass.
  auto user_session = std::make_unique<MockUserSession>();
  EXPECT_CALL(*user_session, VerifyUser(SanitizeUserName(kFakeUsername)))
      .WillOnce(Return(true));
  auto verifier = std::make_unique<MockCredentialVerifier>(
      AuthFactorType::kPassword, kFakeLabel,
      AuthFactorMetadata{.metadata = PasswordMetadata()});
  EXPECT_CALL(*verifier, VerifySync(_)).WillOnce(ReturnOk<CryptohomeError>());
  user_session->AddCredentialVerifier(std::move(verifier));
  EXPECT_TRUE(user_session_map_.Add(kFakeUsername, std::move(user_session)));
  // Create an AuthSession with a fake factor. No authentication mocks are
  // set up, because the lightweight authentication should be used in the
  // test.
  AuthSession auth_session(
      {.username = kFakeUsername,
       .is_ephemeral_user = false,
       .intent = AuthIntent::kVerifyOnly,
       .auth_factor_status_update_timer =
           std::make_unique<base::WallClockTimer>(),
       .user_exists = true,
       .auth_factor_map =
           AfMapBuilder().AddPassword<void>(kFakeLabel).Consume()},
      backing_apis_);

  // Test.
  std::vector<std::string> auth_factor_labels{kFakeLabel};
  user_data_auth::AuthInput auth_input_proto;
  auth_input_proto.mutable_password_input()->set_secret(kFakePass);
  AuthenticateTestFuture authenticate_future;
  SerializedUserAuthFactorTypePolicy auth_factor_type_policy(
      {.type = *SerializeAuthFactorType(
           *DetermineFactorTypeFromAuthInput(auth_input_proto)),
       .enabled_intents = {},
       .disabled_intents = {}});
  auth_session.AuthenticateAuthFactor(
      ToAuthenticateRequest(auth_factor_labels, auth_input_proto),
      auth_factor_type_policy, authenticate_future.GetCallback());

  // Verify.
  auto& [action, status] = authenticate_future.Get();
  EXPECT_THAT(status, IsOk());
  EXPECT_EQ(action.action_type, AuthSession::PostAuthActionType::kNone);
  EXPECT_THAT(auth_session.authorized_intents(),
              UnorderedElementsAre(AuthIntent::kVerifyOnly));
}

// Test that if there is a credential to reset, after a lightweight auth,
// a post action requesting repeating full auth should be returned.
TEST_F(AuthSessionWithUssTest, LightweightPasswordPostAction) {
  // Setup.
  const ObfuscatedUsername obfuscated_username =
      SanitizeUserName(kFakeUsername);
  const brillo::SecureBlob kFakePerCredentialSecret("fake-vkk");
  // Setting the expectation that the user exists.
  EXPECT_CALL(platform_, DirectoryExists(_)).WillRepeatedly(Return(true));
  // Generating the USS.
  CryptohomeStatusOr<DecryptedUss> uss = DecryptedUss::CreateWithRandomMainKey(
      user_uss_storage_, FileSystemKeyset::CreateRandom());
  ASSERT_THAT(uss, IsOk());
  // Creating the auth factor. An arbitrary auth block state is used in this
  // test.
  AuthFactor auth_factor(
      AuthFactorType::kPassword, kFakeLabel,
      AuthFactorMetadata{.metadata = PasswordMetadata()},
      AuthBlockState{.state = TpmBoundToPcrAuthBlockState()});
  EXPECT_TRUE(
      auth_factor_manager_.SaveAuthFactorFile(obfuscated_username, auth_factor)
          .ok());
  AuthFactorMap auth_factor_map;
  auth_factor_map.Add(std::move(auth_factor),
                      AuthFactorStorageType::kUserSecretStash);
  // Adding the auth factor into the USS and persisting the latter.
  const KeyBlobs key_blobs = {.vkk_key = kFakePerCredentialSecret};
  std::optional<brillo::SecureBlob> wrapping_key =
      key_blobs.DeriveUssCredentialSecret();
  ASSERT_TRUE(wrapping_key.has_value());
  {
    auto transaction = uss->StartTransaction();
    ASSERT_THAT(transaction.InsertWrappedMainKey(kFakeLabel, *wrapping_key),
                IsOk());
    // Add a rate-limiter so that later on a reset is needed after full auth.
    ASSERT_THAT(
        transaction.InitializeFingerprintRateLimiterId(kFakeRateLimiterLabel),
        IsOk());
    ASSERT_THAT(std::move(transaction).Commit(), IsOk());
  }
  // Setup the credential verifier.
  auto user_session = std::make_unique<MockUserSession>();
  EXPECT_CALL(*user_session, VerifyUser(SanitizeUserName(kFakeUsername)))
      .WillRepeatedly(Return(true));
  auto verifier = std::make_unique<MockCredentialVerifier>(
      AuthFactorType::kPassword, kFakeLabel,
      AuthFactorMetadata{.metadata = PasswordMetadata()});
  EXPECT_CALL(*verifier, VerifySync(_)).WillOnce(ReturnOk<CryptohomeError>());
  user_session->AddCredentialVerifier(std::move(verifier));
  EXPECT_TRUE(user_session_map_.Add(kFakeUsername, std::move(user_session)));
  AuthSession auth_session({.username = kFakeUsername,
                            .is_ephemeral_user = false,
                            .intent = AuthIntent::kVerifyOnly,
                            .auth_factor_status_update_timer =
                                std::make_unique<base::WallClockTimer>(),
                            .user_exists = true,
                            .auth_factor_map = std::move(auth_factor_map)},
                           backing_apis_);

  // Expectations for the full auth.
  EXPECT_CALL(auth_block_utility_,
              GetAuthBlockTypeFromState(
                  AuthBlockStateTypeIs<TpmBoundToPcrAuthBlockState>()))
      .WillRepeatedly(Return(AuthBlockType::kTpmBoundToPcr));
  EXPECT_CALL(auth_block_utility_, DeriveKeyBlobsWithAuthBlock(
                                       AuthBlockType::kTpmBoundToPcr, _, _, _))
      .WillOnce([this, &kFakePerCredentialSecret](
                    AuthBlockType auth_block_type, const AuthInput& auth_input,
                    const AuthBlockState& auth_state,
                    AuthBlock::DeriveCallback derive_callback) {
        auto key_blobs = std::make_unique<KeyBlobs>();
        key_blobs->vkk_key = kFakePerCredentialSecret;
        task_runner_->PostTask(
            FROM_HERE, base::BindOnce(std::move(derive_callback),
                                      OkStatus<CryptohomeCryptoError>(),
                                      std::move(key_blobs), std::nullopt));
      });

  // Test.
  std::vector<std::string> auth_factor_labels{kFakeLabel};
  user_data_auth::AuthInput auth_input_proto;
  auth_input_proto.mutable_password_input()->set_secret(kFakePass);
  AuthenticateTestFuture authenticate_future;
  SerializedUserAuthFactorTypePolicy auth_factor_type_policy(
      {.type = *SerializeAuthFactorType(
           *DetermineFactorTypeFromAuthInput(auth_input_proto)),
       .enabled_intents = {},
       .disabled_intents = {}});
  auth_session.AuthenticateAuthFactor(
      ToAuthenticateRequest(auth_factor_labels, auth_input_proto),
      auth_factor_type_policy, authenticate_future.GetCallback());

  // Verify.
  auto& [action, status] = authenticate_future.Get();
  EXPECT_THAT(status, IsOk());
  EXPECT_EQ(action.action_type, AuthSession::PostAuthActionType::kRepeat);
  ASSERT_TRUE(action.repeat_request.has_value());
  EXPECT_EQ(action.repeat_request->flags.force_full_auth,
            AuthSession::ForceFullAuthFlag::kForce);
  EXPECT_THAT(auth_session.authorized_intents(),
              UnorderedElementsAre(AuthIntent::kVerifyOnly));

  // Test and verify with repeat request.
  AuthenticateTestFuture second_authenticate_future;
  auth_session.AuthenticateAuthFactor(action.repeat_request.value(),
                                      auth_factor_type_policy,
                                      second_authenticate_future.GetCallback());
  auto& [second_action, second_status] = second_authenticate_future.Get();
  EXPECT_THAT(second_status, IsOk());
  EXPECT_EQ(second_action.action_type, AuthSession::PostAuthActionType::kNone);
}

// Test that AuthenticateAuthFactor succeeds for the
// `AuthIntent::kVerifyOnly` scenario, using the legacy fingerprint.
TEST_F(AuthSessionWithUssTest, LightweightFingerprintAuthentication) {
  // Setup.
  // Add the user session. Configure the credential verifier mock to
  // succeed.
  auto user_session = std::make_unique<MockUserSession>();
  EXPECT_CALL(*user_session, VerifyUser(SanitizeUserName(kFakeUsername)))
      .WillOnce(Return(true));
  auto verifier = std::make_unique<MockCredentialVerifier>(
      AuthFactorType::kLegacyFingerprint, "", AuthFactorMetadata{});
  EXPECT_CALL(*verifier, VerifySync(_)).WillOnce(ReturnOk<CryptohomeError>());
  user_session->AddCredentialVerifier(std::move(verifier));
  EXPECT_TRUE(user_session_map_.Add(kFakeUsername, std::move(user_session)));
  // Create an AuthSession with no factors. No authentication mocks are
  // set up, because the lightweight authentication should be used in the
  // test.
  AuthSession auth_session(
      AuthSession::Params{.username = kFakeUsername,
                          .is_ephemeral_user = false,
                          .intent = AuthIntent::kVerifyOnly,
                          .auth_factor_status_update_timer =
                              std::make_unique<base::WallClockTimer>(),
                          .user_exists = true,
                          .auth_factor_map = AuthFactorMap()},
      backing_apis_);

  // Test.
  user_data_auth::AuthInput auth_input_proto;
  auth_input_proto.mutable_legacy_fingerprint_input();
  AuthenticateTestFuture authenticate_future;
  SerializedUserAuthFactorTypePolicy auth_factor_type_policy(
      {.type = SerializedAuthFactorType::kLegacyFingerprint,
       .enabled_intents = {SerializedAuthIntent::kVerifyOnly},
       .disabled_intents = {}});
  auth_session.AuthenticateAuthFactor(
      ToAuthenticateRequest({}, auth_input_proto), auth_factor_type_policy,
      authenticate_future.GetCallback());

  // Verify.
  auto& [action, status] = authenticate_future.Get();
  EXPECT_THAT(status, IsOk());
  EXPECT_EQ(action.action_type, AuthSession::PostAuthActionType::kNone);
  EXPECT_THAT(auth_session.authorized_intents(),
              UnorderedElementsAre(AuthIntent::kVerifyOnly));
}

// Test that PrepareAuthFactor succeeds for fingerprint with the purpose
// of authentication.
TEST_F(AuthSessionWithUssTest, PrepareLegacyFingerprintAuth) {
  // Add the user session. Configure the credential verifier mock to
  // succeed.
  auto user_session = std::make_unique<MockUserSession>();
  auto auth_session = std::make_unique<AuthSession>(
      AuthSession::Params{.username = kFakeUsername,
                          .is_ephemeral_user = false,
                          .intent = AuthIntent::kVerifyOnly,
                          .auth_factor_status_update_timer =
                              std::make_unique<base::WallClockTimer>(),
                          .user_exists = true,
                          .auth_factor_map = AuthFactorMap()},
      backing_apis_);
  EXPECT_CALL(*bio_processor_,
              StartAuthenticateSession(auth_session->obfuscated_username(), _))
      .WillOnce([](auto&&, auto&& callback) { std::move(callback).Run(true); });

  // Test.
  TestFuture<CryptohomeStatus> prepare_future;
  user_data_auth::PrepareAuthFactorRequest request;
  request.set_auth_session_id(auth_session->serialized_token());
  request.set_auth_factor_type(user_data_auth::AUTH_FACTOR_TYPE_FINGERPRINT);
  request.set_purpose(user_data_auth::PURPOSE_AUTHENTICATE_AUTH_FACTOR);
  auth_session->PrepareAuthFactor(request, prepare_future.GetCallback());
  auth_session.reset();

  // Verify.
  ASSERT_THAT(prepare_future.Get(), IsOk());
}

// Test that PrepareAuthFactor succeeded for password.
TEST_F(AuthSessionWithUssTest, PreparePasswordFailure) {
  // Setup.
  // Add the user session. Configure the credential verifier mock to
  // succeed.
  auto user_session = std::make_unique<MockUserSession>();
  AuthSession auth_session({.username = kFakeUsername,
                            .is_ephemeral_user = false,
                            .intent = AuthIntent::kVerifyOnly,
                            .auth_factor_status_update_timer =
                                std::make_unique<base::WallClockTimer>(),
                            .user_exists = true,
                            .auth_factor_map = AuthFactorMap()},
                           backing_apis_);

  // Test.
  user_data_auth::PrepareAuthFactorRequest request;
  request.set_auth_session_id(auth_session.serialized_token());
  request.set_auth_factor_type(user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
  request.set_purpose(user_data_auth::PURPOSE_AUTHENTICATE_AUTH_FACTOR);
  TestFuture<CryptohomeStatus> prepare_future;
  auth_session.PrepareAuthFactor(request, prepare_future.GetCallback());

  // Verify.
  ASSERT_EQ(prepare_future.Get()->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
}

TEST_F(AuthSessionWithUssTest, TerminateAuthFactorBadTypeFailure) {
  // Setup.
  // Add the user session. Configure the credential verifier mock to
  // succeed.
  auto user_session = std::make_unique<MockUserSession>();
  AuthSession auth_session({.username = kFakeUsername,
                            .is_ephemeral_user = false,
                            .intent = AuthIntent::kVerifyOnly,
                            .auth_factor_status_update_timer =
                                std::make_unique<base::WallClockTimer>(),
                            .user_exists = true,
                            .auth_factor_map = AuthFactorMap()},
                           backing_apis_);

  // Test.
  user_data_auth::TerminateAuthFactorRequest request;
  request.set_auth_session_id(auth_session.serialized_token());
  request.set_auth_factor_type(user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
  TestFuture<CryptohomeStatus> terminate_future;
  auth_session.TerminateAuthFactor(request, terminate_future.GetCallback());

  // Verify.
  ASSERT_EQ(terminate_future.Get()->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
}

TEST_F(AuthSessionWithUssTest, TerminateAuthFactorInactiveFactorFailure) {
  // Setup.
  // Add the user session. Configure the credential verifier mock to
  // succeed.
  auto user_session = std::make_unique<MockUserSession>();
  AuthSession auth_session({.username = kFakeUsername,
                            .is_ephemeral_user = false,
                            .intent = AuthIntent::kVerifyOnly,
                            .auth_factor_status_update_timer =
                                std::make_unique<base::WallClockTimer>(),
                            .user_exists = true,
                            .auth_factor_map = AuthFactorMap()},
                           backing_apis_);

  // Test.
  user_data_auth::TerminateAuthFactorRequest request;
  request.set_auth_session_id(auth_session.serialized_token());
  request.set_auth_factor_type(user_data_auth::AUTH_FACTOR_TYPE_FINGERPRINT);
  TestFuture<CryptohomeStatus> terminate_future;
  auth_session.TerminateAuthFactor(request, terminate_future.GetCallback());

  // Verify.
  ASSERT_EQ(terminate_future.Get()->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
}

TEST_F(AuthSessionWithUssTest, TerminateAuthFactorLegacyFingerprintSuccess) {
  // Setup.
  // Add the user session. Configure the credential verifier mock to
  // succeed.
  auto user_session = std::make_unique<MockUserSession>();
  AuthSession auth_session({.username = kFakeUsername,
                            .is_ephemeral_user = false,
                            .intent = AuthIntent::kVerifyOnly,
                            .auth_factor_status_update_timer =
                                std::make_unique<base::WallClockTimer>(),
                            .user_exists = true,
                            .auth_factor_map = AuthFactorMap()},
                           backing_apis_);
  EXPECT_CALL(*bio_processor_,
              StartAuthenticateSession(auth_session.obfuscated_username(), _))
      .WillOnce([](auto&&, auto&& callback) { std::move(callback).Run(true); });
  TestFuture<CryptohomeStatus> prepare_future;
  user_data_auth::PrepareAuthFactorRequest prepare_request;
  prepare_request.set_auth_session_id(auth_session.serialized_token());
  prepare_request.set_auth_factor_type(
      user_data_auth::AUTH_FACTOR_TYPE_FINGERPRINT);
  prepare_request.set_purpose(user_data_auth::PURPOSE_AUTHENTICATE_AUTH_FACTOR);
  auth_session.PrepareAuthFactor(prepare_request, prepare_future.GetCallback());
  ASSERT_THAT(prepare_future.Get(), IsOk());

  // Test.
  user_data_auth::TerminateAuthFactorRequest request;
  request.set_auth_session_id(auth_session.serialized_token());
  request.set_auth_factor_type(user_data_auth::AUTH_FACTOR_TYPE_FINGERPRINT);
  TestFuture<CryptohomeStatus> terminate_future;
  auth_session.TerminateAuthFactor(request, terminate_future.GetCallback());

  // Verify.
  ASSERT_THAT(terminate_future.Get(), IsOk());
}

TEST_F(AuthSessionWithUssTest, RemoveAuthFactor) {
  // Setup.
  AuthSession auth_session({.username = kFakeUsername,
                            .is_ephemeral_user = false,
                            .intent = AuthIntent::kDecrypt,
                            .auth_factor_status_update_timer =
                                std::make_unique<base::WallClockTimer>(),
                            .user_exists = false,
                            .auth_factor_map = AuthFactorMap()},
                           backing_apis_);
  // Creating the user.
  EXPECT_TRUE(auth_session.OnUserCreated().ok());
  EXPECT_TRUE(auth_session.has_user_secret_stash());

  user_data_auth::CryptohomeErrorCode error =
      user_data_auth::CRYPTOHOME_ERROR_NOT_SET;

  error = AddPasswordAuthFactor(kFakeLabel, kFakePass,
                                /*first_factor=*/true, auth_session);
  EXPECT_EQ(error, user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  error = AddPinAuthFactor(kFakePin, auth_session);
  EXPECT_EQ(error, user_data_auth::CRYPTOHOME_ERROR_NOT_SET);

  // Both password and pin are available.
  std::map<std::string, AuthFactorType> stored_factors =
      auth_factor_manager_.ListAuthFactors(SanitizeUserName(kFakeUsername));
  EXPECT_THAT(stored_factors,
              ElementsAre(Pair(kFakeLabel, AuthFactorType::kPassword),
                          Pair(kFakePinLabel, AuthFactorType::kPin)));
  EXPECT_THAT(auth_session.auth_factor_map().Find(kFakeLabel), Optional(_));
  EXPECT_THAT(auth_session.auth_factor_map().Find(kFakePinLabel), Optional(_));

  // Test.

  // Calling RemoveAuthFactor for pin.
  user_data_auth::RemoveAuthFactorRequest request;
  request.set_auth_session_id(auth_session.serialized_token());
  request.set_auth_factor_label(kFakePinLabel);

  TestFuture<CryptohomeStatus> remove_future;
  auth_session.GetAuthForDecrypt()->RemoveAuthFactor(
      request, remove_future.GetCallback());

  EXPECT_THAT(remove_future.Get(), IsOk());

  // Only password is available.
  std::map<std::string, AuthFactorType> stored_factors_1 =
      auth_factor_manager_.ListAuthFactors(SanitizeUserName(kFakeUsername));
  EXPECT_THAT(stored_factors_1,
              ElementsAre(Pair(kFakeLabel, AuthFactorType::kPassword)));
  EXPECT_THAT(auth_session.auth_factor_map().Find(kFakeLabel), Optional(_));
  EXPECT_THAT(auth_session.auth_factor_map().Find(kFakePinLabel),
              Eq(std::nullopt));

  // Calling AuthenticateAuthFactor for password succeeds.
  error = AuthenticatePasswordAuthFactor(kFakeLabel, kFakePass, auth_session);
  EXPECT_EQ(error, user_data_auth::CRYPTOHOME_ERROR_NOT_SET);

  // Calling AuthenticateAuthFactor for pin fails.
  std::vector<std::string> auth_factor_labels{kFakePinLabel};
  user_data_auth::AuthInput auth_input_proto;
  auth_input_proto.mutable_pin_input()->set_secret(kFakePin);
  AuthenticateTestFuture authenticate_future;
  SerializedUserAuthFactorTypePolicy auth_factor_type_policy(
      {.type = *SerializeAuthFactorType(
           *DetermineFactorTypeFromAuthInput(auth_input_proto)),
       .enabled_intents = {},
       .disabled_intents = {}});
  auth_session.AuthenticateAuthFactor(
      ToAuthenticateRequest(auth_factor_labels, auth_input_proto),
      auth_factor_type_policy, authenticate_future.GetCallback());

  // Verify.
  auto& [action, status] = authenticate_future.Get();
  EXPECT_EQ(action.action_type, AuthSession::PostAuthActionType::kNone);
  EXPECT_THAT(status, NotOk());
  EXPECT_EQ(status->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_KEY_NOT_FOUND);
  // The verifier still uses the password.
  UserSession* user_session = FindOrCreateUserSession(kFakeUsername);
  EXPECT_THAT(user_session->GetCredentialVerifiers(),
              UnorderedElementsAre(
                  IsVerifierPtrWithLabelAndPassword(kFakeLabel, kFakePass)));
}

TEST_F(AuthSessionWithUssTest, RemoveAuthFactorPartialRemoveIsStillOk) {
  // Setup.
  AuthSession auth_session({.username = kFakeUsername,
                            .is_ephemeral_user = false,
                            .intent = AuthIntent::kDecrypt,
                            .auth_factor_status_update_timer =
                                std::make_unique<base::WallClockTimer>(),
                            .user_exists = false,
                            .auth_factor_map = AuthFactorMap()},
                           backing_apis_);
  // Creating the user.
  EXPECT_TRUE(auth_session.OnUserCreated().ok());
  EXPECT_TRUE(auth_session.has_user_secret_stash());

  user_data_auth::CryptohomeErrorCode error =
      user_data_auth::CRYPTOHOME_ERROR_NOT_SET;

  error = AddPasswordAuthFactor(kFakeLabel, kFakePass, /*first_factor=*/true,
                                auth_session);
  EXPECT_EQ(error, user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  error = AddPinAuthFactor(kFakePin, auth_session);
  EXPECT_EQ(error, user_data_auth::CRYPTOHOME_ERROR_NOT_SET);

  // Both password and pin are available.
  std::map<std::string, AuthFactorType> stored_factors =
      auth_factor_manager_.ListAuthFactors(SanitizeUserName(kFakeUsername));
  EXPECT_THAT(stored_factors,
              ElementsAre(Pair(kFakeLabel, AuthFactorType::kPassword),
                          Pair(kFakePinLabel, AuthFactorType::kPin)));
  EXPECT_THAT(auth_session.auth_factor_map().Find(kFakeLabel), Optional(_));
  EXPECT_THAT(auth_session.auth_factor_map().Find(kFakePinLabel), Optional(_));

  // Disable the writing of the USS file. This shouldn't cause the remove
  // operation to fail.
  EXPECT_CALL(platform_,
              WriteFileAtomicDurable(
                  UserSecretStashPath(SanitizeUserName(kFakeUsername),
                                      kUserSecretStashDefaultSlot),
                  _, _))
      .Times(AtLeast(1))
      .WillRepeatedly(Return(false));

  // Test.

  // Calling RemoveAuthFactor for pin.
  user_data_auth::RemoveAuthFactorRequest request;
  request.set_auth_session_id(auth_session.serialized_token());
  request.set_auth_factor_label(kFakePinLabel);

  TestFuture<CryptohomeStatus> remove_future;
  auth_session.GetAuthForDecrypt()->RemoveAuthFactor(
      request, remove_future.GetCallback());

  EXPECT_THAT(remove_future.Get(), IsOk());

  // Only password is available.
  std::map<std::string, AuthFactorType> stored_factors_1 =
      auth_factor_manager_.ListAuthFactors(SanitizeUserName(kFakeUsername));
  EXPECT_THAT(stored_factors_1,
              ElementsAre(Pair(kFakeLabel, AuthFactorType::kPassword)));
  EXPECT_THAT(auth_session.auth_factor_map().Find(kFakeLabel), Optional(_));
  EXPECT_THAT(auth_session.auth_factor_map().Find(kFakePinLabel),
              Eq(std::nullopt));

  // Calling AuthenticateAuthFactor for password succeeds.
  error = AuthenticatePasswordAuthFactor(kFakeLabel, kFakePass, auth_session);
  EXPECT_EQ(error, user_data_auth::CRYPTOHOME_ERROR_NOT_SET);

  // Calling AuthenticateAuthFactor for pin fails.
  std::vector<std::string> auth_factor_labels{kFakePinLabel};
  user_data_auth::AuthInput auth_input_proto;
  auth_input_proto.mutable_pin_input()->set_secret(kFakePin);
  AuthenticateTestFuture authenticate_future;
  SerializedUserAuthFactorTypePolicy auth_factor_type_policy(
      {.type = *SerializeAuthFactorType(
           *DetermineFactorTypeFromAuthInput(auth_input_proto)),
       .enabled_intents = {},
       .disabled_intents = {}});
  auth_session.AuthenticateAuthFactor(
      ToAuthenticateRequest(auth_factor_labels, auth_input_proto),
      auth_factor_type_policy, authenticate_future.GetCallback());

  // Verify.
  auto& [action, status] = authenticate_future.Get();
  EXPECT_EQ(action.action_type, AuthSession::PostAuthActionType::kNone);
  EXPECT_THAT(status, NotOk());
  EXPECT_EQ(status->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_KEY_NOT_FOUND);
  // The verifier still uses the password.
  UserSession* user_session = FindOrCreateUserSession(kFakeUsername);
  EXPECT_THAT(user_session->GetCredentialVerifiers(),
              UnorderedElementsAre(
                  IsVerifierPtrWithLabelAndPassword(kFakeLabel, kFakePass)));
}

TEST_F(AuthSessionWithUssTest, RemoveAuthFactorRemovesCredentialVerifier) {
  // Setup.
  AuthSession auth_session({.username = kFakeUsername,
                            .is_ephemeral_user = false,
                            .intent = AuthIntent::kDecrypt,
                            .auth_factor_status_update_timer =
                                std::make_unique<base::WallClockTimer>(),
                            .user_exists = false,
                            .auth_factor_map = AuthFactorMap()},
                           backing_apis_);
  // Creating the user.
  EXPECT_TRUE(auth_session.OnUserCreated().ok());
  EXPECT_TRUE(auth_session.has_user_secret_stash());

  user_data_auth::CryptohomeErrorCode error =
      user_data_auth::CRYPTOHOME_ERROR_NOT_SET;

  error = AddPasswordAuthFactor(kFakeLabel, kFakePass,
                                /*first_factor=*/true, auth_session);
  EXPECT_EQ(error, user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  error = AddPasswordAuthFactor(kFakeOtherLabel, kFakeOtherPass,
                                /*first_factor=*/false, auth_session);
  EXPECT_EQ(error, user_data_auth::CRYPTOHOME_ERROR_NOT_SET);

  // Both passwords are available, the first one should supply a verifier.
  std::map<std::string, AuthFactorType> stored_factors =
      auth_factor_manager_.ListAuthFactors(SanitizeUserName(kFakeUsername));
  EXPECT_THAT(stored_factors,
              ElementsAre(Pair(kFakeLabel, AuthFactorType::kPassword),
                          Pair(kFakeOtherLabel, AuthFactorType::kPassword)));
  EXPECT_THAT(auth_session.auth_factor_map().Find(kFakeLabel), Optional(_));
  EXPECT_THAT(auth_session.auth_factor_map().Find(kFakeOtherLabel),
              Optional(_));
  UserSession* user_session = FindOrCreateUserSession(kFakeUsername);
  EXPECT_THAT(
      user_session->GetCredentialVerifiers(),
      UnorderedElementsAre(
          IsVerifierPtrWithLabelAndPassword(kFakeLabel, kFakePass),
          IsVerifierPtrWithLabelAndPassword(kFakeOtherLabel, kFakeOtherPass)));

  // Test.

  // Calling RemoveAuthFactor for the second password.
  user_data_auth::RemoveAuthFactorRequest request;
  request.set_auth_session_id(auth_session.serialized_token());
  request.set_auth_factor_label(kFakeOtherLabel);

  TestFuture<CryptohomeStatus> remove_future;
  auth_session.GetAuthForDecrypt()->RemoveAuthFactor(
      request, remove_future.GetCallback());

  EXPECT_THAT(remove_future.Get(), IsOk());

  // Only the first password is available.
  std::map<std::string, AuthFactorType> stored_factors_1 =
      auth_factor_manager_.ListAuthFactors(SanitizeUserName(kFakeUsername));
  EXPECT_THAT(stored_factors_1,
              ElementsAre(Pair(kFakeLabel, AuthFactorType::kPassword)));
  EXPECT_THAT(auth_session.auth_factor_map().Find(kFakeLabel), Optional(_));
  EXPECT_THAT(auth_session.auth_factor_map().Find(kFakeOtherLabel),
              Eq(std::nullopt));

  // Calling AuthenticateAuthFactor for the first password succeeds.
  error = AuthenticatePasswordAuthFactor(kFakeLabel, kFakePass, auth_session);
  EXPECT_EQ(error, user_data_auth::CRYPTOHOME_ERROR_NOT_SET);

  // Calling AuthenticateAuthFactor for the second password fails.
  std::vector<std::string> auth_factor_labels{kFakeOtherLabel};
  user_data_auth::AuthInput auth_input_proto;
  auth_input_proto.mutable_password_input()->set_secret(kFakeOtherPass);
  AuthenticateTestFuture authenticate_future;
  SerializedUserAuthFactorTypePolicy auth_factor_type_policy(
      {.type = *SerializeAuthFactorType(
           *DetermineFactorTypeFromAuthInput(auth_input_proto)),
       .enabled_intents = {},
       .disabled_intents = {}});
  auth_session.AuthenticateAuthFactor(
      ToAuthenticateRequest(auth_factor_labels, auth_input_proto),
      auth_factor_type_policy, authenticate_future.GetCallback());

  // Verify.
  auto& [action, status] = authenticate_future.Get();
  EXPECT_EQ(action.action_type, AuthSession::PostAuthActionType::kNone);
  EXPECT_THAT(status, NotOk());
  EXPECT_EQ(status->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_KEY_NOT_FOUND);
  // Now only the first password verifier is available.
  EXPECT_THAT(user_session->GetCredentialVerifiers(),
              UnorderedElementsAre(
                  IsVerifierPtrWithLabelAndPassword(kFakeLabel, kFakePass)));
}

// The test adds, removes and adds the same auth factor again.
TEST_F(AuthSessionWithUssTest, RemoveAndReAddAuthFactor) {
  // Setup.
  AuthSession auth_session({.username = kFakeUsername,
                            .is_ephemeral_user = false,
                            .intent = AuthIntent::kDecrypt,
                            .auth_factor_status_update_timer =
                                std::make_unique<base::WallClockTimer>(),
                            .user_exists = false,
                            .auth_factor_map = AuthFactorMap()},
                           backing_apis_);
  // Creating the user.
  EXPECT_TRUE(auth_session.OnUserCreated().ok());
  EXPECT_TRUE(auth_session.has_user_secret_stash());

  user_data_auth::CryptohomeErrorCode error =
      user_data_auth::CRYPTOHOME_ERROR_NOT_SET;

  error = AddPasswordAuthFactor(kFakeLabel, kFakePass,
                                /*first_factor=*/true, auth_session);
  EXPECT_EQ(error, user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  error = AddPinAuthFactor(kFakePin, auth_session);
  EXPECT_EQ(error, user_data_auth::CRYPTOHOME_ERROR_NOT_SET);

  // Test.
  // Calling RemoveAuthFactor for pin.
  user_data_auth::RemoveAuthFactorRequest request;
  request.set_auth_session_id(auth_session.serialized_token());
  request.set_auth_factor_label(kFakePinLabel);

  TestFuture<CryptohomeStatus> remove_future;
  auth_session.GetAuthForDecrypt()->RemoveAuthFactor(
      request, remove_future.GetCallback());

  EXPECT_THAT(remove_future.Get(), IsOk());

  // Add the same pin auth factor again.
  error = AddPinAuthFactor(kFakePin, auth_session);
  EXPECT_EQ(error, user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  // The verifier still uses the original password.
  UserSession* user_session = FindOrCreateUserSession(kFakeUsername);
  EXPECT_THAT(user_session->GetCredentialVerifiers(),
              UnorderedElementsAre(
                  IsVerifierPtrWithLabelAndPassword(kFakeLabel, kFakePass)));
}

TEST_F(AuthSessionWithUssTest, RemoveAuthFactorFailsForLastFactor) {
  // Setup.
  AuthSession auth_session({.username = kFakeUsername,
                            .is_ephemeral_user = false,
                            .intent = AuthIntent::kDecrypt,
                            .auth_factor_status_update_timer =
                                std::make_unique<base::WallClockTimer>(),
                            .user_exists = false,
                            .auth_factor_map = AuthFactorMap()},
                           backing_apis_);

  // Creating the user.
  EXPECT_TRUE(auth_session.OnUserCreated().ok());
  EXPECT_TRUE(auth_session.has_user_secret_stash());

  user_data_auth::CryptohomeErrorCode error =
      user_data_auth::CRYPTOHOME_ERROR_NOT_SET;

  error = AddPasswordAuthFactor(kFakeLabel, kFakePass,
                                /*first_factor=*/true, auth_session);
  EXPECT_EQ(error, user_data_auth::CRYPTOHOME_ERROR_NOT_SET);

  // Test.

  // Calling RemoveAuthFactor for password.
  user_data_auth::RemoveAuthFactorRequest request;
  request.set_auth_session_id(auth_session.serialized_token());
  request.set_auth_factor_label(kFakeLabel);

  TestFuture<CryptohomeStatus> remove_future;
  auth_session.GetAuthForDecrypt()->RemoveAuthFactor(
      request, remove_future.GetCallback());

  // Verify.
  ASSERT_THAT(remove_future.Get(), NotOk());
  EXPECT_EQ(remove_future.Get()->local_legacy_error(),
            user_data_auth::CRYPTOHOME_REMOVE_CREDENTIALS_FAILED);
  // The verifier is still set after the removal failed.
  UserSession* user_session = FindOrCreateUserSession(kFakeUsername);
  EXPECT_THAT(user_session->GetCredentialVerifiers(),
              UnorderedElementsAre(
                  IsVerifierPtrWithLabelAndPassword(kFakeLabel, kFakePass)));
}

TEST_F(AuthSessionWithUssTest, UpdateAuthFactor) {
  // Setup.
  std::string new_pass = "update fake pass";

  {
    AuthSession auth_session({.username = kFakeUsername,
                              .is_ephemeral_user = false,
                              .intent = AuthIntent::kDecrypt,
                              .auth_factor_status_update_timer =
                                  std::make_unique<base::WallClockTimer>(),
                              .user_exists = false,
                              .auth_factor_map = AuthFactorMap()},
                             backing_apis_);

    // Creating the user.
    EXPECT_TRUE(auth_session.OnUserCreated().ok());
    EXPECT_TRUE(auth_session.has_user_secret_stash());

    user_data_auth::CryptohomeErrorCode error =
        user_data_auth::CRYPTOHOME_ERROR_NOT_SET;

    // Calling AddAuthFactor.
    error = AddPasswordAuthFactor(kFakeLabel, kFakePass, /*first_factor=*/true,
                                  auth_session);
    EXPECT_EQ(error, user_data_auth::CRYPTOHOME_ERROR_NOT_SET);

    // Test.

    // Calling UpdateAuthFactor.
    error = UpdatePasswordAuthFactor(new_pass, auth_session);
    EXPECT_EQ(error, user_data_auth::CRYPTOHOME_ERROR_NOT_SET);

    // Force the creation of the user session, otherwise any verifiers added
    // will be destroyed when the session is.
    FindOrCreateUserSession(kFakeUsername);
  }

  AuthSession new_auth_session(
      {.username = kFakeUsername,
       .is_ephemeral_user = false,
       .intent = AuthIntent::kDecrypt,
       .auth_factor_status_update_timer =
           std::make_unique<base::WallClockTimer>(),
       .user_exists = true,
       .auth_factor_map =
           AfMapBuilder()
               .WithUss()
               .AddPassword<TpmBoundToPcrAuthBlockState>(kFakeLabel)
               .Consume()},
      backing_apis_);
  EXPECT_THAT(new_auth_session.authorized_intents(), IsEmpty());

  // Verify.
  // The credential verifier uses the new password.
  UserSession* user_session = FindOrCreateUserSession(kFakeUsername);
  EXPECT_THAT(user_session->GetCredentialVerifiers(),
              UnorderedElementsAre(
                  IsVerifierPtrWithLabelAndPassword(kFakeLabel, new_pass)));
  // AuthenticateAuthFactor should succeed using the new password.
  user_data_auth::CryptohomeErrorCode error =
      AuthenticatePasswordAuthFactor(kFakeLabel, new_pass, new_auth_session);
  EXPECT_EQ(error, user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  EXPECT_THAT(
      new_auth_session.authorized_intents(),
      UnorderedElementsAre(AuthIntent::kDecrypt, AuthIntent::kVerifyOnly));
}

// Test that AddauthFactor successfully adds a PIN factor on a
// session that was authenticated via a recovery factor.
TEST_F(AuthSessionWithUssTest, AddPinAfterRecoveryAuth) {
  // Setup.
  {
    // Obtain AuthSession for user setup.
    AuthSession auth_session({.username = kFakeUsername,
                              .is_ephemeral_user = false,
                              .intent = AuthIntent::kDecrypt,
                              .auth_factor_status_update_timer =
                                  std::make_unique<base::WallClockTimer>(),
                              .user_exists = false,
                              .auth_factor_map = AuthFactorMap()},
                             backing_apis_);
    // Create the user with password and recovery factors.
    EXPECT_THAT(auth_session.OnUserCreated(), IsOk());
    EXPECT_EQ(AddPasswordAuthFactor(kFakeLabel, kFakePass,
                                    /*first_factor=*/true, auth_session),
              user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
    EXPECT_EQ(AddRecoveryAuthFactor(kRecoveryLabel, kFakeRecoverySecret,
                                    auth_session),
              user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  }

  // Obtain AuthSession for authentication.
  AuthSession new_auth_session(
      {.username = kFakeUsername,
       .is_ephemeral_user = false,
       .intent = AuthIntent::kDecrypt,
       .auth_factor_status_update_timer =
           std::make_unique<base::WallClockTimer>(),
       .user_exists = true,
       .auth_factor_map =
           AfMapBuilder()
               .WithUss()
               .AddPassword<TpmBoundToPcrAuthBlockState>(kFakeLabel)
               .AddRecovery(kRecoveryLabel)
               .Consume()},
      backing_apis_);

  // Authenticate the new auth session with recovery factor.
  EXPECT_EQ(AuthenticateRecoveryAuthFactor(kRecoveryLabel, kFakeRecoverySecret,
                                           new_auth_session),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  EXPECT_THAT(
      new_auth_session.authorized_intents(),
      UnorderedElementsAre(AuthIntent::kDecrypt, AuthIntent::kVerifyOnly));
  EXPECT_TRUE(new_auth_session.has_user_secret_stash());

  // Test adding a PIN AuthFactor.
  user_data_auth::CryptohomeErrorCode error =
      AddPinAuthFactor(kFakePin, new_auth_session);
  EXPECT_EQ(error, user_data_auth::CRYPTOHOME_ERROR_NOT_SET);

  // Verify PIN factor is added.
  std::map<std::string, AuthFactorType> stored_factors =
      auth_factor_manager_.ListAuthFactors(SanitizeUserName(kFakeUsername));
  EXPECT_THAT(stored_factors,
              UnorderedElementsAre(
                  Pair(kFakeLabel, AuthFactorType::kPassword),
                  Pair(kRecoveryLabel, AuthFactorType::kCryptohomeRecovery),
                  Pair(kFakePinLabel, AuthFactorType::kPin)));
  // Verify that reset secret for the pin label is added to USS.
  EXPECT_TRUE(new_auth_session.HasResetSecretInUssForTesting(kFakePinLabel));
}

// Test that UpdateAuthFactor successfully updates a password factor on a
// session that was authenticated via a recovery factor.
TEST_F(AuthSessionWithUssTest, UpdatePasswordAfterRecoveryAuth) {
  // Setup.
  constexpr char kNewFakePass[] = "new fake pass";
  {
    // Obtain AuthSession for user setup.
    AuthSession auth_session({.username = kFakeUsername,
                              .is_ephemeral_user = false,
                              .intent = AuthIntent::kDecrypt,
                              .auth_factor_status_update_timer =
                                  std::make_unique<base::WallClockTimer>(),
                              .user_exists = false,
                              .auth_factor_map = AuthFactorMap()},
                             backing_apis_);
    // Create the user.
    EXPECT_THAT(auth_session.OnUserCreated(), IsOk());
    // Add password AuthFactor.
    EXPECT_EQ(AddPasswordAuthFactor(kFakeLabel, kFakePass,
                                    /*first_factor=*/true, auth_session),
              user_data_auth::CRYPTOHOME_ERROR_NOT_SET);

    // Add recovery AuthFactor.
    EXPECT_EQ(AddRecoveryAuthFactor(kRecoveryLabel, kFakeRecoverySecret,
                                    auth_session),
              user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  }

  // Set up mocks for the now-existing user.
  EXPECT_CALL(platform_, DirectoryExists(_)).WillRepeatedly(Return(true));
  // Obtain AuthSession for authentication.
  AuthSession new_auth_session(
      {.username = kFakeUsername,
       .is_ephemeral_user = false,
       .intent = AuthIntent::kDecrypt,
       .auth_factor_status_update_timer =
           std::make_unique<base::WallClockTimer>(),
       .user_exists = true,
       .auth_factor_map =
           AfMapBuilder()
               .WithUss()
               .AddPassword<TpmBoundToPcrAuthBlockState>(kFakeLabel)
               .AddRecovery(kRecoveryLabel)
               .Consume()},
      backing_apis_);

  // Authenticate the new auth session with recovery factor.
  EXPECT_EQ(AuthenticateRecoveryAuthFactor(kRecoveryLabel, kFakeRecoverySecret,
                                           new_auth_session),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  EXPECT_THAT(
      new_auth_session.authorized_intents(),
      UnorderedElementsAre(AuthIntent::kDecrypt, AuthIntent::kVerifyOnly));
  EXPECT_TRUE(new_auth_session.has_user_secret_stash());
  EXPECT_THAT(
      new_auth_session.authorized_intents(),
      UnorderedElementsAre(AuthIntent::kDecrypt, AuthIntent::kVerifyOnly));

  // Test updating existing password factor.
  user_data_auth::CryptohomeErrorCode error =
      UpdatePasswordAuthFactor(kNewFakePass, new_auth_session);

  // Verify update succeeded.
  EXPECT_EQ(error, user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
}

TEST_F(AuthSessionWithUssTest, UpdateAuthFactorFailsForWrongLabel) {
  // Setup.
  AuthSession auth_session(
      AuthSession::Params{.username = kFakeUsername,
                          .is_ephemeral_user = false,
                          .intent = AuthIntent::kVerifyOnly,
                          .auth_factor_status_update_timer =
                              std::make_unique<base::WallClockTimer>(),
                          .user_exists = false,
                          .auth_factor_map = AuthFactorMap()},
      backing_apis_);
  // Creating the user.
  EXPECT_TRUE(auth_session.OnUserCreated().ok());
  EXPECT_TRUE(auth_session.has_user_secret_stash());

  user_data_auth::CryptohomeErrorCode error =
      user_data_auth::CRYPTOHOME_ERROR_NOT_SET;

  // Calling AddAuthFactor.
  error = AddPasswordAuthFactor(kFakeLabel, kFakePass, /*first_factor=*/true,
                                auth_session);
  EXPECT_EQ(error, user_data_auth::CRYPTOHOME_ERROR_NOT_SET);

  std::string new_pass = "update fake pass";

  // Test.

  // Calling UpdateAuthFactor.
  user_data_auth::UpdateAuthFactorRequest request;
  request.set_auth_session_id(auth_session.serialized_token());
  request.set_auth_factor_label(kFakeLabel);
  request.mutable_auth_factor()->set_type(
      user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
  request.mutable_auth_factor()->set_label("different new label");
  request.mutable_auth_factor()->mutable_password_metadata();
  request.mutable_auth_input()->mutable_password_input()->set_secret(new_pass);

  TestFuture<CryptohomeStatus> update_future;
  auth_session.GetAuthForDecrypt()->UpdateAuthFactor(
      request, update_future.GetCallback());

  // Verify.
  ASSERT_THAT(update_future.Get(), NotOk());
  EXPECT_EQ(update_future.Get()->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
  // The verifier still uses the original password.
  UserSession* user_session = FindOrCreateUserSession(kFakeUsername);
  EXPECT_THAT(user_session->GetCredentialVerifiers(),
              UnorderedElementsAre(
                  IsVerifierPtrWithLabelAndPassword(kFakeLabel, kFakePass)));
}

TEST_F(AuthSessionWithUssTest, UpdateAuthFactorFailsForWrongType) {
  // Setup.
  AuthSession auth_session({.username = kFakeUsername,
                            .is_ephemeral_user = false,
                            .intent = AuthIntent::kDecrypt,
                            .auth_factor_status_update_timer =
                                std::make_unique<base::WallClockTimer>(),
                            .user_exists = false,
                            .auth_factor_map = AuthFactorMap()},
                           backing_apis_);
  // Creating the user.
  EXPECT_TRUE(auth_session.OnUserCreated().ok());
  EXPECT_TRUE(auth_session.has_user_secret_stash());

  user_data_auth::CryptohomeErrorCode error =
      user_data_auth::CRYPTOHOME_ERROR_NOT_SET;

  // Calling AddAuthFactor.
  error = AddPasswordAuthFactor(kFakeLabel, kFakePass, /*first_factor=*/true,
                                auth_session);
  EXPECT_EQ(error, user_data_auth::CRYPTOHOME_ERROR_NOT_SET);

  // Test.

  // Calling UpdateAuthFactor.
  user_data_auth::UpdateAuthFactorRequest request;
  request.set_auth_session_id(auth_session.serialized_token());
  request.set_auth_factor_label(kFakeLabel);
  request.mutable_auth_factor()->set_type(user_data_auth::AUTH_FACTOR_TYPE_PIN);
  request.mutable_auth_factor()->set_label(kFakeLabel);
  request.mutable_auth_factor()->mutable_pin_metadata();
  request.mutable_auth_input()->mutable_pin_input()->set_secret(kFakePin);

  TestFuture<CryptohomeStatus> update_future;
  auth_session.GetAuthForDecrypt()->UpdateAuthFactor(
      request, update_future.GetCallback());

  // Verify.
  ASSERT_THAT(update_future.Get(), NotOk());
  EXPECT_EQ(update_future.Get()->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
  // The verifier still uses the original password.
  UserSession* user_session = FindOrCreateUserSession(kFakeUsername);
  EXPECT_THAT(user_session->GetCredentialVerifiers(),
              UnorderedElementsAre(
                  IsVerifierPtrWithLabelAndPassword(kFakeLabel, kFakePass)));
}

TEST_F(AuthSessionWithUssTest, UpdateAuthFactorFailsWhenLabelDoesntExist) {
  // Setup.
  AuthSession auth_session({.username = kFakeUsername,
                            .is_ephemeral_user = false,
                            .intent = AuthIntent::kDecrypt,
                            .auth_factor_status_update_timer =
                                std::make_unique<base::WallClockTimer>(),
                            .user_exists = false,
                            .auth_factor_map = AuthFactorMap()},
                           backing_apis_);
  // Creating the user.
  EXPECT_TRUE(auth_session.OnUserCreated().ok());
  EXPECT_TRUE(auth_session.has_user_secret_stash());

  user_data_auth::CryptohomeErrorCode error =
      user_data_auth::CRYPTOHOME_ERROR_NOT_SET;

  // Calling AddAuthFactor.
  error = AddPasswordAuthFactor(kFakeLabel, kFakePass, /*first_factor=*/true,
                                auth_session);
  EXPECT_EQ(error, user_data_auth::CRYPTOHOME_ERROR_NOT_SET);

  // Test.

  // Calling UpdateAuthFactor.
  user_data_auth::UpdateAuthFactorRequest request;
  request.set_auth_session_id(auth_session.serialized_token());
  request.set_auth_factor_label("label doesn't exist");
  request.mutable_auth_factor()->set_type(
      user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
  request.mutable_auth_factor()->set_label(kFakeLabel);
  request.mutable_auth_factor()->mutable_password_metadata();
  request.mutable_auth_input()->mutable_password_input()->set_secret(kFakePass);

  TestFuture<CryptohomeStatus> update_future;
  auth_session.GetAuthForDecrypt()->UpdateAuthFactor(
      request, update_future.GetCallback());

  // Verify.
  ASSERT_THAT(update_future.Get(), NotOk());
  EXPECT_EQ(update_future.Get()->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_KEY_NOT_FOUND);
  // The verifier still uses the original password.
  UserSession* user_session = FindOrCreateUserSession(kFakeUsername);
  EXPECT_THAT(user_session->GetCredentialVerifiers(),
              UnorderedElementsAre(
                  IsVerifierPtrWithLabelAndPassword(kFakeLabel, kFakePass)));
}

// Test that `UpdateAuthFactor` fails when the auth block derivation fails
// (but doesn't crash).
TEST_F(AuthSessionWithUssTest, UpdateAuthFactorFailsInAuthBlock) {
  // Setup.
  AuthSession auth_session({.username = kFakeUsername,
                            .is_ephemeral_user = false,
                            .intent = AuthIntent::kDecrypt,
                            .auth_factor_status_update_timer =
                                std::make_unique<base::WallClockTimer>(),
                            .user_exists = false,
                            .auth_factor_map = AuthFactorMap()},
                           backing_apis_);

  // Creating the user and add the auth factor.
  EXPECT_THAT(auth_session.OnUserCreated(), IsOk());
  EXPECT_TRUE(auth_session.has_user_secret_stash());

  user_data_auth::CryptohomeErrorCode error =
      user_data_auth::CRYPTOHOME_ERROR_NOT_SET;

  // Calling AddAuthFactor.
  error = AddPasswordAuthFactor(kFakeLabel, kFakePass, /*first_factor=*/true,
                                auth_session);
  EXPECT_EQ(error, user_data_auth::CRYPTOHOME_ERROR_NOT_SET);

  // Setting the expectations for the new auth block creation. The mock is set
  // to fail.
  EXPECT_CALL(auth_block_utility_, CreateKeyBlobsWithAuthBlock(_, _, _))
      .WillOnce([](auto, auto, AuthBlock::CreateCallback create_callback) {
        std::move(create_callback)
            .Run(MakeStatus<CryptohomeCryptoError>(
                     kErrorLocationForTestingAuthSession,
                     error::ErrorActionSet(
                         {error::PossibleAction::kDevCheckUnexpectedState}),
                     CryptoError::CE_OTHER_CRYPTO),
                 nullptr, nullptr);
      });

  // Test.
  // Preparing UpdateAuthFactor parameters.
  user_data_auth::UpdateAuthFactorRequest update_request;
  update_request.set_auth_session_id(auth_session.serialized_token());
  update_request.set_auth_factor_label(kFakeLabel);
  update_request.mutable_auth_factor()->set_type(
      user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
  update_request.mutable_auth_factor()->set_label(kFakeLabel);
  update_request.mutable_auth_factor()->mutable_password_metadata();
  update_request.mutable_auth_input()->mutable_password_input()->set_secret(
      kFakePass);
  // Calling UpdateAuthFactor.
  TestFuture<CryptohomeStatus> update_future;
  auth_session.GetAuthForDecrypt()->UpdateAuthFactor(
      update_request, update_future.GetCallback());

  // Verify.
  EXPECT_THAT(update_future.Get(), NotOk());
}

TEST_F(AuthSessionWithUssTest, UpdateAuthFactorMetadataSuccess) {
  // Setup.
  AuthSession auth_session({.username = kFakeUsername,
                            .is_ephemeral_user = false,
                            .intent = AuthIntent::kDecrypt,
                            .auth_factor_status_update_timer =
                                std::make_unique<base::WallClockTimer>(),
                            .user_exists = false,
                            .auth_factor_map = AuthFactorMap()},
                           backing_apis_);

  // Creating the user.
  EXPECT_THAT(auth_session.OnUserCreated(), IsOk());
  EXPECT_TRUE(auth_session.has_user_secret_stash());

  user_data_auth::CryptohomeErrorCode error =
      user_data_auth::CRYPTOHOME_ERROR_NOT_SET;

  // Calling AddAuthFactor.
  error = AddPasswordAuthFactor(kFakeLabel, kFakePass, /*first_factor=*/true,
                                auth_session);
  EXPECT_EQ(error, user_data_auth::CRYPTOHOME_ERROR_NOT_SET);

  // Test.
  user_data_auth::AuthFactor new_auth_factor;
  std::string kFakeChromeVersion = "fake chrome version";
  std::string kUserSpecifiedName = "password";

  new_auth_factor.set_type(user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
  new_auth_factor.set_label(kFakeLabel);
  new_auth_factor.mutable_password_metadata();
  new_auth_factor.mutable_common_metadata()->set_chrome_version_last_updated(
      kFakeChromeVersion);
  new_auth_factor.mutable_common_metadata()->set_user_specified_name(
      kUserSpecifiedName);

  error = UpdateAuthFactorMetadata(new_auth_factor, auth_session);
  EXPECT_EQ(error, user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  auto loaded_auth_factor = auth_factor_manager_.LoadAuthFactor(
      SanitizeUserName(kFakeUsername), AuthFactorType::kPassword, kFakeLabel);
  EXPECT_THAT(loaded_auth_factor, IsOk());
  EXPECT_EQ(loaded_auth_factor->type(), AuthFactorType::kPassword);
  EXPECT_EQ(loaded_auth_factor->label(), kFakeLabel);
  EXPECT_EQ(loaded_auth_factor->metadata().common.chrome_version_last_updated,
            kFakeChromeVersion);
  EXPECT_EQ(loaded_auth_factor->metadata().common.user_specified_name,
            kUserSpecifiedName);

  // Calling AuthenticateAuthFactor with the password succeeds.
  error = AuthenticatePasswordAuthFactor(kFakeLabel, kFakePass, auth_session);
  EXPECT_EQ(error, user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
}

TEST_F(AuthSessionWithUssTest, UpdateAuthFactorMetadataEmptyLabelFailure) {
  // Setup.
  AuthSession auth_session({.username = kFakeUsername,
                            .is_ephemeral_user = false,
                            .intent = AuthIntent::kDecrypt,
                            .auth_factor_status_update_timer =
                                std::make_unique<base::WallClockTimer>(),
                            .user_exists = false,
                            .auth_factor_map = AuthFactorMap()},
                           backing_apis_);

  // Creating the user.
  EXPECT_THAT(auth_session.OnUserCreated(), IsOk());
  EXPECT_TRUE(auth_session.has_user_secret_stash());

  user_data_auth::CryptohomeErrorCode error =
      user_data_auth::CRYPTOHOME_ERROR_NOT_SET;

  // Calling AddAuthFactor.
  error = AddPasswordAuthFactor(kFakeLabel, kFakePass, /*first_factor=*/true,
                                auth_session);
  EXPECT_EQ(error, user_data_auth::CRYPTOHOME_ERROR_NOT_SET);

  // Test.
  user_data_auth::AuthFactor new_auth_factor;

  new_auth_factor.set_type(user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
  new_auth_factor.set_label("");
  new_auth_factor.mutable_password_metadata();

  error = UpdateAuthFactorMetadata(new_auth_factor, auth_session);
  EXPECT_EQ(error, user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
}

TEST_F(AuthSessionWithUssTest, UpdateAuthFactorMetadataWrongLabelFailure) {
  // Setup.
  AuthSession auth_session({.username = kFakeUsername,
                            .is_ephemeral_user = false,
                            .intent = AuthIntent::kDecrypt,
                            .auth_factor_status_update_timer =
                                std::make_unique<base::WallClockTimer>(),
                            .user_exists = false,
                            .auth_factor_map = AuthFactorMap()},
                           backing_apis_);

  // Creating the user.
  EXPECT_THAT(auth_session.OnUserCreated(), IsOk());
  EXPECT_TRUE(auth_session.has_user_secret_stash());

  user_data_auth::CryptohomeErrorCode error =
      user_data_auth::CRYPTOHOME_ERROR_NOT_SET;

  // Calling AddAuthFactor.
  error = AddPasswordAuthFactor(kFakeLabel, kFakePass, /*first_factor=*/true,
                                auth_session);
  EXPECT_EQ(error, user_data_auth::CRYPTOHOME_ERROR_NOT_SET);

  // Test.
  user_data_auth::AuthFactor new_auth_factor;

  new_auth_factor.set_type(user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
  new_auth_factor.set_label(kFakeOtherLabel);
  new_auth_factor.mutable_password_metadata();

  error = UpdateAuthFactorMetadata(new_auth_factor, auth_session);
  EXPECT_EQ(error, user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
}

TEST_F(AuthSessionWithUssTest, UpdateAuthFactorMetadataLongNameFailure) {
  // Setup.
  AuthSession auth_session({.username = kFakeUsername,
                            .is_ephemeral_user = false,
                            .intent = AuthIntent::kDecrypt,
                            .auth_factor_status_update_timer =
                                std::make_unique<base::WallClockTimer>(),
                            .user_exists = false,
                            .auth_factor_map = AuthFactorMap()},
                           backing_apis_);

  // Creating the user.
  EXPECT_THAT(auth_session.OnUserCreated(), IsOk());
  EXPECT_TRUE(auth_session.has_user_secret_stash());

  user_data_auth::CryptohomeErrorCode error =
      user_data_auth::CRYPTOHOME_ERROR_NOT_SET;

  // Calling AddAuthFactor.
  error = AddPasswordAuthFactor(kFakeLabel, kFakePass, /*first_factor=*/true,
                                auth_session);
  EXPECT_EQ(error, user_data_auth::CRYPTOHOME_ERROR_NOT_SET);

  // Test.
  user_data_auth::AuthFactor new_auth_factor;
  std::string extra_long_name(kUserSpecifiedNameSizeLimit + 1, 'x');

  new_auth_factor.set_type(user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
  new_auth_factor.set_label(kFakeLabel);
  new_auth_factor.mutable_password_metadata();
  new_auth_factor.mutable_common_metadata()->set_user_specified_name(
      extra_long_name);

  error = UpdateAuthFactorMetadata(new_auth_factor, auth_session);
  EXPECT_EQ(error, user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
}

TEST_F(AuthSessionWithUssTest, UpdateAuthFactorMetadataWrongTypeFailure) {
  // Setup.
  AuthSession auth_session({.username = kFakeUsername,
                            .is_ephemeral_user = false,
                            .intent = AuthIntent::kDecrypt,
                            .auth_factor_status_update_timer =
                                std::make_unique<base::WallClockTimer>(),
                            .user_exists = false,
                            .auth_factor_map = AuthFactorMap()},
                           backing_apis_);

  // Creating the user.
  EXPECT_THAT(auth_session.OnUserCreated(), IsOk());
  EXPECT_TRUE(auth_session.has_user_secret_stash());

  user_data_auth::CryptohomeErrorCode error =
      user_data_auth::CRYPTOHOME_ERROR_NOT_SET;

  // Calling AddAuthFactor.
  error = AddPasswordAuthFactor(kFakeLabel, kFakePass, /*first_factor=*/true,
                                auth_session);
  EXPECT_EQ(error, user_data_auth::CRYPTOHOME_ERROR_NOT_SET);

  // Test.
  user_data_auth::AuthFactor new_auth_factor;

  new_auth_factor.set_type(user_data_auth::AUTH_FACTOR_TYPE_PIN);
  new_auth_factor.set_label(kFakeLabel);
  new_auth_factor.mutable_pin_metadata();

  error = UpdateAuthFactorMetadata(new_auth_factor, auth_session);
  EXPECT_EQ(error, user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
}

// Test that AuthenticateAuthFactor succeeds for the `AuthIntent::kWebAuthn`
// scenario, using the legacy fingerprint.
TEST_F(AuthSessionWithUssTest, FingerprintAuthenticationForWebAuthn) {
  // Setup.
  // Add the user session. Configure the credential verifier mock to succeed.
  auto user_session = std::make_unique<MockUserSession>();
  EXPECT_CALL(*user_session, VerifyUser(SanitizeUserName(kFakeUsername)))
      .WillOnce(Return(true));
  auto verifier = std::make_unique<MockCredentialVerifier>(
      AuthFactorType::kLegacyFingerprint, "", AuthFactorMetadata{});
  EXPECT_CALL(*verifier, VerifySync(_)).WillOnce(ReturnOk<CryptohomeError>());
  user_session->AddCredentialVerifier(std::move(verifier));
  EXPECT_TRUE(user_session_map_.Add(kFakeUsername, std::move(user_session)));
  // Create an AuthSession and add a mock for a successful auth block verify.
  AuthSession auth_session({.username = kFakeUsername,
                            .is_ephemeral_user = false,
                            .intent = AuthIntent::kWebAuthn,
                            .auth_factor_status_update_timer =
                                std::make_unique<base::WallClockTimer>(),
                            .user_exists = true,
                            .auth_factor_map = AuthFactorMap()},
                           backing_apis_);

  // Test.
  user_data_auth::AuthInput auth_input_proto;
  auth_input_proto.mutable_legacy_fingerprint_input();
  AuthenticateTestFuture authenticate_future;
  SerializedUserAuthFactorTypePolicy auth_factor_type_policy(
      {.type = *SerializeAuthFactorType(
           *DetermineFactorTypeFromAuthInput(auth_input_proto)),
       .enabled_intents = {},
       .disabled_intents = {}});
  auth_session.AuthenticateAuthFactor(
      ToAuthenticateRequest({}, auth_input_proto), auth_factor_type_policy,
      authenticate_future.GetCallback());

  // Verify.
  auto& [action, status] = authenticate_future.Get();
  EXPECT_THAT(status, IsOk());
  EXPECT_EQ(action.action_type, AuthSession::PostAuthActionType::kNone);
  EXPECT_THAT(
      auth_session.authorized_intents(),
      UnorderedElementsAre(AuthIntent::kVerifyOnly, AuthIntent::kWebAuthn));
}

// Test that PrepareAuthFactor succeeds for fingerprint with the purpose of add.
TEST_F(AuthSessionWithUssTest, PrepareFingerprintAdd) {
  auto mock_le_manager = std::make_unique<MockLECredentialManager>();
  crypto_.set_le_manager_for_testing(std::move(mock_le_manager));
  // Create an AuthSession and add a mock for a successful auth block prepare.
  auto auth_session = std::make_unique<AuthSession>(
      AuthSession::Params{.username = kFakeUsername,
                          .is_ephemeral_user = false,
                          .intent = AuthIntent::kVerifyOnly,
                          .auth_factor_status_update_timer =
                              std::make_unique<base::WallClockTimer>(),
                          .user_exists = false,
                          .auth_factor_map = AuthFactorMap()},
      backing_apis_);
  EXPECT_TRUE(auth_session->OnUserCreated().ok());
  EXPECT_CALL(hwsec_pw_manager_, InsertRateLimiter)
      .WillOnce(ReturnValue(/* ret_label */ 0));

  EXPECT_CALL(*bio_processor_, StartEnrollSession(_))
      .WillOnce([](auto&& callback) { std::move(callback).Run(true); });

  // Test.
  TestFuture<CryptohomeStatus> prepare_future;
  user_data_auth::PrepareAuthFactorRequest prepare_request;
  prepare_request.set_auth_session_id(auth_session->serialized_token());
  prepare_request.set_auth_factor_type(
      user_data_auth::AUTH_FACTOR_TYPE_FINGERPRINT);
  prepare_request.set_purpose(user_data_auth::PURPOSE_ADD_AUTH_FACTOR);
  auth_session->PrepareAuthFactor(prepare_request,
                                  prepare_future.GetCallback());
  // Verify.
  ASSERT_THAT(prepare_future.Get(), IsOk());

  // Test.
  TestFuture<CryptohomeStatus> terminate_future;
  user_data_auth::TerminateAuthFactorRequest terminate_request;
  terminate_request.set_auth_session_id(auth_session->serialized_token());
  terminate_request.set_auth_factor_type(
      user_data_auth::AUTH_FACTOR_TYPE_FINGERPRINT);
  auth_session->TerminateAuthFactor(terminate_request,
                                    terminate_future.GetCallback());
  // Verify.
  ASSERT_THAT(terminate_future.Get(), IsOk());

  // This time, the rate-limiter doesn't need to be created anymore.
  EXPECT_CALL(*bio_processor_, StartEnrollSession(_))
      .WillOnce([](auto&& callback) { std::move(callback).Run(true); });

  // Test.
  TestFuture<CryptohomeStatus> prepare_future2;
  user_data_auth::PrepareAuthFactorRequest prepare_request2;
  prepare_request2.set_auth_session_id(auth_session->serialized_token());
  prepare_request2.set_auth_factor_type(
      user_data_auth::AUTH_FACTOR_TYPE_FINGERPRINT);
  prepare_request2.set_purpose(user_data_auth::PURPOSE_ADD_AUTH_FACTOR);
  auth_session->PrepareAuthFactor(prepare_request2,
                                  prepare_future2.GetCallback());
  // Verify.
  ASSERT_THAT(prepare_future2.Get(), IsOk());
}

// Test adding two fingerprint auth factors and authenticating them.
TEST_F(AuthSessionWithUssTest, AddFingerprintAndAuth) {
  const brillo::SecureBlob kFakeAuthPin(32, 1), kFakeAuthSecret(32, 2);
  auto mock_le_manager = std::make_unique<MockLECredentialManager>();
  crypto_.set_le_manager_for_testing(std::move(mock_le_manager));
  // Setup.
  AuthSession auth_session({.username = kFakeUsername,
                            .is_ephemeral_user = false,
                            .intent = AuthIntent::kDecrypt,
                            .auth_factor_status_update_timer =
                                std::make_unique<base::WallClockTimer>(),
                            .user_exists = false,
                            .auth_factor_map = AuthFactorMap()},
                           backing_apis_);

  // Creating the user.
  EXPECT_TRUE(auth_session.OnUserCreated().ok());
  EXPECT_TRUE(auth_session.has_user_secret_stash());

  // Prepare is necessary to create the rate-limiter.
  EXPECT_CALL(hwsec_pw_manager_, InsertRateLimiter)
      .WillOnce(ReturnValue(kFakeRateLimiterLabel));
  EXPECT_CALL(*bio_processor_, StartEnrollSession(_))
      .WillOnce([](auto&& callback) { std::move(callback).Run(true); });
  TestFuture<CryptohomeStatus> prepare_future;
  user_data_auth::PrepareAuthFactorRequest prepare_request;
  prepare_request.set_auth_session_id(auth_session.serialized_token());
  prepare_request.set_auth_factor_type(
      user_data_auth::AUTH_FACTOR_TYPE_FINGERPRINT);
  prepare_request.set_purpose(user_data_auth::PURPOSE_ADD_AUTH_FACTOR);
  auth_session.PrepareAuthFactor(prepare_request, prepare_future.GetCallback());
  ASSERT_THAT(prepare_future.Get(), IsOk());

  EXPECT_EQ(AddFirstFingerprintAuthFactor(auth_session),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  EXPECT_EQ(AddSecondFingerprintAuthFactor(auth_session),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);

  EXPECT_CALL(auth_block_utility_, GetAuthBlockTypeFromState(_))
      .WillRepeatedly(Return(AuthBlockType::kFingerprint));
  EXPECT_CALL(auth_block_utility_, SelectAuthFactorWithAuthBlock(
                                       AuthBlockType::kFingerprint, _, _, _))
      .WillRepeatedly([&](AuthBlockType auth_block_type,
                          const AuthInput& auth_input,
                          std::vector<AuthFactor> auth_factors,
                          AuthBlock::SelectFactorCallback select_callback) {
        ASSERT_TRUE(auth_input.rate_limiter_label.has_value());
        EXPECT_EQ(auth_input.rate_limiter_label.value(), kFakeRateLimiterLabel);
        EXPECT_EQ(auth_factors.size(), 2);

        AuthInput ret_auth_input{
            .user_input = kFakeAuthPin,
            .fingerprint_auth_input =
                FingerprintAuthInput{
                    .auth_secret = kFakeAuthSecret,
                },
        };

        // Assume the second auth factor is matched.
        std::move(select_callback)
            .Run(OkStatus<CryptohomeCryptoError>(), ret_auth_input,
                 auth_factors[1]);
      });
  EXPECT_CALL(auth_block_utility_,
              DeriveKeyBlobsWithAuthBlock(AuthBlockType::kFingerprint, _, _, _))
      .WillRepeatedly([&](AuthBlockType auth_block_type,
                          const AuthInput& auth_input,
                          const AuthBlockState& auth_state,
                          AuthBlock::DeriveCallback derive_callback) {
        ASSERT_TRUE(auth_input.user_input.has_value());
        ASSERT_TRUE(auth_input.fingerprint_auth_input.has_value());
        ASSERT_TRUE(auth_input.fingerprint_auth_input->auth_secret.has_value());
        EXPECT_EQ(auth_input.user_input.value(), kFakeAuthPin);
        EXPECT_EQ(auth_input.fingerprint_auth_input->auth_secret.value(),
                  kFakeAuthSecret);
        ASSERT_TRUE(std::holds_alternative<FingerprintAuthBlockState>(
            auth_state.state));
        auto& state = std::get<FingerprintAuthBlockState>(auth_state.state);
        EXPECT_EQ(state.template_id, kFakeSecondRecordId);

        auto key_blobs = std::make_unique<KeyBlobs>();
        key_blobs->vkk_key = brillo::SecureBlob(kFakeSecondVkkKey);

        std::move(derive_callback)
            .Run(OkStatus<CryptohomeCryptoError>(), std::move(key_blobs),
                 std::nullopt);
      });
  // Set expectations that fingerprint credential leaves with non-zero wrong
  // auth attempts will be reset after a successful authentication.
  EXPECT_CALL(hwsec_pw_manager_, GetWrongAuthAttempts(kFakeFpLabel))
      .Times(2)
      .WillRepeatedly(ReturnValue(1));
  EXPECT_CALL(hwsec_pw_manager_, GetWrongAuthAttempts(kFakeSecondFpLabel))
      .Times(2)
      .WillRepeatedly(ReturnValue(0));
  EXPECT_CALL(hwsec_pw_manager_,
              ResetCredential(kFakeRateLimiterLabel, _,
                              hwsec::PinWeaverManagerFrontend::ResetType::
                                  kWrongAttemptsAndExpirationTime))
      .Times(2);
  EXPECT_CALL(hwsec_pw_manager_,
              ResetCredential(
                  kFakeFpLabel, _,
                  hwsec::PinWeaverManagerFrontend::ResetType::kWrongAttempts))
      .Times(2);
  EXPECT_CALL(hwsec_pw_manager_, ResetCredential(kFakeSecondFpLabel, _, _))
      .Times(0);

  // Test.
  std::vector<std::string> auth_factor_labels{kFakeFingerprintLabel,
                                              kFakeSecondFingerprintLabel};
  user_data_auth::AuthInput auth_input_proto;
  auth_input_proto.mutable_fingerprint_input();
  AuthSession verify_session(
      {.username = kFakeUsername,
       .is_ephemeral_user = false,
       .intent = AuthIntent::kVerifyOnly,
       .auth_factor_status_update_timer =
           std::make_unique<base::WallClockTimer>(),
       .user_exists = true,
       .auth_factor_map = AfMapBuilder()
                              .WithUss()
                              .AddCopiesFromMap(auth_session.auth_factor_map())
                              .Consume()},
      backing_apis_);
  AuthenticateTestFuture verify_future;
  SerializedUserAuthFactorTypePolicy auth_factor_type_policy(
      {.type = *SerializeAuthFactorType(
           *DetermineFactorTypeFromAuthInput(auth_input_proto)),
       .enabled_intents = {},
       .disabled_intents = {}});
  verify_session.AuthenticateAuthFactor(
      ToAuthenticateRequest(auth_factor_labels, auth_input_proto),
      auth_factor_type_policy, verify_future.GetCallback());
  AuthenticateTestFuture decrypt_future_without_policy,
      decrypt_future_with_policy;
  AuthSession decrypt_session1(
      {.username = kFakeUsername,
       .is_ephemeral_user = false,
       .intent = AuthIntent::kDecrypt,
       .auth_factor_status_update_timer =
           std::make_unique<base::WallClockTimer>(),
       .user_exists = true,
       .auth_factor_map = AfMapBuilder()
                              .WithUss()
                              .AddCopiesFromMap(auth_session.auth_factor_map())
                              .Consume()},
      backing_apis_);
  decrypt_session1.AuthenticateAuthFactor(
      ToAuthenticateRequest(auth_factor_labels, auth_input_proto),
      auth_factor_type_policy, decrypt_future_without_policy.GetCallback());
  AuthSession decrypt_session2(
      {.username = kFakeUsername,
       .is_ephemeral_user = false,
       .intent = AuthIntent::kDecrypt,
       .auth_factor_status_update_timer =
           std::make_unique<base::WallClockTimer>(),
       .user_exists = true,
       .auth_factor_map = AfMapBuilder()
                              .WithUss()
                              .AddCopiesFromMap(auth_session.auth_factor_map())
                              .Consume()},
      backing_apis_);
  SerializedUserAuthFactorTypePolicy user_policy(
      {.type = SerializedAuthFactorType::kFingerprint,
       .enabled_intents = {SerializedAuthIntent::kDecrypt},
       .disabled_intents = {}});
  decrypt_session2.AuthenticateAuthFactor(
      ToAuthenticateRequest(auth_factor_labels, auth_input_proto), user_policy,
      decrypt_future_with_policy.GetCallback());
  // Verify.
  auto [action, status] = verify_future.Take();
  EXPECT_EQ(action.action_type, AuthSession::PostAuthActionType::kNone);
  EXPECT_THAT(status, IsOk());
  EXPECT_THAT(verify_session.authorized_intents(),
              UnorderedElementsAre(AuthIntent::kVerifyOnly));
  std::tie(action, status) = decrypt_future_without_policy.Take();
  EXPECT_EQ(action.action_type, AuthSession::PostAuthActionType::kNone);
  EXPECT_THAT(status, NotOk());
  EXPECT_THAT(decrypt_session1.authorized_intents(), IsEmpty());
  std::tie(action, status) = decrypt_future_with_policy.Take();
  EXPECT_EQ(action.action_type, AuthSession::PostAuthActionType::kNone);
  EXPECT_THAT(status, IsOk());
  EXPECT_THAT(
      decrypt_session2.authorized_intents(),
      UnorderedElementsAre(AuthIntent::kDecrypt, AuthIntent::kVerifyOnly));
}

TEST_F(AuthSessionWithUssTest, RelabelAuthFactor) {
  AuthSession auth_session({.username = kFakeUsername,
                            .is_ephemeral_user = false,
                            .intent = AuthIntent::kDecrypt,
                            .auth_factor_status_update_timer =
                                std::make_unique<base::WallClockTimer>(),
                            .user_exists = false,
                            .auth_factor_map = AuthFactorMap()},
                           backing_apis_);

  // Creating the user.
  EXPECT_TRUE(auth_session.OnUserCreated().ok());
  EXPECT_TRUE(auth_session.has_user_secret_stash());

  user_data_auth::CryptohomeErrorCode error =
      user_data_auth::CRYPTOHOME_ERROR_NOT_SET;

  // Calling AddAuthFactor.
  error = AddPasswordAuthFactor(kFakeLabel, kFakePass, /*first_factor=*/true,
                                auth_session);
  EXPECT_EQ(error, user_data_auth::CRYPTOHOME_ERROR_NOT_SET);

  // Test.

  // Calling RelabelAuthFactor.
  error = RelabelAuthFactor(kFakeLabel, kFakeOtherLabel, auth_session);
  EXPECT_EQ(error, user_data_auth::CRYPTOHOME_ERROR_NOT_SET);

  // Calling AuthenticateAuthFactor works with the new label.
  error =
      AuthenticatePasswordAuthFactor(kFakeOtherLabel, kFakePass, auth_session);
  EXPECT_EQ(error, user_data_auth::CRYPTOHOME_ERROR_NOT_SET);

  // The relabel should also be reflected in the session verifiers.
  UserSession* user_session = FindOrCreateUserSession(kFakeUsername);
  EXPECT_THAT(user_session->GetCredentialVerifiers(),
              UnorderedElementsAre(IsVerifierPtrWithLabelAndPassword(
                  kFakeOtherLabel, kFakePass)));
}

TEST_F(AuthSessionWithUssTest, RelabelAuthFactorWithBadInputs) {
  AuthSession auth_session({.username = kFakeUsername,
                            .is_ephemeral_user = false,
                            .intent = AuthIntent::kDecrypt,
                            .auth_factor_status_update_timer =
                                std::make_unique<base::WallClockTimer>(),
                            .user_exists = false,
                            .auth_factor_map = AuthFactorMap()},
                           backing_apis_);

  // Creating the user.
  EXPECT_TRUE(auth_session.OnUserCreated().ok());
  EXPECT_TRUE(auth_session.has_user_secret_stash());

  user_data_auth::CryptohomeErrorCode error =
      user_data_auth::CRYPTOHOME_ERROR_NOT_SET;

  // Add a couple of auth factors.
  error = AddPasswordAuthFactor(kFakeLabel, kFakePass, /*first_factor=*/true,
                                auth_session);
  EXPECT_EQ(error, user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  error = AddPinAuthFactor(kFakePin, auth_session);
  EXPECT_EQ(error, user_data_auth::CRYPTOHOME_ERROR_NOT_SET);

  // Test.

  // Trying to relabel an empty label.
  {
    user_data_auth::RelabelAuthFactorRequest request;
    request.set_auth_session_id(auth_session.serialized_token());
    request.set_new_auth_factor_label(kFakeOtherLabel);

    TestFuture<CryptohomeStatus> relabel_future;
    auth_session.GetAuthForDecrypt()->RelabelAuthFactor(
        request, relabel_future.GetCallback());
    ASSERT_THAT(relabel_future.Get(), NotOk());
    EXPECT_THAT(relabel_future.Get()->local_legacy_error(),
                Optional(user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
  }

  // Trying to relabel to an empty label.
  {
    user_data_auth::RelabelAuthFactorRequest request;
    request.set_auth_session_id(auth_session.serialized_token());
    request.set_auth_factor_label(kFakeLabel);

    TestFuture<CryptohomeStatus> relabel_future;
    auth_session.GetAuthForDecrypt()->RelabelAuthFactor(
        request, relabel_future.GetCallback());
    ASSERT_THAT(relabel_future.Get(), NotOk());
    EXPECT_THAT(relabel_future.Get()->local_legacy_error(),
                Optional(user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
  }

  // Trying to relabel a factor that doesn't exist.
  {
    user_data_auth::RelabelAuthFactorRequest request;
    request.set_auth_session_id(auth_session.serialized_token());
    request.set_auth_factor_label(std::string(kFakeLabel) + "DoesNotExist");
    request.set_new_auth_factor_label(kFakeOtherLabel);

    TestFuture<CryptohomeStatus> relabel_future;
    auth_session.GetAuthForDecrypt()->RelabelAuthFactor(
        request, relabel_future.GetCallback());
    ASSERT_THAT(relabel_future.Get(), NotOk());
    EXPECT_THAT(relabel_future.Get()->local_legacy_error(),
                Optional(user_data_auth::CRYPTOHOME_ERROR_KEY_NOT_FOUND));
  }

  // Trying to relabel a factor to a label that already exists.
  {
    user_data_auth::RelabelAuthFactorRequest request;
    request.set_auth_session_id(auth_session.serialized_token());
    request.set_auth_factor_label(kFakeLabel);
    request.set_new_auth_factor_label(kFakePinLabel);

    TestFuture<CryptohomeStatus> relabel_future;
    auth_session.GetAuthForDecrypt()->RelabelAuthFactor(
        request, relabel_future.GetCallback());
    ASSERT_THAT(relabel_future.Get(), NotOk());
    EXPECT_THAT(relabel_future.Get()->local_legacy_error(),
                Optional(user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
  }

  // Trying to relabel a factor to itself.
  {
    user_data_auth::RelabelAuthFactorRequest request;
    request.set_auth_session_id(auth_session.serialized_token());
    request.set_auth_factor_label(kFakeLabel);
    request.set_new_auth_factor_label(kFakeLabel);

    TestFuture<CryptohomeStatus> relabel_future;
    auth_session.GetAuthForDecrypt()->RelabelAuthFactor(
        request, relabel_future.GetCallback());
    ASSERT_THAT(relabel_future.Get(), NotOk());
    EXPECT_THAT(relabel_future.Get()->local_legacy_error(),
                Optional(user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
  }
}

TEST_F(AuthSessionWithUssTest, RelabelAuthFactorWithFileFailure) {
  AuthSession auth_session({.username = kFakeUsername,
                            .is_ephemeral_user = false,
                            .intent = AuthIntent::kDecrypt,
                            .auth_factor_status_update_timer =
                                std::make_unique<base::WallClockTimer>(),
                            .user_exists = false,
                            .auth_factor_map = AuthFactorMap()},
                           backing_apis_);

  // Creating the user.
  EXPECT_TRUE(auth_session.OnUserCreated().ok());
  EXPECT_TRUE(auth_session.has_user_secret_stash());

  user_data_auth::CryptohomeErrorCode error =
      user_data_auth::CRYPTOHOME_ERROR_NOT_SET;

  // Add a couple of auth factors.
  error = AddPasswordAuthFactor(kFakeLabel, kFakePass, /*first_factor=*/true,
                                auth_session);
  EXPECT_EQ(error, user_data_auth::CRYPTOHOME_ERROR_NOT_SET);

  // Disable the writing of the USS file. The rename should fail and we should
  // still be able to use the old name.
  EXPECT_CALL(platform_, WriteFileAtomicDurable(_, _, _))
      .WillRepeatedly(DoDefault());
  EXPECT_CALL(platform_,
              WriteFileAtomicDurable(
                  UserSecretStashPath(SanitizeUserName(kFakeUsername),
                                      kUserSecretStashDefaultSlot),
                  _, _))
      .Times(AtLeast(1))
      .WillRepeatedly(Return(false));

  // Test.

  // Trying to relabel an empty label.
  user_data_auth::RelabelAuthFactorRequest request;
  request.set_auth_session_id(auth_session.serialized_token());
  request.set_auth_factor_label(kFakeLabel);
  request.set_new_auth_factor_label(kFakeOtherLabel);

  TestFuture<CryptohomeStatus> relabel_future;
  auth_session.GetAuthForDecrypt()->RelabelAuthFactor(
      request, relabel_future.GetCallback());
  ASSERT_THAT(relabel_future.Get(), NotOk());

  // Calling AuthenticateAuthFactor works with the old label.
  error = AuthenticatePasswordAuthFactor(kFakeLabel, kFakePass, auth_session);
  EXPECT_EQ(error, user_data_auth::CRYPTOHOME_ERROR_NOT_SET);

  // The session verifiers should still be under the old label.
  UserSession* user_session = FindOrCreateUserSession(kFakeUsername);
  EXPECT_THAT(user_session->GetCredentialVerifiers(),
              UnorderedElementsAre(
                  IsVerifierPtrWithLabelAndPassword(kFakeLabel, kFakePass)));
}

TEST_F(AuthSessionWithUssTest, RelabelAuthFactorEphemeral) {
  AuthSession auth_session({.username = kFakeUsername,
                            .is_ephemeral_user = true,
                            .intent = AuthIntent::kDecrypt,
                            .auth_factor_status_update_timer =
                                std::make_unique<base::WallClockTimer>(),
                            .user_exists = false,
                            .auth_factor_map = AuthFactorMap()},
                           backing_apis_);

  // Creating the user.
  EXPECT_TRUE(auth_session.OnUserCreated().ok());
  EXPECT_FALSE(auth_session.has_user_secret_stash());

  user_data_auth::CryptohomeErrorCode error =
      user_data_auth::CRYPTOHOME_ERROR_NOT_SET;

  // Add the initial auth factor.
  user_data_auth::AddAuthFactorRequest request;
  request.set_auth_session_id(auth_session.serialized_token());
  user_data_auth::AuthFactor& request_factor = *request.mutable_auth_factor();
  request_factor.set_type(user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
  request_factor.set_label(kFakeLabel);
  request_factor.mutable_password_metadata();
  request.mutable_auth_input()->mutable_password_input()->set_secret(kFakePass);
  TestFuture<CryptohomeStatus> add_future;
  auth_session.GetAuthForDecrypt()->AddAuthFactor(request,
                                                  add_future.GetCallback());
  EXPECT_THAT(add_future.Get(), IsOk());

  // Test.

  // Calling RelabelAuthFactor.
  error = RelabelAuthFactor(kFakeLabel, kFakeOtherLabel, auth_session);
  EXPECT_EQ(error, user_data_auth::CRYPTOHOME_ERROR_NOT_SET);

  // The relabel should be reflected in the session verifiers.
  UserSession* user_session = FindOrCreateUserSession(kFakeUsername);
  EXPECT_THAT(user_session->GetCredentialVerifiers(),
              UnorderedElementsAre(IsVerifierPtrWithLabelAndPassword(
                  kFakeOtherLabel, kFakePass)));
}

TEST_F(AuthSessionWithUssTest, ReplaceAuthFactor) {
  AuthSession auth_session({.username = kFakeUsername,
                            .is_ephemeral_user = false,
                            .intent = AuthIntent::kDecrypt,
                            .auth_factor_status_update_timer =
                                std::make_unique<base::WallClockTimer>(),
                            .user_exists = false,
                            .auth_factor_map = AuthFactorMap()},
                           backing_apis_);

  // Creating the user.
  EXPECT_TRUE(auth_session.OnUserCreated().ok());
  EXPECT_TRUE(auth_session.has_user_secret_stash());

  // Add the initial auth factor.
  user_data_auth::CryptohomeErrorCode error = AddPasswordAuthFactor(
      kFakeLabel, kFakePass, /*first_factor=*/true, auth_session);
  EXPECT_EQ(error, user_data_auth::CRYPTOHOME_ERROR_NOT_SET);

  // Test.

  // Calling ReplaceAuthFactor.
  EXPECT_CALL(auth_block_utility_, SelectAuthBlockTypeForCreation(_))
      .WillRepeatedly(ReturnValue(AuthBlockType::kTpmBoundToPcr));
  EXPECT_CALL(auth_block_utility_,
              CreateKeyBlobsWithAuthBlock(AuthBlockType::kTpmBoundToPcr, _, _))
      .WillOnce([](AuthBlockType auth_block_type, const AuthInput& auth_input,
                   AuthBlock::CreateCallback create_callback) {
        // Make an arbitrary auth block state type can be used in this test.
        auto key_blobs = std::make_unique<KeyBlobs>();
        key_blobs->vkk_key =
            GetFakeDerivedSecret(auth_input.user_input.value());
        auto auth_block_state = std::make_unique<AuthBlockState>();
        auth_block_state->state = TpmBoundToPcrAuthBlockState();
        std::move(create_callback)
            .Run(OkStatus<CryptohomeCryptoError>(), std::move(key_blobs),
                 std::move(auth_block_state));
      });
  user_data_auth::ReplaceAuthFactorRequest replace_request;
  replace_request.set_auth_session_id(auth_session.serialized_token());
  replace_request.set_auth_factor_label(kFakeLabel);
  replace_request.mutable_auth_factor()->set_type(
      user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
  replace_request.mutable_auth_factor()->set_label(kFakeOtherLabel);
  replace_request.mutable_auth_factor()->mutable_password_metadata();
  replace_request.mutable_auth_input()->mutable_password_input()->set_secret(
      kFakeOtherPass);
  TestFuture<CryptohomeStatus> replace_future;
  auth_session.GetAuthForDecrypt()->ReplaceAuthFactor(
      replace_request, replace_future.GetCallback());
  EXPECT_THAT(replace_future.Get(), IsOk());

  // Calling AuthenticateAuthFactor works with the new label.
  error = AuthenticatePasswordAuthFactor(kFakeOtherLabel, kFakeOtherPass,
                                         auth_session);
  EXPECT_EQ(error, user_data_auth::CRYPTOHOME_ERROR_NOT_SET);

  // The replace should be reflected in the session verifiers.
  UserSession* user_session = FindOrCreateUserSession(kFakeUsername);
  EXPECT_THAT(user_session->GetCredentialVerifiers(),
              UnorderedElementsAre(IsVerifierPtrWithLabelAndPassword(
                  kFakeOtherLabel, kFakeOtherPass)));
}

TEST_F(AuthSessionWithUssTest, ReplaceAuthFactorWithBadInputs) {
  AuthSession auth_session({.username = kFakeUsername,
                            .is_ephemeral_user = false,
                            .intent = AuthIntent::kDecrypt,
                            .auth_factor_status_update_timer =
                                std::make_unique<base::WallClockTimer>(),
                            .user_exists = false,
                            .auth_factor_map = AuthFactorMap()},
                           backing_apis_);

  // Creating the user.
  EXPECT_TRUE(auth_session.OnUserCreated().ok());
  EXPECT_TRUE(auth_session.has_user_secret_stash());

  user_data_auth::CryptohomeErrorCode error =
      user_data_auth::CRYPTOHOME_ERROR_NOT_SET;

  // Add a couple of auth factors.
  error = AddPasswordAuthFactor(kFakeLabel, kFakePass, /*first_factor=*/true,
                                auth_session);
  EXPECT_EQ(error, user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  error = AddPinAuthFactor(kFakePin, auth_session);
  EXPECT_EQ(error, user_data_auth::CRYPTOHOME_ERROR_NOT_SET);

  // Test.

  // Standard request parts. All the various tests mess around with the labels.
  user_data_auth::ReplaceAuthFactorRequest request;
  request.set_auth_session_id(auth_session.serialized_token());
  request.mutable_auth_factor()->set_type(
      user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
  request.mutable_auth_factor()->mutable_password_metadata();
  request.mutable_auth_input()->mutable_password_input()->set_secret(
      kFakeOtherPass);

  // Trying to replace an empty label.
  {
    request.set_auth_factor_label("");
    request.mutable_auth_factor()->set_label(kFakeOtherLabel);

    TestFuture<CryptohomeStatus> future;
    auth_session.GetAuthForDecrypt()->ReplaceAuthFactor(request,
                                                        future.GetCallback());
    ASSERT_THAT(future.Get(), NotOk());
    EXPECT_THAT(future.Get()->local_legacy_error(),
                Optional(user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
  }

  // Trying to replace to an empty label.
  {
    request.set_auth_factor_label(kFakeLabel);
    request.mutable_auth_factor()->set_label("");

    TestFuture<CryptohomeStatus> future;
    auth_session.GetAuthForDecrypt()->ReplaceAuthFactor(request,
                                                        future.GetCallback());
    ASSERT_THAT(future.Get(), NotOk());
    EXPECT_THAT(future.Get()->local_legacy_error(),
                Optional(user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
  }

  // Trying to replace a factor that doesn't exist.
  {
    request.set_auth_factor_label(std::string(kFakeLabel) + "DoesNotExist");
    request.mutable_auth_factor()->set_label(kFakeOtherLabel);

    TestFuture<CryptohomeStatus> future;
    auth_session.GetAuthForDecrypt()->ReplaceAuthFactor(request,
                                                        future.GetCallback());
    ASSERT_THAT(future.Get(), NotOk());
    EXPECT_THAT(future.Get()->local_legacy_error(),
                Optional(user_data_auth::CRYPTOHOME_ERROR_KEY_NOT_FOUND));
  }

  // Trying to replace a factor to a label that already exists.
  {
    request.set_auth_factor_label(kFakeLabel);
    request.mutable_auth_factor()->set_label(kFakePinLabel);

    TestFuture<CryptohomeStatus> future;
    auth_session.GetAuthForDecrypt()->ReplaceAuthFactor(request,
                                                        future.GetCallback());
    ASSERT_THAT(future.Get(), NotOk());
    EXPECT_THAT(future.Get()->local_legacy_error(),
                Optional(user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
  }

  // Trying to replace a factor to itself.
  {
    request.set_auth_factor_label(kFakeLabel);
    request.mutable_auth_factor()->set_label(kFakeLabel);

    TestFuture<CryptohomeStatus> future;
    auth_session.GetAuthForDecrypt()->ReplaceAuthFactor(request,
                                                        future.GetCallback());
    ASSERT_THAT(future.Get(), NotOk());
    EXPECT_THAT(future.Get()->local_legacy_error(),
                Optional(user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
  }
}

TEST_F(AuthSessionWithUssTest, ReplaceAuthFactorWithFailedAdd) {
  AuthSession auth_session({.username = kFakeUsername,
                            .is_ephemeral_user = false,
                            .intent = AuthIntent::kDecrypt,
                            .auth_factor_status_update_timer =
                                std::make_unique<base::WallClockTimer>(),
                            .user_exists = false,
                            .auth_factor_map = AuthFactorMap()},
                           backing_apis_);

  // Creating the user.
  EXPECT_TRUE(auth_session.OnUserCreated().ok());
  EXPECT_TRUE(auth_session.has_user_secret_stash());

  // Add the initial auth factor.
  EXPECT_CALL(auth_block_utility_, SelectAuthBlockTypeForCreation(_))
      .WillRepeatedly(ReturnValue(AuthBlockType::kTpmBoundToPcr));
  EXPECT_CALL(auth_block_utility_,
              CreateKeyBlobsWithAuthBlock(AuthBlockType::kTpmBoundToPcr, _, _))
      .WillOnce([](AuthBlockType auth_block_type, const AuthInput& auth_input,
                   AuthBlock::CreateCallback create_callback) {
        // Make an arbitrary auth block state type can be used in this test.
        auto key_blobs = std::make_unique<KeyBlobs>();
        key_blobs->vkk_key =
            GetFakeDerivedSecret(auth_input.user_input.value());
        auto auth_block_state = std::make_unique<AuthBlockState>();
        auth_block_state->state = TpmBoundToPcrAuthBlockState();
        std::move(create_callback)
            .Run(OkStatus<CryptohomeCryptoError>(), std::move(key_blobs),
                 std::move(auth_block_state));
      });
  user_data_auth::AddAuthFactorRequest add_request;
  add_request.set_auth_session_id(auth_session.serialized_token());
  user_data_auth::AuthFactor& request_factor =
      *add_request.mutable_auth_factor();
  request_factor.set_type(user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
  request_factor.set_label(kFakeLabel);
  request_factor.mutable_password_metadata();
  add_request.mutable_auth_input()->mutable_password_input()->set_secret(
      kFakePass);
  TestFuture<CryptohomeStatus> add_future;
  auth_session.GetAuthForDecrypt()->AddAuthFactor(add_request,
                                                  add_future.GetCallback());
  EXPECT_THAT(add_future.Get(), IsOk());

  // Test.

  // Calling ReplaceAuthFactor. Have the key blob creation fail.
  EXPECT_CALL(auth_block_utility_, CreateKeyBlobsWithAuthBlock(_, _, _))
      .WillOnce([](auto, auto, AuthBlock::CreateCallback create_callback) {
        std::move(create_callback)
            .Run(MakeStatus<CryptohomeCryptoError>(
                     kErrorLocationForTestingAuthSession,
                     error::ErrorActionSet(
                         {error::PossibleAction::kDevCheckUnexpectedState}),
                     CryptoError::CE_OTHER_CRYPTO),
                 nullptr, nullptr);
      });
  user_data_auth::ReplaceAuthFactorRequest replace_request;
  replace_request.set_auth_session_id(auth_session.serialized_token());
  replace_request.set_auth_factor_label(kFakeLabel);
  replace_request.mutable_auth_factor()->set_type(
      user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
  replace_request.mutable_auth_factor()->set_label(kFakeOtherLabel);
  replace_request.mutable_auth_factor()->mutable_password_metadata();
  replace_request.mutable_auth_input()->mutable_password_input()->set_secret(
      kFakeOtherPass);
  TestFuture<CryptohomeStatus> replace_future;
  auth_session.GetAuthForDecrypt()->ReplaceAuthFactor(
      replace_request, replace_future.GetCallback());
  EXPECT_THAT(replace_future.Get(), NotOk());

  // Calling AuthenticateAuthFactor still works with the old label.
  user_data_auth::CryptohomeErrorCode error =
      AuthenticatePasswordAuthFactor(kFakeLabel, kFakePass, auth_session);
  EXPECT_EQ(error, user_data_auth::CRYPTOHOME_ERROR_NOT_SET);

  // The replace should not show up in the session verifiers.
  UserSession* user_session = FindOrCreateUserSession(kFakeUsername);
  EXPECT_THAT(user_session->GetCredentialVerifiers(),
              UnorderedElementsAre(
                  IsVerifierPtrWithLabelAndPassword(kFakeLabel, kFakePass)));
}

TEST_F(AuthSessionWithUssTest, ReplaceAuthFactorWithFileFailure) {
  AuthSession auth_session({.username = kFakeUsername,
                            .is_ephemeral_user = false,
                            .intent = AuthIntent::kDecrypt,
                            .auth_factor_status_update_timer =
                                std::make_unique<base::WallClockTimer>(),
                            .user_exists = false,
                            .auth_factor_map = AuthFactorMap()},
                           backing_apis_);

  // Creating the user.
  EXPECT_TRUE(auth_session.OnUserCreated().ok());
  EXPECT_TRUE(auth_session.has_user_secret_stash());

  // Add the initial auth factor.
  EXPECT_CALL(auth_block_utility_, SelectAuthBlockTypeForCreation(_))
      .WillRepeatedly(ReturnValue(AuthBlockType::kTpmBoundToPcr));
  EXPECT_CALL(auth_block_utility_,
              CreateKeyBlobsWithAuthBlock(AuthBlockType::kTpmBoundToPcr, _, _))
      .WillOnce([](AuthBlockType auth_block_type, const AuthInput& auth_input,
                   AuthBlock::CreateCallback create_callback) {
        // Make an arbitrary auth block state type can be used in this test.
        auto key_blobs = std::make_unique<KeyBlobs>();
        key_blobs->vkk_key =
            GetFakeDerivedSecret(auth_input.user_input.value());
        auto auth_block_state = std::make_unique<AuthBlockState>();
        auth_block_state->state = TpmBoundToPcrAuthBlockState();
        std::move(create_callback)
            .Run(OkStatus<CryptohomeCryptoError>(), std::move(key_blobs),
                 std::move(auth_block_state));
      });
  user_data_auth::AddAuthFactorRequest add_request;
  add_request.set_auth_session_id(auth_session.serialized_token());
  user_data_auth::AuthFactor& request_factor =
      *add_request.mutable_auth_factor();
  request_factor.set_type(user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
  request_factor.set_label(kFakeLabel);
  request_factor.mutable_password_metadata();
  add_request.mutable_auth_input()->mutable_password_input()->set_secret(
      kFakePass);
  TestFuture<CryptohomeStatus> add_future;
  auth_session.GetAuthForDecrypt()->AddAuthFactor(add_request,
                                                  add_future.GetCallback());
  EXPECT_THAT(add_future.Get(), IsOk());

  // Test.

  // Disable the writing of the USS file. The replace should fail and we should
  // still be able to use the old name.
  EXPECT_CALL(platform_, WriteFileAtomicDurable(_, _, _))
      .WillRepeatedly(DoDefault());
  EXPECT_CALL(platform_,
              WriteFileAtomicDurable(
                  UserSecretStashPath(SanitizeUserName(kFakeUsername),
                                      kUserSecretStashDefaultSlot),
                  _, _))
      .Times(AtLeast(1))
      .WillRepeatedly(Return(false));

  // Calling ReplaceAuthFactor. The key blob creation will succeed but adding
  // the new factor into USS will not.
  EXPECT_CALL(auth_block_utility_,
              CreateKeyBlobsWithAuthBlock(AuthBlockType::kTpmBoundToPcr, _, _))
      .WillOnce([](AuthBlockType auth_block_type, const AuthInput& auth_input,
                   AuthBlock::CreateCallback create_callback) {
        // Make an arbitrary auth block state type can be used in this test.
        auto key_blobs = std::make_unique<KeyBlobs>();
        key_blobs->vkk_key =
            GetFakeDerivedSecret(auth_input.user_input.value());
        auto auth_block_state = std::make_unique<AuthBlockState>();
        auth_block_state->state = TpmBoundToPcrAuthBlockState();
        std::move(create_callback)
            .Run(OkStatus<CryptohomeCryptoError>(), std::move(key_blobs),
                 std::move(auth_block_state));
      });
  user_data_auth::ReplaceAuthFactorRequest replace_request;
  replace_request.set_auth_session_id(auth_session.serialized_token());
  replace_request.set_auth_factor_label(kFakeLabel);
  replace_request.mutable_auth_factor()->set_type(
      user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
  replace_request.mutable_auth_factor()->set_label(kFakeOtherLabel);
  replace_request.mutable_auth_factor()->mutable_password_metadata();
  replace_request.mutable_auth_input()->mutable_password_input()->set_secret(
      kFakeOtherPass);
  TestFuture<CryptohomeStatus> replace_future;
  auth_session.GetAuthForDecrypt()->ReplaceAuthFactor(
      replace_request, replace_future.GetCallback());
  EXPECT_THAT(replace_future.Get(), NotOk());

  // Calling AuthenticateAuthFactor still works with the old label.
  user_data_auth::CryptohomeErrorCode error =
      AuthenticatePasswordAuthFactor(kFakeLabel, kFakePass, auth_session);
  EXPECT_EQ(error, user_data_auth::CRYPTOHOME_ERROR_NOT_SET);

  // The replace should not show up in the session verifiers.
  UserSession* user_session = FindOrCreateUserSession(kFakeUsername);
  EXPECT_THAT(user_session->GetCredentialVerifiers(),
              UnorderedElementsAre(
                  IsVerifierPtrWithLabelAndPassword(kFakeLabel, kFakePass)));
}

TEST_F(AuthSessionWithUssTest, ReplaceAuthFactorEphemeral) {
  AuthSession auth_session({.username = kFakeUsername,
                            .is_ephemeral_user = true,
                            .intent = AuthIntent::kDecrypt,
                            .auth_factor_status_update_timer =
                                std::make_unique<base::WallClockTimer>(),
                            .user_exists = false,
                            .auth_factor_map = AuthFactorMap()},
                           backing_apis_);

  // Creating the user.
  EXPECT_TRUE(auth_session.OnUserCreated().ok());
  EXPECT_FALSE(auth_session.has_user_secret_stash());

  // Add the initial auth factor.
  user_data_auth::AddAuthFactorRequest add_request;
  add_request.set_auth_session_id(auth_session.serialized_token());
  user_data_auth::AuthFactor& request_factor =
      *add_request.mutable_auth_factor();
  request_factor.set_type(user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
  request_factor.set_label(kFakeLabel);
  request_factor.mutable_password_metadata();
  add_request.mutable_auth_input()->mutable_password_input()->set_secret(
      kFakePass);
  TestFuture<CryptohomeStatus> add_future;
  auth_session.GetAuthForDecrypt()->AddAuthFactor(add_request,
                                                  add_future.GetCallback());
  EXPECT_THAT(add_future.Get(), IsOk());

  // Test.

  // Calling ReplaceAuthFactor.
  user_data_auth::ReplaceAuthFactorRequest replace_request;
  replace_request.set_auth_session_id(auth_session.serialized_token());
  replace_request.set_auth_factor_label(kFakeLabel);
  replace_request.mutable_auth_factor()->set_type(
      user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
  replace_request.mutable_auth_factor()->set_label(kFakeOtherLabel);
  replace_request.mutable_auth_factor()->mutable_password_metadata();
  replace_request.mutable_auth_input()->mutable_password_input()->set_secret(
      kFakeOtherPass);
  TestFuture<CryptohomeStatus> replace_future;
  auth_session.GetAuthForDecrypt()->ReplaceAuthFactor(
      replace_request, replace_future.GetCallback());
  EXPECT_THAT(replace_future.Get(), IsOk());

  // The relabel should be reflected in the session verifiers.
  UserSession* user_session = FindOrCreateUserSession(kFakeUsername);
  EXPECT_THAT(user_session->GetCredentialVerifiers(),
              UnorderedElementsAre(IsVerifierPtrWithLabelAndPassword(
                  kFakeOtherLabel, kFakeOtherPass)));
}

}  // namespace cryptohome
