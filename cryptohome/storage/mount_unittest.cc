// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Unit tests for Mount.

#include "cryptohome/storage/mount.h"

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
#include <policy/mock_device_policy.h>

#include "cryptohome/cleanup/user_oldest_activity_timestamp_cache.h"
#include "cryptohome/crypto.h"
#include "cryptohome/cryptohome_common.h"
#include "cryptohome/filesystem_layout.h"
#include "cryptohome/make_tests.h"
#include "cryptohome/mock_chaps_client_factory.h"
#include "cryptohome/mock_crypto.h"
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
using ::testing::AnyNumber;
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

ACTION_P2(SetOwner, owner_known, owner) {
  if (owner_known)
    *arg0 = owner;
  return owner_known;
}

ACTION_P(SetEphemeralUsersEnabled, ephemeral_users_enabled) {
  *arg0 = ephemeral_users_enabled;
  return true;
}

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

    mock_device_policy_ = new policy::MockDevicePolicy();

    InitializeFilesystemLayout(&platform_, &crypto_, nullptr);
    keyset_management_ = std::make_unique<KeysetManagement>(
        &platform_, &crypto_, helper_.system_salt, nullptr);

    std::unique_ptr<EncryptedContainerFactory> container_factory =
        std::make_unique<EncryptedContainerFactory>(
            &platform_, std::make_unique<FakeBackingDeviceFactory>(&platform_));

    homedirs_ = std::make_unique<HomeDirs>(
        &platform_, keyset_management_.get(), helper_.system_salt, nullptr,
        std::make_unique<policy::PolicyProvider>(
            std::unique_ptr<policy::MockDevicePolicy>(mock_device_policy_)),
        std::make_unique<CryptohomeVaultFactory>(&platform_,
                                                 std::move(container_factory)));

    platform_.GetFake()->SetStandardUsersAndGroups();

    mount_ = new Mount(&platform_, homedirs_.get());

    mount_->set_chaps_client_factory(&chaps_client_factory_);
    // Perform mounts in-process.
    mount_->set_mount_guest_session_out_of_process(false);
    mount_->set_mount_ephemeral_session_out_of_process(false);
    mount_->set_mount_non_ephemeral_session_out_of_process(false);
    mount_->set_mount_guest_session_non_root_namespace(false);
    set_policy(false, "", false);
  }

  void TearDown() {
    mount_ = nullptr;
    helper_.TearDownSystemSalt();
  }

  void InsertTestUsers(const TestUserInfo* user_info_list, int count) {
    helper_.InitTestData(user_info_list, static_cast<size_t>(count),
                         ShouldTestEcryptfs());
  }

  bool DoMountInit() { return mount_->Init(); }

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

  void set_policy(bool owner_known,
                  const std::string& owner,
                  bool ephemeral_users_enabled) {
    EXPECT_CALL(*mock_device_policy_, LoadPolicy())
        .Times(AnyNumber())
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*mock_device_policy_, GetOwner(_))
        .WillRepeatedly(SetOwner(owner_known, owner));
    EXPECT_CALL(*mock_device_policy_, GetEphemeralUsersEnabled(_))
        .WillRepeatedly(SetEphemeralUsersEnabled(ephemeral_users_enabled));
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
      EXPECT_CALL(platform_, Mount(user.vault_path, user.vault_mount_path,
                                   "ecryptfs", kDefaultMountFlags, _))
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
      EXPECT_CALL(platform_, Mount(user.vault_path, user.vault_mount_path,
                                   "ecryptfs", kDefaultMountFlags, _))
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

  void ExpectDownloadsUnmounts(const TestUser& user, bool ephemeral_mount) {
    // Unmounting Downloads to MyFiles/Downloads in user home directory.
    const FilePath user_home = ephemeral_mount ? user.user_ephemeral_mount_path
                                               : user.user_vault_mount_path;

    EXPECT_CALL(platform_,
                Unmount(user_home.Append("MyFiles").Append("Downloads"), _, _))
        .WillOnce(Return(true));
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

  void ExpectEphemeralCryptohomeMount(const TestUser& user) {
    EXPECT_CALL(platform_, StatVFS(FilePath(kEphemeralCryptohomeDir), _))
        .WillOnce(Return(true));
    const FilePath ephemeral_filename =
        MountHelper::GetEphemeralSparseFile(user.obfuscated_username);
    EXPECT_CALL(platform_, CreateSparseFile(ephemeral_filename, _))
        .WillOnce(Return(true));
    EXPECT_CALL(platform_, AttachLoop(ephemeral_filename))
        .WillOnce(Return(kLoopDevice));
    EXPECT_CALL(platform_,
                FormatExt4(ephemeral_filename, kDefaultExt4FormatOpts, 0))
        .WillOnce(Return(true));

    EXPECT_CALL(platform_, Mount(kLoopDevice, _, kEphemeralMountType,
                                 kDefaultMountFlags, _))
        .WillRepeatedly(Return(true));
    EXPECT_CALL(platform_,
                SetSELinuxContext(Property(&FilePath::value,
                                           StartsWith(kEphemeralCryptohomeDir)),
                                  cryptohome::kEphemeralCryptohomeRootContext))
        .WillOnce(Return(true));
    EXPECT_CALL(platform_, Bind(_, _, _, _)).WillRepeatedly(Return(true));

    EXPECT_CALL(platform_, GetFileEnumerator(SkelDir(), _, _))
        .WillOnce(Return(new NiceMock<MockFileEnumerator>()))
        .WillOnce(Return(new NiceMock<MockFileEnumerator>()));
    EXPECT_CALL(
        platform_,
        GetFileEnumerator(
            Property(&FilePath::value, EndsWith("MyFiles/Downloads")), _, _))
        .WillOnce(Return(new NiceMock<MockFileEnumerator>()));
    EXPECT_CALL(platform_, DirectoryExists(_)).WillRepeatedly(Return(true));
    EXPECT_CALL(platform_, CreateDirectory(user.vault_path)).Times(0);
    EXPECT_CALL(platform_, FileExists(_)).WillRepeatedly(Return(true));
    EXPECT_CALL(platform_, CreateDirectory(_)).WillRepeatedly(Return(true));
    EXPECT_CALL(platform_, SafeCreateDirAndSetOwnership(_, _, _))
        .WillRepeatedly(Return(true));
    EXPECT_CALL(platform_,
                SafeCreateDirAndSetOwnershipAndPermissions(_, _, _, _))
        .WillRepeatedly(Return(true));
    ExpectDaemonStoreMounts(user, true /* ephemeral_mount */);
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

  void ExpectCryptohomeRemoval(const TestUser& user) {
    EXPECT_CALL(platform_, DeletePathRecursively(user.base_path)).Times(1);
    EXPECT_CALL(platform_, DeletePathRecursively(user.user_mount_path))
        .Times(1);
    EXPECT_CALL(platform_, DeletePathRecursively(user.root_mount_path))
        .Times(1);
  }

 protected:
  // Protected for trivial access.
  MakeTests helper_;
  NiceMock<MockPlatform> platform_;
  NiceMock<MockTpm> tpm_;
  Crypto crypto_;
  policy::MockDevicePolicy* mock_device_policy_;  // owned by homedirs_
  std::unique_ptr<KeysetManagement> keyset_management_;
  std::unique_ptr<HomeDirs> homedirs_;
  MockChapsClientFactory chaps_client_factory_;
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
  EXPECT_FALSE(mount_->Init());
}

TEST_P(MountTest, MountCryptohomeHasPrivileges) {
  // Check that Mount only works if the mount permission is given.
  InsertTestUsers(&kDefaultUsers[10], 1);
  EXPECT_CALL(platform_, DirectoryExists(ShadowRoot()))
      .WillRepeatedly(Return(true));
  EXPECT_TRUE(DoMountInit());

  TestUser* user = &helper_.users[0];
  user->key_data.set_label("my key!");
  user->use_key_data = true;
  // Regenerate the serialized vault keyset.
  user->GenerateCredentials(ShouldTestEcryptfs());
  // Let the legacy key iteration work here.

  user->InjectUserPaths(&platform_, fake_platform::kChronosUID,
                        fake_platform::kChronosGID, fake_platform::kSharedGID,
                        kDaemonGid, ShouldTestEcryptfs());

  ExpectCryptohomeMount(*user);
  EXPECT_CALL(platform_, ClearUserKeyring()).WillOnce(Return(true));
  EXPECT_CALL(platform_, FileExists(base::FilePath(kLockedToSingleUserFile)))
      .WillRepeatedly(Return(false));

  // user exists, so there'll be no skel copy after.

  MountError error = MOUNT_ERROR_NONE;
  ASSERT_TRUE(mount_->MountCryptohome(user->username, FileSystemKeyset(),
                                      GetDefaultMountArgs(),
                                      /* is_pristine */ false, &error));

  EXPECT_CALL(platform_, Unmount(_, _, _)).WillRepeatedly(Return(true));
  if (ShouldTestEcryptfs())
    EXPECT_CALL(platform_, ClearUserKeyring()).WillOnce(Return(true));
  EXPECT_TRUE(mount_->UnmountCryptohome());
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
                    kDefaultMountFlags, kDmcryptContainerMountOptions))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_,
              Mount(_, user->vault_cache_path, kDmcryptContainerMountType,
                    kDefaultMountFlags, kDmcryptContainerMountOptions))
      .WillOnce(Return(true));

  EXPECT_TRUE(mnt_helper.PerformMount(options, user->username, "foo", "bar",
                                      false, &error));
}

// A fixture for testing chaps directory checks.
class ChapsDirectoryTest : public ::testing::Test {
 public:
  ChapsDirectoryTest() : kBaseDir("/base_chaps_dir") {
    crypto_.set_platform(&platform_);
    platform_.GetFake()->SetStandardUsersAndGroups();

    brillo::SecureBlob salt;
    InitializeFilesystemLayout(&platform_, &crypto_, &salt);
    keyset_management_ =
        std::make_unique<KeysetManagement>(&platform_, &crypto_, salt, nullptr);
    homedirs_ = std::make_unique<HomeDirs>(&platform_, keyset_management_.get(),
                                           salt, nullptr, nullptr);

    mount_ = new Mount(&platform_, homedirs_.get());
    mount_->Init();
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

  bool RunCheck() { return mount_->CheckChapsDirectory(kBaseDir); }

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
    EXPECT_CALL(platform_, Mount(src, dest0, _, kDefaultMountFlags, _))
        .WillOnce(Return(true));
    EXPECT_CALL(platform_, Bind(src, dest1, _, true)).WillOnce(Return(true));
    EXPECT_CALL(platform_, Mount(src, dest2, _, kDefaultMountFlags, _))
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
                                 kDefaultMountFlags, _))
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

// Test setup that initially has no cryptohomes.
const TestUserInfo kNoUsers[] = {
    {"user0@invalid.domain", "zero", false},
    {"user1@invalid.domain", "odin", false},
    {"user2@invalid.domain", "dwaa", false},
    {"owner@invalid.domain", "1234", false},
};
const int kNoUserCount = base::size(kNoUsers);

// Test setup that initially has a cryptohome for the owner only.
const TestUserInfo kOwnerOnlyUsers[] = {
    {"user0@invalid.domain", "zero", false},
    {"user1@invalid.domain", "odin", false},
    {"user2@invalid.domain", "dwaa", false},
    {"owner@invalid.domain", "1234", true},
};
const int kOwnerOnlyUserCount = base::size(kOwnerOnlyUsers);

// Test setup that initially has cryptohomes for all users.
const TestUserInfo kAlternateUsers[] = {
    {"user0@invalid.domain", "zero", true},
    {"user1@invalid.domain", "odin", true},
    {"user2@invalid.domain", "dwaa", true},
    {"owner@invalid.domain", "1234", true},
};
const int kAlternateUserCount = base::size(kAlternateUsers);

class AltImageTest : public MountTest {
 public:
  AltImageTest() {}
  AltImageTest(const AltImageTest&) = delete;
  AltImageTest& operator=(const AltImageTest&) = delete;

  ~AltImageTest() { MountTest::TearDown(); }

  void SetUpAltImage(const TestUserInfo* users, int user_count) {
    // Set up fresh users.
    MountTest::SetUp();
    InsertTestUsers(users, user_count);

    EXPECT_CALL(platform_, DirectoryExists(ShadowRoot()))
        .WillRepeatedly(Return(true));
    EXPECT_TRUE(DoMountInit());
  }

  void PrepareHomedirs(bool inject_keyset,
                       const std::vector<int>* delete_vaults,
                       const std::vector<int>* mounted_vaults) {
    bool populate_vaults = (vaults_.size() == 0);
    // const string contents = "some encrypted contents";
    for (int user = 0; user != static_cast<int>(helper_.users.size()); user++) {
      // Let their Cache dirs be filled with some data.
      // Guarded to keep this function reusable.
      if (populate_vaults) {
        EXPECT_CALL(platform_,
                    DirectoryExists(Property(
                        &FilePath::value,
                        StartsWith(helper_.users[user].base_path.value()))))
            .WillRepeatedly(Return(true));
        vaults_.push_back(helper_.users[user].base_path);
      }
      bool delete_user = false;
      if (delete_vaults && delete_vaults->size() != 0) {
        if (std::find(delete_vaults->begin(), delete_vaults->end(), user) !=
            delete_vaults->end())
          delete_user = true;
      }
      bool mounted_user = false;
      if (mounted_vaults && mounted_vaults->size() != 0) {
        if (std::find(mounted_vaults->begin(), mounted_vaults->end(), user) !=
            mounted_vaults->end())
          mounted_user = true;
      }

      // After Cache & GCache are depleted. Users are deleted. To do so cleanly,
      // their keysets timestamps are read into an in-memory.
      if (inject_keyset && !mounted_user)
        helper_.users[user].InjectKeyset(&platform_, false);
      if (delete_user) {
        EXPECT_CALL(platform_,
                    DeletePathRecursively(helper_.users[user].base_path))
            .WillOnce(Return(true));
      }
    }
  }

  std::vector<FilePath> vaults_;
};

class EphemeralNoUserSystemTest : public AltImageTest {
 public:
  EphemeralNoUserSystemTest() {}
  EphemeralNoUserSystemTest(const EphemeralNoUserSystemTest&) = delete;
  EphemeralNoUserSystemTest& operator=(const EphemeralNoUserSystemTest&) =
      delete;

  void SetUp() { SetUpAltImage(kNoUsers, kNoUserCount); }
};

INSTANTIATE_TEST_SUITE_P(WithEcryptfs,
                         EphemeralNoUserSystemTest,
                         ::testing::Values(true));
INSTANTIATE_TEST_SUITE_P(WithDircrypto,
                         EphemeralNoUserSystemTest,
                         ::testing::Values(false));

TEST_P(EphemeralNoUserSystemTest, CreateMyFilesDownloads) {
  // Checks that MountHelper::SetUpEphemeralCryptohome creates
  // MyFiles/Downloads.
  const FilePath base_path("/ephemeral_home/");
  const FilePath user_home_path = base_path.Append("user");
  const FilePath downloads_path = user_home_path.Append("Downloads");
  const FilePath myfiles_path = user_home_path.Append("MyFiles");
  const FilePath myfiles_downloads_path = myfiles_path.Append("Downloads");
  const FilePath cache_path = user_home_path.Append("Cache");
  const FilePath gcache_path = user_home_path.Append("GCache");
  const FilePath gcache_v1_path = user_home_path.Append("GCache").Append("v1");
  const FilePath gcache_v2_path = user_home_path.Append("GCache").Append("v2");

  // Expecting Downloads to not exist and then be created.
  EXPECT_CALL(platform_, DirectoryExists(downloads_path))
      .WillOnce(Return(false))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SafeCreateDirAndSetOwnershipAndPermissions(
                             downloads_path, 0750, fake_platform::kChronosUID,
                             fake_platform::kSharedGID))
      .WillRepeatedly(Return(true));
  // Expecting MyFiles to not exist and then be created.
  EXPECT_CALL(platform_, DirectoryExists(myfiles_path))
      .WillOnce(Return(false))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SafeCreateDirAndSetOwnershipAndPermissions(
                             myfiles_path, 0750, fake_platform::kChronosUID,
                             fake_platform::kSharedGID))
      .WillRepeatedly(Return(true));
  // Expecting MyFiles/Downloads to not exist and then be created, with right
  // user and group.
  EXPECT_CALL(platform_, DirectoryExists(myfiles_downloads_path))
      .WillOnce(Return(false))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_,
              SafeCreateDirAndSetOwnershipAndPermissions(
                  myfiles_downloads_path, 0750, fake_platform::kChronosUID,
                  fake_platform::kSharedGID))
      .WillRepeatedly(Return(true));

  // Expect Cache to be created with the right user and group.
  EXPECT_CALL(platform_, DirectoryExists(cache_path))
      .WillOnce(Return(false))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SafeCreateDirAndSetOwnershipAndPermissions(
                             cache_path, 0700, fake_platform::kChronosUID,
                             fake_platform::kChronosGID))
      .WillRepeatedly(Return(true));

  // Expect GCache and Gcache/v2 to be created with the right user and group.
  EXPECT_CALL(platform_, DirectoryExists(gcache_path))
      .WillOnce(Return(false))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SafeCreateDirAndSetOwnershipAndPermissions(
                             gcache_path, 0750, fake_platform::kChronosUID,
                             fake_platform::kSharedGID))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, DirectoryExists(gcache_v2_path))
      .WillOnce(Return(false))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SafeCreateDirAndSetOwnershipAndPermissions(
                             gcache_v2_path, 0770, fake_platform::kChronosUID,
                             fake_platform::kSharedGID))
      .WillRepeatedly(Return(true));

  MountHelper mnt_helper(fake_platform::kChronosUID, fake_platform::kChronosGID,
                         fake_platform::kSharedGID, helper_.system_salt,
                         true /*legacy_mount*/, true /* bind_mount_downloads */,
                         &platform_);

  ASSERT_TRUE(mnt_helper.SetUpEphemeralCryptohome(base_path));
}

TEST_P(EphemeralNoUserSystemTest, CreateMyFilesDownloadsAlreadyExists) {
  // Checks that MountHelper::SetUpEphemeralCryptohome doesn't re-recreate if
  // already exists, just sets the ownership and group access for |base_path|.
  const FilePath base_path("/ephemeral_home/");
  const FilePath user_home_path = base_path.Append("user");
  const FilePath downloads_path = user_home_path.Append("Downloads");
  const FilePath myfiles_path = user_home_path.Append("MyFiles");
  const FilePath myfiles_downloads_path = myfiles_path.Append("Downloads");
  const FilePath cache_path = user_home_path.Append("Cache");
  const auto gcache_dirs = Property(
      &FilePath::value, StartsWith(user_home_path.Append("GCache").value()));

  // Expecting Downloads and MyFiles/Downloads to exist thus CreateDirectory
  // isn't called.
  EXPECT_CALL(platform_, DirectoryExists(AnyOf(
                             base_path, myfiles_path, downloads_path,
                             myfiles_downloads_path, cache_path, gcache_dirs)))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_,
              SetGroupAccessible(AnyOf(base_path, myfiles_path, downloads_path,
                                       myfiles_downloads_path, gcache_dirs),
                                 fake_platform::kSharedGID, _))
      .WillRepeatedly(Return(true));

  MountHelper mnt_helper(fake_platform::kChronosUID, fake_platform::kChronosGID,
                         fake_platform::kSharedGID, helper_.system_salt,
                         true /*legacy_mount*/, true /* bind_mount_downloads */,
                         &platform_);

  ASSERT_TRUE(mnt_helper.SetUpEphemeralCryptohome(base_path));
}

TEST_P(EphemeralNoUserSystemTest, OwnerUnknownMountCreateTest) {
  // Checks that when a device is not enterprise enrolled and does not have a
  // known owner, a regular vault is created and mounted.
  set_policy(false, "", true);

  TestUser* user = &helper_.users[0];

  EXPECT_CALL(platform_, FileExists(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, DirectoryExists(user->vault_path))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(platform_, DirectoryExists(user->vault_mount_path))
      .WillRepeatedly(Return(false));
  ExpectCryptohomeKeySetup(*user);

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
  EXPECT_CALL(platform_, WriteFileAtomicDurable(user->keyset_path, _, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, ReadFile(user->keyset_path, _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(user->credentials), Return(true)));
  EXPECT_CALL(platform_, DirectoryExists(Property(
                             &FilePath::value,
                             StartsWith(user->user_vault_mount_path.value()))))
      .WillRepeatedly(Return(true));

  EXPECT_CALL(platform_,
              Mount(_, _, kEphemeralMountType, kDefaultMountFlags, _))
      .Times(0);
  EXPECT_CALL(platform_, Mount(_, _, _, kDefaultMountFlags, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, Bind(_, _, _, _)).WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, IsDirectoryMounted(user->vault_mount_path))
      .WillOnce(Return(false));
  EXPECT_CALL(platform_, IsDirectoryMounted(FilePath("/home/chronos/user")))
      .WillOnce(Return(false));
  ExpectDownloadsBindMounts(*user, false /* ephemeral_mount */);
  ExpectDaemonStoreMounts(*user, false /* is_ephemeral */);

  EXPECT_CALL(platform_, GetFileEnumerator(SkelDir(), _, _))
      .WillOnce(Return(new NiceMock<MockFileEnumerator>()))
      .WillOnce(Return(new NiceMock<MockFileEnumerator>()));

  Mount::MountArgs mount_args = GetDefaultMountArgs();
  mount_args.create_if_missing = true;
  MountError error = MOUNT_ERROR_NONE;
  ASSERT_TRUE(mount_->MountCryptohome(user->username, FileSystemKeyset(),
                                      mount_args,
                                      /* is_pristine */ true, &error));

  // Unmount succeeds.
  ON_CALL(platform_, Unmount(_, _, _)).WillByDefault(Return(true));

  ASSERT_TRUE(mount_->UnmountCryptohome());
}

// TODO(wad) Duplicate these tests with multiple mounts instead of one.

TEST_P(EphemeralNoUserSystemTest, EnterpriseMountNoCreateTest) {
  // Checks that when a device is enterprise enrolled, a tmpfs cryptohome is
  // mounted and no regular vault is created.
  set_policy(false, "", true);
  homedirs_->set_enterprise_owned(true);
  TestUser* user = &helper_.users[0];

  EXPECT_CALL(platform_, Stat(_, _)).WillRepeatedly(Return(false));
  EXPECT_CALL(platform_, Unmount(_, _, _)).WillRepeatedly(Return(true));

  ExpectEphemeralCryptohomeMount(*user);

  ASSERT_EQ(MOUNT_ERROR_NONE, mount_->MountEphemeralCryptohome(user->username));

  // Detach succeeds.
  ON_CALL(platform_, DetachLoop(_)).WillByDefault(Return(true));
}

TEST_P(EphemeralNoUserSystemTest, OwnerUnknownMountIsEphemeralTest) {
  // Checks that when a device is not enterprise enrolled and does not have a
  // known owner, a mount request with the |ensure_ephemeral| flag set fails.
  TestUser* user = &helper_.users[0];

  EXPECT_CALL(platform_, Mount(_, _, _, kDefaultMountFlags, _)).Times(0);

  ASSERT_EQ(MOUNT_ERROR_EPHEMERAL_MOUNT_BY_OWNER,
            mount_->MountEphemeralCryptohome(user->username));
}

TEST_P(EphemeralNoUserSystemTest, EnterpriseMountIsEphemeralTest) {
  // Checks that when a device is enterprise enrolled, a mount request with the
  // |is_ephemeral| flag set causes a tmpfs cryptohome to be mounted and no
  // regular vault to be created.
  set_policy(true, "", false);
  homedirs_->set_enterprise_owned(true);
  TestUser* user = &helper_.users[0];

  // Always removes non-owner cryptohomes.
  std::vector<FilePath> empty;
  EXPECT_CALL(platform_, EnumerateDirectoryEntries(_, _, _))
      .WillRepeatedly(DoAll(SetArgPointee<2>(empty), Return(true)));

  EXPECT_CALL(platform_, Stat(_, _)).WillRepeatedly(Return(false));
  ExpectEphemeralCryptohomeMount(*user);

  ASSERT_EQ(MOUNT_ERROR_NONE, mount_->MountEphemeralCryptohome(user->username));

  EXPECT_CALL(platform_, DetachLoop(kLoopDevice)).WillOnce(Return(true));
  EXPECT_CALL(platform_,
              Unmount(user->ephemeral_mount_path.Append("user"), _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, Unmount(user->ephemeral_mount_path, _, _))
      .WillOnce(Return(true));

  EXPECT_CALL(
      platform_,
      Unmount(Property(&FilePath::value, StartsWith("/home/chronos/u-")), _, _))
      .WillOnce(Return(true));  // user mount
  EXPECT_CALL(
      platform_,
      Unmount(Property(&FilePath::value, StartsWith("/home/user/")), _, _))
      .WillOnce(Return(true));  // user mount
  EXPECT_CALL(
      platform_,
      Unmount(Property(&FilePath::value, StartsWith("/home/root/")), _, _))
      .WillOnce(Return(true));  // user mount
  EXPECT_CALL(platform_, Unmount(FilePath("/home/chronos/user"), _, _))
      .WillOnce(Return(true));  // legacy mount
  EXPECT_CALL(platform_, Unmount(Property(&FilePath::value,
                                          StartsWith(kRunDaemonStoreBaseDir)),
                                 _, _))
      .WillOnce(Return(true));  // daemon store mounts
  EXPECT_CALL(platform_, ClearUserKeyring()).WillRepeatedly(Return(true));

  ExpectDownloadsUnmounts(*user, true /* ephemeral_mount */);

  EXPECT_TRUE(mount_->UnmountCryptohome());
}

TEST_P(EphemeralNoUserSystemTest, EnterpriseMountStatVFSFailure) {
  // Checks the case when ephemeral statvfs call fails.
  set_policy(false, "", true);
  homedirs_->set_enterprise_owned(true);
  const TestUser* const user = &helper_.users[0];

  EXPECT_CALL(platform_, DetachLoop(_)).Times(0);
  ExpectCryptohomeRemoval(*user);

  EXPECT_CALL(platform_, StatVFS(FilePath(kEphemeralCryptohomeDir), _))
      .WillOnce(Return(false));

  ASSERT_EQ(MOUNT_ERROR_FATAL,
            mount_->MountEphemeralCryptohome(user->username));
}

TEST_P(EphemeralNoUserSystemTest, EnterpriseMountCreateSparseDirFailure) {
  // Checks the case when directory for ephemeral sparse files fails to be
  // created.
  set_policy(false, "", true);
  homedirs_->set_enterprise_owned(true);
  const TestUser* const user = &helper_.users[0];

  EXPECT_CALL(platform_, DetachLoop(_)).Times(0);
  ExpectCryptohomeRemoval(*user);

  EXPECT_CALL(platform_, StatVFS(FilePath(kEphemeralCryptohomeDir), _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, CreateDirectory(MountHelper::GetEphemeralSparseFile(
                                             user->obfuscated_username)
                                             .DirName()))
      .WillOnce(Return(false));

  ASSERT_EQ(MOUNT_ERROR_FATAL,
            mount_->MountEphemeralCryptohome(user->username));
}

TEST_P(EphemeralNoUserSystemTest, EnterpriseMountCreateSparseFailure) {
  // Checks the case when ephemeral sparse file fails to create.
  set_policy(false, "", true);
  homedirs_->set_enterprise_owned(true);
  const TestUser* const user = &helper_.users[0];
  const FilePath ephemeral_filename =
      MountHelper::GetEphemeralSparseFile(user->obfuscated_username);

  EXPECT_CALL(platform_, DetachLoop(_)).Times(0);
  EXPECT_CALL(platform_, DeleteFile(ephemeral_filename)).Times(1);
  ExpectCryptohomeRemoval(*user);

  EXPECT_CALL(platform_, StatVFS(FilePath(kEphemeralCryptohomeDir), _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, CreateDirectory(ephemeral_filename.DirName()))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, CreateSparseFile(ephemeral_filename, _))
      .WillOnce(Return(false));

  ASSERT_EQ(MOUNT_ERROR_FATAL,
            mount_->MountEphemeralCryptohome(user->username));
}

TEST_P(EphemeralNoUserSystemTest, EnterpriseMountAttachLoopFailure) {
  // Checks that when ephemeral loop device fails to attach, clean up happens
  // appropriately.
  set_policy(false, "", true);
  homedirs_->set_enterprise_owned(true);
  const TestUser* const user = &helper_.users[0];
  const FilePath ephemeral_filename =
      MountHelper::GetEphemeralSparseFile(user->obfuscated_username);

  EXPECT_CALL(platform_, DetachLoop(_)).Times(0);
  EXPECT_CALL(platform_, DeleteFile(ephemeral_filename)).Times(1);
  ExpectCryptohomeRemoval(*user);

  EXPECT_CALL(platform_, StatVFS(FilePath(kEphemeralCryptohomeDir), _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, CreateDirectory(ephemeral_filename.DirName()))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, CreateSparseFile(ephemeral_filename, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_,
              FormatExt4(ephemeral_filename, kDefaultExt4FormatOpts, 0))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, AttachLoop(ephemeral_filename))
      .WillOnce(Return(FilePath()));

  ASSERT_EQ(MOUNT_ERROR_FATAL,
            mount_->MountEphemeralCryptohome(user->username));
}

TEST_P(EphemeralNoUserSystemTest, EnterpriseMountFormatFailure) {
  // Checks that when ephemeral loop device fails to be formatted, clean up
  // happens appropriately.
  set_policy(false, "", true);
  homedirs_->set_enterprise_owned(true);
  const TestUser* const user = &helper_.users[0];
  const FilePath ephemeral_filename =
      MountHelper::GetEphemeralSparseFile(user->obfuscated_username);

  EXPECT_CALL(platform_, DetachLoop(_)).Times(0);
  EXPECT_CALL(platform_, DeleteFile(ephemeral_filename)).Times(1);
  ExpectCryptohomeRemoval(*user);

  EXPECT_CALL(platform_, StatVFS(FilePath(kEphemeralCryptohomeDir), _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, CreateDirectory(ephemeral_filename.DirName()))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, CreateSparseFile(ephemeral_filename, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_,
              FormatExt4(ephemeral_filename, kDefaultExt4FormatOpts, 0))
      .WillOnce(Return(false));

  ASSERT_EQ(MOUNT_ERROR_FATAL,
            mount_->MountEphemeralCryptohome(user->username));
}

TEST_P(EphemeralNoUserSystemTest, EnterpriseMountEnsureUserMountFailure) {
  // Checks that when ephemeral mount fails to ensure mount points, clean up
  // happens appropriately.
  set_policy(false, "", true);
  homedirs_->set_enterprise_owned(true);
  const TestUser* const user = &helper_.users[0];
  const FilePath ephemeral_filename =
      MountHelper::GetEphemeralSparseFile(user->obfuscated_username);

  EXPECT_CALL(platform_, DetachLoop(_)).Times(1);
  EXPECT_CALL(platform_, DeleteFile(ephemeral_filename)).Times(1);
  ExpectCryptohomeRemoval(*user);

  EXPECT_CALL(platform_, StatVFS(FilePath(kEphemeralCryptohomeDir), _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, CreateSparseFile(ephemeral_filename, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_,
              FormatExt4(ephemeral_filename, kDefaultExt4FormatOpts, 0))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, AttachLoop(ephemeral_filename))
      .WillOnce(Return(FilePath("/dev/loop7")));
  EXPECT_CALL(platform_, Stat(_, _)).WillRepeatedly(Return(false));
  EXPECT_CALL(platform_, CreateDirectory(_)).WillRepeatedly(Return(false));
  EXPECT_CALL(platform_, CreateDirectory(ephemeral_filename.DirName()))
      .WillOnce(Return(true));

  // Detach succeeds.
  ON_CALL(platform_, DetachLoop(_)).WillByDefault(Return(true));

  ASSERT_EQ(MOUNT_ERROR_FATAL,
            mount_->MountEphemeralCryptohome(user->username));
}

class EphemeralOwnerOnlySystemTest : public AltImageTest {
 public:
  EphemeralOwnerOnlySystemTest() {}
  EphemeralOwnerOnlySystemTest(const EphemeralOwnerOnlySystemTest&) = delete;
  EphemeralOwnerOnlySystemTest& operator=(const EphemeralOwnerOnlySystemTest&) =
      delete;

  void SetUp() { SetUpAltImage(kOwnerOnlyUsers, kOwnerOnlyUserCount); }
};

INSTANTIATE_TEST_SUITE_P(WithEcryptfs,
                         EphemeralOwnerOnlySystemTest,
                         ::testing::Values(true));
INSTANTIATE_TEST_SUITE_P(WithDircrypto,
                         EphemeralOwnerOnlySystemTest,
                         ::testing::Values(false));

TEST_P(EphemeralOwnerOnlySystemTest, MountNoCreateTest) {
  // Checks that when a device is not enterprise enrolled and has a known owner,
  // a tmpfs cryptohome is mounted and no regular vault is created.
  TestUser* owner = &helper_.users[3];
  TestUser* user = &helper_.users[0];
  set_policy(true, owner->username, true);

  // Always removes non-owner cryptohomes.
  std::vector<FilePath> owner_only;
  owner_only.push_back(owner->base_path);

  EXPECT_CALL(platform_, IsDirectoryMounted(_)).WillRepeatedly(Return(false));

  EXPECT_CALL(platform_, Stat(_, _)).WillRepeatedly(Return(false));
  ExpectEphemeralCryptohomeMount(*user);

  ASSERT_EQ(MOUNT_ERROR_NONE, mount_->MountEphemeralCryptohome(user->username));

  EXPECT_CALL(platform_, Unmount(user->ephemeral_mount_path, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_,
              Unmount(user->ephemeral_mount_path.Append("user"), _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(
      platform_,
      Unmount(Property(&FilePath::value, StartsWith("/home/chronos/u-")), _, _))
      .WillOnce(Return(true));  // user mount
  EXPECT_CALL(
      platform_,
      Unmount(Property(&FilePath::value, StartsWith("/home/user/")), _, _))
      .WillOnce(Return(true));  // user mount
  EXPECT_CALL(
      platform_,
      Unmount(Property(&FilePath::value, StartsWith("/home/root/")), _, _))
      .WillOnce(Return(true));  // user mount
  EXPECT_CALL(platform_, Unmount(FilePath("/home/chronos/user"), _, _))
      .WillOnce(Return(true));  // legacy mount
  EXPECT_CALL(platform_, Unmount(Property(&FilePath::value,
                                          StartsWith(kRunDaemonStoreBaseDir)),
                                 _, _))
      .WillOnce(Return(true));  // daemon store mounts
  EXPECT_CALL(platform_, ClearUserKeyring()).WillRepeatedly(Return(true));

  ExpectDownloadsUnmounts(*user, true /* ephemeral_mount */);

  // Detach succeeds.
  ON_CALL(platform_, DetachLoop(_)).WillByDefault(Return(true));

  ASSERT_TRUE(mount_->UnmountCryptohome());
}

TEST_P(EphemeralOwnerOnlySystemTest, NonOwnerMountIsEphemeralTest) {
  // Checks that when a device is not enterprise enrolled and has a known owner,
  // a mount request for a non-owner user with the |is_ephemeral| flag set
  // causes a tmpfs cryptohome to be mounted and no regular vault to be created.
  TestUser* owner = &helper_.users[3];
  TestUser* user = &helper_.users[0];
  set_policy(true, owner->username, false);

  // Always removes non-owner cryptohomes.
  std::vector<FilePath> owner_only;
  owner_only.push_back(owner->base_path);

  EXPECT_CALL(platform_, Stat(_, _)).WillRepeatedly(Return(false));
  EXPECT_CALL(platform_, EnumerateDirectoryEntries(_, _, _))
      .WillRepeatedly(DoAll(SetArgPointee<2>(owner_only), Return(true)));

  EXPECT_CALL(platform_, Unmount(_, _, _)).WillRepeatedly(Return(true));
  ExpectEphemeralCryptohomeMount(*user);

  ASSERT_EQ(MOUNT_ERROR_NONE, mount_->MountEphemeralCryptohome(user->username));

  // Detach succeeds.
  ON_CALL(platform_, DetachLoop(_)).WillByDefault(Return(true));

  ASSERT_TRUE(mount_->UnmountCryptohome());
}

TEST_P(EphemeralOwnerOnlySystemTest, OwnerMountIsEphemeralTest) {
  // Checks that when a device is not enterprise enrolled and has a known owner,
  // a mount request for the owner with the |ensure_ephemeral| flag set fails.
  TestUser* owner = &helper_.users[3];
  set_policy(true, owner->username, false);

  EXPECT_CALL(platform_, Mount(_, _, _, kDefaultMountFlags, _)).Times(0);

  ASSERT_EQ(MOUNT_ERROR_EPHEMERAL_MOUNT_BY_OWNER,
            mount_->MountEphemeralCryptohome(owner->username));
}

class EphemeralExistingUserSystemTest : public AltImageTest {
 public:
  EphemeralExistingUserSystemTest() {}
  EphemeralExistingUserSystemTest(const EphemeralExistingUserSystemTest&) =
      delete;
  EphemeralExistingUserSystemTest& operator=(
      const EphemeralExistingUserSystemTest&) = delete;

  void SetUp() { SetUpAltImage(kAlternateUsers, kAlternateUserCount); }
};

INSTANTIATE_TEST_SUITE_P(WithEcryptfs,
                         EphemeralExistingUserSystemTest,
                         ::testing::Values(true));
INSTANTIATE_TEST_SUITE_P(WithDircrypto,
                         EphemeralExistingUserSystemTest,
                         ::testing::Values(false));

TEST_P(EphemeralExistingUserSystemTest, OwnerUnknownMountNoRemoveTest) {
  // Checks that when a device is not enterprise enrolled and does not have a
  // known owner, no stale cryptohomes are removed while mounting.
  set_policy(false, "", true);
  TestUser* user = &helper_.users[0];

  // No c-homes will be removed.  The rest of the mocking just gets us to
  // Mount().
  for (auto& user : helper_.users)
    user.InjectUserPaths(&platform_, fake_platform::kChronosUID,
                         fake_platform::kChronosGID, fake_platform::kSharedGID,
                         kDaemonGid, ShouldTestEcryptfs());

  EXPECT_CALL(platform_, Stat(_, _)).WillRepeatedly(Return(false));
  EXPECT_CALL(platform_, CreateDirectory(user->vault_path)).Times(0);
  EXPECT_CALL(platform_, SafeCreateDirAndSetOwnership(user->vault_path, _, _))
      .Times(0);
  EXPECT_CALL(platform_, SafeCreateDirAndSetOwnershipAndPermissions(
                             user->vault_path, _, _, _))
      .Times(0);
  EXPECT_CALL(platform_, CreateDirectory(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SafeCreateDirAndSetOwnership(_, _, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SafeCreateDirAndSetOwnershipAndPermissions(_, _, _, _))
      .WillRepeatedly(Return(true));

  ExpectCryptohomeMount(*user);
  EXPECT_CALL(platform_, ClearUserKeyring()).WillOnce(Return(true));

  EXPECT_CALL(platform_, SetGroupAccessible(_, _, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, DeleteFile(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, DeletePathRecursively(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, FileExists(_)).WillRepeatedly(Return(true));

  EXPECT_CALL(platform_,
              Mount(_, _, kEphemeralMountType, kDefaultMountFlags, _))
      .Times(0);

  Mount::MountArgs mount_args = GetDefaultMountArgs();
  mount_args.create_if_missing = true;
  MountError error = MOUNT_ERROR_NONE;
  ASSERT_TRUE(mount_->MountCryptohome(user->username, FileSystemKeyset(),
                                      mount_args,
                                      /* is_pristine */ false, &error));

  EXPECT_CALL(platform_, Unmount(_, _, _)).WillRepeatedly(Return(true));
  if (ShouldTestEcryptfs()) {
    EXPECT_CALL(platform_,
                Unmount(Property(&FilePath::value, EndsWith("/mount")), _, _))
        .WillOnce(Return(true));  // user mount
  }
  EXPECT_CALL(
      platform_,
      Unmount(Property(&FilePath::value, StartsWith("/home/chronos/u-")), _, _))
      .WillOnce(Return(true));  // user mount
  EXPECT_CALL(
      platform_,
      Unmount(Property(&FilePath::value, StartsWith("/home/user/")), _, _))
      .WillOnce(Return(true));  // user mount
  EXPECT_CALL(
      platform_,
      Unmount(Property(&FilePath::value, StartsWith("/home/root/")), _, _))
      .WillOnce(Return(true));  // user mount
  EXPECT_CALL(platform_, Unmount(FilePath("/home/chronos/user"), _, _))
      .WillOnce(Return(true));  // legacy mount
  EXPECT_CALL(platform_, Unmount(Property(&FilePath::value,
                                          StartsWith(kRunDaemonStoreBaseDir)),
                                 _, _))
      .WillOnce(Return(true));  // daemon store mounts
  EXPECT_CALL(platform_, ClearUserKeyring()).WillRepeatedly(Return(true));
  ExpectDownloadsUnmounts(*user, false /* ephemeral_mount */);
  ASSERT_TRUE(mount_->UnmountCryptohome());
}

TEST_P(EphemeralExistingUserSystemTest, EnterpriseMountRemoveTest) {
  // Checks that when a device is enterprise enrolled, all stale cryptohomes are
  // removed while mounting.
  set_policy(false, "", true);
  homedirs_->set_enterprise_owned(true);
  TestUser* user = &helper_.users[0];

  std::vector<int> expect_deletion;
  expect_deletion.push_back(0);
  expect_deletion.push_back(1);
  expect_deletion.push_back(2);
  expect_deletion.push_back(3);
  PrepareHomedirs(true, &expect_deletion, NULL);

  // Let Mount know how many vaults there are.
  std::vector<FilePath> no_vaults;
  EXPECT_CALL(platform_, EnumerateDirectoryEntries(ShadowRoot(), false, _))
      .WillOnce(DoAll(SetArgPointee<2>(vaults_), Return(true)))
      // Don't re-delete on Unmount.
      .WillRepeatedly(DoAll(SetArgPointee<2>(no_vaults), Return(true)));
  // Don't say any cryptohomes are mounted
  EXPECT_CALL(platform_, IsDirectoryMounted(_)).WillRepeatedly(Return(false));

  // Expect deletion of cryptohome mount points.
  EXPECT_CALL(platform_,
              DeletePathRecursively(
                  brillo::cryptohome::home::GetRootPath(user->username)))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_,
              DeletePathRecursively(
                  brillo::cryptohome::home::GetUserPath(user->username)))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, DeletePathRecursively(
                             MountHelper::GetNewUserPath(user->username)))
      .WillOnce(Return(true));

  std::vector<FilePath> empty;
  EXPECT_CALL(
      platform_,
      EnumerateDirectoryEntries(
          AnyOf(FilePath("/home/root/"), FilePath("/home/user/")), _, _))
      .WillRepeatedly(DoAll(SetArgPointee<2>(empty), Return(true)));
  EXPECT_CALL(platform_,
              Stat(AnyOf(FilePath("/home/chronos"),
                         MountHelper::GetNewUserPath(user->username)),
                   _))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(platform_,
              Stat(AnyOf(FilePath("/home"), FilePath("/home/root"),
                         brillo::cryptohome::home::GetRootPath(user->username),
                         FilePath("/home/user"),
                         brillo::cryptohome::home::GetUserPath(user->username)),
                   _))
      .WillRepeatedly(Return(false));
  helper_.InjectEphemeralSkeleton(&platform_,
                                  FilePath(user->user_ephemeral_mount_path));
  user->InjectUserPaths(&platform_, fake_platform::kChronosUID,
                        fake_platform::kChronosGID, fake_platform::kSharedGID,
                        kDaemonGid, ShouldTestEcryptfs());
  // Only expect the mounted user to "exist".
  EXPECT_CALL(platform_,
              DirectoryExists(Property(
                  &FilePath::value, StartsWith(user->user_mount_path.value()))))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, CreateDirectory(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetOwnership(_, _, _, _)).WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetPermissions(_, _)).WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetGroupAccessible(_, _, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, DeleteFile(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, DeleteFile(MountHelper::GetEphemeralSparseFile(
                             user->obfuscated_username)))
      .WillOnce(Return(true));

  EXPECT_CALL(platform_, Stat(user->root_ephemeral_mount_path, _))
      .WillOnce(Return(false));
  EXPECT_CALL(platform_, DeletePathRecursively(user->root_ephemeral_mount_path))
      .WillOnce(Return(true));

  ExpectEphemeralCryptohomeMount(*user);

  // Deleting users will cause each user's shadow root subdir to be
  // searched for LE credentials.
  for (const auto& user : helper_.users) {
    EXPECT_CALL(platform_,
                GetFileEnumerator(ShadowRoot().Append(user.obfuscated_username),
                                  false, _))
        .WillOnce(Return(new NiceMock<MockFileEnumerator>()));
  }

  ASSERT_EQ(MOUNT_ERROR_NONE, mount_->MountEphemeralCryptohome(user->username));

  EXPECT_CALL(platform_, Unmount(_, _, _)).WillRepeatedly(Return(true));
  EXPECT_CALL(
      platform_,
      Unmount(Property(&FilePath::value, StartsWith("/home/chronos/u-")), _, _))
      .WillOnce(Return(true));  // user mount
  EXPECT_CALL(
      platform_,
      Unmount(Property(&FilePath::value, StartsWith("/home/user/")), _, _))
      .WillOnce(Return(true));  // user mount
  EXPECT_CALL(
      platform_,
      Unmount(Property(&FilePath::value, StartsWith("/home/root/")), _, _))
      .WillOnce(Return(true));  // user mount
  EXPECT_CALL(platform_, Unmount(FilePath("/home/chronos/user"), _, _))
      .WillOnce(Return(true));  // legacy mount
  EXPECT_CALL(platform_, DeletePathRecursively(user->ephemeral_mount_path))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, ClearUserKeyring()).WillRepeatedly(Return(true));
  ExpectDownloadsUnmounts(*user, true /* ephemeral_mount */);
  // Detach succeeds.
  ON_CALL(platform_, DetachLoop(_)).WillByDefault(Return(true));
  ASSERT_TRUE(mount_->UnmountCryptohome());
}

TEST_P(EphemeralExistingUserSystemTest, MountRemoveTest) {
  // Checks that when a device is not enterprise enrolled and has a known owner,
  // all non-owner cryptohomes are removed while mounting.
  TestUser* owner = &helper_.users[3];
  set_policy(true, owner->username, true);
  TestUser* user = &helper_.users[0];

  std::vector<int> expect_deletion;
  expect_deletion.push_back(0);  // Mounting user shouldn't use be persistent.
  expect_deletion.push_back(1);
  expect_deletion.push_back(2);
  // Expect all users but the owner to be removed.
  PrepareHomedirs(true, &expect_deletion, NULL);

  // Let Mount know how many vaults there are.
  std::vector<FilePath> no_vaults;
  EXPECT_CALL(platform_, EnumerateDirectoryEntries(ShadowRoot(), false, _))
      .WillOnce(DoAll(SetArgPointee<2>(vaults_), Return(true)))
      // Don't re-delete on Unmount.
      .WillRepeatedly(DoAll(SetArgPointee<2>(no_vaults), Return(true)));
  // Don't say any cryptohomes are mounted
  EXPECT_CALL(platform_, IsDirectoryMounted(_)).WillRepeatedly(Return(false));

  // Expect deletion of cryptohome mount points.
  EXPECT_CALL(platform_,
              DeletePathRecursively(
                  brillo::cryptohome::home::GetRootPath(user->username)))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_,
              DeletePathRecursively(
                  brillo::cryptohome::home::GetUserPath(user->username)))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, DeletePathRecursively(
                             MountHelper::GetNewUserPath(user->username)))
      .WillOnce(Return(true));

  std::vector<FilePath> empty;
  EXPECT_CALL(
      platform_,
      EnumerateDirectoryEntries(
          AnyOf(FilePath("/home/root/"), FilePath("/home/user/")), _, _))
      .WillRepeatedly(DoAll(SetArgPointee<2>(empty), Return(true)));
  EXPECT_CALL(platform_,
              Stat(AnyOf(FilePath("/home/chronos"),
                         MountHelper::GetNewUserPath(user->username)),
                   _))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(platform_,
              Stat(AnyOf(FilePath("/home"), FilePath("/home/root"),
                         brillo::cryptohome::home::GetRootPath(user->username),
                         FilePath("/home/user"),
                         brillo::cryptohome::home::GetUserPath(user->username)),
                   _))
      .WillRepeatedly(Return(false));
  helper_.InjectEphemeralSkeleton(&platform_,
                                  FilePath(user->user_ephemeral_mount_path));
  user->InjectUserPaths(&platform_, fake_platform::kChronosUID,
                        fake_platform::kChronosGID, fake_platform::kSharedGID,
                        kDaemonGid, ShouldTestEcryptfs());
  // Only expect the mounted user to "exist".
  EXPECT_CALL(platform_,
              DirectoryExists(Property(
                  &FilePath::value, StartsWith(user->user_mount_path.value()))))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, CreateDirectory(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetOwnership(_, _, _, _)).WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetPermissions(_, _)).WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetGroupAccessible(_, _, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, DeleteFile(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, DeleteFile(MountHelper::GetEphemeralSparseFile(
                             user->obfuscated_username)))
      .WillRepeatedly(Return(true));

  EXPECT_CALL(platform_, Stat(user->root_ephemeral_mount_path, _))
      .WillOnce(Return(false));
  EXPECT_CALL(platform_, DeletePathRecursively(user->root_ephemeral_mount_path))
      .WillOnce(Return(true));

  ExpectEphemeralCryptohomeMount(*user);

  // Deleting users will cause "going-to-be-deleted" users' shadow root
  // subdir to be searched for LE credentials.
  for (int i = 0; i < helper_.users.size() - 1; i++) {
    TestUser* cur_user = &helper_.users[i];
    EXPECT_CALL(
        platform_,
        GetFileEnumerator(ShadowRoot().Append(cur_user->obfuscated_username),
                          false, _))
        .WillOnce(Return(new NiceMock<MockFileEnumerator>()));
  }

  ASSERT_EQ(MOUNT_ERROR_NONE, mount_->MountEphemeralCryptohome(user->username));

  EXPECT_CALL(platform_, Unmount(_, _, _)).WillRepeatedly(Return(true));
  EXPECT_CALL(
      platform_,
      Unmount(Property(&FilePath::value, StartsWith("/home/chronos/u-")), _, _))
      .WillOnce(Return(true));  // user mount
  EXPECT_CALL(
      platform_,
      Unmount(Property(&FilePath::value, StartsWith("/home/user/")), _, _))
      .WillOnce(Return(true));  // user mount
  EXPECT_CALL(
      platform_,
      Unmount(Property(&FilePath::value, StartsWith("/home/root/")), _, _))
      .WillOnce(Return(true));  // user mount
  EXPECT_CALL(platform_, Unmount(FilePath("/home/chronos/user"), _, _))
      .WillOnce(Return(true));  // legacy mount
  EXPECT_CALL(platform_, DeletePathRecursively(user->ephemeral_mount_path))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, ClearUserKeyring()).WillRepeatedly(Return(true));
  ExpectDownloadsUnmounts(*user, true /* ephemeral_mount */);
  // Detach succeeds.
  ON_CALL(platform_, DetachLoop(_)).WillByDefault(Return(true));
  ASSERT_TRUE(mount_->UnmountCryptohome());
}

TEST_P(EphemeralExistingUserSystemTest, OwnerUnknownUnmountNoRemoveTest) {
  // Checks that when a device is not enterprise enrolled and does not have a
  // known owner, no stale cryptohomes are removed while unmounting.
  set_policy(false, "", true);
  ASSERT_TRUE(mount_->UnmountCryptohome());
}

TEST_P(EphemeralExistingUserSystemTest, EnterpriseUnmountRemoveTest) {
  // Checks that when a device is enterprise enrolled, all stale cryptohomes are
  // removed while unmounting.
  set_policy(false, "", true);
  homedirs_->set_enterprise_owned(true);

  EXPECT_CALL(platform_, DirectoryExists(_)).WillRepeatedly(Return(true));

  std::vector<int> expect_deletion;
  expect_deletion.push_back(0);
  expect_deletion.push_back(1);
  expect_deletion.push_back(2);
  expect_deletion.push_back(3);
  PrepareHomedirs(false, &expect_deletion, NULL);

  // Let Mount know how many vaults there are.
  EXPECT_CALL(platform_, EnumerateDirectoryEntries(ShadowRoot(), false, _))
      .WillRepeatedly(DoAll(SetArgPointee<2>(vaults_), Return(true)));

  // Don't say any cryptohomes are mounted
  EXPECT_CALL(platform_, IsDirectoryMounted(_)).WillRepeatedly(Return(false));
  std::vector<FilePath> empty;
  EXPECT_CALL(
      platform_,
      EnumerateDirectoryEntries(
          AnyOf(FilePath("/home/root/"), FilePath("/home/user/")), _, _))
      .WillRepeatedly(DoAll(SetArgPointee<2>(empty), Return(true)));

  ASSERT_TRUE(mount_->UnmountCryptohome());
}

TEST_P(EphemeralExistingUserSystemTest, UnmountRemoveTest) {
  // Checks that when a device is not enterprise enrolled and has a known owner,
  // all stale cryptohomes are removed while unmounting.
  TestUser* owner = &helper_.users[3];
  set_policy(true, owner->username, true);

  EXPECT_CALL(platform_, DirectoryExists(_)).WillRepeatedly(Return(true));

  // All users but the owner.
  std::vector<int> expect_deletion;
  expect_deletion.push_back(0);
  expect_deletion.push_back(1);
  expect_deletion.push_back(2);
  PrepareHomedirs(false, &expect_deletion, NULL);

  // Let Mount know how many vaults there are.
  EXPECT_CALL(platform_, EnumerateDirectoryEntries(ShadowRoot(), false, _))
      .WillRepeatedly(DoAll(SetArgPointee<2>(vaults_), Return(true)));

  // Don't say any cryptohomes are mounted
  EXPECT_CALL(platform_, IsDirectoryMounted(_)).WillRepeatedly(Return(false));
  std::vector<FilePath> empty;
  EXPECT_CALL(
      platform_,
      EnumerateDirectoryEntries(
          AnyOf(FilePath("/home/root/"), FilePath("/home/user/")), _, _))
      .WillRepeatedly(DoAll(SetArgPointee<2>(empty), Return(true)));

  ASSERT_TRUE(mount_->UnmountCryptohome());
}

TEST_P(EphemeralExistingUserSystemTest, NonOwnerMountIsEphemeralTest) {
  // Checks that when a device is not enterprise enrolled and has a known owner,
  // a mount request for a non-owner user with the |is_ephemeral| flag set
  // causes a tmpfs cryptohome to be mounted, even if a regular vault exists for
  // the user.
  // Since ephemeral users aren't enabled, no vaults will be deleted.
  TestUser* owner = &helper_.users[3];
  set_policy(true, owner->username, false);
  TestUser* user = &helper_.users[0];

  EXPECT_CALL(platform_, DirectoryExists(_)).WillRepeatedly(Return(true));

  PrepareHomedirs(true, NULL, NULL);

  // Let Mount know how many vaults there are.
  EXPECT_CALL(platform_, EnumerateDirectoryEntries(ShadowRoot(), false, _))
      .WillRepeatedly(DoAll(SetArgPointee<2>(vaults_), Return(true)));
  // Don't say any cryptohomes are mounted
  EXPECT_CALL(platform_, IsDirectoryMounted(_)).WillRepeatedly(Return(false));
  std::vector<FilePath> empty;
  EXPECT_CALL(
      platform_,
      EnumerateDirectoryEntries(
          AnyOf(FilePath("/home/root/"), FilePath("/home/user/")), _, _))
      .WillRepeatedly(DoAll(SetArgPointee<2>(empty), Return(true)));
  EXPECT_CALL(platform_,
              Stat(AnyOf(FilePath("/home/chronos"),
                         MountHelper::GetNewUserPath(user->username)),
                   _))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(platform_,
              Stat(AnyOf(FilePath("/home"), FilePath("/home/root"),
                         brillo::cryptohome::home::GetRootPath(user->username),
                         FilePath("/home/user"),
                         brillo::cryptohome::home::GetUserPath(user->username)),
                   _))
      .WillRepeatedly(Return(false));
  // Only expect the mounted user to "exist".
  EXPECT_CALL(platform_,
              DirectoryExists(Property(
                  &FilePath::value, StartsWith(user->user_mount_path.value()))))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, CreateDirectory(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetOwnership(_, _, _, _)).WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetPermissions(_, _)).WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetGroupAccessible(_, _, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, FileExists(Property(&FilePath::value,
                                             StartsWith("/home/chronos/user"))))
      .WillRepeatedly(Return(true));

  helper_.InjectEphemeralSkeleton(&platform_,
                                  FilePath(user->user_ephemeral_mount_path));

  EXPECT_CALL(platform_, Stat(user->root_ephemeral_mount_path, _))
      .WillOnce(Return(false));

  EXPECT_CALL(platform_, Unmount(_, _, _)).WillRepeatedly(Return(true));
  ExpectEphemeralCryptohomeMount(*user);

  // Detach succeeds.
  ON_CALL(platform_, DetachLoop(_)).WillByDefault(Return(true));

  ASSERT_EQ(MOUNT_ERROR_NONE, mount_->MountEphemeralCryptohome(user->username));
}

TEST_P(EphemeralExistingUserSystemTest, EnterpriseMountIsEphemeralTest) {
  // Checks that when a device is enterprise enrolled, a mount request with the
  // |is_ephemeral| flag set causes a tmpfs cryptohome to be mounted, even
  // if a regular vault exists for the user.
  // Since ephemeral users aren't enabled, no vaults will be deleted.
  set_policy(true, "", false);
  homedirs_->set_enterprise_owned(true);

  TestUser* user = &helper_.users[0];

  // Mounting user vault won't be deleted, but tmpfs mount should still be
  // used.
  PrepareHomedirs(true, NULL, NULL);

  // Let Mount know how many vaults there are.
  EXPECT_CALL(platform_, EnumerateDirectoryEntries(ShadowRoot(), false, _))
      .WillRepeatedly(DoAll(SetArgPointee<2>(vaults_), Return(true)));
  // Don't say any cryptohomes are mounted.
  EXPECT_CALL(platform_, IsDirectoryMounted(_)).WillRepeatedly(Return(false));
  std::vector<FilePath> empty;
  EXPECT_CALL(
      platform_,
      EnumerateDirectoryEntries(
          AnyOf(FilePath("/home/root/"), FilePath("/home/user/")), _, _))
      .WillRepeatedly(DoAll(SetArgPointee<2>(empty), Return(true)));
  EXPECT_CALL(platform_,
              Stat(AnyOf(FilePath("/home/chronos"),
                         MountHelper::GetNewUserPath(user->username)),
                   _))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(platform_,
              Stat(AnyOf(FilePath("/home"), FilePath("/home/root"),
                         brillo::cryptohome::home::GetRootPath(user->username),
                         FilePath("/home/user"),
                         brillo::cryptohome::home::GetUserPath(user->username)),
                   _))
      .WillRepeatedly(Return(false));
  // Only expect the mounted user to "exist".
  EXPECT_CALL(platform_,
              DirectoryExists(Property(
                  &FilePath::value, StartsWith(user->user_mount_path.value()))))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, CreateDirectory(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetOwnership(_, _, _, _)).WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetPermissions(_, _)).WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetGroupAccessible(_, _, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, FileExists(Property(&FilePath::value,
                                             StartsWith("/home/chronos/user"))))
      .WillRepeatedly(Return(true));

  helper_.InjectEphemeralSkeleton(&platform_,
                                  FilePath(user->user_ephemeral_mount_path));

  EXPECT_CALL(platform_, Stat(user->root_ephemeral_mount_path, _))
      .WillOnce(Return(false));

  EXPECT_CALL(platform_, Unmount(_, _, _)).WillRepeatedly(Return(true));
  ExpectEphemeralCryptohomeMount(*user);

  // Detach succeeds.
  ON_CALL(platform_, DetachLoop(_)).WillByDefault(Return(true));

  ASSERT_EQ(MOUNT_ERROR_NONE, mount_->MountEphemeralCryptohome(user->username));
}

TEST_P(EphemeralNoUserSystemTest, MountGuestUserDir) {
  base::stat_wrapper_t fake_root_st;
  fake_root_st.st_uid = 0;
  fake_root_st.st_gid = 0;
  fake_root_st.st_mode = S_IFDIR | S_IRWXU;
  EXPECT_CALL(platform_, Stat(FilePath("/home"), _))
      .Times(3)
      .WillRepeatedly(DoAll(SetArgPointee<1>(fake_root_st), Return(true)));
  EXPECT_CALL(platform_, Stat(FilePath("/home/root"), _))
      .WillOnce(DoAll(SetArgPointee<1>(fake_root_st), Return(true)));
  EXPECT_CALL(platform_, Stat(FilePath("/home/user"), _))
      .WillOnce(DoAll(SetArgPointee<1>(fake_root_st), Return(true)));
  base::stat_wrapper_t fake_user_st;
  fake_user_st.st_uid = fake_platform::kChronosUID;
  fake_user_st.st_gid = fake_platform::kChronosGID;
  fake_user_st.st_mode = S_IFDIR | S_IRWXU;
  EXPECT_CALL(platform_, Stat(FilePath("/home/chronos"), _))
      .WillOnce(DoAll(SetArgPointee<1>(fake_user_st), Return(true)));
  EXPECT_CALL(platform_, CreateDirectory(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SafeCreateDirAndSetOwnership(_, _, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SafeCreateDirAndSetOwnershipAndPermissions(_, _, _, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, IsDirectoryMounted(_)).WillRepeatedly(Return(false));
  EXPECT_CALL(platform_, DirectoryExists(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, FileExists(_)).WillRepeatedly(Return(true));

  EXPECT_CALL(platform_, StatVFS(FilePath(kEphemeralCryptohomeDir), _))
      .WillOnce(Return(true));
  const std::string sparse_prefix =
      FilePath(kEphemeralCryptohomeDir).Append(kSparseFileDir).value();
  EXPECT_CALL(platform_,
              CreateSparseFile(
                  Property(&FilePath::value, StartsWith(sparse_prefix)), _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_,
              AttachLoop(Property(&FilePath::value, StartsWith(sparse_prefix))))
      .WillOnce(Return(FilePath("/dev/loop7")));
  EXPECT_CALL(platform_,
              FormatExt4(Property(&FilePath::value, StartsWith(sparse_prefix)),
                         kDefaultExt4FormatOpts, 0))
      .WillOnce(Return(true));
  EXPECT_CALL(
      platform_,
      Stat(Property(&FilePath::value, StartsWith(kEphemeralCryptohomeDir)), _))
      .WillOnce(Return(false));
  EXPECT_CALL(platform_, Mount(_, _, _, kDefaultMountFlags, _)).Times(0);
  EXPECT_CALL(platform_, Mount(FilePath("/dev/loop7"), _, kEphemeralMountType,
                               kDefaultMountFlags, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_,
              SetSELinuxContext(Property(&FilePath::value,
                                         StartsWith(kEphemeralCryptohomeDir)),
                                cryptohome::kEphemeralCryptohomeRootContext))
      .WillOnce(Return(true));

  EXPECT_CALL(
      platform_,
      Bind(Property(&FilePath::value, StartsWith(kEphemeralCryptohomeDir)),
           Property(&FilePath::value, StartsWith(kEphemeralCryptohomeDir)), _,
           true))
      .Times(2)
      .WillRepeatedly(Return(true));

  EXPECT_CALL(
      platform_,
      Bind(Property(&FilePath::value, StartsWith(kEphemeralCryptohomeDir)),
           Property(&FilePath::value, StartsWith("/home/root/")), _, true))
      .WillOnce(Return(true));
  EXPECT_CALL(
      platform_,
      Bind(Property(&FilePath::value, StartsWith(kEphemeralCryptohomeDir)),
           Property(&FilePath::value, StartsWith("/home/user/")), _, true))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, Bind(Property(&FilePath::value,
                                       StartsWith(kEphemeralCryptohomeDir)),
                              FilePath("/home/chronos/user"), _, true))
      .WillOnce(Return(true));
  EXPECT_CALL(
      platform_,
      Bind(Property(&FilePath::value, StartsWith(kEphemeralCryptohomeDir)),
           Property(&FilePath::value, StartsWith("/home/chronos/u-")), _, true))
      .WillOnce(Return(true));

  ASSERT_TRUE(mount_->MountGuestCryptohome());

  // Unmount succeeds.
  ON_CALL(platform_, Unmount(_, _, _)).WillByDefault(Return(true));
  // Detach succeeds.
  ON_CALL(platform_, DetachLoop(_)).WillByDefault(Return(true));
}

}  // namespace cryptohome
