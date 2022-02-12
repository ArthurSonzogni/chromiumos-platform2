// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/userdataauth.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <vector>

#include <base/check.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/location.h>
#include <base/stl_util.h>
#include <base/test/bind.h>
#include <base/test/test_mock_time_task_runner.h>
#include <brillo/cryptohome.h>
#include <chaps/token_manager_client_mock.h>
#include <dbus/mock_bus.h>
#include <libhwsec-foundation/error/testing_helper.h>
#include <libhwsec-foundation/tpm/tpm_version.h>
#include <metrics/metrics_library_mock.h>
#include <tpm_manager/client/mock_tpm_manager_utility.h>
#include <tpm_manager-client-test/tpm_manager/dbus-proxy-mocks.h>

#include "cryptohome/challenge_credentials/challenge_credentials_helper.h"
#include "cryptohome/challenge_credentials/mock_challenge_credentials_helper.h"
#include "cryptohome/cleanup/mock_disk_cleanup.h"
#include "cryptohome/cleanup/mock_low_disk_space_handler.h"
#include "cryptohome/cleanup/mock_user_oldest_activity_timestamp_manager.h"
#include "cryptohome/crypto/secure_blob_util.h"
#include "cryptohome/crypto/sha.h"
#include "cryptohome/cryptohome_common.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/filesystem_layout.h"
#include "cryptohome/mock_crypto.h"
#include "cryptohome/mock_cryptohome_keys_manager.h"
#include "cryptohome/mock_fingerprint_manager.h"
#include "cryptohome/mock_firmware_management_parameters.h"
#include "cryptohome/mock_install_attributes.h"
#include "cryptohome/mock_key_challenge_service.h"
#include "cryptohome/mock_key_challenge_service_factory.h"
#include "cryptohome/mock_keyset_management.h"
#include "cryptohome/mock_le_credential_backend.h"
#include "cryptohome/mock_pkcs11_init.h"
#include "cryptohome/mock_platform.h"
#include "cryptohome/mock_tpm.h"
#include "cryptohome/mock_vault_keyset.h"
#include "cryptohome/pkcs11/fake_pkcs11_token.h"
#include "cryptohome/pkcs11/mock_pkcs11_token_factory.h"
#include "cryptohome/protobuf_test_utils.h"
#include "cryptohome/storage/homedirs.h"
#include "cryptohome/storage/mock_arc_disk_quota.h"
#include "cryptohome/storage/mock_homedirs.h"
#include "cryptohome/storage/mock_mount.h"
#include "cryptohome/storage/mock_mount_factory.h"
#include "cryptohome/tpm.h"

using base::FilePath;
using brillo::SecureBlob;
using brillo::cryptohome::home::kGuestUserName;
using brillo::cryptohome::home::SanitizeUserNameWithSalt;

using ::hwsec::StatusChain;
using ::hwsec::TPMError;
using ::hwsec::TPMErrorBase;
using ::hwsec::TPMRetryAction;
using ::hwsec_foundation::error::testing::ReturnError;
using ::testing::_;
using ::testing::AtLeast;
using ::testing::ByMove;
using ::testing::DoAll;
using ::testing::ElementsAre;
using ::testing::ElementsAreArray;
using ::testing::EndsWith;
using ::testing::Eq;
using ::testing::InSequence;
using ::testing::Invoke;
using ::testing::InvokeWithoutArgs;
using ::testing::IsNull;
using ::testing::Mock;
using ::testing::NiceMock;
using ::testing::NotNull;
using ::testing::Pointee;
using ::testing::Property;
using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::SaveArg;
using ::testing::SaveArgPointee;
using ::testing::SetArgPointee;
using ::testing::WithArgs;

namespace cryptohome {

namespace {

bool AssignSalt(SecureBlob* salt) {
  SecureBlob fake_salt(CRYPTOHOME_DEFAULT_SALT_LENGTH, 'S');
  salt->swap(fake_salt);
  return true;
}

// Set to match the 5 minute timer and a 1 minute extension in AuthSession.
constexpr int kAuthSessionExtensionDuration = 60;
constexpr auto kAuthSessionTimeout = base::TimeDelta::FromMinutes(5);
constexpr auto kAuthSessionExtension =
    base::TimeDelta::FromSeconds(kAuthSessionExtensionDuration);

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
    SET_DEFAULT_TPM_FOR_TESTING;
    attrs_ = std::make_unique<NiceMock<MockInstallAttributes>>();
    dbus::Bus::Options options;
    options.bus_type = dbus::Bus::SYSTEM;
    bus_ = base::MakeRefCounted<NiceMock<dbus::MockBus>>(options);
    mount_bus_ = base::MakeRefCounted<NiceMock<dbus::MockBus>>(options);

    if (!userdataauth_) {
      // Note that this branch is usually taken as |userdataauth_| is usually
      // NULL. The reason for this branch is because some derived-class of this
      // class (such as UserDataAuthTestThreaded) need to have the constructor
      // of UserDataAuth run on a specific thread, and therefore will construct
      // |userdataauth_| before calling UserDataAuthTestBase::SetUp().
      userdataauth_.reset(new UserDataAuth());
    }
    userdataauth_->set_crypto(&crypto_);
    userdataauth_->set_keyset_management(&keyset_management_);
    userdataauth_->set_user_activity_timestamp_manager(
        &user_activity_timestamp_manager_);
    userdataauth_->set_homedirs(&homedirs_);
    userdataauth_->set_install_attrs(attrs_.get());
    userdataauth_->set_tpm(&tpm_);
    userdataauth_->set_cryptohome_keys_manager(&cryptohome_keys_manager_);
    userdataauth_->set_tpm_manager_util_(&tpm_manager_utility_);
    userdataauth_->set_platform(&platform_);
    userdataauth_->set_chaps_client(&chaps_client_);
    userdataauth_->set_firmware_management_parameters(&fwmp_);
    userdataauth_->set_fingerprint_manager(&fingerprint_manager_);
    userdataauth_->set_arc_disk_quota(&arc_disk_quota_);
    userdataauth_->set_pkcs11_init(&pkcs11_init_);
    userdataauth_->set_pkcs11_token_factory(&pkcs11_token_factory_);
    userdataauth_->set_mount_factory(&mount_factory_);
    userdataauth_->set_challenge_credentials_helper(
        &challenge_credentials_helper_);
    userdataauth_->set_key_challenge_service_factory(
        &key_challenge_service_factory_);
    userdataauth_->set_low_disk_space_handler(&low_disk_space_handler_);
    // Empty token list by default.  The effect is that there are no attempts
    // to unload tokens unless a test explicitly sets up the token list.
    ON_CALL(chaps_client_, GetTokenList(_, _)).WillByDefault(Return(true));
    // Skip CleanUpStaleMounts by default.
    ON_CALL(platform_, GetMountsBySourcePrefix(_, _))
        .WillByDefault(Return(false));
    // Setup fake public mount salt by default.
    ON_CALL(crypto_, GetPublicMountSalt(_))
        .WillByDefault(WithArgs<0>(Invoke(AssignSalt)));
    // Setup fake system salt by default.
    ON_CALL(crypto_, GetSystemSalt(_))
        .WillByDefault(WithArgs<0>(Invoke(AssignSalt)));
    // It doesnt matter what key it returns for the purposes of the UserDataAuth
    // test.
    ON_CALL(keyset_management_, GetPublicMountPassKey(_))
        .WillByDefault(
            Return(CreateSecureRandomBlob(CRYPTOHOME_DEFAULT_SALT_LENGTH)));
    // ARC Disk Quota initialization will do nothing.
    ON_CALL(arc_disk_quota_, Initialize()).WillByDefault(Return());
    // Low Disk space handler initialization will do nothing.
    ON_CALL(low_disk_space_handler_, Init(_)).WillByDefault(Return(true));
    ON_CALL(low_disk_space_handler_, disk_cleanup())
        .WillByDefault(Return(&disk_cleanup_));

    // Make sure FreeDiskSpaceDuringLogin is not called unexpectedly.
    EXPECT_CALL(disk_cleanup_, FreeDiskSpaceDuringLogin(_)).Times(0);
  }

  // This is a utility function for tests to setup a mount for a particular
  // user. After calling this function, |mount_| is available for use.
  void SetupMount(const std::string& username) {
    brillo::SecureBlob salt;
    AssignSalt(&salt);
    mount_ = new NiceMock<MockMount>();
    session_ = new UserSession(&homedirs_, &disk_cleanup_, &keyset_management_,
                               &user_activity_timestamp_manager_,
                               &pkcs11_token_factory_, salt, mount_);
    userdataauth_->set_session_for_user(username, session_.get());
  }

  // This is a helper function that compute the obfuscated username with the
  // fake salt.
  std::string GetObfuscatedUsername(const std::string& username) {
    brillo::SecureBlob salt;
    AssignSalt(&salt);
    return SanitizeUserNameWithSalt(username, salt);
  }

  // Helper function for creating a brillo::Error
  static brillo::ErrorPtr CreateDefaultError(const base::Location& from_here) {
    brillo::ErrorPtr error;
    brillo::Error::AddTo(&error, from_here, brillo::errors::dbus::kDomain,
                         DBUS_ERROR_FAILED, "Here's a fake error");
    return error;
  }

 protected:
  // Mock Crypto object, will be passed to UserDataAuth for its internal use.
  NiceMock<MockCrypto> crypto_;

  // Mock KeysetManagent object, will be passed to UserDataAuth for its internal
  // use.
  NiceMock<MockKeysetManagement> keyset_management_;

  // Mock UserOldestActivityTimestampManager, will be passed to UserDataAuth for
  // its internal use.
  NiceMock<MockUserOldestActivityTimestampManager>
      user_activity_timestamp_manager_;

  // Mock HomeDirs object, will be passed to UserDataAuth for its internal use.
  NiceMock<MockHomeDirs> homedirs_;

  // Mock DiskCleanup object, will be passed to UserDataAuth for its internal
  // use. Only FreeDiskSpaceDuringLogin should be called and it should not
  // be called more than necessary.
  NiceMock<MockDiskCleanup> disk_cleanup_;

  // Mock InstallAttributes object, will be passed to UserDataAuth for its
  // internal use.
  std::unique_ptr<NiceMock<MockInstallAttributes>> attrs_;

  // Mock Platform object, will be passed to UserDataAuth for its internal use.
  NiceMock<MockPlatform> platform_;

  // Mock TPM object, will be passed to UserDataAuth for its internal use.
  NiceMock<MockTpm> tpm_;

  // Mock Cryptohome Key Loader object, will be passed to UserDataAuth for its
  // internal use.
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager_;

  // Mock TPM Manager utility object, will be passed to UserDataAuth for its
  // internal use.
  NiceMock<tpm_manager::MockTpmManagerUtility> tpm_manager_utility_;

  // Mock ARC Disk Quota object, will be passed to UserDataAuth for its internal
  // use.
  NiceMock<MockArcDiskQuota> arc_disk_quota_;

  // Mock chaps token manager client, will be passed to UserDataAuth for its
  // internal use.
  NiceMock<chaps::TokenManagerClientMock> chaps_client_;

  // Mock PKCS#11 init object, will be passed to UserDataAuth for its internal
  // use.
  NiceMock<MockPkcs11Init> pkcs11_init_;

  // Mock Pcks11TokenFactory, will be passed to UserDataAuth for its internal
  // use.
  NiceMock<MockPkcs11TokenFactory> pkcs11_token_factory_;

  // Mock Firmware Management Parameters object, will be passed to UserDataAuth
  // for its internal use.
  NiceMock<MockFirmwareManagementParameters> fwmp_;

  // Mock Fingerprint Manager object, will be passed to UserDataAuth for its
  // internal use.
  NiceMock<MockFingerprintManager> fingerprint_manager_;

  // Mock challenge credential helper utility object, will be passed to
  // UserDataAuth for its internal use.
  NiceMock<MockChallengeCredentialsHelper> challenge_credentials_helper_;

  // Mock factory of key challenge services, will be passed to UserDataAuth for
  // its internal use.
  NiceMock<MockKeyChallengeServiceFactory> key_challenge_service_factory_;

  // Mock Mount Factory object, will be passed to UserDataAuth for its internal
  // use.
  NiceMock<MockMountFactory> mount_factory_;

  // Mock Low Disk Space handler object, will be passed to UserDataAuth for its
  // internal use.
  NiceMock<MockLowDiskSpaceHandler> low_disk_space_handler_;

  // Mock DBus object, will be passed to UserDataAuth for its internal use.
  scoped_refptr<NiceMock<dbus::MockBus>> bus_;

  // Mock DBus object on mount thread, will be passed to UserDataAuth for its
  // internal use.
  scoped_refptr<NiceMock<dbus::MockBus>> mount_bus_;

  // Session object
  scoped_refptr<UserSession> session_;

  // This is used to hold the mount object when we create a mock mount with
  // SetupMount().
  scoped_refptr<NiceMock<MockMount>> mount_;

  // Declare |userdataauth_| last so it gets destroyed before all the mocks.
  // This is important because otherwise the background thread may call into
  // mocks that have already been destroyed.
  std::unique_ptr<UserDataAuth> userdataauth_;
};

// Test fixture that implements two task runners, which is similar to the task
// environment in UserDataAuth. Developers could fast forward the time in
// UserDataAuth, and prevent the flakiness caused by the real time clock. Note
// that this does not initialize |userdataauth_|. And using WaitableEvent in it
// may hang the test runner. Because all of the task runner is on the same
// thread, we would need to use TaskGuard to let UserDataAuth know which task
// runner is current task runner.
class UserDataAuthTestTasked : public UserDataAuthTestBase {
 public:
  UserDataAuthTestTasked() = default;
  UserDataAuthTestTasked(const UserDataAuthTestTasked&) = delete;
  UserDataAuthTestTasked& operator=(const UserDataAuthTestTasked&) = delete;

  ~UserDataAuthTestTasked() override = default;

  void SetUp() override {
    // Setup the usual stuff
    UserDataAuthTestBase::SetUp();

    // We do the task runner stuff for this test fixture.
    userdataauth_->set_origin_task_runner(origin_task_runner_);
    userdataauth_->set_mount_task_runner(mount_task_runner_);
    userdataauth_->set_current_thread_id_for_test(
        UserDataAuth::TestThreadId::kOriginThread);

    ON_CALL(platform_, GetCurrentTime()).WillByDefault(Invoke([this]() {
      // The time between origin and mount task runner may have a skew when fast
      // forwarding the time. But current running task runner time must be the
      // biggest one.
      return std::max(origin_task_runner_->Now(), mount_task_runner_->Now());
    }));
  }

  void CreatePkcs11TokenInSession(scoped_refptr<NiceMock<MockMount>> mount,
                                  scoped_refptr<UserSession> session) {
    EXPECT_CALL(disk_cleanup_, FreeDiskSpaceDuringLogin(_));
    EXPECT_CALL(*mount, MountCryptohome(_, _, _))
        .WillOnce(Return(MOUNT_ERROR_NONE));

    auto token = std::make_unique<FakePkcs11Token>();
    EXPECT_CALL(pkcs11_token_factory_, New(_, _, _))
        .WillOnce(Return(ByMove(std::move(token))));

    auto vk = std::make_unique<VaultKeyset>();
    vk->Initialize(&platform_, &crypto_);
    vk->CreateRandom();
    ASSERT_EQ(
        MOUNT_ERROR_NONE,
        session->MountVault(session->username_, FileSystemKeyset(*vk.get()),
                            CryptohomeVault::Options()));
  }

  void InitializePkcs11TokenInSession(scoped_refptr<NiceMock<MockMount>> mount,
                                      scoped_refptr<UserSession> session) {
    // PKCS#11 will initialization works only when it's mounted.
    ON_CALL(*mount, IsMounted()).WillByDefault(Return(true));

    userdataauth_->InitializePkcs11(session.get());
  }

  void TearDown() override {
    RunUntilIdle();
    // Destruct the |userdataauth_| object.
    userdataauth_.reset();
  }

  // Initialize |userdataauth_| in |origin_task_runner_|
  void InitializeUserDataAuth() {
    TaskGuard guard(this, UserDataAuth::TestThreadId::kOriginThread);
    ASSERT_TRUE(userdataauth_->Initialize());
    userdataauth_->set_dbus(bus_);
    userdataauth_->set_mount_thread_dbus(mount_bus_);
    ASSERT_TRUE(userdataauth_->PostDBusInitialize());
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
      {
        TaskGuard guard(this, UserDataAuth::TestThreadId::kOriginThread);
        origin_task_runner_->FastForwardBy(delay);
      }

      // Forward and run the mount task runner
      {
        TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
        mount_task_runner_->FastForwardBy(delay);
      }

      // Decrease the virtual time.
      delta -= delay;
    }

    // Make sure there is no zero delay tasks remain.
    RunUntilIdle();
  }

  // Run the all of the task runners until they don't find any zero delay tasks
  // in their queues.
  void RunUntilIdle() {
    bool pending = true;
    while (pending) {
      pending = false;
      bool origin_pending =
          origin_task_runner_->NextPendingTaskDelay().is_zero();
      pending |= origin_pending;
      if (origin_pending) {
        TaskGuard guard(this, UserDataAuth::TestThreadId::kOriginThread);
        origin_task_runner_->RunUntilIdle();
      }
      bool mount_pending = mount_task_runner_->NextPendingTaskDelay().is_zero();
      pending |= mount_pending;
      if (mount_pending) {
        TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
        mount_task_runner_->RunUntilIdle();
      }
    }
  }

 protected:
  // TaskGuard would help us to guarantee the thread id in the unit-tests scope,
  // so we could check AssertOnOriginThread and AssertOnMountThread.
  class TaskGuard {
   public:
    TaskGuard(UserDataAuthTestTasked* uda_test,
              UserDataAuth::TestThreadId thread_id)
        : uda_test_(uda_test),
          old_thread_id_(
              uda_test_->userdataauth_->get_current_thread_id_for_test()),
          new_thread_id_(thread_id) {
      uda_test_->userdataauth_->set_current_thread_id_for_test(new_thread_id_);
    }
    ~TaskGuard() {
      uda_test_->RunUntilIdle();
      EXPECT_EQ(new_thread_id_,
                uda_test_->userdataauth_->get_current_thread_id_for_test());
      uda_test_->userdataauth_->set_current_thread_id_for_test(old_thread_id_);
    }

   private:
    UserDataAuthTestTasked* uda_test_;
    UserDataAuth::TestThreadId old_thread_id_;
    UserDataAuth::TestThreadId new_thread_id_;
  };

  // MockTimeTaskRunner for origin and mount thread.
  scoped_refptr<base::TestMockTimeTaskRunner> origin_task_runner_{
      new base::TestMockTimeTaskRunner()};
  base::TestMockTimeTaskRunner::ScopedContext scoped_origin_context_{
      origin_task_runner_};
  scoped_refptr<base::TestMockTimeTaskRunner> mount_task_runner_{
      new base::TestMockTimeTaskRunner()};
  base::TestMockTimeTaskRunner::ScopedContext scoped_mount_context_{
      mount_task_runner_};
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
    UserDataAuthTestNotInitialized::SetUp();
    InitializeUserDataAuth();
  }
};

namespace CryptohomeErrorCodeEquivalenceTest {
// This test is completely static, so it is not wrapped in the TEST_F() block.
static_assert(static_cast<int>(user_data_auth::CRYPTOHOME_ERROR_NOT_SET) ==
                  static_cast<int>(cryptohome::CRYPTOHOME_ERROR_NOT_SET),
              "Enum member CRYPTOHOME_ERROR_NOT_SET differs between "
              "user_data_auth:: and cryptohome::");
static_assert(
    static_cast<int>(user_data_auth::CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND) ==
        static_cast<int>(cryptohome::CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND),
    "Enum member CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND differs between "
    "user_data_auth:: and cryptohome::");
static_assert(
    static_cast<int>(
        user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_NOT_FOUND) ==
        static_cast<int>(
            cryptohome::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_NOT_FOUND),
    "Enum member CRYPTOHOME_ERROR_AUTHORIZATION_KEY_NOT_FOUND differs between "
    "user_data_auth:: and cryptohome::");
static_assert(
    static_cast<int>(
        user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED) ==
        static_cast<int>(cryptohome::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED),
    "Enum member CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED differs between "
    "user_data_auth:: and cryptohome::");
static_assert(
    static_cast<int>(user_data_auth::CRYPTOHOME_ERROR_NOT_IMPLEMENTED) ==
        static_cast<int>(cryptohome::CRYPTOHOME_ERROR_NOT_IMPLEMENTED),
    "Enum member CRYPTOHOME_ERROR_NOT_IMPLEMENTED differs between "
    "user_data_auth:: and cryptohome::");
static_assert(static_cast<int>(user_data_auth::CRYPTOHOME_ERROR_MOUNT_FATAL) ==
                  static_cast<int>(cryptohome::CRYPTOHOME_ERROR_MOUNT_FATAL),
              "Enum member CRYPTOHOME_ERROR_MOUNT_FATAL differs between "
              "user_data_auth:: and cryptohome::");
static_assert(
    static_cast<int>(user_data_auth::CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY) ==
        static_cast<int>(cryptohome::CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY),
    "Enum member CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY differs between "
    "user_data_auth:: and cryptohome::");
static_assert(
    static_cast<int>(user_data_auth::CRYPTOHOME_ERROR_TPM_COMM_ERROR) ==
        static_cast<int>(cryptohome::CRYPTOHOME_ERROR_TPM_COMM_ERROR),
    "Enum member CRYPTOHOME_ERROR_TPM_COMM_ERROR differs between "
    "user_data_auth:: and cryptohome::");
static_assert(
    static_cast<int>(user_data_auth::CRYPTOHOME_ERROR_TPM_DEFEND_LOCK) ==
        static_cast<int>(cryptohome::CRYPTOHOME_ERROR_TPM_DEFEND_LOCK),
    "Enum member CRYPTOHOME_ERROR_TPM_DEFEND_LOCK differs between "
    "user_data_auth:: and cryptohome::");
static_assert(
    static_cast<int>(user_data_auth::CRYPTOHOME_ERROR_TPM_NEEDS_REBOOT) ==
        static_cast<int>(cryptohome::CRYPTOHOME_ERROR_TPM_NEEDS_REBOOT),
    "Enum member CRYPTOHOME_ERROR_TPM_NEEDS_REBOOT differs between "
    "user_data_auth:: and cryptohome::");
static_assert(
    static_cast<int>(
        user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_DENIED) ==
        static_cast<int>(cryptohome::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_DENIED),
    "Enum member CRYPTOHOME_ERROR_AUTHORIZATION_KEY_DENIED differs between "
    "user_data_auth:: and cryptohome::");
static_assert(
    static_cast<int>(user_data_auth::CRYPTOHOME_ERROR_KEY_QUOTA_EXCEEDED) ==
        static_cast<int>(cryptohome::CRYPTOHOME_ERROR_KEY_QUOTA_EXCEEDED),
    "Enum member CRYPTOHOME_ERROR_KEY_QUOTA_EXCEEDED differs between "
    "user_data_auth:: and cryptohome::");
static_assert(
    static_cast<int>(user_data_auth::CRYPTOHOME_ERROR_KEY_LABEL_EXISTS) ==
        static_cast<int>(cryptohome::CRYPTOHOME_ERROR_KEY_LABEL_EXISTS),
    "Enum member CRYPTOHOME_ERROR_KEY_LABEL_EXISTS differs between "
    "user_data_auth:: and cryptohome::");
static_assert(
    static_cast<int>(user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE) ==
        static_cast<int>(cryptohome::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE),
    "Enum member CRYPTOHOME_ERROR_BACKING_STORE_FAILURE differs between "
    "user_data_auth:: and cryptohome::");
static_assert(
    static_cast<int>(
        user_data_auth::CRYPTOHOME_ERROR_UPDATE_SIGNATURE_INVALID) ==
        static_cast<int>(cryptohome::CRYPTOHOME_ERROR_UPDATE_SIGNATURE_INVALID),
    "Enum member CRYPTOHOME_ERROR_UPDATE_SIGNATURE_INVALID differs between "
    "user_data_auth:: and cryptohome::");
static_assert(
    static_cast<int>(user_data_auth::CRYPTOHOME_ERROR_KEY_NOT_FOUND) ==
        static_cast<int>(cryptohome::CRYPTOHOME_ERROR_KEY_NOT_FOUND),
    "Enum member CRYPTOHOME_ERROR_KEY_NOT_FOUND differs between "
    "user_data_auth:: and cryptohome::");
static_assert(static_cast<int>(
                  user_data_auth::CRYPTOHOME_ERROR_LOCKBOX_SIGNATURE_INVALID) ==
                  static_cast<int>(
                      cryptohome::CRYPTOHOME_ERROR_LOCKBOX_SIGNATURE_INVALID),
              "Enum member CRYPTOHOME_ERROR_LOCKBOX_SIGNATURE_INVALID differs "
              "between user_data_auth:: and cryptohome::");
static_assert(
    static_cast<int>(user_data_auth::CRYPTOHOME_ERROR_LOCKBOX_CANNOT_SIGN) ==
        static_cast<int>(cryptohome::CRYPTOHOME_ERROR_LOCKBOX_CANNOT_SIGN),
    "Enum member CRYPTOHOME_ERROR_LOCKBOX_CANNOT_SIGN differs between "
    "user_data_auth:: and cryptohome::");
static_assert(
    static_cast<int>(
        user_data_auth::CRYPTOHOME_ERROR_BOOT_ATTRIBUTE_NOT_FOUND) ==
        static_cast<int>(cryptohome::CRYPTOHOME_ERROR_BOOT_ATTRIBUTE_NOT_FOUND),
    "Enum member CRYPTOHOME_ERROR_BOOT_ATTRIBUTE_NOT_FOUND differs between "
    "user_data_auth:: and cryptohome::");
static_assert(
    static_cast<int>(
        user_data_auth::CRYPTOHOME_ERROR_BOOT_ATTRIBUTES_CANNOT_SIGN) ==
        static_cast<int>(
            cryptohome::CRYPTOHOME_ERROR_BOOT_ATTRIBUTES_CANNOT_SIGN),
    "Enum member CRYPTOHOME_ERROR_BOOT_ATTRIBUTES_CANNOT_SIGN differs between "
    "user_data_auth:: and cryptohome::");
static_assert(
    static_cast<int>(user_data_auth::CRYPTOHOME_ERROR_TPM_EK_NOT_AVAILABLE) ==
        static_cast<int>(cryptohome::CRYPTOHOME_ERROR_TPM_EK_NOT_AVAILABLE),
    "Enum member CRYPTOHOME_ERROR_TPM_EK_NOT_AVAILABLE differs between "
    "user_data_auth:: and cryptohome::");
static_assert(
    static_cast<int>(user_data_auth::CRYPTOHOME_ERROR_ATTESTATION_NOT_READY) ==
        static_cast<int>(cryptohome::CRYPTOHOME_ERROR_ATTESTATION_NOT_READY),
    "Enum member CRYPTOHOME_ERROR_ATTESTATION_NOT_READY differs between "
    "user_data_auth:: and cryptohome::");
static_assert(
    static_cast<int>(user_data_auth::CRYPTOHOME_ERROR_CANNOT_CONNECT_TO_CA) ==
        static_cast<int>(cryptohome::CRYPTOHOME_ERROR_CANNOT_CONNECT_TO_CA),
    "Enum member CRYPTOHOME_ERROR_CANNOT_CONNECT_TO_CA differs between "
    "user_data_auth:: and cryptohome::");
static_assert(
    static_cast<int>(user_data_auth::CRYPTOHOME_ERROR_CA_REFUSED_ENROLLMENT) ==
        static_cast<int>(cryptohome::CRYPTOHOME_ERROR_CA_REFUSED_ENROLLMENT),
    "Enum member CRYPTOHOME_ERROR_CA_REFUSED_ENROLLMENT differs between "
    "user_data_auth:: and cryptohome::");
static_assert(
    static_cast<int>(user_data_auth::CRYPTOHOME_ERROR_CA_REFUSED_CERTIFICATE) ==
        static_cast<int>(cryptohome::CRYPTOHOME_ERROR_CA_REFUSED_CERTIFICATE),
    "Enum member CRYPTOHOME_ERROR_CA_REFUSED_CERTIFICATE differs between "
    "user_data_auth:: and cryptohome::");
static_assert(
    static_cast<int>(
        user_data_auth::CRYPTOHOME_ERROR_INTERNAL_ATTESTATION_ERROR) ==
        static_cast<int>(
            cryptohome::CRYPTOHOME_ERROR_INTERNAL_ATTESTATION_ERROR),
    "Enum member CRYPTOHOME_ERROR_INTERNAL_ATTESTATION_ERROR differs between "
    "user_data_auth:: and cryptohome::");
static_assert(
    static_cast<int>(
        user_data_auth::
            CRYPTOHOME_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_INVALID) ==
        static_cast<int>(
            cryptohome::
                CRYPTOHOME_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_INVALID),
    "Enum member CRYPTOHOME_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_INVALID "
    "differs between user_data_auth:: and cryptohome::");
static_assert(
    static_cast<int>(
        user_data_auth::
            CRYPTOHOME_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_CANNOT_STORE) ==
        static_cast<int>(
            cryptohome::
                CRYPTOHOME_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_CANNOT_STORE),
    "Enum member CRYPTOHOME_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_CANNOT_STORE "
    "differs between user_data_auth:: and cryptohome::");
static_assert(
    static_cast<int>(
        user_data_auth::
            CRYPTOHOME_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_CANNOT_REMOVE) ==
        static_cast<int>(
            cryptohome::
                CRYPTOHOME_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_CANNOT_REMOVE),
    "Enum member CRYPTOHOME_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_CANNOT_REMOVE "
    "differs between user_data_auth:: and cryptohome::");
static_assert(
    static_cast<int>(user_data_auth::CRYPTOHOME_ERROR_MOUNT_OLD_ENCRYPTION) ==
        static_cast<int>(cryptohome::CRYPTOHOME_ERROR_MOUNT_OLD_ENCRYPTION),
    "Enum member CRYPTOHOME_ERROR_MOUNT_OLD_ENCRYPTION differs between "
    "user_data_auth:: and cryptohome::");
static_assert(
    static_cast<int>(
        user_data_auth::CRYPTOHOME_ERROR_MOUNT_PREVIOUS_MIGRATION_INCOMPLETE) ==
        static_cast<int>(
            cryptohome::CRYPTOHOME_ERROR_MOUNT_PREVIOUS_MIGRATION_INCOMPLETE),
    "Enum member CRYPTOHOME_ERROR_MOUNT_PREVIOUS_MIGRATION_INCOMPLETE differs "
    "between user_data_auth:: and cryptohome::");
static_assert(
    static_cast<int>(user_data_auth::CRYPTOHOME_ERROR_MIGRATE_KEY_FAILED) ==
        static_cast<int>(cryptohome::CRYPTOHOME_ERROR_MIGRATE_KEY_FAILED),
    "Enum member CRYPTOHOME_ERROR_MIGRATE_KEY_FAILED differs between "
    "user_data_auth:: and cryptohome::");
static_assert(
    static_cast<int>(user_data_auth::CRYPTOHOME_ERROR_REMOVE_FAILED) ==
        static_cast<int>(cryptohome::CRYPTOHOME_ERROR_REMOVE_FAILED),
    "Enum member CRYPTOHOME_ERROR_REMOVE_FAILED differs between "
    "user_data_auth:: and cryptohome::");
static_assert(
    static_cast<int>(user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT) ==
        static_cast<int>(cryptohome::CRYPTOHOME_ERROR_INVALID_ARGUMENT),
    "Enum member CRYPTOHOME_ERROR_INVALID_ARGUMENT differs between "
    "user_data_auth:: and cryptohome::");
static_assert(
    static_cast<int>(
        user_data_auth::CRYPTOHOME_ERROR_INSTALL_ATTRIBUTES_GET_FAILED) ==
        static_cast<int>(
            cryptohome::CRYPTOHOME_ERROR_INSTALL_ATTRIBUTES_GET_FAILED),
    "Enum member CRYPTOHOME_ERROR_INSTALL_ATTRIBUTES_GET_FAILED differs "
    "between user_data_auth:: and cryptohome::");
static_assert(
    static_cast<int>(
        user_data_auth::CRYPTOHOME_ERROR_INSTALL_ATTRIBUTES_SET_FAILED) ==
        static_cast<int>(
            cryptohome::CRYPTOHOME_ERROR_INSTALL_ATTRIBUTES_SET_FAILED),
    "Enum member CRYPTOHOME_ERROR_INSTALL_ATTRIBUTES_SET_FAILED differs "
    "between user_data_auth:: and cryptohome::");
static_assert(
    static_cast<int>(
        user_data_auth::CRYPTOHOME_ERROR_INSTALL_ATTRIBUTES_FINALIZE_FAILED) ==
        static_cast<int>(
            cryptohome::CRYPTOHOME_ERROR_INSTALL_ATTRIBUTES_FINALIZE_FAILED),
    "Enum member CRYPTOHOME_ERROR_INSTALL_ATTRIBUTES_FINALIZE_FAILED differs "
    "between user_data_auth:: and cryptohome::");
static_assert(
    static_cast<int>(
        user_data_auth::
            CRYPTOHOME_ERROR_UPDATE_USER_ACTIVITY_TIMESTAMP_FAILED) ==
        static_cast<int>(
            cryptohome::CRYPTOHOME_ERROR_UPDATE_USER_ACTIVITY_TIMESTAMP_FAILED),
    "Enum member CRYPTOHOME_ERROR_UPDATE_USER_ACTIVITY_TIMESTAMP_FAILED "
    "differs between user_data_auth:: and cryptohome::");
static_assert(
    static_cast<int>(user_data_auth::CRYPTOHOME_ERROR_FAILED_TO_READ_PCR) ==
        static_cast<int>(cryptohome::CRYPTOHOME_ERROR_FAILED_TO_READ_PCR),
    "Enum member CRYPTOHOME_ERROR_FAILED_TO_READ_PCR differs between "
    "user_data_auth:: and cryptohome::");
static_assert(
    static_cast<int>(user_data_auth::CRYPTOHOME_ERROR_PCR_ALREADY_EXTENDED) ==
        static_cast<int>(cryptohome::CRYPTOHOME_ERROR_PCR_ALREADY_EXTENDED),
    "Enum member CRYPTOHOME_ERROR_PCR_ALREADY_EXTENDED differs between "
    "user_data_auth:: and cryptohome::");
static_assert(
    static_cast<int>(user_data_auth::CRYPTOHOME_ERROR_FAILED_TO_EXTEND_PCR) ==
        static_cast<int>(cryptohome::CRYPTOHOME_ERROR_FAILED_TO_EXTEND_PCR),
    "Enum member CRYPTOHOME_ERROR_FAILED_TO_EXTEND_PCR differs between "
    "user_data_auth:: and cryptohome::");
static_assert(
    static_cast<int>(user_data_auth::CRYPTOHOME_ERROR_TPM_UPDATE_REQUIRED) ==
        static_cast<int>(cryptohome::CRYPTOHOME_ERROR_TPM_UPDATE_REQUIRED),
    "Enum member CRYPTOHOME_ERROR_TPM_UPDATE_REQUIRED differs between "
    "user_data_auth:: and cryptohome::");

static_assert(
    static_cast<int>(user_data_auth::CRYPTOHOME_ERROR_VAULT_UNRECOVERABLE) ==
        static_cast<int>(cryptohome::CRYPTOHOME_ERROR_VAULT_UNRECOVERABLE),
    "Enum member CRYPTOHOME_ERROR_VAULT_UNRECOVERABLE differs between "
    "user_data_auth:: and cryptohome::");

static_assert(
    static_cast<int>(user_data_auth::CRYPTOHOME_TOKEN_SERIALIZATION_FAILED) ==
        static_cast<int>(cryptohome::CRYPTOHOME_TOKEN_SERIALIZATION_FAILED),
    "Enum member CRYPTOHOME_TOKEN_SERIALIZATION_FAILED differs between "
    "user_data_auth:: and cryptohome::");

static_assert(
    static_cast<int>(user_data_auth::CRYPTOHOME_INVALID_AUTH_SESSION_TOKEN) ==
        static_cast<int>(cryptohome::CRYPTOHOME_INVALID_AUTH_SESSION_TOKEN),
    "Enum member CRYPTOHOME_INVALID_AUTH_SESSION_TOKEN differs between "
    "user_data_auth:: and cryptohome::");

static_assert(
    static_cast<int>(user_data_auth::CRYPTOHOME_ADD_CREDENTIALS_FAILED) ==
        static_cast<int>(cryptohome::CRYPTOHOME_ADD_CREDENTIALS_FAILED),
    "Enum member CRYPTOHOME_ADD_CREDENTIALS_FAILED differs between "
    "user_data_auth:: and cryptohome::");
static_assert(
    static_cast<int>(
        user_data_auth::CRYPTOHOME_ERROR_UNAUTHENTICATED_AUTH_SESSION) ==
        static_cast<int>(
            cryptohome::CRYPTOHOME_ERROR_UNAUTHENTICATED_AUTH_SESSION),
    "Enum member CRYPTOHOME_ERROR_UNAUTHENTICATED_AUTH_SESSION differs between "
    "user_data_auth:: and cryptohome::");
static_assert(
    user_data_auth::CryptohomeErrorCode_MAX == 51,
    "user_data_auth::CrytpohomeErrorCode's element count is incorrect");
static_assert(cryptohome::CryptohomeErrorCode_MAX == 51,
              "cryptohome::CrytpohomeErrorCode's element count is incorrect");
}  // namespace CryptohomeErrorCodeEquivalenceTest

TEST_F(UserDataAuthTest, IsMounted) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
  // By default there are no mount right after initialization
  EXPECT_FALSE(userdataauth_->IsMounted());
  EXPECT_FALSE(userdataauth_->IsMounted("foo@gmail.com"));

  // Add a mount associated with foo@gmail.com
  SetupMount("foo@gmail.com");

  // Test the code path that doesn't specify a user, and when there's a mount
  // that's unmounted.
  EXPECT_CALL(*mount_, IsMounted()).WillOnce(Return(false));
  EXPECT_FALSE(userdataauth_->IsMounted());

  // Test to see if is_ephemeral works and test the code path that doesn't
  // specify a user.
  bool is_ephemeral = true;
  EXPECT_CALL(*mount_, IsMounted()).WillOnce(Return(true));
  EXPECT_CALL(*mount_, IsEphemeral()).WillOnce(Return(false));
  EXPECT_TRUE(userdataauth_->IsMounted("", &is_ephemeral));
  EXPECT_FALSE(is_ephemeral);

  // Test to see if is_ephemeral works, and test the code path that specify the
  // user.
  EXPECT_CALL(*mount_, IsMounted()).WillOnce(Return(true));
  EXPECT_CALL(*mount_, IsEphemeral()).WillOnce(Return(true));
  EXPECT_TRUE(userdataauth_->IsMounted("foo@gmail.com", &is_ephemeral));
  EXPECT_TRUE(is_ephemeral);

  // Note: IsMounted will not be called in this case.
  EXPECT_FALSE(userdataauth_->IsMounted("bar@gmail.com", &is_ephemeral));
  EXPECT_FALSE(is_ephemeral);
}

TEST_F(UserDataAuthTest, Unmount_AllDespiteFailures) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);

  constexpr char kUsername1[] = "foo@gmail.com";
  constexpr char kUsername2[] = "bar@gmail.com";

  scoped_refptr<NiceMock<MockMount>> mount1 = new NiceMock<MockMount>();
  scoped_refptr<UserSession> session1 =
      new UserSession(&homedirs_, &disk_cleanup_, &keyset_management_,
                      &user_activity_timestamp_manager_, &pkcs11_token_factory_,
                      brillo::SecureBlob(), mount1);
  userdataauth_->set_session_for_user(kUsername1, session1.get());

  scoped_refptr<NiceMock<MockMount>> mount2 = new NiceMock<MockMount>();
  scoped_refptr<UserSession> session2 =
      new UserSession(&homedirs_, &disk_cleanup_, &keyset_management_,
                      &user_activity_timestamp_manager_, &pkcs11_token_factory_,
                      brillo::SecureBlob(), mount2);
  userdataauth_->set_session_for_user(kUsername2, session2.get());

  InSequence sequence;
  EXPECT_CALL(*mount2, IsMounted()).WillOnce(Return(true));
  EXPECT_CALL(*mount2, IsNonEphemeralMounted()).WillOnce(Return(true));
  EXPECT_CALL(*mount2, UnmountCryptohome()).WillOnce(Return(false));
  EXPECT_CALL(*mount1, IsMounted()).WillOnce(Return(true));
  EXPECT_CALL(*mount1, IsNonEphemeralMounted()).WillOnce(Return(true));
  EXPECT_CALL(*mount1, UnmountCryptohome()).WillOnce(Return(true));

  EXPECT_FALSE(userdataauth_->RemoveAllMounts());
}

TEST_F(UserDataAuthTest, Unmount_EphemeralNotEnabled) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
  // Unmount validity test.
  // The tests on whether stale mount are cleaned up is in another set of tests
  // called CleanUpStale_*

  // Add a mount associated with foo@gmail.com
  SetupMount("foo@gmail.com");

  // Unmount will be successful.
  EXPECT_CALL(*mount_, UnmountCryptohome()).WillOnce(Return(true));
  // If anyone asks, this mount is still mounted.
  ON_CALL(*mount_, IsMounted()).WillByDefault(Return(true));

  // Test that non-owner's vaults are not touched.
  EXPECT_CALL(homedirs_, AreEphemeralUsersEnabled()).WillOnce(Return(false));
  EXPECT_CALL(homedirs_, RemoveNonOwnerCryptohomes()).Times(0);

  // Unmount should be successful.
  EXPECT_TRUE(userdataauth_->Unmount());

  // It should be unmounted in the end.
  EXPECT_FALSE(userdataauth_->IsMounted());

  // Add another mount associated with bar@gmail.com
  SetupMount("bar@gmail.com");

  // Unmount will be unsuccessful.
  EXPECT_CALL(*mount_, UnmountCryptohome()).WillOnce(Return(false));
  // If anyone asks, this mount is still mounted.
  ON_CALL(*mount_, IsMounted()).WillByDefault(Return(true));

  // Test that non-owner's vaults are not touched.
  EXPECT_CALL(homedirs_, AreEphemeralUsersEnabled()).WillOnce(Return(false));
  EXPECT_CALL(homedirs_, RemoveNonOwnerCryptohomes()).Times(0);

  // Unmount should be honest about failures.
  EXPECT_FALSE(userdataauth_->Unmount());

  // Unmount will remove all mounts even if it failed.
  EXPECT_FALSE(userdataauth_->IsMounted());
}

TEST_F(UserDataAuthTest, Unmount_EphemeralEnabled) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
  // Unmount validity test.
  // The tests on whether stale mount are cleaned up is in another set of tests
  // called CleanUpStale_*

  // Add a mount associated with foo@gmail.com
  SetupMount("foo@gmail.com");

  // Unmount will be successful.
  EXPECT_CALL(*mount_, UnmountCryptohome()).WillOnce(Return(true));
  // If anyone asks, this mount is still mounted.
  ON_CALL(*mount_, IsMounted()).WillByDefault(Return(true));

  // Test that non-owner's vaults are cleaned up.
  EXPECT_CALL(homedirs_, AreEphemeralUsersEnabled()).WillOnce(Return(true));
  EXPECT_CALL(homedirs_, RemoveNonOwnerCryptohomes()).Times(1);

  // Unmount should be successful.
  EXPECT_TRUE(userdataauth_->Unmount());

  // It should be unmounted in the end.
  EXPECT_FALSE(userdataauth_->IsMounted());

  // Add another mount associated with bar@gmail.com
  SetupMount("bar@gmail.com");

  // Unmount will be unsuccessful.
  EXPECT_CALL(*mount_, UnmountCryptohome()).WillOnce(Return(false));
  // If anyone asks, this mount is still mounted.
  ON_CALL(*mount_, IsMounted()).WillByDefault(Return(true));

  // Test that non-owner's vaults are cleaned up anyway.
  EXPECT_CALL(homedirs_, AreEphemeralUsersEnabled()).WillOnce(Return(true));
  EXPECT_CALL(homedirs_, RemoveNonOwnerCryptohomes()).Times(1);

  // Unmount should be honest about failures.
  EXPECT_FALSE(userdataauth_->Unmount());

  // Unmount will remove all mounts even if it failed.
  EXPECT_FALSE(userdataauth_->IsMounted());
}

TEST_F(UserDataAuthTest, InitializePkcs11Success) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
  // This test the most common success case for PKCS#11 initialization.

  EXPECT_FALSE(userdataauth_->IsMounted());

  // Add a mount associated with foo@gmail.com
  SetupMount("foo@gmail.com");

  CreatePkcs11TokenInSession(mount_, session_);

  // At first the token is not ready
  EXPECT_FALSE(session_->GetPkcs11Token()->IsReady());

  InitializePkcs11TokenInSession(mount_, session_);

  EXPECT_TRUE(session_->GetPkcs11Token()->IsReady());
}

TEST_F(UserDataAuthTest, InitializePkcs11TpmNotOwned) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
  // Test when TPM isn't owned.

  // Add a mount associated with foo@gmail.com
  SetupMount("foo@gmail.com");

  CreatePkcs11TokenInSession(mount_, session_);

  // At first the token is not ready
  EXPECT_FALSE(session_->GetPkcs11Token()->IsReady());

  // TPM is enabled but not owned.
  ON_CALL(tpm_, IsEnabled()).WillByDefault(Return(true));
  EXPECT_CALL(tpm_, IsOwned()).Times(AtLeast(1)).WillRepeatedly(Return(false));

  InitializePkcs11TokenInSession(mount_, session_);

  // Still not ready because TPM is not owned.
  EXPECT_FALSE(session_->GetPkcs11Token()->IsReady());

  // We'll need to call Pkcs11Token::Insert() and IsEnabled() later in the test.
  Mock::VerifyAndClearExpectations(mount_.get());
  Mock::VerifyAndClearExpectations(&tpm_);

  // Next check when the TPM is now owned.

  // TPM is enabled and owned.
  ON_CALL(tpm_, IsEnabled()).WillByDefault(Return(true));
  EXPECT_CALL(tpm_, IsOwned()).Times(AtLeast(1)).WillRepeatedly(Return(true));

  InitializePkcs11TokenInSession(mount_, session_);

  EXPECT_TRUE(session_->GetPkcs11Token()->IsReady());
}

TEST_F(UserDataAuthTest, InitializePkcs11Unmounted) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
  // Add a mount associated with foo@gmail.com
  SetupMount("foo@gmail.com");

  CreatePkcs11TokenInSession(mount_, session_);

  // At first the token is not ready
  EXPECT_FALSE(session_->GetPkcs11Token()->IsReady());

  ON_CALL(*mount_, IsMounted()).WillByDefault(Return(false));
  // The initialization code should at least check, right?
  EXPECT_CALL(*mount_, IsMounted())
      .Times(AtLeast(1))
      .WillRepeatedly(Return(false));

  userdataauth_->InitializePkcs11(session_.get());

  // Still not ready because already unmounted
  EXPECT_FALSE(session_->GetPkcs11Token()->IsReady());
}

TEST_F(UserDataAuthTest, Pkcs11IsTpmTokenReady) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
  // When there's no mount at all, it should be true.
  EXPECT_TRUE(userdataauth_->Pkcs11IsTpmTokenReady());

  constexpr char kUsername1[] = "foo@gmail.com";
  constexpr char kUsername2[] = "bar@gmail.com";

  brillo::SecureBlob salt;
  AssignSalt(&salt);

  scoped_refptr<NiceMock<MockMount>> mount1 = new NiceMock<MockMount>();
  scoped_refptr<UserSession> session1 = new UserSession(
      &homedirs_, &disk_cleanup_, &keyset_management_,
      &user_activity_timestamp_manager_, &pkcs11_token_factory_, salt, mount1);
  userdataauth_->set_session_for_user(kUsername1, session1.get());
  CreatePkcs11TokenInSession(mount1, session1);

  scoped_refptr<NiceMock<MockMount>> mount2 = new NiceMock<MockMount>();
  scoped_refptr<UserSession> session2 = new UserSession(
      &homedirs_, &disk_cleanup_, &keyset_management_,
      &user_activity_timestamp_manager_, &pkcs11_token_factory_, salt, mount2);
  userdataauth_->set_session_for_user(kUsername2, session2.get());
  CreatePkcs11TokenInSession(mount2, session2);

  // Both are uninitialized.
  EXPECT_FALSE(userdataauth_->Pkcs11IsTpmTokenReady());

  // Only one is initialized.
  InitializePkcs11TokenInSession(mount2, session2);
  EXPECT_FALSE(userdataauth_->Pkcs11IsTpmTokenReady());

  // Both is initialized.
  InitializePkcs11TokenInSession(mount1, session1);
  EXPECT_TRUE(userdataauth_->Pkcs11IsTpmTokenReady());
}

TEST_F(UserDataAuthTest, Pkcs11GetTpmTokenInfo) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kOriginThread);
  user_data_auth::TpmTokenInfo info;

  constexpr CK_SLOT_ID kSlot = 42;
  constexpr char kUsername1[] = "foo@gmail.com";

  // Check the system token case.
  EXPECT_CALL(pkcs11_init_, GetTpmTokenSlotForPath(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(kSlot), Return(true)));
  info = userdataauth_->Pkcs11GetTpmTokenInfo("");

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
  info = userdataauth_->Pkcs11GetTpmTokenInfo("");
  EXPECT_EQ(info.slot(), -1);

  EXPECT_CALL(pkcs11_init_, GetTpmTokenSlotForPath(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(kSlot), Return(false)));
  info = userdataauth_->Pkcs11GetTpmTokenInfo(kUsername1);
  EXPECT_EQ(info.slot(), -1);
}

TEST_F(UserDataAuthTest, Pkcs11Terminate) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
  // Check that it'll not crash when there's no mount
  userdataauth_->Pkcs11Terminate();

  // Check that we'll indeed get the Mount object to remove the PKCS#11 token.
  constexpr char kUsername1[] = "foo@gmail.com";
  SetupMount(kUsername1);
  CreatePkcs11TokenInSession(mount_, session_);
  InitializePkcs11TokenInSession(mount_, session_);

  EXPECT_TRUE(session_->GetPkcs11Token()->IsReady());

  userdataauth_->Pkcs11Terminate();

  EXPECT_FALSE(session_->GetPkcs11Token()->IsReady());
}

TEST_F(UserDataAuthTest, Pkcs11RestoreTpmTokens) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
  // This test the most common success case for PKCS#11 retrieving TPM tokens.

  // Add a mount associated with foo@gmail.com
  SetupMount("foo@gmail.com");

  CreatePkcs11TokenInSession(mount_, session_);

  // PKCS#11 will initialization works only when it's mounted.
  ON_CALL(*mount_, IsMounted()).WillByDefault(Return(true));
  // The initialization code should at least check, right?
  EXPECT_CALL(*mount_, IsMounted())
      .Times(AtLeast(1))
      .WillRepeatedly(Return(true));

  EXPECT_FALSE(session_->GetPkcs11Token()->IsReady());

  userdataauth_->Pkcs11RestoreTpmTokens();

  EXPECT_TRUE(session_->GetPkcs11Token()->IsReady());
}

TEST_F(UserDataAuthTest, Pkcs11RestoreTpmTokensTpmNotOwned) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
  // This test the case for PKCS#11 retrieving TPM tokens when TPM isn't ready.

  // Add a mount associated with foo@gmail.com
  SetupMount("foo@gmail.com");

  CreatePkcs11TokenInSession(mount_, session_);

  // It shouldn't call any thing.
  EXPECT_CALL(*mount_, IsMounted()).Times(0);

  // TPM is enabled but not owned.
  ON_CALL(tpm_, IsEnabled()).WillByDefault(Return(true));
  EXPECT_CALL(tpm_, IsOwned()).Times(AtLeast(1)).WillRepeatedly(Return(false));

  EXPECT_FALSE(session_->GetPkcs11Token()->IsReady());

  userdataauth_->Pkcs11RestoreTpmTokens();

  EXPECT_FALSE(session_->GetPkcs11Token()->IsReady());
}

TEST_F(UserDataAuthTest, Pkcs11RestoreTpmTokensWaitingOnTPM) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
  // This test the most common success case for PKCS#11 retrieving TPM tokens
  // when it's waiting TPM ready.

  // Add a mount associated with foo@gmail.com
  SetupMount("foo@gmail.com");

  CreatePkcs11TokenInSession(mount_, session_);

  // PKCS#11 will initialization works only when it's mounted.
  ON_CALL(*mount_, IsMounted()).WillByDefault(Return(true));
  // The initialization code should at least check, right?
  EXPECT_CALL(*mount_, IsMounted())
      .Times(AtLeast(1))
      .WillRepeatedly(Return(true));

  EXPECT_FALSE(session_->GetPkcs11Token()->IsReady());

  userdataauth_->Pkcs11RestoreTpmTokens();

  EXPECT_TRUE(session_->GetPkcs11Token()->IsReady());
}

TEST_F(UserDataAuthTestNotInitialized, InstallAttributesEnterpriseOwned) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
  EXPECT_CALL(*attrs_, Init(_)).WillOnce(Return(true));

  std::string str_true = "true";
  std::vector<uint8_t> blob_true(str_true.begin(), str_true.end());
  blob_true.push_back(0);

  EXPECT_CALL(*attrs_, Get("enterprise.owned", _))
      .WillOnce(DoAll(SetArgPointee<1>(blob_true), Return(true)));

  InitializeUserDataAuth();

  EXPECT_TRUE(userdataauth_->IsEnterpriseOwned());
}

TEST_F(UserDataAuthTestNotInitialized, InstallAttributesNotEnterpriseOwned) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
  EXPECT_CALL(*attrs_, Init(_)).WillOnce(Return(true));

  std::string str_true = "false";
  std::vector<uint8_t> blob_true(str_true.begin(), str_true.end());
  blob_true.push_back(0);

  EXPECT_CALL(*attrs_, Get("enterprise.owned", _))
      .WillOnce(DoAll(SetArgPointee<1>(blob_true), Return(true)));

  InitializeUserDataAuth();

  EXPECT_FALSE(userdataauth_->IsEnterpriseOwned());
}

TEST_F(UserDataAuthTestNotInitialized, LowDiskSpaceHandlerInit) {
  // Both callbacks need to be set before Init.
  EXPECT_CALL(low_disk_space_handler_,
              SetUpdateUserActivityTimestampCallback(_));
  EXPECT_CALL(low_disk_space_handler_, SetLowDiskSpaceCallback(_));

  InitializeUserDataAuth();
}

constexpr char kInstallAttributeName[] = "SomeAttribute";
constexpr uint8_t kInstallAttributeData[] = {0x01, 0x02, 0x00,
                                             0x03, 0xFF, 0xAB};

TEST_F(UserDataAuthTest, InstallAttributesGet) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
  // Test for successful case.
  EXPECT_CALL(*attrs_, Get(kInstallAttributeName, _))
      .WillOnce(
          Invoke([](const std::string& name, std::vector<uint8_t>* data_out) {
            *data_out = std::vector<uint8_t>(
                kInstallAttributeData,
                kInstallAttributeData + sizeof(kInstallAttributeData));
            return true;
          }));
  std::vector<uint8_t> data;
  EXPECT_TRUE(
      userdataauth_->InstallAttributesGet(kInstallAttributeName, &data));
  EXPECT_THAT(data, ElementsAreArray(kInstallAttributeData));

  // Test for unsuccessful case.
  EXPECT_CALL(*attrs_, Get(kInstallAttributeName, _)).WillOnce(Return(false));
  EXPECT_FALSE(
      userdataauth_->InstallAttributesGet(kInstallAttributeName, &data));
}

TEST_F(UserDataAuthTest, InstallAttributesSet) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
  // Test for successful case.
  EXPECT_CALL(*attrs_, Set(kInstallAttributeName,
                           ElementsAreArray(kInstallAttributeData)))
      .WillOnce(Return(true));

  std::vector<uint8_t> data(
      kInstallAttributeData,
      kInstallAttributeData + sizeof(kInstallAttributeData));
  EXPECT_TRUE(userdataauth_->InstallAttributesSet(kInstallAttributeName, data));

  // Test for unsuccessful case.
  EXPECT_CALL(*attrs_, Set(kInstallAttributeName,
                           ElementsAreArray(kInstallAttributeData)))
      .WillOnce(Return(false));
  EXPECT_FALSE(
      userdataauth_->InstallAttributesSet(kInstallAttributeName, data));
}

TEST_F(UserDataAuthTest, InstallAttributesFinalize) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
  // Test for successful case.
  EXPECT_CALL(*attrs_, Finalize()).WillOnce(Return(true));
  EXPECT_TRUE(userdataauth_->InstallAttributesFinalize());

  // Test for unsuccessful case.
  EXPECT_CALL(*attrs_, Finalize()).WillOnce(Return(false));
  EXPECT_FALSE(userdataauth_->InstallAttributesFinalize());
}

TEST_F(UserDataAuthTest, InstallAttributesCount) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
  constexpr int kCount = 42;  // The Answer!!
  EXPECT_CALL(*attrs_, Count()).WillOnce(Return(kCount));
  EXPECT_EQ(kCount, userdataauth_->InstallAttributesCount());
}

TEST_F(UserDataAuthTest, InstallAttributesIsSecure) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
  // Test for successful case.
  EXPECT_CALL(*attrs_, is_secure()).WillOnce(Return(true));
  EXPECT_TRUE(userdataauth_->InstallAttributesIsSecure());

  // Test for unsuccessful case.
  EXPECT_CALL(*attrs_, is_secure()).WillOnce(Return(false));
  EXPECT_FALSE(userdataauth_->InstallAttributesIsSecure());
}

TEST_F(UserDataAuthTest, InstallAttributesGetStatus) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
  std::vector<InstallAttributes::Status> status_list = {
      InstallAttributes::Status::kUnknown,
      InstallAttributes::Status::kTpmNotOwned,
      InstallAttributes::Status::kFirstInstall,
      InstallAttributes::Status::kValid, InstallAttributes::Status::kInvalid};

  for (auto& s : status_list) {
    EXPECT_CALL(*attrs_, status()).WillOnce(Return(s));
    EXPECT_EQ(s, userdataauth_->InstallAttributesGetStatus());
  }
}

TEST_F(UserDataAuthTestNotInitialized, InstallAttributesStatusToProtoEnum) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
  EXPECT_EQ(user_data_auth::InstallAttributesState::UNKNOWN,
            UserDataAuth::InstallAttributesStatusToProtoEnum(
                InstallAttributes::Status::kUnknown));
  EXPECT_EQ(user_data_auth::InstallAttributesState::TPM_NOT_OWNED,
            UserDataAuth::InstallAttributesStatusToProtoEnum(
                InstallAttributes::Status::kTpmNotOwned));
  EXPECT_EQ(user_data_auth::InstallAttributesState::FIRST_INSTALL,
            UserDataAuth::InstallAttributesStatusToProtoEnum(
                InstallAttributes::Status::kFirstInstall));
  EXPECT_EQ(user_data_auth::InstallAttributesState::VALID,
            UserDataAuth::InstallAttributesStatusToProtoEnum(
                InstallAttributes::Status::kValid));
  EXPECT_EQ(user_data_auth::InstallAttributesState::INVALID,
            UserDataAuth::InstallAttributesStatusToProtoEnum(
                InstallAttributes::Status::kInvalid));
  static_assert(
      user_data_auth::InstallAttributesState_MAX == 4,
      "Incorrect element count in user_data_auth::InstallAttributesState");
  static_assert(static_cast<int>(InstallAttributes::Status::COUNT) == 5,
                "Incorrect element count in InstallAttributes::Status");
}

TEST_F(UserDataAuthTestNotInitialized, InitializeArcDiskQuota) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kOriginThread);
  EXPECT_CALL(arc_disk_quota_, Initialize()).Times(1);
  EXPECT_TRUE(userdataauth_->Initialize());
}

TEST_F(UserDataAuthTestNotInitialized, IsArcQuotaSupported) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kOriginThread);
  EXPECT_CALL(arc_disk_quota_, IsQuotaSupported()).WillOnce(Return(true));
  EXPECT_TRUE(userdataauth_->IsArcQuotaSupported());

  EXPECT_CALL(arc_disk_quota_, IsQuotaSupported()).WillOnce(Return(false));
  EXPECT_FALSE(userdataauth_->IsArcQuotaSupported());
}

TEST_F(UserDataAuthTestNotInitialized, GetCurrentSpaceFoArcUid) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kOriginThread);
  constexpr uid_t kUID = 42;  // The Answer.
  constexpr int64_t kSpaceUsage = 98765432198765;

  EXPECT_CALL(arc_disk_quota_, GetCurrentSpaceForUid(kUID))
      .WillOnce(Return(kSpaceUsage));
  EXPECT_EQ(kSpaceUsage, userdataauth_->GetCurrentSpaceForArcUid(kUID));
}

TEST_F(UserDataAuthTestNotInitialized, GetCurrentSpaceForArcGid) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kOriginThread);
  constexpr uid_t kGID = 42;  // Yet another answer.
  constexpr int64_t kSpaceUsage = 87654321987654;

  EXPECT_CALL(arc_disk_quota_, GetCurrentSpaceForGid(kGID))
      .WillOnce(Return(kSpaceUsage));
  EXPECT_EQ(kSpaceUsage, userdataauth_->GetCurrentSpaceForArcGid(kGID));
}

TEST_F(UserDataAuthTestNotInitialized, GetCurrentSpaceForArcProjectId) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kOriginThread);
  constexpr int kProjectId = 1001;  // Yet another answer.
  constexpr int64_t kSpaceUsage = 87654321987654;

  EXPECT_CALL(arc_disk_quota_, GetCurrentSpaceForProjectId(kProjectId))
      .WillOnce(Return(kSpaceUsage));
  EXPECT_EQ(kSpaceUsage,
            userdataauth_->GetCurrentSpaceForArcProjectId(kProjectId));
}

TEST_F(UserDataAuthTest, SetProjectId) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kOriginThread);
  constexpr int kProjectId = 1001;
  const base::FilePath kChildPath = base::FilePath("/child/path");
  constexpr char kUsername[] = "foo@gmail.com";
  cryptohome::AccountIdentifier account_id;
  account_id.set_account_id(kUsername);

  EXPECT_CALL(
      arc_disk_quota_,
      SetProjectId(kProjectId, SetProjectIdAllowedPathType::PATH_DOWNLOADS,
                   kChildPath, GetObfuscatedUsername(kUsername)))
      .WillOnce(Return(true));
  EXPECT_TRUE(userdataauth_->SetProjectId(
      kProjectId, user_data_auth::SetProjectIdAllowedPathType::PATH_DOWNLOADS,
      kChildPath, account_id));
}

TEST_F(UserDataAuthTest, SetMediaRWDataFileProjectId) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kOriginThread);
  constexpr int kProjectId = 1001;
  constexpr int kFd = 1234;
  int error = 0;

  EXPECT_CALL(arc_disk_quota_,
              SetMediaRWDataFileProjectId(kProjectId, kFd, &error))
      .WillOnce(Return(true));
  EXPECT_TRUE(
      userdataauth_->SetMediaRWDataFileProjectId(kProjectId, kFd, &error));
}

TEST_F(UserDataAuthTestNotInitialized, SeedUrandomInitialize) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kOriginThread);
  // Should Get Random from TPM
  EXPECT_CALL(tpm_, GetRandomDataBlob(kDefaultRandomSeedLength, _))
      .WillOnce(ReturnError<TPMErrorBase>());

  EXPECT_CALL(platform_, WriteFile(FilePath(kDefaultEntropySourcePath), _))
      .WillOnce(Return(true));

  EXPECT_TRUE(userdataauth_->Initialize());
}

TEST_F(UserDataAuthTest, LockToSingleUserMountUntilRebootValidity20) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kOriginThread);
  constexpr char kUsername1[] = "foo@gmail.com";
  cryptohome::AccountIdentifier account_id;
  account_id.set_account_id(kUsername1);
  const std::string kUsername1Obfuscated = GetObfuscatedUsername(kUsername1);

  // We'll test the TPM 2.0 case.
  ON_CALL(tpm_, GetVersion()).WillByDefault(Return(cryptohome::Tpm::TPM_2_0));

  EXPECT_CALL(homedirs_, SetLockedToSingleUser()).WillOnce(Return(true));
  brillo::Blob empty_pcr = brillo::Blob(32, 0);
  EXPECT_CALL(tpm_, ReadPCR(kTpmSingleUserPCR, _))
      .WillOnce(DoAll(SetArgPointee<1>(empty_pcr), Return(true)));
  brillo::Blob extention_blob(kUsername1Obfuscated.begin(),
                              kUsername1Obfuscated.end());
  EXPECT_CALL(tpm_, ExtendPCR(kTpmSingleUserPCR, extention_blob))
      .WillOnce(Return(true));

  EXPECT_EQ(userdataauth_->LockToSingleUserMountUntilReboot(account_id),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
}

TEST_F(UserDataAuthTest, LockToSingleUserMountUntilRebootValidity12) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kOriginThread);
  constexpr char kUsername1[] = "foo@gmail.com";
  cryptohome::AccountIdentifier account_id;
  account_id.set_account_id(kUsername1);
  const std::string kUsername1Obfuscated = GetObfuscatedUsername(kUsername1);

  // We'll test the TPM 1.2 case.
  ON_CALL(tpm_, GetVersion()).WillByDefault(Return(cryptohome::Tpm::TPM_1_2));

  EXPECT_CALL(homedirs_, SetLockedToSingleUser()).WillOnce(Return(true));
  brillo::Blob empty_pcr = brillo::Blob(32, 0);
  EXPECT_CALL(tpm_, ReadPCR(kTpmSingleUserPCR, _))
      .WillOnce(DoAll(SetArgPointee<1>(empty_pcr), Return(true)));
  brillo::Blob extention_blob(kUsername1Obfuscated.begin(),
                              kUsername1Obfuscated.end());
  extention_blob = Sha1(extention_blob);
  EXPECT_CALL(tpm_, ExtendPCR(kTpmSingleUserPCR, extention_blob))
      .WillOnce(Return(true));

  EXPECT_EQ(userdataauth_->LockToSingleUserMountUntilReboot(account_id),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
}

TEST_F(UserDataAuthTest, LockToSingleUserMountUntilRebootReadPCRFail) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kOriginThread);
  constexpr char kUsername1[] = "foo@gmail.com";
  cryptohome::AccountIdentifier account_id;
  account_id.set_account_id(kUsername1);

  ON_CALL(homedirs_, SetLockedToSingleUser()).WillByDefault(Return(true));
  EXPECT_CALL(tpm_, ReadPCR(kTpmSingleUserPCR, _)).WillOnce(Return(false));

  EXPECT_EQ(userdataauth_->LockToSingleUserMountUntilReboot(account_id),
            user_data_auth::CRYPTOHOME_ERROR_FAILED_TO_READ_PCR);
}

TEST_F(UserDataAuthTest, LockToSingleUserMountUntilRebootAlreadyExtended) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kOriginThread);
  constexpr char kUsername1[] = "foo@gmail.com";
  cryptohome::AccountIdentifier account_id;
  account_id.set_account_id(kUsername1);

  // We'll test the TPM 2.0 case.
  ON_CALL(tpm_, GetVersion()).WillByDefault(Return(cryptohome::Tpm::TPM_2_0));

  ON_CALL(homedirs_, SetLockedToSingleUser()).WillByDefault(Return(true));
  brillo::Blob empty_pcr =
      brillo::Blob(32, 0x42);  // Incorrect PCR value, should cause it to fail.
  EXPECT_CALL(tpm_, ReadPCR(kTpmSingleUserPCR, _))
      .WillOnce(DoAll(SetArgPointee<1>(empty_pcr), Return(true)));

  EXPECT_EQ(userdataauth_->LockToSingleUserMountUntilReboot(account_id),
            user_data_auth::CRYPTOHOME_ERROR_PCR_ALREADY_EXTENDED);
}

TEST_F(UserDataAuthTest, LockToSingleUserMountUntilRebootExtendFail) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kOriginThread);
  constexpr char kUsername1[] = "foo@gmail.com";
  cryptohome::AccountIdentifier account_id;
  account_id.set_account_id(kUsername1);
  const std::string kUsername1Obfuscated = GetObfuscatedUsername(kUsername1);

  // We'll test the TPM 2.0 case.
  ON_CALL(tpm_, GetVersion()).WillByDefault(Return(cryptohome::Tpm::TPM_2_0));

  EXPECT_CALL(homedirs_, SetLockedToSingleUser()).WillOnce(Return(true));
  brillo::Blob empty_pcr = brillo::Blob(32, 0);
  EXPECT_CALL(tpm_, ReadPCR(kTpmSingleUserPCR, _))
      .WillOnce(DoAll(SetArgPointee<1>(empty_pcr), Return(true)));
  brillo::Blob extention_blob(kUsername1Obfuscated.begin(),
                              kUsername1Obfuscated.end());
  EXPECT_CALL(tpm_, ExtendPCR(kTpmSingleUserPCR, extention_blob))
      .WillOnce(Return(false));

  EXPECT_EQ(userdataauth_->LockToSingleUserMountUntilReboot(account_id),
            user_data_auth::CRYPTOHOME_ERROR_FAILED_TO_EXTEND_PCR);
}

// ================== Firmware Management Parameters tests ==================

TEST_F(UserDataAuthTest, GetFirmwareManagementParametersSuccess) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kOriginThread);
  const std::string kHash = "its_a_hash";
  std::vector<uint8_t> hash(kHash.begin(), kHash.end());
  constexpr uint32_t kFlag = 0x1234;

  EXPECT_CALL(fwmp_, Load()).WillOnce(Return(true));
  EXPECT_CALL(fwmp_, GetFlags(_))
      .WillRepeatedly(DoAll(SetArgPointee<0>(kFlag), Return(true)));
  EXPECT_CALL(fwmp_, GetDeveloperKeyHash(_))
      .WillRepeatedly(DoAll(SetArgPointee<0>(hash), Return(true)));

  user_data_auth::FirmwareManagementParameters fwmp;
  EXPECT_EQ(user_data_auth::CRYPTOHOME_ERROR_NOT_SET,
            userdataauth_->GetFirmwareManagementParameters(&fwmp));

  EXPECT_EQ(kFlag, fwmp.flags());
  EXPECT_EQ(kHash, fwmp.developer_key_hash());
}

TEST_F(UserDataAuthTest, GetFirmwareManagementParametersError) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kOriginThread);
  constexpr uint32_t kFlag = 0x1234;

  // Test Load() fail.
  EXPECT_CALL(fwmp_, Load()).WillRepeatedly(Return(false));

  user_data_auth::FirmwareManagementParameters fwmp;
  EXPECT_EQ(
      user_data_auth::CRYPTOHOME_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_INVALID,
      userdataauth_->GetFirmwareManagementParameters(&fwmp));

  // Test GetFlags() fail.
  EXPECT_CALL(fwmp_, Load()).WillRepeatedly(Return(true));
  EXPECT_CALL(fwmp_, GetFlags(_)).WillRepeatedly(Return(false));

  EXPECT_EQ(
      user_data_auth::CRYPTOHOME_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_INVALID,
      userdataauth_->GetFirmwareManagementParameters(&fwmp));

  // Test GetDeveloperKeyHash fail.
  EXPECT_CALL(fwmp_, Load()).WillRepeatedly(Return(true));
  EXPECT_CALL(fwmp_, GetFlags(_))
      .WillRepeatedly(DoAll(SetArgPointee<0>(kFlag), Return(true)));
  EXPECT_CALL(fwmp_, GetDeveloperKeyHash(_)).WillRepeatedly(Return(false));

  EXPECT_EQ(
      user_data_auth::CRYPTOHOME_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_INVALID,
      userdataauth_->GetFirmwareManagementParameters(&fwmp));
}

TEST_F(UserDataAuthTest, SetFirmwareManagementParametersSuccess) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kOriginThread);
  const std::string kHash = "its_a_hash";
  std::vector<uint8_t> hash(kHash.begin(), kHash.end());
  constexpr uint32_t kFlag = 0x1234;

  std::vector<uint8_t> out_hash;

  EXPECT_CALL(fwmp_, Create()).WillOnce(Return(true));
  EXPECT_CALL(fwmp_, Store(kFlag, _))
      .WillOnce(DoAll(SaveArgPointee<1>(&out_hash), Return(true)));

  user_data_auth::FirmwareManagementParameters fwmp;
  fwmp.set_flags(kFlag);
  fwmp.set_developer_key_hash(kHash);

  EXPECT_EQ(user_data_auth::CRYPTOHOME_ERROR_NOT_SET,
            userdataauth_->SetFirmwareManagementParameters(fwmp));

  EXPECT_EQ(hash, out_hash);
}

TEST_F(UserDataAuthTest, SetFirmwareManagementParametersNoHash) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kOriginThread);
  constexpr uint32_t kFlag = 0x1234;

  EXPECT_CALL(fwmp_, Create()).WillOnce(Return(true));
  EXPECT_CALL(fwmp_, Store(kFlag, nullptr)).WillOnce(Return(true));

  user_data_auth::FirmwareManagementParameters fwmp;
  fwmp.set_flags(kFlag);

  EXPECT_EQ(user_data_auth::CRYPTOHOME_ERROR_NOT_SET,
            userdataauth_->SetFirmwareManagementParameters(fwmp));
}

TEST_F(UserDataAuthTest, SetFirmwareManagementParametersCreateError) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kOriginThread);
  const std::string kHash = "its_a_hash";
  constexpr uint32_t kFlag = 0x1234;

  EXPECT_CALL(fwmp_, Create()).WillOnce(Return(false));

  user_data_auth::FirmwareManagementParameters fwmp;
  fwmp.set_flags(kFlag);
  fwmp.set_developer_key_hash(kHash);

  EXPECT_EQ(user_data_auth::
                CRYPTOHOME_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_CANNOT_STORE,
            userdataauth_->SetFirmwareManagementParameters(fwmp));
}

TEST_F(UserDataAuthTest, SetFirmwareManagementParametersStoreError) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kOriginThread);
  const std::string kHash = "its_a_hash";
  constexpr uint32_t kFlag = 0x1234;

  EXPECT_CALL(fwmp_, Create()).WillOnce(Return(true));
  EXPECT_CALL(fwmp_, Store(_, _)).WillOnce(Return(false));

  user_data_auth::FirmwareManagementParameters fwmp;
  fwmp.set_flags(kFlag);
  fwmp.set_developer_key_hash(kHash);

  EXPECT_EQ(user_data_auth::
                CRYPTOHOME_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_CANNOT_STORE,
            userdataauth_->SetFirmwareManagementParameters(fwmp));
}

TEST_F(UserDataAuthTest, RemoveFirmwareManagementParametersSuccess) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kOriginThread);
  EXPECT_CALL(fwmp_, Destroy()).WillOnce(Return(true));

  EXPECT_TRUE(userdataauth_->RemoveFirmwareManagementParameters());
}

TEST_F(UserDataAuthTest, RemoveFirmwareManagementParametersError) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kOriginThread);
  EXPECT_CALL(fwmp_, Destroy()).WillOnce(Return(false));

  EXPECT_FALSE(userdataauth_->RemoveFirmwareManagementParameters());
}

TEST_F(UserDataAuthTest, GetSystemSaltSucess) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kOriginThread);
  brillo::SecureBlob salt;
  AssignSalt(&salt);
  EXPECT_EQ(salt, userdataauth_->GetSystemSalt());
}

TEST_F(UserDataAuthTestNotInitializedDeathTest, GetSystemSaltUninitialized) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kOriginThread);
  EXPECT_DEBUG_DEATH(userdataauth_->GetSystemSalt(),
                     "Cannot call GetSystemSalt before initialization");
}

TEST_F(UserDataAuthTest, OwnershipCallbackRegisterValidity) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
  base::RepeatingCallback<void()> callback;

  // Called by PostDBusInitialize().
  EXPECT_CALL(tpm_manager_utility_, AddOwnershipCallback)
      .WillOnce(SaveArg<0>(&callback));

  InitializeUserDataAuth();

  EXPECT_FALSE(callback.is_null());

  SetupMount("foo@gmail.com");

  // Called by ResetAllTPMContext().
  EXPECT_CALL(crypto_, EnsureTpm(true)).WillOnce(Return(CryptoError::CE_NONE));
  // Called by InitializeInstallAttributes()
  EXPECT_CALL(*attrs_, Init(_)).WillOnce(Return(true));

  callback.Run();
}

TEST_F(UserDataAuthTest, OwnershipCallbackRegisterRepeated) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
  base::RepeatingCallback<void()> callback;

  // Called by PostDBusInitialize().
  EXPECT_CALL(tpm_manager_utility_, AddOwnershipCallback)
      .WillOnce(SaveArg<0>(&callback));

  InitializeUserDataAuth();

  EXPECT_FALSE(callback.is_null());

  SetupMount("foo@gmail.com");

  // Called by ResetAllTPMContext().
  EXPECT_CALL(crypto_, EnsureTpm(true)).WillOnce(Return(CryptoError::CE_NONE));
  // Called by InitializeInstallAttributes()
  EXPECT_CALL(*attrs_, Init(_)).WillOnce(Return(true));

  // Call OwnershipCallback twice and see if any of the above gets called more
  // than once.
  callback.Run();
  callback.Run();
}

TEST_F(UserDataAuthTest, UpdateCurrentUserActivityTimestampSuccess) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
  constexpr int kTimeshift = 5;

  // Test case for single mount
  SetupMount("foo@gmail.com");

  EXPECT_CALL(*mount_, IsNonEphemeralMounted()).WillRepeatedly(Return(true));
  EXPECT_CALL(user_activity_timestamp_manager_,
              UpdateTimestamp(_, base::TimeDelta::FromSeconds(kTimeshift)))
      .WillOnce(Return(true));

  EXPECT_TRUE(userdataauth_->UpdateCurrentUserActivityTimestamp(kTimeshift));

  // Test case for multiple mounts
  scoped_refptr<MockMount> prev_mount = mount_;
  SetupMount("bar@gmail.com");

  EXPECT_CALL(*mount_, IsNonEphemeralMounted()).WillRepeatedly(Return(true));
  EXPECT_CALL(user_activity_timestamp_manager_,
              UpdateTimestamp(_, base::TimeDelta::FromSeconds(kTimeshift)))
      .WillOnce(Return(true))
      .WillOnce(Return(true));

  EXPECT_TRUE(userdataauth_->UpdateCurrentUserActivityTimestamp(kTimeshift));
}

TEST_F(UserDataAuthTest, UpdateCurrentUserActivityTimestampFailure) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
  constexpr int kTimeshift = 5;

  // Test case for single mount
  SetupMount("foo@gmail.com");

  EXPECT_CALL(*mount_, IsNonEphemeralMounted()).WillRepeatedly(Return(true));
  EXPECT_CALL(user_activity_timestamp_manager_,
              UpdateTimestamp(_, base::TimeDelta::FromSeconds(kTimeshift)))
      .WillOnce(Return(false));

  EXPECT_FALSE(userdataauth_->UpdateCurrentUserActivityTimestamp(kTimeshift));
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
const std::vector<Platform::LoopDevice> kLoopDevices = {
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
      if (mounts)
        mounts->insert(std::make_pair(m.src, m.dst));
    }
  }
  return i > 0;
}

bool DmcryptDeviceMounts(
    const std::string& from_prefix,
    std::multimap<const FilePath, const FilePath>* mounts) {
  if (!mounts)
    return false;
  for (const auto& m : kDmcryptMounts)
    mounts->insert(std::make_pair(m.src, m.dst));
  return true;
}

bool LoopDeviceMounts(std::multimap<const FilePath, const FilePath>* mounts) {
  if (!mounts)
    return false;
  for (const auto& m : kLoopDevMounts)
    mounts->insert(std::make_pair(m.src, m.dst));
  return true;
}

bool EnumerateSparseFiles(const base::FilePath& path,
                          bool is_recursive,
                          std::vector<base::FilePath>* ent_list) {
  if (path != FilePath(kEphemeralCryptohomeDir).Append(kSparseFileDir))
    return false;
  ent_list->insert(ent_list->begin(), kSparseFiles.begin(), kSparseFiles.end());
  return true;
}

}  // namespace

TEST_F(UserDataAuthTest, CleanUpStale_NoOpenFiles_Dmcrypt) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
  // Check that when we have dm-crypt mounts, no active mounts,
  // and no open filehandles, all stale mounts are unmounted.

  EXPECT_CALL(platform_, GetMountsByDevicePrefix("/dev/mapper/dmcrypt", _))
      .WillOnce(Invoke(DmcryptDeviceMounts));

  EXPECT_CALL(platform_, ExpireMount(_))
      .Times(kDmcryptMounts.size())
      .WillRepeatedly(Return(ExpireMountResult::kMarked));

  for (int i = 0; i < kDmcryptMounts.size(); ++i) {
    EXPECT_CALL(platform_, Unmount(kDmcryptMounts[i].dst, true, _))
        .WillRepeatedly(Return(true));
  }

  EXPECT_FALSE(userdataauth_->CleanUpStaleMounts(false));
}

TEST_F(UserDataAuthTest, CleanUpStale_OpenFiles_Dmcrypt) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
  // Check that when we have dm-crypt mounts, files open on dm-crypt cryptohome
  // for one user and no open filehandles, all stale mounts for the second user
  // are unmounted.
  EXPECT_CALL(platform_, GetMountsByDevicePrefix("/dev/mapper/dmcrypt", _))
      .WillOnce(Invoke(DmcryptDeviceMounts));

  // The number of expired mounts depends on when the first busy mount is
  // traversed through. In this case, /home/chronos/user is the 3rd mount in
  // the list, so ExpireMount() is called for the first two non-busy mounts for
  // user 1234 and then for the non-busy stale mounts for user 4567.
  const int kBusyMountIndex = 4;
  EXPECT_CALL(platform_, ExpireMount(_))
      .Times(kBusyMountIndex)
      .WillRepeatedly(Return(ExpireMountResult::kMarked));

  EXPECT_CALL(platform_, ExpireMount(kDmcryptMounts[kBusyMountIndex].dst))
      .Times(1)
      .WillRepeatedly(Return(ExpireMountResult::kBusy));

  // Only user 4567's mounts will be unmounted.
  for (int i = 0; i < 2; ++i) {
    EXPECT_CALL(platform_, Unmount(kDmcryptMounts[i].dst, true, _))
        .WillRepeatedly(Return(true));
  }

  EXPECT_TRUE(userdataauth_->CleanUpStaleMounts(false));
}

TEST_F(UserDataAuthTest, CleanUpStale_OpenFiles_Dmcrypt_Forced) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
  // Check that when we have dm-crypt mounts, files open on dm-crypt
  // and no open filehandles, all stale mounts are unmounted.

  EXPECT_CALL(platform_, GetMountsByDevicePrefix("/dev/mapper/dmcrypt", _))
      .WillOnce(Invoke(DmcryptDeviceMounts));
  EXPECT_CALL(platform_, ExpireMount(_)).Times(0);

  for (int i = 0; i < kDmcryptMounts.size(); ++i) {
    EXPECT_CALL(platform_, Unmount(kDmcryptMounts[i].dst, true, _))
        .WillRepeatedly(Return(true));
  }

  EXPECT_FALSE(userdataauth_->CleanUpStaleMounts(true));
}

TEST_F(UserDataAuthTest, CleanUpStale_NoOpenFiles_Ephemeral) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
  // Check that when we have ephemeral mounts, no active mounts,
  // and no open filehandles, all stale mounts are unmounted, loop device is
  // detached and sparse file is deleted.

  EXPECT_CALL(platform_, GetMountsBySourcePrefix(_, _)).WillOnce(Return(false));
  EXPECT_CALL(platform_, GetAttachedLoopDevices())
      .WillRepeatedly(Return(kLoopDevices));
  EXPECT_CALL(platform_, GetLoopDeviceMounts(_))
      .WillOnce(Invoke(LoopDeviceMounts));
  EXPECT_CALL(
      platform_,
      EnumerateDirectoryEntries(
          FilePath(kEphemeralCryptohomeDir).Append(kSparseFileDir), _, _))
      .WillOnce(Invoke(EnumerateSparseFiles));
  EXPECT_CALL(platform_, ExpireMount(_))
      .Times(kEphemeralMountsCount)
      .WillRepeatedly(Return(ExpireMountResult::kMarked));

  for (int i = 0; i < kEphemeralMountsCount; ++i) {
    EXPECT_CALL(platform_, Unmount(kLoopDevMounts[i].dst, true, _))
        .WillRepeatedly(Return(true));
  }
  EXPECT_CALL(platform_, DetachLoop(FilePath("/dev/loop7")))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, DeleteFile(kSparseFiles[0])).WillOnce(Return(true));
  EXPECT_CALL(platform_, DeleteFile(kSparseFiles[1])).WillOnce(Return(true));
  EXPECT_CALL(platform_, DeletePathRecursively(kLoopDevMounts[0].dst))
      .WillOnce(Return(true));
  EXPECT_FALSE(userdataauth_->CleanUpStaleMounts(false));
}

TEST_F(UserDataAuthTest, CleanUpStale_OpenLegacy_Ephemeral) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
  // Check that when we have ephemeral mounts, no active mounts,
  // and some open filehandles to the legacy homedir, everything is kept.

  EXPECT_CALL(platform_, GetMountsBySourcePrefix(_, _)).WillOnce(Return(false));
  EXPECT_CALL(platform_, GetAttachedLoopDevices())
      .WillRepeatedly(Return(kLoopDevices));
  EXPECT_CALL(platform_, GetLoopDeviceMounts(_))
      .WillOnce(Invoke(LoopDeviceMounts));
  EXPECT_CALL(
      platform_,
      EnumerateDirectoryEntries(
          FilePath(kEphemeralCryptohomeDir).Append(kSparseFileDir), _, _))
      .WillOnce(Invoke(EnumerateSparseFiles));
  EXPECT_CALL(platform_, ExpireMount(_))
      .Times(kEphemeralMountsCount - 1)
      .WillRepeatedly(Return(ExpireMountResult::kMarked));
  EXPECT_CALL(platform_, ExpireMount(FilePath("/home/chronos/user")))
      .Times(1)
      .WillRepeatedly(Return(ExpireMountResult::kBusy));

  EXPECT_CALL(platform_, GetMountsBySourcePrefix(FilePath("/dev/loop7"), _))
      .WillOnce(Return(false));

  EXPECT_CALL(platform_, Unmount(_, _, _)).Times(0);
  EXPECT_TRUE(userdataauth_->CleanUpStaleMounts(false));
}

TEST_F(UserDataAuthTest, CleanUpStale_OpenLegacy_Ephemeral_Forced) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
  // Check that when we have ephemeral mounts, no active mounts,
  // and some open filehandles to the legacy homedir, but cleanup is forced,
  // all mounts are unmounted, loop device is detached and file is deleted.

  EXPECT_CALL(platform_, GetMountsBySourcePrefix(_, _)).WillOnce(Return(false));
  EXPECT_CALL(platform_, GetAttachedLoopDevices())
      .WillRepeatedly(Return(kLoopDevices));
  EXPECT_CALL(platform_, GetLoopDeviceMounts(_))
      .WillOnce(Invoke(LoopDeviceMounts));
  EXPECT_CALL(
      platform_,
      EnumerateDirectoryEntries(
          FilePath(kEphemeralCryptohomeDir).Append(kSparseFileDir), _, _))
      .WillOnce(Invoke(EnumerateSparseFiles));
  EXPECT_CALL(platform_, ExpireMount(_)).Times(0);

  for (int i = 0; i < kEphemeralMountsCount; ++i) {
    EXPECT_CALL(platform_, Unmount(kLoopDevMounts[i].dst, true, _))
        .WillRepeatedly(Return(true));
  }
  EXPECT_CALL(platform_, DetachLoop(FilePath("/dev/loop7")))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, DeleteFile(kSparseFiles[0])).WillOnce(Return(true));
  EXPECT_CALL(platform_, DeleteFile(kSparseFiles[1])).WillOnce(Return(true));
  EXPECT_CALL(platform_, DeletePathRecursively(kLoopDevMounts[0].dst))
      .WillOnce(Return(true));
  EXPECT_FALSE(userdataauth_->CleanUpStaleMounts(true));
}

TEST_F(UserDataAuthTest, CleanUpStale_EmptyMap_NoOpenFiles_ShadowOnly) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
  // Check that when we have a bunch of stale shadow mounts, no active mounts,
  // and no open filehandles, all stale mounts are unmounted.

  EXPECT_CALL(platform_, GetMountsBySourcePrefix(_, _))
      .WillOnce(Invoke(StaleShadowMounts));
  EXPECT_CALL(platform_, GetAttachedLoopDevices())
      .WillRepeatedly(Return(std::vector<Platform::LoopDevice>()));
  EXPECT_CALL(platform_, GetLoopDeviceMounts(_)).WillOnce(Return(false));
  EXPECT_CALL(
      platform_,
      EnumerateDirectoryEntries(
          FilePath(kEphemeralCryptohomeDir).Append(kSparseFileDir), _, _))
      .WillOnce(Return(false));
  EXPECT_CALL(platform_, ExpireMount(_))
      .Times(kShadowMounts.size())
      .WillRepeatedly(Return(ExpireMountResult::kMarked));
  EXPECT_CALL(platform_, Unmount(_, true, _))
      .Times(kShadowMounts.size())
      .WillRepeatedly(Return(true));
  EXPECT_FALSE(userdataauth_->CleanUpStaleMounts(false));
}

TEST_F(UserDataAuthTest, CleanUpStale_EmptyMap_NoOpenFiles_ShadowOnly_Forced) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
  // Check that when we have a bunch of stale shadow mounts, no active mounts,
  // and no open filehandles, all stale mounts are unmounted and we attempt
  // to clear the encryption key for fscrypt/ecryptfs mounts.

  EXPECT_CALL(platform_, GetMountsBySourcePrefix(_, _))
      .WillOnce(Invoke(StaleShadowMounts));
  EXPECT_CALL(platform_, GetAttachedLoopDevices())
      .WillRepeatedly(Return(std::vector<Platform::LoopDevice>()));
  EXPECT_CALL(platform_, GetLoopDeviceMounts(_)).WillOnce(Return(false));
  EXPECT_CALL(
      platform_,
      EnumerateDirectoryEntries(
          FilePath(kEphemeralCryptohomeDir).Append(kSparseFileDir), _, _))
      .WillOnce(Return(false));
  EXPECT_CALL(platform_, Unmount(_, true, _))
      .Times(kShadowMounts.size())
      .WillRepeatedly(Return(true));

  // Expect the cleanup to clear user keys.
  EXPECT_CALL(platform_, ClearUserKeyring()).WillOnce(Return(true));
  EXPECT_CALL(platform_, InvalidateDirCryptoKey(_, _))
    .Times(kShadowMounts.size())
    .WillRepeatedly(Return(true));

  EXPECT_FALSE(userdataauth_->CleanUpStaleMounts(true));
}

TEST_F(UserDataAuthTest, CleanUpStale_EmptyMap_OpenLegacy_ShadowOnly) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
  // Check that when we have a bunch of stale shadow mounts, no active mounts,
  // and some open filehandles to the legacy homedir, all mounts without
  // filehandles are unmounted.

  // Called by CleanUpStaleMounts and each time a directory is excluded.
  EXPECT_CALL(platform_, GetMountsBySourcePrefix(_, _))
      .Times(4)
      .WillRepeatedly(Invoke(StaleShadowMounts));
  EXPECT_CALL(platform_, GetAttachedLoopDevices())
      .WillRepeatedly(Return(std::vector<Platform::LoopDevice>()));
  EXPECT_CALL(platform_, GetLoopDeviceMounts(_)).WillOnce(Return(false));
  EXPECT_CALL(
      platform_,
      EnumerateDirectoryEntries(
          FilePath(kEphemeralCryptohomeDir).Append(kSparseFileDir), _, _))
      .WillOnce(Return(false));
  EXPECT_CALL(platform_,
              ExpireMount(Property(&FilePath::value, EndsWith("/0"))))
      .WillRepeatedly(Return(ExpireMountResult::kBusy));
  EXPECT_CALL(platform_, ExpireMount(FilePath("/home/chronos/user")))
      .WillRepeatedly(Return(ExpireMountResult::kBusy));
  EXPECT_CALL(platform_,
              ExpireMount(Property(
                  &FilePath::value,
                  AnyOf(EndsWith("/1"), EndsWith("b/MyFiles/Downloads")))))
      .Times(4)
      .WillRepeatedly(Return(ExpireMountResult::kMarked));
  EXPECT_CALL(platform_, ExpireMount(FilePath("/daemon-store/server/b")))
      .WillOnce(Return(ExpireMountResult::kMarked));
  // Given /home/chronos/user and a is marked as active, only b mounts should be
  // removed.
  EXPECT_CALL(
      platform_,
      Unmount(Property(&FilePath::value,
                       AnyOf(EndsWith("/1"), EndsWith("b/MyFiles/Downloads"))),
              true, _))
      .Times(4)
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, Unmount(FilePath("/daemon-store/server/b"), true, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_,
              Unmount(Property(&FilePath::value, EndsWith("/0")), true, _))
      .Times(0);
  EXPECT_CALL(platform_, Unmount(FilePath("/home/chronos/user"), true, _))
      .Times(0);
  EXPECT_TRUE(userdataauth_->CleanUpStaleMounts(false));
}

TEST_F(UserDataAuthTest, CleanUpStale_FilledMap_NoOpenFiles_ShadowOnly) {
  // Checks that when we have a bunch of stale shadow mounts, some active
  // mounts, and no open filehandles, all inactive mounts are unmounted.

  // ownership handed off to the Service MountMap
  MockMountFactory mount_factory;
  MockMount* mount = new MockMount();
  EXPECT_CALL(mount_factory, New(_, _, _, _, _)).WillOnce(Return(mount));
  userdataauth_->set_mount_factory(&mount_factory);
  EXPECT_CALL(platform_, FileExists(_)).WillOnce(Return(true));
  EXPECT_CALL(platform_, GetMountsBySourcePrefix(_, _)).WillOnce(Return(false));
  EXPECT_CALL(platform_, GetAttachedLoopDevices())
      .WillRepeatedly(Return(std::vector<Platform::LoopDevice>()));
  EXPECT_CALL(platform_, GetLoopDeviceMounts(_)).WillOnce(Return(false));

  InitializeUserDataAuth();

  EXPECT_CALL(homedirs_, CryptohomeExists(_, _)).WillOnce(Return(true));
  auto vk = std::make_unique<VaultKeyset>();
  EXPECT_CALL(keyset_management_, GetValidKeyset(_, _))
      .WillOnce(Return(ByMove(std::move(vk))));
  EXPECT_CALL(disk_cleanup_, FreeDiskSpaceDuringLogin(_));
  EXPECT_CALL(*mount, MountCryptohome(_, _, _))
      .WillOnce(Return(MOUNT_ERROR_NONE));
  EXPECT_CALL(platform_, GetMountsBySourcePrefix(_, _)).WillOnce(Return(false));
  EXPECT_CALL(platform_, GetAttachedLoopDevices())
      .WillRepeatedly(Return(std::vector<Platform::LoopDevice>()));
  EXPECT_CALL(platform_, GetLoopDeviceMounts(_)).WillOnce(Return(false));

  user_data_auth::MountRequest mount_req;
  mount_req.mutable_account()->set_account_id("foo@bar.net");
  mount_req.mutable_authorization()->mutable_key()->set_secret("key");
  mount_req.mutable_authorization()->mutable_key()->mutable_data()->set_label(
      "password");
  mount_req.mutable_create()->set_copy_authorization_key(true);
  bool mount_done = false;
  {
    TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
    userdataauth_->DoMount(
        mount_req,
        base::BindOnce(
            [](bool* mount_done_ptr, const user_data_auth::MountReply& reply) {
              EXPECT_EQ(user_data_auth::CRYPTOHOME_ERROR_NOT_SET,
                        reply.error());
              *mount_done_ptr = true;
            },
            base::Unretained(&mount_done)));
    ASSERT_EQ(TRUE, mount_done);
  }

  EXPECT_CALL(platform_, GetMountsBySourcePrefix(_, _))
      .Times(4)
      .WillRepeatedly(Invoke(StaleShadowMounts));
  EXPECT_CALL(platform_, GetAttachedLoopDevices())
      .WillRepeatedly(Return(std::vector<Platform::LoopDevice>()));
  EXPECT_CALL(platform_, GetLoopDeviceMounts(_)).WillOnce(Return(false));
  EXPECT_CALL(
      platform_,
      EnumerateDirectoryEntries(
          FilePath(kEphemeralCryptohomeDir).Append(kSparseFileDir), _, _))
      .WillOnce(Return(false));
  // Only 5 look ups: user/1 and root/1 are owned, children of these
  // directories are excluded.
  EXPECT_CALL(platform_, ExpireMount(_))
      .Times(5)
      .WillRepeatedly(Return(ExpireMountResult::kMarked));

  EXPECT_CALL(*mount, OwnsMountPoint(_)).WillRepeatedly(Return(false));
  EXPECT_CALL(*mount, OwnsMountPoint(FilePath("/home/user/1")))
      .WillOnce(Return(true));
  EXPECT_CALL(*mount, OwnsMountPoint(FilePath("/home/root/1")))
      .WillOnce(Return(true));

  EXPECT_CALL(platform_,
              Unmount(Property(&FilePath::value, EndsWith("/0")), true, _))
      .Times(2)
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, Unmount(FilePath("/home/chronos/user"), true, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, Unmount(Property(&FilePath::value,
                                          EndsWith("user/MyFiles/Downloads")),
                                 true, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, Unmount(FilePath("/daemon-store/server/a"), true, _))
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
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
  EXPECT_TRUE(userdataauth_->CleanUpStaleMounts(false));
}

TEST_F(UserDataAuthTest,
       CleanUpStale_FilledMap_NoOpenFiles_ShadowOnly_FirstBoot) {
  // Checks that when we have a bunch of stale shadow mounts, some active
  // mounts, and no open filehandles, all inactive mounts are unmounted.

  // ownership handed off to the Service MountMap
  MockMountFactory mount_factory;
  MockMount* mount = new MockMount();
  EXPECT_CALL(mount_factory, New(_, _, _, _, _)).WillOnce(Return(mount));
  userdataauth_->set_mount_factory(&mount_factory);
  EXPECT_CALL(platform_, FileExists(_)).WillOnce(Return(false));
  EXPECT_CALL(platform_, GetMountsBySourcePrefix(_, _)).Times(0);
  EXPECT_CALL(platform_, GetAttachedLoopDevices()).Times(0);
  EXPECT_CALL(platform_, GetLoopDeviceMounts(_)).Times(0);

  InitializeUserDataAuth();

  EXPECT_CALL(homedirs_, CryptohomeExists(_, _)).WillOnce(Return(true));
  auto vk = std::make_unique<VaultKeyset>();
  EXPECT_CALL(keyset_management_, GetValidKeyset(_, _))
      .WillOnce(Return(ByMove(std::move(vk))));
  EXPECT_CALL(disk_cleanup_, FreeDiskSpaceDuringLogin(_));
  EXPECT_CALL(*mount, MountCryptohome(_, _, _))
      .WillOnce(Return(MOUNT_ERROR_NONE));
  EXPECT_CALL(platform_, GetMountsBySourcePrefix(_, _)).WillOnce(Return(false));
  EXPECT_CALL(platform_, GetAttachedLoopDevices())
      .WillRepeatedly(Return(std::vector<Platform::LoopDevice>()));
  EXPECT_CALL(platform_, GetLoopDeviceMounts(_)).WillOnce(Return(false));

  user_data_auth::MountRequest mount_req;
  mount_req.mutable_account()->set_account_id("foo@bar.net");
  mount_req.mutable_authorization()->mutable_key()->set_secret("key");
  mount_req.mutable_authorization()->mutable_key()->mutable_data()->set_label(
      "password");
  mount_req.mutable_create()->set_copy_authorization_key(true);
  bool mount_done = false;
  {
    TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
    userdataauth_->DoMount(
        mount_req,
        base::BindOnce(
            [](bool* mount_done_ptr, const user_data_auth::MountReply& reply) {
              EXPECT_EQ(user_data_auth::CRYPTOHOME_ERROR_NOT_SET,
                        reply.error());
              *mount_done_ptr = true;
            },
            base::Unretained(&mount_done)));
    ASSERT_EQ(TRUE, mount_done);
  }

  EXPECT_CALL(platform_, GetMountsBySourcePrefix(_, _))
      .Times(4)
      .WillRepeatedly(Invoke(StaleShadowMounts));
  EXPECT_CALL(platform_, GetAttachedLoopDevices())
      .WillRepeatedly(Return(std::vector<Platform::LoopDevice>()));
  EXPECT_CALL(platform_, GetLoopDeviceMounts(_)).WillOnce(Return(false));
  EXPECT_CALL(
      platform_,
      EnumerateDirectoryEntries(
          FilePath(kEphemeralCryptohomeDir).Append(kSparseFileDir), _, _))
      .WillOnce(Return(false));
  // Only 5 look ups: user/1 and root/1 are owned, children of these
  // directories are excluded.
  EXPECT_CALL(platform_, ExpireMount(_)).Times(5);

  EXPECT_CALL(*mount, OwnsMountPoint(_)).WillRepeatedly(Return(false));
  EXPECT_CALL(*mount, OwnsMountPoint(FilePath("/home/user/1")))
      .WillOnce(Return(true));
  EXPECT_CALL(*mount, OwnsMountPoint(FilePath("/home/root/1")))
      .WillOnce(Return(true));

  EXPECT_CALL(platform_,
              Unmount(Property(&FilePath::value, EndsWith("/0")), true, _))
      .Times(2)
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, Unmount(FilePath("/home/chronos/user"), true, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, Unmount(Property(&FilePath::value,
                                          EndsWith("user/MyFiles/Downloads")),
                                 true, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, Unmount(FilePath("/daemon-store/server/a"), true, _))
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
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
  EXPECT_TRUE(userdataauth_->CleanUpStaleMounts(false));
}

TEST_F(UserDataAuthTest, StartMigrateToDircryptoValidity) {
  constexpr char kUsername1[] = "foo@gmail.com";

  user_data_auth::StartMigrateToDircryptoRequest request;
  request.mutable_account_id()->set_account_id(kUsername1);
  request.set_minimal_migration(false);

  SetupMount(kUsername1);

  EXPECT_CALL(*mount_, MigrateEncryption(_, MigrationType::FULL))
      .WillOnce(Return(true));

  int success_cnt = 0;
  {
    TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
    userdataauth_->StartMigrateToDircrypto(
        request,
        base::BindRepeating(
            [](int* success_cnt_ptr,
               const user_data_auth::DircryptoMigrationProgress& progress) {
              EXPECT_EQ(progress.status(),
                        user_data_auth::DIRCRYPTO_MIGRATION_SUCCESS);
              (*success_cnt_ptr)++;
            },
            base::Unretained(&success_cnt)));
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
    TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
    userdataauth_->StartMigrateToDircrypto(
        request,
        base::BindRepeating(
            [](int* call_cnt_ptr,
               const user_data_auth::DircryptoMigrationProgress& progress) {
              EXPECT_EQ(progress.status(),
                        user_data_auth::DIRCRYPTO_MIGRATION_FAILED);
              (*call_cnt_ptr)++;
            },
            base::Unretained(&call_cnt)));
  }
  EXPECT_EQ(call_cnt, 1);

  // Test MigrateToDircrypto failed
  SetupMount(kUsername1);

  EXPECT_CALL(*mount_, MigrateEncryption(_, MigrationType::FULL))
      .WillOnce(Return(false));

  call_cnt = 0;
  {
    TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
    userdataauth_->StartMigrateToDircrypto(
        request,
        base::BindRepeating(
            [](int* call_cnt_ptr,
               const user_data_auth::DircryptoMigrationProgress& progress) {
              EXPECT_EQ(progress.status(),
                        user_data_auth::DIRCRYPTO_MIGRATION_FAILED);
              (*call_cnt_ptr)++;
            },
            base::Unretained(&call_cnt)));
  }

  EXPECT_EQ(call_cnt, 1);
}

TEST_F(UserDataAuthTest, NeedsDircryptoMigration) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
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
  TaskGuard guard(this, UserDataAuth::TestThreadId::kOriginThread);
  // Test when there's no Low Entropy Credential Backend.
  EXPECT_CALL(tpm_, GetLECredentialBackend()).WillOnce(Return(nullptr));
  EXPECT_FALSE(userdataauth_->IsLowEntropyCredentialSupported());

  NiceMock<MockLECredentialBackend> backend;
  EXPECT_CALL(tpm_, GetLECredentialBackend()).WillRepeatedly(Return(&backend));

  // Test when the backend says it's not supported.
  EXPECT_CALL(backend, IsSupported()).WillOnce(Return(false));
  EXPECT_FALSE(userdataauth_->IsLowEntropyCredentialSupported());

  // Test when it's supported.
  EXPECT_CALL(backend, IsSupported()).WillOnce(Return(true));
  EXPECT_TRUE(userdataauth_->IsLowEntropyCredentialSupported());
}

TEST_F(UserDataAuthTest, GetAccountDiskUsage) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
  // Test when the user is non-existent.
  AccountIdentifier account;
  account.set_account_id("non_existent_user");

  EXPECT_EQ(0, userdataauth_->GetAccountDiskUsage(account));

  // Test when the user exists and home directory is not empty.
  constexpr char kUsername1[] = "foo@gmail.com";
  account.set_account_id(kUsername1);

  constexpr int64_t kHomedirSize = 12345678912345;
  EXPECT_CALL(homedirs_, ComputeDiskUsage(kUsername1))
      .WillOnce(Return(kHomedirSize));
  EXPECT_EQ(kHomedirSize, userdataauth_->GetAccountDiskUsage(account));
}

TEST_F(UserDataAuthTest, LowDiskSpaceNotificationCallback) {
  EXPECT_CALL(low_disk_space_handler_, SetLowDiskSpaceCallback(_));
  userdataauth_->SetLowDiskSpaceCallback(base::BindRepeating([](uint64_t) {}));
}

TEST_F(UserDataAuthTest, LowDiskSpaceHandlerStopped) {
  EXPECT_CALL(low_disk_space_handler_, Stop());
}

// A test fixture with some utility functions for testing mount and keys related
// functionalities.
class UserDataAuthExTest : public UserDataAuthTest {
 public:
  UserDataAuthExTest() = default;
  UserDataAuthExTest(const UserDataAuthExTest&) = delete;
  UserDataAuthExTest& operator=(const UserDataAuthExTest&) = delete;

  ~UserDataAuthExTest() override = default;

  std::unique_ptr<VaultKeyset> GetNiceMockVaultKeyset(
      const std::string& obfuscated_username,
      const std::string& key_label) const {
    // Note that technically speaking this is not strictly a mock, and probably
    // closer to a stub. However, the underlying class is
    // NiceMock<MockVaultKeyset>, thus we name the method accordingly.
    std::unique_ptr<VaultKeyset> mvk(new NiceMock<MockVaultKeyset>);
    mvk->SetKeyDataLabel(key_label);

    SerializedVaultKeyset::SignatureChallengeInfo sig_challenge_info;
    mvk->SetSignatureChallengeInfo(sig_challenge_info);

    return mvk;
  }

  void CallCheckKeyAndVerify(
      user_data_auth::CryptohomeErrorCode expected_error_code) {
    // Create a callback and verify the error code there.
    bool called = false;
    auto on_done = base::BindOnce(
        [](bool* called_ptr,
           user_data_auth::CryptohomeErrorCode expected_error_code,
           user_data_auth::CryptohomeErrorCode error_code) {
          EXPECT_EQ(error_code, expected_error_code);
          *called_ptr = true;
        },
        base::Unretained(&called), expected_error_code);

    userdataauth_->CheckKey(*check_req_.get(), std::move(on_done));
    EXPECT_TRUE(called);
  }

 protected:
  void PrepareArguments() {
    add_req_.reset(new user_data_auth::AddKeyRequest);
    check_req_.reset(new user_data_auth::CheckKeyRequest);
    mount_req_.reset(new user_data_auth::MountRequest);
    remove_req_.reset(new user_data_auth::RemoveKeyRequest);
    mass_remove_req_.reset(new user_data_auth::MassRemoveKeysRequest);
    list_keys_req_.reset(new user_data_auth::ListKeysRequest);
    get_key_data_req_.reset(new user_data_auth::GetKeyDataRequest);
    migrate_req_.reset(new user_data_auth::MigrateKeyRequest);
    remove_homedir_req_.reset(new user_data_auth::RemoveRequest);
    start_auth_session_req_.reset(new user_data_auth::StartAuthSessionRequest);
    authenticate_auth_session_req_.reset(
        new user_data_auth::AuthenticateAuthSessionRequest);
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

  std::unique_ptr<user_data_auth::AddKeyRequest> add_req_;
  std::unique_ptr<user_data_auth::CheckKeyRequest> check_req_;
  std::unique_ptr<user_data_auth::MountRequest> mount_req_;
  std::unique_ptr<user_data_auth::RemoveKeyRequest> remove_req_;
  std::unique_ptr<user_data_auth::MassRemoveKeysRequest> mass_remove_req_;
  std::unique_ptr<user_data_auth::ListKeysRequest> list_keys_req_;
  std::unique_ptr<user_data_auth::GetKeyDataRequest> get_key_data_req_;
  std::unique_ptr<user_data_auth::MigrateKeyRequest> migrate_req_;
  std::unique_ptr<user_data_auth::RemoveRequest> remove_homedir_req_;
  std::unique_ptr<user_data_auth::StartAuthSessionRequest>
      start_auth_session_req_;
  std::unique_ptr<user_data_auth::AuthenticateAuthSessionRequest>
      authenticate_auth_session_req_;

  static constexpr char kUser[] = "chromeos-user";
  static constexpr char kKey[] = "274146c6e8886a843ddfea373e2dc71b";

  brillo::SecureBlob salt;
};

constexpr char UserDataAuthExTest::kUser[];
constexpr char UserDataAuthExTest::kKey[];

TEST_F(UserDataAuthExTest, MountGuestValidity) {
  PrepareArguments();

  mount_req_->set_guest_mount(true);

  SetupMount(kUser);
  // Expect that existing mounts will be removed.
  EXPECT_CALL(*mount_, IsMounted()).WillOnce(Return(true));
  EXPECT_CALL(*mount_, UnmountCryptohome()).WillOnce(Return(true));

  EXPECT_CALL(mount_factory_, New(_, _, _, _, _))
      .WillOnce(Invoke([](Platform*, HomeDirs*, bool, bool, bool) {
        NiceMock<MockMount>* res = new NiceMock<MockMount>();
        EXPECT_CALL(*res, MountEphemeralCryptohome(kGuestUserName))
            .WillOnce(Return(MOUNT_ERROR_NONE));
        return reinterpret_cast<Mount*>(res);
      }));

  bool called = false;
  {
    TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
    userdataauth_->DoMount(
        *mount_req_,
        base::BindOnce(
            [](bool& called, const user_data_auth::MountReply& reply) {
              called = true;
              EXPECT_FALSE(reply.sanitized_username().empty());
              EXPECT_EQ(user_data_auth::CRYPTOHOME_ERROR_NOT_SET,
                        reply.error());
            },
            std::ref(called)));
  }
  EXPECT_TRUE(called);

  EXPECT_NE(userdataauth_->get_session_for_user(kGuestUserName), nullptr);
}

TEST_F(UserDataAuthExTest, MountGuestMountPointBusy) {
  PrepareArguments();

  mount_req_->set_guest_mount(true);

  SetupMount(kUser);
  // Expect that existing mounts will be removed, but unmounting will fail.
  EXPECT_CALL(*mount_, IsMounted()).WillOnce(Return(true));
  EXPECT_CALL(*mount_, UnmountCryptohome()).WillOnce(Return(false));

  EXPECT_CALL(mount_factory_, New(_, _, _, _, _)).Times(0);

  bool called = false;
  {
    TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
    userdataauth_->DoMount(
        *mount_req_,
        base::BindOnce(
            [](bool* called_ptr, const user_data_auth::MountReply& reply) {
              *called_ptr = true;
              EXPECT_EQ(user_data_auth::CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY,
                        reply.error());
            },
            base::Unretained(&called)));
  }
  EXPECT_TRUE(called);

  EXPECT_EQ(userdataauth_->get_session_for_user(kGuestUserName), nullptr);
}

TEST_F(UserDataAuthExTest, MountGuestMountFailed) {
  PrepareArguments();

  mount_req_->set_guest_mount(true);

  EXPECT_CALL(mount_factory_, New(_, _, _, _, _))
      .WillOnce(Invoke([](Platform*, HomeDirs*, bool, bool, bool) {
        NiceMock<MockMount>* res = new NiceMock<MockMount>();
        EXPECT_CALL(*res, MountEphemeralCryptohome(kGuestUserName))
            .WillOnce(Return(MOUNT_ERROR_FATAL));
        return reinterpret_cast<Mount*>(res);
      }));

  bool called = false;
  {
    TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
    userdataauth_->DoMount(
        *mount_req_,
        base::BindOnce(
            [](bool* called_ptr, const user_data_auth::MountReply& reply) {
              *called_ptr = true;
              EXPECT_EQ(user_data_auth::CRYPTOHOME_ERROR_MOUNT_FATAL,
                        reply.error());
            },
            base::Unretained(&called)));
  }
  EXPECT_TRUE(called);
}

TEST_F(UserDataAuthExTest, MountInvalidArgs) {
  // Note that this test doesn't distinguish between different causes of invalid
  // argument, that is, this doesn't check that
  // CRYPTOHOME_ERROR_INVALID_ARGUMENT is coming back because of the right
  // reason. This is because in the current structuring of the code, it would
  // not be possible to distinguish between those cases. This test only checks
  // that parameters that should lead to invalid argument does indeed lead to
  // invalid argument error.

  bool called;
  user_data_auth::CryptohomeErrorCode error_code;

  // This calls DoMount and check that the result is reported (i.e. the callback
  // is called), and is CRYPTOHOME_ERROR_INVALID_ARGUMENT.
  auto CallDoMountAndGetError = [&called, &error_code, this]() {
    called = false;
    error_code = user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
    {
      TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
      userdataauth_->DoMount(
          *mount_req_, base::BindOnce(
                           [](bool& called_ptr,
                              user_data_auth::CryptohomeErrorCode& error_code,
                              const user_data_auth::MountReply& reply) {
                             called_ptr = true;
                             error_code = reply.error();
                           },
                           std::ref(called), std::ref(error_code)));
    }
  };

  // Test for case with no email.
  PrepareArguments();

  CallDoMountAndGetError();
  EXPECT_TRUE(called);
  EXPECT_EQ(error_code, user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);

  // Test for case with no secrets.
  PrepareArguments();
  mount_req_->mutable_account()->set_account_id("foo@gmail.com");

  CallDoMountAndGetError();
  EXPECT_TRUE(called);
  EXPECT_EQ(error_code, user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);

  // Test for case with empty secret.
  PrepareArguments();
  mount_req_->mutable_account()->set_account_id("foo@gmail.com");
  mount_req_->mutable_authorization()->mutable_key()->set_secret("");

  CallDoMountAndGetError();
  EXPECT_TRUE(called);
  EXPECT_EQ(error_code, user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);

  // Test for create request given but without key.
  PrepareArguments();
  mount_req_->mutable_account()->set_account_id("foo@gmail.com");
  mount_req_->mutable_authorization()->mutable_key()->set_secret("blerg");
  mount_req_->mutable_create();

  CallDoMountAndGetError();
  EXPECT_TRUE(called);
  EXPECT_EQ(error_code, user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);

  // Test for create request given but with an empty key.
  PrepareArguments();
  mount_req_->mutable_account()->set_account_id("foo@gmail.com");
  mount_req_->mutable_authorization()->mutable_key()->set_secret("blerg");
  mount_req_->mutable_create()->add_keys();
  // TODO(wad) Add remaining missing field tests and NULL tests

  CallDoMountAndGetError();
  EXPECT_TRUE(called);
  EXPECT_EQ(error_code, user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);

  // Test for create request given with multiple keys.
  PrepareArguments();
  mount_req_->mutable_account()->set_account_id("foo@gmail.com");
  mount_req_->mutable_authorization()->mutable_key()->set_secret("blerg");
  mount_req_->mutable_create()->add_keys()->set_secret("");
  mount_req_->mutable_create()->add_keys()->set_secret("");

  CallDoMountAndGetError();
  EXPECT_TRUE(called);
  EXPECT_EQ(error_code, user_data_auth::CRYPTOHOME_ERROR_NOT_IMPLEMENTED);
}

TEST_F(UserDataAuthExTest, MountPublicWithExistingMounts) {
  constexpr char kUser[] = "chromeos-user";
  constexpr char kUsername[] = "foo@gmail.com";

  PrepareArguments();
  SetupMount(kUsername);

  mount_req_->mutable_account()->set_account_id(kUser);
  mount_req_->set_public_mount(true);

  bool called = false;
  EXPECT_CALL(homedirs_, Exists(_)).WillOnce(Return(true));
  {
    TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
    userdataauth_->DoMount(
        *mount_req_,
        base::BindOnce(
            [](bool* called_ptr, const user_data_auth::MountReply& reply) {
              *called_ptr = true;
              EXPECT_EQ(user_data_auth::CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY,
                        reply.error());
            },
            base::Unretained(&called)));
  }
  EXPECT_TRUE(called);
}

TEST_F(UserDataAuthExTest, MountPublicUsesPublicMountPasskey) {
  constexpr char kUser[] = "chromeos-user";
  PrepareArguments();

  mount_req_->mutable_account()->set_account_id(kUser);
  mount_req_->set_public_mount(true);

  EXPECT_CALL(homedirs_, Exists(_)).WillOnce(testing::InvokeWithoutArgs([&]() {
    SetupMount(kUser);
    EXPECT_CALL(homedirs_, CryptohomeExists(_, _)).WillOnce(Return(true));
    auto vk = std::make_unique<VaultKeyset>();
    EXPECT_CALL(keyset_management_, GetValidKeyset(_, _))
        .WillOnce(Return(ByMove(std::move(vk))));
    EXPECT_CALL(disk_cleanup_, FreeDiskSpaceDuringLogin(_));
    EXPECT_CALL(*mount_, MountCryptohome(_, _, _))
        .WillOnce(Return(MOUNT_ERROR_NONE));
    return true;
  }));
  bool called = false;
  {
    TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
    userdataauth_->DoMount(
        *mount_req_,
        base::BindOnce(
            [](bool* called_ptr, const user_data_auth::MountReply& reply) {
              *called_ptr = true;
              EXPECT_EQ(user_data_auth::CRYPTOHOME_ERROR_NOT_SET,
                        reply.error());
            },
            base::Unretained(&called)));
  }
  EXPECT_TRUE(called);
}

TEST_F(UserDataAuthExTest, MountPublicUsesPublicMountPasskeyWithNewUser) {
  constexpr char kUser[] = "chromeos-user";

  PrepareArguments();

  mount_req_->mutable_account()->set_account_id(kUser);
  mount_req_->set_public_mount(true);
  Key* add_key = mount_req_->mutable_create()->add_keys();
  add_key->mutable_data()->set_label("public_mount");

  SetupMount(kUser);
  EXPECT_CALL(homedirs_, CryptohomeExists(_, _)).WillOnce(Return(false));
  EXPECT_CALL(homedirs_, Create(kUser)).WillOnce(Return(true));
  VaultKeyset vk;
  EXPECT_CALL(keyset_management_, AddInitialKeyset(_))
      .WillOnce(Return(ByMove(std::make_unique<VaultKeyset>(vk))));
  EXPECT_CALL(keyset_management_, GetValidKeyset(_, _))
      .WillOnce(Return(ByMove(std::make_unique<VaultKeyset>(vk))));
  EXPECT_CALL(disk_cleanup_, FreeDiskSpaceDuringLogin(_));
  EXPECT_CALL(*mount_, MountCryptohome(_, _, _))
      .WillOnce(Return(MOUNT_ERROR_NONE));

  bool called = false;
  user_data_auth::CryptohomeErrorCode error_code =
      user_data_auth::CRYPTOHOME_ERROR_NOT_SET;

  {
    TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
    userdataauth_->DoMount(
        *mount_req_,
        base::BindOnce(
            [](bool& called, user_data_auth::CryptohomeErrorCode& error_code,
               const user_data_auth::MountReply& reply) {
              called = true;
              error_code = reply.error();
            },
            std::ref(called), std::ref(error_code)));
  }
  EXPECT_TRUE(called);
  EXPECT_EQ(user_data_auth::CRYPTOHOME_ERROR_NOT_SET, error_code);
}

TEST_F(UserDataAuthExTest, MountPublicUsesPublicMountPasskeyError) {
  constexpr char kUser[] = "chromeos-user";
  PrepareArguments();

  mount_req_->mutable_account()->set_account_id(kUser);
  mount_req_->set_public_mount(true);
  SecureBlob empty_blob;
  EXPECT_CALL(keyset_management_, GetPublicMountPassKey(_))
      .WillOnce(Return(ByMove(empty_blob)));

  bool called = false;
  user_data_auth::CryptohomeErrorCode error_code =
      user_data_auth::CRYPTOHOME_ERROR_NOT_SET;

  {
    TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
    userdataauth_->DoMount(
        *mount_req_,
        base::BindOnce(
            [](bool& called, user_data_auth::CryptohomeErrorCode& error_code,
               const user_data_auth::MountReply& reply) {
              called = true;
              error_code = reply.error();
            },
            std::ref(called), std::ref(error_code)));
  }
  EXPECT_TRUE(called);
  EXPECT_EQ(user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED,
            error_code);
}

TEST_F(UserDataAuthExTest, AddKeyInvalidArgs) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
  PrepareArguments();

  // Test for when there's no email supplied.
  EXPECT_EQ(userdataauth_->AddKey(*add_req_.get()),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);

  // Test for an invalid account_id, where it is initialized
  // but the underlying string is empty.
  // Initialize the authorization request but leave the secret empty.
  add_req_->mutable_account_id()->set_account_id("");
  add_req_->mutable_authorization_request()->mutable_key();
  EXPECT_EQ(userdataauth_->AddKey(*add_req_.get()),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
  // Cleanup
  add_req_->clear_authorization_request();

  // Test for when there's no secret.
  add_req_->mutable_account_id()->set_account_id("foo@gmail.com");
  EXPECT_EQ(userdataauth_->AddKey(*add_req_.get()),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);

  // Test for valid authorization request but empty secret.
  add_req_->mutable_authorization_request()->mutable_key();
  EXPECT_EQ(userdataauth_->AddKey(*add_req_.get()),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);

  // Test for when there's no new key.
  add_req_->mutable_authorization_request()->mutable_key()->set_secret("blerg");
  add_req_->clear_key();
  EXPECT_EQ(userdataauth_->AddKey(*add_req_.get()),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);

  // Test for no new key label.
  add_req_->mutable_key();
  // No label
  add_req_->mutable_key()->set_secret("some secret");
  EXPECT_EQ(userdataauth_->AddKey(*add_req_.get()),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
}

TEST_F(UserDataAuthExTest, AddKeyNoObfuscatedName) {
  // HomeDirs cant find the existing obfuscated username.
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
  PrepareArguments();

  // Prepare a valid AddKeyRequest.
  add_req_->mutable_account_id()->set_account_id("foo@gmail.com");
  add_req_->mutable_authorization_request()->mutable_key()->set_secret("blerg");
  add_req_->mutable_key();
  add_req_->mutable_key()->set_secret("some secret");
  add_req_->mutable_key()->mutable_data()->set_label("just a label");
  // Inject failure into homedirs->Exists().
  EXPECT_CALL(homedirs_, Exists(_)).WillOnce(Return(false));

  EXPECT_EQ(userdataauth_->AddKey(*add_req_.get()),
            user_data_auth::CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND);
}

TEST_F(UserDataAuthExTest, AddKeyValidity) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
  PrepareArguments();

  add_req_->mutable_account_id()->set_account_id("foo@gmail.com");
  add_req_->mutable_authorization_request()->mutable_key()->set_secret("blerg");
  add_req_->mutable_key();
  add_req_->mutable_key()->set_secret("some secret");
  add_req_->mutable_key()->mutable_data()->set_label("just a label");

  EXPECT_CALL(homedirs_, Exists(_)).WillOnce(Return(true));
  EXPECT_CALL(keyset_management_, GetValidKeyset(_, _))
      .WillOnce(Return(ByMove(std::make_unique<VaultKeyset>())));
  EXPECT_CALL(keyset_management_, AddKeyset(_, _, _))
      .WillOnce(Return(cryptohome::CRYPTOHOME_ERROR_NOT_SET));

  EXPECT_EQ(userdataauth_->AddKey(*add_req_.get()),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
}

// Tests the AddKey interface for reset seed generation.
TEST_F(UserDataAuthExTest, AddKeyResetSeedGeneration) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
  PrepareArguments();

  add_req_->mutable_account_id()->set_account_id("foo@gmail.com");
  add_req_->mutable_authorization_request()->mutable_key()->set_secret("blerg");
  add_req_->mutable_key();
  add_req_->mutable_key()->set_secret("some secret");
  add_req_->mutable_key()->mutable_data()->set_label("just a label");

  EXPECT_CALL(homedirs_, Exists(_)).WillOnce(Return(true));
  EXPECT_CALL(keyset_management_, GetValidKeyset(_, _))
      .WillOnce(Return(ByMove(std::make_unique<VaultKeyset>())));
  EXPECT_CALL(keyset_management_, AddWrappedResetSeedIfMissing(_, _));
  EXPECT_CALL(keyset_management_, AddKeyset(_, _, _))
      .WillOnce(Return(cryptohome::CRYPTOHOME_ERROR_NOT_SET));

  EXPECT_EQ(userdataauth_->AddKey(*add_req_.get()),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
}

// Tests the AddKey interface for vault keyset not found case.
TEST_F(UserDataAuthExTest, AddKeyKeysetNotFound) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
  PrepareArguments();

  add_req_->mutable_account_id()->set_account_id("foo@gmail.com");
  add_req_->mutable_authorization_request()->mutable_key()->set_secret("blerg");
  add_req_->mutable_key();
  add_req_->mutable_key()->set_secret("some secret");
  add_req_->mutable_key()->mutable_data()->set_label("just a label");

  EXPECT_CALL(homedirs_, Exists(_)).WillOnce(Return(true));
  EXPECT_CALL(keyset_management_, GetValidKeyset(_, _))
      .WillOnce(Return(ByMove(nullptr)));

  EXPECT_EQ(userdataauth_->AddKey(*add_req_.get()),
            user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED);
}

// Note that CheckKey tries to two method to check whether a key is valid or
// not. The first is through Homedirs, and the second is through Mount.
// Therefore, we test the combinations of (Homedirs, Mount) x (Success, Fail)
// below.
TEST_F(UserDataAuthExTest, CheckKeyHomedirsCheckSuccess) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
  PrepareArguments();
  SetupMount(kUser);

  check_req_->mutable_account_id()->set_account_id(kUser);
  check_req_->mutable_authorization_request()->mutable_key()->set_secret(kKey);

  Credentials credentials("another", brillo::SecureBlob(kKey));
  session_->SetCredentials(credentials);
  EXPECT_CALL(homedirs_, Exists(_)).WillOnce(Return(true));
  EXPECT_CALL(keyset_management_, AreCredentialsValid(_))
      .WillOnce(Return(true));

  CallCheckKeyAndVerify(user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
}

TEST_F(UserDataAuthExTest, CheckKeyHomedirsCheckFail) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
  PrepareArguments();
  SetupMount(kUser);

  check_req_->mutable_account_id()->set_account_id(kUser);
  check_req_->mutable_authorization_request()->mutable_key()->set_secret(kKey);

  // Ensure failure
  Credentials credentials("another", brillo::SecureBlob(kKey));
  session_->SetCredentials(credentials);
  EXPECT_CALL(homedirs_, Exists(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(keyset_management_, AreCredentialsValid(_))
      .WillOnce(Return(false));

  CallCheckKeyAndVerify(
      user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED);
}

TEST_F(UserDataAuthExTest, CheckKeyMountCheckSuccess) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
  PrepareArguments();
  SetupMount(kUser);

  check_req_->mutable_account_id()->set_account_id(kUser);
  check_req_->mutable_authorization_request()->mutable_key()->set_secret(kKey);

  Credentials credentials(kUser, brillo::SecureBlob(kKey));
  session_->SetCredentials(credentials);

  CallCheckKeyAndVerify(user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
}

TEST_F(UserDataAuthExTest, CheckKeyMountCheckFail) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
  PrepareArguments();
  SetupMount(kUser);

  check_req_->mutable_account_id()->set_account_id(kUser);
  check_req_->mutable_authorization_request()->mutable_key()->set_secret(kKey);

  Credentials credentials(kUser, brillo::SecureBlob("wrong"));
  session_->SetCredentials(credentials);

  EXPECT_CALL(homedirs_, Exists(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(keyset_management_, AreCredentialsValid(_))
      .WillOnce(Return(false));

  CallCheckKeyAndVerify(user_data_auth::CryptohomeErrorCode::
                            CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED);
}

TEST_F(UserDataAuthExTest, StartFingerprintAuthSessionInvalid) {
  PrepareArguments();
  // No account_id, request is invalid.
  user_data_auth::StartFingerprintAuthSessionRequest req;

  bool called = false;
  {
    TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
    userdataauth_->StartFingerprintAuthSession(
        req,
        base::BindOnce(
            [](bool* called_ptr,
               const user_data_auth::StartFingerprintAuthSessionReply& reply) {
              EXPECT_EQ(reply.error(),
                        user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
              *called_ptr = true;
            },
            base::Unretained(&called)));
  }
  EXPECT_TRUE(called);
}

TEST_F(UserDataAuthExTest, StartFingerprintAuthSessionFail) {
  PrepareArguments();
  user_data_auth::StartFingerprintAuthSessionRequest req;
  req.mutable_account_id()->set_account_id(kUser);

  EXPECT_CALL(homedirs_, Exists(_)).WillOnce(Return(true));

  // Let the fingerprint auth session fail to start.
  EXPECT_CALL(fingerprint_manager_, StartAuthSessionAsyncForUser(_, _))
      .WillOnce([](const std::string& user,
                   base::OnceCallback<void(bool success)>
                       auth_session_start_client_callback) {
        std::move(auth_session_start_client_callback).Run(false);
      });

  bool called = false;
  {
    TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
    userdataauth_->StartFingerprintAuthSession(
        req,
        base::BindOnce(
            [](bool* called_ptr,
               const user_data_auth::StartFingerprintAuthSessionReply& reply) {
              EXPECT_EQ(reply.error(),
                        user_data_auth::CryptohomeErrorCode::
                            CRYPTOHOME_ERROR_FINGERPRINT_ERROR_INTERNAL);
              *called_ptr = true;
            },
            base::Unretained(&called)));
  }
  EXPECT_TRUE(called);
}

TEST_F(UserDataAuthExTest, StartFingerprintAuthSessionSuccess) {
  PrepareArguments();
  user_data_auth::StartFingerprintAuthSessionRequest req;
  req.mutable_account_id()->set_account_id(kUser);

  EXPECT_CALL(homedirs_, Exists(_)).WillOnce(Return(true));

  EXPECT_CALL(fingerprint_manager_, StartAuthSessionAsyncForUser(_, _))
      .WillOnce([](const std::string& user,
                   base::OnceCallback<void(bool success)>
                       auth_session_start_client_callback) {
        std::move(auth_session_start_client_callback).Run(true);
      });

  bool called = false;
  {
    TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
    userdataauth_->StartFingerprintAuthSession(
        req,
        base::BindOnce(
            [](bool* called_ptr,
               const user_data_auth::StartFingerprintAuthSessionReply& reply) {
              EXPECT_EQ(reply.error(),
                        user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
              *called_ptr = true;
            },
            base::Unretained(&called)));
  }
  EXPECT_TRUE(called);
}

TEST_F(UserDataAuthExTest, CheckKeyFingerprintFailRetry) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
  PrepareArguments();

  check_req_->mutable_account_id()->set_account_id(kUser);
  check_req_->mutable_authorization_request()
      ->mutable_key()
      ->mutable_data()
      ->set_type(cryptohome::KeyData::KEY_TYPE_FINGERPRINT);

  EXPECT_CALL(fingerprint_manager_, HasAuthSessionForUser(_))
      .WillOnce(Return(true));

  // Simulate a scan result immediately following SetAuthScanDoneCallback().
  EXPECT_CALL(fingerprint_manager_, SetAuthScanDoneCallback(_))
      .WillOnce([](base::OnceCallback<void(FingerprintScanStatus status)>
                       auth_scan_done_callback) {
        std::move(auth_scan_done_callback)
            .Run(FingerprintScanStatus::FAILED_RETRY_ALLOWED);
      });

  CallCheckKeyAndVerify(user_data_auth::CryptohomeErrorCode::
                            CRYPTOHOME_ERROR_FINGERPRINT_RETRY_REQUIRED);
}

TEST_F(UserDataAuthExTest, CheckKeyFingerprintFailNoRetry) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
  PrepareArguments();

  check_req_->mutable_account_id()->set_account_id(kUser);
  check_req_->mutable_authorization_request()
      ->mutable_key()
      ->mutable_data()
      ->set_type(cryptohome::KeyData::KEY_TYPE_FINGERPRINT);

  EXPECT_CALL(fingerprint_manager_, HasAuthSessionForUser(_))
      .WillOnce(Return(true));

  // Simulate a scan result immediately following SetAuthScanDoneCallback().
  EXPECT_CALL(fingerprint_manager_, SetAuthScanDoneCallback(_))
      .WillOnce([](base::OnceCallback<void(FingerprintScanStatus status)>
                       auth_scan_done_callback) {
        std::move(auth_scan_done_callback)
            .Run(FingerprintScanStatus::FAILED_RETRY_NOT_ALLOWED);
      });

  CallCheckKeyAndVerify(
      user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_FINGERPRINT_DENIED);
}

TEST_F(UserDataAuthExTest, CheckKeyFingerprintWrongUser) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
  PrepareArguments();

  check_req_->mutable_account_id()->set_account_id(kUser);
  check_req_->mutable_authorization_request()
      ->mutable_key()
      ->mutable_data()
      ->set_type(cryptohome::KeyData::KEY_TYPE_FINGERPRINT);

  EXPECT_CALL(fingerprint_manager_, HasAuthSessionForUser(_))
      .WillOnce(Return(false));

  CallCheckKeyAndVerify(
      user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_FINGERPRINT_DENIED);
}

TEST_F(UserDataAuthExTest, CheckKeyFingerprintSuccess) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
  PrepareArguments();

  check_req_->mutable_account_id()->set_account_id(kUser);
  check_req_->mutable_authorization_request()
      ->mutable_key()
      ->mutable_data()
      ->set_type(cryptohome::KeyData::KEY_TYPE_FINGERPRINT);

  EXPECT_CALL(fingerprint_manager_, HasAuthSessionForUser(_))
      .WillOnce(Return(true));

  // Simulate a scan result immediately following SetAuthScanDoneCallback().
  EXPECT_CALL(fingerprint_manager_, SetAuthScanDoneCallback(_))
      .WillOnce([](base::OnceCallback<void(FingerprintScanStatus status)>
                       auth_scan_done_callback) {
        std::move(auth_scan_done_callback).Run(FingerprintScanStatus::SUCCESS);
      });

  CallCheckKeyAndVerify(user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
}

TEST_F(UserDataAuthExTest, CheckKeyInvalidArgs) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
  PrepareArguments();

  // No email supplied.
  CallCheckKeyAndVerify(user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);

  // No secret.
  check_req_->mutable_account_id()->set_account_id("foo@gmail.com");
  CallCheckKeyAndVerify(user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);

  // Empty secret.
  check_req_->mutable_authorization_request()->mutable_key()->set_secret("");
  CallCheckKeyAndVerify(user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
}

TEST_F(UserDataAuthExTest, RemoveKeyValidity) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
  PrepareArguments();

  constexpr char kUsername1[] = "foo@gmail.com";
  constexpr char kLabel1[] = "some label";

  remove_req_->mutable_account_id()->set_account_id(kUsername1);
  remove_req_->mutable_authorization_request()->mutable_key()->set_secret(
      "some secret");
  remove_req_->mutable_key()->mutable_data()->set_label(kLabel1);

  // Success case.
  EXPECT_CALL(homedirs_, Exists(_)).WillOnce(Return(true));
  EXPECT_CALL(keyset_management_,
              RemoveKeyset(Property(&Credentials::username, kUsername1),
                           Property(&KeyData::label, kLabel1)))
      .WillOnce(Return(cryptohome::CRYPTOHOME_ERROR_NOT_SET));

  EXPECT_EQ(userdataauth_->RemoveKey(*remove_req_.get()),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);

  // Check the case when the account doesn't exist.
  EXPECT_CALL(homedirs_, Exists(_)).WillOnce(Return(false));

  EXPECT_EQ(userdataauth_->RemoveKey(*remove_req_.get()),
            user_data_auth::CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND);

  // Check when RemoveKeyset failed.
  EXPECT_CALL(homedirs_, Exists(_)).WillOnce(Return(true));
  EXPECT_CALL(keyset_management_,
              RemoveKeyset(Property(&Credentials::username, kUsername1),
                           Property(&KeyData::label, kLabel1)))
      .WillOnce(Return(cryptohome::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE));

  EXPECT_EQ(userdataauth_->RemoveKey(*remove_req_.get()),
            user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
}

TEST_F(UserDataAuthExTest, RemoveKeyInvalidArgs) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
  PrepareArguments();

  // No email supplied.
  EXPECT_EQ(userdataauth_->RemoveKey(*remove_req_.get()),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);

  // No secret.
  remove_req_->mutable_account_id()->set_account_id("foo@gmail.com");
  EXPECT_EQ(userdataauth_->RemoveKey(*remove_req_.get()),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);

  // Empty secret.
  remove_req_->mutable_authorization_request()->mutable_key()->set_secret("");
  EXPECT_EQ(userdataauth_->RemoveKey(*remove_req_.get()),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);

  // No label provided for removal.
  remove_req_->mutable_authorization_request()->mutable_key()->set_secret(
      "some secret");
  remove_req_->mutable_key()->mutable_data();
  EXPECT_EQ(userdataauth_->RemoveKey(*remove_req_.get()),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
}

TEST_F(UserDataAuthExTest, MassRemoveKeysInvalidArgsNoEmail) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
  PrepareArguments();

  EXPECT_EQ(userdataauth_->MassRemoveKeys(*mass_remove_req_.get()),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
}

TEST_F(UserDataAuthExTest, MassRemoveKeysInvalidArgsNoSecret) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
  PrepareArguments();
  mass_remove_req_->mutable_account_id()->set_account_id("foo@gmail.com");

  EXPECT_EQ(userdataauth_->MassRemoveKeys(*mass_remove_req_.get()),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
}

TEST_F(UserDataAuthExTest, MassRemoveKeysAccountNotExist) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
  PrepareArguments();
  mass_remove_req_->mutable_account_id()->set_account_id("foo@gmail.com");
  mass_remove_req_->mutable_authorization_request()->mutable_key()->set_secret(
      "blerg");

  EXPECT_CALL(homedirs_, Exists(_)).WillRepeatedly(Return(false));

  EXPECT_EQ(userdataauth_->MassRemoveKeys(*mass_remove_req_.get()),
            user_data_auth::CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND);
}

TEST_F(UserDataAuthExTest, MassRemoveKeysAuthFailed) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
  PrepareArguments();
  mass_remove_req_->mutable_account_id()->set_account_id("foo@gmail.com");
  mass_remove_req_->mutable_authorization_request()->mutable_key()->set_secret(
      "blerg");

  EXPECT_CALL(homedirs_, Exists(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(keyset_management_, AreCredentialsValid(_))
      .WillRepeatedly(Return(false));

  EXPECT_EQ(userdataauth_->MassRemoveKeys(*mass_remove_req_.get()),
            user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED);
}

TEST_F(UserDataAuthExTest, MassRemoveKeysGetLabelsFailed) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
  PrepareArguments();
  mass_remove_req_->mutable_account_id()->set_account_id("foo@gmail.com");
  mass_remove_req_->mutable_authorization_request()->mutable_key()->set_secret(
      "blerg");

  EXPECT_CALL(homedirs_, Exists(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(keyset_management_, AreCredentialsValid(_))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(keyset_management_, GetVaultKeysetLabels(_, _))
      .WillRepeatedly(Return(false));

  EXPECT_EQ(userdataauth_->MassRemoveKeys(*mass_remove_req_.get()),
            user_data_auth::CRYPTOHOME_ERROR_KEY_NOT_FOUND);
}

TEST_F(UserDataAuthExTest, MassRemoveKeysForceSuccess) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
  PrepareArguments();
  mass_remove_req_->mutable_account_id()->set_account_id("foo@gmail.com");
  mass_remove_req_->mutable_authorization_request()->mutable_key()->set_secret(
      "blerg");

  EXPECT_CALL(homedirs_, Exists(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(keyset_management_, AreCredentialsValid(_))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(keyset_management_, GetVaultKeysetLabels(_, _))
      .WillRepeatedly(Return(true));

  EXPECT_EQ(userdataauth_->MassRemoveKeys(*mass_remove_req_.get()),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
}

constexpr char ListKeysValidityTest_label1[] = "Label 1";
constexpr char ListKeysValidityTest_label2[] = "Yet another label";

TEST_F(UserDataAuthExTest, ListKeysValidity) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
  PrepareArguments();

  list_keys_req_->mutable_account_id()->set_account_id("foo@gmail.com");
  // Note that authorization request in ListKeyRequest is currently not
  // required.

  // Success case.
  EXPECT_CALL(homedirs_, Exists(_)).WillOnce(Return(true));
  EXPECT_CALL(keyset_management_, GetVaultKeysetLabels(_, _))
      .WillOnce(Invoke(
          [](const std::string& ignored, std::vector<std::string>* output) {
            output->clear();
            output->push_back(ListKeysValidityTest_label1);
            output->push_back(ListKeysValidityTest_label2);
            return true;
          }));

  std::vector<std::string> labels;
  EXPECT_EQ(userdataauth_->ListKeys(*list_keys_req_, &labels),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);

  EXPECT_THAT(labels, ElementsAre(ListKeysValidityTest_label1,
                                  ListKeysValidityTest_label2));

  // Test for account not found case.
  EXPECT_CALL(homedirs_, Exists(_)).WillOnce(Return(false));
  EXPECT_EQ(userdataauth_->ListKeys(*list_keys_req_, &labels),
            user_data_auth::CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND);

  // Test for key not found case.
  EXPECT_CALL(homedirs_, Exists(_)).WillOnce(Return(true));
  EXPECT_CALL(keyset_management_, GetVaultKeysetLabels(_, _))
      .WillOnce(Return(false));
  EXPECT_EQ(userdataauth_->ListKeys(*list_keys_req_, &labels),
            user_data_auth::CRYPTOHOME_ERROR_KEY_NOT_FOUND);
}

TEST_F(UserDataAuthExTest, ListKeysInvalidArgs) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
  PrepareArguments();
  std::vector<std::string> labels;

  // No Email.
  EXPECT_EQ(userdataauth_->ListKeys(*list_keys_req_, &labels),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);

  // Empty email.
  list_keys_req_->mutable_account_id()->set_account_id("");
  EXPECT_EQ(userdataauth_->ListKeys(*list_keys_req_, &labels),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
}

TEST_F(UserDataAuthExTest, GetKeyDataExNoMatch) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
  PrepareArguments();

  EXPECT_CALL(homedirs_, Exists(_)).WillRepeatedly(Return(true));

  get_key_data_req_->mutable_account_id()->set_account_id(
      "unittest@example.com");
  get_key_data_req_->mutable_key()->mutable_data()->set_label(
      "non-existent label");

  // Ensure there are no matches.
  std::unique_ptr<VaultKeyset> vk;
  EXPECT_CALL(keyset_management_, GetVaultKeyset(_, _))
      .WillOnce(Return(ByMove(std::move(vk))));

  cryptohome::KeyData keydata_out;
  bool found = false;
  EXPECT_EQ(userdataauth_->GetKeyData(*get_key_data_req_, &keydata_out, &found),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  // In case of no matching key, we should still return no error.

  EXPECT_FALSE(found);
}

TEST_F(UserDataAuthExTest, GetKeyDataExOneMatch) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
  // Request the single key by label.
  PrepareArguments();

  static const char* kExpectedLabel = "find-me";
  get_key_data_req_->mutable_key()->mutable_data()->set_label(kExpectedLabel);
  get_key_data_req_->mutable_account_id()->set_account_id(
      "unittest@example.com");

  EXPECT_CALL(homedirs_, Exists(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(keyset_management_, GetVaultKeyset(_, _))
      .Times(1)
      .WillRepeatedly(
          Invoke(this, &UserDataAuthExTest::GetNiceMockVaultKeyset));

  cryptohome::KeyData keydata_out;
  bool found = false;
  EXPECT_EQ(userdataauth_->GetKeyData(*get_key_data_req_, &keydata_out, &found),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);

  EXPECT_TRUE(found);
  EXPECT_EQ(std::string(kExpectedLabel), keydata_out.label());
}

TEST_F(UserDataAuthExTest, GetKeyDataInvalidArgs) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
  PrepareArguments();

  // No email.
  cryptohome::KeyData keydata_out;
  bool found = false;
  EXPECT_EQ(userdataauth_->GetKeyData(*get_key_data_req_, &keydata_out, &found),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
  EXPECT_FALSE(found);
}

TEST_F(UserDataAuthExTest, MigrateKeyValidity) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
  PrepareArguments();

  constexpr char kUsername1[] = "foo@gmail.com";
  constexpr char kSecret1[] = "some secret";
  migrate_req_->mutable_account_id()->set_account_id(kUsername1);
  migrate_req_->mutable_authorization_request()->mutable_key()->set_secret(
      kSecret1);
  migrate_req_->set_secret("blerg");

  SetupMount(kUsername1);

  // Test for successful case.

  EXPECT_CALL(
      keyset_management_,
      GetValidKeyset(Property(&Credentials::username, kUsername1), nullptr))
      .WillOnce(Return(ByMove(std::make_unique<VaultKeyset>())));
  EXPECT_CALL(keyset_management_,
              Migrate(_, Property(&Credentials::username, kUsername1)))
      .WillOnce(Return(true));
  EXPECT_EQ(userdataauth_->MigrateKey(*migrate_req_),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);

  // Test for unsuccessful case when existing keyset is not validated.
  EXPECT_CALL(
      keyset_management_,
      GetValidKeyset(Property(&Credentials::username, kUsername1), nullptr))
      .WillOnce(Return(ByMove(nullptr)));
  EXPECT_EQ(userdataauth_->MigrateKey(*migrate_req_),
            user_data_auth::CRYPTOHOME_ERROR_MIGRATE_KEY_FAILED);

  // Test for unsuccessful case when keyset migration fails.
  EXPECT_CALL(
      keyset_management_,
      GetValidKeyset(Property(&Credentials::username, kUsername1), nullptr))
      .WillOnce(Return(ByMove(std::make_unique<VaultKeyset>())));
  EXPECT_CALL(keyset_management_,
              Migrate(_, Property(&Credentials::username, kUsername1)))
      .WillOnce(Return(false));
  EXPECT_EQ(userdataauth_->MigrateKey(*migrate_req_),
            user_data_auth::CRYPTOHOME_ERROR_MIGRATE_KEY_FAILED);
}

TEST_F(UserDataAuthExTest, MigrateKeyInvalidArguments) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
  PrepareArguments();

  // No email.
  EXPECT_EQ(userdataauth_->MigrateKey(*migrate_req_),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);

  // No authorization request key secret.
  migrate_req_->mutable_account_id()->set_account_id("foo@gmail.com");
  EXPECT_EQ(userdataauth_->MigrateKey(*migrate_req_),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
}

TEST_F(UserDataAuthExTest, RemoveValidity) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
  PrepareArguments();

  constexpr char kUsername1[] = "foo@gmail.com";

  remove_homedir_req_->mutable_identifier()->set_account_id(kUsername1);

  // Test for successful case.
  EXPECT_CALL(homedirs_, Remove(GetObfuscatedUsername(kUsername1)))
      .WillOnce(Return(true));
  EXPECT_EQ(userdataauth_->Remove(*remove_homedir_req_),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);

  // Test for unsuccessful case.
  EXPECT_CALL(homedirs_, Remove(GetObfuscatedUsername(kUsername1)))
      .WillOnce(Return(false));
  EXPECT_EQ(userdataauth_->Remove(*remove_homedir_req_),
            user_data_auth::CRYPTOHOME_ERROR_REMOVE_FAILED);
}

TEST_F(UserDataAuthExTest, RemoveInvalidArguments) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
  PrepareArguments();

  // No account_id and AuthSession ID
  EXPECT_EQ(userdataauth_->Remove(*remove_homedir_req_),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);

  // Empty account_id
  remove_homedir_req_->mutable_identifier()->set_account_id("");
  EXPECT_EQ(userdataauth_->Remove(*remove_homedir_req_),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
}

TEST_F(UserDataAuthExTest, RemoveInvalidAuthSession) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
  PrepareArguments();
  std::string invalid_token = "invalid_token_16";
  remove_homedir_req_->set_auth_session_id(invalid_token);

  // Test.
  EXPECT_EQ(userdataauth_->Remove(*remove_homedir_req_),
            user_data_auth::CRYPTOHOME_INVALID_AUTH_SESSION_TOKEN);
}

TEST_F(UserDataAuthExTest, RemoveValidityWithAuthSession) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
  PrepareArguments();

  // Setup
  constexpr char kUsername1[] = "foo@gmail.com";

  start_auth_session_req_->mutable_account_id()->set_account_id(kUsername1);
  user_data_auth::StartAuthSessionReply auth_session_reply;
  {
    TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
    userdataauth_->StartAuthSession(
        *start_auth_session_req_,
        base::BindOnce(
            [](user_data_auth::StartAuthSessionReply* auth_reply_ptr,
               const user_data_auth::StartAuthSessionReply& reply) {
              *auth_reply_ptr = reply;
            },
            base::Unretained(&auth_session_reply)));
  }
  EXPECT_EQ(auth_session_reply.error(),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);

  // Test
  remove_homedir_req_->set_auth_session_id(
      auth_session_reply.auth_session_id());
  EXPECT_CALL(homedirs_, Remove(GetObfuscatedUsername(kUsername1)))
      .WillOnce(Return(true));
  EXPECT_EQ(userdataauth_->Remove(*remove_homedir_req_),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);

  // Verify
  EXPECT_EQ(userdataauth_->auth_session_manager_->FindAuthSession(
                auth_session_reply.auth_session_id()),
            nullptr);
}

TEST_F(UserDataAuthExTest, StartAuthSession) {
  PrepareArguments();
  start_auth_session_req_->mutable_account_id()->set_account_id(
      "foo@example.com");
  user_data_auth::StartAuthSessionReply auth_session_reply;
  {
    TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
    userdataauth_->StartAuthSession(
        *start_auth_session_req_,
        base::BindOnce(
            [](user_data_auth::StartAuthSessionReply* auth_reply_ptr,
               const user_data_auth::StartAuthSessionReply& reply) {
              *auth_reply_ptr = reply;
            },
            base::Unretained(&auth_session_reply)));
  }
  EXPECT_EQ(auth_session_reply.error(),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  std::optional<base::UnguessableToken> auth_session_id =
      AuthSession::GetTokenFromSerializedString(
          auth_session_reply.auth_session_id());
  EXPECT_TRUE(auth_session_id.has_value());
  EXPECT_THAT(userdataauth_->auth_session_manager_->FindAuthSession(
                  auth_session_id.value()),
              NotNull());
}

TEST_F(UserDataAuthExTest, AuthenticateAuthSessionInvalidToken) {
  PrepareArguments();
  std::string invalid_token = "invalid_token_16";
  authenticate_auth_session_req_->set_auth_session_id(invalid_token);
  user_data_auth::AuthenticateAuthSessionReply auth_session_reply;
  {
    TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
    userdataauth_->AuthenticateAuthSession(
        *authenticate_auth_session_req_,
        base::BindOnce(
            [](user_data_auth::AuthenticateAuthSessionReply* auth_reply_ptr,
               const user_data_auth::AuthenticateAuthSessionReply& reply) {
              *auth_reply_ptr = reply;
            },
            base::Unretained(&auth_session_reply)));
  }
  EXPECT_EQ(auth_session_reply.error(),
            user_data_auth::CRYPTOHOME_INVALID_AUTH_SESSION_TOKEN);
  EXPECT_FALSE(auth_session_reply.authenticated());
}

TEST_F(UserDataAuthExTest, MountAuthSessionInvalidToken) {
  PrepareArguments();
  std::string invalid_token = "invalid_token_16";
  user_data_auth::MountRequest mount_req;
  mount_req.set_auth_session_id(invalid_token);

  // Test.
  bool mount_done = false;
  {
    TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
    userdataauth_->DoMount(
        mount_req,
        base::BindOnce(
            [](bool* mount_done_ptr, const user_data_auth::MountReply& reply) {
              EXPECT_EQ(user_data_auth::CRYPTOHOME_INVALID_AUTH_SESSION_TOKEN,
                        reply.error());
              *mount_done_ptr = true;
            },
            base::Unretained(&mount_done)));
    ASSERT_EQ(TRUE, mount_done);
  }
}

TEST_F(UserDataAuthExTest, MountUnauthenticatedAuthSession) {
  // Setup.
  PrepareArguments();
  start_auth_session_req_->mutable_account_id()->set_account_id(
      "foo@example.com");
  user_data_auth::StartAuthSessionReply auth_session_reply;
  {
    TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
    userdataauth_->StartAuthSession(
        *start_auth_session_req_,
        base::BindOnce(
            [](user_data_auth::StartAuthSessionReply* auth_reply_ptr,
               const user_data_auth::StartAuthSessionReply& reply) {
              *auth_reply_ptr = reply;
            },
            base::Unretained(&auth_session_reply)));
  }
  EXPECT_EQ(auth_session_reply.error(),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  std::optional<base::UnguessableToken> auth_session_id =
      AuthSession::GetTokenFromSerializedString(
          auth_session_reply.auth_session_id());
  EXPECT_TRUE(auth_session_id.has_value());
  EXPECT_THAT(userdataauth_->auth_session_manager_->FindAuthSession(
                  auth_session_id.value()),
              NotNull());

  user_data_auth::MountRequest mount_req;
  mount_req.set_auth_session_id(auth_session_reply.auth_session_id());

  // Test.
  bool mount_done = false;
  {
    TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
    userdataauth_->DoMount(
        mount_req,
        base::BindOnce(
            [](bool* mount_done_ptr, const user_data_auth::MountReply& reply) {
              EXPECT_EQ(user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT,
                        reply.error());
              *mount_done_ptr = true;
            },
            base::Unretained(&mount_done)));
    ASSERT_EQ(TRUE, mount_done);
  }
}

TEST_F(UserDataAuthExTest, InvalidateAuthSession) {
  // Setup.
  PrepareArguments();
  start_auth_session_req_->mutable_account_id()->set_account_id(
      "foo@example.com");
  user_data_auth::StartAuthSessionReply auth_session_reply;
  {
    TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
    userdataauth_->StartAuthSession(
        *start_auth_session_req_,
        base::BindOnce(
            [](user_data_auth::StartAuthSessionReply* auth_reply_ptr,
               const user_data_auth::StartAuthSessionReply& reply) {
              *auth_reply_ptr = reply;
            },
            base::Unretained(&auth_session_reply)));
  }
  EXPECT_EQ(auth_session_reply.error(),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  std::optional<base::UnguessableToken> auth_session_id =
      AuthSession::GetTokenFromSerializedString(
          auth_session_reply.auth_session_id());
  EXPECT_TRUE(auth_session_id.has_value());
  EXPECT_THAT(userdataauth_->auth_session_manager_->FindAuthSession(
                  auth_session_id.value()),
              NotNull());

  // Test.
  user_data_auth::InvalidateAuthSessionRequest inv_auth_session_req;
  inv_auth_session_req.set_auth_session_id(
      auth_session_reply.auth_session_id());

  // Invalidate the AuthSession immediately.
  bool invalidated = false;
  {
    TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
    userdataauth_->InvalidateAuthSession(
        inv_auth_session_req,
        base::BindOnce(
            [](bool& invalidated_ref,
               const user_data_auth::InvalidateAuthSessionReply& reply) {
              EXPECT_EQ(user_data_auth::CRYPTOHOME_ERROR_NOT_SET,
                        reply.error());
              invalidated_ref = true;
            },
            std::ref(invalidated)));
    EXPECT_EQ(TRUE, invalidated);
  }

  EXPECT_THAT(userdataauth_->auth_session_manager_->FindAuthSession(
                  auth_session_id.value()),
              IsNull());
}

TEST_F(UserDataAuthExTest, ExtendAuthSession) {
  // Setup.
  PrepareArguments();

  start_auth_session_req_->mutable_account_id()->set_account_id(
      "foo@example.com");
  user_data_auth::StartAuthSessionReply auth_session_reply;
  {
    TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
    userdataauth_->StartAuthSession(
        *start_auth_session_req_,
        base::BindOnce(
            [](user_data_auth::StartAuthSessionReply* auth_reply_ptr,
               const user_data_auth::StartAuthSessionReply& reply) {
              *auth_reply_ptr = reply;
            },
            base::Unretained(&auth_session_reply)));
  }
  EXPECT_EQ(auth_session_reply.error(),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  std::optional<base::UnguessableToken> auth_session_id =
      AuthSession::GetTokenFromSerializedString(
          auth_session_reply.auth_session_id());
  EXPECT_TRUE(auth_session_id.has_value());
  EXPECT_THAT(userdataauth_->auth_session_manager_->FindAuthSession(
                  auth_session_id.value()),
              NotNull());

  // Test.
  user_data_auth::ExtendAuthSessionRequest ext_auth_session_req;
  ext_auth_session_req.set_auth_session_id(
      auth_session_reply.auth_session_id());
  ext_auth_session_req.set_extension_duration(kAuthSessionExtensionDuration);

  // Extend the AuthSession.
  bool extended = false;
  {
    TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
    userdataauth_->ExtendAuthSession(
        ext_auth_session_req,
        base::BindOnce(
            [](bool& extended_ref,
               const user_data_auth::ExtendAuthSessionReply& reply) {
              EXPECT_EQ(user_data_auth::CRYPTOHOME_ERROR_NOT_SET,
                        reply.error());
              extended_ref = true;
            },
            std::ref(extended)));
    EXPECT_EQ(TRUE, extended);
  }

  // Verify that timer has changed, within a resaonsable degree of error.
  AuthSession* auth_session =
      userdataauth_->auth_session_manager_->FindAuthSession(
          auth_session_id.value());
  auto requested_delay = auth_session->timer_.GetCurrentDelay();
  auto time_difference =
      (kAuthSessionTimeout + kAuthSessionExtension) - requested_delay;
  EXPECT_LT(time_difference, base::TimeDelta::FromSeconds(1));
}

class ChallengeResponseUserDataAuthExTest : public UserDataAuthExTest {
 public:
  static constexpr const char* kUser = "chromeos-user";
  static constexpr const char* kKeyLabel = "key";
  static constexpr const char* kKeyDelegateDBusService = "key-delegate-service";
  static constexpr const char* kSpkiDer = "fake-spki";
  static constexpr ChallengeSignatureAlgorithm kAlgorithm =
      CHALLENGE_RSASSA_PKCS1_V1_5_SHA256;
  static constexpr const char* kPasskey = "passkey";

  // GMock actions that perform reply to ChallengeCredentialsHelper operations:

  struct ReplyToVerifyKey {
    void operator()(const std::string& account_id,
                    const structure::ChallengePublicKeyInfo& public_key_info,
                    std::unique_ptr<KeyChallengeService> key_challenge_service,
                    ChallengeCredentialsHelper::VerifyKeyCallback callback) {
      std::move(callback).Run(is_key_valid);
    }

    bool is_key_valid = false;
  };

  struct ReplyToDecrypt {
    void operator()(
        const std::string& account_id,
        const structure::ChallengePublicKeyInfo& public_key_info,
        const structure::SignatureChallengeInfo& keyset_challenge_info,
        bool locked_to_single_user,
        std::unique_ptr<KeyChallengeService> key_challenge_service,
        ChallengeCredentialsHelper::DecryptCallback callback) {
      std::unique_ptr<brillo::SecureBlob> passkey_to_pass;
      if (passkey)
        passkey_to_pass = std::make_unique<brillo::SecureBlob>(*passkey);
      std::move(callback).Run(std::move(passkey_to_pass));
    }

    std::optional<brillo::SecureBlob> passkey;
  };

  ChallengeResponseUserDataAuthExTest() {
    key_data_.set_label(kKeyLabel);
    key_data_.set_type(KeyData::KEY_TYPE_CHALLENGE_RESPONSE);
    ChallengePublicKeyInfo* const key_public_info =
        key_data_.add_challenge_response_key();
    key_public_info->set_public_key_spki_der(kSpkiDer);
    key_public_info->add_signature_algorithm(kAlgorithm);

    public_key_info_ = proto::FromProto(*key_public_info);

    PrepareArguments();
    check_req_->mutable_account_id()->set_account_id(kUser);
    *check_req_->mutable_authorization_request()
         ->mutable_key()
         ->mutable_data() = key_data_;
    check_req_->mutable_authorization_request()
        ->mutable_key_delegate()
        ->set_dbus_service_name(kKeyDelegateDBusService);

    ON_CALL(key_challenge_service_factory_,
            New(/*bus=*/_, kKeyDelegateDBusService))
        .WillByDefault(InvokeWithoutArgs(
            []() { return std::make_unique<MockKeyChallengeService>(); }));
  }

  void SetUpActiveUserSession() {
    EXPECT_CALL(homedirs_, Exists(_)).WillRepeatedly(Return(true));
    EXPECT_CALL(keyset_management_, GetVaultKeyset(_, kKeyLabel))
        .WillRepeatedly(
            Invoke(this, &UserDataAuthExTest::GetNiceMockVaultKeyset));

    SetupMount(kUser);
    Credentials credentials(kUser, brillo::SecureBlob(kKey));
    credentials.set_key_data(key_data_);
    session_->SetCredentials(credentials);
  }

 protected:
  KeyData key_data_;
  structure::ChallengePublicKeyInfo public_key_info_;
};

// Tests the CheckKey lightweight check scenario for challenge-response
// credentials, where the credentials are verified without going through full
// decryption.
TEST_F(ChallengeResponseUserDataAuthExTest, LightweightCheckKey) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
  SetUpActiveUserSession();

  // Simulate a successful key verification.
  EXPECT_CALL(challenge_credentials_helper_,
              VerifyKey(kUser, StructureEquals(public_key_info_), _, _))
      .WillOnce(ReplyToVerifyKey{/*is_key_valid=*/true});

  CallCheckKeyAndVerify(user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
}

// Tests the CheckKey full check scenario for challenge-response credentials,
// with falling back from the failed lightweight check.
TEST_F(ChallengeResponseUserDataAuthExTest, FallbackLightweightCheckKey) {
  TaskGuard guard(this, UserDataAuth::TestThreadId::kMountThread);
  SetUpActiveUserSession();

  // Simulate a failure in the lightweight check and a successful decryption.
  EXPECT_CALL(challenge_credentials_helper_,
              VerifyKey(kUser, StructureEquals(public_key_info_), _, _))
      .WillOnce(ReplyToVerifyKey{/*is_key_valid=*/false});
  EXPECT_CALL(challenge_credentials_helper_,
              Decrypt(kUser, StructureEquals(public_key_info_), _, _, _, _))
      .WillOnce(ReplyToDecrypt{SecureBlob(kPasskey)});

  CallCheckKeyAndVerify(user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
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
    userdataauth_.reset(new UserDataAuth());

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
    PostToOriginAndBlock(base::BindOnce(
        [](UserDataAuth* userdataauth) {
          ASSERT_TRUE(userdataauth->Initialize());
        },
        base::Unretained(userdataauth_.get())));
    userdataauth_->set_dbus(bus_);
    userdataauth_->set_mount_thread_dbus(mount_bus_);
    PostToOriginAndBlock(base::BindOnce(
        [](UserDataAuth* userdataauth) {
          ASSERT_TRUE(userdataauth->PostDBusInitialize());
        },
        base::Unretained(userdataauth_.get())));
  }

 protected:
  // The thread on which the |userdataauth_| object is created. This is the same
  // as |userdataauth_->origin_thread_|.
  base::Thread origin_thread_;
};

TEST_F(UserDataAuthTestTasked, UploadAlertsCallback) {
  MetricsLibraryMock metrics;
  OverrideMetricsLibraryForTesting(&metrics);

  Tpm::AlertsData alert_data;
  for (int i = 0; i < Tpm::kAlertsNumber; i++)
    alert_data.counters[i] = 1;

  // Checks that GetAlertsData is called during/after initialization.
  EXPECT_CALL(tpm_, GetAlertsData(_))
      .WillOnce(
          DoAll(SetArgPointee<0>(alert_data), ReturnError<TPMErrorBase>()));

  // Checks that the metrics are reported.
  constexpr char kDiskCleanupResultsHistogram[] =
      "Cryptohome.DiskCleanupResult";
  EXPECT_CALL(metrics, SendEnumToUMA(kDiskCleanupResultsHistogram, _, _))
      .WillRepeatedly(Return(true));

  // Checks that the metrics are reported.
  constexpr char kTpmAlertsHistogram[] = "Platform.TPM.HardwareAlerts";
  EXPECT_CALL(metrics, SendEnumToUMA(kTpmAlertsHistogram, _, _))
      .Times(Tpm::kAlertsNumber);

  InitializeUserDataAuth();

  ClearMetricsLibraryForTesting();
}

TEST_F(UserDataAuthTestTasked, UploadAlertsCallbackPeriodical) {
  // Checks that GetAlertsData is called periodically.
  EXPECT_CALL(tpm_, GetAlertsData(_)).Times(1);

  InitializeUserDataAuth();

  EXPECT_CALL(tpm_, GetAlertsData(_)).Times(5);

  FastForwardBy(base::TimeDelta::FromMilliseconds(kUploadAlertsPeriodMS) * 5);
}

TEST_F(UserDataAuthTestThreaded, DetectEnterpriseOwnership) {
  // If asked, this machine is enterprise owned.
  static const std::string true_str = "true";
  brillo::Blob true_value(true_str.begin(), true_str.end());
  true_value.push_back(0);
  EXPECT_CALL(*attrs_, Get("enterprise.owned", _))
      .WillOnce(DoAll(SetArgPointee<1>(true_value), Return(true)));

  EXPECT_CALL(homedirs_, set_enterprise_owned(true)).WillOnce(Return());

  InitializeUserDataAuth();
}

TEST_F(UserDataAuthTestThreaded, ShutdownTask) {
  InitializeUserDataAuth();
  EXPECT_CALL(*mount_bus_, ShutdownAndBlock()).Times(1);
  PostToOriginAndBlock(base::BindLambdaForTesting([this]() {
    // Destruct the |userdataauth_| object.
    userdataauth_.reset();
  }));
}

}  // namespace cryptohome
