// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/homedirs.h"

#include <memory>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/scoped_temp_dir.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/stringprintf.h>
#include <brillo/cryptohome.h>
#include <brillo/data_encoding.h>
#include <brillo/secure_blob.h>
#include <chromeos/constants/cryptohome.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <policy/mock_device_policy.h>

#include "cryptohome/cryptolib.h"
#include "cryptohome/make_tests.h"
#include "cryptohome/mock_crypto.h"
#include "cryptohome/mock_platform.h"
#include "cryptohome/mock_tpm.h"
#include "cryptohome/mock_user_oldest_activity_timestamp_cache.h"
#include "cryptohome/mock_vault_keyset.h"
#include "cryptohome/mock_vault_keyset_factory.h"
#include "cryptohome/mount.h"
#include "cryptohome/username_passkey.h"

#include "signed_secret.pb.h"  // NOLINT(build/include)

using base::FilePath;
using base::StringPrintf;
using brillo::SecureBlob;
using ::testing::DoAll;
using ::testing::EndsWith;
using ::testing::HasSubstr;
using ::testing::InSequence;
using ::testing::InvokeWithoutArgs;
using ::testing::MatchesRegex;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::SaveArg;
using ::testing::SetArgPointee;
using ::testing::StartsWith;
using ::testing::StrEq;
using ::testing::_;

namespace cryptohome {

extern const char kKeyFile[];
extern const int kKeyFileMax;
extern const char kKeyLegacyPrefix[];

ACTION_P2(SetOwner, owner_known, owner) {
  if (owner_known)
    *arg0 = owner;
  return owner_known;
}

ACTION_P(SetEphemeralUsersEnabled, ephemeral_users_enabled) {
  *arg0 = ephemeral_users_enabled;
  return true;
}

ACTION_P(SetCleanUpStrategy, clean_up_strategy) {
  if (!clean_up_strategy.empty()) {
    *arg0 = clean_up_strategy;
    return true;
  }
  return false;
}

namespace {
const FilePath kTestRoot("alt_test_home_dir");

struct homedir {
  const char *name;
  base::Time::Exploded time;
};

const char *kOwner = "<<OWNER>>";
// Note, the order is important. These should be oldest to newest.
const struct homedir kHomedirs[] = {
  { "d5510a8dda6d743c46dadd979a61ae5603529742", { 2011, 1, 6, 1 } },
  { "8f995cdee8f0711fd32e1cf6246424002c483d47", { 2011, 2, 2, 1 } },
  { "973b9640e86f6073c6b6e2759ff3cf3084515e61", { 2011, 3, 2, 1 } },
  { kOwner, { 2011, 4, 5, 1 } }
};
const base::Time::Exploded jan1st2011_exploded = { 2011, 1, 6, 1 };
const base::Time time_jan1 = base::Time::FromUTCExploded(jan1st2011_exploded);
const base::Time::Exploded feb1st2011_exploded = { 2011, 2, 2, 1 };
const base::Time time_feb1 = base::Time::FromUTCExploded(feb1st2011_exploded);
const base::Time::Exploded mar1st2011_exploded = { 2011, 3, 2, 1 };
const base::Time time_mar1 = base::Time::FromUTCExploded(mar1st2011_exploded);
const base::Time::Exploded apr5th2011_exploded = { 2011, 4, 5, 1 };
const base::Time time_apr5 = base::Time::FromUTCExploded(apr5th2011_exploded);

NiceMock<MockFileEnumerator>* CreateMockFileEnumerator() {
  return new NiceMock<MockFileEnumerator>;
}
}  // namespace

class HomeDirsTest
    : public ::testing::TestWithParam<bool /* should_test_ecryptfs */> {
 public:
  HomeDirsTest() : crypto_(&platform_) { }
  virtual ~HomeDirsTest() { }

  void SetUp() {
    test_helper_.SetUpSystemSalt();
    // TODO(wad) Only generate the user data we need. This is time consuming.
    test_helper_.InitTestData(kTestRoot, kDefaultUsers, kDefaultUserCount,
                              ShouldTestEcryptfs());
    homedirs_.set_shadow_root(kTestRoot);
    test_helper_.InjectSystemSalt(&platform_, kTestRoot.Append("salt"));
    set_policy(true, kOwner, false, "");

    homedirs_.Init(&platform_, &crypto_, &timestamp_cache_);
    FilePath fp = FilePath(kTestRoot);
    for (const auto& hd : kHomedirs) {
      FilePath path = fp.Append(hd.name);
      std::string user;
      if (hd.name == std::string(kOwner))
        homedirs_.GetOwner(&user);
      else
        user = hd.name;
      homedir_paths_.push_back(fp.Append(user));
      user_paths_.push_back(brillo::cryptohome::home::GetHashedUserPath(user));
      base::Time t = base::Time::FromUTCExploded(hd.time);
      homedir_times_.push_back(t);
    }
  }

  void TearDown() {
    test_helper_.TearDownSystemSalt();
  }

  void set_policy(bool owner_known,
                  const std::string& owner,
                  bool ephemeral_users_enabled,
                  const std::string& clean_up_strategy) {
    policy::MockDevicePolicy* device_policy = new policy::MockDevicePolicy();
    EXPECT_CALL(*device_policy, LoadPolicy())
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*device_policy, GetOwner(_))
        .WillRepeatedly(SetOwner(owner_known, owner));
    EXPECT_CALL(*device_policy, GetEphemeralUsersEnabled(_))
        .WillRepeatedly(SetEphemeralUsersEnabled(ephemeral_users_enabled));
    homedirs_.own_policy_provider(new policy::PolicyProvider(device_policy));
  }

  // Returns true if the test is running for eCryptfs, false if for dircrypto.
  bool ShouldTestEcryptfs() const { return GetParam(); }

 protected:
  MakeTests test_helper_;
  NiceMock<MockPlatform> platform_;
  Crypto crypto_;
  std::vector<FilePath> homedir_paths_;
  std::vector<FilePath> user_paths_;
  MockUserOldestActivityTimestampCache timestamp_cache_;
  std::vector<base::Time> homedir_times_;
  MockVaultKeysetFactory vault_keyset_factory_;
  HomeDirs homedirs_;

 private:
  DISALLOW_COPY_AND_ASSIGN(HomeDirsTest);
};

INSTANTIATE_TEST_CASE_P(WithEcryptfs, HomeDirsTest, ::testing::Values(true));
INSTANTIATE_TEST_CASE_P(WithDircrypto, HomeDirsTest, ::testing::Values(false));

TEST_P(HomeDirsTest, RemoveNonOwnerCryptohomes) {
  // Ensure that RemoveNonOwnerCryptohomes does.
  EXPECT_CALL(platform_, EnumerateDirectoryEntries(kTestRoot, false, _))
    .WillOnce(
        DoAll(SetArgPointee<2>(homedir_paths_),
              Return(true)));
  FilePath user_prefix = brillo::cryptohome::home::GetUserPathPrefix();
  FilePath root_prefix = brillo::cryptohome::home::GetRootPathPrefix();
  EXPECT_CALL(platform_, EnumerateDirectoryEntries(user_prefix, _, _))
    .WillOnce(Return(true));
  EXPECT_CALL(platform_, EnumerateDirectoryEntries(root_prefix, _, _))
    .WillOnce(Return(true));
  EXPECT_CALL(platform_, DirectoryExists(_))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_,
              DirectoryExists(Property(&FilePath::value, EndsWith(kVaultDir))))
    .WillRepeatedly(Return(ShouldTestEcryptfs()));
  EXPECT_CALL(platform_, IsDirectoryMounted(_))
    .WillRepeatedly(Return(false));
  EXPECT_CALL(platform_, DeleteFile(homedir_paths_[0], true))
    .WillOnce(Return(true));
  EXPECT_CALL(platform_, DeleteFile(homedir_paths_[1], true))
    .WillOnce(Return(true));
  EXPECT_CALL(platform_, DeleteFile(homedir_paths_[2], true))
    .WillOnce(Return(true));

  homedirs_.RemoveNonOwnerCryptohomes();
}

TEST_P(HomeDirsTest, RenameCryptohome) {
  ASSERT_TRUE(
      base::CreateDirectory(FilePath(test_helper_.users[0].base_path)));
  ASSERT_TRUE(
      base::CreateDirectory(FilePath(test_helper_.users[1].base_path)));
  ASSERT_TRUE(
      base::CreateDirectory(FilePath(test_helper_.users[2].base_path)));

  const char kNewUserId[] = "some_new_user";
  EXPECT_TRUE(homedirs_.Rename(kDefaultUsers[0].username, kNewUserId));

  // If source directory doesn't exist, assume renamed.
  EXPECT_TRUE(homedirs_.Rename(kDefaultUsers[0].username, kNewUserId));

  // This should fail as target directory already exists.
  EXPECT_FALSE(
      homedirs_.Rename(kDefaultUsers[1].username, kDefaultUsers[2].username));

  // Rename back.
  EXPECT_TRUE(homedirs_.Rename(kNewUserId, kDefaultUsers[0].username));
}

TEST_P(HomeDirsTest, ComputeSize) {
  FilePath base_path(test_helper_.users[0].base_path);
  FilePath user_path = brillo::cryptohome::home::GetUserPathPrefix()
      .Append(test_helper_.users[0].obfuscated_username);
  FilePath root_path = brillo::cryptohome::home::GetRootPathPrefix()
      .Append(test_helper_.users[0].obfuscated_username);

  ASSERT_TRUE(base::CreateDirectory(base_path));

  // Put test files under base_path and user_path.
  const char kTestFileName0[] = "test.txt";
  const char kExpectedData0[] = "file content";
  int expected_bytes_0 = arraysize(kExpectedData0);
  ASSERT_EQ(expected_bytes_0,
            base::WriteFile(base_path.Append(kTestFileName0),
                            kExpectedData0, expected_bytes_0));
  const char kTestFileName1[] = "test1.txt";
  const char kExpectedData1[] = "file content";
  int expected_bytes_1 = arraysize(kExpectedData1);
  ASSERT_EQ(expected_bytes_1,
            base::WriteFile(base_path.Append(kTestFileName1),
                            kExpectedData1, expected_bytes_1));

  EXPECT_CALL(platform_, ComputeDirectorySize(base_path))
    .WillOnce(Return(expected_bytes_0));
  EXPECT_CALL(platform_, ComputeDirectorySize(user_path))
    .WillOnce(Return(expected_bytes_1));
  EXPECT_CALL(platform_, ComputeDirectorySize(root_path))
    .WillOnce(Return(0));

  EXPECT_EQ(expected_bytes_0 + expected_bytes_1,
            homedirs_.ComputeSize(kDefaultUsers[0].username));
}

TEST_P(HomeDirsTest, ComputeSizeWithNonexistentUser) {
  // If the specified user doesn't exist, there is no directory for the user, so
  // ComputeSize should return 0.
  const char kNonExistentUserId[] = "non_existent_user";
  EXPECT_EQ(0, homedirs_.ComputeSize(kNonExistentUserId));
}

TEST_P(HomeDirsTest, GetTrackedDirectoryForDirCrypto) {
  Platform real_platform;
  // Use real PathExists.
  EXPECT_CALL(platform_, FileExists(_)).WillRepeatedly(
      Invoke(&real_platform, &Platform::FileExists));
  // Use real FileEnumerator.
  EXPECT_CALL(platform_, GetFileEnumerator(_, _, _)).WillRepeatedly(
      Invoke(&real_platform, &Platform::GetFileEnumerator));
  // Use real HasExtendedFileAttribute.
  EXPECT_CALL(platform_, HasExtendedFileAttribute(_, _))
      .WillRepeatedly(Invoke(&real_platform,
                             &Platform::HasExtendedFileAttribute));
  // Use real GetExtendedFileAttributeAsString.
  EXPECT_CALL(platform_, GetExtendedFileAttributeAsString(_, _, _))
      .WillRepeatedly(Invoke(&real_platform,
                             &Platform::GetExtendedFileAttributeAsString));

  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  const FilePath mount_dir = temp_dir.path().Append(FilePath(kMountDir));
  ASSERT_TRUE(base::CreateDirectory(mount_dir));

  const FilePath::CharType* const kDirectories[] = {
    FILE_PATH_LITERAL("aaa"),
    FILE_PATH_LITERAL("bbb"),
    FILE_PATH_LITERAL("bbb/ccc"),
    FILE_PATH_LITERAL("bbb/ccc/ddd"),
  };
  // Prepare directories.
  for (const auto& directory : kDirectories) {
    const FilePath path = mount_dir.Append(FilePath(directory));
    ASSERT_TRUE(base::CreateDirectory(path));
    std::string name = path.BaseName().value();
    ASSERT_TRUE(real_platform.SetExtendedFileAttribute(
        path, kTrackedDirectoryNameAttribute, name.data(), name.length()));
  }

  // Use GetTrackedDirectoryForDirCrypto() to get the path.
  // When dircrypto is being used and we don't have the key, the returned path
  // will be encrypted, but here we just get the same path.
  for (const auto& directory : kDirectories) {
    SCOPED_TRACE(directory);
    FilePath result;
    EXPECT_TRUE(homedirs_.GetTrackedDirectory(
        temp_dir.path(), FilePath(directory), &result));
    EXPECT_EQ(mount_dir.Append(FilePath(directory)).value(), result.value());
  }
  // Return false for unknown directories.
  FilePath result;
  EXPECT_FALSE(homedirs_.GetTrackedDirectory(
      temp_dir.path(), FilePath(FILE_PATH_LITERAL("zzz")), &result));
  EXPECT_FALSE(homedirs_.GetTrackedDirectory(
      temp_dir.path(), FilePath(FILE_PATH_LITERAL("aaa/zzz")), &result));
}

class FreeDiskSpaceTest : public HomeDirsTest {
 public:
  FreeDiskSpaceTest() { }
  virtual ~FreeDiskSpaceTest() { }

  // Sets up expectaions for tracked directories.
  void ExpectTrackedDirectoriesEnumeration() {
    if (ShouldTestEcryptfs())  // No expecations needed for eCryptfs.
      return;

    for (const auto& path : homedir_paths_) {
      FilePath mount = path.Append(kMountDir);
      FilePath user = mount.Append(kUserHomeSuffix);
      FilePath root = mount.Append(kRootHomeSuffix);
      FilePath cache = user.Append(kCacheDir);
      FilePath gcache = user.Append(kGCacheDir);
      FilePath gcache_version = gcache.Append(kGCacheVersionDir);
      FilePath gcache_tmp = gcache_version.Append(kGCacheTmpDir);
      ExpectTrackedDirectoryEnumeration({user, root});
      ExpectTrackedDirectoryEnumeration({cache, gcache});
      ExpectTrackedDirectoryEnumeration({gcache_version});
      ExpectTrackedDirectoryEnumeration({gcache_tmp});
    }
  }

  // Sets up expectations for the given tracked directories which belong to the
  // same parent directory.
  void ExpectTrackedDirectoryEnumeration(
      const std::vector<FilePath>& child_directories) {
    DCHECK(!child_directories.empty());
    FilePath parent_directory = child_directories[0].DirName();
    // xattr is used to track directories.
    for (const auto& child : child_directories) {
      DCHECK_EQ(parent_directory.value(), child.DirName().value());
      EXPECT_CALL(platform_, GetExtendedFileAttributeAsString(
          child, kTrackedDirectoryNameAttribute, _))
          .WillRepeatedly(DoAll(SetArgPointee<2>(child.BaseName().value()),
                                Return(true)));
      EXPECT_CALL(platform_, HasExtendedFileAttribute(
          child, kTrackedDirectoryNameAttribute))
          .WillRepeatedly(Return(true));
    }
    // |child_directories| should be enumerated as the parent's children.
    auto create_file_enumerator_function = [child_directories]() {
      auto* mock = new NiceMock<MockFileEnumerator>;
      for (const auto& child : child_directories) {
        struct stat stat = {};
        mock->entries_.push_back(FileEnumerator::FileInfo(child, stat));
      }
      return mock;
    };
    EXPECT_CALL(platform_, GetFileEnumerator(
        parent_directory, false, base::FileEnumerator::DIRECTORIES))
        .WillRepeatedly(InvokeWithoutArgs(create_file_enumerator_function));
  }

  // The first half of HomeDirs::FreeDiskSpace does a purge of the Cache and
  // GCached dirs.  Unless these are being explicitly tested, we want these to
  // always succeed for every test. Set those expectations here for the given
  // number of unmounted user directories (mounted dirs aren't processed by
  // the code under test).
  void ExpectCacheDirCleanupCalls(int user_count) {
    EXPECT_CALL(platform_, EnumerateDirectoryEntries(kTestRoot, false, _))
      .WillRepeatedly(
          DoAll(SetArgPointee<2>(homedir_paths_),
                Return(true)));
    EXPECT_CALL(platform_, AmountOfFreeDiskSpace(kTestRoot))
      .Times(4).WillRepeatedly(Return(0))
      .RetiresOnSaturation();
    EXPECT_CALL(platform_, DirectoryExists(_))
      .WillRepeatedly(Return(true));
    EXPECT_CALL(platform_,
                DirectoryExists(Property(&FilePath::value,
                                         EndsWith(kVaultDir))))
      .WillRepeatedly(Return(ShouldTestEcryptfs()));
    // N users * (1 Cache dir + 1 GCache tmp dir)
    EXPECT_CALL(platform_, GetFileEnumerator(_, false, _))
      .Times(user_count * 2)
      .WillRepeatedly(InvokeWithoutArgs(CreateMockFileEnumerator));
    // N users * (1 GCache files dir + 1 Android cache dir)
    EXPECT_CALL(platform_, GetFileEnumerator(_, true, _))
      .Times(user_count * 2)
      .WillRepeatedly(InvokeWithoutArgs(CreateMockFileEnumerator));

    ExpectTrackedDirectoriesEnumeration();
  }
};

INSTANTIATE_TEST_CASE_P(WithEcryptfs, FreeDiskSpaceTest,
                        ::testing::Values(true));
INSTANTIATE_TEST_CASE_P(WithDircrypto, FreeDiskSpaceTest,
                        ::testing::Values(false));

TEST_P(FreeDiskSpaceTest, InitializeTimeCacheWithNoTime) {
  // To get to the init logic, we need to fail four
  // |kTargetFreeSpaceAfterCleanup| checks.
  EXPECT_CALL(platform_, AmountOfFreeDiskSpace(kTestRoot))
    .WillOnce(Return(0))
    .WillOnce(Return(0))
    .WillOnce(Return(0))
    .WillOnce(Return(0));

  // Expect cache clean ups.
  EXPECT_CALL(platform_, EnumerateDirectoryEntries(kTestRoot, false, _))
    .WillRepeatedly(
        DoAll(SetArgPointee<2>(homedir_paths_),
              Return(true)));
  EXPECT_CALL(platform_, DirectoryExists(_))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_,
              DirectoryExists(Property(&FilePath::value, EndsWith(kVaultDir))))
    .WillRepeatedly(Return(ShouldTestEcryptfs()));
  EXPECT_CALL(platform_, IsDirectoryMounted(_))
    .WillRepeatedly(Return(false));

  // The master.* enumerators (wildcard matcher first)
  EXPECT_CALL(platform_, GetFileEnumerator(_, false, _))
    .Times(4).WillRepeatedly(InvokeWithoutArgs(CreateMockFileEnumerator));
  // Empty enumerators per-user per-cache dirs plus
  // enumerators for empty vaults.
  EXPECT_CALL(platform_, GetFileEnumerator(
        Property(&FilePath::value, HasSubstr("user/Cache")),
        false, _))
    .Times(4)
    .WillRepeatedly(InvokeWithoutArgs(CreateMockFileEnumerator));
  EXPECT_CALL(platform_, GetFileEnumerator(
        Property(&FilePath::value, EndsWith("user/GCache/v1/tmp")),
        false, _))
    .Times(4)
    .WillRepeatedly(InvokeWithoutArgs(CreateMockFileEnumerator));
  EXPECT_CALL(platform_, GetFileEnumerator(
        Property(&FilePath::value, EndsWith("user/GCache/v1")),
        true, _))
    .Times(4)
    .WillRepeatedly(InvokeWithoutArgs(CreateMockFileEnumerator));
  EXPECT_CALL(platform_, GetFileEnumerator(
        Property(&FilePath::value,
          EndsWith(std::string(ShouldTestEcryptfs() ? kVaultDir : kMountDir) +
                   "/root")),
        true, _))
    .Times(4)
    .WillRepeatedly(InvokeWithoutArgs(CreateMockFileEnumerator));

  ExpectTrackedDirectoriesEnumeration();

  // Now cover the actual initialization piece
  EXPECT_CALL(timestamp_cache_, initialized())
    .WillOnce(Return(false));
  EXPECT_CALL(timestamp_cache_, Initialize())
    .Times(1);

  // It then walks the user vault to populate.
  MockVaultKeyset* vk[arraysize(kHomedirs)];
  EXPECT_CALL(vault_keyset_factory_, New(_, _))
    .WillOnce(Return(vk[0] = new MockVaultKeyset()))
    .WillOnce(Return(vk[1] = new MockVaultKeyset()))
    .WillOnce(Return(vk[2] = new MockVaultKeyset()))
    .WillOnce(Return(vk[3] = new MockVaultKeyset()));
  for (size_t i = 0; i < arraysize(vk); ++i) {
    EXPECT_CALL(*vk[i], Load(_))
      .WillRepeatedly(Return(false));
  }
  homedirs_.set_vault_keyset_factory(&vault_keyset_factory_);

  // Expect an addition for all users with no time.
  EXPECT_CALL(timestamp_cache_, AddExistingUserNotime(_))
    .Times(4);

  // Now skip the deletion steps by not having a legit owner.
  set_policy(false, "", false, "");

  // Could not delete user, so it doesn't have enough space yet.
  EXPECT_FALSE(homedirs_.FreeDiskSpace());
}

TEST_P(FreeDiskSpaceTest, InitializeTimeCacheWithOneTime) {
  // To get to the init logic, we need to fail four
  // |kTargetFreeSpaceAfterCleanup| checks.
  EXPECT_CALL(platform_, AmountOfFreeDiskSpace(kTestRoot))
    .WillOnce(Return(0))
    .WillOnce(Return(0))
    .WillOnce(Return(0))
    .WillOnce(Return(0));

  // Expect cache clean ups.
  EXPECT_CALL(platform_, EnumerateDirectoryEntries(kTestRoot, false, _))
    .WillRepeatedly(
        DoAll(SetArgPointee<2>(homedir_paths_),
              Return(true)));
  EXPECT_CALL(platform_, DirectoryExists(_))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_,
              DirectoryExists(Property(&FilePath::value, EndsWith(kVaultDir))))
    .WillRepeatedly(Return(ShouldTestEcryptfs()));
  // The master.* enumerators (wildcard matcher first)
  EXPECT_CALL(platform_, GetFileEnumerator(_, false, _))
    .Times(3).WillRepeatedly(InvokeWithoutArgs(CreateMockFileEnumerator));
  // Empty enumerators per-user per-cache dirs plus
  // enumerators for empty vaults.
  EXPECT_CALL(platform_,
      GetFileEnumerator(
        Property(&FilePath::value, HasSubstr("user/Cache")),
        false, _))
    .Times(4).WillRepeatedly(InvokeWithoutArgs(CreateMockFileEnumerator));
  EXPECT_CALL(platform_,
      GetFileEnumerator(
        Property(&FilePath::value, EndsWith("user/GCache/v1/tmp")),
        false, _))
    .Times(4)
    .WillRepeatedly(InvokeWithoutArgs(CreateMockFileEnumerator));
  EXPECT_CALL(platform_,
      GetFileEnumerator(
        Property(&FilePath::value,
          EndsWith(std::string(ShouldTestEcryptfs() ? kVaultDir : kMountDir) +
                   "/user/GCache/v1")),
        true, _))
    .Times(4)
    .WillRepeatedly(InvokeWithoutArgs(CreateMockFileEnumerator));
  EXPECT_CALL(platform_,
      GetFileEnumerator(
        Property(&FilePath::value,
          EndsWith(std::string(ShouldTestEcryptfs() ? kVaultDir : kMountDir) +
                   "/root")),
        true, _))
    .Times(4)
    .WillRepeatedly(InvokeWithoutArgs(CreateMockFileEnumerator));

  ExpectTrackedDirectoriesEnumeration();

  // Now cover the actual initialization piece
  EXPECT_CALL(timestamp_cache_, initialized())
    .WillOnce(Return(false));
  EXPECT_CALL(timestamp_cache_, Initialize())
    .Times(1);
  // Skip vault keyset loading to cause "Notime".
  EXPECT_CALL(platform_,
      FileExists(
        Property(&FilePath::value, StartsWith(homedir_paths_[0].value()))))
    .WillRepeatedly(Return(true));

  MockVaultKeyset* vk[arraysize(kHomedirs)];
  EXPECT_CALL(vault_keyset_factory_, New(_, _))
    .WillOnce(Return(vk[0] = new MockVaultKeyset()))
    .WillOnce(Return(vk[1] = new MockVaultKeyset()))
    .WillOnce(Return(vk[2] = new MockVaultKeyset()))
    .WillOnce(Return(vk[3] = new MockVaultKeyset()));
  // The first three will have no time.
  size_t i;
  for (i = 0; i < arraysize(vk) - 1; ++i) {
    EXPECT_CALL(*vk[i], Load(_))
      .WillRepeatedly(Return(false));
  }
  // Owner will have a master.0
  NiceMock<MockFileEnumerator> *master0;
  EXPECT_CALL(platform_, GetFileEnumerator(homedir_paths_[3], false, _))
    .WillOnce(Return(master0 = new NiceMock<MockFileEnumerator>));
  EXPECT_CALL(*master0, Next())
    .WillOnce(Return(homedir_paths_[3].Append(kKeyFile).AddExtension("0")))
    .WillRepeatedly(Return(FilePath()));

  // The owner will have a time.
  EXPECT_CALL(*vk[i], Load(_))
    .WillOnce(Return(true));
  SerializedVaultKeyset serialized;
  serialized.set_last_activity_timestamp(homedir_times_[3].ToInternalValue());
  EXPECT_CALL(*vk[i], serialized())
    .Times(2)
    .WillRepeatedly(ReturnRef(serialized));
  homedirs_.set_vault_keyset_factory(&vault_keyset_factory_);

  // Expect an addition for all users with no time.
  EXPECT_CALL(timestamp_cache_, AddExistingUserNotime(_))
    .Times(3);
  // Adding the owner
  EXPECT_CALL(timestamp_cache_,
              AddExistingUser(homedir_paths_[3], homedir_times_[3]))
    .Times(1);

  // Now skip the deletion steps by not having a legit owner.
  set_policy(false, "", false, "");

  // Could not delete user, so it doesn't have enough space yet.
  EXPECT_FALSE(homedirs_.FreeDiskSpace());
}

TEST_P(FreeDiskSpaceTest, NoCacheCleanup) {
  // Pretend we have lots of free space
  EXPECT_CALL(platform_, AmountOfFreeDiskSpace(kTestRoot))
    .WillOnce(Return(kFreeSpaceThresholdToTriggerCleanup + 1));
  EXPECT_TRUE(homedirs_.FreeDiskSpace());
}

TEST_P(FreeDiskSpaceTest, OnlyCacheCleanup) {
  // Only clean up the Cache data. Not GCache, etc.
  EXPECT_CALL(platform_, EnumerateDirectoryEntries(kTestRoot, false, _))
    .WillRepeatedly(
        DoAll(SetArgPointee<2>(homedir_paths_),
              Return(true)));

  EXPECT_CALL(platform_, AmountOfFreeDiskSpace(kTestRoot))
    .WillOnce(Return(0))
    .WillOnce(Return(kTargetFreeSpaceAfterCleanup + 1));
  EXPECT_CALL(platform_, DirectoryExists(_))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_,
              DirectoryExists(Property(&FilePath::value, EndsWith(kVaultDir))))
    .WillRepeatedly(Return(ShouldTestEcryptfs()));
  // Empty enumerators per-user per-cache dirs
  NiceMock<MockFileEnumerator>* fe[arraysize(kHomedirs)];
  EXPECT_CALL(platform_, GetFileEnumerator(_, false, _))
    .WillOnce(Return(fe[0] = new NiceMock<MockFileEnumerator>))
    .WillOnce(Return(fe[1] = new NiceMock<MockFileEnumerator>))
    .WillOnce(Return(fe[2] = new NiceMock<MockFileEnumerator>))
    .WillOnce(Return(fe[3] = new NiceMock<MockFileEnumerator>));
  // Exercise the delete file path.
  for (size_t f = 0; f < arraysize(fe); ++f) {
    EXPECT_CALL(*fe[f], Next())
      .WillOnce(Return(homedir_paths_[f].Append("Cache/foo")))
      .WillRepeatedly(Return(FilePath()));
  }
  EXPECT_CALL(platform_,
      DeleteFile(
        Property(&FilePath::value, EndsWith("/Cache/foo")),
        true))
    .Times(4)
    .WillRepeatedly(Return(true));

  ExpectTrackedDirectoriesEnumeration();

  EXPECT_TRUE(homedirs_.FreeDiskSpace());
}

TEST_P(FreeDiskSpaceTest, GCacheCleanup) {
  EXPECT_CALL(platform_, EnumerateDirectoryEntries(kTestRoot, false, _))
    .WillRepeatedly(
        DoAll(SetArgPointee<2>(homedir_paths_),
              Return(true)));
  EXPECT_CALL(platform_, AmountOfFreeDiskSpace(kTestRoot))
    .WillOnce(Return(0))
    .WillOnce(Return(0))
    .WillOnce(Return(kTargetFreeSpaceAfterCleanup + 1));
  EXPECT_CALL(platform_, DirectoryExists(_))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_,
              DirectoryExists(Property(&FilePath::value, EndsWith(kVaultDir))))
    .WillRepeatedly(Return(ShouldTestEcryptfs()));
  // Empty enumerators per-user per-cache dirs
  EXPECT_CALL(platform_,
      GetFileEnumerator(
        Property(&FilePath::value, EndsWith("/Cache")),
        false, _))
    .Times(4)
    .WillRepeatedly(InvokeWithoutArgs(CreateMockFileEnumerator));

  // DeleteGCacheTmpCallback enumerate all directories to find GCache files
  // directory.
  NiceMock<MockFileEnumerator>* fe[arraysize(kHomedirs)];
  EXPECT_CALL(
      platform_,
      GetFileEnumerator(
        Property(&FilePath::value,
          EndsWith(std::string(ShouldTestEcryptfs() ? kVaultDir : kMountDir) +
                   "/user/GCache/v1")),
        true, base::FileEnumerator::DIRECTORIES))
      .WillOnce(Return(fe[0] = new NiceMock<MockFileEnumerator>))
      .WillOnce(Return(fe[1] = new NiceMock<MockFileEnumerator>))
      .WillOnce(Return(fe[2] = new NiceMock<MockFileEnumerator>))
      .WillOnce(Return(fe[3] = new NiceMock<MockFileEnumerator>));
  EXPECT_CALL(platform_,
      GetFileEnumerator(
        Property(&FilePath::value, EndsWith("/GCache/v1/tmp")),
        false, _))
      .Times(4)
      .WillRepeatedly(InvokeWithoutArgs(CreateMockFileEnumerator));

  EXPECT_CALL(*fe[0], Next())
    .WillOnce(Return(homedir_paths_[0].Append("irrelevant_dir")))
    .WillOnce(Return(homedir_paths_[0].Append("GCache/v1/files")))
    .WillRepeatedly(Return(FilePath()));
  // Do nothing for users 1-3.
  for (size_t f = 1; f < arraysize(fe); ++f) {
    EXPECT_CALL(*fe[f], Next()).WillRepeatedly(Return(FilePath()));
  }
  // Irrelevant directory without +d file attribute.
  EXPECT_CALL(platform_,
      HasNoDumpFileAttribute(
        Property(&FilePath::value, EndsWith("irrelevant_dir"))))
    .WillOnce(Return(false));
  // GCache file directory has +d file attribute and the appropriate
  // extended file attribute.
  EXPECT_CALL(platform_,
      HasNoDumpFileAttribute(
        Property(&FilePath::value, EndsWith("GCache/v1/files"))))
    .WillOnce(Return(true));
  EXPECT_CALL(platform_,
      HasExtendedFileAttribute(
        Property(&FilePath::value, EndsWith("GCache/v1/files")),
        kGCacheFilesAttribute))
    .WillOnce(Return(true));

  NiceMock<MockFileEnumerator>* fe2 = new NiceMock<MockFileEnumerator>;
  // The cache directory contains removable (having +d) and unremovable files.
  EXPECT_CALL(platform_,
      GetFileEnumerator(
        Property(&FilePath::value, EndsWith("GCache/v1/files")),
        false, _))
    .WillOnce(Return(fe2));
  EXPECT_CALL(*fe2, Next())
    .WillOnce(Return(homedir_paths_[0].Append("GCache/v1/files/removable")))
    .WillOnce(Return(homedir_paths_[0].Append("GCache/v1/files/unremovable")))
    .WillRepeatedly(Return(FilePath()));
  EXPECT_CALL(platform_,
      HasNoDumpFileAttribute(
        Property(&FilePath::value,
          EndsWith("GCache/v1/files/removable"))))
    .WillOnce(Return(true));
  EXPECT_CALL(platform_,
      HasNoDumpFileAttribute(
        Property(&FilePath::value,
          EndsWith("GCache/v1/files/unremovable"))))
    .WillOnce(Return(false));

  ExpectTrackedDirectoriesEnumeration();

  // Confirm removable file is removed.
  EXPECT_CALL(platform_,
      DeleteFile(
        Property(&FilePath::value,
          EndsWith("/GCache/v1/files/removable")),
        _))
    .WillOnce(Return(true));

  EXPECT_TRUE(homedirs_.FreeDiskSpace());
}

TEST_P(FreeDiskSpaceTest, CacheAndGCacheCleanup) {
  EXPECT_CALL(platform_, EnumerateDirectoryEntries(kTestRoot, false, _))
    .WillRepeatedly(
        DoAll(SetArgPointee<2>(homedir_paths_),
              Return(true)));
  EXPECT_CALL(platform_, AmountOfFreeDiskSpace(kTestRoot))
    .WillOnce(Return(0))  // Before cleanup
    .WillOnce(Return(0))  // After removing cache
    .WillOnce(Return(kMinFreeSpaceInBytes + 1));  // After removing gcache
  EXPECT_CALL(platform_, DirectoryExists(_))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_,
              DirectoryExists(Property(&FilePath::value, EndsWith(kVaultDir))))
    .WillRepeatedly(Return(ShouldTestEcryptfs()));

  // Skip per-cache and Cache enumerations done per user in order to
  // test cache and GCache deletion.
  EXPECT_CALL(platform_, GetFileEnumerator(
        Property(&FilePath::value, EndsWith("/user/Cache")),
        false, _))
    .Times(4)
    .WillRepeatedly(InvokeWithoutArgs(CreateMockFileEnumerator));

  // DeleteGCacheTmpCallback enumerate all directories to find GCache files
  // directory.
  EXPECT_CALL(platform_, GetFileEnumerator(
        Property(
          &FilePath::value,
          EndsWith(std::string(ShouldTestEcryptfs() ? kVaultDir : kMountDir) +
                   "/user/GCache/v1")),
        true, base::FileEnumerator::DIRECTORIES))
    .Times(4)
    .WillRepeatedly(InvokeWithoutArgs(CreateMockFileEnumerator));
  EXPECT_CALL(platform_, GetFileEnumerator(
        Property(&FilePath::value, EndsWith("user/GCache/v1/tmp")),
        false, _))
    .Times(4)
    .WillRepeatedly(InvokeWithoutArgs(CreateMockFileEnumerator));

  // Should not attempt to remove Android cache. (by getting enumerator first)
  EXPECT_CALL(platform_, GetFileEnumerator(
      Property(&FilePath::value, EndsWith(std::string(
          ShouldTestEcryptfs() ? kVaultDir : kMountDir) + "/root")), true, _))
      .Times(0);

  ExpectTrackedDirectoriesEnumeration();

  // Should finish cleaning up because the free space size exceeds
  // |kMinFreeSpaceInBytes| after deleting gcache, although it's still
  // below |kTargetFreeSpaceAfterCleanup|.
  EXPECT_FALSE(homedirs_.FreeDiskSpace());
}

TEST_P(FreeDiskSpaceTest, CacheAndGCacheAndAndroidCleanup) {
  EXPECT_CALL(platform_, EnumerateDirectoryEntries(kTestRoot, false, _))
    .WillRepeatedly(
        DoAll(SetArgPointee<2>(homedir_paths_),
              Return(true)));
  EXPECT_CALL(platform_, AmountOfFreeDiskSpace(kTestRoot))
    .WillOnce(Return(0))
    .WillOnce(Return(0))
    .WillOnce(Return(kMinFreeSpaceInBytes - 1))
    .WillOnce(Return(kMinFreeSpaceInBytes + 1));
  EXPECT_CALL(platform_, DirectoryExists(_))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_,
              DirectoryExists(Property(&FilePath::value, EndsWith(kVaultDir))))
    .WillRepeatedly(Return(ShouldTestEcryptfs()));

  // Skip per-cache and Cache enumerations done per user in order to
  // test Android cache deletions.
  EXPECT_CALL(platform_, GetFileEnumerator(
        Property(&FilePath::value, EndsWith("/user/Cache")),
        false, _))
    .Times(4)
    .WillRepeatedly(InvokeWithoutArgs(CreateMockFileEnumerator));

  // DeleteGCacheTmpCallback enumerate all directories to find GCache files
  // directory.
  EXPECT_CALL(platform_, GetFileEnumerator(
        Property(
          &FilePath::value,
          EndsWith(std::string(ShouldTestEcryptfs() ? kVaultDir : kMountDir) +
                   "/user/GCache/v1")),
        true, base::FileEnumerator::DIRECTORIES))
    .Times(4)
    .WillRepeatedly(InvokeWithoutArgs(CreateMockFileEnumerator));
  EXPECT_CALL(platform_, GetFileEnumerator(
        Property(&FilePath::value, EndsWith("user/GCache/v1/tmp")),
        false, _))
    .Times(4)
    .WillRepeatedly(InvokeWithoutArgs(CreateMockFileEnumerator));

  ExpectTrackedDirectoriesEnumeration();

  // Now test for the Android user, just test for the first user.
  NiceMock<MockFileEnumerator>* fe = new NiceMock<MockFileEnumerator>;
  EXPECT_CALL(platform_, GetFileEnumerator(
        Property(
          &FilePath::value,
          EndsWith(std::string(ShouldTestEcryptfs() ? kVaultDir : kMountDir) +
                   "/root")),
        true, _))
    .WillOnce(Return(fe))
    .WillOnce(InvokeWithoutArgs(CreateMockFileEnumerator))
    .WillOnce(InvokeWithoutArgs(CreateMockFileEnumerator))
    .WillOnce(InvokeWithoutArgs(CreateMockFileEnumerator));

  // Return a cache and non-cache directory.
  EXPECT_CALL(*fe, Next())
    .WillOnce(Return(homedir_paths_[0].Append(
            "android-data/data/data/com.google.hogehoge/cache")))
    .WillOnce(Return(homedir_paths_[0].Append(
            "android-data/data/data/com.google.hogehoge/data")))
    .WillRepeatedly(Return(FilePath()));

  EXPECT_CALL(platform_, HasExtendedFileAttribute(
        Property(
          &FilePath::value,
          EndsWith("android-data/data/data/com.google.hogehoge/cache")),
        kAndroidCacheFilesAttribute))
    .WillOnce(Return(true));
  EXPECT_CALL(platform_, HasExtendedFileAttribute(
        Property(
          &FilePath::value,
          EndsWith("android-data/data/data/com.google.hogehoge/data")),
        kAndroidCacheFilesAttribute))
    .WillOnce(Return(false));

  // Confirm android cache dir is removed and data directory is not.
  EXPECT_CALL(platform_, DeleteFile(
        Property(
          &FilePath::value,
          EndsWith("android-data/data/data/com.google.hogehoge/cache")),
        _))
    .WillOnce(Return(true));
  EXPECT_CALL(platform_, DeleteFile(
        Property(
          &FilePath::value,
          EndsWith("android-data/data/data/com.google.hogehoge/data")),
        _))
    .Times(0);

  // Should finish cleaning up because the free space size exceeds
  // |kMinFreeSpaceInBytes| after deleting Android cache, although it's still
  // below |kTargetFreeSpaceAfterCleanup|.
  EXPECT_FALSE(homedirs_.FreeDiskSpace());
}

TEST_P(FreeDiskSpaceTest, CleanUpOneUser) {
  // Ensure that the oldest user directory deleted, but not any
  // others, if the first deletion frees enough space.
  EXPECT_CALL(timestamp_cache_, initialized())
    .WillRepeatedly(Return(true));

  EXPECT_CALL(timestamp_cache_, empty())
    .WillOnce(Return(false));

  EXPECT_CALL(timestamp_cache_, RemoveOldestUser())
    .WillOnce(Return(homedir_paths_[0]));

  EXPECT_CALL(platform_, AmountOfFreeDiskSpace(kTestRoot))
    .WillOnce(Return(kTargetFreeSpaceAfterCleanup + 1));

  EXPECT_CALL(platform_, DeleteFile(homedir_paths_[0], true))
    .WillOnce(Return(true));

  ExpectCacheDirCleanupCalls(4);
  EXPECT_TRUE(homedirs_.FreeDiskSpace());
}

TEST_P(FreeDiskSpaceTest, CleanUpMultipleUsers) {
  // Ensure that the two oldest user directories are deleted, but not any
  // others, if the second deletion frees enough space.
  EXPECT_CALL(timestamp_cache_, initialized())
    .WillRepeatedly(Return(true));

  EXPECT_CALL(timestamp_cache_, empty())
    .WillOnce(Return(false))
    .WillOnce(Return(false));

  EXPECT_CALL(timestamp_cache_, RemoveOldestUser())
    .WillOnce(Return(homedir_paths_[0]))
    .WillOnce(Return(homedir_paths_[1]));

  EXPECT_CALL(platform_, AmountOfFreeDiskSpace(kTestRoot))
    .WillOnce(Return(kTargetFreeSpaceAfterCleanup - 1))
    .WillOnce(Return(kTargetFreeSpaceAfterCleanup + 1));

  EXPECT_CALL(platform_, DeleteFile(homedir_paths_[0], true))
    .WillOnce(Return(true));
  EXPECT_CALL(platform_, DeleteFile(homedir_paths_[1], true))
    .WillOnce(Return(true));

  ExpectCacheDirCleanupCalls(4);
  EXPECT_TRUE(homedirs_.FreeDiskSpace());
}

TEST_P(FreeDiskSpaceTest, EnterpriseCleanUpAllUsersButLast_LoginScreen) {
  set_policy(true, "", false, "");
  homedirs_.set_enterprise_owned(true);
  UserOldestActivityTimestampCache cache;
  cache.Initialize();
  homedirs_.Init(&platform_, &crypto_, &cache);

  cache.AddExistingUser(homedir_paths_[0], homedir_times_[0]);
  cache.AddExistingUser(homedir_paths_[1], homedir_times_[1]);
  cache.AddExistingUser(homedir_paths_[2], homedir_times_[2]);
  cache.AddExistingUser(homedir_paths_[3], homedir_times_[3]);

  EXPECT_CALL(platform_, AmountOfFreeDiskSpace(kTestRoot))
    .WillRepeatedly(Return(0));

  EXPECT_CALL(platform_, DeleteFile(_, _)).WillRepeatedly(Return(true));

  // Most-recent user isn't deleted.
  EXPECT_CALL(platform_, DeleteFile(homedir_paths_[3], true)).Times(0);

  EXPECT_CALL(platform_,
              IsDirectoryMounted(_)).WillRepeatedly(Return(false));

  ExpectCacheDirCleanupCalls(4);
  EXPECT_TRUE(homedirs_.FreeDiskSpace());

  // Last user is re-inserted into cache, to be a candidate for deletion
  // next time.
  EXPECT_FALSE(cache.empty());
  EXPECT_EQ(homedir_times_[3], cache.oldest_known_timestamp());
}


TEST_P(FreeDiskSpaceTest, EnterpriseCleanUpAllUsersButLast_UserLoggedIn) {
  set_policy(true, "", false, "");
  homedirs_.set_enterprise_owned(true);
  UserOldestActivityTimestampCache cache;
  cache.Initialize();
  homedirs_.Init(&platform_, &crypto_, &cache);

  cache.AddExistingUser(homedir_paths_[0], homedir_times_[0]);
  cache.AddExistingUser((homedir_paths_[1]), homedir_times_[1]);
  // User 2 is logged in, and hence not added to cache during initialization.
  cache.AddExistingUser((homedir_paths_[3]), homedir_times_[3]);

  EXPECT_CALL(platform_, AmountOfFreeDiskSpace(kTestRoot))
    .WillRepeatedly(Return(0));

  // Oldest user (#0) in cache IS deleted, since most-recent user #2 isn't in
  // the cache at all (they are logged in).
  EXPECT_CALL(platform_, DeleteFile(homedir_paths_[0], true))
    .WillOnce(Return(true));
  EXPECT_CALL(platform_, DeleteFile(homedir_paths_[1], true))
    .WillOnce(Return(true));
  EXPECT_CALL(platform_, DeleteFile(homedir_paths_[2], true)).Times(0);
  EXPECT_CALL(platform_, DeleteFile(homedir_paths_[3], true))
    .WillOnce(Return(true));

  EXPECT_CALL(platform_,
      IsDirectoryMounted(_))
    .WillRepeatedly(Return(false));
  // Catch /home/usr/<uid> mount.
  EXPECT_CALL(platform_,
      IsDirectoryMounted(
        Property(&FilePath::value, StrEq(user_paths_[2].value()))))
    .WillRepeatedly(Return(true));

  ExpectCacheDirCleanupCalls(3);
  EXPECT_FALSE(homedirs_.FreeDiskSpace());

  // Cache is empty (oldest user only re-inserted if no one is logged in).
  EXPECT_TRUE(cache.empty());
}

TEST_P(FreeDiskSpaceTest, CleanUpMultipleNonadjacentUsers) {
  // Ensure that the two oldest user directories are deleted, but not any
  // others.  The owner is inserted in the middle.
  EXPECT_CALL(timestamp_cache_, initialized())
    .WillRepeatedly(Return(true));

  EXPECT_CALL(timestamp_cache_, empty())
    .WillOnce(Return(false))
    .WillOnce(Return(false))
    .WillOnce(Return(false));

  EXPECT_CALL(timestamp_cache_, RemoveOldestUser())
    .WillOnce(Return((homedir_paths_[0])))
    .WillOnce(Return((homedir_paths_[3])))
    .WillOnce(Return((homedir_paths_[1])));

  EXPECT_CALL(platform_, AmountOfFreeDiskSpace(kTestRoot))
    .WillOnce(Return(0))
    // Loop continued before we check disk space for owner.
    .WillOnce(Return(kTargetFreeSpaceAfterCleanup + 1));

  EXPECT_CALL(platform_, DeleteFile(homedir_paths_[0], true))
    .WillOnce(Return(true));
  EXPECT_CALL(platform_, DeleteFile(homedir_paths_[1], true))
    .WillOnce(Return(true));
  // Ensure the owner isn't deleted!
  EXPECT_CALL(platform_, DeleteFile(homedir_paths_[3], true))
    .Times(0);

  ExpectCacheDirCleanupCalls(4);
  EXPECT_TRUE(homedirs_.FreeDiskSpace());
}

TEST_P(FreeDiskSpaceTest, NoOwnerNoEnterpriseNoCleanup) {
  // Ensure that no users are deleted with no owner/enterprise-owner.
  EXPECT_CALL(platform_, AmountOfFreeDiskSpace(kTestRoot))
    .WillRepeatedly(Return(0));

  // Skip re-init
  EXPECT_CALL(timestamp_cache_, initialized())
    .WillOnce(Return(true));

  // No user deletions!
  EXPECT_CALL(platform_, DeleteFile(homedir_paths_[0], true))
    .Times(0);
  EXPECT_CALL(platform_, DeleteFile(homedir_paths_[1], true))
    .Times(0);
  EXPECT_CALL(platform_, DeleteFile(homedir_paths_[2], true))
    .Times(0);
  EXPECT_CALL(platform_, DeleteFile(homedir_paths_[3], true))
    .Times(0);

  // Now skip the deletion steps by not having a legit owner.
  set_policy(false, "", false, "");

  ExpectCacheDirCleanupCalls(4);
  EXPECT_FALSE(homedirs_.FreeDiskSpace());
}

TEST_P(FreeDiskSpaceTest, ConsumerEphemeralUsers) {
  // When ephemeral users are enabled, no cryptohomes are kept except the owner.
  set_policy(true, kOwner, true, "");

  EXPECT_CALL(platform_, EnumerateDirectoryEntries(kTestRoot, false, _))
    .WillRepeatedly(
        DoAll(SetArgPointee<2>(homedir_paths_),
              Return(true)));
  EXPECT_CALL(platform_,
      EnumerateDirectoryEntries(brillo::cryptohome::home::GetUserPathPrefix(),
          false, _))
    .WillRepeatedly(
        DoAll(SetArgPointee<2>(homedir_paths_),
              Return(true)));
  EXPECT_CALL(platform_,
      EnumerateDirectoryEntries(brillo::cryptohome::home::GetRootPathPrefix(),
          false, _))
    .WillRepeatedly(
        DoAll(SetArgPointee<2>(homedir_paths_),
              Return(true)));

  EXPECT_CALL(platform_, AmountOfFreeDiskSpace(kTestRoot))
    .WillOnce(Return(kFreeSpaceThresholdToTriggerCleanup - 1));

  EXPECT_CALL(platform_, DirectoryExists(_))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_,
              DirectoryExists(Property(&FilePath::value, EndsWith(kVaultDir))))
    .WillRepeatedly(Return(ShouldTestEcryptfs()));
  EXPECT_CALL(platform_, DeleteFile(homedir_paths_[0], true))
    .WillOnce(Return(true))  // vault
    .WillOnce(Return(true))  // user
    .WillOnce(Return(true));  // root
  EXPECT_CALL(platform_, DeleteFile(homedir_paths_[1], true))
    .WillOnce(Return(true))
    .WillOnce(Return(true))
    .WillOnce(Return(true));
  EXPECT_CALL(platform_, DeleteFile(homedir_paths_[2], true))
    .WillOnce(Return(true))
    .WillOnce(Return(true))
    .WillOnce(Return(true));
  // Ensure the owner isn't deleted!
  EXPECT_CALL(platform_, DeleteFile(homedir_paths_[3], true))
    .Times(0);

  EXPECT_TRUE(homedirs_.FreeDiskSpace());
}

TEST_P(FreeDiskSpaceTest, EnterpriseEphemeralUsers) {
  // When ephemeral users are enabled, no cryptohomes are kept except the owner.
  set_policy(true, "", true, "");
  homedirs_.set_enterprise_owned(true);

  EXPECT_CALL(platform_, EnumerateDirectoryEntries(kTestRoot, false, _))
    .WillRepeatedly(
        DoAll(SetArgPointee<2>(homedir_paths_),
              Return(true)));
  EXPECT_CALL(platform_,
      EnumerateDirectoryEntries(brillo::cryptohome::home::GetUserPathPrefix(),
          false, _))
    .WillRepeatedly(
        DoAll(SetArgPointee<2>(homedir_paths_),
              Return(true)));
  EXPECT_CALL(platform_,
      EnumerateDirectoryEntries(brillo::cryptohome::home::GetRootPathPrefix(),
          false, _))
    .WillRepeatedly(
        DoAll(SetArgPointee<2>(homedir_paths_),
              Return(true)));

  EXPECT_CALL(platform_, AmountOfFreeDiskSpace(kTestRoot))
    .WillOnce(Return(kFreeSpaceThresholdToTriggerCleanup - 1));

  EXPECT_CALL(platform_, DirectoryExists(_))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_,
              DirectoryExists(Property(&FilePath::value, EndsWith(kVaultDir))))
    .WillRepeatedly(Return(ShouldTestEcryptfs()));
  EXPECT_CALL(platform_, DeleteFile(homedir_paths_[0], true))
    .WillOnce(Return(true))  // vault
    .WillOnce(Return(true))  // user
    .WillOnce(Return(true));  // root
  EXPECT_CALL(platform_, DeleteFile(homedir_paths_[1], true))
    .WillOnce(Return(true))
    .WillOnce(Return(true))
    .WillOnce(Return(true));
  EXPECT_CALL(platform_, DeleteFile(homedir_paths_[2], true))
    .WillOnce(Return(true))
    .WillOnce(Return(true))
    .WillOnce(Return(true));
  EXPECT_CALL(platform_, DeleteFile(homedir_paths_[3], true))
    .WillOnce(Return(true))
    .WillOnce(Return(true))
    .WillOnce(Return(true));

  EXPECT_TRUE(homedirs_.FreeDiskSpace());
}

TEST_P(FreeDiskSpaceTest, DontCleanUpMountedUser) {
  // Ensure that a user isn't deleted if it appears to be mounted.
  EXPECT_CALL(timestamp_cache_, initialized())
    .WillRepeatedly(Return(true));
  EXPECT_CALL(timestamp_cache_, empty())
    .WillOnce(Return(false))
    .WillOnce(Return(true));

  // This will only be called once (see time below).
  EXPECT_CALL(timestamp_cache_, RemoveOldestUser())
    .WillOnce(Return((homedir_paths_[0])));

  EXPECT_CALL(platform_, EnumerateDirectoryEntries(kTestRoot, false, _))
    .WillRepeatedly(
        DoAll(SetArgPointee<2>(homedir_paths_),
              Return(true)));
  EXPECT_CALL(platform_, AmountOfFreeDiskSpace(kTestRoot))
    .WillRepeatedly(Return(0));
  EXPECT_CALL(platform_, DirectoryExists(_))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_,
              DirectoryExists(Property(&FilePath::value, EndsWith(kVaultDir))))
    .WillRepeatedly(Return(ShouldTestEcryptfs()));

  // Ensure the mounted user never has (G)Cache traversed!
  EXPECT_CALL(platform_, GetFileEnumerator(
        Property(&FilePath::value,
          StartsWith(homedir_paths_[0].value())),
        false, _))
    .Times(0);

  // 3 users * (1 Cache dir + 1 GCache tmp dir)
  EXPECT_CALL(platform_, GetFileEnumerator(_, false, _))
    .Times(6).WillRepeatedly(InvokeWithoutArgs(CreateMockFileEnumerator));
  // 3 users * (1 GCache files dir + 1 Android cache)
  EXPECT_CALL(platform_, GetFileEnumerator(_, true, _))
    .Times(6).WillRepeatedly(InvokeWithoutArgs(CreateMockFileEnumerator));

  ExpectTrackedDirectoriesEnumeration();

  EXPECT_CALL(platform_,
      IsDirectoryMounted(
        Property(&FilePath::value, StrEq(user_paths_[0].value()))))
    .Times(5)  // Cache, GCache, android, mounted dir count, user removal
    .WillRepeatedly(Return(true));
  for (size_t i = 1; i < arraysize(kHomedirs); ++i) {
    EXPECT_CALL(platform_,
        IsDirectoryMounted(
          Property(&FilePath::value, StrEq(user_paths_[i].value()))))
      .Times(4)  // Cache, GCache, android, mounted dir count
      .WillRepeatedly(Return(false));
  }

  EXPECT_FALSE(homedirs_.FreeDiskSpace());
}

TEST_P(HomeDirsTest, GoodDecryptTest) {
  // create a HomeDirs instance that points to a good shadow root, test that it
  // properly authenticates against the first key.
  SecureBlob system_salt;
  NiceMock<MockTpm> tpm;
  homedirs_.crypto()->set_tpm(&tpm);
  homedirs_.crypto()->set_use_tpm(false);
  ASSERT_TRUE(homedirs_.GetSystemSalt(&system_salt));
  set_policy(false, "", false, "");

  test_helper_.users[1].InjectKeyset(&platform_);
  SecureBlob passkey;
  cryptohome::Crypto::PasswordToPasskey(test_helper_.users[1].password,
                                        system_salt, &passkey);
  UsernamePasskey up(test_helper_.users[1].username, passkey);

  ASSERT_TRUE(homedirs_.AreCredentialsValid(up));
}

TEST_P(HomeDirsTest, BadDecryptTest) {
  // create a HomeDirs instance that points to a good shadow root, test that it
  // properly denies access with a bad passkey
  SecureBlob system_salt;
  NiceMock<MockTpm> tpm;
  homedirs_.crypto()->set_tpm(&tpm);
  homedirs_.crypto()->set_use_tpm(false);
  set_policy(false, "", false, "");

  test_helper_.users[4].InjectKeyset(&platform_);

  SecureBlob passkey;
  cryptohome::Crypto::PasswordToPasskey("bogus", system_salt, &passkey);
  UsernamePasskey up(test_helper_.users[4].username, passkey);

  ASSERT_FALSE(homedirs_.AreCredentialsValid(up));
}

#define MAX_VKS 5
class KeysetManagementTest : public HomeDirsTest {
 public:
  KeysetManagementTest() { }
  virtual ~KeysetManagementTest() { }

  void SetUp() {
    HomeDirsTest::SetUp();
    last_vk_ = -1;
    active_vk_ = NULL;
    memset(active_vks_, 0, sizeof(active_vks_));
  }

  void TearDown() {
    HomeDirsTest::TearDown();
    last_vk_++;
    for ( ; last_vk_ < MAX_VKS; ++last_vk_) {
      if (active_vks_[last_vk_])
        delete active_vks_[last_vk_];
      active_vks_[last_vk_] = NULL;
    }
    last_vk_ = -1;
    active_vk_ = NULL;
  }

  virtual bool VkDecrypt0(const brillo::SecureBlob& key) {
    return memcmp(key.data(), keys_[0].data(), key.size()) == 0;
  }

  virtual const SerializedVaultKeyset& FakeSerialized() const {
    return serialized_;
  }

  virtual SerializedVaultKeyset* FakeMutableSerialized() {
    return &serialized_;
  }

  virtual MockFileEnumerator* NewKeysetFileEnumerator() {
    MockFileEnumerator* files = new MockFileEnumerator();
    {
      InSequence s;
      // Single key.
      EXPECT_CALL(*files, Next())
        .WillOnce(Return(keyset_paths_[0]));
      EXPECT_CALL(*files, Next())
        .WillOnce(Return(FilePath()));
    }
    return files;
  }

  virtual MockVaultKeyset* NewActiveVaultKeyset() {
    last_vk_++;
    CHECK(last_vk_ < MAX_VKS);
    active_vk_ = active_vks_[last_vk_];
    EXPECT_CALL(*active_vk_, Load(keyset_paths_[0]))
      .WillOnce(Return(true));
    EXPECT_CALL(*active_vk_, Decrypt(_))
      .WillRepeatedly(Invoke(this, &KeysetManagementTest::VkDecrypt0));

    EXPECT_CALL(*active_vk_, serialized())
      .WillRepeatedly(
          Invoke(this, &KeysetManagementTest::FakeSerialized));
    EXPECT_CALL(*active_vk_, mutable_serialized())
      .WillRepeatedly(
          Invoke(this, &KeysetManagementTest::FakeMutableSerialized));
    return active_vk_;
  }

  virtual void KeysetSetUp() {
    serialized_.Clear();
    NiceMock<MockTpm> tpm;
    homedirs_.crypto()->set_tpm(&tpm);
    homedirs_.crypto()->set_use_tpm(false);
    ASSERT_TRUE(homedirs_.GetSystemSalt(&system_salt_));
    set_policy(false, "", false, "");

    // Setup the base keyset files for users[1]
    keyset_paths_.push_back(test_helper_.users[1].keyset_path);
    keys_.push_back(test_helper_.users[1].passkey);

    EXPECT_CALL(platform_, GetFileEnumerator(
        test_helper_.users[1].base_path, false, _))
      .WillRepeatedly(
        InvokeWithoutArgs(this,
                          &KeysetManagementTest::NewKeysetFileEnumerator));

    homedirs_.set_vault_keyset_factory(&vault_keyset_factory_);
    // Pre-allocate VKs so that each call can advance
    // but expectations can be set.
    for (int i = 0; i < MAX_VKS; ++i) {
      active_vks_[i] = new MockVaultKeyset();
    }
    active_vk_ = active_vks_[0];

    EXPECT_CALL(vault_keyset_factory_, New(_, _))
      .WillRepeatedly(
          InvokeWithoutArgs(this, &KeysetManagementTest::NewActiveVaultKeyset));
    SecureBlob passkey;
    cryptohome::Crypto::PasswordToPasskey(test_helper_.users[1].password,
                                          system_salt_, &passkey);
    up_.reset(new UsernamePasskey(test_helper_.users[1].username, passkey));
  }

  int last_vk_;
  MockVaultKeyset* active_vk_;
  MockVaultKeyset* active_vks_[MAX_VKS];
  std::vector<FilePath> keyset_paths_;
  std::vector<brillo::SecureBlob> keys_;
  std::unique_ptr<UsernamePasskey> up_;
  SecureBlob system_salt_;
  SerializedVaultKeyset serialized_;
};

INSTANTIATE_TEST_CASE_P(WithEcryptfs, KeysetManagementTest,
                        ::testing::Values(true));
INSTANTIATE_TEST_CASE_P(WithDircrypto, KeysetManagementTest,
                        ::testing::Values(false));

TEST_P(KeysetManagementTest, AddKeysetSuccess) {
  KeysetSetUp();

  SecureBlob newkey;
  cryptohome::Crypto::PasswordToPasskey("why not", system_salt_, &newkey);
  int index = -1;
  // The injected keyset in the fixture handles the up_ validation.
  EXPECT_CALL(platform_,
      OpenFile(
        Property(&FilePath::value, EndsWith("master.0")),
        StrEq("wx")))
    .WillOnce(Return(reinterpret_cast<FILE*>(NULL)));
  EXPECT_CALL(platform_,
      OpenFile(
        Property(&FilePath::value, EndsWith("master.1")),
        StrEq("wx")))
    .WillOnce(Return(reinterpret_cast<FILE*>(0xbeefbeef)));
  EXPECT_CALL(*active_vk_, Encrypt(newkey))
    .WillOnce(Return(true));
  EXPECT_CALL(*active_vk_,
      Save(Property(&FilePath::value, EndsWith("master.1"))))
    .WillOnce(Return(true));
  EXPECT_CALL(platform_, DeleteFile(_, _))
    .Times(0);

  EXPECT_EQ(CRYPTOHOME_ERROR_NOT_SET,
            homedirs_.AddKeyset(*up_, newkey, NULL, false, &index));
  EXPECT_EQ(index, 1);
}

TEST_P(KeysetManagementTest, AddKeysetClobber) {
  KeysetSetUp();

  SecureBlob newkey;
  cryptohome::Crypto::PasswordToPasskey("why not", system_salt_, &newkey);
  serialized_.mutable_key_data()->set_label("current label");
  KeyData key_data;
  key_data.set_label("current label");
  FilePath vk_path("/some/path/master.0");
  // Show that 0 is taken.
  EXPECT_CALL(platform_,
      OpenFile(
        Property(&FilePath::value, EndsWith("master.0")),
        StrEq("wx")))
    .WillOnce(Return(reinterpret_cast<FILE*>(NULL)));
  // Let it claim 1 until it searches the labels.
  EXPECT_CALL(platform_,
      OpenFile(
        Property(&FilePath::value, EndsWith("master.1")),
        StrEq("wx")))
    .WillOnce(Return(reinterpret_cast<FILE*>(0xbeefbeef)));
  EXPECT_CALL(*active_vk_, Encrypt(newkey))
    .WillOnce(Return(true));
  EXPECT_CALL(*active_vks_[1], set_legacy_index(_));
  EXPECT_CALL(*active_vks_[1], legacy_index())
    .WillOnce(Return(0));
  EXPECT_CALL(*active_vks_[1], source_file())
    .WillOnce(ReturnRef(vk_path));
  EXPECT_CALL(*active_vk_, Save(vk_path))
    .WillOnce(Return(true));
  EXPECT_CALL(platform_,
      DeleteFile(
        Property(&FilePath::value, EndsWith("master.1")),
        _))
    .Times(1);

  int index = -1;
  EXPECT_EQ(CRYPTOHOME_ERROR_NOT_SET,
            homedirs_.AddKeyset(*up_, newkey, &key_data, true, &index));
  EXPECT_EQ(index, 0);
}


TEST_P(KeysetManagementTest, AddKeysetNoClobber) {
  KeysetSetUp();

  SecureBlob newkey;
  cryptohome::Crypto::PasswordToPasskey("why not", system_salt_, &newkey);
  int index = -1;
  serialized_.mutable_key_data()->set_label("current label");
  KeyData key_data;
  key_data.set_label("current label");
  // The injected keyset in the fixture handles the up_ validation.
  EXPECT_CALL(platform_,
      OpenFile(
        Property(&FilePath::value, EndsWith("master.0")),
        StrEq("wx")))
    .WillOnce(Return(reinterpret_cast<FILE*>(NULL)));
  EXPECT_CALL(platform_,
      OpenFile(
        Property(&FilePath::value, EndsWith("master.1")),
        StrEq("wx")))
    .WillOnce(Return(reinterpret_cast<FILE*>(0xbeefbeef)));

  EXPECT_EQ(CRYPTOHOME_ERROR_KEY_LABEL_EXISTS,
            homedirs_.AddKeyset(*up_, newkey, &key_data, false, &index));
  EXPECT_EQ(index, -1);
}


TEST_P(KeysetManagementTest, UpdateKeysetSuccess) {
  KeysetSetUp();

  // No need to do PasswordToPasskey as that is the
  // external callers job.
  SecureBlob new_secret("why not");
  Key new_key;
  new_key.set_secret("why not");
  new_key.mutable_data()->set_label("new label");
  // The injected keyset in the fixture handles the up_ validation.
  serialized_.mutable_key_data()->set_label("current label");
  FilePath vk_path("/some/path/master.0");
  EXPECT_CALL(*active_vk_, source_file())
    .WillOnce(ReturnRef(vk_path));
  EXPECT_CALL(*active_vk_, Encrypt(new_secret))
    .WillOnce(Return(true));
  EXPECT_CALL(*active_vk_, Save(vk_path))
    .WillOnce(Return(true));

  EXPECT_EQ(CRYPTOHOME_ERROR_NOT_SET,
            homedirs_.UpdateKeyset(*up_,
                                   const_cast<const Key *>(&new_key),
                                   ""));
  EXPECT_EQ(serialized_.key_data().label(), new_key.data().label());
}

TEST_P(KeysetManagementTest, UpdateKeysetAuthorizedNoSignature) {
  KeysetSetUp();

  // No need to do PasswordToPasskey as that is the
  // external callers job.
  Key new_key;
  new_key.set_secret("why not");
  new_key.mutable_data()->set_label("new label");
  new_key.mutable_data()->set_revision(1);
  // The injected keyset in the fixture handles the up_ validation.
  KeyData* key_data = serialized_.mutable_key_data();
  key_data->set_label("current label");
  // Allow the default override on the revision.
  key_data->mutable_privileges()->set_update(false);
  key_data->mutable_privileges()->set_authorized_update(true);
  KeyAuthorizationData* auth_data = key_data->add_authorization_data();
  auth_data->set_type(KeyAuthorizationData::KEY_AUTHORIZATION_TYPE_HMACSHA256);
  KeyAuthorizationSecret* auth_secret = auth_data->add_secrets();
  auth_secret->mutable_usage()->set_sign(true);
  const std::string kSomeHMACKey("abc123");
  auth_secret->set_symmetric_key(kSomeHMACKey);

  EXPECT_EQ(CRYPTOHOME_ERROR_UPDATE_SIGNATURE_INVALID,
            homedirs_.UpdateKeyset(*up_,
                                   const_cast<const Key *>(&new_key),
                                   ""));
  EXPECT_NE(serialized_.key_data().label(), new_key.data().label());
}

TEST_P(KeysetManagementTest, UpdateKeysetAuthorizedSuccess) {
  KeysetSetUp();

  // No need to do PasswordToPasskey as that is the
  // external callers job.
  SecureBlob new_pass("why not");
  Key new_key;
  new_key.set_secret("why not");
  new_key.mutable_data()->set_label("new label");
  // Allow updating over an undefined revision.
  new_key.mutable_data()->set_revision(0);
  // The injected keyset in the fixture handles the up_ validation.
  KeyData* key_data = serialized_.mutable_key_data();
  key_data->set_label("current label");
  key_data->mutable_privileges()->set_update(false);
  key_data->mutable_privileges()->set_authorized_update(true);
  KeyAuthorizationData* auth_data = key_data->add_authorization_data();
  auth_data->set_type(KeyAuthorizationData::KEY_AUTHORIZATION_TYPE_HMACSHA256);
  KeyAuthorizationSecret* auth_secret = auth_data->add_secrets();
  auth_secret->mutable_usage()->set_sign(true);
  const std::string kSomeHMACKey("abc123");
  auth_secret->set_symmetric_key(kSomeHMACKey);

  FilePath vk_path("/some/path/master.0");
  EXPECT_CALL(*active_vk_, source_file())
    .WillOnce(ReturnRef(vk_path));
  EXPECT_CALL(*active_vk_, Encrypt(new_pass))
    .WillOnce(Return(true));
  EXPECT_CALL(*active_vk_, Save(vk_path))
    .WillOnce(Return(true));

  std::string changes_str;
  ac::chrome::managedaccounts::account::Secret new_secret;
  new_secret.set_revision(new_key.data().revision());
  new_secret.set_secret(new_key.secret());
  ASSERT_TRUE(new_secret.SerializeToString(&changes_str));

  brillo::SecureBlob hmac_key(auth_secret->symmetric_key());
  brillo::SecureBlob hmac_data(changes_str.begin(), changes_str.end());
  SecureBlob hmac = CryptoLib::HmacSha256(hmac_key, hmac_data);
  EXPECT_EQ(CRYPTOHOME_ERROR_NOT_SET,
            homedirs_.UpdateKeyset(*up_,
                                   const_cast<const Key *>(&new_key),
                                   hmac.to_string()));
  EXPECT_EQ(serialized_.key_data().revision(), new_key.data().revision());
}

// Ensure signing matches the test vectors in Chrome.
TEST_P(KeysetManagementTest, UpdateKeysetAuthorizedCompatVector) {
  KeysetSetUp();

  // The salted password passed in from Chrome.
  const char kPassword[] = "OSL3HZZSfK+mDQTYUh3lXhgAzJNWhYz52ax0Bleny7Q=";
  // A no-op encryption key.
  const char kB64CipherKey[] = "QUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUE=\n";
  // The signing key pre-installed.
  const char kB64SigningKey[] =
    "p5TR/34XX0R7IMuffH14BiL1vcdSD8EajPzdIg09z9M=\n";
  // The HMAC-256 signature over kPassword using kSigningKey.
  const char kB64Signature[] = "KOPQmmJcMr9iMkr36N1cX+G9gDdBBu7zutAxNayPMN4=\n";

  // No need to do PasswordToPasskey as that is the
  // external callers job.
  SecureBlob new_pass(kPassword);
  Key new_key;
  new_key.set_secret(std::string(kPassword, sizeof(kPassword)-1));
  new_key.mutable_data()->set_label("new label");
  // The compat revision to test is '1'.
  new_key.mutable_data()->set_revision(1);
  // The injected keyset in the fixture handles the up_ validation.
  KeyData* key_data = serialized_.mutable_key_data();
  key_data->set_label("current label");
  key_data->set_revision(0);
  key_data->mutable_privileges()->set_update(false);
  key_data->mutable_privileges()->set_authorized_update(true);
  KeyAuthorizationData* auth_data = key_data->add_authorization_data();
  auth_data->set_type(KeyAuthorizationData::KEY_AUTHORIZATION_TYPE_HMACSHA256);
  KeyAuthorizationSecret* auth_secret = auth_data->add_secrets();
  // Add an encryption secret to ensure later upgrades are viable.
  auth_secret->mutable_usage()->set_encrypt(true);
  std::string cipher_key;
  ASSERT_TRUE(brillo::data_encoding::Base64Decode(kB64CipherKey,
                                                    &cipher_key));
  auth_secret->set_symmetric_key(cipher_key);
  // Add the signing key
  auth_secret = auth_data->add_secrets();
  auth_secret->mutable_usage()->set_sign(true);
  std::string signing_key;
  ASSERT_TRUE(brillo::data_encoding::Base64Decode(kB64SigningKey,
                                                    &signing_key));
  auth_secret->set_symmetric_key(signing_key);

  FilePath vk_path("/some/path/master.0");
  EXPECT_CALL(*active_vk_, source_file())
    .WillOnce(ReturnRef(vk_path));
  EXPECT_CALL(*active_vk_, Encrypt(new_pass))
    .WillOnce(Return(true));
  EXPECT_CALL(*active_vk_, Save(vk_path))
    .WillOnce(Return(true));

  std::string signature;
  ASSERT_TRUE(brillo::data_encoding::Base64Decode(kB64Signature, &signature));
  EXPECT_EQ(CRYPTOHOME_ERROR_NOT_SET,
            homedirs_.UpdateKeyset(*up_,
                                   const_cast<const Key *>(&new_key),
                                   signature));
  EXPECT_EQ(new_key.data().revision(), serialized_.key_data().revision());
}

TEST_P(KeysetManagementTest, UpdateKeysetAuthorizedNoEqualReplay) {
  KeysetSetUp();

  // No need to do PasswordToPasskey as that is the
  // external callers job.
  Key new_key;
  new_key.set_secret("why not");
  new_key.mutable_data()->set_label("new label");
  new_key.mutable_data()->set_revision(100);
  // The injected keyset in the fixture handles the up_ validation.
  KeyData* key_data = serialized_.mutable_key_data();
  key_data->set_revision(100);
  key_data->set_label("current label");
  key_data->mutable_privileges()->set_update(false);
  key_data->mutable_privileges()->set_authorized_update(true);
  KeyAuthorizationData* auth_data = key_data->add_authorization_data();
  auth_data->set_type(KeyAuthorizationData::KEY_AUTHORIZATION_TYPE_HMACSHA256);
  KeyAuthorizationSecret* auth_secret = auth_data->add_secrets();
  auth_secret->mutable_usage()->set_sign(true);
  const std::string kSomeHMACKey("abc123");
  auth_secret->set_symmetric_key(kSomeHMACKey);

  std::string changes_str;
  ac::chrome::managedaccounts::account::Secret new_secret;
  new_secret.set_revision(new_key.data().revision());
  new_secret.set_secret(new_key.secret());
  ASSERT_TRUE(new_secret.SerializeToString(&changes_str));
  brillo::SecureBlob hmac_key(auth_secret->symmetric_key());
  brillo::SecureBlob hmac_data(changes_str.begin(), changes_str.end());
  SecureBlob hmac = CryptoLib::HmacSha256(hmac_key, hmac_data);
  EXPECT_EQ(CRYPTOHOME_ERROR_UPDATE_SIGNATURE_INVALID,
            homedirs_.UpdateKeyset(*up_,
                                   const_cast<const Key *>(&new_key),
                                   hmac.to_string()));
  EXPECT_NE(serialized_.key_data().label(), new_key.data().label());
}


TEST_P(KeysetManagementTest, UpdateKeysetAuthorizedNoLessReplay) {
  KeysetSetUp();

  // No need to do PasswordToPasskey as that is the
  // external callers job.
  Key new_key;
  new_key.set_secret("why not");
  new_key.mutable_data()->set_label("new label");
  new_key.mutable_data()->set_revision(0);
  // The injected keyset in the fixture handles the up_ validation.
  KeyData* key_data = serialized_.mutable_key_data();
  key_data->set_revision(1);
  key_data->set_label("current label");
  key_data->mutable_privileges()->set_update(false);
  key_data->mutable_privileges()->set_authorized_update(true);
  KeyAuthorizationData* auth_data = key_data->add_authorization_data();
  auth_data->set_type(KeyAuthorizationData::KEY_AUTHORIZATION_TYPE_HMACSHA256);
  KeyAuthorizationSecret* auth_secret = auth_data->add_secrets();
  auth_secret->mutable_usage()->set_sign(true);
  const std::string kSomeHMACKey("abc123");
  auth_secret->set_symmetric_key(kSomeHMACKey);

  std::string changes_str;
  ac::chrome::managedaccounts::account::Secret new_secret;
  new_secret.set_revision(new_key.data().revision());
  new_secret.set_secret(new_key.secret());
  ASSERT_TRUE(new_secret.SerializeToString(&changes_str));

  brillo::SecureBlob hmac_key(auth_secret->symmetric_key());
  brillo::SecureBlob hmac_data(changes_str.begin(), changes_str.end());
  SecureBlob hmac = CryptoLib::HmacSha256(hmac_key, hmac_data);
  EXPECT_EQ(CRYPTOHOME_ERROR_UPDATE_SIGNATURE_INVALID,
            homedirs_.UpdateKeyset(*up_,
                                   const_cast<const Key *>(&new_key),
                                   hmac.to_string()));
  EXPECT_NE(serialized_.key_data().label(), new_key.data().label());
}

TEST_P(KeysetManagementTest, UpdateKeysetAuthorizedBadSignature) {
  KeysetSetUp();

  // No need to do PasswordToPasskey as that is the
  // external callers job.
  Key new_key;
  new_key.set_secret("why not");
  new_key.mutable_data()->set_label("new label");
  new_key.mutable_data()->set_revision(0);
  // The injected keyset in the fixture handles the up_ validation.
  KeyData* key_data = serialized_.mutable_key_data();
  key_data->set_label("current label");
  key_data->mutable_privileges()->set_update(false);
  key_data->mutable_privileges()->set_authorized_update(true);
  KeyAuthorizationData* auth_data = key_data->add_authorization_data();
  auth_data->set_type(KeyAuthorizationData::KEY_AUTHORIZATION_TYPE_HMACSHA256);
  KeyAuthorizationSecret* auth_secret = auth_data->add_secrets();
  auth_secret->mutable_usage()->set_sign(true);
  const std::string kSomeHMACKey("abc123");
  auth_secret->set_symmetric_key(kSomeHMACKey);

  std::string changes_str;
  ac::chrome::managedaccounts::account::Secret bad_secret;
  bad_secret.set_revision(new_key.data().revision());
  bad_secret.set_secret("something else");
  ASSERT_TRUE(bad_secret.SerializeToString(&changes_str));

  brillo::SecureBlob hmac_key(auth_secret->symmetric_key());
  brillo::SecureBlob hmac_data(changes_str.begin(), changes_str.end());
  SecureBlob hmac = CryptoLib::HmacSha256(hmac_key, hmac_data);
  EXPECT_EQ(CRYPTOHOME_ERROR_UPDATE_SIGNATURE_INVALID,
            homedirs_.UpdateKeyset(*up_,
                                   const_cast<const Key *>(&new_key),
                                   hmac.to_string()));
  EXPECT_NE(serialized_.key_data().label(), new_key.data().label());
}

TEST_P(KeysetManagementTest, UpdateKeysetBadSecret) {
  KeysetSetUp();

  // No need to do PasswordToPasskey as that is the
  // external callers job.
  SecureBlob new_secret("why not");
  Key new_key;
  new_key.set_secret("why not");
  new_key.mutable_data()->set_label("new label");
  // The injected keyset in the fixture handles the up_ validation.
  serialized_.mutable_key_data()->set_label("current label");

  SecureBlob bad_pass("not it");
  up_.reset(new UsernamePasskey(test_helper_.users[1].username, bad_pass));
  EXPECT_EQ(CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED,
            homedirs_.UpdateKeyset(*up_,
                                   const_cast<const Key *>(&new_key),
                                   ""));
  EXPECT_NE(serialized_.key_data().label(), new_key.data().label());
}

TEST_P(KeysetManagementTest, UpdateKeysetNotFoundWithLabel) {
  KeysetSetUp();

  KeyData some_label;
  some_label.set_label("key that doesn't exist");
  up_->set_key_data(some_label);
  const Key new_key;
  EXPECT_EQ(CRYPTOHOME_ERROR_AUTHORIZATION_KEY_NOT_FOUND,
            homedirs_.UpdateKeyset(*up_,
                                   &new_key,
                                   ""));
}

TEST_P(KeysetManagementTest, RemoveKeysetSuccess) {
  KeysetSetUp();

  Key remove_key;
  remove_key.mutable_data()->set_label("remove me");
  // Expect the 0 slot since it'll match all the fake keys.
  EXPECT_CALL(*active_vks_[1], set_legacy_index(0));
  // Return a different slot to make sure the code is using the right object.
  EXPECT_CALL(*active_vks_[1], legacy_index())
    .WillOnce(Return(1));
  EXPECT_CALL(platform_,
      FileExists(Property(&FilePath::value, EndsWith("master.1"))))
    .WillOnce(Return(true));

  serialized_.mutable_key_data()->mutable_privileges()->set_remove(true);
  serialized_.mutable_key_data()->set_label("remove me");
  EXPECT_EQ(CRYPTOHOME_ERROR_NOT_SET,
            homedirs_.RemoveKeyset(*up_, remove_key.data()));
}

TEST_P(KeysetManagementTest, RemoveKeysetNotFound) {
  KeysetSetUp();

  Key remove_key;
  remove_key.mutable_data()->set_label("remove me please");

  serialized_.mutable_key_data()->mutable_privileges()->set_remove(true);
  serialized_.mutable_key_data()->set_label("the only key in town");
  EXPECT_EQ(CRYPTOHOME_ERROR_KEY_NOT_FOUND,
            homedirs_.RemoveKeyset(*up_, remove_key.data()));
}

TEST_P(KeysetManagementTest, GetVaultKeysetLabelsOneLabeled) {
  KeysetSetUp();

  serialized_.mutable_key_data()->set_label("a labeled key");
  std::vector<std::string> labels;
  EXPECT_TRUE(homedirs_.GetVaultKeysetLabels(*up_, &labels));
  ASSERT_NE(0, labels.size());
  EXPECT_EQ(serialized_.key_data().label(),
            labels[0]);
}

TEST_P(KeysetManagementTest, GetVaultKeysetLabelsOneLegacyLabeled) {
  KeysetSetUp();

  serialized_.clear_key_data();
  std::vector<std::string> labels;
  EXPECT_TRUE(homedirs_.GetVaultKeysetLabels(*up_, &labels));
  ASSERT_NE(0, labels.size());
  EXPECT_EQ(StringPrintf("%s%d", kKeyLegacyPrefix, 0),
            labels[0]);
}

TEST_P(KeysetManagementTest, AddKeysetInvalidCreds) {
  KeysetSetUp();

  SecureBlob newkey;
  cryptohome::Crypto::PasswordToPasskey("why not", system_salt_, &newkey);
  int index = -1;

  EXPECT_CALL(platform_, DeleteFile(_, _))
    .Times(0);
  // Try to authenticate with an unknown key.
  UsernamePasskey bad_p(test_helper_.users[1].username, newkey);
  ASSERT_EQ(CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED,
            homedirs_.AddKeyset(bad_p, newkey, NULL, false, &index));
  EXPECT_EQ(index, -1);
}

TEST_P(KeysetManagementTest, AddKeysetInvalidPrivileges) {
  // Check for key use that lacks valid add privileges
  KeysetSetUp();

  // The injected keyset in the fixture handles the up_ validation.
  SecureBlob newkey;
  cryptohome::Crypto::PasswordToPasskey("why not", system_salt_, &newkey);

  serialized_.mutable_key_data()->mutable_privileges()->set_add(false);
  int index = -1;
  // Tery to authenticate with a key that cannot add keys.
  ASSERT_EQ(CRYPTOHOME_ERROR_AUTHORIZATION_KEY_DENIED,
            homedirs_.AddKeyset(*up_, newkey, NULL, false, &index));
  EXPECT_EQ(index, -1);
}

TEST_P(KeysetManagementTest, AddKeyset0Available) {
  // While this doesn't affect the hole-finding logic, it's good to cover the
  // full logical behavior by changing which key auths too.
  // master.0 -> master.1
  FilePath new_keyset = test_helper_.users[1].keyset_path
    .ReplaceExtension("1");
  test_helper_.users[1].keyset_path = new_keyset;
  KeysetSetUp();

  // The injected keyset in the fixture handles the up_ validation.
  SecureBlob newkey;
  cryptohome::Crypto::PasswordToPasskey("why not", system_salt_, &newkey);

  EXPECT_CALL(platform_,
      OpenFile(
        Property(&FilePath::value, EndsWith("master.0")),
        StrEq("wx")))
    .WillOnce(Return(reinterpret_cast<FILE*>(0xbeefbeef)));
  EXPECT_CALL(*active_vk_, Encrypt(newkey))
    .WillOnce(Return(true));
  EXPECT_CALL(*active_vk_,
      Save(Property(&FilePath::value, EndsWith("master.0"))))
    .WillOnce(Return(true));
  EXPECT_CALL(platform_, DeleteFile(_, _))
    .Times(0);

  int index = -1;
  // Try to authenticate with an unknown key.
  ASSERT_EQ(CRYPTOHOME_ERROR_NOT_SET,
            homedirs_.AddKeyset(*up_, newkey, NULL, false, &index));
  EXPECT_EQ(index, 0);
}

TEST_P(KeysetManagementTest, AddKeyset10Available) {
  KeysetSetUp();

  // The injected keyset in the fixture handles the up_ validation.
  SecureBlob newkey;
  cryptohome::Crypto::PasswordToPasskey("why not", system_salt_, &newkey);

  EXPECT_CALL(platform_,
      OpenFile(
        Property(&FilePath::value,
          MatchesRegex(".*/master\\..$")),
        StrEq("wx")))
    .Times(10)
    .WillRepeatedly(Return(reinterpret_cast<FILE*>(NULL)));
  EXPECT_CALL(platform_,
      OpenFile(
        Property(&FilePath::value, EndsWith("master.10")),
        StrEq("wx")))
    .WillOnce(Return(reinterpret_cast<FILE*>(0xbeefbeef)));
  EXPECT_CALL(platform_, DeleteFile(_, _))
    .Times(0);
  EXPECT_CALL(*active_vk_, Encrypt(newkey))
    .WillOnce(Return(true));
  EXPECT_CALL(*active_vk_,
      Save(Property(&FilePath::value, EndsWith("master.10"))))
    .WillOnce(Return(true));

  int index = -1;
  ASSERT_EQ(CRYPTOHOME_ERROR_NOT_SET,
            homedirs_.AddKeyset(*up_, newkey, NULL, false, &index));
  EXPECT_EQ(index, 10);
}

TEST_P(KeysetManagementTest, AddKeysetNoFreeIndices) {
  KeysetSetUp();

  // The injected keyset in the fixture handles the up_ validation.
  SecureBlob newkey;
  cryptohome::Crypto::PasswordToPasskey("why not", system_salt_, &newkey);

  EXPECT_CALL(platform_, OpenFile(
        Property(&FilePath::value, MatchesRegex(".*/master\\..*$")),
        StrEq("wx")))
    .Times(kKeyFileMax)
    .WillRepeatedly(Return(reinterpret_cast<FILE*>(NULL)));
  EXPECT_CALL(platform_, DeleteFile(_, _))
    .Times(0);

  int index = -1;
  ASSERT_EQ(CRYPTOHOME_ERROR_KEY_QUOTA_EXCEEDED,
            homedirs_.AddKeyset(*up_, newkey, NULL, false, &index));
  EXPECT_EQ(index, -1);
}

TEST_P(KeysetManagementTest, AddKeysetEncryptFail) {
  KeysetSetUp();

  SecureBlob newkey;
  cryptohome::Crypto::PasswordToPasskey("why not", system_salt_, &newkey);
  int index = -1;
  // The injected keyset in the fixture handles the up_ validation.
  EXPECT_CALL(platform_,
      OpenFile(
        Property(&FilePath::value, EndsWith("master.0")),
        StrEq("wx")))
    .WillOnce(Return(reinterpret_cast<FILE*>(0xbeefbeef)));
  EXPECT_CALL(*active_vk_, Encrypt(newkey))
    .WillOnce(Return(false));
  EXPECT_CALL(platform_, CloseFile(reinterpret_cast<FILE*>(0xbeefbeef)))
    .WillOnce(Return(true));
  EXPECT_CALL(platform_,
      DeleteFile(
        Property(&FilePath::value, EndsWith("master.0")),
        false))
    .WillOnce(Return(true));
  ASSERT_EQ(CRYPTOHOME_ERROR_BACKING_STORE_FAILURE,
            homedirs_.AddKeyset(*up_, newkey, NULL, false, &index));
  EXPECT_EQ(index, -1);
}

TEST_P(KeysetManagementTest, AddKeysetSaveFail) {
  KeysetSetUp();

  SecureBlob newkey;
  cryptohome::Crypto::PasswordToPasskey("why not", system_salt_, &newkey);
  int index = -1;
  // The injected keyset in the fixture handles the up_ validation.
  EXPECT_CALL(platform_,
      OpenFile(
        Property(&FilePath::value, EndsWith("master.0")), StrEq("wx")))
    .WillOnce(Return(reinterpret_cast<FILE*>(0xbeefbeef)));
  EXPECT_CALL(*active_vk_, Encrypt(newkey))
    .WillOnce(Return(true));
  EXPECT_CALL(*active_vk_,
      Save(Property(&FilePath::value, EndsWith("master.0"))))
    .WillOnce(Return(false));
  EXPECT_CALL(platform_, CloseFile(reinterpret_cast<FILE*>(0xbeefbeef)))
    .WillOnce(Return(true));
  EXPECT_CALL(platform_,
      DeleteFile(
        Property(&FilePath::value, EndsWith("master.0")), false))
    .WillOnce(Return(true));
  ASSERT_EQ(CRYPTOHOME_ERROR_BACKING_STORE_FAILURE,
            homedirs_.AddKeyset(*up_, newkey, NULL, false, &index));
  EXPECT_EQ(index, -1);
}

TEST_P(KeysetManagementTest, ForceRemoveKeysetSuccess) {
  EXPECT_CALL(platform_,
      FileExists(Property(&FilePath::value, EndsWith("master.0"))))
    .WillOnce(Return(true));
  EXPECT_CALL(platform_,
      DeleteFile(
        Property(&FilePath::value, EndsWith("master.0")), false))
    .WillOnce(Return(true));
  ASSERT_TRUE(homedirs_.ForceRemoveKeyset("a0b0c0", 0));
}

TEST_P(KeysetManagementTest, ForceRemoveKeysetMissingKeyset) {
  EXPECT_CALL(platform_,
      FileExists(
        Property(&FilePath::value, EndsWith("master.0"))))
    .WillOnce(Return(false));
  ASSERT_TRUE(homedirs_.ForceRemoveKeyset("a0b0c0", 0));
}

TEST_P(KeysetManagementTest, ForceRemoveKeysetNegativeIndex) {
  ASSERT_FALSE(homedirs_.ForceRemoveKeyset("a0b0c0", -1));
}

TEST_P(KeysetManagementTest, ForceRemoveKeysetOverMaxIndex) {
  ASSERT_FALSE(homedirs_.ForceRemoveKeyset("a0b0c0", kKeyFileMax));
}

TEST_P(KeysetManagementTest, ForceRemoveKeysetFailedDelete) {
  EXPECT_CALL(platform_,
      FileExists(
        Property(&FilePath::value, EndsWith("master.0"))))
    .WillOnce(Return(true));
  EXPECT_CALL(platform_,
      DeleteFile(
        Property(&FilePath::value, EndsWith("master.0")),
        false))
    .WillOnce(Return(false));
  ASSERT_FALSE(homedirs_.ForceRemoveKeyset("a0b0c0", 0));
}

TEST_P(KeysetManagementTest, MoveKeysetSuccess_0_to_1) {
  const std::string obfuscated = "a0b0c0";
  EXPECT_CALL(platform_,
      FileExists(Property(&FilePath::value, EndsWith("master.0"))))
    .WillOnce(Return(true));
  EXPECT_CALL(platform_,
      FileExists(Property(&FilePath::value, EndsWith("master.1"))))
    .WillOnce(Return(false));
  EXPECT_CALL(platform_,
      OpenFile(
        Property(&FilePath::value, EndsWith("master.1")), StrEq("wx")))
    .WillOnce(Return(reinterpret_cast<FILE*>(0xbeefbeef)));
  EXPECT_CALL(platform_,
      Rename(
        Property(&FilePath::value, EndsWith("master.0")),
        Property(&FilePath::value, EndsWith("master.1"))))
    .WillOnce(Return(true));
  EXPECT_CALL(platform_, CloseFile(reinterpret_cast<FILE*>(0xbeefbeef)))
    .WillOnce(Return(true));
  ASSERT_TRUE(homedirs_.MoveKeyset(obfuscated, 0, 1));
}

TEST_P(KeysetManagementTest, MoveKeysetSuccess_1_to_99) {
  const std::string obfuscated = "a0b0c0";
  EXPECT_CALL(platform_,
      FileExists(Property(&FilePath::value, EndsWith("master.1"))))
    .WillOnce(Return(true));
  EXPECT_CALL(platform_,
      FileExists(Property(&FilePath::value, EndsWith("master.99"))))
    .WillOnce(Return(false));
  EXPECT_CALL(platform_,
      OpenFile(
        Property(&FilePath::value, EndsWith("master.99")),
        StrEq("wx")))
    .WillOnce(Return(reinterpret_cast<FILE*>(0xbeefbeef)));
  EXPECT_CALL(platform_,
      Rename(
        Property(&FilePath::value, EndsWith("master.1")),
        Property(&FilePath::value, EndsWith("master.99"))))
    .WillOnce(Return(true));
  EXPECT_CALL(platform_, CloseFile(reinterpret_cast<FILE*>(0xbeefbeef)))
    .WillOnce(Return(true));
  ASSERT_TRUE(homedirs_.MoveKeyset(obfuscated, 1, 99));
}

TEST_P(KeysetManagementTest, MoveKeysetNegativeSource) {
  const std::string obfuscated = "a0b0c0";
  ASSERT_FALSE(homedirs_.MoveKeyset(obfuscated, -1, 1));
}

TEST_P(KeysetManagementTest, MoveKeysetNegativeDestination) {
  const std::string obfuscated = "a0b0c0";
  ASSERT_FALSE(homedirs_.MoveKeyset(obfuscated, 1, -1));
}

TEST_P(KeysetManagementTest, MoveKeysetTooLargeDestination) {
  const std::string obfuscated = "a0b0c0";
  ASSERT_FALSE(homedirs_.MoveKeyset(obfuscated, 1, kKeyFileMax));
}

TEST_P(KeysetManagementTest, MoveKeysetTooLargeSource) {
  const std::string obfuscated = "a0b0c0";
  ASSERT_FALSE(homedirs_.MoveKeyset(obfuscated, kKeyFileMax, 0));
}

TEST_P(KeysetManagementTest, MoveKeysetMissingSource) {
  const std::string obfuscated = "a0b0c0";
  EXPECT_CALL(platform_,
      FileExists(Property(&FilePath::value, EndsWith("master.0"))))
    .WillOnce(Return(false));
  ASSERT_FALSE(homedirs_.MoveKeyset(obfuscated, 0, 1));
}

TEST_P(KeysetManagementTest, MoveKeysetDestinationExists) {
  const std::string obfuscated = "a0b0c0";
  EXPECT_CALL(platform_,
      FileExists(Property(&FilePath::value, EndsWith("master.0"))))
    .WillOnce(Return(true));
  EXPECT_CALL(platform_,
      FileExists(Property(&FilePath::value, EndsWith("master.1"))))
    .WillOnce(Return(true));
  ASSERT_FALSE(homedirs_.MoveKeyset(obfuscated, 0, 1));
}

TEST_P(KeysetManagementTest, MoveKeysetExclusiveOpenFailed) {
  const std::string obfuscated = "a0b0c0";
  EXPECT_CALL(platform_,
      FileExists(Property(&FilePath::value, EndsWith("master.0"))))
    .WillOnce(Return(true));
  EXPECT_CALL(platform_,
      FileExists(Property(&FilePath::value, EndsWith("master.1"))))
    .WillOnce(Return(false));
  EXPECT_CALL(platform_,
      OpenFile(
        Property(&FilePath::value, EndsWith("master.1")),
        StrEq("wx")))
    .WillOnce(Return(reinterpret_cast<FILE*>(NULL)));
  ASSERT_FALSE(homedirs_.MoveKeyset(obfuscated, 0, 1));
}

TEST_P(KeysetManagementTest, MoveKeysetRenameFailed) {
  const std::string obfuscated = "a0b0c0";
  EXPECT_CALL(platform_,
      FileExists(Property(&FilePath::value, EndsWith("master.0"))))
    .WillOnce(Return(true));
  EXPECT_CALL(platform_,
      FileExists(Property(&FilePath::value, EndsWith("master.1"))))
    .WillOnce(Return(false));
  EXPECT_CALL(platform_,
      OpenFile(
        Property(&FilePath::value, EndsWith("master.1")),
        StrEq("wx")))
    .WillOnce(Return(reinterpret_cast<FILE*>(0xbeefbeef)));
  EXPECT_CALL(platform_,
      Rename(
        Property(&FilePath::value, EndsWith("master.0")),
        Property(&FilePath::value, EndsWith("master.1"))))
    .WillOnce(Return(false));
  EXPECT_CALL(platform_, CloseFile(reinterpret_cast<FILE*>(0xbeefbeef)))
    .WillOnce(Return(true));
  ASSERT_FALSE(homedirs_.MoveKeyset(obfuscated, 0, 1));
}

}  // namespace cryptohome
