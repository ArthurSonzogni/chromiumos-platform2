// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Unit tests for Mount.

#include "cryptohome/storage/mount.h"

#include <map>
#include <memory>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <pwd.h>
#include <regex>  // NOLINT(build/c++11)
#include <stdlib.h>
#include <string.h>  // For memset(), memcpy()
#include <sys/types.h>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/callback_helpers.h>
#include <base/check.h>
#include <base/check_op.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/stl_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/time/time.h>
#include <brillo/cryptohome.h>
#include <brillo/process/process_mock.h>
#include <brillo/secure_blob.h>
#include <chromeos/constants/cryptohome.h>
#include <gtest/gtest.h>
#include <policy/libpolicy.h>

#include "cryptohome/cleanup/user_oldest_activity_timestamp_cache.h"
#include "cryptohome/crypto.h"
#include "cryptohome/cryptohome_common.h"
#include "cryptohome/filesystem_layout.h"
#include "cryptohome/keyset_management.h"
#include "cryptohome/make_tests.h"
#include "cryptohome/mock_crypto.h"
#include "cryptohome/mock_keyset_management.h"
#include "cryptohome/mock_platform.h"
#include "cryptohome/mock_tpm.h"
#include "cryptohome/mock_vault_keyset.h"
#include "cryptohome/storage/encrypted_container/encrypted_container.h"
#include "cryptohome/storage/encrypted_container/encrypted_container_factory.h"
#include "cryptohome/storage/encrypted_container/fake_backing_device.h"
#include "cryptohome/storage/homedirs.h"
#include "cryptohome/storage/mock_homedirs.h"
#include "cryptohome/storage/mount_helper.h"
#include "cryptohome/vault_keyset.h"
#include "cryptohome/vault_keyset.pb.h"

using base::FilePath;
using brillo::SecureBlob;
using ::testing::_;
using ::testing::AnyOf;
using ::testing::AnyOfArray;
using ::testing::DoAll;
using ::testing::EndsWith;
using ::testing::InSequence;
using ::testing::Invoke;
using ::testing::Mock;
using ::testing::NiceMock;
using ::testing::Not;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::SetArgPointee;
using ::testing::StartsWith;
using ::testing::StrEq;
using ::testing::StrictMock;
using ::testing::Unused;
using ::testing::WithArgs;

namespace {

const FilePath kLoopDevice("/dev/loop7");

const gid_t kDaemonGid = 400;  // TODO(wad): expose this in mount.h

MATCHER_P(FilePathMatchesRegex, pattern, "") {
  std::string arg_string = arg.value();

  if (std::regex_match(arg_string, std::regex(pattern)))
    return true;

  *result_listener << arg_string << " did not match regex: " << pattern;

  return false;
}

}  // namespace

namespace cryptohome {

std::string HexDecode(const std::string& hex) {
  std::vector<uint8_t> output;
  CHECK(base::HexStringToBytes(hex, &output));
  return std::string(output.begin(), output.end());
}

class MountTest
    : public ::testing::TestWithParam<bool /* should_test_ecryptfs */> {
 public:
  MountTest() : crypto_(&platform_) {}
  MountTest(const MountTest&) = delete;
  MountTest& operator=(const MountTest&) = delete;

  virtual ~MountTest() {}

  void SetUp() {
    // Populate the system salt
    helper_.SetUpSystemSalt();
    helper_.InjectSystemSalt(&platform_);

    InitializeFilesystemLayout(&platform_, &crypto_, nullptr);
    keyset_management_ = std::make_unique<KeysetManagement>(
        &platform_, &crypto_, helper_.system_salt, nullptr, nullptr);

    std::unique_ptr<EncryptedContainerFactory> container_factory =
        std::make_unique<EncryptedContainerFactory>(
            &platform_, std::make_unique<FakeBackingDeviceFactory>(&platform_));
    KeysetManagement* keyset_management = keyset_management_.get();
    HomeDirs::RemoveCallback remove_callback =
        base::BindRepeating(&KeysetManagement::RemoveLECredentials,
                            base::Unretained(keyset_management));
    homedirs_ = std::make_unique<HomeDirs>(
        &platform_, helper_.system_salt,
        std::make_unique<policy::PolicyProvider>(), remove_callback,
        std::make_unique<CryptohomeVaultFactory>(&platform_,
                                                 std::move(container_factory)));

    platform_.GetFake()->SetStandardUsersAndGroups();

    mount_ = new Mount(&platform_, homedirs_.get());
  }

  void TearDown() {
    mount_ = nullptr;
    helper_.TearDownSystemSalt();
  }

  void InsertTestUsers(const TestUserInfo* user_info_list, int count) {
    helper_.InitTestData(user_info_list, static_cast<size_t>(count),
                         ShouldTestEcryptfs());
  }

  bool DoMountInit() { return mount_->Init(/*use_init_namespace=*/true); }

  bool LoadSerializedKeyset(const brillo::Blob& contents,
                            cryptohome::SerializedVaultKeyset* serialized) {
    CHECK_NE(contents.size(), 0U);
    return serialized->ParseFromArray(contents.data(), contents.size());
  }

  bool StoreSerializedKeyset(const SerializedVaultKeyset& serialized,
                             TestUser* user) {
    user->credentials.resize(serialized.ByteSizeLong());
    serialized.SerializeWithCachedSizesToArray(
        static_cast<google::protobuf::uint8*>(&user->credentials[0]));
    return true;
  }

  void GetKeysetBlob(const SerializedVaultKeyset& serialized,
                     SecureBlob* blob) {
    SecureBlob local_wrapped_keyset(serialized.wrapped_keyset().length());
    serialized.wrapped_keyset().copy(local_wrapped_keyset.char_data(),
                                     serialized.wrapped_keyset().length(), 0);
    blob->swap(local_wrapped_keyset);
  }

  // Returns true if the test is running for eCryptfs, false if for dircrypto.
  bool ShouldTestEcryptfs() const { return GetParam(); }

  Mount::MountArgs GetDefaultMountArgs() const {
    Mount::MountArgs args;
    args.create_as_ecryptfs = ShouldTestEcryptfs();
    return args;
  }

  // Sets expectations for cryptohome key setup.
  void ExpectCryptohomeKeySetup(const TestUser& user) {
    if (ShouldTestEcryptfs()) {
      ExpectCryptohomeKeySetupForEcryptfs(user);
    } else {
      ExpectCryptohomeKeySetupForDircrypto(user);
    }
  }

  // Sets expectations for cryptohome key setup for ecryptfs.
  void ExpectCryptohomeKeySetupForEcryptfs(const TestUser& user) {
    EXPECT_CALL(platform_, AddEcryptfsAuthToken(_, _, _))
        .Times(2)
        .WillRepeatedly(Return(true));
  }

  // Sets expectations for cryptohome key setup for dircrypto.
  void ExpectCryptohomeKeySetupForDircrypto(const TestUser& user) {
    std::string dircrypto_shadow_mount_regex(ShadowRoot().value() +
                                             "/[0-9a-f]{40}/mount");
    EXPECT_CALL(platform_, AddDirCryptoKeyToKeyring(_, _))
        .WillOnce(Return(true));
    EXPECT_CALL(platform_, SetDirCryptoKey(user.vault_mount_path, _))
        .WillOnce(Return(true));
    EXPECT_CALL(platform_,
                InvalidateDirCryptoKey(
                    _, FilePathMatchesRegex(dircrypto_shadow_mount_regex)))
        .WillRepeatedly(Return(true));
  }

  void ExpectCryptohomeMountShadowOnly(const TestUser& user) {
    ExpectCryptohomeKeySetup(user);
    if (ShouldTestEcryptfs()) {
      EXPECT_CALL(platform_,
                  Mount(user.vault_path, user.vault_mount_path, "ecryptfs",
                        kDefaultMountFlags | MS_NOSYMFOLLOW, _))
          .WillOnce(Return(true));
    }
    EXPECT_CALL(platform_, CreateDirectory(user.vault_mount_path))
        .WillRepeatedly(Return(true));
    EXPECT_CALL(platform_, IsDirectoryMounted(user.vault_mount_path))
        .WillOnce(Return(false));
  }

  // Sets expectations for cryptohome mount.
  void ExpectCryptohomeMount(const TestUser& user) {
    ExpectCryptohomeKeySetup(user);
    ExpectDaemonStoreMounts(user, false /* ephemeral_mount */);
    if (ShouldTestEcryptfs()) {
      EXPECT_CALL(platform_,
                  Mount(user.vault_path, user.vault_mount_path, "ecryptfs",
                        kDefaultMountFlags | MS_NOSYMFOLLOW, _))
          .WillOnce(Return(true));
    }
    EXPECT_CALL(platform_, FileExists(base::FilePath(kLockedToSingleUserFile)))
        .WillRepeatedly(Return(false));
    EXPECT_CALL(platform_, CreateDirectory(user.vault_mount_path))
        .WillRepeatedly(Return(true));
    EXPECT_CALL(platform_,
                CreateDirectory(MountHelper::GetNewUserPath(user.username)))
        .WillRepeatedly(Return(true));

    EXPECT_CALL(platform_, IsDirectoryMounted(user.vault_mount_path))
        .WillOnce(Return(false));
    EXPECT_CALL(platform_, IsDirectoryMounted(FilePath("/home/chronos/user")))
        .WillOnce(Return(false));

    EXPECT_CALL(platform_,
                Bind(user.user_vault_mount_path, user.user_vault_mount_path,
                     RemountOption::kShared, true))
        .WillOnce(Return(true));

    EXPECT_CALL(platform_,
                Bind(user.user_vault_mount_path, user.user_mount_path,
                     RemountOption::kMountsFlowIn, true))
        .WillOnce(Return(true));
    EXPECT_CALL(platform_,
                Bind(user.user_vault_mount_path, user.legacy_user_mount_path,
                     RemountOption::kMountsFlowIn, true))
        .WillOnce(Return(true));
    EXPECT_CALL(platform_, Bind(user.user_vault_mount_path,
                                MountHelper::GetNewUserPath(user.username),
                                RemountOption::kMountsFlowIn, true))
        .WillOnce(Return(true));
    EXPECT_CALL(platform_,
                Bind(user.root_vault_mount_path, user.root_mount_path,
                     RemountOption::kMountsFlowIn, true))
        .WillOnce(Return(true));
    ExpectDownloadsBindMounts(user, false /* ephemeral_mount */);
    EXPECT_CALL(platform_,
                RestoreSELinuxContexts(base::FilePath(user.base_path), true))
        .WillOnce(Return(true));
  }

  void ExpectDownloadsBindMounts(const TestUser& user, bool ephemeral_mount) {
    const FilePath user_home = ephemeral_mount ? user.user_ephemeral_mount_path
                                               : user.user_vault_mount_path;

    // Mounting Downloads to MyFiles/Downloads in user home directory.
    EXPECT_CALL(platform_, Bind(user_home.Append("Downloads"),
                                user_home.Append("MyFiles/Downloads"), _, true))
        .WillOnce(Return(true));

    auto downloads_path = user_home.Append("Downloads");
    auto downloads_in_myfiles =
        user.user_vault_mount_path.Append("MyFiles").Append("Downloads");

    NiceMock<MockFileEnumerator>* in_myfiles_download_enumerator =
        new NiceMock<MockFileEnumerator>();
    EXPECT_CALL(platform_, GetFileEnumerator(downloads_in_myfiles, false, _))
        .WillOnce(Return(in_myfiles_download_enumerator));
  }

  void ExpectCacheBindMounts(const TestUser& user) {
    // Mounting cache/<dir> to mount/<dir> in /home/.shadow/<hash>
    EXPECT_CALL(platform_, Bind(user.vault_cache_path.Append("user/Cache"),
                                user.vault_mount_path.Append("user/Cache"), _,
                                /*nosymfollow=*/true))
        .WillOnce(Return(true));

    EXPECT_CALL(platform_, Bind(user.vault_cache_path.Append("user/GCache"),
                                user.vault_mount_path.Append("user/GCache"), _,
                                /*nosymfollow=*/true))
        .WillOnce(Return(true));
  }

  void ExpectCacheBindUnmounts(const TestUser& user) {
    EXPECT_CALL(platform_,
                Unmount(user.vault_mount_path.Append("user/Cache"), _, _))
        .WillOnce(Return(true));
    EXPECT_CALL(platform_,
                Unmount(user.vault_mount_path.Append("user/GCache"), _, _))
        .WillOnce(Return(true));
  }

  // Sets expectations for MountHelper::MountDaemonStoreDirectories. In
  // particular, sets up |platform_| to pretend that all daemon store
  // directories exists, so that they're all mounted. Without calling this
  // method, daemon store directories are pretended to not exist.
  void ExpectDaemonStoreMounts(const TestUser& user, bool ephemeral_mount) {
    // Return a mock daemon store directory in /etc/daemon-store.
    constexpr char kDaemonName[] = "mock-daemon";
    constexpr uid_t kDaemonUid = 123;
    constexpr gid_t kDaemonGid = 234;
    base::stat_wrapper_t stat_data = {};
    stat_data.st_mode = S_IFDIR;
    stat_data.st_uid = kDaemonUid;
    stat_data.st_gid = kDaemonGid;
    const base::FilePath daemon_store_base_dir(kEtcDaemonStoreBaseDir);
    const FileEnumerator::FileInfo daemon_info(
        daemon_store_base_dir.AppendASCII(kDaemonName), stat_data);
    NiceMock<MockFileEnumerator>* daemon_enumerator =
        new NiceMock<MockFileEnumerator>();
    daemon_enumerator->entries_.push_back(daemon_info);
    EXPECT_CALL(platform_, GetFileEnumerator(daemon_store_base_dir, false,
                                             base::FileEnumerator::DIRECTORIES))
        .WillOnce(Return(daemon_enumerator));

    const FilePath run_daemon_store_path =
        FilePath(kRunDaemonStoreBaseDir).Append(kDaemonName);

    EXPECT_CALL(platform_, DirectoryExists(run_daemon_store_path))
        .WillOnce(Return(true));

    const FilePath root_home = ephemeral_mount ? user.root_ephemeral_mount_path
                                               : user.root_vault_mount_path;
    const FilePath mount_source = root_home.Append(kDaemonName);
    const FilePath mount_target =
        run_daemon_store_path.Append(user.obfuscated_username);

    // TODO(dlunev): made those repeated since in some cases it is strictly
    // impossible to have the mocks perform correctly with current test
    // architecture. Once service.cc and related are gone, re-architect.
    EXPECT_CALL(platform_, DirectoryExists(mount_source))
        .WillRepeatedly(Return(false));

    EXPECT_CALL(platform_, SafeCreateDirAndSetOwnershipAndPermissions(
                               mount_source, stat_data.st_mode,
                               stat_data.st_uid, stat_data.st_gid))
        .WillRepeatedly(Return(true));

    EXPECT_CALL(platform_, CreateDirectory(mount_target))
        .WillOnce(Return(true));

    EXPECT_CALL(platform_, Bind(mount_source, mount_target, _, true))
        .WillOnce(Return(true));
  }

 protected:
  // Protected for trivial access.
  MakeTests helper_;
  NiceMock<MockPlatform> platform_;
  NiceMock<MockTpm> tpm_;
  Crypto crypto_;
  std::unique_ptr<KeysetManagement> keyset_management_;
  std::unique_ptr<HomeDirs> homedirs_;
  scoped_refptr<Mount> mount_;
};

INSTANTIATE_TEST_SUITE_P(WithEcryptfs, MountTest, ::testing::Values(true));
INSTANTIATE_TEST_SUITE_P(WithDircrypto, MountTest, ::testing::Values(false));

TEST_P(MountTest, BadInitTest) {
  SecureBlob passkey;
  cryptohome::Crypto::PasswordToPasskey(kDefaultUsers[0].password,
                                        helper_.system_salt, &passkey);

  // Just fail some initialization calls.
  EXPECT_CALL(platform_, GetUserId(_, _, _)).WillRepeatedly(Return(false));
  EXPECT_CALL(platform_, GetGroupId(_, _)).WillRepeatedly(Return(false));
  EXPECT_FALSE(mount_->Init(/*use_init_namespace=*/true));
}

TEST_P(MountTest, BindMyFilesDownloadsSuccess) {
  FilePath dest_dir("/home/.shadow/userhash/mount/user");
  auto downloads_path = dest_dir.Append("Downloads");
  auto downloads_in_myfiles = dest_dir.Append("MyFiles").Append("Downloads");
  NiceMock<MockFileEnumerator>* in_myfiles_download_enumerator =
      new NiceMock<MockFileEnumerator>();

  EXPECT_CALL(platform_, GetFileEnumerator(downloads_in_myfiles, false, _))
      .WillOnce(Return(in_myfiles_download_enumerator));
  EXPECT_CALL(platform_, Bind(downloads_path, downloads_in_myfiles, _, true))
      .WillOnce(Return(true));

  MountHelper mnt_helper(fake_platform::kChronosUID, fake_platform::kChronosGID,
                         fake_platform::kSharedGID, helper_.system_salt,
                         true /*legacy_mount*/, true /* bind_mount_downloads */,
                         &platform_);

  EXPECT_TRUE(mnt_helper.BindMyFilesDownloads(dest_dir));
}

TEST_P(MountTest, BindMyFilesDownloadsRemoveExistingFiles) {
  FilePath dest_dir("/home/.shadow/userhash/mount/user");
  auto downloads_path = dest_dir.Append("Downloads");
  auto downloads_in_myfiles = dest_dir.Append("MyFiles").Append("Downloads");
  const std::string existing_files[] = {"dir1", "file1"};
  std::vector<FilePath> existing_files_in_download;
  std::vector<FilePath> existing_files_in_myfiles_download;
  auto* in_myfiles_download_enumerator = new NiceMock<MockFileEnumerator>();
  base::stat_wrapper_t stat_file = {};
  stat_file.st_mode = S_IRWXU;
  base::stat_wrapper_t stat_dir = {};
  stat_dir.st_mode = S_IFDIR;

  for (auto base : existing_files) {
    existing_files_in_download.push_back(downloads_path.Append(base));
    existing_files_in_myfiles_download.push_back(
        downloads_in_myfiles.Append(base));
  }
  in_myfiles_download_enumerator->entries_.push_back(
      FileEnumerator::FileInfo(downloads_in_myfiles.Append("dir1"), stat_dir));
  in_myfiles_download_enumerator->entries_.push_back(FileEnumerator::FileInfo(
      downloads_in_myfiles.Append("file1"), stat_file));

  // When MyFiles/Downloads doesn't exists BindMyFilesDownloads returns false.
  EXPECT_CALL(platform_, GetFileEnumerator(downloads_in_myfiles, false, _))
      .WillOnce(Return(in_myfiles_download_enumerator));
  EXPECT_CALL(platform_, FileExists(AnyOfArray(existing_files_in_download)))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, DeletePathRecursively(
                             AnyOfArray(existing_files_in_myfiles_download)))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, Bind(downloads_path, downloads_in_myfiles, _, true))
      .WillOnce(Return(true));

  MountHelper mnt_helper(fake_platform::kChronosUID, fake_platform::kChronosGID,
                         fake_platform::kSharedGID, helper_.system_salt,
                         true /*legacy_mount*/, true /* bind_mount_downloads */,
                         &platform_);

  EXPECT_TRUE(mnt_helper.BindMyFilesDownloads(dest_dir));
}

TEST_P(MountTest, BindMyFilesDownloadsMoveForgottenFiles) {
  FilePath dest_dir("/home/.shadow/userhash/mount/user");
  auto downloads_path = dest_dir.Append("Downloads");
  auto downloads_in_myfiles = dest_dir.Append("MyFiles").Append("Downloads");
  const std::string existing_files[] = {"dir1", "file1"};
  std::vector<FilePath> existing_files_in_download;
  std::vector<FilePath> existing_files_in_myfiles_download;
  auto* in_myfiles_download_enumerator = new NiceMock<MockFileEnumerator>();
  base::stat_wrapper_t stat_file = {};
  stat_file.st_mode = S_IRWXU;
  base::stat_wrapper_t stat_dir = {};
  stat_dir.st_mode = S_IFDIR;

  for (auto base : existing_files) {
    existing_files_in_download.push_back(downloads_path.Append(base));
    existing_files_in_myfiles_download.push_back(
        downloads_in_myfiles.Append(base));
  }
  in_myfiles_download_enumerator->entries_.push_back(FileEnumerator::FileInfo(
      downloads_in_myfiles.Append("file1"), stat_file));
  in_myfiles_download_enumerator->entries_.push_back(
      FileEnumerator::FileInfo(downloads_in_myfiles.Append("dir1"), stat_dir));

  // When MyFiles/Downloads doesn't exists BindMyFilesDownloads returns false.
  EXPECT_CALL(platform_, GetFileEnumerator(downloads_in_myfiles, false, _))
      .WillOnce(Return(in_myfiles_download_enumerator));
  EXPECT_CALL(platform_, FileExists(AnyOfArray(existing_files_in_download)))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(platform_, Move(AnyOfArray(existing_files_in_myfiles_download),
                              AnyOfArray(existing_files_in_download)))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, Bind(downloads_path, downloads_in_myfiles, _, true))
      .WillOnce(Return(true));

  MountHelper mnt_helper(fake_platform::kChronosUID, fake_platform::kChronosGID,
                         fake_platform::kSharedGID, helper_.system_salt,
                         true /*legacy_mount*/, true /* bind_mount_downloads */,
                         &platform_);

  EXPECT_TRUE(mnt_helper.BindMyFilesDownloads(dest_dir));
}

TEST_P(MountTest, CreateDmcryptSubdirectories) {
  InsertTestUsers(&kDefaultUsers[10], 1);
  TestUser* user = &helper_.users[0];
  FilePath user_shadow_dir = ShadowRoot().Append(user->obfuscated_username);

  MountHelper mnt_helper(fake_platform::kChronosUID, fake_platform::kChronosGID,
                         fake_platform::kSharedGID, helper_.system_salt,
                         true /*legacy_mount*/, true /* bind_mount_downloads */,
                         &platform_);

  // Expect creation of all dm-crypt subdirectories.
  for (auto dir : MountHelper::GetDmcryptSubdirectories(
           fake_platform::kChronosUID, fake_platform::kChronosGID,
           fake_platform::kSharedGID)) {
    EXPECT_CALL(platform_, SafeCreateDirAndSetOwnershipAndPermissions(
                               user_shadow_dir.Append(dir.path), dir.mode,
                               dir.uid, dir.gid))
        .WillOnce(Return(true));
  }
  ASSERT_TRUE(
      mnt_helper.CreateDmcryptSubdirectories(user->obfuscated_username));
}

TEST_P(MountTest, BindTrackedSubdirectoriesFromCache) {
  // Checks the cache subdirectories are correctly bind mounted for dm-crypt
  // vaults but not for other vaults.
  InsertTestUsers(&kDefaultUsers[10], 1);
  TestUser* user = &helper_.users[0];

  ASSERT_TRUE(platform_.CreateDirectory(user->vault_cache_path));
  ExpectCacheBindMounts(*user);
  MountHelper mnt_helper(fake_platform::kChronosUID, fake_platform::kChronosGID,
                         fake_platform::kSharedGID, helper_.system_salt,
                         true /*legacy_mount*/, true /* bind_mount_downloads */,
                         &platform_);

  ASSERT_TRUE(mnt_helper.MountCacheSubdirectories(user->obfuscated_username));

  ExpectCacheBindUnmounts(*user);
  mnt_helper.UnmountAll();
}

TEST_P(MountTest, MountDmcrypt) {
  // Checks that PerformMount sets up a dm-crypt vault successfully.
  InsertTestUsers(&kDefaultUsers[10], 1);
  TestUser* user = &helper_.users[0];
  FilePath user_shadow_dir = ShadowRoot().Append(user->obfuscated_username);

  ASSERT_TRUE(platform_.CreateDirectory(user->vault_cache_path));

  MountHelper mnt_helper(fake_platform::kChronosUID, fake_platform::kChronosGID,
                         fake_platform::kSharedGID, helper_.system_salt,
                         true /*legacy_mount*/, true /* bind_mount_downloads */,
                         &platform_);

  MountHelper::Options options;
  options.type = MountType::DMCRYPT;
  options.to_migrate_from_ecryptfs = false;
  MountError error;

  // Expect existing cache and mount subdirectories.
  EXPECT_CALL(platform_, DirectoryExists(Property(
                             &FilePath::value,
                             StartsWith(user->vault_cache_path.value()))))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, DirectoryExists(Property(
                             &FilePath::value,
                             StartsWith(user->vault_mount_path.value()))))
      .WillRepeatedly(Return(true));

  // Expect bind mounts for the user/ and root/ directories.
  EXPECT_CALL(platform_, SafeCreateDirAndSetOwnershipAndPermissions(
                             user->user_vault_mount_path, _, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, SafeCreateDirAndSetOwnershipAndPermissions(
                             user->root_vault_mount_path, _, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_,
              Bind(user->user_vault_mount_path, user->user_vault_mount_path,
                   RemountOption::kShared, true))
      .WillOnce(Return(true));

  EXPECT_CALL(platform_,
              Bind(user->user_vault_mount_path, user->user_mount_path,
                   RemountOption::kMountsFlowIn, true))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_,
              Bind(user->user_vault_mount_path, user->legacy_user_mount_path,
                   RemountOption::kMountsFlowIn, true))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, Bind(user->user_vault_mount_path,
                              MountHelper::GetNewUserPath(user->username),
                              RemountOption::kMountsFlowIn, true))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_,
              Bind(user->root_vault_mount_path, user->root_mount_path,
                   RemountOption::kMountsFlowIn, true))
      .WillOnce(Return(true));

  // Expect existing dm-crypt subdirectories.
  for (auto dir : MountHelper::GetDmcryptSubdirectories(
           fake_platform::kChronosUID, fake_platform::kChronosGID,
           fake_platform::kSharedGID)) {
    EXPECT_CALL(platform_, DirectoryExists(user_shadow_dir.Append(dir.path)))
        .WillOnce(Return(true));
  }

  ExpectCacheBindMounts(*user);
  ExpectDownloadsBindMounts(*user, false /* ephemeral_mount */);
  ExpectDaemonStoreMounts(*user, false /* is_ephemeral */);

  EXPECT_CALL(platform_,
              Mount(_, user->vault_mount_path, kDmcryptContainerMountType,
                    kDefaultMountFlags | MS_NOSYMFOLLOW, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_,
              Mount(_, user->vault_cache_path, kDmcryptContainerMountType,
                    kDefaultMountFlags | MS_NOSYMFOLLOW, _))
      .WillOnce(Return(true));

  EXPECT_TRUE(mnt_helper.PerformMount(options, user->username, "foo", "bar",
                                      false, &error));
}

TEST_P(MountTest, MountCryptohome) {
  // checks that cryptohome tries to mount successfully, and tests that the
  // tracked directories are created/replaced as expected
  InsertTestUsers(&kDefaultUsers[10], 1);
  EXPECT_CALL(platform_, DirectoryExists(ShadowRoot()))
      .WillRepeatedly(Return(true));
  EXPECT_TRUE(DoMountInit());

  TestUser* user = &helper_.users[0];

  user->InjectUserPaths(&platform_, fake_platform::kChronosUID,
                        fake_platform::kChronosGID, fake_platform::kSharedGID,
                        kDaemonGid, ShouldTestEcryptfs());

  ExpectCryptohomeMount(*user);
  EXPECT_CALL(platform_, ClearUserKeyring()).WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, FileExists(base::FilePath(kLockedToSingleUserFile)))
      .WillRepeatedly(Return(false));

  // user exists, so there'll be no skel copy after.

  MountError error = MOUNT_ERROR_NONE;
  EXPECT_TRUE(mount_->MountCryptohome(user->username, FileSystemKeyset(),
                                      GetDefaultMountArgs(),
                                      /* is_pristine */ false, &error));

  EXPECT_CALL(platform_, Unmount(_, _, _)).WillRepeatedly(Return(true));
  if (ShouldTestEcryptfs())
    EXPECT_CALL(platform_, ClearUserKeyring()).WillOnce(Return(true));
  EXPECT_TRUE(mount_->UnmountCryptohome());
}

TEST_P(MountTest, MountPristineCryptohome) {
  // TODO(wad) Drop NiceMock and replace with InSequence EXPECT_CALL()s.
  // It will complain about creating tracked subdirs, but that is non-fatal.
  EXPECT_TRUE(DoMountInit());
  // Test user at index 12 hasn't been created.
  InsertTestUsers(&kDefaultUsers[12], 1);
  TestUser* user = &helper_.users[0];

  EXPECT_CALL(platform_,
              DirectoryExists(AnyOf(user->vault_path, user->vault_mount_path,
                                    user->user_vault_path)))
      .Times(1)
      .WillRepeatedly(Return(false));

  EXPECT_CALL(platform_, FileExists(base::FilePath(kLockedToSingleUserFile)))
      .WillRepeatedly(Return(false));

  EXPECT_CALL(platform_, GetFileEnumerator(SkelDir(), _, _))
      .WillOnce(Return(new NiceMock<MockFileEnumerator>()))
      .WillOnce(Return(new NiceMock<MockFileEnumerator>()));

  EXPECT_CALL(platform_, DirectoryExists(Property(
                             &FilePath::value,
                             AnyOf(StartsWith(user->user_mount_path.value()),
                                   StartsWith(user->root_mount_path.value()),
                                   StartsWith(user->new_user_path.value())))))
      .WillRepeatedly(Return(false));

  EXPECT_CALL(platform_, IsDirectoryMounted(Property(
                             &FilePath::value,
                             AnyOf(StartsWith(user->user_mount_path.value()),
                                   StartsWith(user->root_mount_path.value()),
                                   StartsWith(user->new_user_path.value())))))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(platform_, Stat(_, _)).WillRepeatedly(Return(false));
  EXPECT_CALL(platform_, CreateDirectory(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SafeCreateDirAndSetOwnership(_, _, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SafeCreateDirAndSetOwnershipAndPermissions(_, _, _, _))
      .WillRepeatedly(Return(true));

  ExpectCryptohomeMount(*user);

  // Fake successful mount to /home/chronos/user/*
  EXPECT_CALL(platform_, FileExists(Property(
                             &FilePath::value,
                             StartsWith(user->legacy_user_mount_path.value()))))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, DirectoryExists(Property(
                             &FilePath::value,
                             StartsWith(user->user_vault_mount_path.value()))))
      .WillRepeatedly(Return(true));

  Mount::MountArgs mount_args = GetDefaultMountArgs();
  MountError error = MOUNT_ERROR_NONE;
  ASSERT_TRUE(mount_->MountCryptohome(user->username, FileSystemKeyset(),
                                      mount_args,
                                      /* is_pristine */ true, &error));
  ASSERT_EQ(MOUNT_ERROR_NONE, error);

  ON_CALL(platform_, Unmount(_, _, _)).WillByDefault(Return(true));
  if (ShouldTestEcryptfs())
    EXPECT_CALL(platform_, ClearUserKeyring()).WillOnce(Return(true));
  ASSERT_TRUE(mount_->UnmountCryptohome());
}

TEST_P(MountTest, RememberMountOrderingTest) {
  // Checks that mounts made with MountAndPush/BindAndPush are undone in the
  // right order.
  MountHelper mnt_helper(fake_platform::kChronosUID, fake_platform::kChronosGID,
                         fake_platform::kSharedGID, helper_.system_salt,
                         true /*legacy_mount*/, true /* bind_mount_downloads */,
                         &platform_);

  FilePath src("/src");
  FilePath dest0("/dest/foo");
  FilePath dest1("/dest/bar");
  FilePath dest2("/dest/baz");
  {
    InSequence sequence;
    EXPECT_CALL(platform_,
                Mount(src, dest0, _, kDefaultMountFlags | MS_NOSYMFOLLOW, _))
        .WillOnce(Return(true));
    EXPECT_CALL(platform_, Bind(src, dest1, _, true)).WillOnce(Return(true));
    EXPECT_CALL(platform_,
                Mount(src, dest2, _, kDefaultMountFlags | MS_NOSYMFOLLOW, _))
        .WillOnce(Return(true));
    EXPECT_CALL(platform_, Unmount(dest2, _, _)).WillOnce(Return(true));
    EXPECT_CALL(platform_, Unmount(dest1, _, _)).WillOnce(Return(true));
    EXPECT_CALL(platform_, Unmount(dest0, _, _)).WillOnce(Return(true));

    EXPECT_TRUE(mnt_helper.MountAndPush(src, dest0, "", ""));
    EXPECT_TRUE(mnt_helper.BindAndPush(src, dest1, RemountOption::kShared));
    EXPECT_TRUE(mnt_helper.MountAndPush(src, dest2, "", ""));
    mnt_helper.UnmountAll();
  }
}

TEST_P(MountTest, CreateTrackedSubdirectoriesReplaceExistingDir) {
  EXPECT_TRUE(DoMountInit());
  InsertTestUsers(&kDefaultUsers[0], 1);
  TestUser* user = &helper_.users[0];

  FilePath dest_dir;
  if (ShouldTestEcryptfs()) {
    dest_dir = user->vault_path;
    mount_->mount_type_ = ::cryptohome::MountType::ECRYPTFS;
  } else {
    dest_dir = user->vault_mount_path;
    mount_->mount_type_ = ::cryptohome::MountType::DIR_CRYPTO;
  }
  EXPECT_CALL(platform_, DirectoryExists(dest_dir)).WillOnce(Return(true));

  // Expectations for each tracked subdirectory.
  for (const auto& tracked_dir : MountHelper::GetTrackedSubdirectories(
           fake_platform::kChronosUID, fake_platform::kChronosGID,
           fake_platform::kSharedGID)) {
    const FilePath tracked_dir_path = dest_dir.Append(tracked_dir.path);
    const FilePath userside_dir =
        user->vault_mount_path.Append(tracked_dir.path);
    // Simulate the case there already exists a non-passthrough-dir
    if (ShouldTestEcryptfs()) {
      // For ecryptfs, delete and replace the existing directory.
      EXPECT_CALL(platform_, DirectoryExists(userside_dir))
          .WillOnce(Return(true));
      EXPECT_CALL(platform_, DeletePathRecursively(userside_dir))
          .WillOnce(Return(true));
      EXPECT_CALL(platform_, DeleteFile(tracked_dir_path))
          .WillOnce(Return(true));
      EXPECT_CALL(platform_, DirectoryExists(tracked_dir_path))
          .WillOnce(Return(false))
          .WillOnce(Return(false));
      EXPECT_CALL(platform_, SafeCreateDirAndSetOwnershipAndPermissions(
                                 tracked_dir_path, tracked_dir.mode,
                                 tracked_dir.uid, tracked_dir.gid))
          .WillOnce(Return(true));
    } else {
      // For dircrypto, just skip the directory creation.
      EXPECT_CALL(platform_, DirectoryExists(tracked_dir_path))
          .WillOnce(Return(true));
      EXPECT_CALL(platform_,
                  SetExtendedFileAttribute(
                      tracked_dir_path, kTrackedDirectoryNameAttribute,
                      StrEq(tracked_dir_path.BaseName().value()),
                      tracked_dir_path.BaseName().value().size()))
          .WillOnce(Return(true));
    }
  }
  // Run the method.
  EXPECT_TRUE(mount_->CreateTrackedSubdirectories(user->username));
}

TEST_P(MountTest, MountCryptohomePreviousMigrationIncomplete) {
  // Checks that if both ecryptfs and dircrypto home directories
  // exist, fails with an error.
  EXPECT_CALL(platform_, DirectoryExists(ShadowRoot()))
      .WillRepeatedly(Return(true));
  EXPECT_TRUE(DoMountInit());

  // Prepare a placeholder user and a key.
  InsertTestUsers(&kDefaultUsers[10], 1);
  TestUser* user = &helper_.users[0];

  EXPECT_CALL(platform_, DirectoryExists(Property(
                             &FilePath::value,
                             AnyOf(StartsWith(user->user_mount_path.value()),
                                   StartsWith(user->root_mount_path.value()),
                                   StartsWith(user->new_user_path.value())))))
      .WillRepeatedly(Return(false));

  EXPECT_CALL(platform_, IsDirectoryMounted(Property(
                             &FilePath::value,
                             AnyOf(StartsWith(user->user_mount_path.value()),
                                   StartsWith(user->root_mount_path.value()),
                                   StartsWith(user->new_user_path.value())))))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(platform_, Stat(_, _)).WillRepeatedly(Return(false));
  EXPECT_CALL(platform_, CreateDirectory(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SafeCreateDirAndSetOwnership(_, _, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SafeCreateDirAndSetOwnershipAndPermissions(_, _, _, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, FileExists(base::FilePath(kLockedToSingleUserFile)))
      .WillRepeatedly(Return(false));

  // Mock the situation that both types of data directory exists.
  EXPECT_CALL(platform_,
              DirectoryExists(AnyOf(user->vault_path, user->vault_mount_path,
                                    user->user_vault_path)))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, GetDirCryptoKeyState(user->vault_mount_path))
      .WillRepeatedly(Return(dircrypto::KeyState::ENCRYPTED));

  MountError error = MOUNT_ERROR_NONE;
  ASSERT_FALSE(mount_->MountCryptohome(user->username, FileSystemKeyset(),
                                       GetDefaultMountArgs(),
                                       /* is_pristine */ false, &error));
  ASSERT_EQ(MOUNT_ERROR_PREVIOUS_MIGRATION_INCOMPLETE, error);
}

TEST_P(MountTest, MountCryptohomeToMigrateFromEcryptfs) {
  // Checks that to_migrate_from_ecryptfs option is handled correctly.
  // When the existing vault is ecryptfs, mount it to a temporary location while
  // setting up a new dircrypto directory.
  // When the existing vault is dircrypto, just fail.
  InsertTestUsers(&kDefaultUsers[10], 1);
  EXPECT_CALL(platform_, DirectoryExists(ShadowRoot()))
      .WillRepeatedly(Return(true));
  EXPECT_TRUE(DoMountInit());

  TestUser* user = &helper_.users[0];

  // Inject dircrypto user paths.
  user->InjectUserPaths(&platform_, fake_platform::kChronosUID,
                        fake_platform::kChronosGID, fake_platform::kSharedGID,
                        kDaemonGid, false /* is_ecryptfs */);

  if (ShouldTestEcryptfs()) {
    // Inject user ecryptfs paths too.
    user->InjectUserPaths(&platform_, fake_platform::kChronosUID,
                          fake_platform::kChronosGID, fake_platform::kSharedGID,
                          kDaemonGid, true /* is_ecryptfs */);

    // When an ecryptfs vault exists, mount it to a temporary location.
    FilePath temporary_mount = user->base_path.Append(kTemporaryMountDir);
    EXPECT_CALL(platform_, CreateDirectory(temporary_mount))
        .WillOnce(Return(true));
    EXPECT_CALL(platform_, Mount(user->vault_path, temporary_mount, "ecryptfs",
                                 kDefaultMountFlags | MS_NOSYMFOLLOW, _))
        .WillOnce(Return(true));

    // Key set up for both dircrypto and ecryptfs.
    ExpectCryptohomeKeySetupForDircrypto(*user);
    ExpectCryptohomeKeySetupForEcryptfs(*user);

    EXPECT_CALL(platform_, DirectoryExists(user->vault_path))
        .WillRepeatedly(Return(true));

    EXPECT_CALL(platform_, IsDirectoryMounted(user->vault_mount_path))
        .WillOnce(Return(false));

    EXPECT_CALL(platform_, CreateDirectory(user->vault_mount_path))
        .WillRepeatedly(Return(true));
  }

  EXPECT_CALL(platform_,
              CreateDirectory(MountHelper::GetNewUserPath(user->username)))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, FileExists(base::FilePath(kLockedToSingleUserFile)))
      .WillRepeatedly(Return(false));

  MountError error = MOUNT_ERROR_NONE;
  Mount::MountArgs mount_args = GetDefaultMountArgs();
  mount_args.to_migrate_from_ecryptfs = true;
  if (ShouldTestEcryptfs()) {
    EXPECT_TRUE(mount_->MountCryptohome(user->username, FileSystemKeyset(),
                                        mount_args,
                                        /* is_pristine */ false, &error));
  } else {
    // Fail if the existing vault is not ecryptfs.
    EXPECT_FALSE(mount_->MountCryptohome(user->username, FileSystemKeyset(),
                                         mount_args,
                                         /* is_pristine */ false, &error));
  }
}

TEST_P(MountTest, MountCryptohomeForceDircrypto) {
  // Checks that the force-dircrypto flag correctly rejects to mount ecryptfs.
  EXPECT_CALL(platform_, DirectoryExists(ShadowRoot()))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, FileExists(base::FilePath(kLockedToSingleUserFile)))
      .WillRepeatedly(Return(false));
  EXPECT_TRUE(DoMountInit());

  // Prepare a placeholder user and a key.
  InsertTestUsers(&kDefaultUsers[10], 1);
  TestUser* user = &helper_.users[0];
  user->InjectUserPaths(&platform_, fake_platform::kChronosUID,
                        fake_platform::kChronosGID, fake_platform::kSharedGID,
                        kDaemonGid, ShouldTestEcryptfs());

  EXPECT_CALL(platform_, CreateDirectory(_)).WillRepeatedly(Return(true));

  // Mock setup for successful mount when dircrypto is tested.
  if (!ShouldTestEcryptfs()) {
    ExpectCryptohomeMount(*user);

    // Expectations for tracked subdirectories
    EXPECT_CALL(platform_, DirectoryExists(Property(
                               &FilePath::value,
                               StartsWith(user->vault_mount_path.value()))))
        .WillRepeatedly(Return(true));
    EXPECT_CALL(platform_,
                SetExtendedFileAttribute(
                    Property(&FilePath::value,
                             StartsWith(user->vault_mount_path.value())),
                    _, _, _))
        .WillRepeatedly(Return(true));
    EXPECT_CALL(platform_, FileExists(Property(
                               &FilePath::value,
                               StartsWith(user->vault_mount_path.value()))))
        .WillRepeatedly(Return(true));
    EXPECT_CALL(
        platform_,
        SetGroupAccessible(Property(&FilePath::value,
                                    StartsWith(user->vault_mount_path.value())),
                           _, _))
        .WillRepeatedly(Return(true));
  }

  MountError error = MOUNT_ERROR_NONE;
  Mount::MountArgs mount_args = GetDefaultMountArgs();
  mount_args.force_dircrypto = true;

  if (ShouldTestEcryptfs()) {
    // Should reject mounting ecryptfs vault.
    EXPECT_FALSE(mount_->MountCryptohome(user->username, FileSystemKeyset(),
                                         mount_args,
                                         /* is_pristine */ false, &error));
    EXPECT_EQ(MOUNT_ERROR_OLD_ENCRYPTION, error);
  } else {
    // Should succeed in mounting in dircrypto.
    EXPECT_TRUE(mount_->MountCryptohome(user->username, FileSystemKeyset(),
                                        mount_args,
                                        /* is_pristine */ false, &error));
    EXPECT_EQ(MOUNT_ERROR_NONE, error);
  }
}

namespace {
constexpr int kEphemeralVFSFragmentSize = 1 << 10;
constexpr int kEphemeralVFSSize = 1 << 12;

struct Attributes {
  mode_t mode;
  uid_t uid;
  gid_t gid;
};

constexpr char kEtc[] = "/etc";
constexpr char kEtcSkel[] = "/etc/skel";
constexpr char kEtcDaemonStore[] = "/etc/daemon-store";

constexpr char kRun[] = "/run";
constexpr char kRunCryptohome[] = "/run/cryptohome";
constexpr char kRunDaemonStore[] = "/run/daemon-store";

constexpr char kHome[] = "/home";
constexpr char kHomeChronos[] = "/home/chronos";
constexpr char kHomeChronosUser[] = "/home/chronos/user";
constexpr char kHomeUser[] = "/home/user";
constexpr char kHomeRoot[] = "/home/root";

constexpr char kDir1[] = "dir1";
constexpr char kFile1[] = "file1";
constexpr char kDir1File2[] = "dir1/file2";
constexpr char kDir1Dir2[] = "dir1/dir2";
constexpr char kDir1Dir2File3[] = "dir1/dir2/file3";

constexpr char kFile1Content[] = "content1";
constexpr char kDir1File2Content[] = "content2";
constexpr char kDir1Dir2File3Content[] = "content3";

constexpr char kSomeDaemon[] = "some_daemon";
constexpr Attributes kSomeDaemonAttributes{01735, 12, 27};
constexpr char kAnotherDaemon[] = "another_daemon";
constexpr Attributes kAnotherDaemonAttributes{0600, 0, 0};

constexpr char kDevLoop0[] = "/dev/loop0";

constexpr char kUser[] = "someuser";

// TODO(dlunev): fix mount code to not depend on a fixed gid value.
constexpr gid_t kDaemonStoreGid = 400;

base::FilePath ChronosHashPath(const std::string& username) {
  const std::string obfuscated_username =
      brillo::cryptohome::home::SanitizeUserName(username);
  return base::FilePath(kHomeChronos)
      .Append(base::StringPrintf("u-%s", obfuscated_username.c_str()));
}

void PrepareDirectoryStructure(Platform* platform) {
  // Create environment as defined in
  // src/platform2/cryptohome/tmpfiles.d/cryptohome.conf
  ASSERT_TRUE(platform->SafeCreateDirAndSetOwnershipAndPermissions(
      base::FilePath(kRun), 0755, fake_platform::kRootUID,
      fake_platform::kRootGID));
  ASSERT_TRUE(platform->SafeCreateDirAndSetOwnershipAndPermissions(
      base::FilePath(kRunCryptohome), 0700, fake_platform::kRootUID,
      fake_platform::kRootGID));
  ASSERT_TRUE(platform->SafeCreateDirAndSetOwnershipAndPermissions(
      base::FilePath(kRunDaemonStore), 0755, fake_platform::kRootUID,
      fake_platform::kRootGID));
  ASSERT_TRUE(platform->SafeCreateDirAndSetOwnershipAndPermissions(
      base::FilePath(kHome), 0755, fake_platform::kRootUID,
      fake_platform::kRootGID));
  ASSERT_TRUE(platform->SafeCreateDirAndSetOwnershipAndPermissions(
      base::FilePath(kHomeChronos), 0755, fake_platform::kChronosUID,
      fake_platform::kChronosGID));
  ASSERT_TRUE(platform->SafeCreateDirAndSetOwnershipAndPermissions(
      base::FilePath(kHomeChronosUser), 01755, fake_platform::kChronosUID,
      fake_platform::kChronosGID));
  ASSERT_TRUE(platform->SafeCreateDirAndSetOwnershipAndPermissions(
      base::FilePath(kHomeUser), 0755, fake_platform::kRootUID,
      fake_platform::kRootGID));
  ASSERT_TRUE(platform->SafeCreateDirAndSetOwnershipAndPermissions(
      base::FilePath(kHomeRoot), 01751, fake_platform::kRootUID,
      fake_platform::kRootGID));

  // Setup some skel directories to make sure they are copied over.
  // TODO(dlunev): for now setting permissions is useless, for the code
  // relies on Copy to copy it over for files, meaning we can't intercept it.
  // It can be fixed by setting permissions explicitly in RecursiveCopy.
  ASSERT_TRUE(platform->CreateDirectory(base::FilePath(kEtc)));
  ASSERT_TRUE(platform->CreateDirectory(base::FilePath(kEtcSkel)));
  ASSERT_TRUE(
      platform->CreateDirectory(base::FilePath(kEtcSkel).Append(kDir1)));
  ASSERT_TRUE(platform->WriteStringToFile(
      base::FilePath(kEtcSkel).Append(kFile1), kFile1Content));
  ASSERT_TRUE(platform->WriteStringToFile(
      base::FilePath(kEtcSkel).Append(kDir1File2), kDir1File2Content));
  ASSERT_TRUE(
      platform->CreateDirectory(base::FilePath(kEtcSkel).Append(kDir1Dir2)));
  ASSERT_TRUE(platform->WriteStringToFile(
      base::FilePath(kEtcSkel).Append(kDir1Dir2File3), kDir1Dir2File3Content));

  // Setup daemon-store templates
  ASSERT_TRUE(platform->CreateDirectory(base::FilePath(kEtcDaemonStore)));
  ASSERT_TRUE(platform->SafeCreateDirAndSetOwnershipAndPermissions(
      base::FilePath(kEtcDaemonStore).Append(kSomeDaemon),
      kSomeDaemonAttributes.mode, kSomeDaemonAttributes.uid,
      kSomeDaemonAttributes.gid));
  ASSERT_TRUE(platform->SafeCreateDirAndSetOwnershipAndPermissions(
      base::FilePath(kEtcDaemonStore).Append(kAnotherDaemon),
      kAnotherDaemonAttributes.mode, kAnotherDaemonAttributes.uid,
      kAnotherDaemonAttributes.gid));
  ASSERT_TRUE(platform->CreateDirectory(
      base::FilePath(kRunDaemonStore).Append(kSomeDaemon)));
  ASSERT_TRUE(platform->CreateDirectory(
      base::FilePath(kRunDaemonStore).Append(kAnotherDaemon)));
}

void CheckExistanceAndPermissions(Platform* platform,
                                  const base::FilePath& path,
                                  mode_t expected_mode,
                                  uid_t expected_uid,
                                  gid_t expected_gid,
                                  bool expect_present) {
  ASSERT_THAT(platform->FileExists(path), expect_present)
      << "PATH: " << path.value();

  if (!expect_present) {
    return;
  }

  mode_t mode;
  uid_t uid;
  gid_t gid;

  ASSERT_THAT(platform->GetOwnership(path, &uid, &gid, false), true)
      << "PATH: " << path.value();
  ASSERT_THAT(platform->GetPermissions(path, &mode), true)
      << "PATH: " << path.value();

  ASSERT_THAT(mode, expected_mode) << "PATH: " << path.value();
  ASSERT_THAT(uid, expected_uid) << "PATH: " << path.value();
  ASSERT_THAT(gid, expected_gid) << "PATH: " << path.value();
}

void CheckRootAndDaemonStoreMounts(Platform* platform,
                                   const std::string& username,
                                   const base::FilePath& vault_mount_point,
                                   bool expect_present) {
  const std::string obfuscated_username =
      brillo::cryptohome::home::SanitizeUserName(username);
  const std::multimap<const base::FilePath, const base::FilePath>
      expected_root_mount_map{
          {
              vault_mount_point.Append("root"),
              brillo::cryptohome::home::GetRootPath(username),
          },
          {
              vault_mount_point.Append("root").Append(kSomeDaemon),
              base::FilePath(kRunDaemonStore)
                  .Append(kSomeDaemon)
                  .Append(obfuscated_username),
          },
          {
              vault_mount_point.Append("root").Append(kAnotherDaemon),
              base::FilePath(kRunDaemonStore)
                  .Append(kAnotherDaemon)
                  .Append(obfuscated_username),
          },
      };
  std::multimap<const base::FilePath, const base::FilePath> root_mount_map;

  ASSERT_THAT(platform->IsDirectoryMounted(
                  brillo::cryptohome::home::GetRootPath(username)),
              expect_present);
  if (expect_present) {
    ASSERT_TRUE(platform->GetMountsBySourcePrefix(
        vault_mount_point.Append("root"), &root_mount_map));
    ASSERT_THAT(root_mount_map,
                ::testing::UnorderedElementsAreArray(expected_root_mount_map));
  }
  CheckExistanceAndPermissions(platform, vault_mount_point.Append("root"),
                               01770, fake_platform::kRootUID, kDaemonStoreGid,
                               expect_present);
  CheckExistanceAndPermissions(
      platform, vault_mount_point.Append("root").Append(kSomeDaemon),
      kSomeDaemonAttributes.mode, kSomeDaemonAttributes.uid,
      kSomeDaemonAttributes.gid, expect_present);
  CheckExistanceAndPermissions(
      platform, vault_mount_point.Append("root").Append(kAnotherDaemon),
      kAnotherDaemonAttributes.mode, kAnotherDaemonAttributes.uid,
      kAnotherDaemonAttributes.gid, expect_present);

  if (expect_present) {
    // TODO(dlunev): make this directories to go away on unmount.
    ASSERT_THAT(platform->DirectoryExists(base::FilePath(kRunDaemonStore)
                                              .Append(kSomeDaemon)
                                              .Append(obfuscated_username)),
                expect_present);
    ASSERT_THAT(platform->DirectoryExists(base::FilePath(kRunDaemonStore)
                                              .Append(kAnotherDaemon)
                                              .Append(obfuscated_username)),
                expect_present);
    CheckExistanceAndPermissions(
        platform, brillo::cryptohome::home::GetRootPath(username), 01770,
        fake_platform::kRootUID, kDaemonStoreGid, expect_present);
  }
}

void CheckUserMountPoints(Platform* platform,
                          const std::string& username,
                          const base::FilePath& vault_mount_point,
                          bool expect_present) {
  const base::FilePath chronos_hash_user_mount_point =
      ChronosHashPath(username);

  const std::multimap<const base::FilePath, const base::FilePath>
      expected_user_mount_map{
          {vault_mount_point.Append("user"), vault_mount_point.Append("user")},
          {vault_mount_point.Append("user"),
           brillo::cryptohome::home::GetUserPath(username)},
          {vault_mount_point.Append("user"), chronos_hash_user_mount_point},
          {vault_mount_point.Append("user"), base::FilePath(kHomeChronosUser)},
          {vault_mount_point.Append("user").Append(kDownloadsDir),
           vault_mount_point.Append("user")
               .Append(kMyFilesDir)
               .Append(kDownloadsDir)},
      };
  std::multimap<const base::FilePath, const base::FilePath> user_mount_map;

  ASSERT_THAT(platform->IsDirectoryMounted(base::FilePath(kHomeChronosUser)),
              expect_present);
  ASSERT_THAT(platform->IsDirectoryMounted(
                  brillo::cryptohome::home::GetUserPath(username)),
              expect_present);
  ASSERT_THAT(platform->IsDirectoryMounted(chronos_hash_user_mount_point),
              expect_present);
  ASSERT_THAT(platform->IsDirectoryMounted(vault_mount_point.Append("user")
                                               .Append(kMyFilesDir)
                                               .Append(kDownloadsDir)),
              expect_present);
  if (expect_present) {
    ASSERT_TRUE(platform->GetMountsBySourcePrefix(
        vault_mount_point.Append("user"), &user_mount_map));
    ASSERT_THAT(user_mount_map,
                ::testing::UnorderedElementsAreArray(expected_user_mount_map));
  }
}

void CheckUserMountPaths(Platform* platform,
                         const base::FilePath& base_path,
                         bool expect_present) {
  // The path itself.
  // TODO(dlunev): the mount paths should be cleaned up upon unmount.
  if (expect_present) {
    CheckExistanceAndPermissions(platform, base_path, 0750,
                                 fake_platform::kChronosUID,
                                 fake_platform::kSharedGID, expect_present);
  }

  // Subdirectories
  CheckExistanceAndPermissions(platform, base_path.Append(kDownloadsDir), 0750,
                               fake_platform::kChronosUID,
                               fake_platform::kSharedGID, expect_present);

  CheckExistanceAndPermissions(platform, base_path.Append(kMyFilesDir), 0750,
                               fake_platform::kChronosUID,
                               fake_platform::kSharedGID, expect_present);

  CheckExistanceAndPermissions(
      platform, base_path.Append(kMyFilesDir).Append(kDownloadsDir), 0750,
      fake_platform::kChronosUID, fake_platform::kSharedGID, expect_present);

  CheckExistanceAndPermissions(platform, base_path.Append(kCacheDir), 0700,
                               fake_platform::kChronosUID,
                               fake_platform::kChronosGID, expect_present);

  CheckExistanceAndPermissions(platform, base_path.Append(kGCacheDir), 0750,
                               fake_platform::kChronosUID,
                               fake_platform::kSharedGID, expect_present);

  CheckExistanceAndPermissions(
      platform, base_path.Append(kGCacheDir).Append(kGCacheVersion2Dir), 0770,
      fake_platform::kChronosUID, fake_platform::kSharedGID, expect_present);
}

void CheckSkel(Platform* platform,
               const base::FilePath& base_path,
               bool expect_present) {
  // Presence
  // TODO(dlunev) unfortunately we can not verify if Copy correctly deals with
  // the attributes, because it actually deals with those at the point where
  // we can not intercept it. We can make that explicit by setting those in
  // the copy skel itself.
  CheckExistanceAndPermissions(platform, base_path.Append(kDir1), 0750,
                               fake_platform::kChronosUID,
                               fake_platform::kChronosGID, expect_present);
  CheckExistanceAndPermissions(
      platform, base_path.Append(kFile1),
      0750,  // NOT A PART OF THE CONTRACT, SEE TODO ABOVE.
      fake_platform::kChronosUID, fake_platform::kChronosGID, expect_present);
  CheckExistanceAndPermissions(platform, base_path.Append(kDir1Dir2), 0750,
                               fake_platform::kChronosUID,
                               fake_platform::kChronosGID, expect_present);
  CheckExistanceAndPermissions(
      platform, base_path.Append(kDir1File2),
      0750,  // NOT A PART OF THE CONTRACT, SEE TODO ABOVE.
      fake_platform::kChronosUID, fake_platform::kChronosGID, expect_present);
  CheckExistanceAndPermissions(
      platform, base_path.Append(kDir1Dir2File3),
      0750,  // NOT A PART OF THE CONTRACT, SEE TODO ABOVE.
      fake_platform::kChronosUID, fake_platform::kChronosGID, expect_present);

  // Content
  if (expect_present) {
    std::string result;
    ASSERT_TRUE(platform->ReadFileToString(base_path.Append(kFile1), &result));
    ASSERT_THAT(result, kFile1Content);
    ASSERT_TRUE(
        platform->ReadFileToString(base_path.Append(kDir1File2), &result));
    ASSERT_THAT(result, kDir1File2Content);
    ASSERT_TRUE(
        platform->ReadFileToString(base_path.Append(kDir1Dir2File3), &result));
    ASSERT_THAT(result, kDir1Dir2File3Content);
  }
}

}  // namespace

class EphemeralSystemTest : public ::testing::Test {
 public:
  EphemeralSystemTest() : crypto_(&platform_) {}

  void SetUp() {
    ASSERT_NO_FATAL_FAILURE(PrepareDirectoryStructure(&platform_));
    InitializeFilesystemLayout(&platform_, &crypto_, &system_salt_);
    platform_.GetFake()->SetSystemSaltForLibbrillo(system_salt_);
    platform_.GetFake()->SetStandardUsersAndGroups();

    std::unique_ptr<EncryptedContainerFactory> container_factory =
        std::make_unique<EncryptedContainerFactory>(
            &platform_, std::make_unique<FakeBackingDeviceFactory>(&platform_));
    homedirs_ = std::make_unique<HomeDirs>(
        &platform_, system_salt_, std::make_unique<policy::PolicyProvider>(),
        base::BindRepeating([](const std::string& unused) {}),
        std::make_unique<CryptohomeVaultFactory>(&platform_,
                                                 std::move(container_factory)));

    mount_ = new Mount(&platform_, homedirs_.get());

    EXPECT_TRUE(mount_->Init(/*use_init_namespace=*/true));

    SetupVFSMock();
  }

  void TearDown() { platform_.GetFake()->RemoveSystemSaltForLibbrillo(); }

 protected:
  // Protected for trivial access.
  NiceMock<MockPlatform> platform_;
  Crypto crypto_;
  brillo::SecureBlob system_salt_;
  std::unique_ptr<HomeDirs> homedirs_;
  scoped_refptr<Mount> mount_;
  struct statvfs ephemeral_statvfs_;

  base::FilePath EphemeralBackingFile(const std::string& username) {
    const std::string obfuscated_username =
        brillo::cryptohome::home::SanitizeUserName(username);
    return base::FilePath(kEphemeralCryptohomeDir)
        .Append(kSparseFileDir)
        .Append(obfuscated_username);
  }

  base::FilePath EphemeralMountPoint(const std::string& username) {
    const std::string obfuscated_username =
        brillo::cryptohome::home::SanitizeUserName(username);
    return base::FilePath(kEphemeralCryptohomeDir)
        .Append(kEphemeralMountDir)
        .Append(obfuscated_username);
  }

  void VerifyFS(const std::string& username,
                const base::FilePath& loop_dev,
                bool expect_present) {
    CheckLoopDev(username, loop_dev, expect_present);
    ASSERT_NO_FATAL_FAILURE(CheckRootAndDaemonStoreMounts(
        &platform_, username, EphemeralMountPoint(username), expect_present));
    ASSERT_NO_FATAL_FAILURE(CheckUserMountPoints(
        &platform_, username, EphemeralMountPoint(username), expect_present));

    const std::vector<base::FilePath> user_vault_and_mounts{
        EphemeralMountPoint(username).Append("user"),
        base::FilePath(kHomeChronosUser),
        brillo::cryptohome::home::GetUserPath(username),
        ChronosHashPath(username),
    };

    for (const auto& base_path : user_vault_and_mounts) {
      ASSERT_NO_FATAL_FAILURE(
          CheckUserMountPaths(&platform_, base_path, expect_present));
      ASSERT_NO_FATAL_FAILURE(CheckSkel(&platform_, base_path, expect_present));
    }
  }

 private:
  void CheckLoopDev(const std::string& username,
                    const base::FilePath& loop_dev,
                    bool expect_present) {
    const base::FilePath ephemeral_backing_file =
        EphemeralBackingFile(username);
    const base::FilePath ephemeral_mount_point = EphemeralMountPoint(username);
    const std::multimap<const base::FilePath, const base::FilePath>
        expected_ephemeral_mount_map{
            {loop_dev, ephemeral_mount_point},
        };
    std::multimap<const base::FilePath, const base::FilePath>
        ephemeral_mount_map;

    ASSERT_THAT(platform_.FileExists(ephemeral_backing_file), expect_present);
    ASSERT_THAT(platform_.FileExists(loop_dev), expect_present);
    ASSERT_THAT(platform_.DirectoryExists(ephemeral_mount_point),
                expect_present);
    ASSERT_THAT(platform_.IsDirectoryMounted(ephemeral_mount_point),
                expect_present);
    if (expect_present) {
      ASSERT_TRUE(
          platform_.GetMountsBySourcePrefix(loop_dev, &ephemeral_mount_map));
      ASSERT_THAT(ephemeral_mount_map, ::testing::UnorderedElementsAreArray(
                                           expected_ephemeral_mount_map));
    }
  }

  void SetupVFSMock() {
    ephemeral_statvfs_ = {0};
    ephemeral_statvfs_.f_frsize = kEphemeralVFSFragmentSize;
    ephemeral_statvfs_.f_blocks = kEphemeralVFSSize / kEphemeralVFSFragmentSize;

    ON_CALL(platform_, StatVFS(base::FilePath(kEphemeralCryptohomeDir), _))
        .WillByDefault(
            DoAll(SetArgPointee<1>(ephemeral_statvfs_), Return(true)));
  }
};

namespace {

TEST_F(EphemeralSystemTest, EphemeralMount) {
  EXPECT_CALL(platform_, FormatExt4(EphemeralBackingFile(kUser), _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, SetSELinuxContext(EphemeralMountPoint(kUser), _))
      .WillOnce(Return(true));

  ASSERT_THAT(mount_->MountEphemeralCryptohome(kUser), MOUNT_ERROR_NONE);

  VerifyFS(kUser, base::FilePath(kDevLoop0), /*expect_present=*/true);

  ASSERT_TRUE(mount_->UnmountCryptohome());

  VerifyFS(kUser, base::FilePath(kDevLoop0), /*expect_present=*/false);
}

TEST_F(EphemeralSystemTest, EpmeneralMount_VFSFailure) {
  // Checks the case when ephemeral statvfs call fails.
  ON_CALL(platform_, StatVFS(base::FilePath(kEphemeralCryptohomeDir), _))
      .WillByDefault(Return(false));

  ASSERT_THAT(mount_->MountEphemeralCryptohome(kUser), MOUNT_ERROR_FATAL);

  VerifyFS(kUser, base::FilePath(kDevLoop0), /*expect_present=*/false);
}

TEST_F(EphemeralSystemTest, EphemeralMount_CreateSparseDirFailure) {
  // Checks the case when directory for ephemeral sparse files fails to be
  // created.
  EXPECT_CALL(platform_, CreateDirectory(EphemeralBackingFile(kUser).DirName()))
      .WillOnce(Return(false));

  ASSERT_THAT(mount_->MountEphemeralCryptohome(kUser), MOUNT_ERROR_FATAL);

  VerifyFS(kUser, base::FilePath(kDevLoop0), /*expect_present=*/false);
}

TEST_F(EphemeralSystemTest, EphemeralMount_CreateSparseFailure) {
  // Checks the case when ephemeral sparse file fails to create.
  EXPECT_CALL(platform_, CreateSparseFile(EphemeralBackingFile(kUser), _))
      .WillOnce(Return(false));

  ASSERT_THAT(mount_->MountEphemeralCryptohome(kUser), MOUNT_ERROR_FATAL);

  VerifyFS(kUser, base::FilePath(kDevLoop0), /*expect_present=*/false);
}

TEST_F(EphemeralSystemTest, EphemeralMount_FormatFailure) {
  // Checks that when ephemeral loop device fails to be formatted, clean up
  // happens appropriately.
  EXPECT_CALL(platform_, FormatExt4(EphemeralBackingFile(kUser), _, _))
      .WillOnce(Return(false));

  ASSERT_THAT(mount_->MountEphemeralCryptohome(kUser), MOUNT_ERROR_FATAL);

  VerifyFS(kUser, base::FilePath(kDevLoop0), /*expect_present=*/false);
}

TEST_F(EphemeralSystemTest, EphemeralMount_AttachLoopFailure) {
  // Checks that when ephemeral loop device fails to attach, clean up happens
  // appropriately.
  EXPECT_CALL(platform_, FormatExt4(EphemeralBackingFile(kUser), _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, AttachLoop(EphemeralBackingFile(kUser)))
      .WillOnce(Return(FilePath()));

  ASSERT_THAT(mount_->MountEphemeralCryptohome(kUser), MOUNT_ERROR_FATAL);

  VerifyFS(kUser, base::FilePath(kDevLoop0), /*expect_present=*/false);
}

TEST_F(EphemeralSystemTest, EphemeralMount_EnsureUserMountFailure) {
  // Checks that when ephemeral mount fails to ensure mount points, clean up
  // happens appropriately.
  EXPECT_CALL(platform_, FormatExt4(EphemeralBackingFile(kUser), _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, Mount(base::FilePath(kDevLoop0),
                               EphemeralMountPoint(kUser), _, _, _))
      .WillOnce(Return(false));

  ASSERT_THAT(mount_->MountEphemeralCryptohome(kUser), MOUNT_ERROR_FATAL);

  // TODO(dlunev): some directories cleanup is missing in this case. Fix it and
  // uncomment the following.
  // VerifyFS(kUser, "/dev/loop0", /*expect_present=*/false);
}

}  // namespace

// A fixture for testing chaps directory checks.
class ChapsDirectoryTest : public ::testing::Test {
 public:
  ChapsDirectoryTest() : kBaseDir("/base_chaps_dir") {
    crypto_.set_platform(&platform_);
    platform_.GetFake()->SetStandardUsersAndGroups();

    brillo::SecureBlob salt;
    InitializeFilesystemLayout(&platform_, &crypto_, &salt);
    keyset_management_ = std::make_unique<KeysetManagement>(
        &platform_, &crypto_, salt, nullptr, nullptr);
    HomeDirs::RemoveCallback remove_cb;
    homedirs_ =
        std::make_unique<HomeDirs>(&platform_, salt, nullptr, remove_cb);

    mount_ = new Mount(&platform_, homedirs_.get());
    mount_->Init(/*use_init_namespace=*/true);
    mount_->chaps_user_ = fake_platform::kChapsUID;
    mount_->default_access_group_ = fake_platform::kSharedGID;
    // By default, set stats to the expected values.
    InitStat(&base_stat_, 040750, fake_platform::kChapsUID,
             fake_platform::kSharedGID);
  }
  ChapsDirectoryTest(const ChapsDirectoryTest&) = delete;
  ChapsDirectoryTest& operator=(const ChapsDirectoryTest&) = delete;

  virtual ~ChapsDirectoryTest() {}

  void SetupFakeChapsDirectory() {
    // Configure the base directory.
    EXPECT_CALL(platform_, DirectoryExists(kBaseDir))
        .WillRepeatedly(Return(true));
    EXPECT_CALL(platform_, Stat(kBaseDir, _))
        .WillRepeatedly(DoAll(SetArgPointee<1>(base_stat_), Return(true)));
  }

  bool RunCheck() { return mount_->SetupChapsDirectory(kBaseDir); }

 protected:
  const FilePath kBaseDir;

  base::stat_wrapper_t base_stat_;

  scoped_refptr<Mount> mount_;
  NiceMock<MockPlatform> platform_;
  NiceMock<MockCrypto> crypto_;
  std::unique_ptr<KeysetManagement> keyset_management_;
  std::unique_ptr<HomeDirs> homedirs_;

 private:
  void InitStat(base::stat_wrapper_t* s, mode_t mode, uid_t uid, gid_t gid) {
    memset(s, 0, sizeof(base::stat_wrapper_t));
    s->st_mode = mode;
    s->st_uid = uid;
    s->st_gid = gid;
  }
};

TEST_F(ChapsDirectoryTest, DirectoryOK) {
  SetupFakeChapsDirectory();
  ASSERT_TRUE(RunCheck());
}

TEST_F(ChapsDirectoryTest, DirectoryDoesNotExist) {
  // Specify directory does not exist.
  EXPECT_CALL(platform_, DirectoryExists(kBaseDir))
      .WillRepeatedly(Return(false));
  // Expect basic setup.
  EXPECT_CALL(platform_, SafeCreateDirAndSetOwnershipAndPermissions(
                             kBaseDir, 0750, fake_platform::kChapsUID,
                             fake_platform::kSharedGID))
      .WillRepeatedly(Return(true));
  ASSERT_TRUE(RunCheck());
}

TEST_F(ChapsDirectoryTest, CreateFailure) {
  // Specify directory does not exist.
  EXPECT_CALL(platform_, DirectoryExists(kBaseDir))
      .WillRepeatedly(Return(false));
  // Expect basic setup but fail.
  EXPECT_CALL(platform_, SafeCreateDirAndSetOwnershipAndPermissions(
                             kBaseDir, 0750, fake_platform::kChapsUID,
                             fake_platform::kSharedGID))
      .WillRepeatedly(Return(false));
  ASSERT_FALSE(RunCheck());
}

}  // namespace cryptohome
