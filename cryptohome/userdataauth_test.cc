// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/userdataauth.h"

#include <algorithm>
#include <deque>
#include <limits>
#include <map>
#include <memory>
#include <optional>

#include <absl/container/flat_hash_set.h>
#include <base/check.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/location.h>
#include <base/stl_util.h>
#include <base/test/bind.h>
#include <base/test/test_future.h>
#include <base/test/test_mock_time_task_runner.h>
#include <base/time/time.h>
#include <brillo/cryptohome.h>
#include <brillo/errors/error_codes.h>
#include <brillo/secure_blob.h>
#include <chaps/token_manager_client_mock.h>
#include <cryptohome/proto_bindings/auth_factor.pb.h>
#include <cryptohome/proto_bindings/recoverable_key_store.pb.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>
#include <dbus/mock_bus.h>
#include <featured/fake_platform_features.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libhwsec-foundation/crypto/aes.h>
#include <libhwsec-foundation/crypto/libscrypt_compat.h>
#include <libhwsec-foundation/crypto/secure_blob_util.h>
#include <libhwsec-foundation/crypto/sha.h>
#include <libhwsec-foundation/error/testing_helper.h>
#include <libhwsec-foundation/tpm/tpm_version.h>
#include <libhwsec/backend/mock_backend.h>
#include <libhwsec/error/tpm_error.h>
#include <libhwsec/factory/mock_factory.h>
#include <libhwsec/factory/tpm2_simulator_factory_for_test.h>
#include <libhwsec/frontend/cryptohome/mock_frontend.h>
#include <libhwsec/frontend/pinweaver_manager/mock_frontend.h>
#include <libhwsec/frontend/recovery_crypto/mock_frontend.h>
#include <libhwsec/status.h>
#include <libstorage/platform/mock_platform.h>
#include <metrics/metrics_library_mock.h>

#include "cryptohome/auth_blocks/biometrics_auth_block_service.h"
#include "cryptohome/auth_blocks/mock_auth_block_utility.h"
#include "cryptohome/auth_blocks/mock_biometrics_command_processor.h"
#include "cryptohome/auth_factor/metadata.h"
#include "cryptohome/auth_session/manager.h"
#include "cryptohome/auth_session/protobuf.h"
#include "cryptohome/challenge_credentials/mock_challenge_credentials_helper.h"
#include "cryptohome/cleanup/mock_disk_cleanup.h"
#include "cryptohome/cleanup/mock_low_disk_space_handler.h"
#include "cryptohome/cleanup/mock_user_oldest_activity_timestamp_manager.h"
#include "cryptohome/common/print_UserDataAuth_proto.h"
#include "cryptohome/cryptohome_keys_manager.h"
#include "cryptohome/error/cryptohome_error.h"
#include "cryptohome/error/cryptohome_mount_error.h"
#include "cryptohome/fake_features.h"
#include "cryptohome/fake_platform.h"
#include "cryptohome/features.h"
#include "cryptohome/filesystem_layout.h"
#include "cryptohome/flatbuffer_schemas/auth_factor.h"
#include "cryptohome/fp_migration/legacy_record.h"
#include "cryptohome/mock_credential_verifier.h"
#include "cryptohome/mock_cryptohome_keys_manager.h"
#include "cryptohome/mock_fingerprint_manager.h"
#include "cryptohome/mock_key_challenge_service.h"
#include "cryptohome/mock_key_challenge_service_factory.h"
#include "cryptohome/mock_keyset_management.h"
#include "cryptohome/mock_pkcs11_init.h"
#include "cryptohome/mock_signalling.h"
#include "cryptohome/pkcs11/fake_pkcs11_token.h"
#include "cryptohome/pkcs11/mock_pkcs11_token_factory.h"
#include "cryptohome/recoverable_key_store/mock_backend_cert_provider.h"
#include "cryptohome/storage/file_system_keyset.h"
#include "cryptohome/storage/homedirs.h"
#include "cryptohome/storage/mock_homedirs.h"
#include "cryptohome/storage/mock_mount.h"
#include "cryptohome/storage/mock_mount_factory.h"
#include "cryptohome/storage/mount_constants.h"
#include "cryptohome/user_secret_stash/storage.h"
#include "cryptohome/user_session/mock_user_session.h"
#include "cryptohome/user_session/mock_user_session_factory.h"
#include "cryptohome/userdataauth_test_utils.h"
#include "cryptohome/username.h"

namespace cryptohome {
namespace {

using base::FilePath;
using base::test::TestFuture;
using brillo::BlobFromString;
using brillo::SecureBlob;
using brillo::cryptohome::home::GetGuestUsername;
using brillo::cryptohome::home::SanitizeUserName;
using cryptohome::error::CryptohomeCryptoError;
using cryptohome::error::CryptohomeError;
using cryptohome::error::CryptohomeMountError;
using cryptohome::error::CryptohomeTPMError;
using cryptohome::error::ErrorActionSet;
using cryptohome::error::PossibleAction;
using cryptohome::error::PrimaryAction;

using ::hwsec::TPMError;
using ::hwsec::TPMErrorBase;
using ::hwsec::TPMRetryAction;
using ::hwsec_foundation::CreateSecureRandomBlob;
using ::hwsec_foundation::kAesGcm256KeySize;
using ::hwsec_foundation::Sha1;
using ::hwsec_foundation::error::testing::IsOk;
using ::hwsec_foundation::error::testing::NotOk;
using ::hwsec_foundation::error::testing::ReturnError;
using ::hwsec_foundation::error::testing::ReturnOk;
using ::hwsec_foundation::error::testing::ReturnValue;
using ::hwsec_foundation::status::MakeStatus;
using ::hwsec_foundation::status::OkStatus;
using ::hwsec_foundation::status::StatusChain;
using ::testing::_;
using ::testing::AtLeast;
using ::testing::AtMost;
using ::testing::ByMove;
using ::testing::DoAll;
using ::testing::ElementsAre;
using ::testing::ElementsAreArray;
using ::testing::EndsWith;
using ::testing::Eq;
using ::testing::InSequence;
using ::testing::Invoke;
using ::testing::IsEmpty;
using ::testing::IsFalse;
using ::testing::IsNull;
using ::testing::NiceMock;
using ::testing::NotNull;
using ::testing::Pointee;
using ::testing::Property;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::SaveArgPointee;
using ::testing::SetArgPointee;
using ::testing::UnorderedElementsAre;

// Set to match the 5 minute timer and a 1 minute extension in AuthSession.
constexpr int kAuthSessionExtensionDuration = 60;
constexpr auto kAuthSessionTimeout = base::Minutes(5);

// Fake labels to be in used in this test suite.
constexpr char kFakeLabel[] = "test_label";

ACTION_P(SetEphemeralSettings, ephemeral_settings) {
  *arg0 = ephemeral_settings;
  return true;
}

// Matches against user_data_auth::CryptohomeErrorInfo to see if it contains an
// active recommendation for the specified PossibleAction |action|. "Active
// recommendation" here refers to a correct PrimaryAction value such that the
// PossibleAction field is active and not disregarded.
MATCHER_P(HasPossibleAction, action, "") {
  if (arg.primary_action() != user_data_auth::PrimaryAction::PRIMARY_NONE) {
    *result_listener
        << "Invalid PrimaryAction when checking for PossibleAction: "
        << user_data_auth::PrimaryAction_Name(arg.primary_action());
    return false;
  }
  for (int i = 0; i < arg.possible_actions_size(); i++) {
    if (arg.possible_actions(i) == action) {
      return true;
    }
  }

  return false;
}

// Local alias for hash sets of possible actions. Makes the very long name a
// little more consise to use when defining values in tests.
using PossibleActionSet = absl::flat_hash_set<user_data_auth::PossibleAction>;

// Same as multiple invocation of HasPossibleAction. This matcher checks that
// the CryptohomeErrorInfo contains a correct PrimaryAction and the list of
// recommended PossibleAction(s) contains the specified actions. |actions|
// should be set<user_data_auth::PossibleAction>.
MATCHER_P(HasPossibleActions, actions, "") {
  // We need to copy the actions to strip off the constness.
  PossibleActionSet to_match = actions;
  if (arg.primary_action() != user_data_auth::PrimaryAction::PRIMARY_NONE) {
    *result_listener
        << "Invalid PrimaryAction when checking for PossibleAction: "
        << user_data_auth::PrimaryAction_Name(arg.primary_action());
    return false;
  }
  for (int i = 0; i < arg.possible_actions_size(); i++) {
    const auto current_action = arg.possible_actions(i);
    if (to_match.count(current_action) != 0) {
      to_match.erase(current_action);
    }
  }
  for (const auto& action : to_match) {
    *result_listener << "Action " << user_data_auth::PossibleAction_Name(action)
                     << " not found";
  }
  return to_match.size() == 0;
}

}  // namespace

// UserDataAuthTestBase is a test fixture that does not call
// UserDataAuth::Initialize() during setup. Therefore, it's suited to tests that
// can be conducted without calling UserDataAuth::Initialize(), or for tests
// that wants some flexibility before calling UserDataAuth::Initialize(), note
// that in this case the test have to call UserDataAuth::Initialize().
// Note: We shouldn't use this test fixture directly.
class UserDataAuthTestBase : public ::testing::Test {
 public:
  UserDataAuthTestBase() = default;
  UserDataAuthTestBase(const UserDataAuthTestBase&) = delete;
  UserDataAuthTestBase& operator=(const UserDataAuthTestBase&) = delete;

  ~UserDataAuthTestBase() override = default;

  void SetUp() override {
    // Note: If anything is modified/added here, we might need to adjust
    // UserDataAuthApiTest::SetUp() as well.

    SetupDefaultUserDataAuth();
    SetupHwsec();
  }

  void SetupHwsec() {
    userdataauth_->set_device_management_client(&device_management_client_);
    userdataauth_->set_auth_block_utility(&auth_block_utility_);
    userdataauth_->set_challenge_credentials_helper(
        &challenge_credentials_helper_);
    userdataauth_->set_user_session_factory(&user_session_factory_);
  }

  void SetupDefaultUserDataAuth() {
    SET_DEFAULT_TPM_FOR_TESTING;
    dbus::Bus::Options options;
    options.bus_type = dbus::Bus::SYSTEM;
    mount_bus_ = base::MakeRefCounted<NiceMock<dbus::MockBus>>(options);
    ON_CALL(system_apis_.hwsec, IsEnabled()).WillByDefault(ReturnValue(true));
    ON_CALL(system_apis_.hwsec, IsReady()).WillByDefault(ReturnValue(true));
    ON_CALL(system_apis_.hwsec, IsPinWeaverEnabled())
        .WillByDefault(ReturnValue(false));
    ON_CALL(system_apis_.hwsec, IsSealingSupported())
        .WillByDefault(ReturnValue(true));
    ON_CALL(system_apis_.hwsec_pw_manager, IsEnabled())
        .WillByDefault(ReturnValue(true));
    ON_CALL(system_apis_.hwsec_pw_manager, GetVersion())
        .WillByDefault(ReturnValue(2));
    ON_CALL(system_apis_.hwsec_pw_manager, BlockGeneratePk())
        .WillByDefault(ReturnOk<TPMError>());

    if (!userdataauth_) {
      // Note that this branch is usually taken as |userdataauth_| is usually
      // NULL. The reason for this branch is because some derived-class of this
      // class (such as UserDataAuthTestThreaded) need to have the constructor
      // of UserDataAuth run on a specific thread, and therefore will construct
      // |userdataauth_| before calling UserDataAuthTestBase::SetUp().
      userdataauth_ =
          std::make_unique<UserDataAuth>(system_apis_.ToBackingApis());
    }

    userdataauth_->set_homedirs(&homedirs_);
    userdataauth_->set_device_management_client(&device_management_client_);
    userdataauth_->set_chaps_client(&chaps_client_);
    userdataauth_->set_fingerprint_manager(&fingerprint_manager_);
    userdataauth_->set_key_store_cert_provider(&key_store_cert_provider_);
    userdataauth_->set_pkcs11_init(&pkcs11_init_);
    userdataauth_->set_pkcs11_token_factory(&pkcs11_token_factory_);
    userdataauth_->set_key_challenge_service_factory(
        &key_challenge_service_factory_);
    userdataauth_->set_low_disk_space_handler(&low_disk_space_handler_);

    {
      auto mock_processor =
          std::make_unique<NiceMock<MockBiometricsCommandProcessor>>();
      bio_processor_ = mock_processor.get();
      bio_service_ = std::make_unique<BiometricsAuthBlockService>(
          std::move(mock_processor),
          /*enroll_signal_sender=*/base::DoNothing(),
          /*auth_signal_sender=*/base::DoNothing());
    }
    userdataauth_->set_biometrics_service(bio_service_.get());
    userdataauth_->set_features(&features_.object);
    // Empty token list by default.  The effect is that there are no
    // attempts to unload tokens unless a test explicitly sets up the token
    // list.
    ON_CALL(chaps_client_, GetTokenList(_, _)).WillByDefault(Return(true));
    // Skip CleanUpStaleMounts by default.
    ON_CALL(system_apis_.platform, GetMountsBySourcePrefix(_, _))
        .WillByDefault(Return(false));
    // Low Disk space handler initialization will do nothing.
    ON_CALL(low_disk_space_handler_, Init(_)).WillByDefault(Return(true));
    ON_CALL(low_disk_space_handler_, disk_cleanup())
        .WillByDefault(Return(&disk_cleanup_));

    // Make sure FreeDiskSpaceDuringLogin is not called unexpectedly.
    EXPECT_CALL(disk_cleanup_, FreeDiskSpaceDuringLogin(_)).Times(0);
  }

  // Create a new session and store an unowned pointer to it in |session_|.
  std::unique_ptr<NiceMock<MockUserSession>> CreateSessionAndRememberPtr() {
    auto owned_session = std::make_unique<NiceMock<MockUserSession>>();
    session_ = owned_session.get();
    return owned_session;
  }

  // This is a utility function for tests to setup a mount for a particular
  // user. After calling this function, |session_| is available for use.
  void SetupMount(const std::string& username) {
    EXPECT_TRUE(userdataauth_->AddUserSessionForTest(
        Username(username), CreateSessionAndRememberPtr()));
  }

  // This is a helper function that compute the obfuscated username with the
  // fake salt.
  ObfuscatedUsername GetObfuscatedUsername(const Username& username) {
    return SanitizeUserName(username);
  }

  // Helper function for creating a brillo::Error
  static brillo::ErrorPtr CreateDefaultError(const base::Location& from_here) {
    brillo::ErrorPtr error;
    brillo::Error::AddTo(&error, from_here, brillo::errors::dbus::kDomain,
                         DBUS_ERROR_FAILED, "Here's a fake error");
    return error;
  }

 protected:
  // Mock AuthBlockUtility object, will be passed to UserDataAuth for its
  // internal use.
  NiceMock<MockAuthBlockUtility> auth_block_utility_;

  // Mock MockDeviceManagementClientProxy, will be passed to UserDataAuth for
  // its internal use.
  NiceMock<MockDeviceManagementClientProxy> device_management_client_;

  // Mock HomeDirs object, will be passed to UserDataAuth for its internal use.
  NiceMock<MockHomeDirs> homedirs_;

  // Mock DiskCleanup object, will be passed to UserDataAuth for its internal
  // use. Only FreeDiskSpaceDuringLogin should be called and it should not
  // be called more than necessary.
  NiceMock<MockDiskCleanup> disk_cleanup_;

  // Mock system API objects needed to initialize UserDataAuth.
  MockSystemApis<WithMockKeysetManagement> system_apis_;

  // Mock chaps token manager client, will be passed to UserDataAuth for its
  // internal use.
  NiceMock<chaps::TokenManagerClientMock> chaps_client_;

  // Mock PKCS#11 init object, will be passed to UserDataAuth for its internal
  // use.
  NiceMock<MockPkcs11Init> pkcs11_init_;

  // Mock Pcks11TokenFactory, will be passed to UserDataAuth for its internal
  // use.
  NiceMock<MockPkcs11TokenFactory> pkcs11_token_factory_;

  // Mock Fingerprint Manager object, will be passed to UserDataAuth for its
  // internal use.
  NiceMock<MockFingerprintManager> fingerprint_manager_;

  // Mock Recoverable Key Store Backend Cert Provider object, will be passed to
  // UserDataAuth for its internal use.
  NiceMock<MockRecoverableKeyStoreBackendCertProvider> key_store_cert_provider_;

  // Biometrics service object and the mock biometrics command processor object
  // that it is wrapping, the service object will be passed into UserDataAuth.
  NiceMock<MockBiometricsCommandProcessor>* bio_processor_;
  std::unique_ptr<BiometricsAuthBlockService> bio_service_;

  // Mock challenge credential helper utility object, will be passed to
  // UserDataAuth for its internal use.
  NiceMock<MockChallengeCredentialsHelper> challenge_credentials_helper_;

  // Mock factory of key challenge services, will be passed to UserDataAuth for
  // its internal use.
  NiceMock<MockKeyChallengeServiceFactory> key_challenge_service_factory_;

  // Mock User Session Factory object.
  NiceMock<MockUserSessionFactory> user_session_factory_;

  // Mock Low Disk Space handler object, will be passed to UserDataAuth for its
  // internal use.
  NiceMock<MockLowDiskSpaceHandler> low_disk_space_handler_;

  // Mock DBus object on mount thread, will be passed to UserDataAuth for its
  // internal use.
  scoped_refptr<NiceMock<dbus::MockBus>> mount_bus_;

  // Unowned pointer to the session object.
  NiceMock<MockUserSession>* session_ = nullptr;

  // Fake PlatformFeatures object, will be passed to Features for its internal
  // use.
  FakeFeaturesForTesting features_;

  // Declare |userdataauth_| last so it gets destroyed before all the mocks.
  // This is important because otherwise the background thread may call into
  // mocks that have already been destroyed.
  std::unique_ptr<UserDataAuth> userdataauth_;

  const error::CryptohomeError::ErrorLocationPair kErrorLocationPlaceholder =
      error::CryptohomeError::ErrorLocationPair(
          static_cast<::cryptohome::error::CryptohomeError::ErrorLocation>(1),
          "Testing1");
};

// Test fixture that implements two task runners, which is similar to the task
// environment in UserDataAuth. Developers could fast forward the time in
// UserDataAuth, and prevent the flakiness caused by the real time clock. Note
// that this does not initialize |userdataauth_|. And using WaitableEvent in it
// may hang the test runner.
class UserDataAuthTestTasked : public UserDataAuthTestBase {
 public:
  UserDataAuthTestTasked() = default;
  UserDataAuthTestTasked(const UserDataAuthTestTasked&) = delete;
  UserDataAuthTestTasked& operator=(const UserDataAuthTestTasked&) = delete;

  ~UserDataAuthTestTasked() override = default;

  void SetUp() override {
    // Note: If anything is modified/added here, we might need to adjust
    // UserDataAuthApiTest::SetUp() as well.

    // Setup the usual stuff
    UserDataAuthTestBase::SetUp();
    SetupTasks();
  }

  void SetupTasks() {
    // We do the task runner stuff for this test fixture.
    userdataauth_->set_origin_task_runner(origin_task_runner_);
    userdataauth_->set_mount_task_runner(mount_task_runner_);

    ON_CALL(system_apis_.platform, GetCurrentTime())
        .WillByDefault(Invoke([this]() {
          // The time between origin and mount task runner may have a skew when
          // fast forwarding the time. But current running task runner time must
          // be the biggest one.
          return std::max(origin_task_runner_->Now(),
                          mount_task_runner_->Now());
        }));
  }

  void CreatePkcs11TokenInSession(NiceMock<MockUserSession>* session) {
    auto token = std::make_unique<FakePkcs11Token>();
    ON_CALL(*session, GetPkcs11Token()).WillByDefault(Return(token.get()));
    tokens_.insert(std::move(token));
  }

  void InitializePkcs11TokenInSession(NiceMock<MockUserSession>* session) {
    // PKCS#11 will initialization works only when it's mounted.
    ON_CALL(*session, IsActive()).WillByDefault(Return(true));

    userdataauth_->InitializePkcs11(session);
  }

  void TearDown() override {
    RunUntilIdle();
    // Destruct the |userdataauth_| object.
    userdataauth_.reset();
  }

  // Initialize |userdataauth_| in |origin_task_runner_|
  void InitializeUserDataAuth() {
    ASSERT_TRUE(userdataauth_->Initialize(mount_bus_));
    // Let all initialization tasks complete.
    RunUntilIdle();
  }

  // Fast-forwards virtual time by |delta|
  void FastForwardBy(base::TimeDelta delta) {
    // Keep running the loop until there is no virtual time remain.
    while (!delta.is_zero()) {
      const base::TimeDelta& origin_delay =
          origin_task_runner_->NextPendingTaskDelay();
      const base::TimeDelta& mount_delay =
          mount_task_runner_->NextPendingTaskDelay();

      // Find the earliest task/deadline to forward.
      const base::TimeDelta& delay =
          std::min(delta, std::min(origin_delay, mount_delay));

      // Forward and run the origin task runner
      origin_task_runner_->FastForwardBy(delay);

      // Forward and run the mount task runner
      mount_task_runner_->FastForwardBy(delay);

      // Decrease the virtual time.
      delta -= delay;
    }

    // Make sure there is no zero delay tasks remain.
    RunUntilIdle();
  }

  // Run the all of the task runners until they don't find any zero delay tasks
  // in their queues.
  void RunUntilIdle() {
    while (origin_task_runner_->NextPendingTaskDelay().is_zero() ||
           mount_task_runner_->NextPendingTaskDelay().is_zero()) {
      origin_task_runner_->RunUntilIdle();
      mount_task_runner_->RunUntilIdle();
    }
  }

 protected:
  // Holder for tokens to preserve life time.
  absl::flat_hash_set<std::unique_ptr<FakePkcs11Token>> tokens_;

  // MockTimeTaskRunner for origin and mount thread.
  scoped_refptr<base::TestMockTimeTaskRunner> origin_task_runner_{
      new base::TestMockTimeTaskRunner(
          base::TestMockTimeTaskRunner::Type::kBoundToThread)};
  scoped_refptr<base::TestMockTimeTaskRunner> mount_task_runner_{
      new base::TestMockTimeTaskRunner()};
};

// Using UserDataAuthTestTasked for not initialized tests.
using UserDataAuthTestNotInitialized = UserDataAuthTestTasked;

// Variant of UserDataAuthTestNotInitialized for DeathTest. We should be careful
// in not creating threads in this class.
using UserDataAuthTestNotInitializedDeathTest = UserDataAuthTestNotInitialized;

// Standard, fully initialized UserDataAuth test fixture
class UserDataAuthTest : public UserDataAuthTestNotInitialized {
 public:
  UserDataAuthTest() = default;
  UserDataAuthTest(const UserDataAuthTest&) = delete;
  UserDataAuthTest& operator=(const UserDataAuthTest&) = delete;

  ~UserDataAuthTest() override = default;

  void SetUp() override {
    // Note: If anything is modified/added here, we might need to adjust
    // UserDataAuthApiTest::SetUp() as well.

    UserDataAuthTestNotInitialized::SetUp();
    InitializeUserDataAuth();
  }
};

namespace SignatureAlgorithmEquivalenceTest {
// This test is completely static, so it is not wrapped in the TEST_F() block.
static_assert(static_cast<int>(user_data_auth::SmartCardSignatureAlgorithm::
                                   CHALLENGE_RSASSA_PKCS1_V1_5_SHA1) ==
                  static_cast<int>(cryptohome::ChallengeSignatureAlgorithm::
                                       CHALLENGE_RSASSA_PKCS1_V1_5_SHA1),
              "Enum member CHALLENGE_RSASSA_PKCS1_V1_5_SHA1 differs between "
              "user_data_auth:: and cryptohome::");
static_assert(static_cast<int>(user_data_auth::SmartCardSignatureAlgorithm::
                                   CHALLENGE_RSASSA_PKCS1_V1_5_SHA256) ==
                  static_cast<int>(cryptohome::ChallengeSignatureAlgorithm::
                                       CHALLENGE_RSASSA_PKCS1_V1_5_SHA256),
              "Enum member CHALLENGE_RSASSA_PKCS1_V1_5_SHA256 differs between "
              "user_data_auth:: and cryptohome::");
static_assert(static_cast<int>(user_data_auth::SmartCardSignatureAlgorithm::
                                   CHALLENGE_RSASSA_PKCS1_V1_5_SHA384) ==
                  static_cast<int>(cryptohome::ChallengeSignatureAlgorithm::
                                       CHALLENGE_RSASSA_PKCS1_V1_5_SHA384),
              "Enum member CHALLENGE_RSASSA_PKCS1_V1_5_SHA384 differs between "
              "user_data_auth:: and cryptohome::");
static_assert(static_cast<int>(user_data_auth::SmartCardSignatureAlgorithm::
                                   CHALLENGE_RSASSA_PKCS1_V1_5_SHA512) ==
                  static_cast<int>(cryptohome::ChallengeSignatureAlgorithm::
                                       CHALLENGE_RSASSA_PKCS1_V1_5_SHA512),
              "Enum member CHALLENGE_RSASSA_PKCS1_V1_5_SHA512 differs between "
              "user_data_auth:: and cryptohome::");
static_assert(
    user_data_auth::SmartCardSignatureAlgorithm_MAX == 4,
    "user_data_auth::CrytpohomeErrorCode's element count is incorrect");
static_assert(cryptohome::ChallengeSignatureAlgorithm_MAX == 4,
              "cryptohome::CrytpohomeErrorCode's element count is incorrect");
}  // namespace SignatureAlgorithmEquivalenceTest

TEST_F(UserDataAuthTest, IsMounted) {
  // By default there are no mount right after initialization
  EXPECT_FALSE(userdataauth_->IsMounted());
  EXPECT_FALSE(userdataauth_->IsMounted(Username("foo@gmail.com")));

  // Add a mount associated with foo@gmail.com
  SetupMount("foo@gmail.com");

  // Test the code path that doesn't specify a user, and when there's a mount
  // that's unmounted.
  EXPECT_CALL(*session_, IsActive()).WillOnce(Return(false));
  EXPECT_FALSE(userdataauth_->IsMounted());

  // Test to see if is_ephemeral works and test the code path that doesn't
  // specify a user.
  bool is_ephemeral = true;
  EXPECT_CALL(*session_, IsActive()).WillOnce(Return(true));
  EXPECT_CALL(*session_, IsEphemeral()).WillOnce(Return(false));
  EXPECT_TRUE(userdataauth_->IsMounted(Username(""), &is_ephemeral));
  EXPECT_FALSE(is_ephemeral);

  // Test to see if is_ephemeral works, and test the code path that specify the
  // user.
  EXPECT_CALL(*session_, IsActive()).WillOnce(Return(true));
  EXPECT_CALL(*session_, IsEphemeral()).WillOnce(Return(true));
  EXPECT_TRUE(
      userdataauth_->IsMounted(Username("foo@gmail.com"), &is_ephemeral));
  EXPECT_TRUE(is_ephemeral);

  // Note: IsMounted will not be called in this case.
  EXPECT_FALSE(
      userdataauth_->IsMounted(Username("bar@gmail.com"), &is_ephemeral));
  EXPECT_FALSE(is_ephemeral);
}

TEST_F(UserDataAuthTest, GetVaultProperties) {
  user_data_auth::GetVaultPropertiesRequest req;
  req.set_username("foo@gmail.com");

  // By default there are no mount right after initialization
  {
    user_data_auth::GetVaultPropertiesReply reply =
        userdataauth_->GetVaultProperties(req);
    EXPECT_THAT(reply.error_info(),
                HasPossibleActions(PossibleActionSet(
                    {user_data_auth::PossibleAction::
                         POSSIBLY_DEV_CHECK_UNEXPECTED_STATE})));
  }

  // Add a mount associated with foo@gmail.com and that will be used in
  // subsequent tests.
  SetupMount("foo@gmail.com");

  // Test the code path that doesn't specify a user, and when there's a mount
  // that's unmounted.
  {
    EXPECT_CALL(*session_, IsActive()).WillOnce(Return(false));
    user_data_auth::GetVaultPropertiesReply reply =
        userdataauth_->GetVaultProperties(req);
    EXPECT_THAT(reply.error_info(),
                HasPossibleActions(PossibleActionSet(
                    {user_data_auth::PossibleAction::
                         POSSIBLY_DEV_CHECK_UNEXPECTED_STATE})));
  }

  // Subsequent tests will be on active sessions.
  EXPECT_CALL(*session_, IsActive()).WillRepeatedly(Return(true));

  // Test to see if is_ephemeral mounts works correctly.
  {
    EXPECT_CALL(*session_, GetMountType())
        .WillOnce(Return(cryptohome::MountType::EPHEMERAL));
    user_data_auth::GetVaultPropertiesReply reply =
        userdataauth_->GetVaultProperties(req);
    EXPECT_THAT(reply.error_info(),
                HasPossibleActions(PossibleActionSet(
                    {user_data_auth::PossibleAction::
                         POSSIBLY_DEV_CHECK_UNEXPECTED_STATE})));
  }

  // Test to see when there is no mount, the case is handled correctly.
  {
    EXPECT_CALL(*session_, GetMountType())
        .WillOnce(Return(cryptohome::MountType::NONE));
    user_data_auth::GetVaultPropertiesReply reply =
        userdataauth_->GetVaultProperties(req);
    EXPECT_THAT(reply.error_info(),
                HasPossibleActions(PossibleActionSet(
                    {user_data_auth::PossibleAction::
                         POSSIBLY_DEV_CHECK_UNEXPECTED_STATE})));
  }

  // Test to see various mount cases are handled correctly.
  {
    EXPECT_CALL(*session_, GetMountType())
        .WillOnce(Return(cryptohome::MountType::DMCRYPT));
    user_data_auth::GetVaultPropertiesReply reply =
        userdataauth_->GetVaultProperties(req);
    EXPECT_THAT(reply.error_info().possible_actions(), IsEmpty());
    EXPECT_EQ(reply.encryption_type(), user_data_auth::VaultEncryptionType::
                                           CRYPTOHOME_VAULT_ENCRYPTION_DMCRYPT);
  }
  {
    EXPECT_CALL(*session_, GetMountType())
        .WillOnce(Return(cryptohome::MountType::ECRYPTFS));
    user_data_auth::GetVaultPropertiesReply reply =
        userdataauth_->GetVaultProperties(req);
    EXPECT_THAT(reply.error_info().possible_actions(), IsEmpty());
    EXPECT_EQ(reply.encryption_type(),
              user_data_auth::VaultEncryptionType::
                  CRYPTOHOME_VAULT_ENCRYPTION_ECRYPTFS);
  }
  {
    EXPECT_CALL(*session_, GetMountType())
        .WillOnce(Return(cryptohome::MountType::ECRYPTFS_TO_DIR_CRYPTO));
    user_data_auth::GetVaultPropertiesReply reply =
        userdataauth_->GetVaultProperties(req);
    EXPECT_THAT(reply.error_info().possible_actions(), IsEmpty());
    EXPECT_EQ(reply.encryption_type(),
              user_data_auth::VaultEncryptionType::
                  CRYPTOHOME_VAULT_ENCRYPTION_ECRYPTFS);
  }
  {
    EXPECT_CALL(*session_, GetMountType())
        .WillOnce(Return(cryptohome::MountType::ECRYPTFS_TO_DMCRYPT));
    user_data_auth::GetVaultPropertiesReply reply =
        userdataauth_->GetVaultProperties(req);
    EXPECT_THAT(reply.error_info().possible_actions(), IsEmpty());
    EXPECT_EQ(reply.encryption_type(),
              user_data_auth::VaultEncryptionType::
                  CRYPTOHOME_VAULT_ENCRYPTION_ECRYPTFS);
  }
  {
    EXPECT_CALL(*session_, GetMountType())
        .WillOnce(Return(cryptohome::MountType::DIR_CRYPTO));
    user_data_auth::GetVaultPropertiesReply reply =
        userdataauth_->GetVaultProperties(req);
    EXPECT_THAT(reply.error_info().possible_actions(), IsEmpty());
    EXPECT_EQ(reply.encryption_type(), user_data_auth::VaultEncryptionType::
                                           CRYPTOHOME_VAULT_ENCRYPTION_FSCRYPT);
  }
  {
    EXPECT_CALL(*session_, GetMountType())
        .WillOnce(Return(cryptohome::MountType::DIR_CRYPTO_TO_DMCRYPT));
    user_data_auth::GetVaultPropertiesReply reply =
        userdataauth_->GetVaultProperties(req);
    EXPECT_THAT(reply.error_info().possible_actions(), IsEmpty());
    EXPECT_EQ(reply.encryption_type(), user_data_auth::VaultEncryptionType::
                                           CRYPTOHOME_VAULT_ENCRYPTION_FSCRYPT);
  }
}

TEST_F(UserDataAuthTest, Unmount_AllDespiteFailures) {
  const Username kUsername1("foo@gmail.com");
  const Username kUsername2("bar@gmail.com");

  auto owned_session1 = std::make_unique<NiceMock<MockUserSession>>();
  auto* const session1 = owned_session1.get();
  EXPECT_TRUE(userdataauth_->AddUserSessionForTest(kUsername1,
                                                   std::move(owned_session1)));

  auto owned_session2 = std::make_unique<NiceMock<MockUserSession>>();
  auto* const session2 = owned_session2.get();
  EXPECT_TRUE(userdataauth_->AddUserSessionForTest(kUsername2,
                                                   std::move(owned_session2)));

  {
    InSequence sequence;
    EXPECT_CALL(*session2, IsActive()).WillOnce(Return(true));
    EXPECT_CALL(*session2, Unmount()).WillOnce(Return(false));
  }
  {
    InSequence sequence;
    EXPECT_CALL(*session1, IsActive()).WillOnce(Return(true));
    EXPECT_CALL(*session1, Unmount()).WillOnce(Return(true));
  }
  EXPECT_FALSE(userdataauth_->RemoveAllMounts());
}

TEST_F(UserDataAuthTest, Unmount_EphemeralNotEnabled) {
  // Unmount validity test.
  // The tests on whether stale mount are cleaned up is in another set of tests
  // called CleanUpStale_*

  // Add a mount associated with foo@gmail.com
  SetupMount("foo@gmail.com");

  // Unmount will be successful.
  EXPECT_CALL(*session_, Unmount()).WillOnce(Return(true));
  // If anyone asks, this mount is still mounted.
  ON_CALL(*session_, IsActive()).WillByDefault(Return(true));

  // Test that non-owner's vaults are not touched.
  policy::DevicePolicy::EphemeralSettings ephemeral_settings;
  ephemeral_settings.global_ephemeral_users_enabled = false;
  EXPECT_CALL(homedirs_, GetEphemeralSettings(_))
      .WillRepeatedly(SetEphemeralSettings(ephemeral_settings));
  EXPECT_CALL(homedirs_, RemoveCryptohomesBasedOnPolicy())
      .WillOnce(Return(HomeDirs::CryptohomesRemovedStatus::kNone));

  // Unmount should be successful.
  EXPECT_EQ(userdataauth_->Unmount().error_info().primary_action(),
            user_data_auth::PrimaryAction::PRIMARY_NO_ERROR);

  // It should be unmounted in the end.
  EXPECT_FALSE(userdataauth_->IsMounted());

  // Add another mount associated with bar@gmail.com
  SetupMount("bar@gmail.com");

  // Unmount will be unsuccessful.
  EXPECT_CALL(*session_, Unmount()).WillOnce(Return(false));
  // If anyone asks, this mount is still mounted.
  ON_CALL(*session_, IsActive()).WillByDefault(Return(true));

  // Test that non-owner's vaults are not touched.
  ephemeral_settings.global_ephemeral_users_enabled = false;
  EXPECT_CALL(homedirs_, GetEphemeralSettings(_))
      .WillRepeatedly(SetEphemeralSettings(ephemeral_settings));
  EXPECT_CALL(homedirs_, RemoveCryptohomesBasedOnPolicy())
      .WillOnce(Return(HomeDirs::CryptohomesRemovedStatus::kNone));

  // Unmount should be honest about failures.
  EXPECT_NE(userdataauth_->Unmount().error_info().primary_action(),
            user_data_auth::PrimaryAction::PRIMARY_NO_ERROR);

  // Unmount will remove all mounts even if it failed.
  EXPECT_FALSE(userdataauth_->IsMounted());
}

TEST_F(UserDataAuthTest, Unmount_EphemeralEnabled) {
  // Unmount validity test.
  // The tests on whether stale mount are cleaned up is in another set of tests
  // called CleanUpStale_*

  // Add a mount associated with foo@gmail.com
  SetupMount("foo@gmail.com");

  // Unmount will be successful.
  EXPECT_CALL(*session_, Unmount()).WillOnce(Return(true));
  // If anyone asks, this mount is still mounted.
  ON_CALL(*session_, IsActive()).WillByDefault(Return(true));

  // Test that non-owner's vaults are cleaned up.
  policy::DevicePolicy::EphemeralSettings ephemeral_settings;
  ephemeral_settings.global_ephemeral_users_enabled = true;
  EXPECT_CALL(homedirs_, GetEphemeralSettings(_))
      .WillRepeatedly(SetEphemeralSettings(ephemeral_settings));
  EXPECT_CALL(homedirs_, RemoveCryptohomesBasedOnPolicy())
      .WillOnce(Return(HomeDirs::CryptohomesRemovedStatus::kSome));

  // Unmount should be successful.
  EXPECT_EQ(userdataauth_->Unmount().error_info().primary_action(),
            user_data_auth::PrimaryAction::PRIMARY_NO_ERROR);

  // It should be unmounted in the end.
  EXPECT_FALSE(userdataauth_->IsMounted());

  // Add another mount associated with bar@gmail.com
  SetupMount("bar@gmail.com");

  // Unmount will be unsuccessful.
  EXPECT_CALL(*session_, Unmount()).WillOnce(Return(false));
  // If anyone asks, this mount is still mounted.
  ON_CALL(*session_, IsActive()).WillByDefault(Return(true));

  // Test that non-owner's vaults are cleaned up anyway.
  ephemeral_settings.global_ephemeral_users_enabled = true;
  EXPECT_CALL(homedirs_, GetEphemeralSettings(_))
      .WillRepeatedly(SetEphemeralSettings(ephemeral_settings));
  EXPECT_CALL(homedirs_, RemoveCryptohomesBasedOnPolicy())
      .WillOnce(Return(HomeDirs::CryptohomesRemovedStatus::kSome));

  // Unmount should be honest about failures.
  EXPECT_NE(userdataauth_->Unmount().error_info().primary_action(),
            user_data_auth::PrimaryAction::PRIMARY_NO_ERROR);

  // Unmount will remove all mounts even if it failed.
  EXPECT_FALSE(userdataauth_->IsMounted());
}

TEST_F(UserDataAuthTest, InitializePkcs11Success) {
  // This test the most common success case for PKCS#11 initialization.

  EXPECT_FALSE(userdataauth_->IsMounted());

  // Add a mount associated with foo@gmail.com
  SetupMount("foo@gmail.com");

  CreatePkcs11TokenInSession(session_);

  // At first the token is not ready
  EXPECT_FALSE(session_->GetPkcs11Token()->IsReady());

  InitializePkcs11TokenInSession(session_);

  EXPECT_TRUE(session_->GetPkcs11Token()->IsReady());
}

TEST_F(UserDataAuthTest, InitializePkcs11Unmounted) {
  // Add a mount associated with foo@gmail.com
  SetupMount("foo@gmail.com");

  CreatePkcs11TokenInSession(session_);

  // At first the token is not ready
  EXPECT_FALSE(session_->GetPkcs11Token()->IsReady());

  ON_CALL(*session_, IsActive()).WillByDefault(Return(false));
  // The initialization code should at least check, right?
  EXPECT_CALL(*session_, IsActive())
      .Times(AtLeast(1))
      .WillRepeatedly(Return(false));

  userdataauth_->InitializePkcs11(session_);

  // Still not ready because already unmounted
  EXPECT_FALSE(session_->GetPkcs11Token()->IsReady());
}

TEST_F(UserDataAuthTest, Pkcs11IsTpmTokenReady) {
  // When there's no mount at all, it should be true.
  EXPECT_TRUE(userdataauth_->Pkcs11IsTpmTokenReady());

  const Username kUsername1("foo@gmail.com");
  const Username kUsername2("bar@gmail.com");

  auto owned_session1 = std::make_unique<NiceMock<MockUserSession>>();
  auto* const session1 = owned_session1.get();
  EXPECT_TRUE(userdataauth_->AddUserSessionForTest(kUsername1,
                                                   std::move(owned_session1)));
  CreatePkcs11TokenInSession(session1);

  auto owned_session2 = std::make_unique<NiceMock<MockUserSession>>();
  auto* const session2 = owned_session2.get();
  EXPECT_TRUE(userdataauth_->AddUserSessionForTest(kUsername2,
                                                   std::move(owned_session2)));
  CreatePkcs11TokenInSession(session2);

  // Both are uninitialized.
  EXPECT_FALSE(userdataauth_->Pkcs11IsTpmTokenReady());

  // Only one is initialized.
  InitializePkcs11TokenInSession(session2);
  EXPECT_FALSE(userdataauth_->Pkcs11IsTpmTokenReady());

  // Both is initialized.
  InitializePkcs11TokenInSession(session1);
  EXPECT_TRUE(userdataauth_->Pkcs11IsTpmTokenReady());
}

TEST_F(UserDataAuthTest, Pkcs11GetTpmTokenInfo) {
  user_data_auth::TpmTokenInfo info;

  constexpr CK_SLOT_ID kSlot = 42;
  const Username kUsername1("foo@gmail.com");

  // Check the system token case.
  EXPECT_CALL(pkcs11_init_, GetTpmTokenSlotForPath(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(kSlot), Return(true)));
  info = userdataauth_->Pkcs11GetTpmTokenInfo(Username());

  EXPECT_EQ(info.label(), Pkcs11Init::kDefaultSystemLabel);
  EXPECT_EQ(info.user_pin(), Pkcs11Init::kDefaultPin);
  EXPECT_EQ(info.slot(), kSlot);

  // Check the user token case.
  EXPECT_CALL(pkcs11_init_, GetTpmTokenSlotForPath(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(kSlot), Return(true)));
  info = userdataauth_->Pkcs11GetTpmTokenInfo(kUsername1);

  // Note that the label will usually be appended with a part of the sanitized
  // username. However, the sanitized username cannot be generated during
  // testing as we can't mock global functions in libbrillo. Therefore, we'll
  // only test that it is prefixed by prefix.
  EXPECT_EQ(info.label().substr(0, strlen(Pkcs11Init::kDefaultUserLabelPrefix)),
            Pkcs11Init::kDefaultUserLabelPrefix);
  EXPECT_EQ(info.user_pin(), Pkcs11Init::kDefaultPin);
  EXPECT_EQ(info.slot(), kSlot);

  // Verify that if GetTpmTokenSlotForPath fails, we'll get -1 for slot.
  EXPECT_CALL(pkcs11_init_, GetTpmTokenSlotForPath(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(kSlot), Return(false)));
  info = userdataauth_->Pkcs11GetTpmTokenInfo(Username());
  EXPECT_EQ(info.slot(), -1);

  EXPECT_CALL(pkcs11_init_, GetTpmTokenSlotForPath(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(kSlot), Return(false)));
  info = userdataauth_->Pkcs11GetTpmTokenInfo(kUsername1);
  EXPECT_EQ(info.slot(), -1);
}

TEST_F(UserDataAuthTest, Pkcs11Terminate) {
  // Check that it'll not crash when there's no mount
  userdataauth_->Pkcs11Terminate();

  // Check that we'll indeed get the Mount object to remove the PKCS#11 token.
  constexpr char kUsername1[] = "foo@gmail.com";
  SetupMount(kUsername1);
  CreatePkcs11TokenInSession(session_);
  InitializePkcs11TokenInSession(session_);

  EXPECT_TRUE(session_->GetPkcs11Token()->IsReady());

  userdataauth_->Pkcs11Terminate();

  EXPECT_FALSE(session_->GetPkcs11Token()->IsReady());
}

TEST_F(UserDataAuthTest, Pkcs11RestoreTpmTokens) {
  // This test the most common success case for PKCS#11 retrieving TPM tokens.

  // Add a mount associated with foo@gmail.com
  SetupMount("foo@gmail.com");

  CreatePkcs11TokenInSession(session_);

  // PKCS#11 will initialization works only when it's mounted.
  ON_CALL(*session_, IsActive()).WillByDefault(Return(true));
  // The initialization code should at least check, right?
  EXPECT_CALL(*session_, IsActive())
      .Times(AtLeast(1))
      .WillRepeatedly(Return(true));

  EXPECT_FALSE(session_->GetPkcs11Token()->IsReady());

  userdataauth_->Pkcs11RestoreTpmTokens();

  EXPECT_TRUE(session_->GetPkcs11Token()->IsReady());
}

TEST_F(UserDataAuthTest, Pkcs11RestoreTpmTokensWaitingOnTPM) {
  // This test the most common success case for PKCS#11 retrieving TPM tokens
  // when it's waiting TPM ready.

  // Add a mount associated with foo@gmail.com
  SetupMount("foo@gmail.com");

  CreatePkcs11TokenInSession(session_);

  // PKCS#11 will initialization works only when it's mounted.
  ON_CALL(*session_, IsActive()).WillByDefault(Return(true));
  // The initialization code should at least check, right?
  EXPECT_CALL(*session_, IsActive())
      .Times(AtLeast(1))
      .WillRepeatedly(Return(true));

  EXPECT_FALSE(session_->GetPkcs11Token()->IsReady());

  userdataauth_->Pkcs11RestoreTpmTokens();

  EXPECT_TRUE(session_->GetPkcs11Token()->IsReady());
}

TEST_F(UserDataAuthTest, LockToSingleUserMountUntilRebootValidity) {
  const Username kUsername1("foo@gmail.com");
  AccountIdentifier account_id;
  account_id.set_account_id(*kUsername1);
  const ObfuscatedUsername kUsername1Obfuscated =
      GetObfuscatedUsername(kUsername1);

  EXPECT_CALL(homedirs_, SetLockedToSingleUser()).WillOnce(Return(true));
  EXPECT_CALL(system_apis_.hwsec, IsCurrentUserSet())
      .WillOnce(ReturnValue(false));
  EXPECT_CALL(system_apis_.hwsec, SetCurrentUser(*kUsername1Obfuscated))
      .WillOnce(ReturnOk<TPMError>());

  EXPECT_EQ(userdataauth_->LockToSingleUserMountUntilReboot(account_id),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
}

TEST_F(UserDataAuthTest, LockToSingleUserMountUntilRebootReadPCRFail) {
  constexpr char kUsername1[] = "foo@gmail.com";
  AccountIdentifier account_id;
  account_id.set_account_id(kUsername1);

  ON_CALL(homedirs_, SetLockedToSingleUser()).WillByDefault(Return(true));
  EXPECT_CALL(system_apis_.hwsec, IsCurrentUserSet())
      .WillOnce(ReturnError<TPMError>("fake", TPMRetryAction::kNoRetry));

  EXPECT_EQ(userdataauth_->LockToSingleUserMountUntilReboot(account_id),
            user_data_auth::CRYPTOHOME_ERROR_FAILED_TO_READ_PCR);
}

TEST_F(UserDataAuthTest, LockToSingleUserMountUntilRebootAlreadyExtended) {
  constexpr char kUsername1[] = "foo@gmail.com";
  AccountIdentifier account_id;
  account_id.set_account_id(kUsername1);

  ON_CALL(homedirs_, SetLockedToSingleUser()).WillByDefault(Return(true));
  EXPECT_CALL(system_apis_.hwsec, IsCurrentUserSet())
      .WillOnce(ReturnValue(true));

  EXPECT_EQ(userdataauth_->LockToSingleUserMountUntilReboot(account_id),
            user_data_auth::CRYPTOHOME_ERROR_PCR_ALREADY_EXTENDED);
}

TEST_F(UserDataAuthTest, LockToSingleUserMountUntilRebootExtendFail) {
  const Username kUsername1("foo@gmail.com");
  AccountIdentifier account_id;
  account_id.set_account_id(*kUsername1);
  const ObfuscatedUsername kUsername1Obfuscated =
      GetObfuscatedUsername(kUsername1);

  EXPECT_CALL(homedirs_, SetLockedToSingleUser()).WillOnce(Return(true));
  EXPECT_CALL(system_apis_.hwsec, IsCurrentUserSet())
      .WillOnce(ReturnValue(false));
  EXPECT_CALL(system_apis_.hwsec, SetCurrentUser(*kUsername1Obfuscated))
      .WillOnce(ReturnError<TPMError>("fake", TPMRetryAction::kNoRetry));

  EXPECT_EQ(userdataauth_->LockToSingleUserMountUntilReboot(account_id),
            user_data_auth::CRYPTOHOME_ERROR_FAILED_TO_EXTEND_PCR);
}

TEST_F(UserDataAuthTest, GetSystemSaltSucess) {
  EXPECT_EQ(brillo::SecureBlob(*brillo::cryptohome::home::GetSystemSalt()),
            userdataauth_->GetSystemSalt());
}

TEST_F(UserDataAuthTestNotInitializedDeathTest, GetSystemSaltUninitialized) {
  EXPECT_DEATH(userdataauth_->GetSystemSalt(),
               "Cannot call GetSystemSalt before initialization");
}

TEST_F(UserDataAuthTest, HwsecReadyCallbackSuccess) {
  base::OnceCallback<void(hwsec::Status)> callback;

  // Called by Initialize().
  EXPECT_CALL(system_apis_.hwsec, RegisterOnReadyCallback)
      .WillOnce([&](base::OnceCallback<void(hwsec::Status)> cb) {
        callback = std::move(cb);
      });

  InitializeUserDataAuth();

  EXPECT_FALSE(callback.is_null());

  SetupMount("foo@gmail.com");

  // Called by EnsureCryptohomeKeys().
  EXPECT_CALL(system_apis_.cryptohome_keys_manager, HasAnyCryptohomeKey())
      .WillOnce(Return(true));

  std::move(callback).Run(hwsec::OkStatus());
}

TEST_F(UserDataAuthTest, HwsecReadyCallbackFail) {
  base::OnceCallback<void(hwsec::Status)> callback;

  // Called by Initialize().
  EXPECT_CALL(system_apis_.hwsec, RegisterOnReadyCallback)
      .WillOnce([&](base::OnceCallback<void(hwsec::Status)> cb) {
        callback = std::move(cb);
      });

  InitializeUserDataAuth();

  EXPECT_FALSE(callback.is_null());

  SetupMount("foo@gmail.com");

  // This function will not be called.
  EXPECT_CALL(system_apis_.cryptohome_keys_manager, HasAnyCryptohomeKey())
      .Times(0);

  std::move(callback).Run(
      MakeStatus<hwsec::TPMError>("fake", TPMRetryAction::kNoRetry));
}

TEST_F(UserDataAuthTest, UpdateCurrentUserActivityTimestampSuccess) {
  constexpr int kTimeshift = 5;

  // Test case for single mount
  SetupMount("foo@gmail.com");

  EXPECT_CALL(*session_, IsActive()).WillOnce(Return(true));
  EXPECT_CALL(*session_, IsEphemeral()).WillOnce(Return(false));
  EXPECT_CALL(system_apis_.user_activity_timestamp_manager,
              UpdateTimestamp(_, base::Seconds(kTimeshift)))
      .WillOnce(Return(true));

  EXPECT_TRUE(userdataauth_->UpdateCurrentUserActivityTimestamp(kTimeshift));

  // Test case for multiple mounts
  NiceMock<MockUserSession>* const prev_session = session_;
  SetupMount("bar@gmail.com");

  EXPECT_CALL(*session_, IsActive()).WillOnce(Return(true));
  EXPECT_CALL(*session_, IsEphemeral()).WillOnce(Return(false));
  EXPECT_CALL(*prev_session, IsActive()).WillOnce(Return(true));
  EXPECT_CALL(*prev_session, IsEphemeral()).WillOnce(Return(false));
  EXPECT_CALL(system_apis_.user_activity_timestamp_manager,
              UpdateTimestamp(_, base::Seconds(kTimeshift)))
      .Times(2)
      .WillRepeatedly(Return(true));

  EXPECT_TRUE(userdataauth_->UpdateCurrentUserActivityTimestamp(kTimeshift));
}

TEST_F(UserDataAuthTest, UpdateCurrentUserActivityTimestampFailure) {
  constexpr int kTimeshift = 5;

  // Test case for single mount
  SetupMount("foo@gmail.com");

  EXPECT_CALL(*session_, IsActive()).WillOnce(Return(true));
  EXPECT_CALL(*session_, IsEphemeral()).WillOnce(Return(false));
  EXPECT_CALL(system_apis_.user_activity_timestamp_manager,
              UpdateTimestamp(_, base::Seconds(kTimeshift)))
      .WillOnce(Return(false));

  EXPECT_FALSE(userdataauth_->UpdateCurrentUserActivityTimestamp(kTimeshift));
}

TEST_F(UserDataAuthTest, GetPinWeaverInfo) {
  // Case 1: PinWeaver has credential.
  EXPECT_CALL(system_apis_.hwsec_pw_manager, IsEnabled).WillOnce(Return(true));
  EXPECT_CALL(system_apis_.hwsec_pw_manager, HasAnyCredential)
      .WillOnce(Return(true));

  auto reply = userdataauth_->GetPinWeaverInfo();
  EXPECT_EQ(reply.error(), user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  EXPECT_TRUE(reply.has_credential());

  // Case 2: PinWeaver has no credentials.
  EXPECT_CALL(system_apis_.hwsec_pw_manager, IsEnabled).WillOnce(Return(true));
  EXPECT_CALL(system_apis_.hwsec_pw_manager, HasAnyCredential)
      .WillOnce(Return(false));

  reply = userdataauth_->GetPinWeaverInfo();
  EXPECT_EQ(reply.error(), user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  EXPECT_FALSE(reply.has_credential());

  // Case 3: PinWeaver is not enabled.
  EXPECT_CALL(system_apis_.hwsec_pw_manager, IsEnabled).WillOnce(Return(false));

  reply = userdataauth_->GetPinWeaverInfo();
  EXPECT_EQ(reply.error(), user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  EXPECT_FALSE(reply.has_credential());

  // Case 4: Get PinWeaver status failed.
  EXPECT_CALL(system_apis_.hwsec_pw_manager, IsEnabled)
      .WillOnce(ReturnError<TPMError>("fake", TPMRetryAction::kNoRetry));

  reply = userdataauth_->GetPinWeaverInfo();
  EXPECT_EQ(reply.error(),
            user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
}

// ======================= CleanUpStaleMounts tests ==========================

namespace {

struct Mounts {
  const FilePath src;
  const FilePath dst;
};

const std::vector<Mounts> kShadowMounts = {
    {FilePath("/home/.shadow/a"), FilePath("/home/root/0")},
    {FilePath("/home/.shadow/a"), FilePath("/home/user/0")},
    {FilePath("/home/.shadow/a"), FilePath("/home/chronos/user")},
    {FilePath("/home/.shadow/a/Downloads"),
     FilePath("/home/chronos/user/MyFiles/Downloads")},
    {FilePath("/home/.shadow/a/server/run"),
     FilePath("/daemon-store/server/a")},
    {FilePath("/home/.shadow/b"), FilePath("/home/root/1")},
    {FilePath("/home/.shadow/b"), FilePath("/home/user/1")},
    {FilePath("/home/.shadow/b/Downloads"),
     FilePath("/home/chronos/u-b/MyFiles/Downloads")},
    {FilePath("/home/.shadow/b/Downloads"),
     FilePath("/home/user/b/MyFiles/Downloads")},
    {FilePath("/home/.shadow/b/server/run"),
     FilePath("/daemon-store/server/b")},
};

const std::vector<Mounts> kDmcryptMounts = {
    {FilePath("/dev/mapper/dmcrypt-4567-data"), FilePath("/home/root/1")},
    {FilePath("/dev/mapper/dmcrypt-4567-data"), FilePath("/home/user/1")},
    {FilePath("/dev/mapper/dmcrypt-1234-data"), FilePath("/home/root/0")},
    {FilePath("/dev/mapper/dmcrypt-1234-data"), FilePath("/home/user/0")},
    {FilePath("/dev/mapper/dmcrypt-1234-data"), FilePath("/home/chronos/user")},
    {FilePath("/dev/mapper/dmcrypt-1234-data"),
     FilePath("/home/chronos/user/MyFiles/Downloads")},
    {FilePath("/dev/mapper/dmcrypt-1234-data"),
     FilePath("/daemon-store/server/a")},
    {FilePath("/dev/mapper/dmcrypt-1234-data"),
     FilePath("/home/chronos/u-b/MyFiles/Downloads")},
    {FilePath("/dev/mapper/dmcrypt-1234-data"),
     FilePath("/home/user/b/MyFiles/Downloads")},
    {FilePath("/dev/mapper/dmcrypt-1234-data"),
     FilePath("/daemon-store/server/b")},
    {FilePath("/dev/mapper/dmcrypt-1234-data"),
     FilePath("/home/chronos/user/Cache")},
    {FilePath("/dev/mapper/dmcrypt-1234-data"),
     FilePath("/home/chronos/user/GCache")},
    {FilePath("/dev/mapper/dmcrypt-1234-data"),
     FilePath("/home/chronos/u-1234/Cache")},
    {FilePath("/dev/mapper/dmcrypt-1234-data"),
     FilePath("/home/chronos/u-1234/GCache")},
    {FilePath("/dev/mapper/dmcrypt-1234-data"),
     FilePath("/home/user/1234/Cache")},
    {FilePath("/dev/mapper/dmcrypt-1234-data"),
     FilePath("/home/user/1234/GCache")},
};

// Ephemeral mounts must be at the beginning.
const std::vector<Mounts> kLoopDevMounts = {
    {FilePath("/dev/loop7"), FilePath("/run/cryptohome/ephemeral_mount/1")},
    {FilePath("/dev/loop7"), FilePath("/home/user/0")},
    {FilePath("/dev/loop7"), FilePath("/home/root/0")},
    {FilePath("/dev/loop7"), FilePath("/home/chronos/u-1")},
    {FilePath("/dev/loop7"), FilePath("/home/chronos/user")},
    {FilePath("/dev/loop1"), FilePath("/opt/google/containers")},
    {FilePath("/dev/loop2"), FilePath("/home/root/1")},
    {FilePath("/dev/loop2"), FilePath("/home/user/1")},
};

// 5 Mounts in the above are from /dev/loop7, which is ephemeral as seen
// in kLoopDevices.
const int kEphemeralMountsCount = 5;

// Constants used by CleanUpStaleMounts tests.
const std::vector<libstorage::Platform::LoopDevice> kLoopDevices = {
    {FilePath("/mnt/stateful_partition/encrypted.block"),
     FilePath("/dev/loop0")},
    {FilePath("/run/cryptohome/ephemeral_data/1"), FilePath("/dev/loop7")},
};

const std::vector<FilePath> kSparseFiles = {
    FilePath("/run/cryptohome/ephemeral_data/2"),
    FilePath("/run/cryptohome/ephemeral_data/1"),
};

// Utility functions used by CleanUpStaleMounts tests.
bool StaleShadowMounts(const FilePath& from_prefix,
                       std::multimap<const FilePath, const FilePath>* mounts) {
  int i = 0;

  for (const auto& m : kShadowMounts) {
    if (m.src.value().find(from_prefix.value()) == 0) {
      i++;
      if (mounts) {
        mounts->insert(std::make_pair(m.src, m.dst));
      }
    }
  }
  return i > 0;
}

bool DmcryptDeviceMounts(
    const std::string& from_prefix,
    std::multimap<const FilePath, const FilePath>* mounts) {
  if (!mounts) {
    return false;
  }
  for (const auto& m : kDmcryptMounts) {
    mounts->insert(std::make_pair(m.src, m.dst));
  }
  return true;
}

bool LoopDeviceMounts(std::multimap<const FilePath, const FilePath>* mounts) {
  if (!mounts) {
    return false;
  }
  for (const auto& m : kLoopDevMounts) {
    mounts->insert(std::make_pair(m.src, m.dst));
  }
  return true;
}

bool EnumerateSparseFiles(const base::FilePath& path,
                          bool is_recursive,
                          std::vector<base::FilePath>* ent_list) {
  if (path != FilePath(kEphemeralCryptohomeDir).Append(kSparseFileDir)) {
    return false;
  }
  ent_list->insert(ent_list->begin(), kSparseFiles.begin(), kSparseFiles.end());
  return true;
}

}  // namespace

TEST_F(UserDataAuthTest, CleanUpStale_NoOpenFiles_Dmcrypt) {
  // Check that when we have dm-crypt mounts, no active mounts,
  // and no open filehandles, all stale mounts are unmounted.

  EXPECT_CALL(system_apis_.platform,
              GetMountsByDevicePrefix("/dev/mapper/dmcrypt", _))
      .WillOnce(Invoke(DmcryptDeviceMounts));

  EXPECT_CALL(system_apis_.platform, ExpireMount(_))
      .Times(kDmcryptMounts.size())
      .WillRepeatedly(Return(libstorage::ExpireMountResult::kMarked));

  for (int i = 0; i < kDmcryptMounts.size(); ++i) {
    EXPECT_CALL(system_apis_.platform, Unmount(kDmcryptMounts[i].dst, true, _))
        .WillRepeatedly(Return(true));
  }

  EXPECT_FALSE(userdataauth_->CleanUpStaleMounts(false));
}

TEST_F(UserDataAuthTest, CleanUpStale_OpenFiles_Dmcrypt) {
  // Check that when we have dm-crypt mounts, files open on dm-crypt cryptohome
  // for one user and no open filehandles, all stale mounts for the second user
  // are unmounted.
  EXPECT_CALL(system_apis_.platform,
              GetMountsByDevicePrefix("/dev/mapper/dmcrypt", _))
      .WillOnce(Invoke(DmcryptDeviceMounts));

  // The number of expired mounts depends on when the first busy mount is
  // traversed through. In this case, /home/chronos/user is the 3rd mount in
  // the list, so ExpireMount() is called for the first two non-busy mounts for
  // user 1234 and then for the non-busy stale mounts for user 4567.
  const int kBusyMountIndex = 4;
  EXPECT_CALL(system_apis_.platform, ExpireMount(_))
      .Times(kBusyMountIndex)
      .WillRepeatedly(Return(libstorage::ExpireMountResult::kMarked));

  EXPECT_CALL(system_apis_.platform,
              ExpireMount(kDmcryptMounts[kBusyMountIndex].dst))
      .Times(1)
      .WillRepeatedly(Return(libstorage::ExpireMountResult::kBusy));

  // Only user 4567's mounts will be unmounted.
  for (int i = 0; i < 2; ++i) {
    EXPECT_CALL(system_apis_.platform, Unmount(kDmcryptMounts[i].dst, true, _))
        .WillRepeatedly(Return(true));
  }

  EXPECT_TRUE(userdataauth_->CleanUpStaleMounts(false));
}

TEST_F(UserDataAuthTest, CleanUpStale_OpenFiles_Dmcrypt_Forced) {
  // Check that when we have dm-crypt mounts, files open on dm-crypt
  // and no open filehandles, all stale mounts are unmounted.

  EXPECT_CALL(system_apis_.platform,
              GetMountsByDevicePrefix("/dev/mapper/dmcrypt", _))
      .WillOnce(Invoke(DmcryptDeviceMounts));
  EXPECT_CALL(system_apis_.platform, ExpireMount(_)).Times(0);

  for (int i = 0; i < kDmcryptMounts.size(); ++i) {
    EXPECT_CALL(system_apis_.platform, Unmount(kDmcryptMounts[i].dst, true, _))
        .WillRepeatedly(Return(true));
  }

  EXPECT_FALSE(userdataauth_->CleanUpStaleMounts(true));
}

TEST_F(UserDataAuthTest, CleanUpStale_NoOpenFiles_Ephemeral) {
  // Check that when we have ephemeral mounts, no active mounts,
  // and no open filehandles, all stale mounts are unmounted, loop device is
  // detached and sparse file is deleted.

  EXPECT_CALL(system_apis_.platform, GetMountsBySourcePrefix(_, _))
      .WillOnce(Return(false));
  EXPECT_CALL(system_apis_.platform, GetAttachedLoopDevices())
      .WillRepeatedly(Return(kLoopDevices));
  EXPECT_CALL(system_apis_.platform, GetLoopDeviceMounts(_))
      .WillOnce(Invoke(LoopDeviceMounts));
  EXPECT_CALL(
      system_apis_.platform,
      EnumerateDirectoryEntries(
          FilePath(kEphemeralCryptohomeDir).Append(kSparseFileDir), _, _))
      .WillOnce(Invoke(EnumerateSparseFiles));
  EXPECT_CALL(system_apis_.platform, ExpireMount(_))
      .Times(kEphemeralMountsCount)
      .WillRepeatedly(Return(libstorage::ExpireMountResult::kMarked));

  for (int i = 0; i < kEphemeralMountsCount; ++i) {
    EXPECT_CALL(system_apis_.platform, Unmount(kLoopDevMounts[i].dst, true, _))
        .WillRepeatedly(Return(true));
  }
  EXPECT_CALL(system_apis_.platform, DetachLoop(FilePath("/dev/loop7")))
      .WillOnce(Return(true));
  EXPECT_CALL(system_apis_.platform, DeleteFile(kSparseFiles[0]))
      .WillOnce(Return(true));
  EXPECT_CALL(system_apis_.platform, DeleteFile(kSparseFiles[1]))
      .WillOnce(Return(true));
  EXPECT_CALL(system_apis_.platform,
              DeletePathRecursively(kLoopDevMounts[0].dst))
      .WillOnce(Return(true));
  EXPECT_FALSE(userdataauth_->CleanUpStaleMounts(false));
}

TEST_F(UserDataAuthTest, CleanUpStale_OpenLegacy_Ephemeral) {
  // Check that when we have ephemeral mounts, no active mounts,
  // and some open filehandles to the legacy homedir, everything is kept.

  EXPECT_CALL(system_apis_.platform, GetMountsBySourcePrefix(_, _))
      .WillOnce(Return(false));
  EXPECT_CALL(system_apis_.platform, GetAttachedLoopDevices())
      .WillRepeatedly(Return(kLoopDevices));
  EXPECT_CALL(system_apis_.platform, GetLoopDeviceMounts(_))
      .WillOnce(Invoke(LoopDeviceMounts));
  EXPECT_CALL(
      system_apis_.platform,
      EnumerateDirectoryEntries(
          FilePath(kEphemeralCryptohomeDir).Append(kSparseFileDir), _, _))
      .WillOnce(Invoke(EnumerateSparseFiles));
  EXPECT_CALL(system_apis_.platform, ExpireMount(_))
      .Times(kEphemeralMountsCount - 1)
      .WillRepeatedly(Return(libstorage::ExpireMountResult::kMarked));
  EXPECT_CALL(system_apis_.platform,
              ExpireMount(FilePath("/home/chronos/user")))
      .Times(1)
      .WillRepeatedly(Return(libstorage::ExpireMountResult::kBusy));

  EXPECT_CALL(system_apis_.platform,
              GetMountsBySourcePrefix(FilePath("/dev/loop7"), _))
      .WillOnce(Return(false));

  EXPECT_CALL(system_apis_.platform, Unmount(_, _, _)).Times(0);
  EXPECT_TRUE(userdataauth_->CleanUpStaleMounts(false));
}

TEST_F(UserDataAuthTest, CleanUpStale_OpenLegacy_Ephemeral_Forced) {
  // Check that when we have ephemeral mounts, no active mounts,
  // and some open filehandles to the legacy homedir, but cleanup is forced,
  // all mounts are unmounted, loop device is detached and file is deleted.

  EXPECT_CALL(system_apis_.platform, GetMountsBySourcePrefix(_, _))
      .WillOnce(Return(false));
  EXPECT_CALL(system_apis_.platform, GetAttachedLoopDevices())
      .WillRepeatedly(Return(kLoopDevices));
  EXPECT_CALL(system_apis_.platform, GetLoopDeviceMounts(_))
      .WillOnce(Invoke(LoopDeviceMounts));
  EXPECT_CALL(
      system_apis_.platform,
      EnumerateDirectoryEntries(
          FilePath(kEphemeralCryptohomeDir).Append(kSparseFileDir), _, _))
      .WillOnce(Invoke(EnumerateSparseFiles));
  EXPECT_CALL(system_apis_.platform, ExpireMount(_)).Times(0);

  for (int i = 0; i < kEphemeralMountsCount; ++i) {
    EXPECT_CALL(system_apis_.platform, Unmount(kLoopDevMounts[i].dst, true, _))
        .WillRepeatedly(Return(true));
  }
  EXPECT_CALL(system_apis_.platform, DetachLoop(FilePath("/dev/loop7")))
      .WillOnce(Return(true));
  EXPECT_CALL(system_apis_.platform, DeleteFile(kSparseFiles[0]))
      .WillOnce(Return(true));
  EXPECT_CALL(system_apis_.platform, DeleteFile(kSparseFiles[1]))
      .WillOnce(Return(true));
  EXPECT_CALL(system_apis_.platform,
              DeletePathRecursively(kLoopDevMounts[0].dst))
      .WillOnce(Return(true));
  EXPECT_FALSE(userdataauth_->CleanUpStaleMounts(true));
}

TEST_F(UserDataAuthTest, CleanUpStale_EmptyMap_NoOpenFiles_ShadowOnly) {
  // Check that when we have a bunch of stale shadow mounts, no active mounts,
  // and no open filehandles, all stale mounts are unmounted.

  EXPECT_CALL(system_apis_.platform, GetMountsBySourcePrefix(_, _))
      .WillOnce(Invoke(StaleShadowMounts));
  EXPECT_CALL(system_apis_.platform, GetAttachedLoopDevices())
      .WillRepeatedly(Return(std::vector<libstorage::Platform::LoopDevice>()));
  EXPECT_CALL(system_apis_.platform, GetLoopDeviceMounts(_))
      .WillOnce(Return(false));
  EXPECT_CALL(
      system_apis_.platform,
      EnumerateDirectoryEntries(
          FilePath(kEphemeralCryptohomeDir).Append(kSparseFileDir), _, _))
      .WillOnce(Return(false));
  EXPECT_CALL(system_apis_.platform, ExpireMount(_))
      .Times(kShadowMounts.size())
      .WillRepeatedly(Return(libstorage::ExpireMountResult::kMarked));
  EXPECT_CALL(system_apis_.platform, Unmount(_, true, _))
      .Times(kShadowMounts.size())
      .WillRepeatedly(Return(true));
  EXPECT_FALSE(userdataauth_->CleanUpStaleMounts(false));
}

TEST_F(UserDataAuthTest, CleanUpStale_EmptyMap_NoOpenFiles_ShadowOnly_Forced) {
  // Check that when we have a bunch of stale shadow mounts, no active mounts,
  // and no open filehandles, all stale mounts are unmounted and we attempt
  // to clear the encryption key for fscrypt/ecryptfs mounts.

  EXPECT_CALL(system_apis_.platform, GetMountsBySourcePrefix(_, _))
      .WillOnce(Invoke(StaleShadowMounts));
  EXPECT_CALL(system_apis_.platform, GetAttachedLoopDevices())
      .WillRepeatedly(Return(std::vector<libstorage::Platform::LoopDevice>()));
  EXPECT_CALL(system_apis_.platform, GetLoopDeviceMounts(_))
      .WillOnce(Return(false));
  EXPECT_CALL(
      system_apis_.platform,
      EnumerateDirectoryEntries(
          FilePath(kEphemeralCryptohomeDir).Append(kSparseFileDir), _, _))
      .WillOnce(Return(false));
  EXPECT_CALL(system_apis_.platform, Unmount(_, true, _))
      .Times(kShadowMounts.size())
      .WillRepeatedly(Return(true));

  // Expect the cleanup to clear user keys.
  EXPECT_CALL(system_apis_.platform, ClearUserKeyring()).WillOnce(Return(true));
  EXPECT_CALL(system_apis_.platform, InvalidateDirCryptoKey(_, _))
      .Times(kShadowMounts.size())
      .WillRepeatedly(Return(true));

  EXPECT_FALSE(userdataauth_->CleanUpStaleMounts(true));
}

TEST_F(UserDataAuthTest, CleanUpStale_EmptyMap_OpenLegacy_ShadowOnly) {
  // Check that when we have a bunch of stale shadow mounts, no active mounts,
  // and some open filehandles to the legacy homedir, all mounts without
  // filehandles are unmounted.

  // Called by CleanUpStaleMounts and each time a directory is excluded.
  EXPECT_CALL(system_apis_.platform, GetMountsBySourcePrefix(_, _))
      .Times(4)
      .WillRepeatedly(Invoke(StaleShadowMounts));
  EXPECT_CALL(system_apis_.platform, GetAttachedLoopDevices())
      .WillRepeatedly(Return(std::vector<libstorage::Platform::LoopDevice>()));
  EXPECT_CALL(system_apis_.platform, GetLoopDeviceMounts(_))
      .WillOnce(Return(false));
  EXPECT_CALL(
      system_apis_.platform,
      EnumerateDirectoryEntries(
          FilePath(kEphemeralCryptohomeDir).Append(kSparseFileDir), _, _))
      .WillOnce(Return(false));
  EXPECT_CALL(system_apis_.platform,
              ExpireMount(Property(&FilePath::value, EndsWith("/0"))))
      .WillRepeatedly(Return(libstorage::ExpireMountResult::kBusy));
  EXPECT_CALL(system_apis_.platform,
              ExpireMount(FilePath("/home/chronos/user")))
      .WillRepeatedly(Return(libstorage::ExpireMountResult::kBusy));
  EXPECT_CALL(system_apis_.platform,
              ExpireMount(Property(
                  &FilePath::value,
                  AnyOf(EndsWith("/1"), EndsWith("b/MyFiles/Downloads")))))
      .Times(4)
      .WillRepeatedly(Return(libstorage::ExpireMountResult::kMarked));
  EXPECT_CALL(system_apis_.platform,
              ExpireMount(FilePath("/daemon-store/server/b")))
      .WillOnce(Return(libstorage::ExpireMountResult::kMarked));
  // Given /home/chronos/user and a is marked as active, only b mounts should be
  // removed.
  EXPECT_CALL(
      system_apis_.platform,
      Unmount(Property(&FilePath::value,
                       AnyOf(EndsWith("/1"), EndsWith("b/MyFiles/Downloads"))),
              true, _))
      .Times(4)
      .WillRepeatedly(Return(true));
  EXPECT_CALL(system_apis_.platform,
              Unmount(FilePath("/daemon-store/server/b"), true, _))
      .WillOnce(Return(true));
  EXPECT_CALL(system_apis_.platform,
              Unmount(Property(&FilePath::value, EndsWith("/0")), true, _))
      .Times(0);
  EXPECT_CALL(system_apis_.platform,
              Unmount(FilePath("/home/chronos/user"), true, _))
      .Times(0);
  EXPECT_TRUE(userdataauth_->CleanUpStaleMounts(false));
}

TEST_F(UserDataAuthTest, CleanUpStale_FilledMap_NoOpenFiles_ShadowOnly) {
  constexpr char kUser[] = "foo@bar.net";

  // Checks that when we have a bunch of stale shadow mounts, some active
  // mounts, and no open filehandles, all inactive mounts are unmounted.

  EXPECT_CALL(system_apis_.platform,
              FileExists(base::FilePath("/home/.shadow/salt")))
      .WillOnce(Return(false));
  EXPECT_CALL(system_apis_.platform,
              FileExists(base::FilePath("/var/lib/system_salt")))
      .WillOnce(Return(true));
  EXPECT_CALL(system_apis_.platform,
              FileExists(base::FilePath("/run/cryptohome/not_first_boot")))
      .WillOnce(Return(true));
  EXPECT_CALL(
      system_apis_.platform,
      FileExists(base::FilePath("/run/cryptohome/pw_pk_establishment_blocked")))
      .WillOnce(Return(true));

  EXPECT_CALL(system_apis_.platform, GetMountsBySourcePrefix(_, _))
      .WillOnce(Return(false));
  EXPECT_CALL(system_apis_.platform, GetAttachedLoopDevices())
      .WillRepeatedly(Return(std::vector<libstorage::Platform::LoopDevice>()));
  EXPECT_CALL(system_apis_.platform, GetLoopDeviceMounts(_))
      .WillOnce(Return(false));

  InitializeUserDataAuth();

  EXPECT_CALL(user_session_factory_, New(Username(kUser), _, _))
      .WillOnce(Return(ByMove(CreateSessionAndRememberPtr())));
  EXPECT_CALL(homedirs_, Exists(_)).WillOnce(Return(true));
  EXPECT_CALL(disk_cleanup_, FreeDiskSpaceDuringLogin(_));
  EXPECT_CALL(*session_, MountVault(_, _, _))
      .WillOnce(ReturnError<CryptohomeMountError>());
  EXPECT_CALL(system_apis_.platform, GetMountsBySourcePrefix(_, _))
      .WillOnce(Return(false));
  EXPECT_CALL(system_apis_.platform, GetAttachedLoopDevices())
      .WillRepeatedly(Return(std::vector<libstorage::Platform::LoopDevice>()));
  EXPECT_CALL(system_apis_.platform, GetLoopDeviceMounts(_))
      .WillOnce(Return(false));

  // StartAuthSession for new user.
  user_data_auth::StartAuthSessionRequest start_session_req;
  start_session_req.mutable_account_id()->set_account_id(kUser);
  start_session_req.set_intent(user_data_auth::AuthIntent::AUTH_INTENT_DECRYPT);

  TestFuture<user_data_auth::StartAuthSessionReply> reply_future;
  userdataauth_->StartAuthSession(
      start_session_req,
      reply_future.GetCallback<const user_data_auth::StartAuthSessionReply&>());
  RunUntilIdle();
  EXPECT_EQ(reply_future.Get().error(),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  std::optional<base::UnguessableToken> auth_session_id =
      AuthSession::GetTokenFromSerializedString(
          reply_future.Get().auth_session_id());
  ASSERT_TRUE(auth_session_id.has_value());

  // Get the session into an authenticated state by treating it as if we just
  // freshly created the user.
  userdataauth_->auth_session_manager_->RunWhenAvailable(
      *auth_session_id, base::BindOnce([](InUseAuthSession auth_session) {
        ASSERT_THAT(auth_session.AuthSessionStatus(), IsOk());
        ASSERT_THAT(auth_session->OnUserCreated(), IsOk());
      }));
  RunUntilIdle();

  // Mount user vault.
  user_data_auth::PreparePersistentVaultRequest prepare_request;
  prepare_request.set_auth_session_id(reply_future.Get().auth_session_id());
  TestFuture<user_data_auth::PreparePersistentVaultReply> prepare_future;
  userdataauth_->PreparePersistentVault(
      prepare_request,
      prepare_future
          .GetCallback<const user_data_auth::PreparePersistentVaultReply&>());
  RunUntilIdle();
  ASSERT_EQ(prepare_future.Get().error_info().primary_action(),
            user_data_auth::PrimaryAction::PRIMARY_NO_ERROR);

  // Test CleanUpStaleMounts.

  EXPECT_CALL(system_apis_.platform, GetMountsBySourcePrefix(_, _))
      .Times(4)
      .WillRepeatedly(Invoke(StaleShadowMounts));
  EXPECT_CALL(system_apis_.platform, GetAttachedLoopDevices())
      .WillRepeatedly(Return(std::vector<libstorage::Platform::LoopDevice>()));
  EXPECT_CALL(system_apis_.platform, GetLoopDeviceMounts(_))
      .WillOnce(Return(false));
  EXPECT_CALL(
      system_apis_.platform,
      EnumerateDirectoryEntries(
          FilePath(kEphemeralCryptohomeDir).Append(kSparseFileDir), _, _))
      .WillOnce(Return(false));
  // Only 5 look ups: user/1 and root/1 are owned, children of these
  // directories are excluded. ExpireMount is expected to run on exactly the
  // same mount points that are expected to be unmounted below. But it is
  // important to check the number of calls here to make sure ExpireMount
  // doesn't run on any other mount points.
  EXPECT_CALL(system_apis_.platform, ExpireMount(_))
      .Times(5)
      .WillRepeatedly(Return(libstorage::ExpireMountResult::kMarked));

  EXPECT_CALL(*session_, OwnsMountPoint(_)).WillRepeatedly(Return(false));
  EXPECT_CALL(*session_, OwnsMountPoint(FilePath("/home/user/1")))
      .WillOnce(Return(true));
  EXPECT_CALL(*session_, OwnsMountPoint(FilePath("/home/root/1")))
      .WillOnce(Return(true));

  EXPECT_CALL(system_apis_.platform,
              Unmount(Property(&FilePath::value, EndsWith("/0")), true, _))
      .Times(2)
      .WillRepeatedly(Return(true));
  EXPECT_CALL(system_apis_.platform,
              Unmount(FilePath("/home/chronos/user"), true, _))
      .WillOnce(Return(true));
  EXPECT_CALL(
      system_apis_.platform,
      Unmount(Property(&FilePath::value, EndsWith("user/MyFiles/Downloads")),
              true, _))
      .WillOnce(Return(true));
  EXPECT_CALL(system_apis_.platform,
              Unmount(FilePath("/daemon-store/server/a"), true, _))
      .WillOnce(Return(true));

  std::vector<std::string> fake_token_list;
  fake_token_list.push_back("/home/chronos/user/token");
  fake_token_list.push_back("/home/user/1/token");
  fake_token_list.push_back("/home/root/1/token");
  EXPECT_CALL(chaps_client_, GetTokenList(_, _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(fake_token_list), Return(true)));

  EXPECT_CALL(chaps_client_,
              UnloadToken(_, FilePath("/home/chronos/user/token")))
      .Times(1);
  // Expect that CleanUpStaleMounts() tells us it skipped mounts since 1 is
  // still logged in.
  EXPECT_TRUE(userdataauth_->CleanUpStaleMounts(false));
}

TEST_F(UserDataAuthTest,
       CleanUpStale_FilledMap_NoOpenFiles_ShadowOnly_FirstBoot) {
  constexpr char kUser[] = "foo@bar.net";

  // Checks that when we have a bunch of stale shadow mounts, some active
  // mounts, and no open filehandles, all inactive mounts are unmounted.

  EXPECT_CALL(system_apis_.platform,
              FileExists(base::FilePath("/home/.shadow/salt")))
      .WillOnce(Return(false));
  EXPECT_CALL(system_apis_.platform,
              FileExists(base::FilePath("/var/lib/system_salt")))
      .WillOnce(Return(false));
  EXPECT_CALL(system_apis_.platform,
              FileExists(base::FilePath("/run/cryptohome/not_first_boot")))
      .WillOnce(Return(false));
  EXPECT_CALL(
      system_apis_.platform,
      FileExists(base::FilePath("/run/cryptohome/pw_pk_establishment_blocked")))
      .WillOnce(Return(true));
  EXPECT_CALL(system_apis_.platform, GetMountsBySourcePrefix(_, _)).Times(0);
  EXPECT_CALL(system_apis_.platform, GetAttachedLoopDevices()).Times(0);
  EXPECT_CALL(system_apis_.platform, GetLoopDeviceMounts(_)).Times(0);

  InitializeUserDataAuth();

  EXPECT_CALL(user_session_factory_, New(Username(kUser), _, _))
      .WillOnce(Return(ByMove(CreateSessionAndRememberPtr())));
  EXPECT_CALL(homedirs_, Exists(_)).WillOnce(ReturnValue(true));
  EXPECT_CALL(disk_cleanup_, FreeDiskSpaceDuringLogin(_));
  EXPECT_CALL(*session_, MountVault(_, _, _))
      .WillOnce(ReturnError<CryptohomeMountError>());
  EXPECT_CALL(system_apis_.platform, GetMountsBySourcePrefix(_, _))
      .WillOnce(Return(false));
  EXPECT_CALL(system_apis_.platform, GetAttachedLoopDevices())
      .WillRepeatedly(Return(std::vector<libstorage::Platform::LoopDevice>()));
  EXPECT_CALL(system_apis_.platform, GetLoopDeviceMounts(_))
      .WillOnce(Return(false));

  // StartAuthSession for new user
  user_data_auth::StartAuthSessionRequest start_session_req;
  start_session_req.mutable_account_id()->set_account_id(kUser);
  start_session_req.set_intent(user_data_auth::AuthIntent::AUTH_INTENT_DECRYPT);

  TestFuture<user_data_auth::StartAuthSessionReply> reply_future;
  userdataauth_->StartAuthSession(
      start_session_req,
      reply_future.GetCallback<const user_data_auth::StartAuthSessionReply&>());
  RunUntilIdle();
  EXPECT_EQ(reply_future.Get().error(),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  std::optional<base::UnguessableToken> auth_session_id =
      AuthSession::GetTokenFromSerializedString(
          reply_future.Get().auth_session_id());
  ASSERT_TRUE(auth_session_id.has_value());

  // Get the session into an authenticated state by treating it as if we just
  // freshly created the user.
  userdataauth_->auth_session_manager_->RunWhenAvailable(
      *auth_session_id, base::BindOnce([](InUseAuthSession auth_session) {
        ASSERT_THAT(auth_session.AuthSessionStatus(), IsOk());
        ASSERT_THAT(auth_session->OnUserCreated(), IsOk());
      }));
  RunUntilIdle();

  // Mount user vault.
  user_data_auth::PreparePersistentVaultRequest prepare_request;
  prepare_request.set_auth_session_id(reply_future.Get().auth_session_id());
  TestFuture<user_data_auth::PreparePersistentVaultReply> prepare_future;

  userdataauth_->PreparePersistentVault(
      prepare_request,
      prepare_future
          .GetCallback<const user_data_auth::PreparePersistentVaultReply&>());
  RunUntilIdle();
  ASSERT_EQ(prepare_future.Get().error_info().primary_action(),
            user_data_auth::PrimaryAction::PRIMARY_NO_ERROR);

  EXPECT_CALL(system_apis_.platform, GetMountsBySourcePrefix(_, _))
      .Times(4)
      .WillRepeatedly(Invoke(StaleShadowMounts));
  EXPECT_CALL(system_apis_.platform, GetAttachedLoopDevices())
      .WillRepeatedly(Return(std::vector<libstorage::Platform::LoopDevice>()));
  EXPECT_CALL(system_apis_.platform, GetLoopDeviceMounts(_))
      .WillOnce(Return(false));
  EXPECT_CALL(
      system_apis_.platform,
      EnumerateDirectoryEntries(
          FilePath(kEphemeralCryptohomeDir).Append(kSparseFileDir), _, _))
      .WillOnce(Return(false));

  // Only 5 look ups: user/1 and root/1 are owned, children of these
  // directories are excluded. ExpireMount is expected to run on exactly the
  // same mount points that are expected to be unmounted below. But it is
  // important to check the number of calls here to make sure ExpireMount
  // doesn't run on any other mount points.
  EXPECT_CALL(system_apis_.platform, ExpireMount(_)).Times(5);

  EXPECT_CALL(*session_, OwnsMountPoint(_)).WillRepeatedly(Return(false));
  EXPECT_CALL(*session_, OwnsMountPoint(FilePath("/home/user/1")))
      .WillOnce(Return(true));
  EXPECT_CALL(*session_, OwnsMountPoint(FilePath("/home/root/1")))
      .WillOnce(Return(true));

  EXPECT_CALL(system_apis_.platform,
              Unmount(Property(&FilePath::value, EndsWith("/0")), true, _))
      .Times(2)
      .WillRepeatedly(Return(true));
  EXPECT_CALL(system_apis_.platform,
              Unmount(FilePath("/home/chronos/user"), true, _))
      .WillOnce(Return(true));
  EXPECT_CALL(
      system_apis_.platform,
      Unmount(Property(&FilePath::value, EndsWith("user/MyFiles/Downloads")),
              true, _))
      .WillOnce(Return(true));
  EXPECT_CALL(system_apis_.platform,
              Unmount(FilePath("/daemon-store/server/a"), true, _))
      .WillOnce(Return(true));

  std::vector<std::string> fake_token_list;
  fake_token_list.push_back("/home/chronos/user/token");
  fake_token_list.push_back("/home/user/1/token");
  fake_token_list.push_back("/home/root/1/token");
  EXPECT_CALL(chaps_client_, GetTokenList(_, _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(fake_token_list), Return(true)));

  EXPECT_CALL(chaps_client_,
              UnloadToken(_, FilePath("/home/chronos/user/token")))
      .Times(1);

  // Expect that CleanUpStaleMounts() tells us it skipped mounts since 1 is
  // still logged in.
  EXPECT_TRUE(userdataauth_->CleanUpStaleMounts(false));
}

TEST_F(UserDataAuthTest, StartMigrateToDircryptoValidity) {
  constexpr char kUsername1[] = "foo@gmail.com";

  user_data_auth::StartMigrateToDircryptoRequest request;
  request.mutable_account_id()->set_account_id(kUsername1);
  request.set_minimal_migration(false);

  SetupMount(kUsername1);

  EXPECT_CALL(*session_, MigrateVault(_, MigrationType::FULL))
      .WillOnce(Return(true));

  int success_cnt = 0;
  {
    TestFuture<user_data_auth::StartMigrateToDircryptoReply> reply_future;
    userdataauth_->StartMigrateToDircrypto(
        request,
        reply_future
            .GetCallback<const user_data_auth::StartMigrateToDircryptoReply&>(),
        base::BindRepeating(
            [](int* success_cnt_ptr,
               const user_data_auth::DircryptoMigrationProgress& progress) {
              EXPECT_EQ(progress.status(),
                        user_data_auth::DIRCRYPTO_MIGRATION_SUCCESS);
              (*success_cnt_ptr)++;
            },
            base::Unretained(&success_cnt)));
    EXPECT_THAT(reply_future.Get().has_error_info(), IsFalse());
  }
  EXPECT_EQ(success_cnt, 1);
}

TEST_F(UserDataAuthTest, StartMigrateToDircryptoFailure) {
  constexpr char kUsername1[] = "foo@gmail.com";

  user_data_auth::StartMigrateToDircryptoRequest request;
  request.mutable_account_id()->set_account_id(kUsername1);
  request.set_minimal_migration(false);

  // Test mount non-existent.
  int call_cnt = 0;
  {
    TestFuture<user_data_auth::StartMigrateToDircryptoReply> reply_future;
    userdataauth_->StartMigrateToDircrypto(
        request,
        reply_future
            .GetCallback<const user_data_auth::StartMigrateToDircryptoReply&>(),
        base::BindRepeating(
            [](int* call_cnt_ptr,
               const user_data_auth::DircryptoMigrationProgress& progress) {
              EXPECT_EQ(progress.status(),
                        user_data_auth::DIRCRYPTO_MIGRATION_FAILED);
              (*call_cnt_ptr)++;
            },
            base::Unretained(&call_cnt)));
    EXPECT_THAT(reply_future.Get().has_error_info(), IsFalse());
  }
  EXPECT_EQ(call_cnt, 1);

  // Test MigrateToDircrypto failed
  SetupMount(kUsername1);

  EXPECT_CALL(*session_, MigrateVault(_, MigrationType::FULL))
      .WillOnce(Return(false));

  call_cnt = 0;
  {
    TestFuture<user_data_auth::StartMigrateToDircryptoReply> reply_future;
    userdataauth_->StartMigrateToDircrypto(
        request,
        reply_future
            .GetCallback<const user_data_auth::StartMigrateToDircryptoReply&>(),
        base::BindRepeating(
            [](int* call_cnt_ptr,
               const user_data_auth::DircryptoMigrationProgress& progress) {
              EXPECT_EQ(progress.status(),
                        user_data_auth::DIRCRYPTO_MIGRATION_FAILED);
              (*call_cnt_ptr)++;
            },
            base::Unretained(&call_cnt)));
    EXPECT_THAT(reply_future.Get().has_error_info(), IsFalse());
  }
  EXPECT_EQ(call_cnt, 1);
}

TEST_F(UserDataAuthTest, NeedsDircryptoMigration) {
  bool result;
  AccountIdentifier account;
  account.set_account_id("foo@gmail.com");

  // Test the case when we are forced to use eCryptfs, and thus no migration is
  // needed.
  userdataauth_->set_force_ecryptfs(true);
  EXPECT_CALL(homedirs_, Exists(_)).WillOnce(Return(true));
  EXPECT_EQ(userdataauth_->NeedsDircryptoMigration(account, &result),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  EXPECT_FALSE(result);

  // Test the case when dircrypto is already in use.
  userdataauth_->set_force_ecryptfs(false);
  EXPECT_CALL(homedirs_, NeedsDircryptoMigration(_)).WillOnce(Return(false));
  EXPECT_CALL(homedirs_, Exists(_)).WillOnce(Return(true));
  EXPECT_EQ(userdataauth_->NeedsDircryptoMigration(account, &result),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  EXPECT_FALSE(result);

  // Test the case when eCryptfs is being used.
  userdataauth_->set_force_ecryptfs(false);
  EXPECT_CALL(homedirs_, NeedsDircryptoMigration(_)).WillOnce(Return(true));
  EXPECT_CALL(homedirs_, Exists(_)).WillOnce(Return(true));
  EXPECT_EQ(userdataauth_->NeedsDircryptoMigration(account, &result),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  EXPECT_TRUE(result);

  // Test for account not found.
  EXPECT_CALL(homedirs_, Exists(_)).WillOnce(Return(false));
  EXPECT_EQ(userdataauth_->NeedsDircryptoMigration(account, &result),
            user_data_auth::CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND);
}

TEST_F(UserDataAuthTest, LowEntropyCredentialSupported) {
  EXPECT_CALL(system_apis_.hwsec, IsPinWeaverEnabled())
      .WillRepeatedly(ReturnValue(false));
  EXPECT_FALSE(userdataauth_->IsLowEntropyCredentialSupported());

  EXPECT_CALL(system_apis_.hwsec, IsPinWeaverEnabled())
      .WillRepeatedly(ReturnValue(true));
  EXPECT_TRUE(userdataauth_->IsLowEntropyCredentialSupported());
}

TEST_F(UserDataAuthTest, GetAccountDiskUsage) {
  // Test when the user is non-existent.
  AccountIdentifier account;
  account.set_account_id("non_existent_user");

  EXPECT_EQ(0, userdataauth_->GetAccountDiskUsage(account));

  // Test when the user exists and home directory is not empty.
  const Username kUsername1("foo@gmail.com");
  account.set_account_id(*kUsername1);

  constexpr int64_t kHomedirSize = 12345678912345;
  EXPECT_CALL(homedirs_, ComputeDiskUsage(SanitizeUserName(kUsername1)))
      .WillOnce(Return(kHomedirSize));
  EXPECT_EQ(kHomedirSize, userdataauth_->GetAccountDiskUsage(account));
}

TEST_F(UserDataAuthTest, LowDiskSpaceHandlerStopped) {
  EXPECT_CALL(low_disk_space_handler_, Stop());
}

TEST_F(UserDataAuthTest, SetUserDataStorageWriteEnabled) {
  constexpr char kUsername1[] = "foo@gmail.com";

  user_data_auth::SetUserDataStorageWriteEnabledRequest request;
  request.mutable_account_id()->set_account_id(kUsername1);

  SetupMount(kUsername1);

  EXPECT_CALL(*session_, IsActive()).WillRepeatedly(Return(true));
  EXPECT_CALL(*session_, EnableWriteUserDataStorage(false))
      .WillOnce(Return(true));
  request.set_enabled(false);
  {
    user_data_auth::SetUserDataStorageWriteEnabledReply reply =
        userdataauth_->SetUserDataStorageWriteEnabled(request);
    EXPECT_THAT(reply.has_error_info(), IsFalse());
  }

  EXPECT_CALL(*session_, EnableWriteUserDataStorage(true))
      .WillOnce(Return(true));
  request.set_enabled(true);
  {
    user_data_auth::SetUserDataStorageWriteEnabledReply reply =
        userdataauth_->SetUserDataStorageWriteEnabled(request);
    EXPECT_THAT(reply.has_error_info(), IsFalse());
  }
}

TEST_F(UserDataAuthTest, SetUserDataStorageWriteEnabledNoSession) {
  constexpr char kUsername1[] = "foo@gmail.com";

  user_data_auth::SetUserDataStorageWriteEnabledRequest request;
  request.mutable_account_id()->set_account_id(kUsername1);

  request.set_enabled(false);
  {
    user_data_auth::SetUserDataStorageWriteEnabledReply reply =
        userdataauth_->SetUserDataStorageWriteEnabled(request);
    EXPECT_TRUE(reply.has_error_info());
    EXPECT_THAT(
        reply.error_info(),
        HasPossibleAction(user_data_auth::PossibleAction::POSSIBLY_REBOOT));
  }
}

// A test fixture with some utility functions for testing mount and keys related
// functionalities.
class UserDataAuthExTest : public UserDataAuthTest {
 public:
  UserDataAuthExTest() = default;
  UserDataAuthExTest(const UserDataAuthExTest&) = delete;
  UserDataAuthExTest& operator=(const UserDataAuthExTest&) = delete;

  // Create a USS with wrapped keys registered for all of the given labels. Note
  // that the generated USS will not contain any "real" keys.
  void MakeUssWithLabelsAndRateLimiter(
      const ObfuscatedUsername& obfuscated_username,
      const std::vector<std::string>& labels,
      bool create_rate_limiter) {
    // Create a random USS.
    UserUssStorage user_storage(system_apis_.uss_storage, obfuscated_username);
    auto uss = DecryptedUss::CreateWithRandomMainKey(
        user_storage, FileSystemKeyset::CreateRandom());
    if (!uss.ok()) {
      ADD_FAILURE() << "Making a test USS failed at CreateRandom: "
                    << uss.status();
      return;
    }
    {
      auto transaction = uss->StartTransaction();
      // Generate a main key and some wrap it for each label. Note that we just
      // make up junk wrapping keys because we don't actually plan to decrypt
      // the container.
      for (const std::string& label : labels) {
        SecureBlob wrapping_key(kAesGcm256KeySize, 0xC0);
        CryptohomeStatus status =
            transaction.InsertWrappedMainKey(label, std::move(wrapping_key));
        if (!status.ok()) {
          ADD_FAILURE() << "Making a test USS failed adding label " << label
                        << ": " << status;
          return;
        }
      }

      if (create_rate_limiter) {
        CryptohomeStatus status =
            transaction.InitializeFingerprintRateLimiterId(0x10);
        if (!status.ok()) {
          ADD_FAILURE()
              << "Making a test USS failed adding fingerprint rate-limiter.";
          return;
        }
      }

      auto status = std::move(transaction).Commit();
      if (!status.ok()) {
        ADD_FAILURE() << "Making a test USS failed during Commit: " << status;
        return;
      }
    }
    auto status = system_apis_.uss_manager.AddDecrypted(obfuscated_username,
                                                        std::move(*uss));
    if (!status.ok()) {
      ADD_FAILURE() << "Making a test USS failed during AddDecrypted: "
                    << status.status();
      return;
    }
  }

  void MakeUssWithLabels(const ObfuscatedUsername& obfuscated_username,
                         const std::vector<std::string>& labels) {
    MakeUssWithLabelsAndRateLimiter(obfuscated_username, labels, false);
  }

 protected:
  void PrepareArguments() {
    remove_homedir_req_ = std::make_unique<user_data_auth::RemoveRequest>();
    start_auth_session_req_ =
        std::make_unique<user_data_auth::StartAuthSessionRequest>();
    start_auth_session_req_->set_intent(user_data_auth::AUTH_INTENT_DECRYPT);
  }

  template <class ProtoBuf>
  brillo::Blob BlobFromProtobuf(const ProtoBuf& pb) {
    std::string serialized;
    CHECK(pb.SerializeToString(&serialized));
    return brillo::BlobFromString(serialized);
  }

  template <class ProtoBuf>
  brillo::SecureBlob SecureBlobFromProtobuf(const ProtoBuf& pb) {
    std::string serialized;
    CHECK(pb.SerializeToString(&serialized));
    return brillo::SecureBlob(serialized);
  }

  std::unique_ptr<user_data_auth::RemoveRequest> remove_homedir_req_;
  std::unique_ptr<user_data_auth::StartAuthSessionRequest>
      start_auth_session_req_;

  // Mock to use to capture any signals sent.
  NiceMock<MockSignalling> signalling_;

  const Username kUser{"chromeos-user"};
  static constexpr char kKey[] = "274146c6e8886a843ddfea373e2dc71b";
};

constexpr char UserDataAuthExTest::kKey[];

TEST_F(UserDataAuthExTest,
       StartMigrateToDircryptoWithAuthenticatedAuthSession) {
  // Setup.
  PrepareArguments();
  constexpr char kUsername1[] = "foo@gmail.com";

  start_auth_session_req_->mutable_account_id()->set_account_id(kUsername1);
  TestFuture<user_data_auth::StartAuthSessionReply> auth_session_reply_future;
  userdataauth_->StartAuthSession(
      *start_auth_session_req_,
      auth_session_reply_future
          .GetCallback<const user_data_auth::StartAuthSessionReply&>());
  RunUntilIdle();
  EXPECT_EQ(auth_session_reply_future.Get().error(),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  std::optional<base::UnguessableToken> auth_session_id =
      AuthSession::GetTokenFromSerializedString(
          auth_session_reply_future.Get().auth_session_id());
  ASSERT_TRUE(auth_session_id.has_value());

  // Get the session into an authenticated state by treating it as if we just
  // freshly created the user.
  userdataauth_->auth_session_manager_->RunWhenAvailable(
      *auth_session_id, base::BindOnce([](InUseAuthSession auth_session) {
        ASSERT_THAT(auth_session.AuthSessionStatus(), IsOk());
        ASSERT_THAT(auth_session->OnUserCreated(), IsOk());
      }));
  RunUntilIdle();

  user_data_auth::StartMigrateToDircryptoRequest request;
  request.set_auth_session_id(
      auth_session_reply_future.Get().auth_session_id());
  request.set_minimal_migration(false);

  SetupMount(kUsername1);

  EXPECT_CALL(*session_, MigrateVault(_, MigrationType::FULL))
      .WillOnce(Return(true));

  int success_cnt = 0;
  {
    TestFuture<user_data_auth::StartMigrateToDircryptoReply> reply_future;
    userdataauth_->StartMigrateToDircrypto(
        request,
        reply_future
            .GetCallback<const user_data_auth::StartMigrateToDircryptoReply&>(),
        base::BindRepeating(
            [](int* success_cnt_ptr,
               const user_data_auth::DircryptoMigrationProgress& progress) {
              EXPECT_EQ(progress.status(),
                        user_data_auth::DIRCRYPTO_MIGRATION_SUCCESS);
              (*success_cnt_ptr)++;
            },
            base::Unretained(&success_cnt)));
    RunUntilIdle();
    EXPECT_THAT(reply_future.Get().has_error_info(), IsFalse());
  }
  EXPECT_EQ(success_cnt, 1);
}

TEST_F(UserDataAuthExTest,
       StartMigrateToDircryptoWithUnAuthenticatedAuthSession) {
  // Setup.
  PrepareArguments();
  constexpr char kUsername1[] = "foo@gmail.com";

  start_auth_session_req_->mutable_account_id()->set_account_id(kUsername1);
  TestFuture<user_data_auth::StartAuthSessionReply> auth_session_reply_future;
  userdataauth_->StartAuthSession(
      *start_auth_session_req_,
      auth_session_reply_future
          .GetCallback<const user_data_auth::StartAuthSessionReply&>());
  RunUntilIdle();
  EXPECT_EQ(auth_session_reply_future.Get().error(),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  std::optional<base::UnguessableToken> auth_session_id =
      AuthSession::GetTokenFromSerializedString(
          auth_session_reply_future.Get().auth_session_id());
  ASSERT_TRUE(auth_session_id.has_value());

  userdataauth_->auth_session_manager_->RunWhenAvailable(
      *auth_session_id, base::BindOnce([](InUseAuthSession auth_session) {
        ASSERT_THAT(auth_session.AuthSessionStatus(), IsOk());
      }));
  RunUntilIdle();

  user_data_auth::StartMigrateToDircryptoRequest request;
  request.set_auth_session_id(
      auth_session_reply_future.Get().auth_session_id());
  request.set_minimal_migration(false);

  int called_ctr = 0;
  {
    TestFuture<user_data_auth::StartMigrateToDircryptoReply> reply_future;
    userdataauth_->StartMigrateToDircrypto(
        request,
        reply_future
            .GetCallback<const user_data_auth::StartMigrateToDircryptoReply&>(),
        base::BindRepeating(
            [](int* called_ctr_ptr,
               const user_data_auth::DircryptoMigrationProgress& progress) {
              EXPECT_EQ(progress.status(),
                        user_data_auth::DIRCRYPTO_MIGRATION_FAILED);
              (*called_ctr_ptr)++;
            },
            base::Unretained(&called_ctr)));
    RunUntilIdle();
    EXPECT_THAT(reply_future.Get().has_error_info(), IsFalse());
  }
  EXPECT_EQ(called_ctr, 1);
}

TEST_F(UserDataAuthExTest, StartMigrateToDircryptoWithInvalidAuthSession) {
  // Setup.
  PrepareArguments();
  constexpr char kFakeAuthSessionId[] = "foo";
  user_data_auth::StartMigrateToDircryptoRequest request;
  request.set_auth_session_id(kFakeAuthSessionId);
  request.set_minimal_migration(false);

  int called_ctr = 0;
  {
    TestFuture<user_data_auth::StartMigrateToDircryptoReply> reply_future;
    userdataauth_->StartMigrateToDircrypto(
        request,
        reply_future
            .GetCallback<const user_data_auth::StartMigrateToDircryptoReply&>(),
        base::BindRepeating(
            [](int* called_ctr_ptr,
               const user_data_auth::DircryptoMigrationProgress& progress) {
              EXPECT_EQ(progress.status(),
                        user_data_auth::DIRCRYPTO_MIGRATION_FAILED);
              (*called_ctr_ptr)++;
            },
            base::Unretained(&called_ctr)));
    RunUntilIdle();
    EXPECT_THAT(reply_future.Get().has_error_info(), IsFalse());
  }
  EXPECT_EQ(called_ctr, 1);
}

TEST_F(UserDataAuthExTest, RemoveValidity) {
  // Setup.
  PrepareArguments();
  const Username kUsername1("foo@gmail.com");
  MakeUssWithLabels(GetObfuscatedUsername(kUsername1), {"password"});
  remove_homedir_req_->mutable_identifier()->set_account_id(*kUsername1);
  userdataauth_->SetSignallingInterface(signalling_);

  // Test for successful case.
  EXPECT_CALL(homedirs_, Remove(GetObfuscatedUsername(kUsername1)))
      .WillOnce(Return(true));
  user_data_auth::RemoveCompleted remove_completed;
  EXPECT_CALL(signalling_, SendRemoveCompleted)
      .WillOnce([&](user_data_auth::RemoveCompleted signal) {
        remove_completed = signal;
      });
  TestFuture<user_data_auth::RemoveReply> remove_reply_future1;
  userdataauth_->Remove(
      *remove_homedir_req_,
      remove_reply_future1.GetCallback<const user_data_auth::RemoveReply&>());
  RunUntilIdle();
  EXPECT_EQ(remove_reply_future1.Get().error_info().primary_action(),
            user_data_auth::PrimaryAction::PRIMARY_NO_ERROR);

  // The USS state should have been removed. Test by adding the same user's USS
  // again.
  MakeUssWithLabels(GetObfuscatedUsername(kUsername1), {"password"});

  // Verify signal was called;
  EXPECT_EQ(*GetObfuscatedUsername(kUsername1),
            remove_completed.sanitized_username());

  // Test for unsuccessful case.
  EXPECT_CALL(homedirs_, Remove(GetObfuscatedUsername(kUsername1)))
      .WillOnce(Return(false));
  TestFuture<user_data_auth::RemoveReply> remove_reply_future2;
  userdataauth_->Remove(
      *remove_homedir_req_,
      remove_reply_future2.GetCallback<const user_data_auth::RemoveReply&>());
  RunUntilIdle();
  EXPECT_NE(remove_reply_future2.Get().error_info().primary_action(),
            user_data_auth::PrimaryAction::PRIMARY_NO_ERROR);
}

TEST_F(UserDataAuthExTest, RemoveBusyMounted) {
  PrepareArguments();
  SetupMount(*kUser);
  remove_homedir_req_->mutable_identifier()->set_account_id(*kUser);
  userdataauth_->SetSignallingInterface(signalling_);
  ON_CALL(*session_, IsActive()).WillByDefault(Return(true));
  EXPECT_CALL(signalling_, SendRemoveCompleted).Times(0);
  TestFuture<user_data_auth::RemoveReply> remove_reply_future;
  userdataauth_->Remove(
      *remove_homedir_req_,
      remove_reply_future.GetCallback<const user_data_auth::RemoveReply&>());
  RunUntilIdle();
  EXPECT_NE(remove_reply_future.Get().error_info().primary_action(),
            user_data_auth::PrimaryAction::PRIMARY_NO_ERROR);
}

TEST_F(UserDataAuthExTest, RemoveInvalidArguments) {
  PrepareArguments();
  userdataauth_->SetSignallingInterface(signalling_);

  // No account_id and AuthSession ID
  EXPECT_CALL(signalling_, SendRemoveCompleted).Times(0);
  TestFuture<user_data_auth::RemoveReply> remove_reply_future1;
  userdataauth_->Remove(
      *remove_homedir_req_,
      remove_reply_future1.GetCallback<const user_data_auth::RemoveReply&>());
  RunUntilIdle();
  EXPECT_NE(remove_reply_future1.Get().error_info().primary_action(),
            user_data_auth::PrimaryAction::PRIMARY_NO_ERROR);

  // Empty account_id
  remove_homedir_req_->mutable_identifier()->set_account_id("");
  TestFuture<user_data_auth::RemoveReply> remove_reply_future2;
  userdataauth_->Remove(
      *remove_homedir_req_,
      remove_reply_future2.GetCallback<const user_data_auth::RemoveReply&>());
  RunUntilIdle();
  EXPECT_NE(remove_reply_future2.Get().error_info().primary_action(),
            user_data_auth::PrimaryAction::PRIMARY_NO_ERROR);
}

TEST_F(UserDataAuthExTest, RemoveInvalidAuthSession) {
  PrepareArguments();
  std::string invalid_token = "invalid_token_16";
  remove_homedir_req_->set_auth_session_id(invalid_token);
  userdataauth_->SetSignallingInterface(signalling_);
  EXPECT_CALL(signalling_, SendRemoveCompleted).Times(0);

  // Test.
  TestFuture<user_data_auth::RemoveReply> remove_reply_future;
  userdataauth_->Remove(
      *remove_homedir_req_,
      remove_reply_future.GetCallback<const user_data_auth::RemoveReply&>());
  RunUntilIdle();
  EXPECT_NE(remove_reply_future.Get().error_info().primary_action(),
            user_data_auth::PrimaryAction::PRIMARY_NO_ERROR);
}

TEST_F(UserDataAuthExTest, RemoveValidityWithAuthSession) {
  PrepareArguments();

  // Setup
  const Username kUsername1("foo@gmail.com");
  userdataauth_->SetSignallingInterface(signalling_);
  user_data_auth::RemoveCompleted remove_completed;
  EXPECT_CALL(signalling_, SendRemoveCompleted)
      .WillOnce([&](user_data_auth::RemoveCompleted signal) {
        remove_completed = signal;
      });

  start_auth_session_req_->mutable_account_id()->set_account_id(*kUsername1);
  TestFuture<user_data_auth::StartAuthSessionReply> auth_session_reply_future;
  userdataauth_->StartAuthSession(
      *start_auth_session_req_,
      auth_session_reply_future
          .GetCallback<const user_data_auth::StartAuthSessionReply&>());
  RunUntilIdle();
  EXPECT_EQ(auth_session_reply_future.Get().error(),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  const std::string auth_session_id =
      auth_session_reply_future.Get().auth_session_id();

  // Test
  remove_homedir_req_->set_auth_session_id(auth_session_id);
  EXPECT_CALL(homedirs_, Remove(GetObfuscatedUsername(kUsername1)))
      .WillOnce(Return(true));
  TestFuture<user_data_auth::RemoveReply> remove_reply_future;
  userdataauth_->Remove(
      *remove_homedir_req_,
      remove_reply_future.GetCallback<const user_data_auth::RemoveReply&>());
  RunUntilIdle();
  EXPECT_EQ(remove_reply_future.Get().error_info().primary_action(),
            user_data_auth::PrimaryAction::PRIMARY_NO_ERROR);

  // Verify
  userdataauth_->auth_session_manager_->RunWhenAvailable(
      auth_session_id, base::BindOnce([](InUseAuthSession auth_session) {
        ASSERT_THAT(auth_session.AuthSessionStatus(), NotOk());
      }));
  RunUntilIdle();
  EXPECT_EQ(*GetObfuscatedUsername(kUsername1),
            remove_completed.sanitized_username());
}

TEST_F(UserDataAuthExTest, StartAuthSession) {
  PrepareArguments();
  start_auth_session_req_->mutable_account_id()->set_account_id(
      "foo@example.com");
  TestFuture<user_data_auth::StartAuthSessionReply> auth_session_reply_future;
  userdataauth_->StartAuthSession(
      *start_auth_session_req_,
      auth_session_reply_future
          .GetCallback<const user_data_auth::StartAuthSessionReply&>());
  RunUntilIdle();
  EXPECT_EQ(auth_session_reply_future.Get().error(),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  std::optional<base::UnguessableToken> auth_session_id =
      AuthSession::GetTokenFromSerializedString(
          auth_session_reply_future.Get().auth_session_id());
  ASSERT_TRUE(auth_session_id.has_value());
  std::optional<base::UnguessableToken> broadcast_id =
      AuthSession::GetTokenFromSerializedString(
          auth_session_reply_future.Get().broadcast_id());
  ASSERT_TRUE(broadcast_id.has_value());
  userdataauth_->auth_session_manager_->RunWhenAvailable(
      *auth_session_id,
      base::BindOnce(
          [](base::UnguessableToken token, base::UnguessableToken public_token,
             InUseAuthSession auth_session) {
            ASSERT_THAT(auth_session.AuthSessionStatus(), IsOk());
            ASSERT_THAT(auth_session->token(), Eq(token));
            ASSERT_THAT(auth_session->public_token(), Eq(public_token));
          },
          *auth_session_id, *broadcast_id));
  RunUntilIdle();
}

TEST_F(UserDataAuthExTest, StartAuthSessionUnusableClobber) {
  PrepareArguments();
  start_auth_session_req_->mutable_account_id()->set_account_id(
      "foo@example.com");
  EXPECT_CALL(system_apis_.platform, DirectoryExists(_)).WillOnce(Return(true));
  EXPECT_CALL(system_apis_.platform, GetFileEnumerator(_, _, _, std::string()))
      .WillOnce(Return(new NiceMock<libstorage::MockFileEnumerator>));
  TestFuture<user_data_auth::StartAuthSessionReply> auth_session_reply_future;
  userdataauth_->StartAuthSession(
      *start_auth_session_req_,
      auth_session_reply_future
          .GetCallback<const user_data_auth::StartAuthSessionReply&>());
  RunUntilIdle();
  EXPECT_EQ(auth_session_reply_future.Get().error(),
            user_data_auth::CRYPTOHOME_ERROR_UNUSABLE_VAULT);
  std::optional<base::UnguessableToken> auth_session_id =
      AuthSession::GetTokenFromSerializedString(
          auth_session_reply_future.Get().auth_session_id());
  EXPECT_TRUE(auth_session_id.has_value());
  userdataauth_->auth_session_manager_->RunWhenAvailable(
      *auth_session_id, base::BindOnce([](InUseAuthSession auth_session) {
        ASSERT_THAT(auth_session.AuthSessionStatus(), IsOk());
      }));
  RunUntilIdle();
}

TEST_F(UserDataAuthExTest, InvalidateAuthSession) {
  // Setup.
  PrepareArguments();
  start_auth_session_req_->mutable_account_id()->set_account_id(
      "foo@example.com");
  TestFuture<user_data_auth::StartAuthSessionReply> auth_session_reply_future;
  userdataauth_->StartAuthSession(
      *start_auth_session_req_,
      auth_session_reply_future
          .GetCallback<const user_data_auth::StartAuthSessionReply&>());
  RunUntilIdle();
  EXPECT_EQ(auth_session_reply_future.Get().error(),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  std::optional<base::UnguessableToken> auth_session_id =
      AuthSession::GetTokenFromSerializedString(
          auth_session_reply_future.Get().auth_session_id());
  EXPECT_TRUE(auth_session_id.has_value());
  userdataauth_->auth_session_manager_->RunWhenAvailable(
      *auth_session_id, base::BindOnce([](InUseAuthSession auth_session) {
        ASSERT_THAT(auth_session.AuthSessionStatus(), IsOk());
      }));
  RunUntilIdle();

  // Test.
  user_data_auth::InvalidateAuthSessionRequest inv_auth_session_req;
  inv_auth_session_req.set_auth_session_id(
      auth_session_reply_future.Get().auth_session_id());

  // Invalidate the AuthSession immediately.
  TestFuture<user_data_auth::InvalidateAuthSessionReply> reply_future;
  userdataauth_->InvalidateAuthSession(
      inv_auth_session_req,
      reply_future
          .GetCallback<const user_data_auth::InvalidateAuthSessionReply&>());
  EXPECT_EQ(reply_future.Get().error(),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  userdataauth_->auth_session_manager_->RunWhenAvailable(
      *auth_session_id, base::BindOnce([](InUseAuthSession auth_session) {
        ASSERT_THAT(auth_session.AuthSessionStatus(), NotOk());
      }));
  RunUntilIdle();
}

TEST_F(UserDataAuthExTest, ExtendAuthSession) {
  // Setup.
  PrepareArguments();

  start_auth_session_req_->mutable_account_id()->set_account_id(
      "foo@example.com");
  TestFuture<user_data_auth::StartAuthSessionReply> auth_session_reply_future;
  userdataauth_->StartAuthSession(
      *start_auth_session_req_,
      auth_session_reply_future
          .GetCallback<const user_data_auth::StartAuthSessionReply&>());
  RunUntilIdle();
  EXPECT_EQ(auth_session_reply_future.Get().error(),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  std::optional<base::UnguessableToken> auth_session_id =
      AuthSession::GetTokenFromSerializedString(
          auth_session_reply_future.Get().auth_session_id());
  EXPECT_TRUE(auth_session_id.has_value());

  // Get the session into an authenticated state by treating it as if we just
  // freshly created the user.
  userdataauth_->auth_session_manager_->RunWhenAvailable(
      *auth_session_id, base::BindOnce([](InUseAuthSession auth_session) {
        ASSERT_THAT(auth_session.AuthSessionStatus(), IsOk());
        ASSERT_THAT(auth_session->OnUserCreated(), IsOk());
      }));
  RunUntilIdle();

  // Test.
  user_data_auth::ExtendAuthSessionRequest ext_auth_session_req;
  ext_auth_session_req.set_auth_session_id(
      auth_session_reply_future.Get().auth_session_id());
  ext_auth_session_req.set_extension_duration(kAuthSessionExtensionDuration);

  // Extend the AuthSession.
  TestFuture<user_data_auth::ExtendAuthSessionReply> reply_future;
  userdataauth_->ExtendAuthSession(
      ext_auth_session_req,
      reply_future
          .GetCallback<const user_data_auth::ExtendAuthSessionReply&>());
  RunUntilIdle();
  EXPECT_EQ(reply_future.Get().error(),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  EXPECT_TRUE(reply_future.Get().has_seconds_left());
  EXPECT_GT(reply_future.Get().seconds_left(), kAuthSessionExtensionDuration);

  // Verify that timer has changed, within a resaonsable degree of error.
  userdataauth_->auth_session_manager_->RunWhenAvailable(
      *auth_session_id, base::BindOnce([](InUseAuthSession auth_session) {
        ASSERT_THAT(auth_session.AuthSessionStatus(), IsOk());
        auto requested_delay = auth_session.GetRemainingTime();
        auto time_difference = kAuthSessionTimeout - requested_delay;
        EXPECT_LT(time_difference, base::Seconds(1));
      }));
  RunUntilIdle();
}

TEST_F(UserDataAuthExTest, ExtendUnAuthenticatedAuthSessionFail) {
  // Setup.
  PrepareArguments();

  start_auth_session_req_->mutable_account_id()->set_account_id(
      "foo@example.com");
  TestFuture<user_data_auth::StartAuthSessionReply> auth_session_reply_future;
  userdataauth_->StartAuthSession(
      *start_auth_session_req_,
      auth_session_reply_future
          .GetCallback<const user_data_auth::StartAuthSessionReply&>());
  RunUntilIdle();
  EXPECT_EQ(auth_session_reply_future.Get().error(),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  std::optional<base::UnguessableToken> auth_session_id =
      AuthSession::GetTokenFromSerializedString(
          auth_session_reply_future.Get().auth_session_id());
  EXPECT_TRUE(auth_session_id.has_value());
  userdataauth_->auth_session_manager_->RunWhenAvailable(
      *auth_session_id, base::BindOnce([](InUseAuthSession auth_session) {
        ASSERT_THAT(auth_session.AuthSessionStatus(), IsOk());
      }));
  RunUntilIdle();

  // Test.
  user_data_auth::ExtendAuthSessionRequest ext_auth_session_req;
  ext_auth_session_req.set_auth_session_id(
      auth_session_reply_future.Get().auth_session_id());
  ext_auth_session_req.set_extension_duration(kAuthSessionExtensionDuration);

  // Extend the AuthSession.
  TestFuture<user_data_auth::ExtendAuthSessionReply> reply_future;
  userdataauth_->ExtendAuthSession(
      ext_auth_session_req,
      reply_future
          .GetCallback<const user_data_auth::ExtendAuthSessionReply&>());
  RunUntilIdle();
  EXPECT_EQ(reply_future.Get().error(),
            user_data_auth::CRYPTOHOME_ERROR_UNAUTHENTICATED_AUTH_SESSION);
  EXPECT_FALSE(reply_future.Get().has_seconds_left());
}

TEST_F(UserDataAuthExTest, CheckTimeoutTimerSetAfterAuthentication) {
  // Setup.
  PrepareArguments();

  start_auth_session_req_->mutable_account_id()->set_account_id(
      "foo@example.com");
  TestFuture<user_data_auth::StartAuthSessionReply> auth_session_reply_future;
  userdataauth_->StartAuthSession(
      *start_auth_session_req_,
      auth_session_reply_future
          .GetCallback<const user_data_auth::StartAuthSessionReply&>());
  RunUntilIdle();
  EXPECT_EQ(auth_session_reply_future.Get().error(),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  std::optional<base::UnguessableToken> auth_session_id =
      AuthSession::GetTokenFromSerializedString(
          auth_session_reply_future.Get().auth_session_id());
  EXPECT_TRUE(auth_session_id.has_value());

  userdataauth_->auth_session_manager_->RunWhenAvailable(
      *auth_session_id, base::BindOnce([](InUseAuthSession auth_session) {
        ASSERT_THAT(auth_session.AuthSessionStatus(), IsOk());

        // Timer is not set before authentication.
        EXPECT_TRUE(auth_session.GetRemainingTime().is_max());
        // Extension only happens for authenticated auth session.
        EXPECT_THAT(auth_session->OnUserCreated(), IsOk());
        // Test timer is correctly set after authentication.
        EXPECT_FALSE(auth_session.GetRemainingTime().is_max());
      }));
  RunUntilIdle();
}

TEST_F(UserDataAuthExTest, StartAuthSessionReplyCheck) {
  PrepareArguments();
  // Setup
  start_auth_session_req_->mutable_account_id()->set_account_id(
      "foo@example.com");

  KeyData key_data;
  key_data.set_label(kFakeLabel);
  key_data.set_type(KeyData::KEY_TYPE_PASSWORD);

  EXPECT_CALL(system_apis_.platform, DirectoryExists(_))
      .WillRepeatedly(Return(true));
  std::vector<int> vk_indicies = {0};
  EXPECT_CALL(system_apis_.keyset_management, GetVaultKeysets(_, _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(vk_indicies), Return(true)));
  EXPECT_CALL(system_apis_.keyset_management, LoadVaultKeysetForUser(_, 0))
      .WillRepeatedly([key_data](const ObfuscatedUsername&, int) {
        auto vk = std::make_unique<VaultKeyset>();
        vk->SetFlags(SerializedVaultKeyset::TPM_WRAPPED |
                     SerializedVaultKeyset::PCR_BOUND);
        vk->SetKeyData(key_data);
        vk->SetTPMKey(BlobFromString("fake tpm key"));
        vk->SetExtendedTPMKey(BlobFromString("fake extended tpm key"));
        return vk;
      });

  TestFuture<user_data_auth::StartAuthSessionReply>
      start_auth_session_reply_future;
  userdataauth_->StartAuthSession(
      *start_auth_session_req_,
      start_auth_session_reply_future
          .GetCallback<const user_data_auth::StartAuthSessionReply&>());
  RunUntilIdle();
  const user_data_auth::StartAuthSessionReply& start_auth_session_reply =
      start_auth_session_reply_future.Get();

  EXPECT_THAT(start_auth_session_reply.auth_factors().size(), 1);
  EXPECT_THAT(start_auth_session_reply.auth_factors().at(0).label(),
              kFakeLabel);
  EXPECT_THAT(start_auth_session_reply.auth_factors().at(0).type(),
              user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);

  EXPECT_THAT(
      start_auth_session_reply.configured_auth_factors_with_status().size(), 1);
  EXPECT_THAT(start_auth_session_reply.configured_auth_factors_with_status()
                  .at(0)
                  .auth_factor()
                  .label(),
              kFakeLabel);
  EXPECT_THAT(start_auth_session_reply.configured_auth_factors_with_status()
                  .at(0)
                  .auth_factor()
                  .type(),
              user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
  EXPECT_THAT(start_auth_session_reply.configured_auth_factors_with_status()
                  .at(0)
                  .available_for_intents(),
              UnorderedElementsAre(user_data_auth::AUTH_INTENT_VERIFY_ONLY,
                                   user_data_auth::AUTH_INTENT_DECRYPT,
                                   user_data_auth::AUTH_INTENT_WEBAUTHN));
}

TEST_F(UserDataAuthExTest, StartAuthSessionVerifyOnlyFactors) {
  PrepareArguments();
  SetupMount("foo@example.com");
  // Setup
  start_auth_session_req_->mutable_account_id()->set_account_id(
      "foo@example.com");
  start_auth_session_req_->set_intent(user_data_auth::AUTH_INTENT_VERIFY_ONLY);

  KeyData key_data;
  key_data.set_label(kFakeLabel);
  key_data.set_type(KeyData::KEY_TYPE_PASSWORD);

  // Add persistent auth factors.
  EXPECT_CALL(system_apis_.platform, DirectoryExists(_))
      .WillRepeatedly(Return(true));
  std::vector<int> vk_indicies = {0};
  EXPECT_CALL(system_apis_.keyset_management, GetVaultKeysets(_, _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(vk_indicies), Return(true)));
  EXPECT_CALL(system_apis_.keyset_management, LoadVaultKeysetForUser(_, 0))
      .WillRepeatedly([key_data](const ObfuscatedUsername&, int) {
        auto vk = std::make_unique<VaultKeyset>();
        vk->SetFlags(SerializedVaultKeyset::TPM_WRAPPED |
                     SerializedVaultKeyset::PCR_BOUND);
        vk->SetKeyData(key_data);
        vk->SetTPMKey(BlobFromString("fake tpm key"));
        vk->SetExtendedTPMKey(BlobFromString("fake extended tpm key"));
        return vk;
      });
  // Add a verifier as well.
  session_->AddCredentialVerifier(std::make_unique<MockCredentialVerifier>(
      AuthFactorType::kPassword, kFakeLabel,
      AuthFactorMetadata{.metadata = PasswordMetadata()}));

  TestFuture<user_data_auth::StartAuthSessionReply>
      start_auth_session_reply_future;
  userdataauth_->StartAuthSession(
      *start_auth_session_req_,
      start_auth_session_reply_future
          .GetCallback<const user_data_auth::StartAuthSessionReply&>());
  RunUntilIdle();
  const user_data_auth::StartAuthSessionReply& start_auth_session_reply =
      start_auth_session_reply_future.Get();

  EXPECT_EQ(start_auth_session_reply.error(),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  ASSERT_THAT(start_auth_session_reply.auth_factors().size(), 1);
  // We should only find one factor, not two. There's a persistent factor and a
  // verifier but they have the same label.
  EXPECT_THAT(start_auth_session_reply.auth_factors().at(0).label(),
              kFakeLabel);
  EXPECT_THAT(start_auth_session_reply.auth_factors().at(0).type(),
              user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
  EXPECT_THAT(
      start_auth_session_reply.configured_auth_factors_with_status().size(), 1);
  EXPECT_THAT(start_auth_session_reply.configured_auth_factors_with_status()
                  .at(0)
                  .auth_factor()
                  .label(),
              kFakeLabel);
  EXPECT_THAT(start_auth_session_reply.configured_auth_factors_with_status()
                  .at(0)
                  .auth_factor()
                  .type(),
              user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
  EXPECT_THAT(start_auth_session_reply.configured_auth_factors_with_status()
                  .at(0)
                  .available_for_intents(),
              UnorderedElementsAre(user_data_auth::AUTH_INTENT_VERIFY_ONLY,
                                   user_data_auth::AUTH_INTENT_DECRYPT,
                                   user_data_auth::AUTH_INTENT_WEBAUTHN));
}

TEST_F(UserDataAuthExTest, StartAuthSessionEphemeralFactors) {
  PrepareArguments();
  SetupMount("foo@example.com");
  // Setup
  start_auth_session_req_->mutable_account_id()->set_account_id(
      "foo@example.com");
  start_auth_session_req_->set_intent(user_data_auth::AUTH_INTENT_VERIFY_ONLY);
  start_auth_session_req_->set_is_ephemeral_user(true);

  EXPECT_CALL(system_apis_.platform, DirectoryExists(_))
      .WillRepeatedly(Return(true));
  session_->AddCredentialVerifier(std::make_unique<MockCredentialVerifier>(
      AuthFactorType::kPassword, "password-verifier-label",
      AuthFactorMetadata{.metadata = PasswordMetadata()}));

  TestFuture<user_data_auth::StartAuthSessionReply>
      start_auth_session_reply_future;
  userdataauth_->StartAuthSession(
      *start_auth_session_req_,
      start_auth_session_reply_future
          .GetCallback<const user_data_auth::StartAuthSessionReply&>());
  RunUntilIdle();
  const user_data_auth::StartAuthSessionReply& start_auth_session_reply =
      start_auth_session_reply_future.Get();

  EXPECT_EQ(start_auth_session_reply.error(),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  ASSERT_THAT(start_auth_session_reply.auth_factors().size(), 1);
  EXPECT_THAT(start_auth_session_reply.auth_factors().at(0).label(),
              "password-verifier-label");
  EXPECT_THAT(start_auth_session_reply.auth_factors().at(0).type(),
              user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);

  EXPECT_THAT(
      start_auth_session_reply.configured_auth_factors_with_status().size(), 1);
  EXPECT_THAT(start_auth_session_reply.configured_auth_factors_with_status()
                  .at(0)
                  .auth_factor()
                  .label(),
              "password-verifier-label");
  EXPECT_THAT(start_auth_session_reply.configured_auth_factors_with_status()
                  .at(0)
                  .auth_factor()
                  .type(),
              user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
  EXPECT_THAT(start_auth_session_reply.configured_auth_factors_with_status()
                  .at(0)
                  .available_for_intents(),
              UnorderedElementsAre(user_data_auth::AUTH_INTENT_VERIFY_ONLY));
}

TEST_F(UserDataAuthExTest, ListAuthFactorsUserDoesNotExist) {
  EXPECT_CALL(system_apis_.platform, DirectoryExists(_))
      .WillOnce(Return(false));

  user_data_auth::ListAuthFactorsRequest list_request;
  list_request.mutable_account_id()->set_account_id("foo@example.com");
  TestFuture<user_data_auth::ListAuthFactorsReply> list_reply_future;
  userdataauth_->ListAuthFactors(
      list_request,
      list_reply_future
          .GetCallback<const user_data_auth::ListAuthFactorsReply&>());

  EXPECT_EQ(list_reply_future.Get().error(),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
}

TEST_F(UserDataAuthExTest, ListAuthFactorsUserIsPersistentButHasNoStorage) {
  SetupMount("foo@example.com");
  EXPECT_CALL(*session_, IsEphemeral()).WillRepeatedly(Return(false));

  user_data_auth::ListAuthFactorsRequest list_request;
  list_request.mutable_account_id()->set_account_id("foo@example.com");
  TestFuture<user_data_auth::ListAuthFactorsReply> list_reply_future;
  userdataauth_->ListAuthFactors(
      list_request,
      list_reply_future
          .GetCallback<const user_data_auth::ListAuthFactorsReply&>());
  const user_data_auth::ListAuthFactorsReply& list_reply =
      list_reply_future.Get();

  EXPECT_EQ(list_reply.error(), user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  EXPECT_THAT(list_reply.configured_auth_factors_with_status(), IsEmpty());
  EXPECT_THAT(
      list_reply.supported_auth_factors(),
      UnorderedElementsAre(user_data_auth::AUTH_FACTOR_TYPE_PASSWORD,
                           user_data_auth::AUTH_FACTOR_TYPE_CRYPTOHOME_RECOVERY,
                           user_data_auth::AUTH_FACTOR_TYPE_KIOSK,
                           user_data_auth::AUTH_FACTOR_TYPE_SMART_CARD));

  std::vector<user_data_auth::AuthFactorType> types_with_intents;
  for (const auto& intents_for_type : list_reply.auth_intents_for_types()) {
    types_with_intents.push_back(intents_for_type.type());
    EXPECT_THAT(intents_for_type.current(),
                UnorderedElementsAre(user_data_auth::AUTH_INTENT_DECRYPT,
                                     user_data_auth::AUTH_INTENT_VERIFY_ONLY,
                                     user_data_auth::AUTH_INTENT_WEBAUTHN));
  }
  EXPECT_THAT(
      types_with_intents,
      UnorderedElementsAre(user_data_auth::AUTH_FACTOR_TYPE_PASSWORD,
                           user_data_auth::AUTH_FACTOR_TYPE_CRYPTOHOME_RECOVERY,
                           user_data_auth::AUTH_FACTOR_TYPE_KIOSK,
                           user_data_auth::AUTH_FACTOR_TYPE_SMART_CARD));
}

TEST_F(UserDataAuthExTest, ListAuthFactorsUserIsEphemeralWithoutVerifier) {
  EXPECT_CALL(system_apis_.platform, DirectoryExists(_))
      .WillOnce(Return(false));
  // Add a mount (and user session) for the ephemeral user.
  SetupMount("foo@example.com");
  EXPECT_CALL(*session_, IsEphemeral()).WillRepeatedly(Return(true));

  user_data_auth::ListAuthFactorsRequest list_request;
  list_request.mutable_account_id()->set_account_id("foo@example.com");
  TestFuture<user_data_auth::ListAuthFactorsReply> list_reply_future;
  userdataauth_->ListAuthFactors(
      list_request,
      list_reply_future
          .GetCallback<const user_data_auth::ListAuthFactorsReply&>());
  const user_data_auth::ListAuthFactorsReply& list_reply =
      list_reply_future.Get();

  EXPECT_EQ(list_reply.error(), user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  EXPECT_THAT(list_reply.configured_auth_factors_with_status(), IsEmpty());
  EXPECT_THAT(
      list_reply.supported_auth_factors(),
      UnorderedElementsAre(user_data_auth::AUTH_FACTOR_TYPE_PASSWORD,
                           user_data_auth::AUTH_FACTOR_TYPE_SMART_CARD));

  std::vector<user_data_auth::AuthFactorType> types_with_intents;
  for (const auto& intents_for_type : list_reply.auth_intents_for_types()) {
    types_with_intents.push_back(intents_for_type.type());
    EXPECT_THAT(intents_for_type.current(),
                UnorderedElementsAre(user_data_auth::AUTH_INTENT_VERIFY_ONLY));
  }
  EXPECT_THAT(
      types_with_intents,
      UnorderedElementsAre(user_data_auth::AUTH_FACTOR_TYPE_PASSWORD,
                           user_data_auth::AUTH_FACTOR_TYPE_SMART_CARD));
}

TEST_F(UserDataAuthExTest, ListAuthFactorsUserIsEphemeralWithVerifier) {
  EXPECT_CALL(system_apis_.platform, DirectoryExists(_))
      .WillOnce(Return(false));
  // Add a mount (and user session) for the ephemeral user.
  SetupMount("foo@example.com");
  EXPECT_CALL(*session_, IsEphemeral()).WillRepeatedly(Return(true));
  session_->AddCredentialVerifier(std::make_unique<MockCredentialVerifier>(
      AuthFactorType::kPassword, "password-label",
      AuthFactorMetadata{.metadata = PasswordMetadata()}));

  user_data_auth::ListAuthFactorsRequest list_request;
  list_request.mutable_account_id()->set_account_id("foo@example.com");
  TestFuture<user_data_auth::ListAuthFactorsReply> list_reply_future;
  userdataauth_->ListAuthFactors(
      list_request,
      list_reply_future
          .GetCallback<const user_data_auth::ListAuthFactorsReply&>());
  const user_data_auth::ListAuthFactorsReply& list_reply =
      list_reply_future.Get();

  EXPECT_EQ(list_reply.error(), user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  ASSERT_EQ(list_reply.configured_auth_factors_with_status_size(), 1);
  EXPECT_EQ(
      list_reply.configured_auth_factors_with_status(0).auth_factor().type(),
      user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
  EXPECT_EQ(
      list_reply.configured_auth_factors_with_status(0).auth_factor().label(),
      "password-label");
  EXPECT_TRUE(list_reply.configured_auth_factors_with_status(0)
                  .auth_factor()
                  .has_password_metadata());
  EXPECT_TRUE(list_reply.configured_auth_factors_with_status(0)
                  .auth_factor()
                  .has_common_metadata());
  EXPECT_EQ(list_reply.configured_auth_factors_with_status(0)
                .auth_factor()
                .common_metadata()
                .lockout_policy(),
            user_data_auth::LOCKOUT_POLICY_NONE);
  EXPECT_THAT(
      list_reply.supported_auth_factors(),
      UnorderedElementsAre(user_data_auth::AUTH_FACTOR_TYPE_PASSWORD,
                           user_data_auth::AUTH_FACTOR_TYPE_SMART_CARD));

  std::vector<user_data_auth::AuthFactorType> types_with_intents;
  for (const auto& intents_for_type : list_reply.auth_intents_for_types()) {
    types_with_intents.push_back(intents_for_type.type());
    EXPECT_THAT(intents_for_type.current(),
                UnorderedElementsAre(user_data_auth::AUTH_INTENT_VERIFY_ONLY));
  }
  EXPECT_THAT(
      types_with_intents,
      UnorderedElementsAre(user_data_auth::AUTH_FACTOR_TYPE_PASSWORD,
                           user_data_auth::AUTH_FACTOR_TYPE_SMART_CARD));
}

TEST_F(UserDataAuthExTest, ListAuthFactorsUserExistsWithoutPinweaver) {
  EXPECT_CALL(system_apis_.platform, DirectoryExists(_)).WillOnce(Return(true));

  user_data_auth::ListAuthFactorsRequest list_request;
  list_request.mutable_account_id()->set_account_id("foo@example.com");
  TestFuture<user_data_auth::ListAuthFactorsReply> list_reply_future;
  userdataauth_->ListAuthFactors(
      list_request,
      list_reply_future
          .GetCallback<const user_data_auth::ListAuthFactorsReply&>());
  const user_data_auth::ListAuthFactorsReply& list_reply =
      list_reply_future.Get();

  EXPECT_EQ(list_reply.error(), user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  EXPECT_THAT(list_reply.configured_auth_factors_with_status(), IsEmpty());
  EXPECT_THAT(
      list_reply.supported_auth_factors(),
      UnorderedElementsAre(user_data_auth::AUTH_FACTOR_TYPE_PASSWORD,
                           user_data_auth::AUTH_FACTOR_TYPE_CRYPTOHOME_RECOVERY,
                           user_data_auth::AUTH_FACTOR_TYPE_KIOSK,
                           user_data_auth::AUTH_FACTOR_TYPE_SMART_CARD));
}

TEST_F(UserDataAuthExTest, ListAuthFactorsUserExistsWithPinweaver) {
  EXPECT_CALL(system_apis_.platform, DirectoryExists(_)).WillOnce(Return(true));
  EXPECT_CALL(system_apis_.hwsec, IsPinWeaverEnabled())
      .WillRepeatedly(ReturnValue(true));

  user_data_auth::ListAuthFactorsRequest list_request;
  list_request.mutable_account_id()->set_account_id("foo@example.com");
  TestFuture<user_data_auth::ListAuthFactorsReply> list_reply_future;
  userdataauth_->ListAuthFactors(
      list_request,
      list_reply_future
          .GetCallback<const user_data_auth::ListAuthFactorsReply&>());
  const user_data_auth::ListAuthFactorsReply& list_reply =
      list_reply_future.Get();

  EXPECT_EQ(list_reply.error(), user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  EXPECT_THAT(list_reply.configured_auth_factors_with_status(), IsEmpty());
  EXPECT_THAT(
      list_reply.supported_auth_factors(),
      UnorderedElementsAre(user_data_auth::AUTH_FACTOR_TYPE_PASSWORD,
                           user_data_auth::AUTH_FACTOR_TYPE_PIN,
                           user_data_auth::AUTH_FACTOR_TYPE_CRYPTOHOME_RECOVERY,
                           user_data_auth::AUTH_FACTOR_TYPE_KIOSK,
                           user_data_auth::AUTH_FACTOR_TYPE_SMART_CARD));
}

TEST_F(UserDataAuthExTest,
       ListAuthFactorsUserExistsWithNoFactorsButUssEnabled) {
  EXPECT_CALL(system_apis_.platform, DirectoryExists(_)).WillOnce(Return(true));
  EXPECT_CALL(system_apis_.hwsec, IsPinWeaverEnabled())
      .WillRepeatedly(ReturnValue(true));

  user_data_auth::ListAuthFactorsRequest list_request;
  list_request.mutable_account_id()->set_account_id("foo@example.com");
  TestFuture<user_data_auth::ListAuthFactorsReply> list_reply_future;
  userdataauth_->ListAuthFactors(
      list_request,
      list_reply_future
          .GetCallback<const user_data_auth::ListAuthFactorsReply&>());
  const user_data_auth::ListAuthFactorsReply& list_reply =
      list_reply_future.Get();

  EXPECT_EQ(list_reply.error(), user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  EXPECT_THAT(list_reply.configured_auth_factors_with_status(), IsEmpty());
  EXPECT_THAT(
      list_reply.supported_auth_factors(),
      UnorderedElementsAre(user_data_auth::AUTH_FACTOR_TYPE_PASSWORD,
                           user_data_auth::AUTH_FACTOR_TYPE_PIN,
                           user_data_auth::AUTH_FACTOR_TYPE_CRYPTOHOME_RECOVERY,
                           user_data_auth::AUTH_FACTOR_TYPE_KIOSK,
                           user_data_auth::AUTH_FACTOR_TYPE_SMART_CARD));
}

TEST_F(UserDataAuthExTest, ListAuthFactorsUserExistsWithFactorsFromVks) {
  const Username kUser("foo@example.com");
  const ObfuscatedUsername kObfuscatedUser = SanitizeUserName(kUser);
  EXPECT_CALL(system_apis_.platform, DirectoryExists(_)).WillOnce(Return(true));

  // Set up mocks for a few of VKs. We deliberately have the second not work to
  // test that the listing correctly skips it.
  std::vector<int> vk_indicies = {0, 1, 2};
  EXPECT_CALL(system_apis_.keyset_management,
              GetVaultKeysets(kObfuscatedUser, _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(vk_indicies), Return(true)));
  EXPECT_CALL(system_apis_.keyset_management,
              LoadVaultKeysetForUser(kObfuscatedUser, 0))
      .WillRepeatedly([](const ObfuscatedUsername&, int) {
        auto vk = std::make_unique<VaultKeyset>();
        vk->SetFlags(SerializedVaultKeyset::TPM_WRAPPED |
                     SerializedVaultKeyset::PCR_BOUND);
        KeyData key_data;
        key_data.set_type(KeyData::KEY_TYPE_PASSWORD);
        key_data.set_label("password-label");
        vk->SetKeyData(key_data);
        vk->SetTPMKey(BlobFromString("fake tpm key"));
        vk->SetExtendedTPMKey(BlobFromString("fake extended tpm key"));
        return vk;
      });
  EXPECT_CALL(system_apis_.keyset_management,
              LoadVaultKeysetForUser(kObfuscatedUser, 1))
      .WillRepeatedly([](const ObfuscatedUsername&, int) { return nullptr; });
  EXPECT_CALL(system_apis_.keyset_management,
              LoadVaultKeysetForUser(kObfuscatedUser, 2))
      .WillRepeatedly([](const ObfuscatedUsername&, int) {
        auto vk = std::make_unique<VaultKeyset>();
        vk->SetFlags(SerializedVaultKeyset::SCRYPT_WRAPPED);
        KeyData key_data;
        key_data.set_type(KeyData::KEY_TYPE_PASSWORD);
        key_data.set_label("password-scrypt-label");
        vk->SetKeyData(key_data);
        const brillo::Blob kScryptPlaintext =
            brillo::BlobFromString("plaintext");
        const auto blob_to_encrypt = brillo::SecureBlob(brillo::CombineBlobs(
            {kScryptPlaintext, hwsec_foundation::Sha1(kScryptPlaintext)}));
        brillo::Blob wrapped_keyset;
        brillo::Blob wrapped_chaps_key;
        brillo::Blob wrapped_reset_seed;
        brillo::SecureBlob derived_key = {
            0x67, 0xeb, 0xcd, 0x84, 0x49, 0x5e, 0xa2, 0xf3, 0xb1, 0xe6, 0xe7,
            0x5b, 0x13, 0xb9, 0x16, 0x2f, 0x5a, 0x39, 0xc8, 0xfe, 0x6a, 0x60,
            0xd4, 0x7a, 0xd8, 0x2b, 0x44, 0xc4, 0x45, 0x53, 0x1a, 0x85, 0x4a,
            0x97, 0x9f, 0x2d, 0x06, 0xf5, 0xd0, 0xd3, 0xa6, 0xe7, 0xac, 0x9b,
            0x02, 0xaf, 0x3c, 0x08, 0xce, 0x43, 0x46, 0x32, 0x6d, 0xd7, 0x2b,
            0xe9, 0xdf, 0x8b, 0x38, 0x0e, 0x60, 0x3d, 0x64, 0x12};
        brillo::Blob scrypt_salt = brillo::BlobFromString("salt");
        brillo::Blob chaps_salt = brillo::BlobFromString("chaps_salt");
        brillo::Blob reset_seed_salt =
            brillo::BlobFromString("reset_seed_salt");
        scrypt_salt.resize(hwsec_foundation::kLibScryptSaltSize);
        chaps_salt.resize(hwsec_foundation::kLibScryptSaltSize);
        reset_seed_salt.resize(hwsec_foundation::kLibScryptSaltSize);
        if (hwsec_foundation::LibScryptCompat::Encrypt(
                derived_key, scrypt_salt, blob_to_encrypt,
                hwsec_foundation::kDefaultScryptParams, &wrapped_keyset)) {
          vk->SetWrappedKeyset(wrapped_keyset);
        }
        if (hwsec_foundation::LibScryptCompat::Encrypt(
                derived_key, chaps_salt, blob_to_encrypt,
                hwsec_foundation::kDefaultScryptParams, &wrapped_chaps_key)) {
          vk->SetWrappedChapsKey(wrapped_chaps_key);
        }
        if (hwsec_foundation::LibScryptCompat::Encrypt(
                derived_key, reset_seed_salt, blob_to_encrypt,
                hwsec_foundation::kDefaultScryptParams, &wrapped_reset_seed)) {
          vk->SetWrappedResetSeed(wrapped_reset_seed);
        }
        return vk;
      });

  user_data_auth::ListAuthFactorsRequest list_request;
  list_request.mutable_account_id()->set_account_id(*kUser);
  TestFuture<user_data_auth::ListAuthFactorsReply> list_reply_future;
  userdataauth_->ListAuthFactors(
      list_request,
      list_reply_future
          .GetCallback<const user_data_auth::ListAuthFactorsReply&>());
  const user_data_auth::ListAuthFactorsReply& list_reply =
      list_reply_future.Get();

  EXPECT_EQ(list_reply.error(), user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  ASSERT_EQ(list_reply.configured_auth_factors_with_status_size(), 2);
  EXPECT_EQ(
      list_reply.configured_auth_factors_with_status(0).auth_factor().label(),
      "password-label");
  EXPECT_TRUE(list_reply.configured_auth_factors_with_status(0)
                  .auth_factor()
                  .has_password_metadata());
  EXPECT_TRUE(list_reply.configured_auth_factors_with_status(0)
                  .auth_factor()
                  .has_common_metadata());
  EXPECT_EQ(list_reply.configured_auth_factors_with_status(0)
                .auth_factor()
                .common_metadata()
                .lockout_policy(),
            user_data_auth::LOCKOUT_POLICY_NONE);
  EXPECT_EQ(
      list_reply.configured_auth_factors_with_status(1).auth_factor().label(),
      "password-scrypt-label");
  EXPECT_TRUE(list_reply.configured_auth_factors_with_status(1)
                  .auth_factor()
                  .has_password_metadata());
  EXPECT_TRUE(list_reply.configured_auth_factors_with_status(1)
                  .auth_factor()
                  .has_common_metadata());
  EXPECT_EQ(list_reply.configured_auth_factors_with_status(1)
                .auth_factor()
                .common_metadata()
                .lockout_policy(),
            user_data_auth::LOCKOUT_POLICY_NONE);
  EXPECT_THAT(
      list_reply.supported_auth_factors(),
      UnorderedElementsAre(user_data_auth::AUTH_FACTOR_TYPE_PASSWORD,
                           user_data_auth::AUTH_FACTOR_TYPE_CRYPTOHOME_RECOVERY,
                           user_data_auth::AUTH_FACTOR_TYPE_SMART_CARD));
}

TEST_F(UserDataAuthExTest, ListAuthFactorsWithFactorsFromUss) {
  const Username kUser("foo@example.com");
  const ObfuscatedUsername kObfuscatedUser = SanitizeUserName(kUser);

  EXPECT_CALL(system_apis_.hwsec, IsPinWeaverEnabled())
      .WillRepeatedly(ReturnValue(true));
  EXPECT_CALL(system_apis_.hwsec_pw_manager, GetDelayInSeconds(_))
      .WillRepeatedly(ReturnValue(UINT32_MAX));
  EXPECT_CALL(system_apis_.platform, DirectoryExists(_))
      .WillRepeatedly(Return(true));

  // Set up standard list auth factor parameters, we'll be calling this a few
  // times during the test.
  user_data_auth::ListAuthFactorsRequest list_request;
  list_request.mutable_account_id()->set_account_id(*kUser);
  TestFuture<user_data_auth::ListAuthFactorsReply> list_reply_future_1;

  // List all the auth factors, there should be none at the start.
  userdataauth_->ListAuthFactors(
      list_request,
      list_reply_future_1
          .GetCallback<const user_data_auth::ListAuthFactorsReply&>());
  EXPECT_EQ(list_reply_future_1.Get().error(),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  EXPECT_THAT(list_reply_future_1.Get().configured_auth_factors_with_status(),
              IsEmpty());
  EXPECT_THAT(list_reply_future_1.Get().supported_auth_factors(),
              UnorderedElementsAre(
                  user_data_auth::AUTH_FACTOR_TYPE_PASSWORD,
                  user_data_auth::AUTH_FACTOR_TYPE_PIN,
                  user_data_auth::AUTH_FACTOR_TYPE_KIOSK,
                  user_data_auth::AUTH_FACTOR_TYPE_SMART_CARD,
                  user_data_auth::AUTH_FACTOR_TYPE_CRYPTOHOME_RECOVERY));
  system_apis_.auth_factor_manager.DiscardAuthFactorMap(kObfuscatedUser);

  // Add uss auth factors, we should be able to list them.
  auto password_factor = std::make_unique<AuthFactor>(
      AuthFactorType::kPassword, "password-label",
      AuthFactorMetadata{.metadata = PasswordMetadata()},
      AuthBlockState{
          .state = TpmBoundToPcrAuthBlockState{
              .scrypt_derived = false,
              .salt = BlobFromString("fake salt"),
              .tpm_key = BlobFromString("fake tpm key"),
              .extended_tpm_key = BlobFromString("fake extended tpm key"),
              .tpm_public_key_hash = BlobFromString("fake tpm public key hash"),
          }});
  ASSERT_THAT(system_apis_.auth_factor_manager.SaveAuthFactorFile(
                  kObfuscatedUser, *password_factor),
              IsOk());
  auto pin_factor = std::make_unique<AuthFactor>(
      AuthFactorType::kPin, "pin-label",
      AuthFactorMetadata{.metadata = PinMetadata()},
      AuthBlockState{.state = PinWeaverAuthBlockState{
                         .le_label = 0xbaadf00d,
                         .salt = BlobFromString("fake salt"),
                         .chaps_iv = BlobFromString("fake chaps IV"),
                         .fek_iv = BlobFromString("fake file encryption IV"),
                         .reset_salt = BlobFromString("more fake salt"),
                     }});
  ASSERT_THAT(system_apis_.auth_factor_manager.SaveAuthFactorFile(
                  kObfuscatedUser, *pin_factor),
              IsOk());
  MakeUssWithLabels(kObfuscatedUser, {"password-label", "pin-label"});

  TestFuture<user_data_auth::ListAuthFactorsReply> list_reply_future_2;
  userdataauth_->ListAuthFactors(
      list_request,
      list_reply_future_2
          .GetCallback<const user_data_auth::ListAuthFactorsReply&>());
  user_data_auth::ListAuthFactorsReply list_reply_2 =
      list_reply_future_2.Take();
  EXPECT_EQ(list_reply_2.error(), user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  std::sort(
      list_reply_2.mutable_configured_auth_factors_with_status()
          ->pointer_begin(),
      list_reply_2.mutable_configured_auth_factors_with_status()->pointer_end(),
      [](const user_data_auth::AuthFactorWithStatus* lhs,
         const user_data_auth::AuthFactorWithStatus* rhs) {
        return lhs->auth_factor().label() < rhs->auth_factor().label();
      });
  ASSERT_EQ(list_reply_2.configured_auth_factors_with_status_size(), 2);
  EXPECT_EQ(
      list_reply_2.configured_auth_factors_with_status(0).auth_factor().label(),
      "password-label");
  EXPECT_TRUE(list_reply_2.configured_auth_factors_with_status(0)
                  .auth_factor()
                  .has_password_metadata());
  EXPECT_TRUE(list_reply_2.configured_auth_factors_with_status(0)
                  .auth_factor()
                  .has_common_metadata());
  EXPECT_EQ(list_reply_2.configured_auth_factors_with_status(0)
                .auth_factor()
                .common_metadata()
                .lockout_policy(),
            user_data_auth::LOCKOUT_POLICY_NONE);
  EXPECT_EQ(
      list_reply_2.configured_auth_factors_with_status(1).auth_factor().label(),
      "pin-label");
  EXPECT_TRUE(list_reply_2.configured_auth_factors_with_status(1)
                  .auth_factor()
                  .has_pin_metadata());
  EXPECT_TRUE(list_reply_2.configured_auth_factors_with_status(1)
                  .auth_factor()
                  .has_common_metadata());
  EXPECT_EQ(list_reply_2.configured_auth_factors_with_status(1)
                .auth_factor()
                .common_metadata()
                .lockout_policy(),
            user_data_auth::LOCKOUT_POLICY_ATTEMPT_LIMITED);
  EXPECT_THAT(
      list_reply_2.supported_auth_factors(),
      UnorderedElementsAre(user_data_auth::AUTH_FACTOR_TYPE_PASSWORD,
                           user_data_auth::AUTH_FACTOR_TYPE_PIN,
                           user_data_auth::AUTH_FACTOR_TYPE_CRYPTOHOME_RECOVERY,
                           user_data_auth::AUTH_FACTOR_TYPE_SMART_CARD));
  system_apis_.auth_factor_manager.DiscardAuthFactorMap(kObfuscatedUser);

  // Remove an auth factor, we should still be able to list the remaining one.
  TestFuture<CryptohomeStatus> remove_result;
  system_apis_.auth_factor_manager.RemoveAuthFactor(
      kObfuscatedUser, *pin_factor, &auth_block_utility_,
      remove_result.GetCallback());
  EXPECT_THAT(remove_result.Take(), IsOk());
  TestFuture<user_data_auth::ListAuthFactorsReply> list_reply_future_3;
  userdataauth_->ListAuthFactors(
      list_request,
      list_reply_future_3
          .GetCallback<const user_data_auth::ListAuthFactorsReply&>());
  const user_data_auth::ListAuthFactorsReply& list_reply_3 =
      list_reply_future_3.Get();
  EXPECT_EQ(list_reply_3.error(), user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  ASSERT_EQ(list_reply_3.configured_auth_factors_with_status_size(), 1);
  EXPECT_EQ(
      list_reply_3.configured_auth_factors_with_status(0).auth_factor().label(),
      "password-label");
  EXPECT_TRUE(list_reply_3.configured_auth_factors_with_status(0)
                  .auth_factor()
                  .has_password_metadata());
  EXPECT_TRUE(list_reply_3.configured_auth_factors_with_status(0)
                  .auth_factor()
                  .has_common_metadata());
  EXPECT_EQ(list_reply_3.configured_auth_factors_with_status(0)
                .auth_factor()
                .common_metadata()
                .lockout_policy(),
            user_data_auth::LOCKOUT_POLICY_NONE);
  EXPECT_THAT(
      list_reply_3.supported_auth_factors(),
      UnorderedElementsAre(user_data_auth::AUTH_FACTOR_TYPE_PASSWORD,
                           user_data_auth::AUTH_FACTOR_TYPE_PIN,
                           user_data_auth::AUTH_FACTOR_TYPE_CRYPTOHOME_RECOVERY,
                           user_data_auth::AUTH_FACTOR_TYPE_SMART_CARD));
}

TEST_F(UserDataAuthExTest, ListAuthFactorsWithIncompleteFactorsFromUss) {
  const Username kUser("foo@example.com");
  const ObfuscatedUsername kObfuscatedUser = SanitizeUserName(kUser);

  EXPECT_CALL(system_apis_.hwsec, IsPinWeaverEnabled())
      .WillRepeatedly(ReturnValue(true));
  EXPECT_CALL(system_apis_.platform, DirectoryExists(_))
      .WillRepeatedly(Return(true));

  // Set up standard list auth factor parameters, we'll be calling this a few
  // times during the test.
  user_data_auth::ListAuthFactorsRequest list_request;
  list_request.mutable_account_id()->set_account_id(*kUser);
  TestFuture<user_data_auth::ListAuthFactorsReply> list_reply_future_1;

  // List all the auth factors, there should be none at the start.
  userdataauth_->ListAuthFactors(
      list_request,
      list_reply_future_1
          .GetCallback<const user_data_auth::ListAuthFactorsReply&>());
  EXPECT_EQ(list_reply_future_1.Get().error(),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  EXPECT_THAT(list_reply_future_1.Get().configured_auth_factors_with_status(),
              IsEmpty());
  EXPECT_THAT(list_reply_future_1.Get().supported_auth_factors(),
              UnorderedElementsAre(
                  user_data_auth::AUTH_FACTOR_TYPE_PASSWORD,
                  user_data_auth::AUTH_FACTOR_TYPE_PIN,
                  user_data_auth::AUTH_FACTOR_TYPE_KIOSK,
                  user_data_auth::AUTH_FACTOR_TYPE_SMART_CARD,
                  user_data_auth::AUTH_FACTOR_TYPE_CRYPTOHOME_RECOVERY));
  system_apis_.auth_factor_manager.DiscardAuthFactorMap(kObfuscatedUser);

  // Add uss auth factors, but with only one of them having both the auth factor
  // and USS components of the fact. Only the complete one should work.
  auto password_factor = std::make_unique<AuthFactor>(
      AuthFactorType::kPassword, "password-label",
      AuthFactorMetadata{.metadata = PasswordMetadata()},
      AuthBlockState{
          .state = TpmBoundToPcrAuthBlockState{
              .scrypt_derived = false,
              .salt = BlobFromString("fake salt"),
              .tpm_key = BlobFromString("fake tpm key"),
              .extended_tpm_key = BlobFromString("fake extended tpm key"),
              .tpm_public_key_hash = BlobFromString("fake tpm public key hash"),
          }});
  ASSERT_THAT(system_apis_.auth_factor_manager.SaveAuthFactorFile(
                  kObfuscatedUser, *password_factor),
              IsOk());
  auto pin_factor = std::make_unique<AuthFactor>(
      AuthFactorType::kPin, "pin-label",
      AuthFactorMetadata{.metadata = PinMetadata()},
      AuthBlockState{.state = PinWeaverAuthBlockState{
                         .le_label = 0xbaadf00d,
                         .salt = BlobFromString("fake salt"),
                         .chaps_iv = BlobFromString("fake chaps IV"),
                         .fek_iv = BlobFromString("fake file encryption IV"),
                         .reset_salt = BlobFromString("more fake salt"),
                     }});
  ASSERT_THAT(system_apis_.auth_factor_manager.SaveAuthFactorFile(
                  kObfuscatedUser, *pin_factor),
              IsOk());
  MakeUssWithLabels(kObfuscatedUser, {"password-label"});

  TestFuture<user_data_auth::ListAuthFactorsReply> list_reply_future_2;
  userdataauth_->ListAuthFactors(
      list_request,
      list_reply_future_2
          .GetCallback<const user_data_auth::ListAuthFactorsReply&>());
  user_data_auth::ListAuthFactorsReply list_reply_2 =
      list_reply_future_2.Take();
  EXPECT_EQ(list_reply_2.error(), user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  std::sort(
      list_reply_2.mutable_configured_auth_factors_with_status()
          ->pointer_begin(),
      list_reply_2.mutable_configured_auth_factors_with_status()->pointer_end(),
      [](const user_data_auth::AuthFactorWithStatus* lhs,
         const user_data_auth::AuthFactorWithStatus* rhs) {
        return lhs->auth_factor().label() < rhs->auth_factor().label();
      });
  ASSERT_EQ(list_reply_2.configured_auth_factors_with_status_size(), 1);
  EXPECT_EQ(
      list_reply_2.configured_auth_factors_with_status(0).auth_factor().label(),
      "password-label");
  EXPECT_TRUE(list_reply_2.configured_auth_factors_with_status(0)
                  .auth_factor()
                  .has_password_metadata());
  EXPECT_TRUE(list_reply_2.configured_auth_factors_with_status(0)
                  .auth_factor()
                  .has_common_metadata());
  EXPECT_EQ(list_reply_2.configured_auth_factors_with_status(0)
                .auth_factor()
                .common_metadata()
                .lockout_policy(),
            user_data_auth::LOCKOUT_POLICY_NONE);
  EXPECT_THAT(
      list_reply_2.supported_auth_factors(),
      UnorderedElementsAre(user_data_auth::AUTH_FACTOR_TYPE_PASSWORD,
                           user_data_auth::AUTH_FACTOR_TYPE_PIN,
                           user_data_auth::AUTH_FACTOR_TYPE_CRYPTOHOME_RECOVERY,
                           user_data_auth::AUTH_FACTOR_TYPE_SMART_CARD));
}

TEST_F(UserDataAuthExTest, StartAuthSessionPinLockedLegacy) {
  // Setup.
  const Username kUser("foo@example.com");
  const ObfuscatedUsername kObfuscatedUser = SanitizeUserName(kUser);

  EXPECT_CALL(system_apis_.hwsec, IsPinWeaverEnabled())
      .WillRepeatedly(ReturnValue(true));
  EXPECT_CALL(system_apis_.platform, DirectoryExists(_))
      .WillRepeatedly(Return(false));

  // Set up standard start authsession parameters, we'll be calling this a few
  // times during the test.
  user_data_auth::StartAuthSessionRequest start_request;
  start_request.mutable_account_id()->set_account_id(*kUser);
  start_request.set_intent(user_data_auth::AUTH_INTENT_DECRYPT);
  TestFuture<user_data_auth::StartAuthSessionReply> start_reply_future_1;

  // List all the auth factors, there should be none at the start.
  userdataauth_->StartAuthSession(
      start_request,
      start_reply_future_1
          .GetCallback<const user_data_auth::StartAuthSessionReply&>());
  RunUntilIdle();
  EXPECT_EQ(start_reply_future_1.Get().error(),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  EXPECT_THAT(start_reply_future_1.Get().configured_auth_factors_with_status(),
              IsEmpty());
  EXPECT_THAT(start_reply_future_1.Get().user_exists(), false);
  system_apis_.auth_factor_manager.DiscardAuthFactorMap(kObfuscatedUser);

  // Now that we are starting to save AuthFactors, let's assume user exists.
  EXPECT_CALL(system_apis_.platform, DirectoryExists(_))
      .WillRepeatedly(Return(true));
  // Add uss auth factors, we should be able to list them.
  auto password_factor = std::make_unique<AuthFactor>(
      AuthFactorType::kPassword, "password-label",
      AuthFactorMetadata{.metadata = PasswordMetadata()},
      AuthBlockState{
          .state = TpmBoundToPcrAuthBlockState{
              .scrypt_derived = false,
              .salt = BlobFromString("fake salt"),
              .tpm_key = BlobFromString("fake tpm key"),
              .extended_tpm_key = BlobFromString("fake extended tpm key"),
              .tpm_public_key_hash = BlobFromString("fake tpm public key hash"),
          }});
  ASSERT_THAT(system_apis_.auth_factor_manager.SaveAuthFactorFile(
                  kObfuscatedUser, *password_factor),
              IsOk());
  auto pin_factor = std::make_unique<AuthFactor>(
      AuthFactorType::kPin, "pin-label",
      AuthFactorMetadata{.metadata = PinMetadata()},
      AuthBlockState{.state = PinWeaverAuthBlockState{
                         .le_label = 0xbaadf00d,
                         .salt = BlobFromString("fake salt"),
                         .chaps_iv = BlobFromString("fake chaps IV"),
                         .fek_iv = BlobFromString("fake file encryption IV"),
                         .reset_salt = BlobFromString("more fake salt"),
                     }});
  ASSERT_THAT(system_apis_.auth_factor_manager.SaveAuthFactorFile(
                  kObfuscatedUser, *pin_factor),
              IsOk());
  MakeUssWithLabels(kObfuscatedUser, {"password-label", "pin-label"});

  EXPECT_CALL(system_apis_.hwsec_pw_manager, GetDelayInSeconds)
      .WillRepeatedly(ReturnValue(UINT32_MAX));

  TestFuture<user_data_auth::StartAuthSessionReply> start_reply_future_2;
  userdataauth_->StartAuthSession(
      start_request,
      start_reply_future_2
          .GetCallback<const user_data_auth::StartAuthSessionReply&>());
  RunUntilIdle();
  user_data_auth::StartAuthSessionReply start_reply =
      start_reply_future_2.Take();
  EXPECT_EQ(start_reply.error(), user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  std::sort(
      start_reply.mutable_configured_auth_factors_with_status()
          ->pointer_begin(),
      start_reply.mutable_configured_auth_factors_with_status()->pointer_end(),
      [](const user_data_auth::AuthFactorWithStatus* lhs,
         const user_data_auth::AuthFactorWithStatus* rhs) {
        return lhs->auth_factor().label() < rhs->auth_factor().label();
      });
  ASSERT_EQ(start_reply.configured_auth_factors_with_status_size(), 2);
  EXPECT_EQ(
      start_reply.configured_auth_factors_with_status(0).auth_factor().label(),
      "password-label");
  EXPECT_TRUE(start_reply.configured_auth_factors_with_status(0)
                  .auth_factor()
                  .has_password_metadata());
  EXPECT_TRUE(start_reply.configured_auth_factors_with_status(0)
                  .auth_factor()
                  .has_common_metadata());
  EXPECT_EQ(start_reply.configured_auth_factors_with_status(0)
                .auth_factor()
                .common_metadata()
                .lockout_policy(),
            user_data_auth::LOCKOUT_POLICY_NONE);
  EXPECT_EQ(
      start_reply.configured_auth_factors_with_status(1).auth_factor().label(),
      "pin-label");
  EXPECT_TRUE(start_reply.configured_auth_factors_with_status(1)
                  .auth_factor()
                  .has_pin_metadata());
  EXPECT_TRUE(start_reply.configured_auth_factors_with_status(1)
                  .auth_factor()
                  .has_common_metadata());
  EXPECT_EQ(start_reply.configured_auth_factors_with_status(1)
                .auth_factor()
                .common_metadata()
                .lockout_policy(),
            user_data_auth::LOCKOUT_POLICY_ATTEMPT_LIMITED);
  EXPECT_TRUE(
      start_reply.configured_auth_factors_with_status(1).has_status_info());
  EXPECT_EQ(start_reply.configured_auth_factors_with_status(1)
                .status_info()
                .time_available_in(),
            std::numeric_limits<uint64_t>::max());
  EXPECT_EQ(start_reply.configured_auth_factors_with_status(1)
                .status_info()
                .time_expiring_in(),
            std::numeric_limits<uint64_t>::max());
  EXPECT_TRUE(start_reply.user_exists());
}

TEST_F(UserDataAuthExTest, StartAuthSessionPinLockedModern) {
  // Setup.
  const Username kUser("foo@example.com");
  const ObfuscatedUsername kObfuscatedUser = SanitizeUserName(kUser);

  EXPECT_CALL(system_apis_.hwsec, IsPinWeaverEnabled())
      .WillRepeatedly(ReturnValue(true));
  EXPECT_CALL(system_apis_.platform, DirectoryExists(_))
      .WillRepeatedly(Return(false));

  // Set up standard start authsession parameters, we'll be calling this a few
  // times during the test.
  user_data_auth::StartAuthSessionRequest start_request;
  start_request.mutable_account_id()->set_account_id(*kUser);
  start_request.set_intent(user_data_auth::AUTH_INTENT_DECRYPT);
  TestFuture<user_data_auth::StartAuthSessionReply> start_reply_future_1;

  // List all the auth factors, there should be none at the start.
  userdataauth_->StartAuthSession(
      start_request,
      start_reply_future_1
          .GetCallback<const user_data_auth::StartAuthSessionReply&>());
  RunUntilIdle();
  EXPECT_EQ(start_reply_future_1.Get().error(),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  EXPECT_THAT(start_reply_future_1.Get().configured_auth_factors_with_status(),
              IsEmpty());
  EXPECT_THAT(start_reply_future_1.Get().user_exists(), false);
  system_apis_.auth_factor_manager.DiscardAuthFactorMap(kObfuscatedUser);

  // Now that we are starting to save AuthFactors, let's assume user exists.
  EXPECT_CALL(system_apis_.platform, DirectoryExists(_))
      .WillRepeatedly(Return(true));
  // Add uss auth factors, we should be able to list them.
  auto password_factor = std::make_unique<AuthFactor>(
      AuthFactorType::kPassword, "password-label",
      AuthFactorMetadata{.metadata = PasswordMetadata()},
      AuthBlockState{
          .state = TpmBoundToPcrAuthBlockState{
              .scrypt_derived = false,
              .salt = BlobFromString("fake salt"),
              .tpm_key = BlobFromString("fake tpm key"),
              .extended_tpm_key = BlobFromString("fake extended tpm key"),
              .tpm_public_key_hash = BlobFromString("fake tpm public key hash"),
          }});
  ASSERT_THAT(system_apis_.auth_factor_manager.SaveAuthFactorFile(
                  kObfuscatedUser, *password_factor),
              IsOk());
  auto pin_factor = std::make_unique<AuthFactor>(
      AuthFactorType::kPin, "pin-label",
      AuthFactorMetadata{
          .common = CommonMetadata{.lockout_policy =
                                       SerializedLockoutPolicy::TIME_LIMITED},
          .metadata = PinMetadata()},
      AuthBlockState{.state = PinWeaverAuthBlockState{
                         .le_label = 0xbaadf00d,
                         .salt = BlobFromString("fake salt"),
                         .chaps_iv = BlobFromString("fake chaps IV"),
                         .fek_iv = BlobFromString("fake file encryption IV"),
                         .reset_salt = BlobFromString("more fake salt"),
                     }});
  ASSERT_THAT(system_apis_.auth_factor_manager.SaveAuthFactorFile(
                  kObfuscatedUser, *pin_factor),
              IsOk());
  MakeUssWithLabels(kObfuscatedUser, {"password-label", "pin-label"});

  EXPECT_CALL(system_apis_.hwsec_pw_manager, GetDelayInSeconds)
      .WillRepeatedly(ReturnValue(30));

  TestFuture<user_data_auth::StartAuthSessionReply> start_reply_future_2;
  userdataauth_->StartAuthSession(
      start_request,
      start_reply_future_2
          .GetCallback<const user_data_auth::StartAuthSessionReply&>());
  RunUntilIdle();
  user_data_auth::StartAuthSessionReply start_reply =
      start_reply_future_2.Take();
  EXPECT_EQ(start_reply.error(), user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  std::sort(
      start_reply.mutable_configured_auth_factors_with_status()
          ->pointer_begin(),
      start_reply.mutable_configured_auth_factors_with_status()->pointer_end(),
      [](const user_data_auth::AuthFactorWithStatus* lhs,
         const user_data_auth::AuthFactorWithStatus* rhs) {
        return lhs->auth_factor().label() < rhs->auth_factor().label();
      });
  ASSERT_EQ(start_reply.configured_auth_factors_with_status_size(), 2);
  EXPECT_EQ(
      start_reply.configured_auth_factors_with_status(0).auth_factor().label(),
      "password-label");
  EXPECT_TRUE(start_reply.configured_auth_factors_with_status(0)
                  .auth_factor()
                  .has_password_metadata());
  EXPECT_TRUE(start_reply.configured_auth_factors_with_status(0)
                  .auth_factor()
                  .has_common_metadata());
  EXPECT_EQ(start_reply.configured_auth_factors_with_status(0)
                .auth_factor()
                .common_metadata()
                .lockout_policy(),
            user_data_auth::LOCKOUT_POLICY_NONE);
  EXPECT_EQ(
      start_reply.configured_auth_factors_with_status(1).auth_factor().label(),
      "pin-label");
  EXPECT_TRUE(start_reply.configured_auth_factors_with_status(1)
                  .auth_factor()
                  .has_pin_metadata());
  EXPECT_TRUE(start_reply.configured_auth_factors_with_status(1)
                  .auth_factor()
                  .has_common_metadata());
  EXPECT_EQ(start_reply.configured_auth_factors_with_status(1)
                .auth_factor()
                .common_metadata()
                .lockout_policy(),
            user_data_auth::LOCKOUT_POLICY_TIME_LIMITED);
  EXPECT_TRUE(
      start_reply.configured_auth_factors_with_status(1).has_status_info());
  EXPECT_EQ(start_reply.configured_auth_factors_with_status(1)
                .status_info()
                .time_available_in(),
            30000);
  EXPECT_EQ(start_reply.configured_auth_factors_with_status(1)
                .status_info()
                .time_expiring_in(),
            std::numeric_limits<uint64_t>::max());
}

TEST_F(UserDataAuthExTest, StartAuthSessionFingerprintLocked) {
  // Setup.
  const Username kUser("foo@example.com");
  const ObfuscatedUsername kObfuscatedUser = SanitizeUserName(kUser);

  ON_CALL(system_apis_.hwsec, IsBiometricsPinWeaverEnabled())
      .WillByDefault(ReturnValue(true));
  EXPECT_CALL(system_apis_.platform, DirectoryExists(_))
      .WillRepeatedly(Return(false));

  // Set up standard start authsession parameters, we'll be calling this a few
  // times during the test.
  user_data_auth::StartAuthSessionRequest start_request;
  start_request.mutable_account_id()->set_account_id(*kUser);
  start_request.set_intent(user_data_auth::AUTH_INTENT_DECRYPT);
  TestFuture<user_data_auth::StartAuthSessionReply> start_reply_future_1;

  // List all the auth factors, there should be none at the start.
  userdataauth_->StartAuthSession(
      start_request,
      start_reply_future_1
          .GetCallback<const user_data_auth::StartAuthSessionReply&>());
  RunUntilIdle();
  EXPECT_EQ(start_reply_future_1.Get().error(),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  EXPECT_THAT(start_reply_future_1.Get().configured_auth_factors_with_status(),
              IsEmpty());
  EXPECT_THAT(start_reply_future_1.Get().user_exists(), false);
  system_apis_.auth_factor_manager.DiscardAuthFactorMap(kObfuscatedUser);

  // Now that we are starting to save AuthFactors, let's assume user exists.
  EXPECT_CALL(system_apis_.platform, DirectoryExists(_))
      .WillRepeatedly(Return(true));
  // Add uss auth factors, we should be able to list them.
  auto fp_factor = std::make_unique<AuthFactor>(
      AuthFactorType::kFingerprint, "fp-label",
      AuthFactorMetadata{.metadata = FingerprintMetadata()},
      AuthBlockState{.state = FingerprintAuthBlockState{}});
  ASSERT_THAT(system_apis_.auth_factor_manager.SaveAuthFactorFile(
                  kObfuscatedUser, *fp_factor),
              IsOk());
  MakeUssWithLabelsAndRateLimiter(kObfuscatedUser, {"fp-label"}, true);

  EXPECT_CALL(system_apis_.hwsec_pw_manager, GetDelayInSeconds)
      .WillRepeatedly(ReturnValue(30));
  EXPECT_CALL(system_apis_.hwsec_pw_manager, GetExpirationInSeconds)
      .WillRepeatedly(ReturnValue(20));

  TestFuture<user_data_auth::StartAuthSessionReply> start_reply_future_2;
  userdataauth_->StartAuthSession(
      start_request,
      start_reply_future_2
          .GetCallback<const user_data_auth::StartAuthSessionReply&>());
  RunUntilIdle();
  user_data_auth::StartAuthSessionReply start_reply =
      start_reply_future_2.Take();
  EXPECT_EQ(start_reply.error(), user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  std::sort(
      start_reply.mutable_configured_auth_factors_with_status()
          ->pointer_begin(),
      start_reply.mutable_configured_auth_factors_with_status()->pointer_end(),
      [](const user_data_auth::AuthFactorWithStatus* lhs,
         const user_data_auth::AuthFactorWithStatus* rhs) {
        return lhs->auth_factor().label() < rhs->auth_factor().label();
      });
  ASSERT_EQ(start_reply.configured_auth_factors_with_status_size(), 1);
  EXPECT_EQ(
      start_reply.configured_auth_factors_with_status(0).auth_factor().label(),
      "fp-label");
  EXPECT_TRUE(start_reply.configured_auth_factors_with_status(0)
                  .auth_factor()
                  .has_fingerprint_metadata());
  EXPECT_TRUE(
      start_reply.configured_auth_factors_with_status(0).has_status_info());
  EXPECT_EQ(start_reply.configured_auth_factors_with_status(0)
                .status_info()
                .time_available_in(),
            30000);
  EXPECT_EQ(start_reply.configured_auth_factors_with_status(0)
                .status_info()
                .time_expiring_in(),
            20000);
}

TEST_F(UserDataAuthExTest, ListAuthFactorsWithFactorsFromUssPinLockedLegacy) {
  // Setup.
  const Username kUser("foo@example.com");
  const ObfuscatedUsername kObfuscatedUser = SanitizeUserName(kUser);

  EXPECT_CALL(system_apis_.hwsec, IsPinWeaverEnabled())
      .WillRepeatedly(ReturnValue(true));
  EXPECT_CALL(system_apis_.platform, DirectoryExists(_))
      .WillRepeatedly(Return(true));

  // Set up standard list auth factor parameters, we'll be calling this a few
  // times during the test.
  user_data_auth::ListAuthFactorsRequest list_request;
  list_request.mutable_account_id()->set_account_id(*kUser);
  TestFuture<user_data_auth::ListAuthFactorsReply> list_reply_future_1;

  // List all the auth factors, there should be none at the start.
  userdataauth_->ListAuthFactors(
      list_request,
      list_reply_future_1
          .GetCallback<const user_data_auth::ListAuthFactorsReply&>());
  EXPECT_EQ(list_reply_future_1.Get().error(),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  EXPECT_THAT(list_reply_future_1.Get().configured_auth_factors_with_status(),
              IsEmpty());
  EXPECT_THAT(list_reply_future_1.Get().supported_auth_factors(),
              UnorderedElementsAre(
                  user_data_auth::AUTH_FACTOR_TYPE_PASSWORD,
                  user_data_auth::AUTH_FACTOR_TYPE_PIN,
                  user_data_auth::AUTH_FACTOR_TYPE_KIOSK,
                  user_data_auth::AUTH_FACTOR_TYPE_SMART_CARD,
                  user_data_auth::AUTH_FACTOR_TYPE_CRYPTOHOME_RECOVERY));
  system_apis_.auth_factor_manager.DiscardAuthFactorMap(kObfuscatedUser);

  // Add uss auth factors, we should be able to list them.
  auto password_factor = std::make_unique<AuthFactor>(
      AuthFactorType::kPassword, "password-label",
      AuthFactorMetadata{.metadata = PasswordMetadata()},
      AuthBlockState{
          .state = TpmBoundToPcrAuthBlockState{
              .scrypt_derived = false,
              .salt = BlobFromString("fake salt"),
              .tpm_key = BlobFromString("fake tpm key"),
              .extended_tpm_key = BlobFromString("fake extended tpm key"),
              .tpm_public_key_hash = BlobFromString("fake tpm public key hash"),
          }});
  ASSERT_THAT(system_apis_.auth_factor_manager.SaveAuthFactorFile(
                  kObfuscatedUser, *password_factor),
              IsOk());
  auto pin_factor = std::make_unique<AuthFactor>(
      AuthFactorType::kPin, "pin-label",
      AuthFactorMetadata{.metadata = PinMetadata()},
      AuthBlockState{.state = PinWeaverAuthBlockState{
                         .le_label = 0xbaadf00d,
                         .salt = BlobFromString("fake salt"),
                         .chaps_iv = BlobFromString("fake chaps IV"),
                         .fek_iv = BlobFromString("fake file encryption IV"),
                         .reset_salt = BlobFromString("more fake salt"),
                     }});
  ASSERT_THAT(system_apis_.auth_factor_manager.SaveAuthFactorFile(
                  kObfuscatedUser, *pin_factor),
              IsOk());
  MakeUssWithLabels(kObfuscatedUser, {"password-label", "pin-label"});

  EXPECT_CALL(system_apis_.hwsec_pw_manager, GetDelayInSeconds)
      .WillRepeatedly(ReturnValue(UINT32_MAX));
  // ListAuthFactors() load the factors according to the USS experiment status.
  TestFuture<user_data_auth::ListAuthFactorsReply> list_reply_future_2;
  userdataauth_->ListAuthFactors(
      list_request,
      list_reply_future_2
          .GetCallback<const user_data_auth::ListAuthFactorsReply&>());
  user_data_auth::ListAuthFactorsReply list_reply_2 =
      list_reply_future_2.Take();
  EXPECT_EQ(list_reply_2.error(), user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  std::sort(
      list_reply_2.mutable_configured_auth_factors_with_status()
          ->pointer_begin(),
      list_reply_2.mutable_configured_auth_factors_with_status()->pointer_end(),
      [](const user_data_auth::AuthFactorWithStatus* lhs,
         const user_data_auth::AuthFactorWithStatus* rhs) {
        return lhs->auth_factor().label() < rhs->auth_factor().label();
      });
  ASSERT_EQ(list_reply_2.configured_auth_factors_with_status_size(), 2);
  EXPECT_EQ(
      list_reply_2.configured_auth_factors_with_status(0).auth_factor().label(),
      "password-label");
  EXPECT_TRUE(list_reply_2.configured_auth_factors_with_status(0)
                  .auth_factor()
                  .has_password_metadata());
  EXPECT_TRUE(list_reply_2.configured_auth_factors_with_status(0)
                  .auth_factor()
                  .has_common_metadata());
  EXPECT_EQ(list_reply_2.configured_auth_factors_with_status(0)
                .auth_factor()
                .common_metadata()
                .lockout_policy(),
            user_data_auth::LOCKOUT_POLICY_NONE);
  EXPECT_EQ(
      list_reply_2.configured_auth_factors_with_status(1).auth_factor().label(),
      "pin-label");
  EXPECT_TRUE(list_reply_2.configured_auth_factors_with_status(1)
                  .auth_factor()
                  .has_pin_metadata());
  EXPECT_TRUE(list_reply_2.configured_auth_factors_with_status(1)
                  .auth_factor()
                  .has_common_metadata());
  EXPECT_EQ(list_reply_2.configured_auth_factors_with_status(1)
                .auth_factor()
                .common_metadata()
                .lockout_policy(),
            user_data_auth::LOCKOUT_POLICY_ATTEMPT_LIMITED);
  EXPECT_TRUE(
      list_reply_2.configured_auth_factors_with_status(1).has_status_info());
  EXPECT_EQ(list_reply_2.configured_auth_factors_with_status(1)
                .status_info()
                .time_available_in(),
            std::numeric_limits<uint64_t>::max());
  EXPECT_EQ(list_reply_2.configured_auth_factors_with_status(1)
                .status_info()
                .time_expiring_in(),
            std::numeric_limits<uint64_t>::max());
  EXPECT_THAT(
      list_reply_2.supported_auth_factors(),
      UnorderedElementsAre(user_data_auth::AUTH_FACTOR_TYPE_PASSWORD,
                           user_data_auth::AUTH_FACTOR_TYPE_PIN,
                           user_data_auth::AUTH_FACTOR_TYPE_CRYPTOHOME_RECOVERY,
                           user_data_auth::AUTH_FACTOR_TYPE_SMART_CARD));
}

TEST_F(UserDataAuthExTest, ListAuthFactorsWithFactorsFromUssPinLockedModern) {
  // Setup.
  const Username kUser("foo@example.com");
  const ObfuscatedUsername kObfuscatedUser = SanitizeUserName(kUser);

  EXPECT_CALL(system_apis_.hwsec, IsPinWeaverEnabled())
      .WillRepeatedly(ReturnValue(true));
  EXPECT_CALL(system_apis_.platform, DirectoryExists(_))
      .WillRepeatedly(Return(true));

  // Set up standard list auth factor parameters, we'll be calling this a few
  // times during the test.
  user_data_auth::ListAuthFactorsRequest list_request;
  list_request.mutable_account_id()->set_account_id(*kUser);
  TestFuture<user_data_auth::ListAuthFactorsReply> list_reply_future_1;

  // List all the auth factors, there should be none at the start.
  userdataauth_->ListAuthFactors(
      list_request,
      list_reply_future_1
          .GetCallback<const user_data_auth::ListAuthFactorsReply&>());
  EXPECT_EQ(list_reply_future_1.Get().error(),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  EXPECT_THAT(list_reply_future_1.Get().configured_auth_factors_with_status(),
              IsEmpty());
  EXPECT_THAT(list_reply_future_1.Get().supported_auth_factors(),
              UnorderedElementsAre(
                  user_data_auth::AUTH_FACTOR_TYPE_PASSWORD,
                  user_data_auth::AUTH_FACTOR_TYPE_PIN,
                  user_data_auth::AUTH_FACTOR_TYPE_KIOSK,
                  user_data_auth::AUTH_FACTOR_TYPE_SMART_CARD,
                  user_data_auth::AUTH_FACTOR_TYPE_CRYPTOHOME_RECOVERY));
  system_apis_.auth_factor_manager.DiscardAuthFactorMap(kObfuscatedUser);

  // Add uss auth factors, we should be able to list them.
  auto password_factor = std::make_unique<AuthFactor>(
      AuthFactorType::kPassword, "password-label",
      AuthFactorMetadata{.metadata = PasswordMetadata()},
      AuthBlockState{
          .state = TpmBoundToPcrAuthBlockState{
              .scrypt_derived = false,
              .salt = BlobFromString("fake salt"),
              .tpm_key = BlobFromString("fake tpm key"),
              .extended_tpm_key = BlobFromString("fake extended tpm key"),
              .tpm_public_key_hash = BlobFromString("fake tpm public key hash"),
          }});
  ASSERT_THAT(system_apis_.auth_factor_manager.SaveAuthFactorFile(
                  kObfuscatedUser, *password_factor),
              IsOk());
  auto pin_factor = std::make_unique<AuthFactor>(
      AuthFactorType::kPin, "pin-label",
      AuthFactorMetadata{
          .common = CommonMetadata{.lockout_policy =
                                       SerializedLockoutPolicy::TIME_LIMITED},
          .metadata = PinMetadata()},
      AuthBlockState{.state = PinWeaverAuthBlockState{
                         .le_label = 0xbaadf00d,
                         .salt = BlobFromString("fake salt"),
                         .chaps_iv = BlobFromString("fake chaps IV"),
                         .fek_iv = BlobFromString("fake file encryption IV"),
                         .reset_salt = BlobFromString("more fake salt"),
                     }});
  ASSERT_THAT(system_apis_.auth_factor_manager.SaveAuthFactorFile(
                  kObfuscatedUser, *pin_factor),
              IsOk());
  MakeUssWithLabels(kObfuscatedUser, {"password-label", "pin-label"});

  EXPECT_CALL(system_apis_.hwsec_pw_manager, GetDelayInSeconds)
      .WillRepeatedly(ReturnValue(30));

  TestFuture<user_data_auth::ListAuthFactorsReply> list_reply_future_2;
  userdataauth_->ListAuthFactors(
      list_request,
      list_reply_future_2
          .GetCallback<const user_data_auth::ListAuthFactorsReply&>());
  user_data_auth::ListAuthFactorsReply list_reply_2 =
      list_reply_future_2.Take();
  EXPECT_EQ(list_reply_2.error(), user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  std::sort(
      list_reply_2.mutable_configured_auth_factors_with_status()
          ->pointer_begin(),
      list_reply_2.mutable_configured_auth_factors_with_status()->pointer_end(),
      [](const user_data_auth::AuthFactorWithStatus* lhs,
         const user_data_auth::AuthFactorWithStatus* rhs) {
        return lhs->auth_factor().label() < rhs->auth_factor().label();
      });
  ASSERT_EQ(list_reply_2.configured_auth_factors_with_status_size(), 2);
  EXPECT_EQ(
      list_reply_2.configured_auth_factors_with_status(0).auth_factor().label(),
      "password-label");
  EXPECT_TRUE(list_reply_2.configured_auth_factors_with_status(0)
                  .auth_factor()
                  .has_password_metadata());
  EXPECT_TRUE(list_reply_2.configured_auth_factors_with_status(0)
                  .auth_factor()
                  .has_common_metadata());
  EXPECT_EQ(list_reply_2.configured_auth_factors_with_status(0)
                .auth_factor()
                .common_metadata()
                .lockout_policy(),
            user_data_auth::LOCKOUT_POLICY_NONE);
  EXPECT_EQ(
      list_reply_2.configured_auth_factors_with_status(1).auth_factor().label(),
      "pin-label");
  EXPECT_TRUE(list_reply_2.configured_auth_factors_with_status(1)
                  .auth_factor()
                  .has_pin_metadata());
  EXPECT_TRUE(list_reply_2.configured_auth_factors_with_status(1)
                  .auth_factor()
                  .has_common_metadata());
  EXPECT_EQ(list_reply_2.configured_auth_factors_with_status(1)
                .auth_factor()
                .common_metadata()
                .lockout_policy(),
            user_data_auth::LOCKOUT_POLICY_TIME_LIMITED);
  EXPECT_TRUE(
      list_reply_2.configured_auth_factors_with_status(1).has_status_info());
  EXPECT_EQ(list_reply_2.configured_auth_factors_with_status(1)
                .status_info()
                .time_available_in(),
            30000);
  EXPECT_EQ(list_reply_2.configured_auth_factors_with_status(1)
                .status_info()
                .time_expiring_in(),
            std::numeric_limits<uint64_t>::max());
  EXPECT_THAT(
      list_reply_2.supported_auth_factors(),
      UnorderedElementsAre(user_data_auth::AUTH_FACTOR_TYPE_PASSWORD,
                           user_data_auth::AUTH_FACTOR_TYPE_PIN,
                           user_data_auth::AUTH_FACTOR_TYPE_CRYPTOHOME_RECOVERY,
                           user_data_auth::AUTH_FACTOR_TYPE_SMART_CARD));
}

TEST_F(UserDataAuthExTest, ListAuthFactorsWithFactorsFromUssAndVk) {
  const Username kUser("foo@example.com");
  const ObfuscatedUsername kObfuscatedUser = SanitizeUserName(kUser);

  EXPECT_CALL(system_apis_.hwsec, IsPinWeaverEnabled())
      .WillRepeatedly(ReturnValue(true));
  EXPECT_CALL(system_apis_.hwsec_pw_manager, GetDelayInSeconds(_))
      .WillRepeatedly(ReturnValue(UINT32_MAX));
  EXPECT_CALL(system_apis_.platform, DirectoryExists(_))
      .WillRepeatedly(Return(true));

  // Set up standard list auth factor parameters, we'll be calling this a few
  // times during the test.
  user_data_auth::ListAuthFactorsRequest list_request;
  list_request.mutable_account_id()->set_account_id(*kUser);
  TestFuture<user_data_auth::ListAuthFactorsReply> list_reply_future_1;

  // List all the auth factors, there should be none at the start.
  userdataauth_->ListAuthFactors(
      list_request,
      list_reply_future_1
          .GetCallback<const user_data_auth::ListAuthFactorsReply&>());
  EXPECT_EQ(list_reply_future_1.Get().error(),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  EXPECT_THAT(list_reply_future_1.Get().configured_auth_factors_with_status(),
              IsEmpty());
  EXPECT_THAT(list_reply_future_1.Get().supported_auth_factors(),
              UnorderedElementsAre(
                  user_data_auth::AUTH_FACTOR_TYPE_PASSWORD,
                  user_data_auth::AUTH_FACTOR_TYPE_PIN,
                  user_data_auth::AUTH_FACTOR_TYPE_KIOSK,
                  user_data_auth::AUTH_FACTOR_TYPE_SMART_CARD,
                  user_data_auth::AUTH_FACTOR_TYPE_CRYPTOHOME_RECOVERY));
  system_apis_.auth_factor_manager.DiscardAuthFactorMap(kObfuscatedUser);

  // Set up mocks for a VK.
  std::vector<int> vk_indice = {0};
  EXPECT_CALL(system_apis_.keyset_management,
              GetVaultKeysets(kObfuscatedUser, _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(vk_indice), Return(true)));
  EXPECT_CALL(system_apis_.keyset_management,
              LoadVaultKeysetForUser(kObfuscatedUser, 0))
      .WillRepeatedly([](const ObfuscatedUsername&, int) {
        auto vk = std::make_unique<VaultKeyset>();
        vk->SetFlags(SerializedVaultKeyset::TPM_WRAPPED |
                     SerializedVaultKeyset::PCR_BOUND);
        KeyData key_data;
        key_data.set_type(KeyData::KEY_TYPE_PASSWORD);
        key_data.set_label("password-label");
        vk->SetKeyData(key_data);
        vk->SetTPMKey(BlobFromString("fake tpm key"));
        vk->SetExtendedTPMKey(BlobFromString("fake extended tpm key"));
        return vk;
      });
  // Add an AuthFactor backed by USS.
  auto pin_factor = std::make_unique<AuthFactor>(
      AuthFactorType::kPin, "pin-label",
      AuthFactorMetadata{.metadata = PinMetadata()},
      AuthBlockState{.state = PinWeaverAuthBlockState{
                         .le_label = 0xbaadf00d,
                         .salt = BlobFromString("fake salt"),
                         .chaps_iv = BlobFromString("fake chaps IV"),
                         .fek_iv = BlobFromString("fake file encryption IV"),
                         .reset_salt = BlobFromString("more fake salt"),
                     }});
  ASSERT_THAT(system_apis_.auth_factor_manager.SaveAuthFactorFile(
                  kObfuscatedUser, *pin_factor),
              IsOk());
  MakeUssWithLabels(kObfuscatedUser, {"pin-label"});

  TestFuture<user_data_auth::ListAuthFactorsReply> list_reply_future_2;
  userdataauth_->ListAuthFactors(
      list_request,
      list_reply_future_2
          .GetCallback<const user_data_auth::ListAuthFactorsReply&>());
  user_data_auth::ListAuthFactorsReply list_reply_2 =
      list_reply_future_2.Take();
  EXPECT_EQ(list_reply_2.error(), user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  std::sort(
      list_reply_2.mutable_configured_auth_factors_with_status()
          ->pointer_begin(),
      list_reply_2.mutable_configured_auth_factors_with_status()->pointer_end(),
      [](const user_data_auth::AuthFactorWithStatus* lhs,
         const user_data_auth::AuthFactorWithStatus* rhs) {
        return lhs->auth_factor().label() < rhs->auth_factor().label();
      });
  ASSERT_EQ(list_reply_2.configured_auth_factors_with_status_size(), 2);
  EXPECT_EQ(
      list_reply_2.configured_auth_factors_with_status(0).auth_factor().label(),
      "password-label");
  EXPECT_TRUE(list_reply_2.configured_auth_factors_with_status(0)
                  .auth_factor()
                  .has_password_metadata());
  EXPECT_TRUE(list_reply_2.configured_auth_factors_with_status(0)
                  .auth_factor()
                  .has_common_metadata());
  EXPECT_EQ(list_reply_2.configured_auth_factors_with_status(0)
                .auth_factor()
                .common_metadata()
                .lockout_policy(),
            user_data_auth::LOCKOUT_POLICY_NONE);
  EXPECT_EQ(
      list_reply_2.configured_auth_factors_with_status(1).auth_factor().label(),
      "pin-label");
  EXPECT_TRUE(list_reply_2.configured_auth_factors_with_status(1)
                  .auth_factor()
                  .has_pin_metadata());
  EXPECT_TRUE(list_reply_2.configured_auth_factors_with_status(1)
                  .auth_factor()
                  .has_common_metadata());
  EXPECT_EQ(list_reply_2.configured_auth_factors_with_status(1)
                .auth_factor()
                .common_metadata()
                .lockout_policy(),
            user_data_auth::LOCKOUT_POLICY_ATTEMPT_LIMITED);
  EXPECT_THAT(
      list_reply_2.supported_auth_factors(),
      UnorderedElementsAre(user_data_auth::AUTH_FACTOR_TYPE_PASSWORD,
                           user_data_auth::AUTH_FACTOR_TYPE_PIN,
                           user_data_auth::AUTH_FACTOR_TYPE_CRYPTOHOME_RECOVERY,
                           user_data_auth::AUTH_FACTOR_TYPE_SMART_CARD));
}

TEST_F(UserDataAuthExTest, PrepareAuthFactorNoAuthSessionIdFailure) {
  // Setup.
  PrepareArguments();
  // Prepare the request and set up the mock components.
  user_data_auth::PrepareAuthFactorRequest prepare_auth_factor_req;
  prepare_auth_factor_req.set_auth_factor_type(
      user_data_auth::AUTH_FACTOR_TYPE_FINGERPRINT);
  prepare_auth_factor_req.set_purpose(
      user_data_auth::PURPOSE_AUTHENTICATE_AUTH_FACTOR);

  // Test.
  TestFuture<user_data_auth::PrepareAuthFactorReply>
      prepare_auth_factor_reply_future;
  userdataauth_->PrepareAuthFactor(
      prepare_auth_factor_req,
      prepare_auth_factor_reply_future
          .GetCallback<const user_data_auth::PrepareAuthFactorReply&>());
  RunUntilIdle();

  // Verify.
  EXPECT_EQ(prepare_auth_factor_reply_future.Get().error(),
            user_data_auth::CRYPTOHOME_INVALID_AUTH_SESSION_TOKEN);
}

TEST_F(UserDataAuthExTest, PrepareAuthFactorPasswordFailure) {
  // Setup.
  PrepareArguments();
  start_auth_session_req_->mutable_account_id()->set_account_id(
      "foo@example.com");
  TestFuture<user_data_auth::StartAuthSessionReply> auth_session_reply_future;
  userdataauth_->StartAuthSession(
      *start_auth_session_req_,
      auth_session_reply_future
          .GetCallback<const user_data_auth::StartAuthSessionReply&>());
  RunUntilIdle();
  EXPECT_EQ(auth_session_reply_future.Get().error(),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  const std::string auth_session_id =
      auth_session_reply_future.Get().auth_session_id();
  EXPECT_TRUE(
      AuthSession::GetTokenFromSerializedString(auth_session_id).has_value());

  // Prepare the request and set up the mock components.
  user_data_auth::PrepareAuthFactorRequest prepare_auth_factor_req;
  prepare_auth_factor_req.set_auth_session_id(auth_session_id);
  prepare_auth_factor_req.set_auth_factor_type(
      user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
  prepare_auth_factor_req.set_purpose(
      user_data_auth::PURPOSE_AUTHENTICATE_AUTH_FACTOR);

  // Test.
  TestFuture<user_data_auth::PrepareAuthFactorReply>
      prepare_auth_factor_reply_future;
  userdataauth_->PrepareAuthFactor(
      prepare_auth_factor_req,
      prepare_auth_factor_reply_future
          .GetCallback<const user_data_auth::PrepareAuthFactorReply&>());
  RunUntilIdle();

  // Verify.
  EXPECT_EQ(prepare_auth_factor_reply_future.Get().error(),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
}

TEST_F(UserDataAuthExTest, TerminateAuthFactorFingerprintSuccess) {
  // Setup.
  PrepareArguments();
  start_auth_session_req_->mutable_account_id()->set_account_id(
      "foo@example.com");
  TestFuture<user_data_auth::StartAuthSessionReply> auth_session_reply_future;
  userdataauth_->StartAuthSession(
      *start_auth_session_req_,
      auth_session_reply_future
          .GetCallback<const user_data_auth::StartAuthSessionReply&>());
  RunUntilIdle();
  EXPECT_EQ(auth_session_reply_future.Get().error(),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  const std::string auth_session_id =
      auth_session_reply_future.Get().auth_session_id();
  EXPECT_TRUE(
      AuthSession::GetTokenFromSerializedString(auth_session_id).has_value());

  // Execute a successful PrepareAuthFactor with mocked response.
  user_data_auth::PrepareAuthFactorRequest prepare_auth_factor_req;
  prepare_auth_factor_req.set_auth_session_id(auth_session_id);
  prepare_auth_factor_req.set_auth_factor_type(
      user_data_auth::AUTH_FACTOR_TYPE_LEGACY_FINGERPRINT);
  prepare_auth_factor_req.set_purpose(
      user_data_auth::PURPOSE_AUTHENTICATE_AUTH_FACTOR);
  EXPECT_CALL(fingerprint_manager_, StartAuthSessionAsyncForUser)
      .WillOnce([](auto&&, auto&& callback) { std::move(callback).Run(true); });
  TestFuture<user_data_auth::PrepareAuthFactorReply>
      prepare_auth_factor_reply_future;
  userdataauth_->PrepareAuthFactor(
      prepare_auth_factor_req,
      prepare_auth_factor_reply_future
          .GetCallback<const user_data_auth::PrepareAuthFactorReply&>());
  RunUntilIdle();
  EXPECT_EQ(prepare_auth_factor_reply_future.Get().error(),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);

  // Test.
  user_data_auth::TerminateAuthFactorRequest terminate_auth_factor_req;
  terminate_auth_factor_req.set_auth_session_id(auth_session_id);
  terminate_auth_factor_req.set_auth_factor_type(
      user_data_auth::AUTH_FACTOR_TYPE_LEGACY_FINGERPRINT);
  TestFuture<user_data_auth::TerminateAuthFactorReply>
      terminate_auth_factor_reply_future;
  userdataauth_->TerminateAuthFactor(
      terminate_auth_factor_req,
      terminate_auth_factor_reply_future
          .GetCallback<const user_data_auth::TerminateAuthFactorReply&>());
  RunUntilIdle();

  // Verify.
  EXPECT_EQ(terminate_auth_factor_reply_future.Get().error(),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
}

TEST_F(UserDataAuthExTest, TerminateAuthFactorInactiveFactorFailure) {
  // Setup.
  PrepareArguments();
  start_auth_session_req_->mutable_account_id()->set_account_id(
      "foo@example.com");
  TestFuture<user_data_auth::StartAuthSessionReply> auth_session_reply_future;
  userdataauth_->StartAuthSession(
      *start_auth_session_req_,
      auth_session_reply_future
          .GetCallback<const user_data_auth::StartAuthSessionReply&>());
  RunUntilIdle();
  EXPECT_EQ(auth_session_reply_future.Get().error(),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  const std::string auth_session_id =
      auth_session_reply_future.Get().auth_session_id();
  EXPECT_TRUE(
      AuthSession::GetTokenFromSerializedString(auth_session_id).has_value());

  // Test. TerminateAuthFactor fails when there is
  // no pending fingerprint auth factor to be terminated.
  user_data_auth::TerminateAuthFactorRequest terminate_auth_factor_req;
  terminate_auth_factor_req.set_auth_session_id(auth_session_id);
  terminate_auth_factor_req.set_auth_factor_type(
      user_data_auth::AUTH_FACTOR_TYPE_FINGERPRINT);
  TestFuture<user_data_auth::TerminateAuthFactorReply>
      terminate_auth_factor_reply_future;
  userdataauth_->TerminateAuthFactor(
      terminate_auth_factor_req,
      terminate_auth_factor_reply_future
          .GetCallback<const user_data_auth::TerminateAuthFactorReply&>());
  RunUntilIdle();

  // Verify.
  EXPECT_EQ(terminate_auth_factor_reply_future.Get().error(),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
}

TEST_F(UserDataAuthExTest, TerminateAuthFactorBadTypeFailure) {
  // Setup.
  PrepareArguments();
  start_auth_session_req_->mutable_account_id()->set_account_id(
      "foo@example.com");
  TestFuture<user_data_auth::StartAuthSessionReply> auth_session_reply_future;
  userdataauth_->StartAuthSession(
      *start_auth_session_req_,
      auth_session_reply_future
          .GetCallback<const user_data_auth::StartAuthSessionReply&>());
  RunUntilIdle();
  EXPECT_EQ(auth_session_reply_future.Get().error(),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  const std::string auth_session_id =
      auth_session_reply_future.Get().auth_session_id();
  EXPECT_TRUE(
      AuthSession::GetTokenFromSerializedString(auth_session_id).has_value());

  // Test. TerminateAuthFactor fails when the auth factor type
  // does not support PrepareAuthFactor.
  user_data_auth::TerminateAuthFactorRequest terminate_auth_factor_req;
  terminate_auth_factor_req.set_auth_session_id(auth_session_id);
  terminate_auth_factor_req.set_auth_factor_type(
      user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
  TestFuture<user_data_auth::TerminateAuthFactorReply>
      terminate_auth_factor_reply_future;
  userdataauth_->TerminateAuthFactor(
      terminate_auth_factor_req,
      terminate_auth_factor_reply_future
          .GetCallback<const user_data_auth::TerminateAuthFactorReply&>());
  RunUntilIdle();

  // Verify.
  EXPECT_EQ(terminate_auth_factor_reply_future.Get().error(),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
}

TEST_F(UserDataAuthExTest, GetRecoverableKeyStores) {
  const Username kUser("foo@example.com");
  const ObfuscatedUsername kObfuscatedUser = SanitizeUserName(kUser);

  EXPECT_CALL(system_apis_.platform, DirectoryExists(_))
      .WillRepeatedly(Return(true));

  // Add uss auth factors, 1 with recoverable key store and 1 without.
  auto password_factor = std::make_unique<AuthFactor>(
      AuthFactorType::kPassword, "password-label",
      AuthFactorMetadata{.metadata = PasswordMetadata()},
      AuthBlockState{.state = TpmBoundToPcrAuthBlockState{}});
  ASSERT_THAT(system_apis_.auth_factor_manager.SaveAuthFactorFile(
                  kObfuscatedUser, *password_factor),
              IsOk());
  std::string key_store_proto;
  EXPECT_TRUE(RecoverableKeyStore().SerializeToString(&key_store_proto));
  auto pin_factor = std::make_unique<AuthFactor>(
      AuthFactorType::kPin, "pin-label",
      AuthFactorMetadata{.metadata = PinMetadata()},
      AuthBlockState{
          .state = PinWeaverAuthBlockState{},
          .recoverable_key_store_state = RecoverableKeyStoreState{
              .key_store_proto = brillo::BlobFromString(key_store_proto)}});
  ASSERT_THAT(system_apis_.auth_factor_manager.SaveAuthFactorFile(
                  kObfuscatedUser, *pin_factor),
              IsOk());
  MakeUssWithLabels(kObfuscatedUser, {"password-label", "pin-label"});

  TestFuture<user_data_auth::GetRecoverableKeyStoresReply> reply_future;
  user_data_auth::GetRecoverableKeyStoresRequest request;
  request.mutable_account_id()->set_account_id(*kUser);
  userdataauth_->GetRecoverableKeyStores(
      request,
      reply_future
          .GetCallback<const user_data_auth::GetRecoverableKeyStoresReply&>());
  RunUntilIdle();
  user_data_auth::GetRecoverableKeyStoresReply reply = reply_future.Take();
  EXPECT_EQ(reply.error(), user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
}

// ================ Tests requiring fully threaded environment ================

// Test fixture that implements fully threaded environment in UserDataAuth.
// Note that this does not initialize |userdataauth_|.
class UserDataAuthTestThreaded : public UserDataAuthTestBase {
 public:
  UserDataAuthTestThreaded() : origin_thread_("origin_thread") {}
  UserDataAuthTestThreaded(const UserDataAuthTestThreaded&) = delete;
  UserDataAuthTestThreaded& operator=(const UserDataAuthTestThreaded&) = delete;

  ~UserDataAuthTestThreaded() override = default;

  // Post a task to the origin thread, then wait for it to finish.
  void PostToOriginAndBlock(base::OnceClosure task) {
    base::WaitableEvent done(base::WaitableEvent::ResetPolicy::MANUAL,
                             base::WaitableEvent::InitialState::NOT_SIGNALED);

    origin_thread_.task_runner()->PostTask(
        FROM_HERE, base::BindOnce(
                       [](base::OnceClosure task, base::WaitableEvent* done) {
                         std::move(task).Run();
                         done->Signal();
                       },
                       std::move(task), base::Unretained(&done)));

    done.Wait();
  }

  void SetUp() override {
    origin_thread_.Start();

    PostToOriginAndBlock(base::BindOnce(
        &UserDataAuthTestThreaded::SetUpInOrigin, base::Unretained(this)));
  }

  void SetUpInOrigin() {
    // Create the |userdataauth_| object.
    userdataauth_ =
        std::make_unique<UserDataAuth>(system_apis_.ToBackingApis());

    // Setup the usual stuff
    UserDataAuthTestBase::SetUp();
  }

  void TearDown() override {
    PostToOriginAndBlock(base::BindOnce(
        &UserDataAuthTestThreaded::TearDownInOrigin, base::Unretained(this)));

    origin_thread_.Stop();
  }

  void TearDownInOrigin() {
    // Destruct the |userdataauth_| object.
    userdataauth_.reset();
  }

  // Initialize |userdataauth_| in |origin_thread_|
  void InitializeUserDataAuth() {
    PostToOriginAndBlock(
        base::BindOnce(base::IgnoreResult(&UserDataAuth::Initialize),
                       base::Unretained(userdataauth_.get()), mount_bus_));
  }

 protected:
  // The thread on which the |userdataauth_| object is created. This is the same
  // as |userdataauth_->origin_thread_|.
  base::Thread origin_thread_;
};

TEST_F(UserDataAuthTestThreaded, ShutdownTask) {
  InitializeUserDataAuth();
  EXPECT_CALL(*mount_bus_, ShutdownAndBlock()).Times(1);
  PostToOriginAndBlock(base::BindLambdaForTesting([this]() {
    // Destruct the |userdataauth_| object.
    userdataauth_.reset();
  }));
}

// ============== Full API Behaviour Test for Negative Testing ==============

// This section holds tests that simulate API calls so that we can test that the
// right error comes up in error conditions.

// This serves as the base class for all full API behaviour tests. It is for a
// set of integration-style unit tests that is aimed at stressing the negative
// cases from an API usage perspective. This differs from other unit tests in
// which it is written in more of a integration test style and verifies the
// behaviour of cryptohomed APIs rather than the UserDataAuth class.
class UserDataAuthApiTest : public UserDataAuthTest {
 public:
  UserDataAuthApiTest() = default;

  void SetUp() override {
    // We need to simulate manufacturer to allow ECC auth blocks.
    ON_CALL(sim_factory_.GetMockBackend().GetMock().vendor, GetManufacturer)
        .WillByDefault(ReturnValue(0x43524F53));
    // Assume that TPM is ready.
    ON_CALL(sim_factory_.GetMockBackend().GetMock().state, IsReady)
        .WillByDefault(ReturnValue(true));
    // Sealing is supported.
    ON_CALL(sim_factory_.GetMockBackend().GetMock().sealing, IsSupported)
        .WillByDefault(ReturnValue(true));

    sim_hwsec_ = sim_factory_.GetCryptohomeFrontend();
    sim_hwsec_pw_manager_ = sim_factory_.GetPinWeaverManagerFrontend();
    sim_recovery_crypto_ = sim_factory_.GetRecoveryCryptoFrontend();
    sim_keys_manager_ = std::make_unique<CryptohomeKeysManager>(
        sim_hwsec_.get(), &system_apis_.platform);
    sim_crypto_ = std::make_unique<Crypto>(
        sim_hwsec_.get(), sim_hwsec_pw_manager_.get(), sim_keys_manager_.get(),
        sim_recovery_crypto_.get());
    auto backing_apis = system_apis_.ToBackingApis();
    backing_apis.hwsec = sim_hwsec_.get();
    backing_apis.hwsec_pw_manager = sim_hwsec_pw_manager_.get();
    backing_apis.recovery_crypto = sim_recovery_crypto_.get();
    backing_apis.cryptohome_keys_manager = sim_keys_manager_.get();
    backing_apis.crypto = sim_crypto_.get();
    userdataauth_ = std::make_unique<UserDataAuth>(backing_apis);
    userdataauth_->SetSignallingInterface(signalling_);

    SetupDefaultUserDataAuth();
    SetupMountFactory();
    // Note: We skip SetupHwsec() because we use the simulated libhwsec layer.
    SetupTasks();
    InitializeUserDataAuth();
  }

  void SetupMountFactory() {
    userdataauth_->set_mount_factory_for_testing(&mount_factory_);

    ON_CALL(mount_factory_, New(_, _, _, _))
        .WillByDefault(Invoke([this](libstorage::Platform* platform,
                                     HomeDirs* homedirs, bool legacy_mount,
                                     bool bind_mount_downloads) -> Mount* {
          if (new_mounts_.empty()) {
            ADD_FAILURE() << "Not enough objects in new_mounts_";
            return nullptr;
          }
          Mount* result = new_mounts_[0];
          new_mounts_.pop_front();
          return result;
        }));
  }

  // Simply the Sync() version of StartAuthSession(). Caller should check that
  // the returned value is not nullopt, which indicates that the call did not
  // finish.
  std::optional<user_data_auth::StartAuthSessionReply> StartAuthSessionSync(
      const user_data_auth::StartAuthSessionRequest& in_request) {
    TestFuture<user_data_auth::StartAuthSessionReply> reply_future;
    userdataauth_->StartAuthSession(
        in_request,
        reply_future
            .GetCallback<const user_data_auth::StartAuthSessionReply&>());
    RunUntilIdle();
    return reply_future.Get();
  }

  // Obtain a test auth session for kUsername1. Result is nullopt if it's
  // unsuccessful.
  std::optional<std::string> GetTestUnauthedAuthSession(
      const Username& username, AuthSession::CreateOptions options) {
    user_data_auth::StartAuthSessionRequest req;
    req.mutable_account_id()->set_account_id(*username);
    req.set_intent(AuthIntentToProto(*options.intent));
    req.set_is_ephemeral_user(*options.is_ephemeral_user);
    std::optional<user_data_auth::StartAuthSessionReply> reply =
        StartAuthSessionSync(req);
    if (!reply.has_value()) {
      LOG(ERROR) << "GetTestUnauthedAuthSession() failed because "
                    "StartAuthSession() did not complete.";
      return std::nullopt;
    }

    if (reply.value().error_info().primary_action() !=
        user_data_auth::PrimaryAction::PRIMARY_NO_ERROR) {
      LOG(ERROR) << "GetTestUnauthedAuthSession() failed because "
                    "StartAuthSession() failed.";
      return std::nullopt;
    }
    return reply.value().auth_session_id();
  }

  // Create a test user named kUsername1 with kPassword1. Return true if
  // successful. This doesn't create the vault.
  bool CreateTestUser() {
    std::optional<std::string> session_id = GetTestUnauthedAuthSession(
        kUsername1,
        {.is_ephemeral_user = false, .intent = AuthIntent::kDecrypt});
    if (!session_id.has_value()) {
      LOG(ERROR) << "No session ID in CreateTestUser().";
      return false;
    }

    EXPECT_CALL(homedirs_, CryptohomeExists(_)).WillOnce(ReturnValue(false));
    EXPECT_CALL(homedirs_, Create(_)).WillOnce(Return(true));

    // Create the user.
    user_data_auth::CreatePersistentUserRequest create_request;
    create_request.set_auth_session_id(session_id.value());

    std::optional<user_data_auth::CreatePersistentUserReply> create_reply =
        CreatePersistentUserSync(create_request);
    if (!create_reply.has_value()) {
      LOG(ERROR) << "Call to CreatePersistentUser() did not complete in "
                    "CreateTestUser().";
      return false;
    }
    if (create_reply->error_info().primary_action() !=
        user_data_auth::PrimaryAction::PRIMARY_NO_ERROR) {
      LOG(ERROR)
          << "Call to CreatePersistentUser() failed in CreateTestUser(): "
          << GetProtoDebugString(create_reply.value());
      return false;
    }
    EXPECT_THAT(create_reply->auth_properties().authorized_for(),
                UnorderedElementsAre(user_data_auth::AUTH_INTENT_DECRYPT,
                                     user_data_auth::AUTH_INTENT_VERIFY_ONLY));

    // Add the password auth factor.
    user_data_auth::AddAuthFactorRequest add_factor_request;
    add_factor_request.set_auth_session_id(session_id.value());
    add_factor_request.mutable_auth_factor()->set_type(
        user_data_auth::AuthFactorType::AUTH_FACTOR_TYPE_PASSWORD);
    add_factor_request.mutable_auth_factor()->set_label(kPasswordLabel);
    add_factor_request.mutable_auth_factor()->mutable_password_metadata();
    add_factor_request.mutable_auth_input()
        ->mutable_password_input()
        ->set_secret(kPassword1);

    bool signal_sent = false;
    user_data_auth::AuthFactorAdded signal_proto;
    EXPECT_CALL(signalling_, SendAuthFactorAdded(_))
        .Times(AtMost(1))
        .WillOnce([&](auto&& signal) {
          signal_sent = true;
          signal_proto = std::move(signal);
        });

    std::optional<user_data_auth::AddAuthFactorReply> add_factor_reply =
        AddAuthFactorSync(add_factor_request);
    if (!add_factor_reply.has_value()) {
      LOG(ERROR)
          << "Call to AddAuthFactor() did not complete in CreateTestUser().";
      EXPECT_FALSE(signal_sent);
      ::testing::Mock::VerifyAndClearExpectations(&signalling_);
      return false;
    }
    if (add_factor_reply->error_info().primary_action() !=
        user_data_auth::PrimaryAction::PRIMARY_NO_ERROR) {
      LOG(ERROR) << "Call to AddAuthFactor() failed in CreateTestUser(): "
                 << GetProtoDebugString(add_factor_reply.value());
      EXPECT_FALSE(signal_sent);
      ::testing::Mock::VerifyAndClearExpectations(&signalling_);
      return false;
    }

    EXPECT_TRUE(signal_sent);
    EXPECT_THAT(signal_proto.auth_factor().label(), kPasswordLabel);
    EXPECT_THAT(signal_proto.auth_factor().type(),
                user_data_auth::AuthFactorType::AUTH_FACTOR_TYPE_PASSWORD);

    // Invalidate the session.
    user_data_auth::InvalidateAuthSessionRequest invalidate_request;
    invalidate_request.set_auth_session_id(session_id.value());
    std::optional<user_data_auth::InvalidateAuthSessionReply> invalidate_reply =
        InvalidateAuthSessionSync(invalidate_request);
    if (!invalidate_reply.has_value()) {
      LOG(ERROR) << "Call to InvalidateAuthSession() did not complete in "
                    "CreateTestUser().";
      return false;
    }
    if (invalidate_reply->error_info().primary_action() !=
        user_data_auth::PrimaryAction::PRIMARY_NO_ERROR) {
      LOG(ERROR)
          << "Call to InvalidateAuthSession() failed in CreateTestUser(): "
          << GetProtoDebugString(invalidate_reply.value());
      return false;
    }

    if (!::testing::Mock::VerifyAndClearExpectations(&signalling_)) {
      return false;
    }
    return true;
  }

  // Create a kiosk test user, return true if successful. This doesn't create
  // the vault.
  bool CreateKioskTestUser() {
    std::optional<std::string> session_id = GetTestUnauthedAuthSession(
        kKioskUser,
        {.is_ephemeral_user = false, .intent = AuthIntent::kDecrypt});
    if (!session_id.has_value()) {
      LOG(ERROR) << "No session ID in CreateKioskTestUser().";
      return false;
    }

    EXPECT_CALL(homedirs_, CryptohomeExists(_)).WillOnce(ReturnValue(false));
    EXPECT_CALL(homedirs_, Create(_)).WillOnce(Return(true));

    // Create the user.
    user_data_auth::CreatePersistentUserRequest create_request;
    create_request.set_auth_session_id(session_id.value());

    std::optional<user_data_auth::CreatePersistentUserReply> create_reply =
        CreatePersistentUserSync(create_request);
    if (!create_reply.has_value()) {
      LOG(ERROR) << "Call to CreatePersistentUser() did not complete in "
                    "CreateKioskTestUser().";
      return false;
    }
    if (create_reply->error_info().primary_action() !=
        user_data_auth::PrimaryAction::PRIMARY_NO_ERROR) {
      LOG(ERROR)
          << "Call to CreatePersistentUser() failed in CreateKioskTestUser(): "
          << GetProtoDebugString(create_reply.value());
      return false;
    }
    EXPECT_THAT(create_reply->auth_properties().authorized_for(),
                UnorderedElementsAre(user_data_auth::AUTH_INTENT_DECRYPT,
                                     user_data_auth::AUTH_INTENT_VERIFY_ONLY));

    // Add the kiosk auth factor.
    user_data_auth::AddAuthFactorRequest add_factor_request;
    add_factor_request.set_auth_session_id(session_id.value());
    add_factor_request.mutable_auth_factor()->set_type(
        user_data_auth::AuthFactorType::AUTH_FACTOR_TYPE_KIOSK);
    add_factor_request.mutable_auth_factor()->set_label(kKioskLabel);
    add_factor_request.mutable_auth_factor()->mutable_kiosk_metadata();
    add_factor_request.mutable_auth_input()->mutable_kiosk_input();

    bool signal_sent = false;
    user_data_auth::AuthFactorAdded signal_proto;
    EXPECT_CALL(signalling_, SendAuthFactorAdded(_))
        .WillOnce(DoAll((SaveArg<0>(&signal_proto)),
                        [&](auto&&) { signal_sent = true; }));

    std::optional<user_data_auth::AddAuthFactorReply> add_factor_reply =
        AddAuthFactorSync(add_factor_request);
    if (!add_factor_reply.has_value()) {
      LOG(ERROR) << "Call to AddAuthFactor() did not complete in "
                    "CreateKioskTestUser().";
      EXPECT_FALSE(signal_sent);
      ::testing::Mock::VerifyAndClearExpectations(&signalling_);
      return false;
    }
    if (add_factor_reply->error_info().primary_action() !=
        user_data_auth::PrimaryAction::PRIMARY_NO_ERROR) {
      LOG(ERROR) << "Call to AddAuthFactor() failed in CreateKioskTestUser(): "
                 << GetProtoDebugString(add_factor_reply.value());
      EXPECT_FALSE(signal_sent);
      ::testing::Mock::VerifyAndClearExpectations(&signalling_);
      return false;
    }

    EXPECT_TRUE(signal_sent);
    EXPECT_THAT(signal_proto.auth_factor().label(), kKioskLabel);
    EXPECT_THAT(signal_proto.auth_factor().type(),
                user_data_auth::AuthFactorType::AUTH_FACTOR_TYPE_KIOSK);

    // Invalidate the session.
    user_data_auth::InvalidateAuthSessionRequest invalidate_request;
    invalidate_request.set_auth_session_id(session_id.value());
    std::optional<user_data_auth::InvalidateAuthSessionReply> invalidate_reply =
        InvalidateAuthSessionSync(invalidate_request);
    if (!invalidate_reply.has_value()) {
      LOG(ERROR) << "Call to InvalidateAuthSession() did not complete in "
                    "CreateKioskTestUser().";
      return false;
    }
    if (invalidate_reply->error_info().primary_action() !=
        user_data_auth::PrimaryAction::PRIMARY_NO_ERROR) {
      LOG(ERROR)
          << "Call to InvalidateAuthSession() failed in CreateKioskTestUser(): "
          << GetProtoDebugString(invalidate_reply.value());
      return false;
    }

    if (!::testing::Mock::VerifyAndClearExpectations(&signalling_)) {
      return false;
    }
    return true;
  }

  // Starts an AuthSession for kiosk user and authenticates it. On
  // success returns the AuthSession ID, on failure returns nullptr.
  std::optional<std::string> GetTestAuthedAuthSessionForKiosk() {
    std::optional<std::string> session_id = GetTestUnauthedAuthSession(
        kKioskUser,
        {.is_ephemeral_user = false, .intent = AuthIntent::kDecrypt});
    if (!session_id.has_value()) {
      LOG(ERROR) << "No session ID in GetTestAuthedAuthSessionForKiosk().";
      return std::nullopt;
    }

    user_data_auth::AuthenticateAuthFactorRequest auth_request;
    auth_request.set_auth_session_id(*session_id);
    auth_request.add_auth_factor_labels(kKioskLabel);
    auth_request.mutable_auth_input()->mutable_kiosk_input();

    std::optional<user_data_auth::AuthenticateAuthFactorReply> auth_reply =
        AuthenticateAuthFactorSync(auth_request);
    if (!auth_reply.has_value()) {
      LOG(ERROR) << "Call to AuthenticateAuthFactor() did not complete in "
                    "GetTestAuthedAuthSessionForKiosk().";
      return std::nullopt;
    }
    if (auth_reply->error_info().primary_action() !=
        user_data_auth::PrimaryAction::PRIMARY_NO_ERROR) {
      LOG(ERROR) << "Call to AuthenticateAuthFactor() failed in "
                    "GetTestAuthedAuthSessionForKiosk(): "
                 << GetProtoDebugString(auth_reply.value());
      return std::nullopt;
    }

    return session_id.value();
  }

  std::optional<std::string> GetTestAuthedAuthSession(AuthIntent intent) {
    std::optional<std::string> session_id = GetTestUnauthedAuthSession(
        kUsername1, {.is_ephemeral_user = false, .intent = intent});
    if (!session_id.has_value()) {
      LOG(ERROR) << "No session ID in GetTestAuthedAuthSession().";
      return std::nullopt;
    }

    user_data_auth::AuthenticateAuthFactorRequest auth_request;
    auth_request.set_auth_session_id(session_id.value());
    auth_request.add_auth_factor_labels(kPasswordLabel);
    auth_request.mutable_auth_input()->mutable_password_input()->set_secret(
        kPassword1);

    std::optional<user_data_auth::AuthenticateAuthFactorReply> auth_reply =
        AuthenticateAuthFactorSync(auth_request);
    if (!auth_reply.has_value()) {
      LOG(ERROR) << "Call to AuthenticateAuthFactor() did not complete in "
                    "GetTestAuthedAuthSession().";
      return std::nullopt;
    }
    if (auth_reply->error_info().primary_action() !=
        user_data_auth::PrimaryAction::PRIMARY_NO_ERROR) {
      LOG(ERROR) << "Call to AuthenticateAuthFactor() failed in "
                    "GetTestAuthedAuthSession(): "
                 << GetProtoDebugString(auth_reply.value());
      return std::nullopt;
    }

    return session_id.value();
  }

  std::optional<user_data_auth::AuthenticateAuthFactorReply>
  AuthenticateAuthFactorSync(
      const user_data_auth::AuthenticateAuthFactorRequest& in_request) {
    TestFuture<user_data_auth::AuthenticateAuthFactorReply> reply_future;
    userdataauth_->AuthenticateAuthFactor(
        in_request,
        reply_future
            .GetCallback<const user_data_auth::AuthenticateAuthFactorReply&>());
    RunUntilIdle();
    return reply_future.Get();
  }

  std::optional<user_data_auth::CreatePersistentUserReply>
  CreatePersistentUserSync(
      const user_data_auth::CreatePersistentUserRequest& in_request) {
    TestFuture<user_data_auth::CreatePersistentUserReply> reply_future;
    userdataauth_->CreatePersistentUser(
        in_request,
        reply_future
            .GetCallback<const user_data_auth::CreatePersistentUserReply&>());
    RunUntilIdle();
    return reply_future.Get();
  }

  std::optional<user_data_auth::AddAuthFactorReply> AddAuthFactorSync(
      const user_data_auth::AddAuthFactorRequest& in_request) {
    TestFuture<user_data_auth::AddAuthFactorReply> reply_future;
    userdataauth_->AddAuthFactor(
        in_request,
        reply_future.GetCallback<const user_data_auth::AddAuthFactorReply&>());
    RunUntilIdle();
    return reply_future.Get();
  }

  std::optional<user_data_auth::InvalidateAuthSessionReply>
  InvalidateAuthSessionSync(
      const user_data_auth::InvalidateAuthSessionRequest& in_request) {
    TestFuture<user_data_auth::InvalidateAuthSessionReply> reply_future;
    userdataauth_->InvalidateAuthSession(
        in_request,
        reply_future
            .GetCallback<const user_data_auth::InvalidateAuthSessionReply&>());
    RunUntilIdle();
    return reply_future.Get();
  }

  std::optional<user_data_auth::PreparePersistentVaultReply>
  PreparePersistentVaultSync(
      const user_data_auth::PreparePersistentVaultRequest& in_request) {
    TestFuture<user_data_auth::PreparePersistentVaultReply> reply_future;
    userdataauth_->PreparePersistentVault(
        in_request,
        reply_future
            .GetCallback<const user_data_auth::PreparePersistentVaultReply&>());
    RunUntilIdle();
    return reply_future.Get();
  }

  std::optional<user_data_auth::ModifyAuthFactorIntentsReply>
  ModifyAuthFactorIntentsSync(
      const user_data_auth::ModifyAuthFactorIntentsRequest& in_request) {
    TestFuture<user_data_auth::ModifyAuthFactorIntentsReply> reply_future;
    userdataauth_->ModifyAuthFactorIntents(
        in_request, reply_future.GetCallback<
                        const user_data_auth::ModifyAuthFactorIntentsReply&>());
    RunUntilIdle();
    return reply_future.Get();
  }

  std::optional<user_data_auth::PrepareEphemeralVaultReply>
  PrepareEphemeralVaultSync(
      const user_data_auth::PrepareEphemeralVaultRequest& in_request) {
    TestFuture<user_data_auth::PrepareEphemeralVaultReply> reply_future;
    userdataauth_->PrepareEphemeralVault(
        in_request,
        reply_future
            .GetCallback<const user_data_auth::PrepareEphemeralVaultReply&>());
    RunUntilIdle();
    return reply_future.Get();
  }

  std::optional<user_data_auth::PrepareGuestVaultReply> PrepareGuestVaultSync(
      const user_data_auth::PrepareGuestVaultRequest& in_request) {
    TestFuture<user_data_auth::PrepareGuestVaultReply> reply_future;
    userdataauth_->PrepareGuestVault(
        in_request,
        reply_future
            .GetCallback<const user_data_auth::PrepareGuestVaultReply&>());
    RunUntilIdle();
    return reply_future.Get();
  }

  std::optional<user_data_auth::MigrateLegacyFingerprintsReply>
  MigrateLegacyFingerprintsSync(
      const user_data_auth::MigrateLegacyFingerprintsRequest& in_request) {
    TestFuture<user_data_auth::MigrateLegacyFingerprintsReply> reply_future;
    userdataauth_->MigrateLegacyFingerprints(
        in_request,
        reply_future.GetCallback<
            const user_data_auth::MigrateLegacyFingerprintsReply&>());
    RunUntilIdle();
    return reply_future.Get();
  }

  std::optional<user_data_auth::AuthenticateAuthFactorReply>
  AuthenticatePasswordAuthFactor(const std::string& auth_session_id,
                                 const std::string& label,
                                 const std::string& password) {
    user_data_auth::AuthenticateAuthFactorRequest auth_request;
    auth_request.set_auth_session_id(auth_session_id);
    auth_request.add_auth_factor_labels(label);
    auth_request.mutable_auth_input()->mutable_password_input()->set_secret(
        password);
    return AuthenticateAuthFactorSync(auth_request);
  }

  std::optional<user_data_auth::AuthenticateAuthFactorReply>
  AuthenticatePinAuthFactor(const std::string& auth_session_id,
                            const std::string& label,
                            const std::string& pin) {
    user_data_auth::AuthenticateAuthFactorRequest auth_request;
    auth_request.set_auth_session_id(auth_session_id);
    auth_request.add_auth_factor_labels(label);
    auth_request.mutable_auth_input()->mutable_pin_input()->set_secret(pin);
    return AuthenticateAuthFactorSync(auth_request);
  }

 protected:
  // Mock mount factory for mocking Mount objects.
  MockMountFactory mount_factory_;
  // Any elements added to this queue will be returned when mount_factory_.New()
  // is called.
  std::deque<Mount*> new_mounts_;

  const Username kUsername1{"foo@gmail.com"};
  const Username kUsername2{"bar@gmail.com"};
  const Username kKioskUser{"kiosk"};
  static constexpr char kPassword1[] = "MyP@ssW0rd!!";
  static constexpr char kPasswordLabel[] = "Password1";
  static constexpr char kKioskLabel[] = "Kiosk";
  static constexpr char kSmartCardLabel[] = "SmartCard1";
  static constexpr char kTestErrorString[] = "ErrorForTestingOnly";

  hwsec::Tpm2SimulatorFactoryForTest sim_factory_;
  std::unique_ptr<const hwsec::CryptohomeFrontend> sim_hwsec_;
  std::unique_ptr<const hwsec::PinWeaverManagerFrontend> sim_hwsec_pw_manager_;
  std::unique_ptr<const hwsec::RecoveryCryptoFrontend> sim_recovery_crypto_;
  std::unique_ptr<CryptohomeKeysManager> sim_keys_manager_;
  std::unique_ptr<Crypto> sim_crypto_;

  // Mock to use to capture any signals sent.
  NiceMock<MockSignalling> signalling_;
};

TEST_F(UserDataAuthApiTest, LockRecoverySuccessFileExists) {
  user_data_auth::LockFactorUntilRebootRequest req;
  req.set_auth_factor_type(
      user_data_auth::AUTH_FACTOR_TYPE_CRYPTOHOME_RECOVERY);
  TestFuture<user_data_auth::LockFactorUntilRebootReply> reply;

  EXPECT_CALL(system_apis_.platform, FileExists(GetRecoveryFactorLockPath()))
      .WillOnce(Return(true));

  userdataauth_->LockFactorUntilReboot(
      req,
      reply.GetCallback<const user_data_auth::LockFactorUntilRebootReply&>());
  EXPECT_THAT(reply.Get().has_error_info(), IsFalse());
}

TEST_F(UserDataAuthApiTest, LockRecoverySuccessCreate) {
  user_data_auth::LockFactorUntilRebootRequest req;
  req.set_auth_factor_type(
      user_data_auth::AUTH_FACTOR_TYPE_CRYPTOHOME_RECOVERY);
  TestFuture<user_data_auth::LockFactorUntilRebootReply> reply;

  EXPECT_CALL(system_apis_.platform, FileExists(GetRecoveryFactorLockPath()))
      .WillOnce(Return(false));
  EXPECT_CALL(system_apis_.platform,
              TouchFileDurable(GetRecoveryFactorLockPath()))
      .WillOnce(Return(true));

  userdataauth_->LockFactorUntilReboot(
      req,
      reply.GetCallback<const user_data_auth::LockFactorUntilRebootReply&>());
  EXPECT_THAT(reply.Get().has_error_info(), IsFalse());
}

TEST_F(UserDataAuthApiTest, LockRecoveryFails) {
  user_data_auth::LockFactorUntilRebootRequest req;
  req.set_auth_factor_type(
      user_data_auth::AUTH_FACTOR_TYPE_CRYPTOHOME_RECOVERY);
  TestFuture<user_data_auth::LockFactorUntilRebootReply> reply;

  EXPECT_CALL(system_apis_.platform, FileExists(GetRecoveryFactorLockPath()))
      .WillOnce(Return(false));
  EXPECT_CALL(system_apis_.platform,
              TouchFileDurable(GetRecoveryFactorLockPath()))
      .WillOnce(Return(false));

  userdataauth_->LockFactorUntilReboot(
      req,
      reply.GetCallback<const user_data_auth::LockFactorUntilRebootReply&>());
  EXPECT_THAT(reply.Get().error_info(),
              HasPossibleActions(PossibleActionSet(
                  {user_data_auth::PossibleAction::POSSIBLY_RETRY,
                   user_data_auth::PossibleAction::POSSIBLY_REBOOT})));
}

TEST_F(UserDataAuthApiTest, LockWrongTypeFails) {
  user_data_auth::LockFactorUntilRebootRequest req;
  req.set_auth_factor_type(user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
  TestFuture<user_data_auth::LockFactorUntilRebootReply> reply;

  userdataauth_->LockFactorUntilReboot(
      req,
      reply.GetCallback<const user_data_auth::LockFactorUntilRebootReply&>());
  EXPECT_THAT(reply.Get().error_info(),
              HasPossibleActions(PossibleActionSet(
                  {user_data_auth::PossibleAction::
                       POSSIBLY_DEV_CHECK_UNEXPECTED_STATE})));
}

TEST_F(UserDataAuthApiTest, RemoveStillMounted) {
  // If a home directory is mounted it'll return false for Remove().
  EXPECT_CALL(homedirs_, Remove(_)).WillOnce(Return(false));

  std::optional<std::string> session_id = GetTestUnauthedAuthSession(
      kUsername1, {.is_ephemeral_user = false, .intent = AuthIntent::kDecrypt});
  ASSERT_TRUE(session_id.has_value());

  user_data_auth::RemoveRequest req;
  req.set_auth_session_id(session_id.value());

  TestFuture<user_data_auth::RemoveReply> remove_reply_future;
  userdataauth_->Remove(
      req,
      remove_reply_future.GetCallback<const user_data_auth::RemoveReply&>());
  RunUntilIdle();

  // Failure to Remove() due to still mounted vault should result in Reboot and
  // Powerwash recommendation.
  EXPECT_THAT(remove_reply_future.Get().error_info(),
              HasPossibleActions(PossibleActionSet(
                  {user_data_auth::PossibleAction::POSSIBLY_REBOOT,
                   user_data_auth::PossibleAction::POSSIBLY_POWERWASH})));
}

TEST_F(UserDataAuthApiTest, RemoveNoID) {
  user_data_auth::RemoveRequest req;

  TestFuture<user_data_auth::RemoveReply> remove_reply_future;
  userdataauth_->Remove(
      req,
      remove_reply_future.GetCallback<const user_data_auth::RemoveReply&>());

  // Failure to Remove() due to the lack of username in the request is
  // unexpected, and should result in POSSIBLY_DEV_CHECK_UNEXPECTED_STATE.
  EXPECT_THAT(
      remove_reply_future.Get().error_info(),
      HasPossibleAction(
          user_data_auth::PossibleAction::POSSIBLY_DEV_CHECK_UNEXPECTED_STATE));
}

TEST_F(UserDataAuthApiTest, AuthAuthFactorNoSession) {
  user_data_auth::AuthenticateAuthFactorRequest req;
  req.set_auth_session_id("NOT_A_VALID_AUTH_SESSION!");
  user_data_auth::AuthenticateAuthFactorReply reply;

  std::optional<user_data_auth::AuthenticateAuthFactorReply> result =
      AuthenticateAuthFactorSync(req);
  ASSERT_TRUE(result.has_value());
  reply = *result;

  // Failure to AuthenticateAuthFactor() due to missing session should result in
  // recommendation to reboot, because we'll need to restart the session after
  // reboot so the problem might go away.
  EXPECT_THAT(
      reply.error_info(),
      HasPossibleAction(user_data_auth::PossibleAction::POSSIBLY_REBOOT));
}

TEST_F(UserDataAuthApiTest, ChalCredBadSRKROCA) {
  ASSERT_TRUE(CreateTestUser());
  std::optional<std::string> session_id =
      GetTestAuthedAuthSession(AuthIntent::kDecrypt);
  ASSERT_TRUE(session_id.has_value());

  ON_CALL(sim_factory_.GetMockBackend().GetMock().vendor, IsSrkRocaVulnerable)
      .WillByDefault(ReturnValue(true));

  ON_CALL(key_challenge_service_factory_, New(_))
      .WillByDefault(
          Return(ByMove(std::make_unique<MockKeyChallengeService>())));

  user_data_auth::AddAuthFactorRequest add_factor_request;
  add_factor_request.set_auth_session_id(session_id.value());
  add_factor_request.mutable_auth_factor()->set_type(
      user_data_auth::AuthFactorType::AUTH_FACTOR_TYPE_SMART_CARD);
  add_factor_request.mutable_auth_factor()->set_label(kSmartCardLabel);
  add_factor_request.mutable_auth_factor()
      ->mutable_smart_card_metadata()
      ->set_public_key_spki_der("test_pubkey_spki_der");
  add_factor_request.mutable_auth_input()
      ->mutable_smart_card_input()
      ->add_signature_algorithms(user_data_auth::SmartCardSignatureAlgorithm::
                                     CHALLENGE_RSASSA_PKCS1_V1_5_SHA256);
  add_factor_request.mutable_auth_input()
      ->mutable_smart_card_input()
      ->set_key_delegate_dbus_service_name("test_challenge_dbus");

  std::optional<user_data_auth::AddAuthFactorReply> add_factor_reply =
      AddAuthFactorSync(add_factor_request);
  ASSERT_TRUE(add_factor_reply.has_value());
  EXPECT_EQ(add_factor_reply->error_info().primary_action(),
            user_data_auth::PrimaryAction::PRIMARY_TPM_UDPATE_REQUIRED);
  EXPECT_FALSE(add_factor_reply->has_added_auth_factor());
}

TEST_F(UserDataAuthApiTest, MountFailed) {
  // Prepare an account.
  ASSERT_TRUE(CreateTestUser());
  std::optional<std::string> session_id =
      GetTestAuthedAuthSession(AuthIntent::kDecrypt);
  ASSERT_TRUE(session_id.has_value());

  // Ensure that the mount fails.
  scoped_refptr<MockMount> mount = new MockMount();
  EXPECT_CALL(*mount, MountCryptohome(_, _, _))
      .WillOnce(ReturnError<StorageError>(FROM_HERE, kTestErrorString,
                                          MOUNT_ERROR_FATAL, false));
  new_mounts_.push_back(mount.get());

  EXPECT_CALL(homedirs_, Exists(_)).WillOnce(Return(true));
  EXPECT_CALL(disk_cleanup_, FreeDiskSpaceDuringLogin(_))
      .WillRepeatedly(Return(true));

  // Make the call to check that the result is correct.
  user_data_auth::PreparePersistentVaultRequest prepare_req;
  prepare_req.set_auth_session_id(session_id.value());
  std::optional<user_data_auth::PreparePersistentVaultReply> prepare_reply =
      PreparePersistentVaultSync(prepare_req);

  ASSERT_TRUE(prepare_reply.has_value());
  EXPECT_THAT(prepare_reply->error_info(),
              HasPossibleActions(PossibleActionSet(
                  {user_data_auth::PossibleAction::POSSIBLY_RETRY,
                   user_data_auth::PossibleAction::POSSIBLY_REBOOT,
                   user_data_auth::PossibleAction::POSSIBLY_DELETE_VAULT,
                   user_data_auth::PossibleAction::POSSIBLY_POWERWASH})));
}

TEST_F(UserDataAuthApiTest, MountKioskFailsIfExistingUserSession) {
  // 1 - Create the user and kiosk account.
  ASSERT_TRUE(CreateTestUser());
  ASSERT_TRUE(CreateKioskTestUser());

  // 2 - Setup the regular-user session.

  std::optional<std::string> session_id =
      GetTestAuthedAuthSession(AuthIntent::kDecrypt);
  ASSERT_TRUE(session_id.has_value());

  SetupMount(*kUsername1);

  scoped_refptr<MockMount> mount = new MockMount();
  new_mounts_.push_back(mount.get());

  EXPECT_CALL(homedirs_, Exists(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(disk_cleanup_, FreeDiskSpaceDuringLogin(_))
      .WillRepeatedly(Return(true));

  user_data_auth::PreparePersistentVaultRequest prepare_req;
  prepare_req.set_auth_session_id(session_id.value());
  std::optional<user_data_auth::PreparePersistentVaultReply> prepare_reply =
      PreparePersistentVaultSync(prepare_req);
  ASSERT_TRUE(prepare_reply.has_value());
  ASSERT_EQ(prepare_reply->error_info().primary_action(),
            user_data_auth::PrimaryAction::PRIMARY_NO_ERROR);

  // 3 - Attempt kiosk mount when the user cryptohome is still mounted.

  session_id = GetTestAuthedAuthSessionForKiosk();
  ASSERT_TRUE(session_id.has_value());

  // User mount is still active; mounting kiosk session should fail.
  // Check the posisble actions on error.
  EXPECT_CALL(*session_, IsActive()).WillOnce(Return(true));
  prepare_req.set_auth_session_id(session_id.value());
  prepare_reply = PreparePersistentVaultSync(prepare_req);
  ASSERT_TRUE(prepare_reply.has_value());
  EXPECT_THAT(
      prepare_reply->error_info(),
      HasPossibleActions(PossibleActionSet(
          {user_data_auth::PossibleAction::POSSIBLY_DEV_CHECK_UNEXPECTED_STATE,
           user_data_auth::PossibleAction::POSSIBLY_REBOOT})));
}

TEST_F(UserDataAuthApiTest, MountFailsIfExistingKioskSession) {
  // 1 - Create the user and kiosk account.
  ASSERT_TRUE(CreateTestUser());
  ASSERT_TRUE(CreateKioskTestUser());

  // 2 - Setup the kiosk session.

  std::optional<std::string> session_id = GetTestAuthedAuthSessionForKiosk();
  ASSERT_TRUE(session_id.has_value());

  SetupMount(*kKioskUser);

  scoped_refptr<MockMount> mount = new MockMount();
  new_mounts_.push_back(mount.get());

  EXPECT_CALL(homedirs_, Exists(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(disk_cleanup_, FreeDiskSpaceDuringLogin(_))
      .WillRepeatedly(Return(true));

  user_data_auth::PreparePersistentVaultRequest prepare_req;
  prepare_req.set_auth_session_id(session_id.value());
  std::optional<user_data_auth::PreparePersistentVaultReply> prepare_reply =
      PreparePersistentVaultSync(prepare_req);
  ASSERT_TRUE(prepare_reply.has_value());
  ASSERT_EQ(prepare_reply->error_info().primary_action(),
            user_data_auth::PrimaryAction::PRIMARY_NO_ERROR);

  // 3 - Attempt user mount when the kiosk cryptohome is still mounted.

  session_id = GetTestAuthedAuthSession(AuthIntent::kDecrypt);
  ASSERT_TRUE(session_id.has_value());

  // Kiosk mount is still active; mounting a session should fail.
  // Check the posisble actions on error.
  EXPECT_CALL(*session_, IsActive()).WillOnce(Return(true));
  prepare_req.set_auth_session_id(session_id.value());
  prepare_reply = PreparePersistentVaultSync(prepare_req);
  ASSERT_TRUE(prepare_reply.has_value());
  EXPECT_THAT(
      prepare_reply->error_info(),
      HasPossibleActions(PossibleActionSet(
          {user_data_auth::PossibleAction::POSSIBLY_DEV_CHECK_UNEXPECTED_STATE,
           user_data_auth::PossibleAction::POSSIBLY_REBOOT})));
}

TEST_F(UserDataAuthApiTest, GuestMountFailed) {
  // Ensure that the guest mount fails.
  scoped_refptr<MockMount> mount = new MockMount();
  EXPECT_CALL(*mount, MountEphemeralCryptohome(_))
      .WillOnce(ReturnError<StorageError>(FROM_HERE, kTestErrorString,
                                          MOUNT_ERROR_FATAL, false));
  new_mounts_.push_back(mount.get());

  // Make the call to check that it failed correctly.
  user_data_auth::PrepareGuestVaultRequest prepare_req;
  std::optional<user_data_auth::PrepareGuestVaultReply> prepare_reply =
      PrepareGuestVaultSync(prepare_req);
  ASSERT_TRUE(prepare_reply.has_value());
  EXPECT_THAT(prepare_reply->error_info(),
              HasPossibleActions(PossibleActionSet(
                  {user_data_auth::PossibleAction::POSSIBLY_RETRY,
                   user_data_auth::PossibleAction::POSSIBLY_REBOOT,
                   user_data_auth::PossibleAction::POSSIBLY_POWERWASH})));
}

TEST_F(UserDataAuthApiTest, EphemeralMountFailed) {
  // Prepare an auth session for ephemeral mount.
  std::optional<std::string> session_id = GetTestUnauthedAuthSession(
      kUsername1, {.is_ephemeral_user = true, .intent = AuthIntent::kDecrypt});
  ASSERT_TRUE(session_id.has_value());

  // Ensure that the mount fails.
  scoped_refptr<MockMount> mount = new MockMount();
  EXPECT_CALL(*mount, MountEphemeralCryptohome(_))
      .WillOnce(ReturnError<StorageError>(FROM_HERE, kTestErrorString,
                                          MOUNT_ERROR_FATAL, false));
  new_mounts_.push_back(mount.get());
  EXPECT_CALL(homedirs_, GetOwner(_))
      .WillRepeatedly(
          DoAll(SetArgPointee<0>(SanitizeUserName(kUsername2)), Return(true)));

  // Make the call to check that the result is correct.
  user_data_auth::PrepareEphemeralVaultRequest prepare_req;
  prepare_req.set_auth_session_id(session_id.value());
  std::optional<user_data_auth::PrepareEphemeralVaultReply> prepare_reply =
      PrepareEphemeralVaultSync(prepare_req);

  ASSERT_TRUE(prepare_reply.has_value());
  EXPECT_THAT(prepare_reply->error_info(),
              HasPossibleActions(PossibleActionSet(
                  {user_data_auth::PossibleAction::POSSIBLY_RETRY,
                   user_data_auth::PossibleAction::POSSIBLY_REBOOT,
                   user_data_auth::PossibleAction::POSSIBLY_POWERWASH})));
  EXPECT_THAT(prepare_reply->auth_properties().authorized_for(), IsEmpty());
}

// This is designed to trigger the unrecoverable vault flow.
TEST_F(UserDataAuthApiTest, VaultWithoutAuth) {
  // Mock that the user exists.
  base::FilePath upath = UserPath(SanitizeUserName(kUsername1));
  EXPECT_CALL(system_apis_.platform, DirectoryExists(upath))
      .WillOnce(Return(true));

  // Call StartAuthSession and it should fail.
  user_data_auth::StartAuthSessionRequest req;
  req.mutable_account_id()->set_account_id(*kUsername1);
  req.set_intent(user_data_auth::AuthIntent::AUTH_INTENT_DECRYPT);
  std::optional<user_data_auth::StartAuthSessionReply> reply =
      StartAuthSessionSync(req);
  ASSERT_TRUE(reply.has_value());

  EXPECT_THAT(
      reply->error_info(),
      HasPossibleAction(user_data_auth::PossibleAction::POSSIBLY_DELETE_VAULT));
}

// This is designed to trigger FailureReason::COULD_NOT_MOUNT_CRYPTOHOME on
// Chromium side for AuthenticateAuthFactor().
TEST_F(UserDataAuthApiTest, AuthAuthFactorWithoutLabel) {
  // Prepare an account.
  ASSERT_TRUE(CreateTestUser());

  // Call AuthenticateAuthFactor with an empty label.
  std::optional<std::string> session_id = GetTestUnauthedAuthSession(
      kUsername1, {.is_ephemeral_user = false, .intent = AuthIntent::kDecrypt});
  ASSERT_TRUE(session_id.has_value());

  user_data_auth::AuthenticateAuthFactorRequest auth_request;
  auth_request.set_auth_session_id(session_id.value());
  auth_request.mutable_auth_input()->mutable_password_input()->set_secret(
      kPassword1);

  std::optional<user_data_auth::AuthenticateAuthFactorReply> auth_reply =
      AuthenticateAuthFactorSync(auth_request);

  // Should result is POSSIBLY_DEV_CHECK_UNEXPECTED_STATE.
  ASSERT_TRUE(auth_reply.has_value());
  EXPECT_THAT(
      auth_reply->error_info(),
      HasPossibleAction(
          user_data_auth::PossibleAction::POSSIBLY_DEV_CHECK_UNEXPECTED_STATE));
}

// This is designed to trigger FailureReason::COULD_NOT_MOUNT_CRYPTOHOME on
// Chromium side for CreatePersistentUserAlreadyExist().
TEST_F(UserDataAuthApiTest, CreatePeristentUserAlreadyExist) {
  // Setup auth session.
  std::optional<std::string> session_id = GetTestUnauthedAuthSession(
      kUsername1, {.is_ephemeral_user = false, .intent = AuthIntent::kDecrypt});
  ASSERT_TRUE(session_id.has_value());

  // Call CreatePersistentUser() while the user already exists.
  EXPECT_CALL(homedirs_, CryptohomeExists(_)).WillOnce(ReturnValue(true));
  user_data_auth::CreatePersistentUserRequest create_request;
  create_request.set_auth_session_id(session_id.value());

  std::optional<user_data_auth::CreatePersistentUserReply> create_reply =
      CreatePersistentUserSync(create_request);
  ASSERT_TRUE(create_reply.has_value());
  EXPECT_THAT(
      create_reply->error_info(),
      HasPossibleActions(PossibleActionSet(
          {user_data_auth::PossibleAction::POSSIBLY_DEV_CHECK_UNEXPECTED_STATE,
           user_data_auth::PossibleAction::POSSIBLY_DELETE_VAULT})));
  EXPECT_THAT(create_reply->auth_properties().authorized_for(), IsEmpty());
}

// This is designed to check if modifying auth factor intents results in
// enabling/disabling a configurable intent. And also that non-configurable
// intents are not configured.
TEST_F(UserDataAuthApiTest, ModifyAuthFactorIntents) {
  // Setup auth session.
  auto mock_processor =
      std::make_unique<NiceMock<MockBiometricsCommandProcessor>>();
  MockBiometricsCommandProcessor* bio_command_processor_ = mock_processor.get();
  EXPECT_CALL(*bio_command_processor_, SetEnrollScanDoneCallback(_));
  EXPECT_CALL(*bio_command_processor_, SetAuthScanDoneCallback(_));
  EXPECT_CALL(*bio_command_processor_, SetSessionFailedCallback(_));
  bio_service_ = std::make_unique<BiometricsAuthBlockService>(
      std::move(mock_processor),
      /*enroll_signal_sender=*/base::DoNothing(),
      /*auth_signal_sender=*/base::DoNothing());
  userdataauth_->set_biometrics_service(bio_service_.get());
  userdataauth_->set_fingerprint_manager(&fingerprint_manager_);
  ASSERT_TRUE(CreateTestUser());
  std::optional<std::string> session_id =
      GetTestAuthedAuthSession(AuthIntent::kDecrypt);
  ASSERT_TRUE(session_id.has_value());

  // CreateModifyAuthFactorIntentRequest
  user_data_auth::ModifyAuthFactorIntentsRequest modify_req;
  modify_req.set_auth_session_id(session_id.value());
  modify_req.set_type(
      user_data_auth::AuthFactorType::AUTH_FACTOR_TYPE_FINGERPRINT);
  // This would both test that Decrypt is enabled and that webauthn is not
  // disabled.
  modify_req.add_intents(user_data_auth::AuthIntent::AUTH_INTENT_DECRYPT);
  modify_req.add_intents(user_data_auth::AuthIntent::AUTH_INTENT_VERIFY_ONLY);
  EXPECT_CALL(*bio_command_processor_, IsReady()).WillOnce(Return(true));
  std::optional<user_data_auth::ModifyAuthFactorIntentsReply> modify_reply =
      ModifyAuthFactorIntentsSync(modify_req);
  EXPECT_TRUE(modify_reply.has_value());
  EXPECT_THAT(modify_reply->auth_intents().type(),
              user_data_auth::AuthFactorType::AUTH_FACTOR_TYPE_FINGERPRINT);
  EXPECT_THAT(modify_reply->auth_intents().current(),
              UnorderedElementsAre(user_data_auth::AUTH_INTENT_VERIFY_ONLY,
                                   user_data_auth::AUTH_INTENT_DECRYPT,
                                   user_data_auth::AUTH_INTENT_WEBAUTHN));
  EXPECT_THAT(modify_reply->auth_intents().minimum(),
              UnorderedElementsAre(user_data_auth::AUTH_INTENT_WEBAUTHN));
  EXPECT_THAT(modify_reply->auth_intents().maximum(),
              UnorderedElementsAre(user_data_auth::AUTH_INTENT_VERIFY_ONLY,
                                   user_data_auth::AUTH_INTENT_DECRYPT,
                                   user_data_auth::AUTH_INTENT_WEBAUTHN));
}

// This is designed to trigger FailureReason::COULD_NOT_MOUNT_CRYPTOHOME on
// Chromium side for PreparePersistentVault().
TEST_F(UserDataAuthApiTest, PreparePersistentVaultWithoutUser) {
  // Prepare an account.
  ASSERT_TRUE(CreateTestUser());
  std::optional<std::string> session_id =
      GetTestAuthedAuthSession(AuthIntent::kDecrypt);
  ASSERT_TRUE(session_id.has_value());

  // Vault doesn't exist.
  EXPECT_CALL(homedirs_, Exists(_)).WillOnce(Return(false));

  // Make the call to check that the result is correct.
  user_data_auth::PreparePersistentVaultRequest prepare_req;
  prepare_req.set_auth_session_id(session_id.value());
  std::optional<user_data_auth::PreparePersistentVaultReply> prepare_reply =
      PreparePersistentVaultSync(prepare_req);

  ASSERT_TRUE(prepare_reply.has_value());
  EXPECT_THAT(
      prepare_reply->error_info(),
      HasPossibleActions(PossibleActionSet(
          {user_data_auth::PossibleAction::POSSIBLY_DEV_CHECK_UNEXPECTED_STATE,
           user_data_auth::PossibleAction::POSSIBLY_REBOOT,
           user_data_auth::PossibleAction::POSSIBLY_DELETE_VAULT,
           user_data_auth::PossibleAction::POSSIBLY_POWERWASH})));
}

// This is designed to trigger FailureReason::COULD_NOT_MOUNT_CRYPTOHOME on
// Chromium side for PrepareEphemeralVault().
TEST_F(UserDataAuthApiTest, EphemeralMountWithRegularSession) {
  // Prepare an auth session for ephemeral mount, note that we intentionally
  // does not specify it as ephemeral.
  std::optional<std::string> session_id = GetTestUnauthedAuthSession(
      kUsername1, {.is_ephemeral_user = false, .intent = AuthIntent::kDecrypt});
  ASSERT_TRUE(session_id.has_value());

  // Make the call to check that it fails due to the session not being
  // ephemeral.
  user_data_auth::PrepareEphemeralVaultRequest prepare_req;
  prepare_req.set_auth_session_id(session_id.value());
  std::optional<user_data_auth::PrepareEphemeralVaultReply> prepare_reply =
      PrepareEphemeralVaultSync(prepare_req);

  ASSERT_TRUE(prepare_reply.has_value());
  EXPECT_THAT(
      prepare_reply->error_info(),
      HasPossibleActions(PossibleActionSet(
          {user_data_auth::PossibleAction::POSSIBLY_DEV_CHECK_UNEXPECTED_STATE,
           user_data_auth::PossibleAction::POSSIBLY_REBOOT,
           user_data_auth::PossibleAction::POSSIBLY_POWERWASH})));
  EXPECT_THAT(prepare_reply->auth_properties().authorized_for(), IsEmpty());
}

// This is designed to trigger FailureReason::COULD_NOT_MOUNT_CRYPTOHOME on
// Chromium side for PrepareGuestVault().
TEST_F(UserDataAuthApiTest, MountGuestWithOtherMounts) {
  // Create test user and mount the vault.
  ASSERT_TRUE(CreateTestUser());
  std::optional<std::string> session_id =
      GetTestAuthedAuthSession(AuthIntent::kDecrypt);
  ASSERT_TRUE(session_id.has_value());

  // Setup the mount.
  scoped_refptr<MockMount> mount = new MockMount();
  EXPECT_CALL(*mount, MountCryptohome(_, _, _))
      .WillOnce(ReturnOk<StorageError>());
  new_mounts_.push_back(mount.get());

  EXPECT_CALL(homedirs_, Exists(_)).WillOnce(Return(true));
  EXPECT_CALL(disk_cleanup_, FreeDiskSpaceDuringLogin(_))
      .WillRepeatedly(Return(true));

  user_data_auth::PreparePersistentVaultRequest prepare_req;
  prepare_req.set_auth_session_id(session_id.value());
  std::optional<user_data_auth::PreparePersistentVaultReply> prepare_reply =
      PreparePersistentVaultSync(prepare_req);
  ASSERT_TRUE(prepare_reply.has_value());
  ASSERT_EQ(prepare_reply->error_info().primary_action(),
            user_data_auth::PrimaryAction::PRIMARY_NO_ERROR);

  // Try to mount the guest vault and it should fail.
  user_data_auth::PrepareGuestVaultRequest guest_req;
  std::optional<user_data_auth::PrepareGuestVaultReply> guest_reply =
      PrepareGuestVaultSync(guest_req);
  ASSERT_TRUE(guest_reply.has_value());
  EXPECT_THAT(guest_reply->error_info(),
              HasPossibleActions(PossibleActionSet(
                  {user_data_auth::PossibleAction::POSSIBLY_REBOOT})));
}

TEST_F(UserDataAuthApiTest, MigrateLegacyFingerprintsEmptyListSucceeds) {
  // Prepare an account.
  ASSERT_TRUE(CreateTestUser());
  // Set up mount.
  SetupMount(*kUsername1);
  ON_CALL(*session_, IsActive()).WillByDefault(Return(true));
  std::vector<LegacyRecord> empty_list;
  ON_CALL(*bio_processor_, ListLegacyRecords(_))
      .WillByDefault([empty_list](auto&& callback) {
        std::move(callback).Run(empty_list);
      });

  std::optional<std::string> auth_session_id =
      GetTestAuthedAuthSession(AuthIntent::kDecrypt);
  ASSERT_TRUE(auth_session_id.has_value());

  // Check that MigrateLegacyFingerprints succeeds when there is no legacy fp to
  // be migrated.
  user_data_auth::MigrateLegacyFingerprintsRequest req;
  req.set_auth_session_id(auth_session_id.value());
  std::optional<user_data_auth::MigrateLegacyFingerprintsReply> reply =
      MigrateLegacyFingerprintsSync(req);
  ASSERT_TRUE(reply.has_value());
  ASSERT_EQ(reply->error(), user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
}

TEST_F(UserDataAuthApiTest, MigrateLegacyFingerprintsNoActiveUserSession) {
  // Prepare an account.
  ASSERT_TRUE(CreateTestUser());
  // Set up mount.
  SetupMount(*kUsername1);
  ON_CALL(*session_, IsActive()).WillByDefault(Return(false));

  std::optional<std::string> auth_session_id =
      GetTestAuthedAuthSession(AuthIntent::kDecrypt);
  ASSERT_TRUE(auth_session_id.has_value());

  // Check that MigrateLegacyFingerprints succeeds when there is no legacy fp to
  // be migrated.
  user_data_auth::MigrateLegacyFingerprintsRequest req;
  req.set_auth_session_id(auth_session_id.value());
  std::optional<user_data_auth::MigrateLegacyFingerprintsReply> reply =
      MigrateLegacyFingerprintsSync(req);
  ASSERT_TRUE(reply.has_value());
  ASSERT_EQ(reply->error(), user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
}

TEST_F(UserDataAuthApiTest, ResetLECredentialsSuccess) {
  constexpr char kPinLabel[] = "pin-label";
  constexpr char kPin[] = "123456";
  constexpr char kWrongPin[] = "111111";
  // Prepare an account.
  ASSERT_TRUE(CreateTestUser());
  std::optional<std::string> session_id =
      GetTestAuthedAuthSession(AuthIntent::kDecrypt);
  ASSERT_TRUE(session_id.has_value());

  // Add the PIN auth factor.
  user_data_auth::AddAuthFactorRequest add_factor_request;
  add_factor_request.set_auth_session_id(session_id.value());
  add_factor_request.mutable_auth_factor()->set_type(
      user_data_auth::AuthFactorType::AUTH_FACTOR_TYPE_PIN);
  add_factor_request.mutable_auth_factor()->set_label(kPinLabel);
  add_factor_request.mutable_auth_factor()->mutable_pin_metadata();
  add_factor_request.mutable_auth_input()->mutable_pin_input()->set_secret(
      kPin);
  std::optional<user_data_auth::AddAuthFactorReply> add_factor_reply =
      AddAuthFactorSync(add_factor_request);
  ASSERT_TRUE(add_factor_reply.has_value());
  ASSERT_EQ(add_factor_reply->error(),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);

  // Lock out PIN factor.
  for (int i = 0; i < 5; i++) {
    std::optional<user_data_auth::AuthenticateAuthFactorReply> auth_reply =
        AuthenticatePinAuthFactor(session_id.value(), kPinLabel, kWrongPin);
    ASSERT_TRUE(auth_reply.has_value());
    ASSERT_NE(auth_reply->error(), user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  }

  // Correct PIN failed due to lockout.
  std::optional<user_data_auth::AuthenticateAuthFactorReply> auth_reply =
      AuthenticatePinAuthFactor(session_id.value(), kPinLabel, kPin);
  ASSERT_TRUE(auth_reply.has_value());
  ASSERT_EQ(auth_reply->error_info().primary_action(),
            user_data_auth::PRIMARY_FACTOR_LOCKED_OUT);

  // Reset PIN by password auth.
  auth_reply = AuthenticatePasswordAuthFactor(session_id.value(),
                                              kPasswordLabel, kPassword1);
  ASSERT_TRUE(auth_reply.has_value());
  ASSERT_EQ(auth_reply->error(), user_data_auth::CRYPTOHOME_ERROR_NOT_SET);

  // Correct PIN should succeed now.
  auth_reply = AuthenticatePinAuthFactor(session_id.value(), kPinLabel, kPin);
  ASSERT_TRUE(auth_reply.has_value());
  ASSERT_EQ(auth_reply->error(), user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
}

}  // namespace cryptohome
