// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Unit tests for Mounter.

#include "cryptohome/namespace_mounter/mounter.h"

#include <map>
#include <memory>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/functional/bind.h>
#include <base/functional/callback_helpers.h>
#include <base/stl_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/time/time.h>
#include <brillo/cryptohome.h>
#include <brillo/secure_blob.h>
#include <chromeos/constants/cryptohome.h>
#include <gtest/gtest.h>
#include <libhwsec-foundation/crypto/secure_blob_util.h>
#include <libhwsec-foundation/error/testing_helper.h>

#include "cryptohome/filesystem_layout.h"
#include "cryptohome/mock_platform.h"
#include "cryptohome/platform.h"
#include "cryptohome/storage/error_test_helpers.h"
#include "cryptohome/storage/file_system_keyset.h"
#include "cryptohome/storage/mount_constants.h"
#include "cryptohome/username.h"

namespace cryptohome {
namespace {

using base::FilePath;
using brillo::SecureBlob;
using hwsec_foundation::SecureBlobToHex;

using ::hwsec_foundation::error::testing::IsOk;
using ::testing::_;
using ::testing::AnyNumber;
using ::testing::DoAll;
using ::testing::Eq;
using ::testing::InSequence;
using ::testing::NiceMock;
using ::testing::Pointee;
using ::testing::Return;
using ::testing::StartsWith;

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
constexpr char kRunDaemonStoreCache[] = "/run/daemon-store-cache";

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

constexpr char kDevLoopPrefix[] = "/dev/loop-1";

MATCHER_P(DirCryptoReferenceMatcher, reference, "") {
  if (reference.reference != arg.reference) {
    return false;
  }
  if (reference.policy_version != arg.policy_version) {
    return false;
  }
  return true;
}

base::FilePath ChronosHashPath(const Username& username) {
  const ObfuscatedUsername obfuscated_username =
      brillo::cryptohome::home::SanitizeUserName(username);
  return base::FilePath(kHomeChronos)
      .Append(base::StringPrintf("u-%s", obfuscated_username->c_str()));
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
      base::FilePath(kRunDaemonStoreCache), 0755, kRootUid, kRootGid));
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
      base::FilePath(kRunDaemonStoreCache).Append(kSomeDaemon)));
  ASSERT_TRUE(platform->CreateDirectory(
      base::FilePath(kRunDaemonStore).Append(kAnotherDaemon)));
  ASSERT_TRUE(platform->CreateDirectory(
      base::FilePath(kRunDaemonStoreCache).Append(kAnotherDaemon)));
}

void CheckExistenceAndPermissions(Platform* platform,
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
                                   const Username& username,
                                   const base::FilePath& vault_mount_point,
                                   bool expect_present) {
  const ObfuscatedUsername obfuscated_username =
      brillo::cryptohome::home::SanitizeUserName(username);
  const std::multimap<const base::FilePath, const base::FilePath>
      expected_root_mount_map{
          {
              vault_mount_point.Append(kRootHomeSuffix),
              vault_mount_point.Append(kRootHomeSuffix),
          },
          {
              vault_mount_point.Append(kRootHomeSuffix),
              brillo::cryptohome::home::GetRootPath(username),
          },
          {
              vault_mount_point.Append(kRootHomeSuffix).Append(kSomeDaemon),
              base::FilePath(kRunDaemonStore)
                  .Append(kSomeDaemon)
                  .Append(*obfuscated_username),
          },
          {
              vault_mount_point.Append(kRootHomeSuffix)
                  .Append(kDaemonStoreCacheDir)
                  .Append(kSomeDaemon),
              base::FilePath(kRunDaemonStoreCache)
                  .Append(kSomeDaemon)
                  .Append(*obfuscated_username),
          },
          {
              vault_mount_point.Append(kRootHomeSuffix).Append(kAnotherDaemon),
              base::FilePath(kRunDaemonStore)
                  .Append(kAnotherDaemon)
                  .Append(*obfuscated_username),
          },
          {
              vault_mount_point.Append(kRootHomeSuffix)
                  .Append(kDaemonStoreCacheDir)
                  .Append(kAnotherDaemon),
              base::FilePath(kRunDaemonStoreCache)
                  .Append(kAnotherDaemon)
                  .Append(*obfuscated_username),
          },
      };
  std::multimap<const base::FilePath, const base::FilePath> root_mount_map;

  ASSERT_THAT(platform->IsDirectoryMounted(
                  brillo::cryptohome::home::GetRootPath(username)),
              expect_present);
  if (expect_present) {
    ASSERT_TRUE(platform->GetMountsBySourcePrefix(
        vault_mount_point.Append(kRootHomeSuffix), &root_mount_map));
    ASSERT_THAT(root_mount_map,
                ::testing::UnorderedElementsAreArray(expected_root_mount_map));
  }
  CheckExistenceAndPermissions(platform,
                               vault_mount_point.Append(kRootHomeSuffix), 01770,
                               kRootUid, kDaemonStoreGid, expect_present);
  CheckExistenceAndPermissions(
      platform, vault_mount_point.Append(kRootHomeSuffix).Append(kSomeDaemon),
      kSomeDaemonAttributes.mode, kSomeDaemonAttributes.uid,
      kSomeDaemonAttributes.gid, expect_present);
  CheckExistenceAndPermissions(
      platform,
      vault_mount_point.Append(kRootHomeSuffix).Append(kAnotherDaemon),
      kAnotherDaemonAttributes.mode, kAnotherDaemonAttributes.uid,
      kAnotherDaemonAttributes.gid, expect_present);

  if (expect_present) {
    // TODO(dlunev): make this directories to go away on unmount.
    ASSERT_THAT(platform->DirectoryExists(base::FilePath(kRunDaemonStore)
                                              .Append(kSomeDaemon)
                                              .Append(*obfuscated_username)),
                expect_present);
    ASSERT_THAT(platform->DirectoryExists(base::FilePath(kRunDaemonStoreCache)
                                              .Append(kSomeDaemon)
                                              .Append(*obfuscated_username)),
                expect_present);
    ASSERT_THAT(platform->DirectoryExists(base::FilePath(kRunDaemonStore)
                                              .Append(kAnotherDaemon)
                                              .Append(*obfuscated_username)),
                expect_present);
    ASSERT_THAT(platform->DirectoryExists(base::FilePath(kRunDaemonStoreCache)
                                              .Append(kAnotherDaemon)
                                              .Append(*obfuscated_username)),
                expect_present);
    CheckExistenceAndPermissions(
        platform, brillo::cryptohome::home::GetRootPath(username), 01770,
        kRootUid, kDaemonStoreGid, expect_present);
  }
}

void CheckUserMountPoints(Platform* platform,
                          const Username& username,
                          const base::FilePath& vault_mount_point,
                          bool expect_present,
                          bool downloads_bind_mount = true) {
  const base::FilePath chronos_hash_user_mount_point =
      ChronosHashPath(username);

  std::multimap<const base::FilePath, const base::FilePath>
      expected_user_mount_map{
          {vault_mount_point.Append(kUserHomeSuffix),
           vault_mount_point.Append(kUserHomeSuffix)},
          {vault_mount_point.Append(kUserHomeSuffix),
           brillo::cryptohome::home::GetUserPath(username)},
          {vault_mount_point.Append(kUserHomeSuffix),
           chronos_hash_user_mount_point},
          {vault_mount_point.Append(kUserHomeSuffix),
           base::FilePath(kHomeChronosUser)},
      };

  if (downloads_bind_mount) {
    expected_user_mount_map.insert(
        {vault_mount_point.Append(kUserHomeSuffix).Append(kDownloadsDir),
         vault_mount_point.Append(kUserHomeSuffix)
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

  ASSERT_THAT(
      platform->IsDirectoryMounted(vault_mount_point.Append(kUserHomeSuffix)
                                       .Append(kMyFilesDir)
                                       .Append(kDownloadsDir)),
      expect_present && downloads_bind_mount);
  if (expect_present) {
    ASSERT_TRUE(platform->GetMountsBySourcePrefix(
        vault_mount_point.Append(kUserHomeSuffix), &user_mount_map));
    ASSERT_THAT(user_mount_map,
                ::testing::UnorderedElementsAreArray(expected_user_mount_map));
  }
}

void CheckUserMountPaths(Platform* platform,
                         const base::FilePath& base_path,
                         bool expect_present,
                         bool downloads_bind_mount) {
  // The path itself.
  // TODO(dlunev): the mount paths should be cleaned up upon unmount.
  if (expect_present) {
    CheckExistenceAndPermissions(platform, base_path, 0750, kChronosUid,
                                 kChronosAccessGid, expect_present);
  }

  // Subdirectories
  if (downloads_bind_mount) {
    CheckExistenceAndPermissions(platform, base_path.Append(kDownloadsDir),
                                 0750, kChronosUid, kChronosAccessGid,
                                 expect_present);
  } else {
    ASSERT_FALSE(platform->DirectoryExists(base_path.Append(kDownloadsDir)));
  }

  CheckExistenceAndPermissions(platform, base_path.Append(kMyFilesDir), 0750,
                               kChronosUid, kChronosAccessGid, expect_present);

  CheckExistenceAndPermissions(
      platform, base_path.Append(kMyFilesDir).Append(kDownloadsDir), 0750,
      kChronosUid, kChronosAccessGid, expect_present);

  CheckExistenceAndPermissions(platform, base_path.Append(kCacheDir), 0700,
                               kChronosUid, kChronosGid, expect_present);

  CheckExistenceAndPermissions(platform, base_path.Append(kGCacheDir), 0750,
                               kChronosUid, kChronosAccessGid, expect_present);

  CheckExistenceAndPermissions(
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
  CheckExistenceAndPermissions(platform, base_path.Append(kDir1), 0750,
                               kChronosUid, kChronosGid, expect_present);
  CheckExistenceAndPermissions(
      platform, base_path.Append(kFile1),
      0750,  // NOT A PART OF THE CONTRACT, SEE TODO ABOVE.
      kChronosUid, kChronosGid, expect_present);
  CheckExistenceAndPermissions(platform, base_path.Append(kDir1Dir2), 0750,
                               kChronosUid, kChronosGid, expect_present);
  CheckExistenceAndPermissions(
      platform, base_path.Append(kDir1File2),
      0750,  // NOT A PART OF THE CONTRACT, SEE TODO ABOVE.
      kChronosUid, kChronosGid, expect_present);
  CheckExistenceAndPermissions(
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

class MounterTest : public ::testing::Test {
 public:
  const Username kUser{"someuser"};

  void SetUp() override {
    ASSERT_NO_FATAL_FAILURE(PrepareDirectoryStructure(&platform_));
    mount_helper_ = std::make_unique<Mounter>(
        true /* legacy_mount */, true /* bind_mount_downloads */, &platform_);
  }

 protected:
  // Protected for trivial access.
  NiceMock<MockPlatform> platform_;
  std::unique_ptr<Mounter> mount_helper_;

  void VerifyFS(const Username& username,
                MountType type,
                bool expect_present,
                bool downloads_bind_mount) {
    const ObfuscatedUsername obfuscated_username =
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
        GetUserMountDirectory(obfuscated_username).Append(kUserHomeSuffix),
        base::FilePath(kHomeChronosUser),
        brillo::cryptohome::home::GetUserPath(username),
        ChronosHashPath(username),
    };

    for (const auto& base_path : user_vault_and_mounts) {
      ASSERT_NO_FATAL_FAILURE(CheckUserMountPaths(
          &platform_, base_path, expect_present, downloads_bind_mount));
      ASSERT_NO_FATAL_FAILURE(CheckSkel(&platform_, base_path, expect_present));
    }

    if (type == MountType::DIR_CRYPTO && expect_present) {
      CheckTrackingXattr(username, downloads_bind_mount);
    }
  }

  void SetHomedir(const Username& username) {
    const ObfuscatedUsername obfuscated_username =
        brillo::cryptohome::home::SanitizeUserName(username);
    ASSERT_TRUE(platform_.CreateDirectory(UserPath(obfuscated_username)));
  }

 private:
  void CheckEcryptfsMount(const Username& username, bool expect_present) {
    const ObfuscatedUsername obfuscated_username =
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

  void CheckDircryptoMount(const Username& username, bool expect_present) {
    const ObfuscatedUsername obfuscated_username =
        brillo::cryptohome::home::SanitizeUserName(username);
    const base::FilePath dircrypto_mount_point =
        GetUserMountDirectory(obfuscated_username);
    if (expect_present) {
      ASSERT_THAT(platform_.DirectoryExists(dircrypto_mount_point),
                  expect_present);
    }
  }

  void CheckDmcryptMount(const Username& username, bool expect_present) {
    const base::FilePath kDevMapperPath(kDeviceMapperDir);
    const ObfuscatedUsername obfuscated_username =
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
                 .Append(kUserHomeSuffix)
                 .Append(kCacheDir),
             GetUserMountDirectory(obfuscated_username)
                 .Append(kUserHomeSuffix)
                 .Append(kCacheDir)},
            {GetDmcryptUserCacheDirectory(obfuscated_username)
                 .Append(kUserHomeSuffix)
                 .Append(kGCacheDir),
             GetUserMountDirectory(obfuscated_username)
                 .Append(kUserHomeSuffix)
                 .Append(kGCacheDir)},
            {GetDmcryptUserCacheDirectory(obfuscated_username)
                 .Append(kRootHomeSuffix)
                 .Append(kDaemonStoreCacheDir),
             GetUserMountDirectory(obfuscated_username)
                 .Append(kRootHomeSuffix)
                 .Append(kDaemonStoreCacheDir)},
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
                                         .Append(kUserHomeSuffix)
                                         .Append(kCacheDir)),
        expect_present);
    ASSERT_THAT(
        platform_.IsDirectoryMounted(GetUserMountDirectory(obfuscated_username)
                                         .Append(kUserHomeSuffix)
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

  void CheckTrackingXattr(const Username& username, bool downloads_bind_mount) {
    const ObfuscatedUsername obfuscated_username =
        brillo::cryptohome::home::SanitizeUserName(username);
    const base::FilePath mount_point =
        GetUserMountDirectory(obfuscated_username);

    std::string result;
    ASSERT_TRUE(platform_.GetExtendedFileAttributeAsString(
        mount_point.Append(kRootHomeSuffix), kTrackedDirectoryNameAttribute,
        &result));
    ASSERT_THAT(result, Eq(kRootHomeSuffix));

    ASSERT_TRUE(platform_.GetExtendedFileAttributeAsString(
        mount_point.Append(kUserHomeSuffix), kTrackedDirectoryNameAttribute,
        &result));
    ASSERT_THAT(result, Eq(kUserHomeSuffix));

    ASSERT_TRUE(platform_.GetExtendedFileAttributeAsString(
        mount_point.Append(kUserHomeSuffix).Append(kGCacheDir),
        kTrackedDirectoryNameAttribute, &result));
    ASSERT_THAT(result, Eq(kGCacheDir));

    ASSERT_TRUE(platform_.GetExtendedFileAttributeAsString(
        mount_point.Append(kUserHomeSuffix)
            .Append(kGCacheDir)
            .Append(kGCacheVersion2Dir),
        kTrackedDirectoryNameAttribute, &result));
    ASSERT_THAT(result, Eq(kGCacheVersion2Dir));

    ASSERT_TRUE(platform_.GetExtendedFileAttributeAsString(
        mount_point.Append(kUserHomeSuffix).Append(kCacheDir),
        kTrackedDirectoryNameAttribute, &result));
    ASSERT_THAT(result, Eq(kCacheDir));

    if (downloads_bind_mount) {
      ASSERT_TRUE(platform_.GetExtendedFileAttributeAsString(
          mount_point.Append(kUserHomeSuffix).Append(kDownloadsDir),
          kTrackedDirectoryNameAttribute, &result));
      ASSERT_THAT(result, Eq(kDownloadsDir));
    }

    ASSERT_TRUE(platform_.GetExtendedFileAttributeAsString(
        mount_point.Append(kUserHomeSuffix).Append(kMyFilesDir),
        kTrackedDirectoryNameAttribute, &result));
    ASSERT_THAT(result, Eq(kMyFilesDir));

    ASSERT_TRUE(platform_.GetExtendedFileAttributeAsString(
        mount_point.Append(kUserHomeSuffix)
            .Append(kMyFilesDir)
            .Append(kDownloadsDir),
        kTrackedDirectoryNameAttribute, &result));
    ASSERT_THAT(result, Eq(kDownloadsDir));
  }
};

TEST_F(MounterTest, MountOrdering) {
  // Checks that mounts made with MountAndPush/BindAndPush are undone in the
  // right order. We mock everything here, so we can isolate testing of the
  // ordering only.
  // TODO(dlunev): once mount_helper is refactored, change this test to be able
  // to live within an anonymous namespace.
  SetHomedir(kUser);
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

    EXPECT_TRUE(mount_helper_->MountAndPush(src, dest0, "", ""));
    EXPECT_TRUE(mount_helper_->BindAndPush(src, dest1, RemountOption::kShared));
    EXPECT_TRUE(mount_helper_->MountAndPush(src, dest2, "", ""));
    mount_helper_->UnmountAll();
  }
}

namespace {

TEST_F(MounterTest, BindDownloads) {
  // Make sure that the flag to bind downloads is honoured and the file
  // migration happens to `user/Downloads`.
  const std::string kContent{"some_content"};
  const base::FilePath kFile{"some_file"};
  const FileSystemKeyset keyset = FileSystemKeyset::CreateRandom();

  SetHomedir(kUser);

  ASSERT_THAT(mount_helper_->PerformMount(
                  MountType::DIR_CRYPTO, kUser,
                  SecureBlobToHex(keyset.KeyReference().fek_sig),
                  SecureBlobToHex(keyset.KeyReference().fnek_sig)),
              IsOk());
  VerifyFS(kUser, MountType::DIR_CRYPTO, /*expect_present=*/true,
           /*downloads_bind_mount=*/true);

  mount_helper_->UnmountAll();
  // TODO(dlunev): figure out how to properly abstract the unmount on dircrypto
  // VerifyFS(kUser, MountType::DIR_CRYPTO, /*expect_present=*/false);

  const ObfuscatedUsername obfuscated_username =
      brillo::cryptohome::home::SanitizeUserName(kUser);
  const base::FilePath dircrypto_mount_point =
      GetUserMountDirectory(obfuscated_username);

  ASSERT_TRUE(
      platform_.WriteStringToFile(dircrypto_mount_point.Append(kUserHomeSuffix)
                                      .Append(kMyFilesDir)
                                      .Append(kDownloadsDir)
                                      .Append(kFile),
                                  kContent));

  ASSERT_THAT(mount_helper_->PerformMount(
                  MountType::DIR_CRYPTO, kUser,
                  SecureBlobToHex(keyset.KeyReference().fek_sig),
                  SecureBlobToHex(keyset.KeyReference().fnek_sig)),
              IsOk());
  VerifyFS(kUser, MountType::DIR_CRYPTO, /*expect_present=*/true,
           /*downloads_bind_mount=*/true);

  mount_helper_->UnmountAll();
  // TODO(dlunev): figure out how to properly abstract the unmount on dircrypto
  // VerifyFS(kUser, MountType::DIR_CRYPTO, /*expect_present=*/false);

  // The file should migrate to user/Downloads
  ASSERT_FALSE(
      platform_.FileExists(dircrypto_mount_point.Append(kUserHomeSuffix)
                               .Append(kMyFilesDir)
                               .Append(kDownloadsDir)
                               .Append(kFile)));
  std::string result;
  ASSERT_TRUE(
      platform_.ReadFileToString(dircrypto_mount_point.Append(kUserHomeSuffix)
                                     .Append(kDownloadsDir)
                                     .Append(kFile),
                                 &result));
  ASSERT_THAT(result, kContent);
}

TEST_F(MounterTest, NoBindDownloads) {
  // Make sure that the flag to bind downloads is honoured and the file
  // migration happens to `user/MyFiles/Downloads`
  const std::string kContent{"some_content"};
  const base::FilePath kFile{"some_file"};
  const FileSystemKeyset keyset = FileSystemKeyset::CreateRandom();

  SetHomedir(kUser);
  ASSERT_THAT(mount_helper_->PerformMount(
                  MountType::DIR_CRYPTO, kUser,
                  SecureBlobToHex(keyset.KeyReference().fek_sig),
                  SecureBlobToHex(keyset.KeyReference().fnek_sig)),
              IsOk());
  VerifyFS(kUser, MountType::DIR_CRYPTO, /*expect_present=*/true,
           /*downloads_bind_mount=*/true);

  mount_helper_->UnmountAll();

  const ObfuscatedUsername obfuscated_username =
      brillo::cryptohome::home::SanitizeUserName(kUser);
  const base::FilePath dircrypto_mount_point =
      GetUserMountDirectory(obfuscated_username);

  ASSERT_TRUE(
      platform_.WriteStringToFile(dircrypto_mount_point.Append(kUserHomeSuffix)
                                      .Append(kDownloadsDir)
                                      .Append(kFile),
                                  kContent));

  // Ensure that bind_mount_downloads is false.
  mount_helper_ = std::make_unique<Mounter>(
      true /* legacy_mount */, false /* bind_mount_downloads */, &platform_);

  ASSERT_THAT(mount_helper_->PerformMount(
                  MountType::DIR_CRYPTO, kUser,
                  SecureBlobToHex(keyset.KeyReference().fek_sig),
                  SecureBlobToHex(keyset.KeyReference().fnek_sig)),
              IsOk());
  VerifyFS(kUser, MountType::DIR_CRYPTO, /*expect_present=*/true,
           /*downloads_bind_mount=*/false);

  mount_helper_->UnmountAll();
  // TODO(dlunev): figure out how to properly abstract the unmount on dircrypto
  // VerifyFS(kUser, MountType::DIR_CRYPTO, /*expect_present=*/false);

  // The entire directory under `kDownloadsDir` should be migrated including the
  // test file that was written.
  ASSERT_FALSE(platform_.DirectoryExists(
      dircrypto_mount_point.Append(kUserHomeSuffix).Append(kDownloadsDir)));
  std::string result;
  ASSERT_TRUE(
      platform_.ReadFileToString(dircrypto_mount_point.Append(kUserHomeSuffix)
                                     .Append(kMyFilesDir)
                                     .Append(kDownloadsDir)
                                     .Append(kFile),
                                 &result));
  ASSERT_THAT(result, kContent);
}

TEST_F(MounterTest, IsFirstMountComplete_False) {
  const base::FilePath kSkelFile{"skel_file"};
  const std::string kSkelFileContent{"skel_content"};
  const FileSystemKeyset keyset = FileSystemKeyset::CreateRandom();
  const ObfuscatedUsername obfuscated_username =
      brillo::cryptohome::home::SanitizeUserName(kUser);
  // Ensure that bind_mount_downloads is false.
  mount_helper_ = std::make_unique<Mounter>(
      true /* legacy_mount */, false /* bind_mount_downloads */, &platform_);

  SetHomedir(kUser);
  ASSERT_THAT(mount_helper_->PerformMount(
                  MountType::DIR_CRYPTO, kUser,
                  SecureBlobToHex(keyset.KeyReference().fek_sig),
                  SecureBlobToHex(keyset.KeyReference().fnek_sig)),
              IsOk());
  VerifyFS(kUser, MountType::DIR_CRYPTO, /*expect_present=*/true,
           /*downloads_bind_mount=*/false);

  mount_helper_->UnmountAll();
  // TODO(dlunev): figure out how to properly abstract the unmount on dircrypto
  // VerifyFS(kUser, MountType::DIR_CRYPTO, /*expect_present=*/false);

  // Add a file to skel dir.
  ASSERT_TRUE(platform_.WriteStringToFile(
      base::FilePath(kEtcSkel).Append(kSkelFile), kSkelFileContent));

  // No new files in the vault, so the freshly added skel file should be added.

  ASSERT_THAT(mount_helper_->PerformMount(
                  MountType::DIR_CRYPTO, kUser,
                  SecureBlobToHex(keyset.KeyReference().fek_sig),
                  SecureBlobToHex(keyset.KeyReference().fnek_sig)),
              IsOk());
  VerifyFS(kUser, MountType::DIR_CRYPTO, /*expect_present=*/true,
           /*downloads_bind_mount=*/false);
  ASSERT_TRUE(platform_.FileExists(GetUserMountDirectory(obfuscated_username)
                                       .Append(kUserHomeSuffix)
                                       .Append(kSkelFile)));

  mount_helper_->UnmountAll();
  // TODO(dlunev): figure out how to properly abstract the unmount on dircrypto
  // VerifyFS(kUser, MountType::DIR_CRYPTO, /*expect_present=*/false);
}

TEST_F(MounterTest, Dircrypto_IsFirstMountComplete_True) {
  const base::FilePath kSkelFile{"skel_file"};
  const std::string kSkelFileContent{"skel_content"};
  const base::FilePath kVaultFile{"vault_file"};
  const std::string kVaultFileContent{"vault_content"};
  const FileSystemKeyset keyset = FileSystemKeyset::CreateRandom();
  const ObfuscatedUsername obfuscated_username =
      brillo::cryptohome::home::SanitizeUserName(kUser);
  // Ensure that bind_mount_downloads is false.
  mount_helper_ = std::make_unique<Mounter>(
      true /* legacy_mount */, false /* bind_mount_downloads */, &platform_);

  SetHomedir(kUser);
  ASSERT_THAT(mount_helper_->PerformMount(
                  MountType::DIR_CRYPTO, kUser,
                  SecureBlobToHex(keyset.KeyReference().fek_sig),
                  SecureBlobToHex(keyset.KeyReference().fnek_sig)),
              IsOk());
  VerifyFS(kUser, MountType::DIR_CRYPTO, /*expect_present=*/true,
           /*downloads_bind_mount=*/false);
  // Add a file to vault.
  ASSERT_TRUE(
      platform_.WriteStringToFile(GetUserMountDirectory(obfuscated_username)
                                      .Append(kUserHomeSuffix)
                                      .Append(kVaultFile),
                                  kVaultFileContent));

  mount_helper_->UnmountAll();
  // TODO(dlunev): figure out how to properly abstract the unmount on dircrypto
  // VerifyFS(kUser, MountType::DIR_CRYPTO, /*expect_present=*/false);

  // Add a file to skel dir.
  ASSERT_TRUE(platform_.WriteStringToFile(
      base::FilePath(kEtcSkel).Append(kSkelFile), kSkelFileContent));

  // Ensure that bind_mount_downloads is false.
  mount_helper_ = std::make_unique<Mounter>(
      true /* legacy_mount */, false /* bind_mount_downloads */, &platform_);

  ASSERT_THAT(mount_helper_->PerformMount(
                  MountType::DIR_CRYPTO, kUser,
                  SecureBlobToHex(keyset.KeyReference().fek_sig),
                  SecureBlobToHex(keyset.KeyReference().fnek_sig)),
              IsOk());
  VerifyFS(kUser, MountType::DIR_CRYPTO, /*expect_present=*/true,
           /*downloads_bind_mount=*/false);
  ASSERT_FALSE(platform_.FileExists(GetUserMountDirectory(obfuscated_username)
                                        .Append(kUserHomeSuffix)
                                        .Append(kSkelFile)));

  mount_helper_->UnmountAll();
  // TODO(dlunev): figure out how to properly abstract the unmount on dircrypto
  // VerifyFS(kUser, MountType::DIR_CRYPTO, /*expect_present=*/false);
}

// For Dmcrypt we test only mount part, without container. In fact, we should do
// the same for all and rely on the vault container to setup things properly and
// uniformly.
TEST_F(MounterTest, Dmcrypt_MountUnmount) {
  const FileSystemKeyset keyset = FileSystemKeyset::CreateRandom();

  ASSERT_THAT(mount_helper_->PerformMount(
                  MountType::DMCRYPT, kUser,
                  SecureBlobToHex(keyset.KeyReference().fek_sig),
                  SecureBlobToHex(keyset.KeyReference().fnek_sig)),
              IsOk());
  VerifyFS(kUser, MountType::DMCRYPT, /*expect_present=*/true,
           /*downloads_bind_mount=*/true);

  mount_helper_->UnmountAll();
  VerifyFS(kUser, MountType::DMCRYPT, /*expect_present=*/false,
           /*downloads_bind_mount=*/true);
}

TEST_F(MounterTest, Ecryptfs_IsFirstMountComplete_True) {
  const base::FilePath kSkelFile{"skel_file"};
  const std::string kSkelFileContent{"skel_content"};
  const base::FilePath kVaultFile{"vault_file"};
  const std::string kVaultFileContent{"vault_content"};
  const FileSystemKeyset keyset = FileSystemKeyset::CreateRandom();
  const ObfuscatedUsername obfuscated_username =
      brillo::cryptohome::home::SanitizeUserName(kUser);
  // Ensure that bind_mount_downloads is false.
  mount_helper_ = std::make_unique<Mounter>(
      true /* legacy_mount */, false /* bind_mount_downloads */, &platform_);

  SetHomedir(kUser);
  ASSERT_THAT(mount_helper_->PerformMount(
                  MountType::ECRYPTFS, kUser,
                  SecureBlobToHex(keyset.KeyReference().fek_sig),
                  SecureBlobToHex(keyset.KeyReference().fnek_sig)),
              IsOk());
  VerifyFS(kUser, MountType::ECRYPTFS, /*expect_present=*/true,
           /*downloads_bind_mount=*/false);
  // Add a file to vault.

  auto test_file_path = GetUserMountDirectory(obfuscated_username)
                            .Append(kUserHomeSuffix)
                            .Append(kVaultFile);
  ASSERT_TRUE(platform_.WriteStringToFile(test_file_path, kVaultFileContent));
  mount_helper_->UnmountAll();

  // Add a file to skel dir.
  ASSERT_TRUE(platform_.WriteStringToFile(
      base::FilePath(kEtcSkel).Append(kSkelFile), kSkelFileContent));

  // Ensure that bind_mount_downloads is false.
  mount_helper_ = std::make_unique<Mounter>(
      true /* legacy_mount */, false /* bind_mount_downloads */, &platform_);

  ASSERT_THAT(mount_helper_->PerformMount(
                  MountType::ECRYPTFS, kUser,
                  SecureBlobToHex(keyset.KeyReference().fek_sig),
                  SecureBlobToHex(keyset.KeyReference().fnek_sig)),
              IsOk());
  VerifyFS(kUser, MountType::ECRYPTFS, /*expect_present=*/true,
           /*downloads_bind_mount=*/false);

  std::string result;
  ASSERT_TRUE(platform_.ReadFileToString(test_file_path, &result));
  ASSERT_THAT(result, kVaultFileContent);

  mount_helper_->UnmountAll();
}

class DownloadsBindMountMigrationTest : public MounterTest {
 public:
  void SetUp() override {
    MounterTest::SetUp();

    keyset_ = FileSystemKeyset::CreateRandom();

    const base::FilePath dircrypto_mount_mount =
        GetUserMountDirectory(brillo::cryptohome::home::SanitizeUserName(kUser))
            .Append(kUserHomeSuffix);
    downloads_ = dircrypto_mount_mount.Append(kDownloadsDir);
    downloads_in_my_files_ =
        dircrypto_mount_mount.Append(kMyFilesDir).Append(kDownloadsDir);
    downloads_backup_ = dircrypto_mount_mount.Append(kDownloadsBackupDir);

    SetHomedir(kUser);
  }

  bool CreateTestFileAtPath(const FilePath& path) {
    return platform_.WriteStringToFile(path, kContent);
  }

  bool ExpectFileContentsCorrect(const FilePath& path) {
    std::string result;
    EXPECT_TRUE(platform_.ReadFileToString(path, &result));
    return result == kContent;
  }

  std::string GetMigrationXattr(const FilePath& path) {
    std::string xattr;
    if (!platform_.GetExtendedFileAttributeAsString(
            path, kBindMountMigrationXattrName, &xattr)) {
      return "";
    }
    return xattr;
  }

  bool SetMigrationXattr(const FilePath& path, const std::string& xattr) {
    return platform_.SetExtendedFileAttribute(
        path, kBindMountMigrationXattrName, xattr.c_str(), xattr.size());
  }

  void SetUpAndVerifyUserHome(bool bind_mount_downloads) {
    // Create a mounter that sets up ~/Downloads bind mounted to
    // ~/MyFiles/Downloads and mount it.
    mount_helper_ = std::make_unique<Mounter>(
        /*legacy_mount=*/true, bind_mount_downloads, &platform_);
    EXPECT_THAT(mount_helper_->PerformMount(
                    MountType::DIR_CRYPTO, kUser,
                    SecureBlobToHex(keyset_.KeyReference().fek_sig),
                    SecureBlobToHex(keyset_.KeyReference().fnek_sig)),
                IsOk());

    // Verify that the bind mount was created successfully.
    VerifyFS(kUser, MountType::DIR_CRYPTO, /*expect_present=*/true,
             bind_mount_downloads);
    EXPECT_EQ(platform_.IsDirectoryMounted(downloads_in_my_files_),
              bind_mount_downloads);
  }

 protected:
  base::FilePath downloads_;
  base::FilePath downloads_in_my_files_;
  base::FilePath downloads_backup_;
  FileSystemKeyset keyset_;

  const std::string kContent = "some_content";
};

TEST_F(DownloadsBindMountMigrationTest,
       DownloadsIsMigratedToMyFilesSuccessfully) {
  SetUpAndVerifyUserHome(/*bind_mount_downloads*/ true);

  // Create a test file in ~/Downloads, which we expect to move to
  // ~/MyFiles/Downloads after migration.
  const FilePath test_file_path = downloads_.Append("test_file_name");
  ASSERT_TRUE(CreateTestFileAtPath(test_file_path));

  // Unmount the helper with the file system still in tact.
  mount_helper_->UnmountAll();

  // Mount the user home without a bind mount Downloads.
  SetUpAndVerifyUserHome(/*bind_mount_downloads*/ false);

  // Expect the file has been moved to the new location (not just bind mounted)
  // and the contents match and that the extended attribute has been set to
  // "migrated".
  ASSERT_TRUE(ExpectFileContentsCorrect(
      downloads_in_my_files_.Append(test_file_path.BaseName())));
  EXPECT_EQ(GetMigrationXattr(downloads_in_my_files_), kBindMountMigratedStage);
}

TEST_F(DownloadsBindMountMigrationTest, NewMountSetsXattrOnFirstMount) {
  SetUpAndVerifyUserHome(/*bind_mount_downloads*/ false);

  // Ensure the directory has the right xattr set.
  EXPECT_EQ(GetMigrationXattr(downloads_in_my_files_), kBindMountMigratedStage);
}

TEST_F(DownloadsBindMountMigrationTest,
       MountPreviouslyMigratedButNotUpdatedXattrGetsUpdatedOnNextMount) {
  SetUpAndVerifyUserHome(/*bind_mount_downloads*/ false);

  // Update the xattr on ~/MyFiles/Downloads to be the "migrating" instead of
  // "migrated".
  EXPECT_TRUE(
      SetMigrationXattr(downloads_in_my_files_, kBindMountMigratingStage));

  // Unmount the helper with the file system still in tact, then remount it.
  mount_helper_->UnmountAll();
  ASSERT_THAT(mount_helper_->PerformMount(
                  MountType::DIR_CRYPTO, kUser,
                  SecureBlobToHex(keyset_.KeyReference().fek_sig),
                  SecureBlobToHex(keyset_.KeyReference().fnek_sig)),
              IsOk());

  // Ensure the directory gets the xattr updated.
  EXPECT_EQ(GetMigrationXattr(downloads_in_my_files_), kBindMountMigratedStage);
}

TEST_F(DownloadsBindMountMigrationTest,
       FilesInMyFilesDownloadsShouldBeMovedBeforeMigration) {
  SetUpAndVerifyUserHome(/*bind_mount_downloads*/ true);

  // In the event the ~/MyFiles/Downloads bind mount fails and files are written
  // there, they should be moved prior to migrating ~/Downloads to
  // ~/MyFiles/Downloads.
  const FilePath test_file_path =
      downloads_in_my_files_.Append("test_file_name");
  ASSERT_TRUE(CreateTestFileAtPath(test_file_path));

  // Unmount the helper with the file system still in tact.
  mount_helper_->UnmountAll();

  SetUpAndVerifyUserHome(/*bind_mount_downloads*/ false);

  // Expect the file has been moved to the new location (not just bind mounted)
  // and the contents match and that the extended attribute has been set to
  // "migrated".
  ASSERT_TRUE(ExpectFileContentsCorrect(test_file_path));
  EXPECT_EQ(GetMigrationXattr(downloads_in_my_files_), kBindMountMigratedStage);
}

TEST_F(DownloadsBindMountMigrationTest,
       FailingToCleanUpTheBackupFolderShouldFallbackToBindMount) {
  SetUpAndVerifyUserHome(/*bind_mount_downloads*/ true);

  // Create the backup directory.
  ASSERT_TRUE(platform_.CreateDirectory(downloads_backup_));

  // Unmount the helper with the file system still in tact, reset the helper to
  // setup a new one with the downloads bind mount disabled.
  mount_helper_->UnmountAll();

  // Create a mounter that doesn't bind mount at all and mount it.
  mount_helper_ = std::make_unique<Mounter>(
      /*legacy_mount=*/true, /*bind_mount_downloads=*/false, &platform_);

  // Ignore all other calls to DeletePathRecursively but when the
  // ~/Downloads-backup call is made, return false to mock failing to remove the
  // backup folder.
  EXPECT_CALL(platform_, DeletePathRecursively(_)).Times(AnyNumber());
  EXPECT_CALL(platform_, DeletePathRecursively(downloads_backup_))
      .WillOnce(Return(false));
  ASSERT_THAT(mount_helper_->PerformMount(
                  MountType::DIR_CRYPTO, kUser,
                  SecureBlobToHex(keyset_.KeyReference().fek_sig),
                  SecureBlobToHex(keyset_.KeyReference().fnek_sig)),
              IsOk());

  // Verify that the underlying filesystem has fallen back to bind mounting.
  VerifyFS(kUser, MountType::DIR_CRYPTO, /*expect_present=*/true,
           /*downloads_bind_mount=*/true);
  ASSERT_TRUE(platform_.IsDirectoryMounted(downloads_in_my_files_));
}

TEST_F(DownloadsBindMountMigrationTest,
       FailingToSetTheXattrBeforeMigratingShouldFallback) {
  SetUpAndVerifyUserHome(/*bind_mount_downloads*/ true);

  // Unmount the helper with the file system still in tact, reset the helper to
  // setup a new one with the downloads bind mount disabled.
  mount_helper_->UnmountAll();

  // Create a mounter that doesn't bind mount at all and mount it.
  mount_helper_ = std::make_unique<Mounter>(
      /*legacy_mount=*/true, /*bind_mount_downloads=*/false, &platform_);

  // Ignore all other calls to SetExtendedFileAttribute but when the
  // "migrating" call is made, return false to mock failing to set the xattr.
  EXPECT_CALL(platform_, SetExtendedFileAttribute(_, _, _, _))
      .Times(AnyNumber());
  EXPECT_CALL(platform_, SetExtendedFileAttribute(
                             downloads_, kBindMountMigrationXattrName, _, _))
      .WillOnce(Return(false));
  ASSERT_THAT(mount_helper_->PerformMount(
                  MountType::DIR_CRYPTO, kUser,
                  SecureBlobToHex(keyset_.KeyReference().fek_sig),
                  SecureBlobToHex(keyset_.KeyReference().fnek_sig)),
              IsOk());
  // Verify that the underlying filesystem has fallen back to bind mounting.
  VerifyFS(kUser, MountType::DIR_CRYPTO, /*expect_present=*/true,
           /*downloads_bind_mount=*/true);
  ASSERT_TRUE(platform_.IsDirectoryMounted(downloads_in_my_files_));
}

TEST_F(DownloadsBindMountMigrationTest,
       IfRenamingMyFilesDownloadsToDownloadsBackupFailsFallbackToBindMount) {
  SetUpAndVerifyUserHome(/*bind_mount_downloads*/ true);

  // Unmount the helper with the file system still in tact, reset the helper to
  // setup a new one with the downloads bind mount disabled.
  mount_helper_->UnmountAll();

  // Create a mounter that doesn't bind mount at all.
  mount_helper_ = std::make_unique<Mounter>(
      /*legacy_mount=*/true, /*bind_mount_downloads=*/false, &platform_);

  // Ignore all other calls to Rename but when the ~/Downloads-backup rename
  // call is made, return false to mock a failure.
  EXPECT_CALL(platform_, Rename(_, _)).Times(AnyNumber());
  EXPECT_CALL(platform_, Rename(downloads_in_my_files_, downloads_backup_))
      .WillOnce(Return(false));
  ASSERT_THAT(mount_helper_->PerformMount(
                  MountType::DIR_CRYPTO, kUser,
                  SecureBlobToHex(keyset_.KeyReference().fek_sig),
                  SecureBlobToHex(keyset_.KeyReference().fnek_sig)),
              IsOk());
  // Verify that the underlying filesystem has fallen back to bind mounting.
  VerifyFS(kUser, MountType::DIR_CRYPTO, /*expect_present=*/true,
           /*downloads_bind_mount=*/true);
  ASSERT_TRUE(platform_.IsDirectoryMounted(downloads_in_my_files_));
}

TEST_F(DownloadsBindMountMigrationTest,
       IfRenamingDownloadsToMyFilesFailsTheBackupIsRestored) {
  SetUpAndVerifyUserHome(/*bind_mount_downloads*/ true);

  // Unmount the helper with the file system still in tact, reset the helper to
  // setup a new one with the downloads bind mount disabled.
  mount_helper_->UnmountAll();

  // Create a mounter that doesn't bind mount at all.
  mount_helper_ = std::make_unique<Mounter>(
      /*legacy_mount=*/true, /*bind_mount_downloads=*/false, &platform_);

  // Ignore all other calls to Rename but when the ~/Downloads-backup rename
  // call is made, return false to mock a failure.
  EXPECT_CALL(platform_, Rename(_, _)).Times(AnyNumber());
  EXPECT_CALL(platform_, Rename(downloads_, downloads_in_my_files_))
      .WillOnce(Return(false));
  ASSERT_THAT(mount_helper_->PerformMount(
                  MountType::DIR_CRYPTO, kUser,
                  SecureBlobToHex(keyset_.KeyReference().fek_sig),
                  SecureBlobToHex(keyset_.KeyReference().fnek_sig)),
              IsOk());
  // Verify that the underlying filesystem has fallen back to bind mounting.
  VerifyFS(kUser, MountType::DIR_CRYPTO, /*expect_present=*/true,
           /*downloads_bind_mount=*/true);
  ASSERT_TRUE(platform_.IsDirectoryMounted(downloads_in_my_files_));
}

TEST_F(DownloadsBindMountMigrationTest,
       SettingTheXattrToMigratedFailingShouldNotFallback) {
  SetUpAndVerifyUserHome(/*bind_mount_downloads*/ true);

  // Unmount the helper with the file system still in tact, reset the helper to
  // setup a new one with the downloads bind mount disabled.
  mount_helper_->UnmountAll();

  // Create a mounter that doesn't bind mount at all.
  mount_helper_ = std::make_unique<Mounter>(
      /*legacy_mount=*/true, /*bind_mount_downloads=*/false, &platform_);

  // Ignore all other calls to SetExtendedFileAttribute but when the
  // "migrated" call is made, return false to mock failing to set the xattr.
  EXPECT_CALL(platform_, SetExtendedFileAttribute(_, _, _, _))
      .Times(AnyNumber());
  EXPECT_CALL(platform_,
              SetExtendedFileAttribute(downloads_in_my_files_,
                                       kBindMountMigrationXattrName, _, _))
      .WillOnce(Return(false));
  ASSERT_THAT(mount_helper_->PerformMount(
                  MountType::DIR_CRYPTO, kUser,
                  SecureBlobToHex(keyset_.KeyReference().fek_sig),
                  SecureBlobToHex(keyset_.KeyReference().fnek_sig)),
              IsOk());
  // Verify that the underlying filesystem has not fallen back to bind mounting.
  VerifyFS(kUser, MountType::DIR_CRYPTO, /*expect_present=*/true,
           /*downloads_bind_mount=*/false);
  ASSERT_FALSE(platform_.IsDirectoryMounted(downloads_in_my_files_));
}

TEST_F(
    DownloadsBindMountMigrationTest,
    IfANewDownloadsFolderIsCreatedAfterMigrationItShouldNotRetriggerMigration) {
  SetUpAndVerifyUserHome(/*bind_mount_downloads*/ false);

  // Create a test file in ~/Downloads and expect that neither get moved as the
  // migration has stabilised already.
  ASSERT_TRUE(platform_.CreateDirectory(downloads_));
  const FilePath test_downloads_file_path =
      downloads_.Append("test_downloads_file");
  ASSERT_TRUE(CreateTestFileAtPath(test_downloads_file_path));

  // Create a test file in ~/MyFiles/Downloads and expect that neither get moved
  // as the migration has stabilised already.
  const FilePath test_downloads_in_my_files_file_path =
      downloads_in_my_files_.Append("test_downloads_in_my_files_file");
  ASSERT_TRUE(CreateTestFileAtPath(test_downloads_in_my_files_file_path));

  // Unmount and remount.
  mount_helper_->UnmountAll();
  ASSERT_THAT(mount_helper_->PerformMount(
                  MountType::DIR_CRYPTO, kUser,
                  SecureBlobToHex(keyset_.KeyReference().fek_sig),
                  SecureBlobToHex(keyset_.KeyReference().fnek_sig)),
              IsOk());
  // Verify that ~/MyFiles/Downloads is not mounted and that all the files
  // reside in the correct places, not having been migrated.
  ASSERT_FALSE(platform_.IsDirectoryMounted(downloads_in_my_files_));
  ASSERT_TRUE(ExpectFileContentsCorrect(test_downloads_file_path));
  ASSERT_TRUE(ExpectFileContentsCorrect(test_downloads_in_my_files_file_path));
}

}  // namespace

class MounterEphemeral : public ::testing::Test {
 public:
  const Username kUser{"someuser"};

  MounterEphemeral() = default;

  void SetUp() override {
    ASSERT_NO_FATAL_FAILURE(PrepareDirectoryStructure(&platform_));
    mount_helper_ = std::make_unique<Mounter>(
        true /* legacy_mount */, true /* bind_mount_downloads */, &platform_);
  }

 protected:
  // Protected for trivial access.
  NiceMock<MockPlatform> platform_;
  std::unique_ptr<Mounter> mount_helper_;
  struct statvfs ephemeral_statvfs_;

  base::FilePath EphemeralBackingFile(const Username& username) {
    const ObfuscatedUsername obfuscated_username =
        brillo::cryptohome::home::SanitizeUserName(username);
    return base::FilePath(kEphemeralCryptohomeDir)
        .Append(kSparseFileDir)
        .Append(*obfuscated_username);
  }

  base::FilePath EphemeralMountPoint(const Username& username) {
    const ObfuscatedUsername obfuscated_username =
        brillo::cryptohome::home::SanitizeUserName(username);
    return base::FilePath(kEphemeralCryptohomeDir)
        .Append(kEphemeralMountDir)
        .Append(*obfuscated_username);
  }

  void VerifyFS(const Username& username, bool expect_present) {
    ASSERT_NO_FATAL_FAILURE(CheckRootAndDaemonStoreMounts(
        &platform_, username, EphemeralMountPoint(username), expect_present));
    ASSERT_NO_FATAL_FAILURE(CheckUserMountPoints(
        &platform_, username, EphemeralMountPoint(username), expect_present));

    const std::vector<base::FilePath> user_vault_and_mounts{
        EphemeralMountPoint(username).Append(kUserHomeSuffix),
        base::FilePath(kHomeChronosUser),
        brillo::cryptohome::home::GetUserPath(username),
        ChronosHashPath(username),
    };

    for (const auto& base_path : user_vault_and_mounts) {
      ASSERT_NO_FATAL_FAILURE(
          CheckUserMountPaths(&platform_, base_path, expect_present, true));
      ASSERT_NO_FATAL_FAILURE(CheckSkel(&platform_, base_path, expect_present));
    }
  }
};

namespace {

TEST_F(MounterEphemeral, EphemeralMount) {
  EXPECT_CALL(platform_, SetSELinuxContext(EphemeralMountPoint(kUser), _))
      .WillOnce(Return(true));
  const ObfuscatedUsername obfuscated_username =
      brillo::cryptohome::home::SanitizeUserName(kUser);
  ASSERT_THAT(mount_helper_->PerformEphemeralMount(
                  kUser, base::FilePath(kDevLoopPrefix)),
              IsOk());

  VerifyFS(kUser, /*expect_present=*/true);

  mount_helper_->UnmountAll();

  VerifyFS(kUser, /*expect_present=*/false);
}

TEST_F(MounterEphemeral, EphemeralMount_EnsureUserMountFailure) {
  // Checks that when ephemeral mount fails to ensure mount points.
  EXPECT_CALL(platform_, Mount(Property(&base::FilePath::value,
                                        StartsWith(kDevLoopPrefix)),
                               EphemeralMountPoint(kUser), _, _, _))
      .WillOnce(Return(false));

  ASSERT_THAT(mount_helper_->PerformEphemeralMount(
                  kUser, base::FilePath(kDevLoopPrefix)),
              storage::testing::IsError(MOUNT_ERROR_FATAL));

  VerifyFS(kUser, /*expect_present=*/false);
}

}  // namespace

}  // namespace cryptohome
