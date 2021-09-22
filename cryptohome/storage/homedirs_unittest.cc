// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/storage/homedirs.h"

#include <set>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/time/time.h>
#include <brillo/cryptohome.h>
// TODO(b/177929620): Cleanup once lvm utils are built unconditionally.
#if USE_LVM_STATEFUL_PARTITION
#include <brillo/blkdev_utils/mock_lvm.h>
#endif  // USE_LVM_STATEFUL_PARTITION
#include <brillo/secure_blob.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <policy/mock_device_policy.h>

#include "cryptohome/credentials.h"
#include "cryptohome/crypto.h"
#include "cryptohome/filesystem_layout.h"
#include "cryptohome/mock_keyset_management.h"
#include "cryptohome/mock_platform.h"
#include "cryptohome/storage/cryptohome_vault_factory.h"
#include "cryptohome/storage/encrypted_container/encrypted_container.h"
#include "cryptohome/storage/encrypted_container/encrypted_container_factory.h"
#include "cryptohome/storage/encrypted_container/fake_backing_device.h"
#include "cryptohome/storage/mount_constants.h"

using ::testing::_;
using ::testing::DoAll;
using ::testing::NiceMock;
using ::testing::Return;

namespace cryptohome {
namespace {

struct UserPassword {
  const char* name;
  const char* password;
};

constexpr char kUser0[] = "First User";
constexpr char kUserPassword0[] = "user0_pass";
constexpr char kUser1[] = "Second User";
constexpr char kUserPassword1[] = "user1_pass";
constexpr char kUser2[] = "Third User";
constexpr char kUserPassword2[] = "user2_pass";
constexpr char kOwner[] = "I am the device owner";
constexpr char kOwnerPassword[] = "owner_pass";

constexpr int kOwnerIndex = 3;

ACTION_P2(SetOwner, owner_known, owner) {
  if (owner_known)
    *arg0 = owner;
  return owner_known;
}

ACTION_P(SetEphemeralUsersEnabled, ephemeral_users_enabled) {
  *arg0 = ephemeral_users_enabled;
  return true;
}

struct UserInfo {
  std::string name;
  std::string obfuscated;
  brillo::SecureBlob passkey;
  Credentials credentials;
  base::FilePath homedir_path;
  base::FilePath user_path;
};

struct test_homedir {
  const char* obfuscated;
  base::Time::Exploded time;
};

}  // namespace

class HomeDirsTest
    : public ::testing::TestWithParam<bool /* should_test_ecryptfs */> {
 public:
  HomeDirsTest()
      : crypto_(&platform_),
        mock_device_policy_(new policy::MockDevicePolicy()) {}
  ~HomeDirsTest() override {}

  // Not copyable or movable
  HomeDirsTest(const HomeDirsTest&) = delete;
  HomeDirsTest& operator=(const HomeDirsTest&) = delete;
  HomeDirsTest(HomeDirsTest&&) = delete;
  HomeDirsTest& operator=(HomeDirsTest&&) = delete;

  void SetUp() override {
    PreparePolicy(true, kOwner, false, "");

    InitializeFilesystemLayout(&platform_, &crypto_, &system_salt_);
    std::unique_ptr<EncryptedContainerFactory> container_factory =
        std::make_unique<EncryptedContainerFactory>(
            &platform_, std::make_unique<FakeBackingDeviceFactory>(&platform_));
    HomeDirs::RemoveCallback remove_callback =
        base::BindRepeating(&MockKeysetManagement::RemoveLECredentials,
                            base::Unretained(&keyset_management_));
    homedirs_ = std::make_unique<HomeDirs>(
        &platform_, system_salt_,
        std::make_unique<policy::PolicyProvider>(
            std::unique_ptr<policy::MockDevicePolicy>(mock_device_policy_)),
        remove_callback,
        std::make_unique<CryptohomeVaultFactory>(&platform_,
                                                 std::move(container_factory)));

    platform_.GetFake()->SetSystemSaltForLibbrillo(system_salt_);

    AddUser(kUser0, kUserPassword0);
    AddUser(kUser1, kUserPassword1);
    AddUser(kUser2, kUserPassword2);
    AddUser(kOwner, kOwnerPassword);

    ASSERT_EQ(kOwner, users_[kOwnerIndex].name);

    PrepareDirectoryStructure();
  }

  void TearDown() override {
    platform_.GetFake()->RemoveSystemSaltForLibbrillo();
  }

  void AddUser(const char* name, const char* password) {
    std::string obfuscated =
        brillo::cryptohome::home::SanitizeUserNameWithSalt(name, system_salt_);
    brillo::SecureBlob passkey;
    cryptohome::Crypto::PasswordToPasskey(password, system_salt_, &passkey);
    Credentials credentials(name, passkey);

    UserInfo info = {name,
                     obfuscated,
                     passkey,
                     credentials,
                     ShadowRoot().Append(obfuscated),
                     brillo::cryptohome::home::GetHashedUserPath(obfuscated)};
    users_.push_back(info);
  }

  void PreparePolicy(bool owner_known,
                     const std::string& owner,
                     bool ephemeral_users_enabled,
                     const std::string& clean_up_strategy) {
    EXPECT_CALL(*mock_device_policy_, LoadPolicy())
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*mock_device_policy_, GetOwner(_))
        .WillRepeatedly(SetOwner(owner_known, owner));
    EXPECT_CALL(*mock_device_policy_, GetEphemeralUsersEnabled(_))
        .WillRepeatedly(SetEphemeralUsersEnabled(ephemeral_users_enabled));
  }

  // Returns true if the test is running for eCryptfs, false if for dircrypto.
  bool ShouldTestEcryptfs() const { return GetParam(); }

 protected:
  NiceMock<MockPlatform> platform_;
  MockKeysetManagement keyset_management_;
  Crypto crypto_;
  brillo::SecureBlob system_salt_;
  policy::MockDevicePolicy* mock_device_policy_;  // owned by homedirs_
  std::unique_ptr<HomeDirs> homedirs_;

  // Information about users' homedirs. The order of users is equal to kUsers.
  std::vector<UserInfo> users_;

  static const uid_t kAndroidSystemRealUid =
      HomeDirs::kAndroidSystemUid + kArcContainerShiftUid;

  void PrepareDirectoryStructure() {
    ASSERT_TRUE(platform_.CreateDirectory(
        brillo::cryptohome::home::GetUserPathPrefix()));
    for (const auto& user : users_) {
      ASSERT_TRUE(platform_.CreateDirectory(user.homedir_path));
      ASSERT_TRUE(
          platform_.CreateDirectory(user.homedir_path.Append(kMountDir)));
      if (ShouldTestEcryptfs()) {
        ASSERT_TRUE(platform_.CreateDirectory(
            user.homedir_path.Append(kEcryptfsVaultDir)));
      }
      ASSERT_TRUE(platform_.CreateDirectory(user.user_path));
    }
  }
};

INSTANTIATE_TEST_SUITE_P(WithEcryptfs, HomeDirsTest, ::testing::Values(true));
INSTANTIATE_TEST_SUITE_P(WithDircrypto, HomeDirsTest, ::testing::Values(false));

TEST_P(HomeDirsTest, RemoveNonOwnerCryptohomes) {
  EXPECT_TRUE(platform_.DirectoryExists(users_[0].homedir_path));
  EXPECT_TRUE(platform_.DirectoryExists(users_[1].homedir_path));
  EXPECT_TRUE(platform_.DirectoryExists(users_[2].homedir_path));
  EXPECT_TRUE(platform_.DirectoryExists(users_[kOwnerIndex].homedir_path));

  EXPECT_CALL(platform_, IsDirectoryMounted(_)).WillRepeatedly(Return(false));
  EXPECT_CALL(keyset_management_, RemoveLECredentials(_)).Times(3);

  homedirs_->RemoveNonOwnerCryptohomes();

  // Non-owners' vaults are removed
  EXPECT_FALSE(platform_.DirectoryExists(users_[0].homedir_path));
  EXPECT_FALSE(platform_.DirectoryExists(users_[1].homedir_path));
  EXPECT_FALSE(platform_.DirectoryExists(users_[2].homedir_path));

  // Owner's vault still exists
  EXPECT_TRUE(platform_.DirectoryExists(users_[kOwnerIndex].homedir_path));
}

TEST_P(HomeDirsTest, RenameCryptohome) {
  constexpr char kNewUserId[] = "some_new_user";
  const std::string kHashedNewUserId =
      brillo::cryptohome::home::SanitizeUserNameWithSalt(kNewUserId,
                                                         system_salt_);
  const base::FilePath kNewUserPath = ShadowRoot().Append(kHashedNewUserId);

  // Original state - pregenerated users' vaults exist, kNewUserId's vault
  // doesn't exist
  EXPECT_TRUE(platform_.DirectoryExists(users_[0].homedir_path));
  EXPECT_TRUE(platform_.DirectoryExists(users_[1].homedir_path));
  EXPECT_TRUE(platform_.DirectoryExists(users_[2].homedir_path));
  EXPECT_TRUE(platform_.DirectoryExists(users_[kOwnerIndex].homedir_path));
  EXPECT_FALSE(platform_.DirectoryExists(kNewUserPath));

  // Rename user0
  EXPECT_TRUE(homedirs_->Rename(users_[0].name, kNewUserId));

  // Renamed user0 to kNewUserId, thus user0's vault doesn't exist and
  // kNewUserId's does.
  EXPECT_FALSE(platform_.DirectoryExists(users_[0].homedir_path));
  EXPECT_TRUE(platform_.DirectoryExists(users_[1].homedir_path));
  EXPECT_TRUE(platform_.DirectoryExists(users_[2].homedir_path));
  EXPECT_TRUE(platform_.DirectoryExists(users_[kOwnerIndex].homedir_path));
  EXPECT_TRUE(platform_.DirectoryExists(kNewUserPath));

  // If source directory doesn't exist, assume renamed.
  EXPECT_TRUE(homedirs_->Rename(users_[0].name, kNewUserId));

  // Since renaming already renamed user, no changes are expected.
  EXPECT_FALSE(platform_.DirectoryExists(users_[0].homedir_path));
  EXPECT_TRUE(platform_.DirectoryExists(users_[1].homedir_path));
  EXPECT_TRUE(platform_.DirectoryExists(users_[2].homedir_path));
  EXPECT_TRUE(platform_.DirectoryExists(users_[kOwnerIndex].homedir_path));
  EXPECT_TRUE(platform_.DirectoryExists(kNewUserPath));

  // This should fail as target directory already exists.
  EXPECT_FALSE(homedirs_->Rename(users_[1].name, users_[2].name));

  // Since renaming failed, no changes are expected.
  EXPECT_FALSE(platform_.DirectoryExists(users_[0].homedir_path));
  EXPECT_TRUE(platform_.DirectoryExists(users_[1].homedir_path));
  EXPECT_TRUE(platform_.DirectoryExists(users_[2].homedir_path));
  EXPECT_TRUE(platform_.DirectoryExists(users_[kOwnerIndex].homedir_path));
  EXPECT_TRUE(platform_.DirectoryExists(kNewUserPath));

  // Rename back.
  EXPECT_TRUE(homedirs_->Rename(kNewUserId, users_[0].name));

  // Back to the original state - pregenerated users' vaults exist, kNewUserId's
  // vault doesn't exist.
  EXPECT_TRUE(platform_.DirectoryExists(users_[0].homedir_path));
  EXPECT_TRUE(platform_.DirectoryExists(users_[1].homedir_path));
  EXPECT_TRUE(platform_.DirectoryExists(users_[2].homedir_path));
  EXPECT_TRUE(platform_.DirectoryExists(users_[kOwnerIndex].homedir_path));
  EXPECT_FALSE(platform_.DirectoryExists(kNewUserPath));
}

TEST_P(HomeDirsTest, CreateCryptohome) {
  constexpr char kNewUserId[] = "some_new_user";
  const std::string kHashedNewUserId =
      brillo::cryptohome::home::SanitizeUserNameWithSalt(kNewUserId,
                                                         system_salt_);
  const base::FilePath kNewUserPath = ShadowRoot().Append(kHashedNewUserId);

  EXPECT_TRUE(homedirs_->Create(kNewUserId));
  EXPECT_TRUE(platform_.DirectoryExists(kNewUserPath));
}

TEST_P(HomeDirsTest, ComputeDiskUsage) {
  // /home/.shadow/$hash/mount in production code.
  base::FilePath mount_dir = users_[0].homedir_path.Append(kMountDir);
  // /home/.shadow/$hash/vault in production code.
  base::FilePath vault_dir = users_[0].homedir_path.Append(kEcryptfsVaultDir);
  // /home/user/$hash in production code and here in unit test.
  base::FilePath user_dir = users_[0].user_path;

  constexpr int64_t mount_bytes = 123456789012345;
  constexpr int64_t vault_bytes = 98765432154321;

  EXPECT_CALL(platform_, ComputeDirectoryDiskUsage(mount_dir))
      .WillRepeatedly(Return(mount_bytes));
  EXPECT_CALL(platform_, ComputeDirectoryDiskUsage(vault_dir))
      .WillRepeatedly(Return(vault_bytes));
  EXPECT_CALL(platform_, ComputeDirectoryDiskUsage(user_dir)).Times(0);

  const int64_t expected_bytes =
      ShouldTestEcryptfs() ? vault_bytes : mount_bytes;
  EXPECT_EQ(expected_bytes, homedirs_->ComputeDiskUsage(users_[0].name));
}

TEST_P(HomeDirsTest, ComputeDiskUsageEphemeral) {
  // /home/.shadow/$hash/mount in production code.
  base::FilePath mount_dir = users_[0].homedir_path.Append(kMountDir);
  // /home/.shadow/$hash/vault in production code.
  base::FilePath vault_dir = users_[0].homedir_path.Append(kEcryptfsVaultDir);
  // /home/user/$hash in production code and here in unit test.
  base::FilePath user_dir = users_[0].user_path;

  // Ephemeral users have no vault.
  EXPECT_TRUE(platform_.DeletePathRecursively(users_[0].homedir_path));

  constexpr int64_t userdir_bytes = 349857223479;

  EXPECT_CALL(platform_, ComputeDirectoryDiskUsage(mount_dir)).Times(0);
  EXPECT_CALL(platform_, ComputeDirectoryDiskUsage(vault_dir)).Times(0);
  EXPECT_CALL(platform_, ComputeDirectoryDiskUsage(user_dir))
      .WillRepeatedly(Return(userdir_bytes));

  int64_t expected_bytes = userdir_bytes;
  EXPECT_EQ(expected_bytes, homedirs_->ComputeDiskUsage(users_[0].name));
}

TEST_P(HomeDirsTest, ComputeDiskUsageWithNonexistentUser) {
  // If the specified user doesn't exist, there is no directory for the user, so
  // ComputeDiskUsage should return 0.
  const char kNonExistentUserId[] = "non_existent_user";
  EXPECT_EQ(0, homedirs_->ComputeDiskUsage(kNonExistentUserId));
}

TEST_P(HomeDirsTest, GetTrackedDirectoryForDirCrypto) {
  // /home/.shadow/$hash/mount in production code.
  base::FilePath mount_dir = users_[0].homedir_path.Append(kMountDir);
  // /home/.shadow/$hash/vault in production code.
  base::FilePath vault_dir = users_[0].homedir_path.Append(kEcryptfsVaultDir);

  const char* const kDirectories[] = {
      "aaa",
      "bbb",
      "bbb/ccc",
      "bbb/ccc/ddd",
  };
  // Prepare directories.
  for (const auto& directory : kDirectories) {
    const base::FilePath path = mount_dir.Append(base::FilePath(directory));
    ASSERT_TRUE(platform_.CreateDirectory(path));
    std::string name = path.BaseName().value();
    ASSERT_TRUE(platform_.SetExtendedFileAttribute(
        path, kTrackedDirectoryNameAttribute, name.data(), name.length()));
  }

  // Use GetTrackedDirectoryForDirCrypto() to get the path.
  // When dircrypto is being used and we don't have the key, the returned path
  // will be encrypted, but here we just get the same path.
  for (const auto& directory : kDirectories) {
    SCOPED_TRACE(directory);
    base::FilePath result;
    EXPECT_TRUE(homedirs_->GetTrackedDirectory(
        users_[0].homedir_path, base::FilePath(directory), &result));
    if (ShouldTestEcryptfs()) {
      EXPECT_EQ(vault_dir.Append(base::FilePath(directory)).value(),
                result.value());
    } else {
      EXPECT_EQ(mount_dir.Append(base::FilePath(directory)).value(),
                result.value());
    }
  }

  // TODO(chromium:1141301, dlunev): GetTrackedDirectory always returns true for
  // ecryptfs. Figure out what should actually be the behaviour in the case.
  if (!ShouldTestEcryptfs()) {
    // Return false for unknown directories.
    base::FilePath result;
    EXPECT_FALSE(homedirs_->GetTrackedDirectory(
        users_[0].homedir_path, base::FilePath("zzz"), &result));
    EXPECT_FALSE(homedirs_->GetTrackedDirectory(
        users_[0].homedir_path, base::FilePath("aaa/zzz"), &result));
  }
}

TEST_P(HomeDirsTest, GetUnmountedAndroidDataCount) {
  if (ShouldTestEcryptfs()) {
    // We don't support Ecryptfs.
    EXPECT_EQ(0, homedirs_->GetUnmountedAndroidDataCount());
    return;
  }

  for (const auto& user : users_) {
    // Set up a root hierarchy for the encrypted version of homedir_path
    // without android-data (added a suffix _encrypted in the code to mark them
    // encrypted).
    // root
    //     |-session_manager
    //          |-policy
    base::FilePath root =
        user.homedir_path.Append(kMountDir).Append(kRootHomeSuffix);
    base::FilePath session_manager = root.Append("session_manager_encrypted");
    ASSERT_TRUE(platform_.CreateDirectory(session_manager));
    base::FilePath policy = session_manager.Append("policy_encrypted");
    ASSERT_TRUE(platform_.CreateDirectory(policy));
  }

  // Add android data for the first user.
  //     |-android-data
  //          |-cache
  //          |-data
  base::FilePath root =
      users_[0].homedir_path.Append(kMountDir).Append(kRootHomeSuffix);
  ASSERT_TRUE(platform_.CreateDirectory(root));
  std::string name = root.BaseName().value();
  ASSERT_TRUE(platform_.SetExtendedFileAttribute(
      root, kTrackedDirectoryNameAttribute, name.data(), name.length()));

  base::FilePath android_data = root.Append("android-data_encrypted");
  ASSERT_TRUE(platform_.CreateDirectory(android_data));
  base::FilePath data = android_data.Append("data_encrypted");
  base::FilePath cache = android_data.Append("cache_encrypted");
  ASSERT_TRUE(platform_.CreateDirectory(data));
  ASSERT_TRUE(platform_.CreateDirectory(cache));
  ASSERT_TRUE(platform_.SetOwnership(cache, kAndroidSystemRealUid,
                                     kAndroidSystemRealUid, false));

  // Expect 1 home directory with android-data: homedir_paths_[0].
  EXPECT_EQ(1, homedirs_->GetUnmountedAndroidDataCount());
}

TEST_P(HomeDirsTest, GetHomedirsAllMounted) {
  std::vector<bool> all_mounted(users_.size(), true);
  std::set<std::string> hashes, got_hashes;

  for (int i = 0; i < users_.size(); i++) {
    hashes.insert(users_[i].obfuscated);
  }

  EXPECT_CALL(platform_, AreDirectoriesMounted(_))
      .WillOnce(Return(all_mounted));
  auto dirs = homedirs_->GetHomeDirs();

  for (const auto& dir : dirs) {
    EXPECT_TRUE(dir.is_mounted);
    got_hashes.insert(dir.obfuscated);
  }
  EXPECT_EQ(hashes, got_hashes);
}

TEST_P(HomeDirsTest, GetHomedirsSomeMounted) {
  std::vector<bool> some_mounted(users_.size());
  std::set<std::string> hashes, got_hashes;

  for (int i = 0; i < users_.size(); i++) {
    hashes.insert(users_[i].obfuscated);
    some_mounted[i] = i % 2;
  }

  EXPECT_CALL(platform_, AreDirectoriesMounted(_))
      .WillOnce(Return(some_mounted));
  auto dirs = homedirs_->GetHomeDirs();
  for (int i = 0; i < users_.size(); i++) {
    EXPECT_EQ(dirs[i].is_mounted, some_mounted[i]);
    got_hashes.insert(dirs[i].obfuscated);
  }
  EXPECT_EQ(hashes, got_hashes);
}

class HomeDirsVaultTest : public ::testing::Test {
 public:
  HomeDirsVaultTest()
      : user_({.obfuscated = "foo",
               .homedir_path = base::FilePath(ShadowRoot().Append("foo"))}),
        key_reference_({.fek_sig = brillo::SecureBlob("random keyref")}),
        crypto_(&platform_),
        mock_device_policy_(new policy::MockDevicePolicy()) {}
  ~HomeDirsVaultTest() override = default;

  void SetUp() override {
    HomeDirs::RemoveCallback remove_callback;
    homedirs_ = std::make_unique<HomeDirs>(
        &platform_, system_salt_,
        std::make_unique<policy::PolicyProvider>(
            std::unique_ptr<policy::MockDevicePolicy>(mock_device_policy_)),
        remove_callback);
  }
// TODO(b/177929620): Cleanup once lvm utils are built unconditionally.
#if USE_LVM_STATEFUL_PARTITION
  void ExpectLogicalVolumeStatefulPartition(
      const std::string& obfuscated_username, bool existing_cryptohome) {
    brillo::PhysicalVolume pv(base::FilePath("/dev/mmcblk0p1"), nullptr);
    brillo::VolumeGroup vg("stateful", nullptr);
    brillo::Thinpool thinpool("thinpool", "stateful", nullptr);
    brillo::LogicalVolume lv(LogicalVolumePrefix(obfuscated_username)
                                 .append(kDmcryptDataContainerSuffix),
                             "stateful", nullptr);
    std::unique_ptr<brillo::MockLogicalVolumeManager> lvm(
        new brillo::MockLogicalVolumeManager());

    EXPECT_CALL(platform_, GetStatefulDevice())
        .WillRepeatedly(Return(base::FilePath("/dev/mmcblk0")));
    EXPECT_CALL(platform_, GetBlkSize(_, _))
        .WillRepeatedly(
            DoAll(SetArgPointee<1>(1024 * 1024 * 1024), Return(true)));
    EXPECT_CALL(*lvm.get(), GetPhysicalVolume(_)).WillRepeatedly(Return(pv));
    EXPECT_CALL(*lvm.get(), GetVolumeGroup(_)).WillRepeatedly(Return(vg));
    EXPECT_CALL(*lvm.get(), GetThinpool(_, _)).WillRepeatedly(Return(thinpool));
    if (existing_cryptohome) {
      EXPECT_CALL(*lvm.get(), GetLogicalVolume(_, _))
          .WillRepeatedly(Return(lv));
    }

    homedirs_->SetLogicalVolumeManagerForTesting(std::move(lvm));
  }
#endif  // USE_LVM_STATEFUL_PARTITION

 protected:
  const UserInfo user_;
  const FileSystemKeyReference key_reference_;

  NiceMock<MockPlatform> platform_;
  Crypto crypto_;
  brillo::SecureBlob system_salt_;
  policy::MockDevicePolicy* mock_device_policy_;  // owned by homedirs_
  std::unique_ptr<HomeDirs> homedirs_;
};

// TODO(b/177929620): Cleanup once lvm utils are built unconditionally.
#if USE_LVM_STATEFUL_PARTITION
TEST_F(HomeDirsVaultTest, PristineVaultLvmStatefulSupport) {
  ExpectLogicalVolumeStatefulPartition(user_.obfuscated,
                                       /*existing_cryptohome=*/false);

  CryptohomeVault::Options options;
  MountError mount_error = MOUNT_ERROR_NONE;

  auto vault = homedirs_->GenerateCryptohomeVault(
      user_.obfuscated, key_reference_, options, /*is_pristine=*/true,
      &mount_error);
  EXPECT_EQ(vault->GetContainerType(), EncryptedContainerType::kDmcrypt);
  EXPECT_EQ(vault->GetMigratingContainerType(),
            EncryptedContainerType::kUnknown);
  EXPECT_EQ(vault->GetCacheContainerType(), EncryptedContainerType::kDmcrypt);
  EXPECT_EQ(mount_error, MOUNT_ERROR_NONE);
}

TEST_F(HomeDirsVaultTest, ExistingDmcryptContainer) {
  ExpectLogicalVolumeStatefulPartition(user_.obfuscated,
                                       /*existing_cryptohome=*/true);

  CryptohomeVault::Options options;
  MountError mount_error = MOUNT_ERROR_NONE;

  auto vault = homedirs_->GenerateCryptohomeVault(
      user_.obfuscated, key_reference_, options, /*is_pristine=*/false,
      &mount_error);
  EXPECT_EQ(vault->GetContainerType(), EncryptedContainerType::kDmcrypt);
  EXPECT_EQ(vault->GetMigratingContainerType(),
            EncryptedContainerType::kUnknown);
  EXPECT_EQ(vault->GetCacheContainerType(), EncryptedContainerType::kDmcrypt);
  EXPECT_EQ(mount_error, MOUNT_ERROR_NONE);
}
#endif  // USE_LVM_STATEFUL_PARTITION

// Tests cryptohome vault generation with fscrypt support.
TEST_F(HomeDirsVaultTest, PristineVault) {
  EXPECT_CALL(platform_, GetDirCryptoKeyState(_))
      .WillOnce(Return(dircrypto::KeyState::NO_KEY));

  CryptohomeVault::Options options;
  MountError mount_error = MOUNT_ERROR_NONE;

  auto vault = homedirs_->GenerateCryptohomeVault(
      user_.obfuscated, key_reference_, options, /*is_pristine=*/true,
      &mount_error);
  EXPECT_EQ(vault->GetContainerType(), EncryptedContainerType::kFscrypt);
  EXPECT_EQ(vault->GetMigratingContainerType(),
            EncryptedContainerType::kUnknown);
  EXPECT_EQ(mount_error, MOUNT_ERROR_NONE);
}

// Tests cryptohome vault generation in absence of fscrypt support.
TEST_F(HomeDirsVaultTest, PristineVaultNoFscrypt) {
  EXPECT_CALL(platform_, GetDirCryptoKeyState(_))
      .WillOnce(Return(dircrypto::KeyState::NOT_SUPPORTED));

  CryptohomeVault::Options options;
  MountError mount_error = MOUNT_ERROR_NONE;

  auto vault = homedirs_->GenerateCryptohomeVault(
      user_.obfuscated, key_reference_, options, /*is_pristine=*/true,
      &mount_error);
  EXPECT_EQ(vault->GetContainerType(), EncryptedContainerType::kEcryptfs);
  EXPECT_EQ(vault->GetMigratingContainerType(),
            EncryptedContainerType::kUnknown);
  EXPECT_EQ(mount_error, MOUNT_ERROR_NONE);
}

// Tests cryptohome vault generation with forced eCryptfs usage.
TEST_F(HomeDirsVaultTest, PristineVaultForceEcryptfs) {
  CryptohomeVault::Options options;
  options.force_type = EncryptedContainerType::kEcryptfs;
  MountError mount_error = MOUNT_ERROR_NONE;

  auto vault = homedirs_->GenerateCryptohomeVault(
      user_.obfuscated, key_reference_, options, /*is_pristine=*/true,
      &mount_error);
  EXPECT_EQ(vault->GetContainerType(), EncryptedContainerType::kEcryptfs);
  EXPECT_EQ(vault->GetMigratingContainerType(),
            EncryptedContainerType::kUnknown);
  EXPECT_EQ(mount_error, MOUNT_ERROR_NONE);
}

// Tests cryptohome vault generation for an existing eCryptfs container with
// forced fscrypt usage.
TEST_F(HomeDirsVaultTest, PristineForceFscrypt) {
  CryptohomeVault::Options options;
  options.force_type = EncryptedContainerType::kFscrypt;
  MountError mount_error = MOUNT_ERROR_NONE;

  auto vault = homedirs_->GenerateCryptohomeVault(
      user_.obfuscated, key_reference_, options, /*is_pristine=*/true,
      &mount_error);
  EXPECT_EQ(vault->GetContainerType(), EncryptedContainerType::kFscrypt);
  EXPECT_EQ(vault->GetMigratingContainerType(),
            EncryptedContainerType::kUnknown);
  EXPECT_EQ(mount_error, MOUNT_ERROR_NONE);
}

// Tests cryptohome vault generation for an existing eCryptfs container with no
// migration.
TEST_F(HomeDirsVaultTest, ExistingEcryptfsContainerNoMigrate) {
  ASSERT_TRUE(
      platform_.CreateDirectory(user_.homedir_path.Append(kEcryptfsVaultDir)));

  CryptohomeVault::Options options;
  MountError mount_error = MOUNT_ERROR_NONE;

  auto vault = homedirs_->GenerateCryptohomeVault(
      user_.obfuscated, key_reference_, options, /*is_pristine=*/false,
      &mount_error);
  EXPECT_EQ(vault->GetContainerType(), EncryptedContainerType::kEcryptfs);
  EXPECT_EQ(vault->GetMigratingContainerType(),
            EncryptedContainerType::kUnknown);
  EXPECT_EQ(mount_error, MOUNT_ERROR_NONE);
}

// Tests cryptohome vault generation for an existing eCryptfs container with
// migration.
TEST_F(HomeDirsVaultTest, ExistingEcryptfsContainerMigrate) {
  ASSERT_TRUE(
      platform_.CreateDirectory(user_.homedir_path.Append(kEcryptfsVaultDir)));

  CryptohomeVault::Options options;
  options.migrate = true;
  MountError mount_error = MOUNT_ERROR_NONE;

  auto vault = homedirs_->GenerateCryptohomeVault(
      user_.obfuscated, key_reference_, options, /*is_pristine*/ false,
      &mount_error);
  EXPECT_EQ(vault->GetContainerType(), EncryptedContainerType::kEcryptfs);
  EXPECT_EQ(vault->GetMigratingContainerType(),
            EncryptedContainerType::kFscrypt);
  EXPECT_EQ(mount_error, MOUNT_ERROR_NONE);
}

// Tests cryptohome vault generation if there is an existing eCryptfs container
// and a fscrypt container but migration is not enabled.
TEST_F(HomeDirsVaultTest, ExistingEcryptfsContainerNoMigrateFscryptExists) {
  ASSERT_TRUE(platform_.CreateDirectory(user_.homedir_path.Append(kMountDir)));
  ASSERT_TRUE(
      platform_.CreateDirectory(user_.homedir_path.Append(kEcryptfsVaultDir)));
  EXPECT_CALL(platform_, GetDirCryptoKeyState(_))
      .WillOnce(Return(dircrypto::KeyState::ENCRYPTED));

  CryptohomeVault::Options options;
  MountError mount_error = MOUNT_ERROR_NONE;

  auto vault = homedirs_->GenerateCryptohomeVault(
      user_.obfuscated, key_reference_, options, /*is_pristine=*/false,
      &mount_error);
  EXPECT_EQ(vault, nullptr);
  EXPECT_EQ(mount_error, MOUNT_ERROR_PREVIOUS_MIGRATION_INCOMPLETE);
}

// Tests cryptohome vault generation if there is an existing eCryptfs container,
// but migration is not enabled and dircrypto is forced.
TEST_F(HomeDirsVaultTest, ExistingEcryptfsContainerNoMigrateForceFscrypt) {
  ASSERT_TRUE(
      platform_.CreateDirectory(user_.homedir_path.Append(kEcryptfsVaultDir)));

  CryptohomeVault::Options options;
  options.block_ecryptfs = true;
  MountError mount_error = MOUNT_ERROR_NONE;

  auto vault = homedirs_->GenerateCryptohomeVault(
      user_.obfuscated, key_reference_, options, /*is_pristine=*/false,
      &mount_error);
  EXPECT_EQ(vault, nullptr);
  EXPECT_EQ(mount_error, MOUNT_ERROR_OLD_ENCRYPTION);
}

// Tests cryptohome vault generation if there is an existing fscrypt container.
TEST_F(HomeDirsVaultTest, ExistingFscryptContainer) {
  ASSERT_TRUE(platform_.CreateDirectory(user_.homedir_path.Append(kMountDir)));
  EXPECT_CALL(platform_, GetDirCryptoKeyState(_))
      .WillRepeatedly(Return(dircrypto::KeyState::ENCRYPTED));

  CryptohomeVault::Options options;
  MountError mount_error = MOUNT_ERROR_NONE;
  auto vault = homedirs_->GenerateCryptohomeVault(
      user_.obfuscated, key_reference_, options, /*is_pristine=*/false,
      &mount_error);
  EXPECT_EQ(vault->GetContainerType(), EncryptedContainerType::kFscrypt);
  EXPECT_EQ(vault->GetMigratingContainerType(),
            EncryptedContainerType::kUnknown);
  EXPECT_EQ(mount_error, MOUNT_ERROR_NONE);
}

}  // namespace cryptohome
