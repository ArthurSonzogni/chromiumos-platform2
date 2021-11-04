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
#include "cryptohome/crypto/secure_blob_util.h"
#include "cryptohome/cryptohome_common.h"
#include "cryptohome/filesystem_layout.h"
#include "cryptohome/keyset_management.h"
#include "cryptohome/mock_crypto.h"
#include "cryptohome/mock_keyset_management.h"
#include "cryptohome/mock_platform.h"
#include "cryptohome/mock_tpm.h"
#include "cryptohome/mock_vault_keyset.h"
#include "cryptohome/storage/encrypted_container/encrypted_container.h"
#include "cryptohome/storage/encrypted_container/encrypted_container_factory.h"
#include "cryptohome/storage/encrypted_container/fake_backing_device.h"
#include "cryptohome/storage/file_system_keyset.h"
#include "cryptohome/storage/homedirs.h"
#include "cryptohome/storage/mock_homedirs.h"
#include "cryptohome/storage/mount_helper.h"
#include "cryptohome/vault_keyset.h"
#include "cryptohome/vault_keyset.pb.h"

namespace cryptohome {

using base::FilePath;
using brillo::SecureBlob;
using ::testing::_;
using ::testing::DoAll;
using ::testing::Eq;
using ::testing::InSequence;
using ::testing::NiceMock;
using ::testing::Pointee;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::StartsWith;

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

constexpr char kDevLoopPrefix[] = "/dev/loop";

constexpr char kUser[] = "someuser";

MATCHER_P(DirCryptoReferenceMatcher, reference, "") {
  if (reference.reference != arg.reference) {
    return false;
  }
  if (reference.policy_version != arg.policy_version) {
    return false;
  }
  return true;
}

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
      base::FilePath(kRun), 0755, kRootUid, kRootGid));
  ASSERT_TRUE(platform->SafeCreateDirAndSetOwnershipAndPermissions(
      base::FilePath(kRunCryptohome), 0700, kRootUid, kRootGid));
  ASSERT_TRUE(platform->SafeCreateDirAndSetOwnershipAndPermissions(
      base::FilePath(kRunDaemonStore), 0755, kRootUid, kRootGid));
  ASSERT_TRUE(platform->SafeCreateDirAndSetOwnershipAndPermissions(
      base::FilePath(kHome), 0755, kRootUid, kRootGid));
  ASSERT_TRUE(platform->SafeCreateDirAndSetOwnershipAndPermissions(
      base::FilePath(kHomeChronos), 0755, kChronosUid, kChronosGid));
  ASSERT_TRUE(platform->SafeCreateDirAndSetOwnershipAndPermissions(
      base::FilePath(kHomeChronosUser), 01755, kChronosUid, kChronosGid));
  ASSERT_TRUE(platform->SafeCreateDirAndSetOwnershipAndPermissions(
      base::FilePath(kHomeUser), 0755, kRootUid, kRootGid));
  ASSERT_TRUE(platform->SafeCreateDirAndSetOwnershipAndPermissions(
      base::FilePath(kHomeRoot), 01751, kRootUid, kRootGid));

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
                               01770, kRootUid, kDaemonStoreGid,
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
        kRootUid, kDaemonStoreGid, expect_present);
  }
}

void CheckUserMountPoints(Platform* platform,
                          const std::string& username,
                          const base::FilePath& vault_mount_point,
                          bool expect_present,
                          bool downloads_bind_mount = true) {
  const base::FilePath chronos_hash_user_mount_point =
      ChronosHashPath(username);

  std::multimap<const base::FilePath, const base::FilePath>
      expected_user_mount_map{
          {vault_mount_point.Append("user"), vault_mount_point.Append("user")},
          {vault_mount_point.Append("user"),
           brillo::cryptohome::home::GetUserPath(username)},
          {vault_mount_point.Append("user"), chronos_hash_user_mount_point},
          {vault_mount_point.Append("user"), base::FilePath(kHomeChronosUser)},
      };

  if (downloads_bind_mount) {
    expected_user_mount_map.insert(
        {vault_mount_point.Append("user").Append(kDownloadsDir),
         vault_mount_point.Append("user")
             .Append(kMyFilesDir)
             .Append(kDownloadsDir)});
  }
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
              expect_present && downloads_bind_mount);
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
    CheckExistanceAndPermissions(platform, base_path, 0750, kChronosUid,
                                 kChronosAccessGid, expect_present);
  }

  // Subdirectories
  CheckExistanceAndPermissions(platform, base_path.Append(kDownloadsDir), 0750,
                               kChronosUid, kChronosAccessGid, expect_present);

  CheckExistanceAndPermissions(platform, base_path.Append(kMyFilesDir), 0750,
                               kChronosUid, kChronosAccessGid, expect_present);

  CheckExistanceAndPermissions(
      platform, base_path.Append(kMyFilesDir).Append(kDownloadsDir), 0750,
      kChronosUid, kChronosAccessGid, expect_present);

  CheckExistanceAndPermissions(platform, base_path.Append(kCacheDir), 0700,
                               kChronosUid, kChronosGid, expect_present);

  CheckExistanceAndPermissions(platform, base_path.Append(kGCacheDir), 0750,
                               kChronosUid, kChronosAccessGid, expect_present);

  CheckExistanceAndPermissions(
      platform, base_path.Append(kGCacheDir).Append(kGCacheVersion2Dir), 0770,
      kChronosUid, kChronosAccessGid, expect_present);
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
                               kChronosUid, kChronosGid, expect_present);
  CheckExistanceAndPermissions(
      platform, base_path.Append(kFile1),
      0750,  // NOT A PART OF THE CONTRACT, SEE TODO ABOVE.
      kChronosUid, kChronosGid, expect_present);
  CheckExistanceAndPermissions(platform, base_path.Append(kDir1Dir2), 0750,
                               kChronosUid, kChronosGid, expect_present);
  CheckExistanceAndPermissions(
      platform, base_path.Append(kDir1File2),
      0750,  // NOT A PART OF THE CONTRACT, SEE TODO ABOVE.
      kChronosUid, kChronosGid, expect_present);
  CheckExistanceAndPermissions(
      platform, base_path.Append(kDir1Dir2File3),
      0750,  // NOT A PART OF THE CONTRACT, SEE TODO ABOVE.
      kChronosUid, kChronosGid, expect_present);

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

// TODO(dlunev): add test ecryptfs blasts "mount".
class PersistentSystemTest : public ::testing::Test {
 public:
  PersistentSystemTest() : crypto_(&platform_) {}

  void SetUp() {
    ASSERT_NO_FATAL_FAILURE(PrepareDirectoryStructure(&platform_));
    brillo::SecureBlob system_salt;
    InitializeFilesystemLayout(&platform_, &crypto_, &system_salt);
    platform_.GetFake()->SetSystemSaltForLibbrillo(system_salt);

    std::unique_ptr<EncryptedContainerFactory> container_factory =
        std::make_unique<EncryptedContainerFactory>(
            &platform_, std::make_unique<FakeBackingDeviceFactory>(&platform_));
    homedirs_ = std::make_unique<HomeDirs>(
        &platform_, std::make_unique<policy::PolicyProvider>(),
        base::BindRepeating([](const std::string& unused) {}),
        std::make_unique<CryptohomeVaultFactory>(&platform_,
                                                 std::move(container_factory)));

    mount_ = new Mount(&platform_, homedirs_.get());

    EXPECT_TRUE(mount_->Init(/*use_local_mounter=*/true));
  }

  void TearDown() { platform_.GetFake()->RemoveSystemSaltForLibbrillo(); }

 protected:
  // Protected for trivial access.
  NiceMock<MockPlatform> platform_;
  Crypto crypto_;
  std::unique_ptr<HomeDirs> homedirs_;
  scoped_refptr<Mount> mount_;

  void VerifyFS(const std::string& username,
                MountType type,
                bool expect_present,
                bool downloads_bind_mount = true) {
    const std::string obfuscated_username =
        brillo::cryptohome::home::SanitizeUserName(username);
    if (type == MountType::ECRYPTFS) {
      CheckEcryptfsMount(username, expect_present);
    } else if (type == MountType::DIR_CRYPTO) {
      CheckDircryptoMount(username, expect_present);
    } else if (type == MountType::DMCRYPT) {
      CheckDmcryptMount(username, expect_present);
    } else {
      NOTREACHED();
    }
    ASSERT_NO_FATAL_FAILURE(CheckRootAndDaemonStoreMounts(
        &platform_, username, GetUserMountDirectory(obfuscated_username),
        expect_present));
    ASSERT_NO_FATAL_FAILURE(CheckUserMountPoints(
        &platform_, username, GetUserMountDirectory(obfuscated_username),
        expect_present, downloads_bind_mount));

    const std::vector<base::FilePath> user_vault_and_mounts{
        GetUserMountDirectory(obfuscated_username).Append("user"),
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

  void MockPreclearKeyring(bool success) {
    EXPECT_CALL(platform_, ClearUserKeyring()).WillOnce(Return(success));
  }

  void MockEcryptfsKeyringSetup(const FileSystemKeyset& keyset, bool success) {
    EXPECT_CALL(platform_, AddEcryptfsAuthToken(
                               keyset.Key().fek,
                               SecureBlobToHex(keyset.KeyReference().fek_sig),
                               keyset.Key().fek_salt))
        .WillOnce(Return(success));
    EXPECT_CALL(platform_, AddEcryptfsAuthToken(
                               keyset.Key().fnek,
                               SecureBlobToHex(keyset.KeyReference().fnek_sig),
                               keyset.Key().fnek_salt))
        .WillOnce(Return(success));
  }

  void MockEcryptfsKeyringTeardown(bool success) {
    EXPECT_CALL(platform_, ClearUserKeyring()).WillOnce(Return(success));
  }

  void MockDircryptoKeyringSetup(const std::string& username,
                                 const FileSystemKeyset& keyset,
                                 bool existing_dir,
                                 bool success) {
    const std::string obfuscated_username =
        brillo::cryptohome::home::SanitizeUserName(username);
    const base::FilePath backing_dir =
        GetUserMountDirectory(obfuscated_username);
    const dircrypto::KeyReference reference = {
        .policy_version = FSCRYPT_POLICY_V1,
        .reference = keyset.KeyReference().fek_sig,
    };

    EXPECT_CALL(platform_, GetDirectoryPolicyVersion(backing_dir))
        .WillOnce(Return(existing_dir ? FSCRYPT_POLICY_V1 : -1));
    EXPECT_CALL(platform_, GetDirCryptoKeyState(ShadowRoot()))
        .WillRepeatedly(Return(dircrypto::KeyState::NO_KEY));
    EXPECT_CALL(platform_, GetDirCryptoKeyState(backing_dir))
        .WillRepeatedly(Return(existing_dir ? dircrypto::KeyState::ENCRYPTED
                                            : dircrypto::KeyState::NO_KEY));
    // EXPECT_CALL(platform_,
    // CheckDircryptoKeyIoctlSupport()).WillOnce(Return(true));
    EXPECT_CALL(platform_, AddDirCryptoKeyToKeyring(
                               keyset.Key().fek,
                               Pointee(DirCryptoReferenceMatcher(reference))))
        .WillOnce(Return(success));
    EXPECT_CALL(
        platform_,
        SetDirCryptoKey(backing_dir, DirCryptoReferenceMatcher(reference)))
        .WillOnce(Return(success));
  }

  void MockDircryptoKeyringTeardown(const std::string& username,
                                    const FileSystemKeyset& keyset,
                                    bool success) {
    const std::string obfuscated_username =
        brillo::cryptohome::home::SanitizeUserName(username);
    const base::FilePath backing_dir =
        GetUserMountDirectory(obfuscated_username);
    const dircrypto::KeyReference reference = {
        .policy_version = FSCRYPT_POLICY_V1,
        .reference = keyset.KeyReference().fek_sig,
    };
    EXPECT_CALL(platform_,
                InvalidateDirCryptoKey(DirCryptoReferenceMatcher(reference),
                                       backing_dir))
        .WillOnce(Return(success));
  }

  void SetHomedir(const std::string& username) {
    const std::string obfuscated_username =
        brillo::cryptohome::home::SanitizeUserName(username);
    ASSERT_TRUE(
        platform_.CreateDirectory(ShadowRoot().Append(obfuscated_username)));
  }

  void SetDmcryptPrereqs(const std::string& username) {
    const std::string obfuscated_username =
        brillo::cryptohome::home::SanitizeUserName(username);
    SetHomedir(username);
    ASSERT_TRUE(
        platform_.TouchFileDurable(GetDmcryptDataVolume(obfuscated_username)));
    ASSERT_TRUE(
        platform_.TouchFileDurable(GetDmcryptCacheVolume(obfuscated_username)));
  }

 private:
  void CheckEcryptfsMount(const std::string& username, bool expect_present) {
    const std::string obfuscated_username =
        brillo::cryptohome::home::SanitizeUserName(username);
    const base::FilePath ecryptfs_vault =
        GetEcryptfsUserVaultPath(obfuscated_username);
    const base::FilePath ecryptfs_mount_point =
        GetUserMountDirectory(obfuscated_username);
    const std::multimap<const base::FilePath, const base::FilePath>
        expected_ecryptfs_mount_map{
            {ecryptfs_vault, ecryptfs_mount_point},
        };
    std::multimap<const base::FilePath, const base::FilePath>
        ecryptfs_mount_map;
    ASSERT_THAT(platform_.IsDirectoryMounted(ecryptfs_mount_point),
                expect_present);
    if (expect_present) {
      ASSERT_THAT(platform_.DirectoryExists(ecryptfs_mount_point),
                  expect_present);
      ASSERT_TRUE(platform_.GetMountsBySourcePrefix(ecryptfs_vault,
                                                    &ecryptfs_mount_map));
      ASSERT_THAT(ecryptfs_mount_map, ::testing::UnorderedElementsAreArray(
                                          expected_ecryptfs_mount_map));
    }
  }

  void CheckDircryptoMount(const std::string& username, bool expect_present) {
    const std::string obfuscated_username =
        brillo::cryptohome::home::SanitizeUserName(username);
    const base::FilePath dircrypto_mount_point =
        GetUserMountDirectory(obfuscated_username);
    if (expect_present) {
      ASSERT_THAT(platform_.DirectoryExists(dircrypto_mount_point),
                  expect_present);
    }
  }

  void CheckDmcryptMount(const std::string& username, bool expect_present) {
    const base::FilePath kDevMapperPath(kDeviceMapperDir);
    const std::string obfuscated_username =
        brillo::cryptohome::home::SanitizeUserName(username);
    const std::multimap<const base::FilePath, const base::FilePath>
        expected_volume_mount_map{
            {GetDmcryptDataVolume(obfuscated_username),
             GetUserMountDirectory(obfuscated_username)},
            {GetDmcryptCacheVolume(obfuscated_username),
             GetDmcryptUserCacheDirectory(obfuscated_username)},
        };
    const std::multimap<const base::FilePath, const base::FilePath>
        expected_cache_mount_map{
            {GetDmcryptUserCacheDirectory(obfuscated_username)
                 .Append("user")
                 .Append(kCacheDir),
             GetUserMountDirectory(obfuscated_username)
                 .Append("user")
                 .Append(kCacheDir)},
            {GetDmcryptUserCacheDirectory(obfuscated_username)
                 .Append("user")
                 .Append(kGCacheDir),
             GetUserMountDirectory(obfuscated_username)
                 .Append("user")
                 .Append(kGCacheDir)},
        };
    std::multimap<const base::FilePath, const base::FilePath> volume_mount_map;
    std::multimap<const base::FilePath, const base::FilePath> cache_mount_map;
    ASSERT_THAT(platform_.IsDirectoryMounted(
                    GetUserMountDirectory(obfuscated_username)),
                expect_present);
    ASSERT_THAT(platform_.IsDirectoryMounted(
                    GetDmcryptUserCacheDirectory(obfuscated_username)),
                expect_present);
    ASSERT_THAT(
        platform_.IsDirectoryMounted(GetUserMountDirectory(obfuscated_username)
                                         .Append("user")
                                         .Append(kCacheDir)),
        expect_present);
    ASSERT_THAT(
        platform_.IsDirectoryMounted(GetUserMountDirectory(obfuscated_username)
                                         .Append("user")
                                         .Append(kGCacheDir)),
        expect_present);
    if (expect_present) {
      ASSERT_TRUE(
          platform_.GetMountsBySourcePrefix(kDevMapperPath, &volume_mount_map));
      ASSERT_THAT(volume_mount_map, ::testing::UnorderedElementsAreArray(
                                        expected_volume_mount_map));
      ASSERT_TRUE(platform_.GetMountsBySourcePrefix(
          GetDmcryptUserCacheDirectory(obfuscated_username), &cache_mount_map));
      ASSERT_THAT(cache_mount_map, ::testing::UnorderedElementsAreArray(
                                       expected_cache_mount_map));
    }
  }
};

TEST_F(PersistentSystemTest, MountOrdering) {
  // Checks that mounts made with MountAndPush/BindAndPush are undone in the
  // right order. We mock everything here, so we can isolate testing of the
  // ordering only.
  // TODO(dlunev): once mount_helper is refactored, change this test to be able
  // to live within an anonymous namespace.
  SetHomedir(kUser);
  MountHelper mnt_helper(true /*legacy_mount*/, true /* bind_mount_downloads */,
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

namespace {

TEST_F(PersistentSystemTest, BindDownloads) {
  // Make sure that the flag to bind downloads is honoured and the file
  // migration happens to `user/Downloads`.
  const std::string kContent{"some_content"};
  const base::FilePath kFile{"some_file"};
  const FileSystemKeyset keyset = FileSystemKeyset::CreateRandom();

  SetHomedir(kUser);
  MountHelper mnt_helper(true /*legacy_mount*/, true /* bind_mount_downloads */,
                         &platform_);

  ASSERT_THAT(
      mnt_helper.PerformMount(MountType::DIR_CRYPTO, kUser,
                              SecureBlobToHex(keyset.KeyReference().fek_sig),
                              SecureBlobToHex(keyset.KeyReference().fnek_sig),
                              /*is_pristine=*/true),
      Eq(MOUNT_ERROR_NONE));
  VerifyFS(kUser, MountType::DIR_CRYPTO, /*expect_present=*/true);

  mnt_helper.UnmountAll();
  // TODO(dlunev): figure out how to properly abstract the unmount on dircrypto
  // VerifyFS(kUser, MountType::DIR_CRYPTO, /*expect_present=*/false);

  const std::string obfuscated_username =
      brillo::cryptohome::home::SanitizeUserName(kUser);
  const base::FilePath dircrypto_mount_point =
      GetUserMountDirectory(obfuscated_username);

  ASSERT_TRUE(platform_.WriteStringToFile(dircrypto_mount_point.Append("user")
                                              .Append(kMyFilesDir)
                                              .Append(kDownloadsDir)
                                              .Append(kFile),
                                          kContent));

  ASSERT_THAT(
      mnt_helper.PerformMount(MountType::DIR_CRYPTO, kUser,
                              SecureBlobToHex(keyset.KeyReference().fek_sig),
                              SecureBlobToHex(keyset.KeyReference().fnek_sig),
                              /*is_pristine=*/false),
      Eq(MOUNT_ERROR_NONE));
  VerifyFS(kUser, MountType::DIR_CRYPTO, /*expect_present=*/true);

  mnt_helper.UnmountAll();
  // TODO(dlunev): figure out how to properly abstract the unmount on dircrypto
  // VerifyFS(kUser, MountType::DIR_CRYPTO, /*expect_present=*/false);

  // The file should migrate to user/Downloads
  ASSERT_FALSE(platform_.FileExists(dircrypto_mount_point.Append("user")
                                        .Append(kMyFilesDir)
                                        .Append(kDownloadsDir)
                                        .Append(kFile)));
  std::string result;
  ASSERT_TRUE(platform_.ReadFileToString(
      dircrypto_mount_point.Append("user").Append(kDownloadsDir).Append(kFile),
      &result));
  ASSERT_THAT(result, kContent);
}

TEST_F(PersistentSystemTest, NoBindDownloads) {
  // Make sure that the flag to bind downloads is honoured and the file
  // migration happens to `user/MyFiles/Downloads`
  const std::string kContent{"some_content"};
  const base::FilePath kFile{"some_file"};
  const FileSystemKeyset keyset = FileSystemKeyset::CreateRandom();

  SetHomedir(kUser);
  MountHelper mnt_helper(true /*legacy_mount*/,
                         false /* bind_mount_downloads */, &platform_);

  ASSERT_THAT(
      mnt_helper.PerformMount(MountType::DIR_CRYPTO, kUser,
                              SecureBlobToHex(keyset.KeyReference().fek_sig),
                              SecureBlobToHex(keyset.KeyReference().fnek_sig),
                              /*is_pristine=*/true),
      Eq(MOUNT_ERROR_NONE));
  VerifyFS(kUser, MountType::DIR_CRYPTO, /*expect_present=*/true,
           /*downloads_bind_mount=*/false);

  mnt_helper.UnmountAll();
  // TODO(dlunev): figure out how to properly abstract the unmount on dircrypto
  // VerifyFS(kUser, MountType::DIR_CRYPTO, /*expect_present=*/false);

  const std::string obfuscated_username =
      brillo::cryptohome::home::SanitizeUserName(kUser);
  const base::FilePath dircrypto_mount_point =
      GetUserMountDirectory(obfuscated_username);

  ASSERT_TRUE(platform_.WriteStringToFile(
      dircrypto_mount_point.Append("user").Append(kDownloadsDir).Append(kFile),
      kContent));

  ASSERT_THAT(
      mnt_helper.PerformMount(MountType::DIR_CRYPTO, kUser,
                              SecureBlobToHex(keyset.KeyReference().fek_sig),
                              SecureBlobToHex(keyset.KeyReference().fnek_sig),
                              /*is_pristine=*/false),
      Eq(MOUNT_ERROR_NONE));
  VerifyFS(kUser, MountType::DIR_CRYPTO, /*expect_present=*/true,
           /*downloads_bind_mount=*/false);

  mnt_helper.UnmountAll();
  // TODO(dlunev): figure out how to properly abstract the unmount on dircrypto
  // VerifyFS(kUser, MountType::DIR_CRYPTO, /*expect_present=*/false);

  // The file should migrate to user/MyFiles/Downloads
  ASSERT_FALSE(platform_.FileExists(dircrypto_mount_point.Append("user")
                                        .Append(kDownloadsDir)
                                        .Append(kFile)));
  std::string result;
  ASSERT_TRUE(platform_.ReadFileToString(dircrypto_mount_point.Append("user")
                                             .Append(kMyFilesDir)
                                             .Append(kDownloadsDir)
                                             .Append(kFile),
                                         &result));
  ASSERT_THAT(result, kContent);
}

// For Dmcrypt we test only mount part, without container. In fact, we should do
// the same for all and rely on the vault container to setup things properly and
// uniformly.
TEST_F(PersistentSystemTest, Dmcrypt_MountUnmount) {
  const FileSystemKeyset keyset = FileSystemKeyset::CreateRandom();

  SetDmcryptPrereqs(kUser);
  MountHelper mnt_helper(true /*legacy_mount*/, true /* bind_mount_downloads */,
                         &platform_);

  ASSERT_THAT(
      mnt_helper.PerformMount(MountType::DMCRYPT, kUser,
                              SecureBlobToHex(keyset.KeyReference().fek_sig),
                              SecureBlobToHex(keyset.KeyReference().fnek_sig),
                              /*is_prisinte=*/true),
      Eq(MOUNT_ERROR_NONE));
  VerifyFS(kUser, MountType::DMCRYPT, /*expect_present=*/true);

  mnt_helper.UnmountAll();
  VerifyFS(kUser, MountType::DMCRYPT, /*expect_present=*/false);
}

TEST_F(PersistentSystemTest, Ecryptfs_MountPristineTouchFileUnmountMountAgain) {
  // Verify mount and unmount of ecryptfs vault and file preservation.
  const std::string kContent{"some_content"};
  const base::FilePath kFile{"some_file"};
  const FileSystemKeyset keyset = FileSystemKeyset::CreateRandom();
  const Mount::MountArgs args = {
      .create_as_ecryptfs = true,
  };

  MockPreclearKeyring(/*success=*/true);
  MockEcryptfsKeyringSetup(keyset, /*success=*/true);
  ASSERT_THAT(
      mount_->MountCryptohome(kUser, keyset, args, /*is_pristine=*/true),
      Eq(MOUNT_ERROR_NONE));
  VerifyFS(kUser, MountType::ECRYPTFS, /*expect_present=*/true);

  ASSERT_TRUE(platform_.WriteStringToFile(
      base::FilePath(kHomeChronosUser).Append(kFile), kContent));

  MockEcryptfsKeyringTeardown(/*success=*/true);
  ASSERT_TRUE(mount_->UnmountCryptohome());
  VerifyFS(kUser, MountType::ECRYPTFS, /*expect_present=*/false);

  ASSERT_FALSE(
      platform_.FileExists(base::FilePath(kHomeChronosUser).Append(kFile)));

  MockPreclearKeyring(/*success=*/true);
  MockEcryptfsKeyringSetup(keyset, /*success=*/true);
  ASSERT_THAT(mount_->MountCryptohome(kUser, keyset, args,
                                      /*is_pristine=*/false),
              Eq(MOUNT_ERROR_NONE));
  VerifyFS(kUser, MountType::ECRYPTFS, /*expect_present=*/true);

  std::string result;
  ASSERT_TRUE(platform_.ReadFileToString(
      base::FilePath(kHomeChronosUser).Append(kFile), &result));
  ASSERT_THAT(result, kContent);

  MockEcryptfsKeyringTeardown(/*success=*/true);
  ASSERT_TRUE(mount_->UnmountCryptohome());
  VerifyFS(kUser, MountType::ECRYPTFS, /*expect_present=*/false);
}

// TODO(dlunev): Add V2 policy test.
TEST_F(PersistentSystemTest,
       Dircrypto_MountPristineTouchFileUnmountMountAgain) {
  // Verify mount and unmount of fsrypt vault and file preservation.
  const std::string kContent{"some_content"};
  const base::FilePath kFile{"some_file"};
  const FileSystemKeyset keyset = FileSystemKeyset::CreateRandom();
  const Mount::MountArgs args = {
      .force_dircrypto = true,
  };

  MockPreclearKeyring(/*success=*/true);
  MockDircryptoKeyringSetup(kUser, keyset, /*existing_dir=*/false,
                            /*success=*/true);
  ASSERT_THAT(
      mount_->MountCryptohome(kUser, keyset, args, /*is_pristine=*/true),
      Eq(MOUNT_ERROR_NONE));
  VerifyFS(kUser, MountType::DIR_CRYPTO, /*expect_present=*/true);

  ASSERT_TRUE(platform_.WriteStringToFile(
      base::FilePath(kHomeChronosUser).Append(kFile), kContent));

  MockDircryptoKeyringTeardown(kUser, keyset, /*success=*/true);
  ASSERT_TRUE(mount_->UnmountCryptohome());
  // TODO(dlunev): figure out how to properly abstract the unmount on dircrypto
  // VerifyFS(kUser, MountType::DIR_CRYPTO, /*expect_present=*/false);

  // ASSERT_FALSE(
  // platform_.FileExists(base::FilePath(kHomeChronosUser).Append(kFile)));

  MockPreclearKeyring(/*success=*/true);
  MockDircryptoKeyringSetup(kUser, keyset, /*existing_dir=*/true,
                            /*success=*/true);
  ASSERT_THAT(mount_->MountCryptohome(kUser, keyset, args,
                                      /*is_pristine=*/false),
              Eq(MOUNT_ERROR_NONE));
  VerifyFS(kUser, MountType::DIR_CRYPTO, /*expect_present=*/true);

  std::string result;
  ASSERT_TRUE(platform_.ReadFileToString(
      base::FilePath(kHomeChronosUser).Append(kFile), &result));
  ASSERT_THAT(result, kContent);

  MockDircryptoKeyringTeardown(kUser, keyset, /*success=*/true);
  ASSERT_TRUE(mount_->UnmountCryptohome());
  // TODO(dlunev): figure out how to properly abstract the unmount on dircrypto
  // VerifyFS(kUser, MountType::DIR_CRYPTO, /*expect_present=*/false);
}

TEST_F(PersistentSystemTest, NoEcryptfsMountWhenForcedDircrypto) {
  // Verify force_dircrypto flag prohibits ecryptfs mounts.
  const FileSystemKeyset keyset = FileSystemKeyset::CreateRandom();
  MountError error = MOUNT_ERROR_NONE;

  Mount::MountArgs args = {
      .create_as_ecryptfs = true,
  };
  MockPreclearKeyring(/*success=*/true);
  MockEcryptfsKeyringSetup(keyset, /*success=*/true);
  ASSERT_THAT(
      mount_->MountCryptohome(kUser, keyset, args, /*is_pristine=*/true),
      MOUNT_ERROR_NONE)
      << "ERROR: " << error;
  VerifyFS(kUser, MountType::ECRYPTFS, /*expect_present=*/true);

  MockEcryptfsKeyringTeardown(/*success=*/true);
  ASSERT_TRUE(mount_->UnmountCryptohome());
  VerifyFS(kUser, MountType::ECRYPTFS, /*expect_present=*/false);

  args = {
      .force_dircrypto = true,
  };
  ASSERT_THAT(mount_->MountCryptohome(kUser, keyset, args,
                                      /*is_pristine=*/false),
              Eq(MOUNT_ERROR_OLD_ENCRYPTION));
}

TEST_F(PersistentSystemTest, EcryptfsMigration) {
  // Verify ecryptfs->dircrypto migration.
  const std::string kContent{"some_content"};
  const base::FilePath kFile{"some_file"};
  const FileSystemKeyset keyset = FileSystemKeyset::CreateRandom();

  // Create ecryptfs
  Mount::MountArgs args = {
      .create_as_ecryptfs = true,
  };
  MockPreclearKeyring(/*success=*/true);
  MockEcryptfsKeyringSetup(keyset, /*success=*/true);
  ASSERT_THAT(
      mount_->MountCryptohome(kUser, keyset, args, /*is_pristine=*/true),
      MOUNT_ERROR_NONE);

  ASSERT_TRUE(platform_.WriteStringToFile(
      base::FilePath(kHomeChronosUser).Append(kFile), kContent));

  MockEcryptfsKeyringTeardown(/*success=*/true);
  ASSERT_TRUE(mount_->UnmountCryptohome());

  // Start migration
  args = {
      .to_migrate_from_ecryptfs = true,
  };
  MockPreclearKeyring(/*success=*/true);
  MockEcryptfsKeyringSetup(keyset, /*success=*/true);
  MockDircryptoKeyringSetup(kUser, keyset, /*existing_dir=*/false,
                            /*success=*/true);
  ASSERT_THAT(
      mount_->MountCryptohome(kUser, keyset, args, /*is_pristine=*/false),
      MOUNT_ERROR_NONE);

  MockEcryptfsKeyringTeardown(/*success=*/true);
  MockDircryptoKeyringTeardown(kUser, keyset, /*success=*/true);
  ASSERT_TRUE(mount_->UnmountCryptohome());

  // We haven't migrated anything really, so we are in continuation.
  // Create a new mount object, because interface rises a flag prohibiting
  // migration on unmount.
  // TODO(dlunev): fix the behaviour.
  scoped_refptr<Mount> new_mount = new Mount(&platform_, homedirs_.get());
  EXPECT_TRUE(new_mount->Init(/*use_local_mounter=*/true));
  args = {
      .to_migrate_from_ecryptfs = true,
  };
  MockPreclearKeyring(/*success=*/true);
  MockEcryptfsKeyringSetup(keyset, /*success=*/true);
  MockDircryptoKeyringSetup(kUser, keyset, /*existing_dir=*/false,
                            /*success=*/true);
  ASSERT_THAT(
      new_mount->MountCryptohome(kUser, keyset, args, /*is_pristine=*/false),
      MOUNT_ERROR_NONE);

  MockEcryptfsKeyringTeardown(/*success=*/true);
  MockDircryptoKeyringTeardown(kUser, keyset, /*success=*/true);
  ASSERT_TRUE(new_mount->MigrateToDircrypto(
      base::BindRepeating(
          [](const user_data_auth::DircryptoMigrationProgress& unused) {}),
      MigrationType::FULL));
  // TODO(dlunev): figure out how to properly abstract the unmount on dircrypto
  // VerifyFS(kUser, MountType::ECRYPTFS, /*expect_present=*/false);
  // VerifyFS(kUser, MountType::DIR_CRYPTO, /*expect_present=*/false);

  // "vault" should be gone.
  const std::string obfuscated_username =
      brillo::cryptohome::home::SanitizeUserName(kUser);
  const base::FilePath ecryptfs_vault =
      GetEcryptfsUserVaultPath(obfuscated_username);
  ASSERT_FALSE(platform_.DirectoryExists(ecryptfs_vault));

  // Now we should be able to mount with dircrypto.
  args = {
      .force_dircrypto = true,
  };
  MockPreclearKeyring(/*success=*/true);
  MockDircryptoKeyringSetup(kUser, keyset, /*existing_dir=*/true,
                            /*success=*/true);
  ASSERT_THAT(
      mount_->MountCryptohome(kUser, keyset, args, /*is_pristine=*/false),
      MOUNT_ERROR_NONE);
  VerifyFS(kUser, MountType::DIR_CRYPTO, /*expect_present=*/true);

  std::string result;
  ASSERT_TRUE(platform_.ReadFileToString(
      base::FilePath(kHomeChronosUser).Append(kFile), &result));
  ASSERT_THAT(result, kContent);

  MockDircryptoKeyringTeardown(kUser, keyset, /*success=*/true);
  ASSERT_TRUE(mount_->UnmountCryptohome());
  // TODO(dlunev): figure out how to properly abstract the unmount on dircrypto
  // VerifyFS(kUser, MountType::DIR_CRYPTO, /*expect_present=*/false);
}

}  // namespace

class EphemeralSystemTest : public ::testing::Test {
 public:
  EphemeralSystemTest() : crypto_(&platform_) {}

  void SetUp() {
    ASSERT_NO_FATAL_FAILURE(PrepareDirectoryStructure(&platform_));
    brillo::SecureBlob system_salt;
    InitializeFilesystemLayout(&platform_, &crypto_, &system_salt);
    platform_.GetFake()->SetSystemSaltForLibbrillo(system_salt);

    std::unique_ptr<EncryptedContainerFactory> container_factory =
        std::make_unique<EncryptedContainerFactory>(
            &platform_, std::make_unique<FakeBackingDeviceFactory>(&platform_));
    homedirs_ = std::make_unique<HomeDirs>(
        &platform_, std::make_unique<policy::PolicyProvider>(),
        base::BindRepeating([](const std::string& unused) {}),
        std::make_unique<CryptohomeVaultFactory>(&platform_,
                                                 std::move(container_factory)));

    mount_ = new Mount(&platform_, homedirs_.get());

    EXPECT_TRUE(mount_->Init(/*use_local_mounter=*/true));

    SetupVFSMock();
  }

  void TearDown() { platform_.GetFake()->RemoveSystemSaltForLibbrillo(); }

 protected:
  // Protected for trivial access.
  NiceMock<MockPlatform> platform_;
  Crypto crypto_;
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
                bool expect_present) {
    CheckLoopDev(username, expect_present);
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

  base::FilePath GetLoopDevice() {
    return platform_.GetLoopDeviceManager()
        ->GetAttachedDeviceByName("ephemeral")
        ->GetDevicePath();
  }

 private:
  void CheckLoopDev(const std::string& username,
                    bool expect_present) {
    const base::FilePath ephemeral_backing_file =
        EphemeralBackingFile(username);
    const base::FilePath ephemeral_mount_point = EphemeralMountPoint(username);

    ASSERT_THAT(platform_.FileExists(ephemeral_backing_file), expect_present);
    ASSERT_THAT(platform_.DirectoryExists(ephemeral_mount_point),
                expect_present);
    ASSERT_THAT(platform_.IsDirectoryMounted(ephemeral_mount_point),
                expect_present);
    if (expect_present) {
      const std::multimap<const base::FilePath, const base::FilePath>
          expected_ephemeral_mount_map{
              {GetLoopDevice(), ephemeral_mount_point},
          };
      std::multimap<const base::FilePath, const base::FilePath>
          ephemeral_mount_map;
      ASSERT_TRUE(platform_.GetMountsBySourcePrefix(GetLoopDevice(),
                                                    &ephemeral_mount_map));
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
  EXPECT_CALL(platform_, FormatExt4(Property(&base::FilePath::value,
                                             StartsWith(kDevLoopPrefix)),
                                    _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, SetSELinuxContext(EphemeralMountPoint(kUser), _))
      .WillOnce(Return(true));

  ASSERT_THAT(mount_->MountEphemeralCryptohome(kUser), MOUNT_ERROR_NONE);

  VerifyFS(kUser, /*expect_present=*/true);

  ASSERT_TRUE(mount_->UnmountCryptohome());

  VerifyFS(kUser, /*expect_present=*/false);
}

TEST_F(EphemeralSystemTest, EpmeneralMount_VFSFailure) {
  // Checks the case when ephemeral statvfs call fails.
  ON_CALL(platform_, StatVFS(base::FilePath(kEphemeralCryptohomeDir), _))
      .WillByDefault(Return(false));

  ASSERT_THAT(mount_->MountEphemeralCryptohome(kUser), MOUNT_ERROR_FATAL);

  VerifyFS(kUser, /*expect_present=*/false);
}

TEST_F(EphemeralSystemTest, EphemeralMount_CreateSparseDirFailure) {
  // Checks the case when directory for ephemeral sparse files fails to be
  // created.
  EXPECT_CALL(platform_, CreateDirectory(EphemeralBackingFile(kUser).DirName()))
      .WillOnce(Return(false));

  ASSERT_THAT(mount_->MountEphemeralCryptohome(kUser),
              MOUNT_ERROR_KEYRING_FAILED);

  VerifyFS(kUser, /*expect_present=*/false);
}

TEST_F(EphemeralSystemTest, EphemeralMount_CreateSparseFailure) {
  // Checks the case when ephemeral sparse file fails to create.
  EXPECT_CALL(platform_, CreateSparseFile(EphemeralBackingFile(kUser), _))
      .WillOnce(Return(false));

  ASSERT_THAT(mount_->MountEphemeralCryptohome(kUser),
              MOUNT_ERROR_KEYRING_FAILED);

  VerifyFS(kUser, /*expect_present=*/false);
}

TEST_F(EphemeralSystemTest, EphemeralMount_FormatFailure) {
  // Checks that when ephemeral loop device fails to be formatted, clean up
  // happens appropriately.
  EXPECT_CALL(platform_, FormatExt4(Property(&base::FilePath::value,
                                             StartsWith(kDevLoopPrefix)),
                                    _, _))
      .WillOnce(Return(false));

  ASSERT_THAT(mount_->MountEphemeralCryptohome(kUser),
              MOUNT_ERROR_KEYRING_FAILED);

  VerifyFS(kUser, /*expect_present=*/false);
}

TEST_F(EphemeralSystemTest, EphemeralMount_EnsureUserMountFailure) {
  // Checks that when ephemeral mount fails to ensure mount points, clean up
  // happens appropriately.
  EXPECT_CALL(platform_, FormatExt4(Property(&base::FilePath::value,
                                             StartsWith(kDevLoopPrefix)),
                                    _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, Mount(Property(&base::FilePath::value,
                                        StartsWith(kDevLoopPrefix)),
                               EphemeralMountPoint(kUser), _, _, _))
      .WillOnce(Return(false));

  ASSERT_THAT(mount_->MountEphemeralCryptohome(kUser), MOUNT_ERROR_FATAL);

  VerifyFS(kUser, /*expect_present=*/false);
}

}  // namespace

}  // namespace cryptohome
