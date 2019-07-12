// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/userdataauth.h"

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <chaps/token_manager_client_mock.h>
#include <vector>

#include "cryptohome/mock_arc_disk_quota.h"
#include "cryptohome/mock_crypto.h"
#include "cryptohome/mock_homedirs.h"
#include "cryptohome/mock_install_attributes.h"
#include "cryptohome/mock_le_credential_backend.h"
#include "cryptohome/mock_mount.h"
#include "cryptohome/mock_mount_factory.h"
#include "cryptohome/mock_pkcs11_init.h"
#include "cryptohome/mock_platform.h"
#include "cryptohome/mock_tpm.h"
#include "cryptohome/mock_tpm_init.h"
#include "cryptohome/mock_vault_keyset.h"
#include "cryptohome/obfuscated_username.h"
#include "cryptohome/protobuf_test_utils.h"

using base::FilePath;
using brillo::SecureBlob;

using ::testing::_;
using ::testing::AtLeast;
using ::testing::ElementsAre;
using ::testing::EndsWith;
using ::testing::Eq;
using ::testing::Invoke;
using ::testing::Mock;
using ::testing::NiceMock;
using ::testing::Pointee;
using ::testing::Property;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::WithArgs;

namespace cryptohome {

namespace {

bool AssignSalt(size_t size, SecureBlob* salt) {
  SecureBlob fake_salt(size, 'S');
  salt->swap(fake_salt);
  return true;
}

}  // namespace

// UserDataAuthTestNotInitialized is a test fixture that does not call
// UserDataAuth::Initialize() during setup. Therefore, it's suited to tests that
// can be conducted without calling UserDataAuth::Initialize(), or for tests
// that wants some flexibility before calling UserDataAuth::Initialize(), note
// that in this case the test have to call UserDataAuth::Initialize().
class UserDataAuthTestNotInitialized : public ::testing::Test {
 public:
  UserDataAuthTestNotInitialized() = default;
  ~UserDataAuthTestNotInitialized() override = default;

  void SetUp() override {
    tpm_init_.set_tpm(&tpm_);

    userdataauth_.set_crypto(&crypto_);
    userdataauth_.set_homedirs(&homedirs_);
    userdataauth_.set_install_attrs(&attrs_);
    userdataauth_.set_tpm(&tpm_);
    userdataauth_.set_tpm_init(&tpm_init_);
    userdataauth_.set_platform(&platform_);
    userdataauth_.set_chaps_client(&chaps_client_);
    userdataauth_.set_arc_disk_quota(&arc_disk_quota_);
    userdataauth_.set_pkcs11_init(&pkcs11_init_);
    userdataauth_.set_disable_threading(true);
    homedirs_.set_crypto(&crypto_);
    homedirs_.set_platform(&platform_);
    ON_CALL(homedirs_, Init(_, _, _)).WillByDefault(Return(true));
    // Empty token list by default.  The effect is that there are no attempts
    // to unload tokens unless a test explicitly sets up the token list.
    ON_CALL(chaps_client_, GetTokenList(_, _)).WillByDefault(Return(true));
    // Skip CleanUpStaleMounts by default.
    ON_CALL(platform_, GetMountsBySourcePrefix(_, _))
        .WillByDefault(Return(false));
    // Setup fake salt by default.
    ON_CALL(crypto_, GetOrCreateSalt(_, _, _, _))
        .WillByDefault(WithArgs<1, 3>(Invoke(AssignSalt)));
    // ARC Disk Quota initialization will do nothing.
    ON_CALL(arc_disk_quota_, Initialize()).WillByDefault(Return());
  }

  // This is a utility function for tests to setup a mount for a particular
  // user. After calling this function, |mount_| is available for use.
  void SetupMount(const std::string& username) {
    mount_ = new NiceMock<MockMount>();
    userdataauth_.set_mount_for_user(username, mount_.get());
  }

  // This is a helper function that compute the obfuscated username with the
  // fake salt.
  std::string GetObfuscatedUsername(const std::string& username) {
    brillo::SecureBlob salt;
    AssignSalt(CRYPTOHOME_DEFAULT_SALT_LENGTH, &salt);
    return BuildObfuscatedUsername(username, salt);
  }

 protected:
  // Mock Crypto object, will be passed to UserDataAuth for its internal use.
  NiceMock<MockCrypto> crypto_;

  // Mock HomeDirs object, will be passed to UserDataAuth for its internal use.
  NiceMock<MockHomeDirs> homedirs_;

  // Mock InstallAttributes object, will be passed to UserDataAuth for its
  // internal use.
  NiceMock<MockInstallAttributes> attrs_;

  // Mock Platform object, will be passed to UserDataAuth for its internal use.
  NiceMock<MockPlatform> platform_;

  // Mock TPM object, will be passed to UserDataAuth for its internal use.
  NiceMock<MockTpm> tpm_;

  // Mock TPM Init object, will be passed to UserDataAuth for its internal use.
  NiceMock<MockTpmInit> tpm_init_;

  // Mock ARC Disk Quota object, will be passed to UserDataAuth for its internal
  // use.
  NiceMock<MockArcDiskQuota> arc_disk_quota_;

  // Mock chaps token manager client, will be passed to UserDataAuth for its
  // internal use.
  NiceMock<chaps::TokenManagerClientMock> chaps_client_;

  // Mock PKCS#11 init object, will be passed to UserDataAuth for its internal
  // use.
  NiceMock<MockPkcs11Init> pkcs11_init_;

  // This is used to hold the mount object when we create a mock mount with
  // SetupMount().
  scoped_refptr<MockMount> mount_;

  // Declare |userdataauth_| last so it gets destroyed before all the mocks.
  // This is important because otherwise the background thread may call into
  // mocks that have already been destroyed.
  UserDataAuth userdataauth_;

 private:
  DISALLOW_COPY_AND_ASSIGN(UserDataAuthTestNotInitialized);
};

// Standard, fully initialized UserDataAuth test fixture
class UserDataAuthTest : public UserDataAuthTestNotInitialized {
 public:
  UserDataAuthTest() = default;
  ~UserDataAuthTest() override = default;

  void SetUp() override {
    UserDataAuthTestNotInitialized::SetUp();
    ASSERT_TRUE(userdataauth_.Initialize());
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(UserDataAuthTest);
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
    user_data_auth::CryptohomeErrorCode_MAX == 33,
    "user_data_auth::CrytpohomeErrorCode's element count is incorrect");
static_assert(cryptohome::CryptohomeErrorCode_MAX == 33,
              "cryptohome::CrytpohomeErrorCode's element count is incorrect");
}  // namespace CryptohomeErrorCodeEquivalenceTest

TEST_F(UserDataAuthTest, IsMounted) {
  // By default there are no mount right after initialization
  EXPECT_FALSE(userdataauth_.IsMounted());
  EXPECT_FALSE(userdataauth_.IsMounted("foo@gmail.com"));

  // Add a mount associated with foo@gmail.com
  SetupMount("foo@gmail.com");

  // Test the code path that doesn't specify a user, and when there's a mount
  // that's unmounted.
  EXPECT_CALL(*mount_, IsMounted()).WillOnce(Return(false));
  EXPECT_FALSE(userdataauth_.IsMounted());

  // Test to see if is_ephemeral works and test the code path that doesn't
  // specify a user.
  bool is_ephemeral = true;
  EXPECT_CALL(*mount_, IsMounted()).WillOnce(Return(true));
  EXPECT_CALL(*mount_, IsNonEphemeralMounted()).WillOnce(Return(true));
  EXPECT_TRUE(userdataauth_.IsMounted("", &is_ephemeral));
  EXPECT_FALSE(is_ephemeral);

  // Test to see if is_ephemeral works, and test the code path that specify the
  // user.
  EXPECT_CALL(*mount_, IsMounted()).WillOnce(Return(true));
  EXPECT_CALL(*mount_, IsNonEphemeralMounted()).WillOnce(Return(false));
  EXPECT_TRUE(userdataauth_.IsMounted("foo@gmail.com", &is_ephemeral));
  EXPECT_TRUE(is_ephemeral);

  // Note: IsMounted will not be called in this case.
  EXPECT_FALSE(userdataauth_.IsMounted("bar@gmail.com", &is_ephemeral));
  EXPECT_FALSE(is_ephemeral);
}

TEST_F(UserDataAuthTest, Unmount) {
  // Unmount sanity test.
  // The tests on whether stale mount are cleaned up is in another set of tests
  // called CleanUpStale_*

  // Add a mount associated with foo@gmail.com
  SetupMount("foo@gmail.com");

  // Unmount will be successful.
  EXPECT_CALL(*mount_, UnmountCryptohome()).WillOnce(Return(true));
  // If anyone asks, this mount is still mounted.
  ON_CALL(*mount_, IsMounted()).WillByDefault(Return(true));

  // Unmount should be successful.
  EXPECT_TRUE(userdataauth_.Unmount());

  // It should be unmounted in the end.
  EXPECT_FALSE(userdataauth_.IsMounted());

  // Add another mount associated with bar@gmail.com
  SetupMount("bar@gmail.com");

  // Unmount will be unsuccessful.
  EXPECT_CALL(*mount_, UnmountCryptohome()).WillOnce(Return(false));
  // If anyone asks, this mount is still mounted.
  ON_CALL(*mount_, IsMounted()).WillByDefault(Return(true));

  // Unmount should be honest about failures.
  EXPECT_FALSE(userdataauth_.Unmount());

  // Unmount will remove all mounts even if it failed.
  EXPECT_FALSE(userdataauth_.IsMounted());
}

TEST_F(UserDataAuthTest, InitializePkcs11Success) {
  // This test the most common success case for PKCS#11 initialization.

  EXPECT_FALSE(userdataauth_.IsMounted());

  // Add a mount associated with foo@gmail.com
  SetupMount("foo@gmail.com");

  // PKCS#11 will initialization works only when it's mounted.
  ON_CALL(*mount_, IsMounted()).WillByDefault(Return(true));
  // The initialization code should at least check, right?
  EXPECT_CALL(*mount_, IsMounted())
      .Times(AtLeast(1))
      .WillRepeatedly(Return(true));
  // |mount_| should get a request to insert PKCS#11 token.
  EXPECT_CALL(*mount_, InsertPkcs11Token()).WillOnce(Return(true));

  userdataauth_.InitializePkcs11(mount_.get());

  EXPECT_EQ(mount_->pkcs11_state(), cryptohome::Mount::kIsInitialized);
}

TEST_F(UserDataAuthTest, InitializePkcs11TpmNotOwned) {
  // Test when TPM isn't owned.

  // Add a mount associated with foo@gmail.com
  SetupMount("foo@gmail.com");

  // PKCS#11 will initialization works only when it's mounted.
  ON_CALL(*mount_, IsMounted()).WillByDefault(Return(true));

  // |mount_| should not get a request to insert PKCS#11 token.
  EXPECT_CALL(*mount_, InsertPkcs11Token()).Times(0);

  // TPM is enabled but not owned.
  ON_CALL(tpm_, IsEnabled()).WillByDefault(Return(true));
  EXPECT_CALL(tpm_, IsOwned()).Times(AtLeast(1)).WillRepeatedly(Return(false));

  userdataauth_.InitializePkcs11(mount_.get());

  EXPECT_EQ(mount_->pkcs11_state(), cryptohome::Mount::kIsWaitingOnTPM);

  // We'll need to call InsertPkcs11Token() and IsEnabled() later in the test.
  Mock::VerifyAndClearExpectations(mount_.get());
  Mock::VerifyAndClearExpectations(&tpm_);

  // Next check when the TPM is now owned.

  // The initialization code should at least check, right?
  EXPECT_CALL(*mount_, IsMounted())
      .Times(AtLeast(1))
      .WillRepeatedly(Return(true));

  // |mount_| should get a request to insert PKCS#11 token.
  EXPECT_CALL(*mount_, InsertPkcs11Token()).WillOnce(Return(true));

  // TPM is enabled and owned.
  ON_CALL(tpm_, IsEnabled()).WillByDefault(Return(true));
  EXPECT_CALL(tpm_, IsOwned()).Times(AtLeast(1)).WillRepeatedly(Return(true));

  userdataauth_.InitializePkcs11(mount_.get());

  EXPECT_EQ(mount_->pkcs11_state(), cryptohome::Mount::kIsInitialized);
}

TEST_F(UserDataAuthTest, InitializePkcs11Unmounted) {
  // Add a mount associated with foo@gmail.com
  SetupMount("foo@gmail.com");

  ON_CALL(*mount_, IsMounted()).WillByDefault(Return(false));
  // The initialization code should at least check, right?
  EXPECT_CALL(*mount_, IsMounted())
      .Times(AtLeast(1))
      .WillRepeatedly(Return(false));

  // |mount_| should not get a request to insert PKCS#11 token.
  EXPECT_CALL(*mount_, InsertPkcs11Token()).Times(0);

  userdataauth_.InitializePkcs11(mount_.get());

  EXPECT_EQ(mount_->pkcs11_state(), cryptohome::Mount::kUninitialized);
}

TEST_F(UserDataAuthTest, Pkcs11IsTpmTokenReady) {
  // When there's no mount at all, it should be true.
  EXPECT_TRUE(userdataauth_.Pkcs11IsTpmTokenReady());

  constexpr char kUsername1[] = "foo@gmail.com";
  constexpr char kUsername2[] = "bar@gmail.com";

  // Check when there's 1 mount, and it's initialized.
  scoped_refptr<NiceMock<MockMount>> mount1 = new NiceMock<MockMount>();
  userdataauth_.set_mount_for_user(kUsername1, mount1.get());
  EXPECT_CALL(*mount1, pkcs11_state())
      .WillOnce(Return(cryptohome::Mount::kIsInitialized));
  EXPECT_TRUE(userdataauth_.Pkcs11IsTpmTokenReady());

  // Check various other PKCS#11 states.
  EXPECT_CALL(*mount1, pkcs11_state())
      .WillOnce(Return(cryptohome::Mount::kUninitialized));
  EXPECT_FALSE(userdataauth_.Pkcs11IsTpmTokenReady());

  EXPECT_CALL(*mount1, pkcs11_state())
      .WillOnce(Return(cryptohome::Mount::kIsWaitingOnTPM));
  EXPECT_FALSE(userdataauth_.Pkcs11IsTpmTokenReady());

  EXPECT_CALL(*mount1, pkcs11_state())
      .WillOnce(Return(cryptohome::Mount::kIsBeingInitialized));
  EXPECT_FALSE(userdataauth_.Pkcs11IsTpmTokenReady());

  EXPECT_CALL(*mount1, pkcs11_state())
      .WillOnce(Return(cryptohome::Mount::kIsFailed));
  EXPECT_FALSE(userdataauth_.Pkcs11IsTpmTokenReady());

  EXPECT_CALL(*mount1, pkcs11_state())
      .WillOnce(Return(cryptohome::Mount::kInvalidState));
  EXPECT_FALSE(userdataauth_.Pkcs11IsTpmTokenReady());

  // Check when there's another mount.
  scoped_refptr<NiceMock<MockMount>> mount2 = new NiceMock<MockMount>();
  userdataauth_.set_mount_for_user(kUsername2, mount2.get());

  // Both is initialized.
  EXPECT_CALL(*mount1, pkcs11_state())
      .WillOnce(Return(cryptohome::Mount::kIsInitialized));
  EXPECT_CALL(*mount2, pkcs11_state())
      .WillOnce(Return(cryptohome::Mount::kIsInitialized));
  EXPECT_TRUE(userdataauth_.Pkcs11IsTpmTokenReady());

  // Only one is initialized.
  EXPECT_CALL(*mount1, pkcs11_state())
      .WillOnce(Return(cryptohome::Mount::kIsInitialized));
  EXPECT_CALL(*mount2, pkcs11_state())
      .WillOnce(Return(cryptohome::Mount::kUninitialized));
  EXPECT_FALSE(userdataauth_.Pkcs11IsTpmTokenReady());

  // Both uninitialized.
  EXPECT_CALL(*mount1, pkcs11_state())
      .WillOnce(Return(cryptohome::Mount::kUninitialized));
  EXPECT_CALL(*mount2, pkcs11_state())
      .WillOnce(Return(cryptohome::Mount::kUninitialized));
  EXPECT_FALSE(userdataauth_.Pkcs11IsTpmTokenReady());
}

TEST_F(UserDataAuthTest, Pkcs11GetTpmTokenInfo) {
  user_data_auth::TpmTokenInfo info;

  constexpr CK_SLOT_ID kSlot = 42;
  constexpr char kUsername1[] = "foo@gmail.com";

  // Check the system token case.
  EXPECT_CALL(pkcs11_init_, GetTpmTokenSlotForPath(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(kSlot), Return(true)));
  info = userdataauth_.Pkcs11GetTpmTokenInfo("");

  EXPECT_EQ(info.label(), Pkcs11Init::kDefaultSystemLabel);
  EXPECT_EQ(info.user_pin(), Pkcs11Init::kDefaultPin);
  EXPECT_EQ(info.slot(), kSlot);

  // Check the user token case.
  EXPECT_CALL(pkcs11_init_, GetTpmTokenSlotForPath(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(kSlot), Return(true)));
  info = userdataauth_.Pkcs11GetTpmTokenInfo(kUsername1);

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
  info = userdataauth_.Pkcs11GetTpmTokenInfo("");
  EXPECT_EQ(info.slot(), -1);

  EXPECT_CALL(pkcs11_init_, GetTpmTokenSlotForPath(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(kSlot), Return(false)));
  info = userdataauth_.Pkcs11GetTpmTokenInfo(kUsername1);
  EXPECT_EQ(info.slot(), -1);
}

TEST_F(UserDataAuthTestNotInitialized, InstallAttributesEnterpriseOwned) {
  EXPECT_CALL(attrs_, Init(_)).WillOnce(Return(true));

  std::string str_true = "true";
  std::vector<uint8_t> blob_true(str_true.begin(), str_true.end());
  blob_true.push_back(0);

  EXPECT_CALL(attrs_, Get("enterprise.owned", _))
      .WillOnce(DoAll(SetArgPointee<1>(blob_true), Return(true)));
  userdataauth_.Initialize();

  EXPECT_TRUE(userdataauth_.IsEnterpriseOwned());
}

TEST_F(UserDataAuthTestNotInitialized, InstallAttributesNotEnterpriseOwned) {
  EXPECT_CALL(attrs_, Init(_)).WillOnce(Return(true));

  std::string str_true = "false";
  std::vector<uint8_t> blob_true(str_true.begin(), str_true.end());
  blob_true.push_back(0);

  EXPECT_CALL(attrs_, Get("enterprise.owned", _))
      .WillOnce(DoAll(SetArgPointee<1>(blob_true), Return(true)));
  userdataauth_.Initialize();

  EXPECT_FALSE(userdataauth_.IsEnterpriseOwned());
}

TEST_F(UserDataAuthTestNotInitialized, InitializeArcDiskQuota) {
  EXPECT_CALL(arc_disk_quota_, Initialize()).Times(1);
  EXPECT_TRUE(userdataauth_.Initialize());
}

TEST_F(UserDataAuthTestNotInitialized, IsArcQuotaSupported) {
  EXPECT_CALL(arc_disk_quota_, IsQuotaSupported()).WillOnce(Return(true));
  EXPECT_TRUE(userdataauth_.IsArcQuotaSupported());

  EXPECT_CALL(arc_disk_quota_, IsQuotaSupported()).WillOnce(Return(false));
  EXPECT_FALSE(userdataauth_.IsArcQuotaSupported());
}

TEST_F(UserDataAuthTestNotInitialized, GetCurrentSpaceFoArcUid) {
  constexpr uid_t kUID = 42;  // The Answer.
  constexpr int64_t kSpaceUsage = 98765432198765;

  EXPECT_CALL(arc_disk_quota_, GetCurrentSpaceForUid(kUID))
      .WillOnce(Return(kSpaceUsage));
  EXPECT_EQ(kSpaceUsage, userdataauth_.GetCurrentSpaceForArcUid(kUID));
}

TEST_F(UserDataAuthTestNotInitialized, GetCurrentSpaceForArcGid) {
  constexpr uid_t kGID = 42;  // Yet another answer.
  constexpr int64_t kSpaceUsage = 87654321987654;

  EXPECT_CALL(arc_disk_quota_, GetCurrentSpaceForGid(kGID))
      .WillOnce(Return(kSpaceUsage));
  EXPECT_EQ(kSpaceUsage, userdataauth_.GetCurrentSpaceForArcGid(kGID));
}

// ======================= CleanUpStaleMounts tests ==========================

namespace {

struct Mounts {
  const FilePath src;
  const FilePath dst;
};

const Mounts kShadowMounts[] = {
    {FilePath("/home/.shadow/a"), FilePath("/home/user/0")},
    {FilePath("/home/.shadow/a"), FilePath("/home/root/0")},
    {FilePath("/home/.shadow/b"), FilePath("/home/user/1")},
    {FilePath("/home/.shadow/a"), FilePath("/home/chronos/user")},
    {FilePath("/home/.shadow/b"), FilePath("/home/root/1")},
    {FilePath("/home/user/b/Downloads"),
     FilePath("/home/user/b/MyFiles/Downloads")},
    {FilePath("/home/chronos/u-b/Downloads"),
     FilePath("/home/chronos/u-b/MyFiles/Downloads")},
    {FilePath("/home/chronos/user/Downloads"),
     FilePath("/home/chronos/user/MyFiles/Downloads")},
};

const int kShadowMountsCount = 8;

const Mounts kLoopDevMounts[] = {
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
const Platform::LoopDevice kLoopDevices[] = {
    {FilePath("/mnt/stateful_partition/encrypted.block"),
     FilePath("/dev/loop0")},
    {FilePath("/run/cryptohome/ephemeral_data/1"), FilePath("/dev/loop7")},
};

const FilePath kSparseFiles[] = {
    FilePath("/run/cryptohome/ephemeral_data/2"),
    FilePath("/run/cryptohome/ephemeral_data/1"),
};

// Utility functions used by CleanUpStaleMounts tests.
bool StaleShadowMounts(const FilePath& from_prefix,
                       std::multimap<const FilePath, const FilePath>* mounts) {
  if (from_prefix.value() == "/home/.shadow") {
    if (!mounts)
      return true;
    const struct Mounts* m = &kShadowMounts[0];
    for (int i = 0; i < kShadowMountsCount; ++i, ++m) {
      mounts->insert(std::pair<const FilePath, const FilePath>(m->src, m->dst));
    }
    return true;
  }
  return false;
}

bool LoopDeviceMounts(std::multimap<const FilePath, const FilePath>* mounts) {
  if (!mounts)
    return false;
  const Mounts* m = kLoopDevMounts;
  for (int i = 0; i < arraysize(kLoopDevMounts); ++i, ++m)
    mounts->insert(std::make_pair(m->src, m->dst));
  return true;
}

bool EnumerateSparseFiles(const base::FilePath& path,
                          bool is_recursive,
                          std::vector<base::FilePath>* ent_list) {
  if (path != FilePath(kEphemeralCryptohomeDir).Append(kSparseFileDir))
    return false;
  ent_list->insert(ent_list->end(), std::begin(kSparseFiles),
                   std::end(kSparseFiles));
  return true;
}

}  // namespace

TEST_F(UserDataAuthTest, CleanUpStale_NoOpenFiles_Ephemeral) {
  // Check that when we have ephemeral mounts, no active mounts,
  // and no open filehandles, all stale mounts are unmounted, loop device is
  // detached and sparse file is deleted.

  EXPECT_CALL(platform_, GetMountsBySourcePrefix(homedirs_.shadow_root(), _))
      .WillOnce(Return(false));
  EXPECT_CALL(platform_, GetAttachedLoopDevices())
      .WillRepeatedly(Return(std::vector<Platform::LoopDevice>(
          std::begin(kLoopDevices), std::end(kLoopDevices))));
  EXPECT_CALL(platform_, GetLoopDeviceMounts(_))
      .WillOnce(Invoke(LoopDeviceMounts));
  EXPECT_CALL(
      platform_,
      EnumerateDirectoryEntries(
          FilePath(kEphemeralCryptohomeDir).Append(kSparseFileDir), _, _))
      .WillOnce(Invoke(EnumerateSparseFiles));
  EXPECT_CALL(platform_, GetProcessesWithOpenFiles(_, _))
      .Times(kEphemeralMountsCount);

  for (int i = 0; i < kEphemeralMountsCount; ++i) {
    EXPECT_CALL(platform_, Unmount(kLoopDevMounts[i].dst, true, _))
        .WillRepeatedly(Return(true));
  }
  EXPECT_CALL(platform_, DetachLoop(FilePath("/dev/loop7")))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, DeleteFile(kSparseFiles[0], _)).WillOnce(Return(true));
  EXPECT_CALL(platform_, DeleteFile(kSparseFiles[1], _)).WillOnce(Return(true));
  EXPECT_CALL(platform_, DeleteFile(kLoopDevMounts[0].dst, _))
      .WillOnce(Return(true));
  EXPECT_FALSE(userdataauth_.CleanUpStaleMounts(false));
}

TEST_F(UserDataAuthTest, CleanUpStale_OpenLegacy_Ephemeral) {
  // Check that when we have ephemeral mounts, no active mounts,
  // and some open filehandles to the legacy homedir, everything is kept.

  EXPECT_CALL(platform_, GetMountsBySourcePrefix(homedirs_.shadow_root(), _))
      .WillOnce(Return(false));
  EXPECT_CALL(platform_, GetAttachedLoopDevices())
      .WillRepeatedly(Return(std::vector<Platform::LoopDevice>(
          std::begin(kLoopDevices), std::end(kLoopDevices))));
  EXPECT_CALL(platform_, GetLoopDeviceMounts(_))
      .WillOnce(Invoke(LoopDeviceMounts));
  EXPECT_CALL(
      platform_,
      EnumerateDirectoryEntries(
          FilePath(kEphemeralCryptohomeDir).Append(kSparseFileDir), _, _))
      .WillOnce(Invoke(EnumerateSparseFiles));
  EXPECT_CALL(platform_, GetProcessesWithOpenFiles(_, _))
      .Times(kEphemeralMountsCount - 1);
  std::vector<ProcessInformation> processes(1);
  processes[0].set_process_id(1);
  EXPECT_CALL(platform_,
              GetProcessesWithOpenFiles(FilePath("/home/chronos/user"), _))
      .Times(1)
      .WillRepeatedly(SetArgPointee<1>(processes));

  EXPECT_CALL(platform_, Unmount(_, _, _)).Times(0);
  EXPECT_TRUE(userdataauth_.CleanUpStaleMounts(false));
}

TEST_F(UserDataAuthTest, CleanUpStale_OpenLegacy_Ephemeral_Forced) {
  // Check that when we have ephemeral mounts, no active mounts,
  // and some open filehandles to the legacy homedir, but cleanup is forced,
  // all mounts are unmounted, loop device is detached and file is deleted.

  EXPECT_CALL(platform_, GetMountsBySourcePrefix(homedirs_.shadow_root(), _))
      .WillOnce(Return(false));
  EXPECT_CALL(platform_, GetAttachedLoopDevices())
      .WillRepeatedly(Return(std::vector<Platform::LoopDevice>(
          std::begin(kLoopDevices), std::end(kLoopDevices))));
  EXPECT_CALL(platform_, GetLoopDeviceMounts(_))
      .WillOnce(Invoke(LoopDeviceMounts));
  EXPECT_CALL(
      platform_,
      EnumerateDirectoryEntries(
          FilePath(kEphemeralCryptohomeDir).Append(kSparseFileDir), _, _))
      .WillOnce(Invoke(EnumerateSparseFiles));
  EXPECT_CALL(platform_, GetProcessesWithOpenFiles(_, _)).Times(0);

  for (int i = 0; i < kEphemeralMountsCount; ++i) {
    EXPECT_CALL(platform_, Unmount(kLoopDevMounts[i].dst, true, _))
        .WillRepeatedly(Return(true));
  }
  EXPECT_CALL(platform_, DetachLoop(FilePath("/dev/loop7")))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, DeleteFile(kSparseFiles[0], _)).WillOnce(Return(true));
  EXPECT_CALL(platform_, DeleteFile(kSparseFiles[1], _)).WillOnce(Return(true));
  EXPECT_CALL(platform_, DeleteFile(kLoopDevMounts[0].dst, _))
      .WillOnce(Return(true));
  EXPECT_FALSE(userdataauth_.CleanUpStaleMounts(true));
}

TEST_F(UserDataAuthTest, CleanUpStale_EmptyMap_NoOpenFiles_ShadowOnly) {
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
  EXPECT_CALL(platform_, GetProcessesWithOpenFiles(_, _))
      .Times(kShadowMountsCount);
  EXPECT_CALL(platform_, Unmount(_, true, _))
      .Times(kShadowMountsCount)
      .WillRepeatedly(Return(true));
  EXPECT_FALSE(userdataauth_.CleanUpStaleMounts(false));
}

TEST_F(UserDataAuthTest, CleanUpStale_EmptyMap_OpenLegacy_ShadowOnly) {
  // Check that when we have a bunch of stale shadow mounts, no active mounts,
  // and some open filehandles to the legacy homedir, all mounts without
  // filehandles are unmounted.

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
  std::vector<ProcessInformation> processes(1);
  processes[0].set_process_id(1);
  EXPECT_CALL(platform_, GetProcessesWithOpenFiles(_, _))
      .Times(kShadowMountsCount - 1);
  EXPECT_CALL(platform_,
              GetProcessesWithOpenFiles(FilePath("/home/chronos/user"), _))
      .Times(1)
      .WillRepeatedly(SetArgPointee<1>(processes));
  EXPECT_CALL(
      platform_,
      Unmount(Property(&FilePath::value,
                       AnyOf(EndsWith("/1"), EndsWith("/MyFiles/Downloads"))),
              true, _))
      .Times(5)
      .WillRepeatedly(Return(true));
  EXPECT_TRUE(userdataauth_.CleanUpStaleMounts(false));
}

TEST_F(UserDataAuthTest, StartMigrateToDircryptoSanity) {
  constexpr char kUsername1[] = "foo@gmail.com";

  user_data_auth::StartMigrateToDircryptoRequest request;
  request.mutable_account_id()->set_account_id(kUsername1);
  request.set_minimal_migration(false);

  SetupMount(kUsername1);

  EXPECT_CALL(*mount_, MigrateToDircrypto(_, MigrationType::FULL))
      .WillOnce(Return(true));

  int success_cnt = 0;
  userdataauth_.StartMigrateToDircrypto(
      request,
      base::Bind(
          [](int* success_cnt_ptr,
             const user_data_auth::DircryptoMigrationProgress& progress) {
            EXPECT_EQ(progress.status(),
                      user_data_auth::DIRCRYPTO_MIGRATION_SUCCESS);
            (*success_cnt_ptr)++;
          },
          base::Unretained(&success_cnt)));

  EXPECT_EQ(success_cnt, 1);
}

TEST_F(UserDataAuthTest, StartMigrateToDircryptoFailure) {
  constexpr char kUsername1[] = "foo@gmail.com";

  user_data_auth::StartMigrateToDircryptoRequest request;
  request.mutable_account_id()->set_account_id(kUsername1);
  request.set_minimal_migration(false);

  // Test mount non-existent.
  int call_cnt = 0;
  userdataauth_.StartMigrateToDircrypto(
      request,
      base::Bind(
          [](int* call_cnt_ptr,
             const user_data_auth::DircryptoMigrationProgress& progress) {
            EXPECT_EQ(progress.status(),
                      user_data_auth::DIRCRYPTO_MIGRATION_FAILED);
            (*call_cnt_ptr)++;
          },
          base::Unretained(&call_cnt)));

  EXPECT_EQ(call_cnt, 1);

  // Test MigrateToDircrypto failed
  SetupMount(kUsername1);

  EXPECT_CALL(*mount_, MigrateToDircrypto(_, MigrationType::FULL))
      .WillOnce(Return(false));

  call_cnt = 0;
  userdataauth_.StartMigrateToDircrypto(
      request,
      base::Bind(
          [](int* call_cnt_ptr,
             const user_data_auth::DircryptoMigrationProgress& progress) {
            EXPECT_EQ(progress.status(),
                      user_data_auth::DIRCRYPTO_MIGRATION_FAILED);
            (*call_cnt_ptr)++;
          },
          base::Unretained(&call_cnt)));

  EXPECT_EQ(call_cnt, 1);
}

TEST_F(UserDataAuthTest, NeedsDircryptoMigration) {
  bool result;
  AccountIdentifier account;
  account.set_account_id("foo@gmail.com");

  // Test the case when we are forced to use eCryptfs, and thus no migration is
  // needed.
  userdataauth_.set_force_ecryptfs(true);
  EXPECT_CALL(homedirs_, Exists(_)).WillOnce(Return(true));
  EXPECT_EQ(userdataauth_.NeedsDircryptoMigration(account, &result),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  EXPECT_FALSE(result);

  // Test the case when dircrypto is already in use.
  userdataauth_.set_force_ecryptfs(false);
  EXPECT_CALL(homedirs_, NeedsDircryptoMigration(_)).WillOnce(Return(false));
  EXPECT_CALL(homedirs_, Exists(_)).WillOnce(Return(true));
  EXPECT_EQ(userdataauth_.NeedsDircryptoMigration(account, &result),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  EXPECT_FALSE(result);

  // Test the case when eCryptfs is being used.
  userdataauth_.set_force_ecryptfs(false);
  EXPECT_CALL(homedirs_, NeedsDircryptoMigration(_)).WillOnce(Return(true));
  EXPECT_CALL(homedirs_, Exists(_)).WillOnce(Return(true));
  EXPECT_EQ(userdataauth_.NeedsDircryptoMigration(account, &result),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  EXPECT_TRUE(result);

  // Test for account not found.
  EXPECT_CALL(homedirs_, Exists(_)).WillOnce(Return(false));
  EXPECT_EQ(userdataauth_.NeedsDircryptoMigration(account, &result),
            user_data_auth::CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND);
}

TEST_F(UserDataAuthTest, LowEntropyCredentialSupported) {
  // Test when there's no Low Entropy Credential Backend.
  EXPECT_CALL(tpm_, GetLECredentialBackend()).WillOnce(Return(nullptr));
  EXPECT_FALSE(userdataauth_.IsLowEntropyCredentialSupported());

  NiceMock<MockLECredentialBackend> backend;
  EXPECT_CALL(tpm_, GetLECredentialBackend()).WillRepeatedly(Return(&backend));

  // Test when the backend says it's not supported.
  EXPECT_CALL(backend, IsSupported()).WillOnce(Return(false));
  EXPECT_FALSE(userdataauth_.IsLowEntropyCredentialSupported());

  // Test when it's supported.
  EXPECT_CALL(backend, IsSupported()).WillOnce(Return(true));
  EXPECT_TRUE(userdataauth_.IsLowEntropyCredentialSupported());
}

TEST_F(UserDataAuthTest, GetAccountDiskUsage) {
  // Test when the user is non-existent.
  AccountIdentifier account;
  account.set_account_id("non_existent_user");

  EXPECT_EQ(0, userdataauth_.GetAccountDiskUsage(account));

  // Test when the user exists and home directory is not empty.
  constexpr char kUsername1[] = "foo@gmail.com";
  account.set_account_id(kUsername1);

  constexpr int64_t kHomedirSize = 12345678912345;
  EXPECT_CALL(homedirs_, ComputeSize(kUsername1))
      .WillOnce(Return(kHomedirSize));
  EXPECT_EQ(kHomedirSize, userdataauth_.GetAccountDiskUsage(account));
}

// ==================== Mount and Keys related tests =======================

// A test fixture with some utility functions for testing mount and keys related
// functionalities.
class UserDataAuthExTest : public UserDataAuthTest {
 public:
  UserDataAuthExTest() = default;
  ~UserDataAuthExTest() override = default;

  VaultKeyset* GetNiceMockVaultKeyset(const std::string& obfuscated_username,
                                      const std::string& key_label) const {
    // Note that technically speaking this is not strictly a mock, and probably
    // closer to a stub. However, the underlying class is
    // NiceMock<MockVaultKeyset>, thus we name the method accordingly.
    std::unique_ptr<VaultKeyset> mvk(new NiceMock<MockVaultKeyset>);
    mvk->mutable_serialized()->mutable_key_data()->set_label(key_label);
    return mvk.release();
  }

 protected:
  void PrepareArguments() {
    add_req_.reset(new user_data_auth::AddKeyRequest);
    check_req_.reset(new user_data_auth::CheckKeyRequest);
    mount_req_.reset(new user_data_auth::MountRequest);
    remove_req_.reset(new user_data_auth::RemoveKeyRequest);
    list_keys_req_.reset(new user_data_auth::ListKeysRequest);
    get_key_data_req_.reset(new user_data_auth::GetKeyDataRequest);
    update_req_.reset(new user_data_auth::UpdateKeyRequest);
    migrate_req_.reset(new user_data_auth::MigrateKeyRequest);
    remove_homedir_req_.reset(new user_data_auth::RemoveRequest);
    rename_homedir_req_.reset(new user_data_auth::RenameRequest);
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
  std::unique_ptr<user_data_auth::ListKeysRequest> list_keys_req_;
  std::unique_ptr<user_data_auth::GetKeyDataRequest> get_key_data_req_;
  std::unique_ptr<user_data_auth::UpdateKeyRequest> update_req_;
  std::unique_ptr<user_data_auth::MigrateKeyRequest> migrate_req_;
  std::unique_ptr<user_data_auth::RemoveRequest> remove_homedir_req_;
  std::unique_ptr<user_data_auth::RenameRequest> rename_homedir_req_;

  static constexpr char kUser[] = "chromeos-user";
  static constexpr char kKey[] = "274146c6e8886a843ddfea373e2dc71b";

 private:
  DISALLOW_COPY_AND_ASSIGN(UserDataAuthExTest);
};

constexpr char UserDataAuthExTest::kUser[];
constexpr char UserDataAuthExTest::kKey[];

TEST_F(UserDataAuthExTest, MountInvalidArgs) {
  // Note that this test doesn't distinguish between different causes of invalid
  // argument, that is, this doesn't check that
  // CRYPTOHOME_ERROR_INVALID_ARGUMENT is coming back because of the right
  // reason. This is because in the current structuring of the code, it would
  // not be possible to distinguish between those cases. This test only checks
  // that parameters that should lead to invalid argument does indeed lead to
  // invalid argument error.

  bool called;

  // This calls DoMount and check that the result is reported (i.e. the callback
  // is called), and is CRYPTOHOME_ERROR_INVALID_ARGUMENT.
  auto CallDoMountAndCheckResultIsInvalidArgument = [&called, this]() {
    called = false;
    userdataauth_.DoMount(
        *mount_req_,
        base::BindOnce(
            [](bool* called_ptr, const user_data_auth::MountReply& reply) {
              *called_ptr = true;
              EXPECT_EQ(user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT,
                        reply.error());
            },
            base::Unretained(&called)));
    EXPECT_TRUE(called);
  };

  // Test for case with no email.
  PrepareArguments();

  CallDoMountAndCheckResultIsInvalidArgument();

  // Test for case with no secrets.
  PrepareArguments();
  mount_req_->mutable_account()->set_account_id("foo@gmail.com");

  CallDoMountAndCheckResultIsInvalidArgument();

  // Test for case with empty secret.
  PrepareArguments();
  mount_req_->mutable_account()->set_account_id("foo@gmail.com");
  mount_req_->mutable_authorization()->mutable_key()->set_secret("");

  CallDoMountAndCheckResultIsInvalidArgument();

  // Test for create request given but without key.
  PrepareArguments();
  mount_req_->mutable_account()->set_account_id("foo@gmail.com");
  mount_req_->mutable_authorization()->mutable_key()->set_secret("blerg");
  mount_req_->mutable_create();

  CallDoMountAndCheckResultIsInvalidArgument();

  // Test for create request given but with an empty key.
  PrepareArguments();
  mount_req_->mutable_account()->set_account_id("foo@gmail.com");
  mount_req_->mutable_authorization()->mutable_key()->set_secret("blerg");
  mount_req_->mutable_create()->add_keys();
  // TODO(wad) Add remaining missing field tests and NULL tests

  CallDoMountAndCheckResultIsInvalidArgument();
}

TEST_F(UserDataAuthExTest, MountPublicWithExistingMounts) {
  constexpr char kUser[] = "chromeos-user";
  PrepareArguments();
  SetupMount("foo@gmail.com");

  mount_req_->mutable_account()->set_account_id(kUser);
  mount_req_->set_public_mount(true);

  bool called = false;
  EXPECT_CALL(homedirs_, Exists(_)).WillOnce(Return(true));
  userdataauth_.DoMount(
      *mount_req_,
      base::BindOnce(
          [](bool* called_ptr, const user_data_auth::MountReply& reply) {
            *called_ptr = true;
            EXPECT_EQ(user_data_auth::CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY,
                      reply.error());
          },
          base::Unretained(&called)));

  EXPECT_TRUE(called);
}

TEST_F(UserDataAuthExTest, MountPublicUsesPublicMountPasskey) {
  constexpr char kUser[] = "chromeos-user";
  PrepareArguments();

  mount_req_->mutable_account()->set_account_id(kUser);
  mount_req_->set_public_mount(true);
  EXPECT_CALL(homedirs_, Exists(_)).WillOnce(testing::InvokeWithoutArgs([&]() {
    SetupMount(kUser);
    EXPECT_CALL(*mount_, MountCryptohome(_, _, _))
        .WillOnce(testing::Invoke([](const Credentials& credentials,
                                     const Mount::MountArgs& mount_args,
                                     MountError* error) {
          brillo::SecureBlob passkey;
          credentials.GetPasskey(&passkey);
          // Tests that the passkey is filled when public_mount is set.
          EXPECT_FALSE(passkey.empty());
          return true;
        }));
    return true;
  }));

  bool called = false;
  userdataauth_.DoMount(
      *mount_req_,
      base::BindOnce(
          [](bool* called_ptr, const user_data_auth::MountReply& reply) {
            *called_ptr = true;
            EXPECT_EQ(user_data_auth::CRYPTOHOME_ERROR_NOT_SET, reply.error());
          },
          base::Unretained(&called)));

  EXPECT_TRUE(called);
}

TEST_F(UserDataAuthExTest, AddKeyInvalidArgs) {
  PrepareArguments();

  // Test for when there's no email supplied.
  EXPECT_EQ(userdataauth_.AddKey(*add_req_.get()),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);

  // Test for when there's no secret.
  add_req_->mutable_account_id()->set_account_id("foo@gmail.com");
  EXPECT_EQ(userdataauth_.AddKey(*add_req_.get()),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);

  // Test for when there's no new key.
  add_req_->mutable_authorization_request()->mutable_key()->set_secret("blerg");
  add_req_->clear_key();
  EXPECT_EQ(userdataauth_.AddKey(*add_req_.get()),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);

  // Test for no new key label.
  add_req_->mutable_key();
  // No label
  add_req_->mutable_key()->set_secret("some secret");
  EXPECT_EQ(userdataauth_.AddKey(*add_req_.get()),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
}

TEST_F(UserDataAuthExTest, AddKeySanity) {
  PrepareArguments();

  add_req_->mutable_account_id()->set_account_id("foo@gmail.com");
  add_req_->mutable_authorization_request()->mutable_key()->set_secret("blerg");
  add_req_->mutable_key();
  add_req_->mutable_key()->set_secret("some secret");
  add_req_->mutable_key()->mutable_data()->set_label("just a label");

  EXPECT_CALL(homedirs_, Exists(_)).WillOnce(Return(true));
  EXPECT_CALL(homedirs_, AddKeyset(_, _, _, _, _))
      .WillOnce(Return(cryptohome::CRYPTOHOME_ERROR_NOT_SET));

  EXPECT_EQ(userdataauth_.AddKey(*add_req_.get()),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
}

// Note that CheckKey tries to two method to check whether a key is valid or
// not. The first is through Homedirs, and the second is through Mount.
// Therefore, we test the combinations of (Homedirs, Mount) x (Success, Fail)
// below.
TEST_F(UserDataAuthExTest, CheckKeyHomedirsCheckSuccess) {
  PrepareArguments();
  SetupMount(kUser);

  check_req_->mutable_account_id()->set_account_id(kUser);
  check_req_->mutable_authorization_request()->mutable_key()->set_secret(kKey);

  EXPECT_CALL(*mount_, AreSameUser(_)).WillOnce(Return(false));
  EXPECT_CALL(homedirs_, Exists(_)).WillOnce(Return(true));
  EXPECT_CALL(homedirs_, AreCredentialsValid(_)).WillOnce(Return(true));
  EXPECT_EQ(userdataauth_.CheckKey(*check_req_.get()),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
}

TEST_F(UserDataAuthExTest, CheckKeyHomedirsCheckFail) {
  PrepareArguments();
  SetupMount(kUser);

  check_req_->mutable_account_id()->set_account_id(kUser);
  check_req_->mutable_authorization_request()->mutable_key()->set_secret(kKey);

  // Ensure failure
  EXPECT_CALL(*mount_, AreSameUser(_)).WillRepeatedly(Return(false));
  EXPECT_CALL(homedirs_, Exists(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(homedirs_, AreCredentialsValid(_)).WillOnce(Return(false));

  EXPECT_EQ(userdataauth_.CheckKey(*check_req_.get()),
            user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED);
}

TEST_F(UserDataAuthExTest, CheckKeyMountCheckSuccess) {
  PrepareArguments();
  SetupMount(kUser);

  check_req_->mutable_account_id()->set_account_id(kUser);
  check_req_->mutable_authorization_request()->mutable_key()->set_secret(kKey);

  EXPECT_CALL(*mount_, AreSameUser(_)).WillOnce(Return(true));
  EXPECT_CALL(*mount_, AreValid(_)).WillOnce(Return(true));

  EXPECT_EQ(userdataauth_.CheckKey(*check_req_.get()),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
}

TEST_F(UserDataAuthExTest, CheckKeyMountCheckFail) {
  PrepareArguments();
  SetupMount(kUser);

  check_req_->mutable_account_id()->set_account_id(kUser);
  check_req_->mutable_authorization_request()->mutable_key()->set_secret(kKey);

  EXPECT_CALL(*mount_, AreSameUser(_)).WillOnce(Return(true));
  EXPECT_CALL(*mount_, AreValid(_)).WillOnce(Return(false));
  EXPECT_CALL(homedirs_, Exists(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(homedirs_, AreCredentialsValid(_)).WillOnce(Return(false));

  EXPECT_EQ(userdataauth_.CheckKey(*check_req_.get()),
            user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED);
}

TEST_F(UserDataAuthExTest, CheckKeyInvalidArgs) {
  PrepareArguments();

  // No email supplied.
  EXPECT_EQ(userdataauth_.CheckKey(*check_req_.get()),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);

  // No secret.
  check_req_->mutable_account_id()->set_account_id("foo@gmail.com");
  EXPECT_EQ(userdataauth_.CheckKey(*check_req_.get()),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);

  // Empty secret.
  check_req_->mutable_authorization_request()->mutable_key()->set_secret("");
  EXPECT_EQ(userdataauth_.CheckKey(*check_req_.get()),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
}

TEST_F(UserDataAuthExTest, RemoveKeySanity) {
  PrepareArguments();

  constexpr char kUsername1[] = "foo@gmail.com";
  constexpr char kLabel1[] = "some label";

  remove_req_->mutable_account_id()->set_account_id(kUsername1);
  remove_req_->mutable_authorization_request()->mutable_key()->set_secret(
      "some secret");
  remove_req_->mutable_key()->mutable_data()->set_label(kLabel1);

  // Success case.
  EXPECT_CALL(homedirs_, Exists(_)).WillOnce(Return(true));
  EXPECT_CALL(homedirs_,
              RemoveKeyset(Property(&Credentials::username, kUsername1),
                           Property(&KeyData::label, kLabel1)))
      .WillOnce(Return(cryptohome::CRYPTOHOME_ERROR_NOT_SET));

  EXPECT_EQ(userdataauth_.RemoveKey(*remove_req_.get()),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);

  // Check the case when the account doesn't exist.
  EXPECT_CALL(homedirs_, Exists(_)).WillOnce(Return(false));

  EXPECT_EQ(userdataauth_.RemoveKey(*remove_req_.get()),
            user_data_auth::CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND);

  // Check when RemoveKeyset failed.
  EXPECT_CALL(homedirs_, Exists(_)).WillOnce(Return(true));
  EXPECT_CALL(homedirs_,
              RemoveKeyset(Property(&Credentials::username, kUsername1),
                           Property(&KeyData::label, kLabel1)))
      .WillOnce(Return(cryptohome::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE));

  EXPECT_EQ(userdataauth_.RemoveKey(*remove_req_.get()),
            user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
}

TEST_F(UserDataAuthExTest, RemoveKeyInvalidArgs) {
  PrepareArguments();

  // No email supplied.
  EXPECT_EQ(userdataauth_.RemoveKey(*remove_req_.get()),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);

  // No secret.
  remove_req_->mutable_account_id()->set_account_id("foo@gmail.com");
  EXPECT_EQ(userdataauth_.RemoveKey(*remove_req_.get()),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);

  // Empty secret.
  remove_req_->mutable_authorization_request()->mutable_key()->set_secret("");
  EXPECT_EQ(userdataauth_.RemoveKey(*remove_req_.get()),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);

  // No label provided for removal.
  remove_req_->mutable_authorization_request()->mutable_key()->set_secret(
      "some secret");
  remove_req_->mutable_key()->mutable_data();
  EXPECT_EQ(userdataauth_.RemoveKey(*remove_req_.get()),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
}

constexpr char ListKeysSanityTest_label1[] = "Label 1";
constexpr char ListKeysSanityTest_label2[] = "Yet another label";

TEST_F(UserDataAuthExTest, ListKeysSanity) {
  PrepareArguments();

  list_keys_req_->mutable_account_id()->set_account_id("foo@gmail.com");
  // Note that authorization request in ListKeyRequest is currently not
  // required.

  // Success case.
  EXPECT_CALL(homedirs_, Exists(_)).WillOnce(Return(true));
  EXPECT_CALL(homedirs_, GetVaultKeysetLabels(_, _))
      .WillOnce(Invoke(
          [](const std::string& ignored, std::vector<std::string>* output) {
            output->clear();
            output->push_back(ListKeysSanityTest_label1);
            output->push_back(ListKeysSanityTest_label2);
            return true;
          }));

  std::vector<std::string> labels;
  EXPECT_EQ(userdataauth_.ListKeys(*list_keys_req_, &labels),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);

  EXPECT_THAT(labels, ElementsAre(ListKeysSanityTest_label1,
                                  ListKeysSanityTest_label2));

  // Test for account not found case.
  EXPECT_CALL(homedirs_, Exists(_)).WillOnce(Return(false));
  EXPECT_EQ(userdataauth_.ListKeys(*list_keys_req_, &labels),
            user_data_auth::CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND);

  // Test for key not found case.
  EXPECT_CALL(homedirs_, Exists(_)).WillOnce(Return(true));
  EXPECT_CALL(homedirs_, GetVaultKeysetLabels(_, _)).WillOnce(Return(false));
  EXPECT_EQ(userdataauth_.ListKeys(*list_keys_req_, &labels),
            user_data_auth::CRYPTOHOME_ERROR_KEY_NOT_FOUND);
}

TEST_F(UserDataAuthExTest, ListKeysInvalidArgs) {
  PrepareArguments();
  std::vector<std::string> labels;

  // No Email.
  EXPECT_EQ(userdataauth_.ListKeys(*list_keys_req_, &labels),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);

  // Empty email.
  list_keys_req_->mutable_account_id()->set_account_id("");
  EXPECT_EQ(userdataauth_.ListKeys(*list_keys_req_, &labels),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
}

TEST_F(UserDataAuthExTest, GetKeyDataExNoMatch) {
  PrepareArguments();

  EXPECT_CALL(homedirs_, Exists(_)).WillRepeatedly(Return(true));

  get_key_data_req_->mutable_account_id()->set_account_id(
      "unittest@example.com");
  get_key_data_req_->mutable_key()->mutable_data()->set_label(
      "non-existent label");

  // Ensure there are no matches.
  EXPECT_CALL(homedirs_, GetVaultKeyset(_, _))
      .Times(1)
      .WillRepeatedly(Return(static_cast<VaultKeyset*>(NULL)));

  cryptohome::KeyData keydata_out;
  bool found = false;
  EXPECT_EQ(userdataauth_.GetKeyData(*get_key_data_req_, &keydata_out, &found),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
  // In case of no matching key, we should still return no error.

  EXPECT_FALSE(found);
}

TEST_F(UserDataAuthExTest, GetKeyDataExOneMatch) {
  // Request the single key by label.
  PrepareArguments();

  static const char* kExpectedLabel = "find-me";
  get_key_data_req_->mutable_key()->mutable_data()->set_label(kExpectedLabel);
  get_key_data_req_->mutable_account_id()->set_account_id(
      "unittest@example.com");

  EXPECT_CALL(homedirs_, Exists(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(homedirs_, GetVaultKeyset(_, _))
      .Times(1)
      .WillRepeatedly(
          Invoke(this, &UserDataAuthExTest::GetNiceMockVaultKeyset));

  cryptohome::KeyData keydata_out;
  bool found = false;
  EXPECT_EQ(userdataauth_.GetKeyData(*get_key_data_req_, &keydata_out, &found),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);

  EXPECT_TRUE(found);
  EXPECT_EQ(std::string(kExpectedLabel), keydata_out.label());
}

TEST_F(UserDataAuthExTest, GetKeyDataInvalidArgs) {
  PrepareArguments();

  // No email.
  cryptohome::KeyData keydata_out;
  bool found = false;
  EXPECT_EQ(userdataauth_.GetKeyData(*get_key_data_req_, &keydata_out, &found),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
  EXPECT_FALSE(found);
}

TEST_F(UserDataAuthExTest, UpdateKeySanity) {
  PrepareArguments();

  constexpr char kUsername1[] = "foo@gmail.com";

  update_req_->mutable_account_id()->set_account_id(kUsername1);
  update_req_->mutable_authorization_request()->mutable_key()->set_secret(
      "some secret");
  update_req_->mutable_changes()->mutable_data()->set_label("some label");

  EXPECT_CALL(homedirs_, Exists(GetObfuscatedUsername(kUsername1)))
      .WillOnce(Return(true));
  EXPECT_CALL(homedirs_,
              UpdateKeyset(Property(&Credentials::username, kUsername1),
                           Pointee(ProtobufEquals(update_req_->changes())),
                           update_req_->authorization_signature()))
      .WillOnce(Return(CRYPTOHOME_ERROR_NOT_SET));

  EXPECT_EQ(userdataauth_.UpdateKey(*update_req_),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
}

TEST_F(UserDataAuthExTest, UpdateKeyInvalidArguments) {
  PrepareArguments();

  // No email.
  EXPECT_EQ(userdataauth_.UpdateKey(*update_req_),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);

  // No authorization request key secret.
  update_req_->mutable_account_id()->set_account_id("foo@gmail.com");
  EXPECT_EQ(userdataauth_.UpdateKey(*update_req_),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);

  // No changes field.
  update_req_->mutable_authorization_request()->mutable_key()->set_secret(
      "some secret");
  EXPECT_EQ(userdataauth_.UpdateKey(*update_req_),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
}

TEST_F(UserDataAuthExTest, UpdateKeyError) {
  PrepareArguments();

  constexpr char kUsername1[] = "foo@gmail.com";

  update_req_->mutable_account_id()->set_account_id(kUsername1);
  update_req_->mutable_authorization_request()->mutable_key()->set_secret(
      "some secret");
  update_req_->mutable_changes()->mutable_data()->set_label("some label");

  // Check when the homedir doesn't exist.
  EXPECT_CALL(homedirs_, Exists(GetObfuscatedUsername(kUsername1)))
      .WillOnce(Return(false));

  EXPECT_EQ(userdataauth_.UpdateKey(*update_req_),
            user_data_auth::CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND);

  // Check when UpdateKeyset returns an error.
  EXPECT_CALL(homedirs_, Exists(GetObfuscatedUsername(kUsername1)))
      .WillOnce(Return(true));
  EXPECT_CALL(homedirs_,
              UpdateKeyset(_, Pointee(ProtobufEquals(update_req_->changes())),
                           update_req_->authorization_signature()))
      .WillOnce(Return(CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED));

  EXPECT_EQ(userdataauth_.UpdateKey(*update_req_),
            user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED);
}

TEST_F(UserDataAuthExTest, MigrateKeySanity) {
  PrepareArguments();

  constexpr char kUsername1[] = "foo@gmail.com";
  constexpr char kSecret1[] = "some secret";
  migrate_req_->mutable_account_id()->set_account_id(kUsername1);
  migrate_req_->mutable_authorization_request()->mutable_key()->set_secret(
      kSecret1);
  migrate_req_->set_secret("blerg");

  SetupMount(kUsername1);

  // Test for successful case.
  EXPECT_CALL(homedirs_, Migrate(Property(&Credentials::username, kUsername1),
                                 brillo::SecureBlob(kSecret1), Eq(mount_)))
      .WillOnce(Return(true));
  EXPECT_EQ(userdataauth_.MigrateKey(*migrate_req_),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);

  // Test for unsuccessful case.
  EXPECT_CALL(homedirs_, Migrate(_, brillo::SecureBlob(kSecret1), Eq(mount_)))
      .WillOnce(Return(false));
  EXPECT_EQ(userdataauth_.MigrateKey(*migrate_req_),
            user_data_auth::CRYPTOHOME_ERROR_MIGRATE_KEY_FAILED);
}

TEST_F(UserDataAuthExTest, MigrateKeyInvalidArguments) {
  PrepareArguments();

  // No email.
  EXPECT_EQ(userdataauth_.MigrateKey(*migrate_req_),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);

  // No authorization request key secret.
  migrate_req_->mutable_account_id()->set_account_id("foo@gmail.com");
  EXPECT_EQ(userdataauth_.MigrateKey(*migrate_req_),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
}

TEST_F(UserDataAuthExTest, RemoveSanity) {
  PrepareArguments();

  constexpr char kUsername1[] = "foo@gmail.com";

  remove_homedir_req_->mutable_identifier()->set_account_id(kUsername1);

  // Test for successful case.
  EXPECT_CALL(homedirs_, Remove(kUsername1)).WillOnce(Return(true));
  EXPECT_EQ(userdataauth_.Remove(*remove_homedir_req_),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);

  // Test for unsuccessful case.
  EXPECT_CALL(homedirs_, Remove(kUsername1)).WillOnce(Return(false));
  EXPECT_EQ(userdataauth_.Remove(*remove_homedir_req_),
            user_data_auth::CRYPTOHOME_ERROR_REMOVE_FAILED);
}

TEST_F(UserDataAuthExTest, RemoveInvalidArguments) {
  PrepareArguments();

  // No account_id
  EXPECT_EQ(userdataauth_.Remove(*remove_homedir_req_),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);

  // Empty account_id
  remove_homedir_req_->mutable_identifier()->set_account_id("");
  EXPECT_EQ(userdataauth_.Remove(*remove_homedir_req_),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
}

TEST_F(UserDataAuthExTest, RenameSanity) {
  PrepareArguments();

  constexpr char kUsername1[] = "foo@gmail.com";
  constexpr char kUsername2[] = "bar@gmail.com";
  rename_homedir_req_->mutable_id_from()->set_account_id(kUsername1);
  rename_homedir_req_->mutable_id_to()->set_account_id(kUsername2);

  SetupMount(kUsername1);

  // Test for successful case.
  EXPECT_CALL(*mount_, IsMounted()).WillOnce(Return(false));
  EXPECT_CALL(homedirs_, Rename(kUsername1, kUsername2)).WillOnce(Return(true));
  EXPECT_EQ(userdataauth_.Rename(*rename_homedir_req_),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);

  // Test for unsuccessful case.
  EXPECT_CALL(*mount_, IsMounted()).WillOnce(Return(false));
  EXPECT_CALL(homedirs_, Rename(kUsername1, kUsername2))
      .WillOnce(Return(false));
  EXPECT_EQ(userdataauth_.Rename(*rename_homedir_req_),
            user_data_auth::CRYPTOHOME_ERROR_MOUNT_FATAL);

  // Test for mount point busy case.
  EXPECT_CALL(*mount_, IsMounted()).WillOnce(Return(true));
  EXPECT_EQ(userdataauth_.Rename(*rename_homedir_req_),
            user_data_auth::CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY);
}

TEST_F(UserDataAuthExTest, RenameInvalidArguments) {
  PrepareArguments();

  constexpr char kUsername1[] = "foo@gmail.com";

  rename_homedir_req_->mutable_id_from()->set_account_id(kUsername1);

  // No id_to
  EXPECT_EQ(userdataauth_.Rename(*rename_homedir_req_),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);

  // No id_from
  rename_homedir_req_->clear_id_from();
  rename_homedir_req_->mutable_id_to()->set_account_id(kUsername1);
  EXPECT_EQ(userdataauth_.Rename(*rename_homedir_req_),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
}

}  // namespace cryptohome
