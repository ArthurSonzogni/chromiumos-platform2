// Copyright 2013 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Unit tests for Mount.

#include "cryptohome/storage/mount.h"

#include <memory>
#include <utility>

#include <base/check.h>
#include <base/check_op.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/functional/bind.h>
#include <base/functional/callback_helpers.h>
#include <base/logging.h>
#include <base/stl_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/time/time.h>
#include <brillo/cryptohome.h>
#include <brillo/process/process_mock.h>
#include <brillo/secure_blob.h>
#include <chromeos/constants/cryptohome.h>
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <libhwsec-foundation/crypto/secure_blob_util.h>
#include <libhwsec-foundation/error/testing_helper.h>
#include <policy/libpolicy.h>

#include "cryptohome/filesystem_layout.h"
#include "cryptohome/mock_keyset_management.h"
#include "cryptohome/mock_platform.h"
#include "cryptohome/storage/encrypted_container/encrypted_container.h"
#include "cryptohome/storage/encrypted_container/fake_backing_device.h"
#include "cryptohome/storage/encrypted_container/fake_encrypted_container_factory.h"
#include "cryptohome/storage/error_test_helpers.h"
#include "cryptohome/storage/file_system_keyset.h"
#include "cryptohome/storage/homedirs.h"
#include "cryptohome/storage/keyring/fake_keyring.h"
#include "cryptohome/storage/mock_mount_helper_interface.h"
#include "cryptohome/storage/mount_constants.h"
#include "cryptohome/username.h"

namespace cryptohome {

using base::FilePath;
using brillo::SecureBlob;

using ::cryptohome::storage::testing::IsError;
using ::hwsec_foundation::error::testing::IsOk;
using ::testing::_;
using ::testing::DoAll;
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

// TODO(hardigoyal, b:290897808): Cleanup the rest of this file, there is
// duplicated mount_helper_unittest.cc. This would require migrating tests
// to just use MountHelper and not Mount+MountHelper.
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

constexpr char kDevLoopPrefix[] = "/dev/loop";

MATCHER_P(DirCryptoReferenceMatcher, reference, "") {
  if (reference.reference != arg.reference) {
    return false;
  }
  if (reference.policy_version != arg.policy_version) {
    return false;
  }
  return true;
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

}  // namespace

// TODO(dlunev): add test ecryptfs blasts "mount".
class PersistentSystemTest : public ::testing::Test {
 public:
  const Username kUser{"someuser"};

  PersistentSystemTest() = default;

  void SetUp() {
    ASSERT_NO_FATAL_FAILURE(PrepareDirectoryStructure(&platform_));
    std::unique_ptr<EncryptedContainerFactory> container_factory =
        std::make_unique<FakeEncryptedContainerFactory>(
            &platform_, std::make_unique<FakeKeyring>());

    vault_factory_ = std::make_unique<CryptohomeVaultFactory>(
        &platform_, std::move(container_factory));
    std::shared_ptr<brillo::LvmCommandRunner> command_runner =
        std::make_shared<brillo::MockLvmCommandRunner>();
    brillo::VolumeGroup vg("STATEFUL", command_runner);
    brillo::Thinpool thinpool("thinpool", "STATEFUL", command_runner);
    vault_factory_->CacheLogicalVolumeObjects(vg, thinpool);

    homedirs_ = std::make_unique<HomeDirs>(
        &platform_, std::make_unique<policy::PolicyProvider>(),
        base::BindRepeating([](const ObfuscatedUsername& unused) {}),
        vault_factory_.get());

    std::unique_ptr<MockMounterHelperInterface> mock_mount_helper_interface =
        std::make_unique<MockMounterHelperInterface>();
    mock_mount_interface_ = mock_mount_helper_interface.get();
    mount_ = new Mount(&platform_, homedirs_.get(),
                       std::move(mock_mount_helper_interface));
  }

 protected:
  // Protected for trivial access.
  NiceMock<MockPlatform> platform_;
  std::unique_ptr<CryptohomeVaultFactory> vault_factory_;
  std::unique_ptr<HomeDirs> homedirs_;
  scoped_refptr<Mount> mount_;
  MockMounterHelperInterface* mock_mount_interface_;

  void MockPreclearKeyring(bool success) {
    EXPECT_CALL(platform_, ClearUserKeyring()).WillOnce(Return(success));
  }

  void MockDircryptoKeyringSetup(const Username& username,
                                 const FileSystemKeyset& keyset,
                                 bool existing_dir,
                                 bool success) {
    const ObfuscatedUsername obfuscated_username =
        brillo::cryptohome::home::SanitizeUserName(username);
    const base::FilePath backing_dir =
        GetUserMountDirectory(obfuscated_username);
    const dircrypto::KeyReference reference = {
        .policy_version = FSCRYPT_POLICY_V1,
        .reference = keyset.KeyReference().fek_sig,
    };
    EXPECT_CALL(platform_, GetDirectoryPolicyVersion(backing_dir))
        .WillRepeatedly(Return(existing_dir ? FSCRYPT_POLICY_V1 : -1));
    EXPECT_CALL(platform_, GetDirCryptoKeyState(ShadowRoot()))
        .WillRepeatedly(Return(dircrypto::KeyState::NO_KEY));
    EXPECT_CALL(platform_, GetDirCryptoKeyState(backing_dir))
        .WillRepeatedly(Return(
            existing_dir
                ? dircrypto::KeyState::ENCRYPTED
                : dircrypto::KeyState::NO_KEY));  // EXPECT_CALL(platform_,
    // CheckDircryptoKeyIoctlSupport()).WillOnce(Return(true));
    EXPECT_CALL(
        platform_,
        SetDirCryptoKey(backing_dir, DirCryptoReferenceMatcher(reference)))
        .WillOnce(Return(success));
  }

  void SetHomedir(const Username& username) {
    const ObfuscatedUsername obfuscated_username =
        brillo::cryptohome::home::SanitizeUserName(username);
    ASSERT_TRUE(platform_.CreateDirectory(UserPath(obfuscated_username)));
  }

  void SetDmcryptPrereqs(const Username& username) {
    const ObfuscatedUsername obfuscated_username =
        brillo::cryptohome::home::SanitizeUserName(username);
    SetHomedir(username);
    ASSERT_TRUE(
        platform_.TouchFileDurable(GetDmcryptDataVolume(obfuscated_username)));
    ASSERT_TRUE(
        platform_.TouchFileDurable(GetDmcryptCacheVolume(obfuscated_username)));
    ON_CALL(platform_, GetStatefulDevice())
        .WillByDefault(Return(base::FilePath("/dev/somedev")));
    ON_CALL(platform_, GetBlkSize(_, _))
        .WillByDefault(DoAll(SetArgPointee<1>(4096), Return(true)));
    ON_CALL(platform_, UdevAdmSettle(_, _)).WillByDefault(Return(true));
    ON_CALL(platform_, FormatExt4(_, _, _)).WillByDefault(Return(true));
    ON_CALL(platform_, Tune2Fs(_, _)).WillByDefault(Return(true));
  }
};

namespace {

TEST_F(PersistentSystemTest, NoEcryptfsMountWhenForcedDircrypto) {
  // Verify force_dircrypto flag prohibits ecryptfs mounts.
  const FileSystemKeyset keyset = FileSystemKeyset::CreateRandom();
  CryptohomeVault::Options options = {
      .force_type = EncryptedContainerType::kEcryptfs,
  };

  // Essentially, EcryptFS type should be called for mount.
  EXPECT_CALL(*mock_mount_interface_,
              PerformMount(MountType::ECRYPTFS, kUser, _, _))
      .WillOnce(Return(StorageStatus::Ok()));
  ASSERT_THAT(mount_->MountCryptohome(kUser, keyset, options), IsOk());
}

TEST_F(PersistentSystemTest, MigrateAttemptOnUnmountedPath) {
  EXPECT_CALL(*mock_mount_interface_, MountPerformed())
      .WillRepeatedly(Return(false));

  ASSERT_FALSE(mount_->MigrateEncryption(
      base::BindRepeating(
          [](const user_data_auth::DircryptoMigrationProgress& unused) {}),
      MigrationType::FULL));
}

TEST_F(PersistentSystemTest, MigrateEcryptfsToFscrypt) {
  // Verify ecryptfs->dircrypto migration.
  const std::string kContent{"some_content"};
  const base::FilePath kFile{"some_file"};
  const FileSystemKeyset keyset = FileSystemKeyset::CreateRandom();

  // Create ecryptfs
  CryptohomeVault::Options options = {
      .force_type = EncryptedContainerType::kEcryptfs,
  };
  MockPreclearKeyring(/*success=*/true);
  EXPECT_CALL(*mock_mount_interface_,
              PerformMount(MountType::ECRYPTFS, kUser, _, _))
      .WillOnce(Return(StorageStatus::Ok()));
  ASSERT_THAT(mount_->MountCryptohome(kUser, keyset, options), IsOk());

  ASSERT_TRUE(platform_.WriteStringToFile(
      base::FilePath(kHomeChronosUser).Append(kFile), kContent));

  ASSERT_TRUE(mount_->UnmountCryptohome());

  // The vault exists.
  const ObfuscatedUsername obfuscated_username =
      brillo::cryptohome::home::SanitizeUserName(kUser);
  const base::FilePath ecryptfs_vault =
      GetEcryptfsUserVaultPath(obfuscated_username);
  // Vault exists before migration.
  ASSERT_TRUE(platform_.DirectoryExists(ecryptfs_vault));
  // Start migration
  // Create a new mount object, because interface rises a flag prohibiting
  // migration on unmount.
  // TODO(dlunev): fix the behaviour.
  std::unique_ptr<MockMounterHelperInterface> mock_mount_helper_interface =
      std::make_unique<MockMounterHelperInterface>();
  mock_mount_interface_ = mock_mount_helper_interface.get();
  mount_ = new Mount(&platform_, homedirs_.get(),
                     std::move(mock_mount_helper_interface));
  options = {
      .migrate = true,
  };
  MockPreclearKeyring(/*success=*/true);
  MockDircryptoKeyringSetup(kUser, keyset, /*existing_dir=*/false,
                            /*success=*/true);
  EXPECT_CALL(*mock_mount_interface_,
              PerformMount(MountType::ECRYPTFS_TO_DIR_CRYPTO, kUser, _, _))
      .WillOnce(Return(StorageStatus::Ok()));
  ASSERT_THAT(mount_->MountCryptohome(kUser, keyset, options), IsOk());

  EXPECT_CALL(*mock_mount_interface_, IsPathMounted(_)).WillOnce(Return(true));
  EXPECT_CALL(*mock_mount_interface_, MountPerformed())
      .WillRepeatedly(Return(true));

  ASSERT_TRUE(mount_->MigrateEncryption(
      base::BindRepeating(
          [](const user_data_auth::DircryptoMigrationProgress& unused) {}),
      MigrationType::FULL));

  // "vault" should be gone.
  ASSERT_FALSE(platform_.DirectoryExists(ecryptfs_vault));
  const base::FilePath dircrypto_mount_point =
      GetUserMountDirectory(obfuscated_username);
  ASSERT_TRUE(platform_.DirectoryExists(dircrypto_mount_point));

  // Now we should be able to mount with dircrypto.
  options = {
      .force_type = EncryptedContainerType::kFscrypt,
  };
  MockPreclearKeyring(/*success=*/true);
  MockDircryptoKeyringSetup(kUser, keyset, /*existing_dir=*/true,
                            /*success=*/true);
  EXPECT_CALL(*mock_mount_interface_,
              PerformMount(MountType::DIR_CRYPTO, kUser, _, _))
      .WillOnce(Return(StorageStatus::Ok()));
  ASSERT_THAT(mount_->MountCryptohome(kUser, keyset, options), IsOk());

  std::string result;
  ASSERT_TRUE(platform_.ReadFileToString(
      base::FilePath(kHomeChronosUser).Append(kFile), &result));
  ASSERT_THAT(result, kContent);
}

TEST_F(PersistentSystemTest, MigrateEcryptfsToDmcrypt) {
  // Verify ecryptfs->dircrypto migration.
  const std::string kContent{"some_content"};
  const base::FilePath kFile{"some_file"};
  const FileSystemKeyset keyset = FileSystemKeyset::CreateRandom();

  homedirs_->set_lvm_migration_enabled(true);

  // Create ecryptfs
  CryptohomeVault::Options options = {
      .force_type = EncryptedContainerType::kEcryptfs,
  };
  MockPreclearKeyring(/*success=*/true);
  EXPECT_CALL(*mock_mount_interface_,
              PerformMount(MountType::ECRYPTFS, kUser, _, _))
      .WillOnce(Return(StorageStatus::Ok()));
  ASSERT_THAT(mount_->MountCryptohome(kUser, keyset, options), IsOk());

  ASSERT_TRUE(platform_.WriteStringToFile(
      base::FilePath(kHomeChronosUser).Append(kFile), kContent));

  ASSERT_TRUE(mount_->UnmountCryptohome());

  // The vault exists.
  const ObfuscatedUsername obfuscated_username =
      brillo::cryptohome::home::SanitizeUserName(kUser);
  const base::FilePath ecryptfs_vault =
      GetEcryptfsUserVaultPath(obfuscated_username);
  // Vault exists before migration.
  ASSERT_TRUE(platform_.DirectoryExists(ecryptfs_vault));

  // Start migration
  // Create a new mount object, because interface rises a flag prohibiting
  // migration on unmount.
  // TODO(dlunev): fix the behaviour.
  std::unique_ptr<MockMounterHelperInterface> mock_mount_helper_interface =
      std::make_unique<MockMounterHelperInterface>();
  mock_mount_interface_ = mock_mount_helper_interface.get();
  mount_ = new Mount(&platform_, homedirs_.get(),
                     std::move(mock_mount_helper_interface));
  options = {
      .migrate = true,
  };
  MockPreclearKeyring(/*success=*/true);
  SetDmcryptPrereqs(kUser);
  EXPECT_CALL(*mock_mount_interface_,
              PerformMount(MountType::ECRYPTFS_TO_DMCRYPT, kUser, _, _))
      .WillOnce(Return(StorageStatus::Ok()));
  ASSERT_THAT(mount_->MountCryptohome(kUser, keyset, options), IsOk());

  EXPECT_CALL(*mock_mount_interface_, IsPathMounted(_)).WillOnce(Return(true));
  EXPECT_CALL(*mock_mount_interface_, MountPerformed())
      .WillRepeatedly(Return(true));
  ASSERT_TRUE(mount_->MigrateEncryption(
      base::BindRepeating(
          [](const user_data_auth::DircryptoMigrationProgress& unused) {}),
      MigrationType::FULL));

  // "vault" should be gone.
  ASSERT_FALSE(platform_.DirectoryExists(ecryptfs_vault));

  // Now we should be able to mount with dircrypto.
  options = {
      .force_type = EncryptedContainerType::kDmcrypt,
  };
  MockPreclearKeyring(/*success=*/true);
  EXPECT_CALL(*mock_mount_interface_,
              PerformMount(MountType::DMCRYPT, kUser, _, _))
      .WillOnce(Return(StorageStatus::Ok()));

  ASSERT_THAT(mount_->MountCryptohome(kUser, keyset, options), IsOk());

  std::string result;
  ASSERT_TRUE(platform_.ReadFileToString(
      base::FilePath(kHomeChronosUser).Append(kFile), &result));
  ASSERT_THAT(result, kContent);
}

TEST_F(PersistentSystemTest, MigrateFscryptToDmcrypt) {
  // Verify ecryptfs->dircrypto migration.
  const std::string kContent{"some_content"};
  const base::FilePath kFile{"some_file"};
  const FileSystemKeyset keyset = FileSystemKeyset::CreateRandom();

  homedirs_->set_lvm_migration_enabled(true);

  // Create ecryptfs
  CryptohomeVault::Options options = {
      .force_type = EncryptedContainerType::kFscrypt,
  };
  MockPreclearKeyring(/*success=*/true);
  MockDircryptoKeyringSetup(kUser, keyset, /*existing_dir=*/false,
                            /*success=*/true);
  EXPECT_CALL(*mock_mount_interface_,
              PerformMount(MountType::DIR_CRYPTO, kUser, _, _))
      .WillOnce(Return(StorageStatus::Ok()));
  ASSERT_THAT(mount_->MountCryptohome(kUser, keyset, options), IsOk());

  ASSERT_TRUE(platform_.WriteStringToFile(
      base::FilePath(kHomeChronosUser).Append(kFile), kContent));

  ASSERT_TRUE(mount_->UnmountCryptohome());

  // "vault" should be gone.
  const ObfuscatedUsername obfuscated_username =
      brillo::cryptohome::home::SanitizeUserName(kUser);
  const base::FilePath dircrypto_mount_point =
      GetUserMountDirectory(obfuscated_username);
  ASSERT_TRUE(platform_.DirectoryExists(dircrypto_mount_point));
  // Start migration
  // Create a new mount object, because interface rises a flag prohibiting
  // migration on unmount.
  // TODO(dlunev): fix the behaviour.
  std::unique_ptr<MockMounterHelperInterface> mock_mount_helper_interface =
      std::make_unique<MockMounterHelperInterface>();
  mock_mount_interface_ = mock_mount_helper_interface.get();
  mount_ = new Mount(&platform_, homedirs_.get(),
                     std::move(mock_mount_helper_interface));
  options = {
      .migrate = true,
  };
  MockPreclearKeyring(/*success=*/true);
  MockDircryptoKeyringSetup(kUser, keyset, /*existing_dir=*/true,
                            /*success=*/true);
  SetDmcryptPrereqs(kUser);
  EXPECT_CALL(*mock_mount_interface_,
              PerformMount(MountType::DIR_CRYPTO_TO_DMCRYPT, kUser, _, _))
      .WillOnce(Return(StorageStatus::Ok()));
  ASSERT_THAT(mount_->MountCryptohome(kUser, keyset, options), IsOk());

  EXPECT_CALL(*mock_mount_interface_, IsPathMounted(_)).WillOnce(Return(true));
  EXPECT_CALL(*mock_mount_interface_, MountPerformed())
      .WillRepeatedly(Return(true));
  ASSERT_TRUE(mount_->MigrateEncryption(
      base::BindRepeating(
          [](const user_data_auth::DircryptoMigrationProgress& unused) {}),
      MigrationType::FULL));

  // "vault" should be gone.
  ASSERT_FALSE(platform_.DirectoryExists(dircrypto_mount_point));

  // Now we should be able to mount with dircrypto.
  options = {
      .force_type = EncryptedContainerType::kDmcrypt,
  };
  MockPreclearKeyring(/*success=*/true);
  EXPECT_CALL(*mock_mount_interface_,
              PerformMount(MountType::DMCRYPT, kUser, _, _))
      .WillOnce(Return(StorageStatus::Ok()));
  ASSERT_THAT(mount_->MountCryptohome(kUser, keyset, options), IsOk());

  std::string result;
  ASSERT_TRUE(platform_.ReadFileToString(
      base::FilePath(kHomeChronosUser).Append(kFile), &result));
  ASSERT_THAT(result, kContent);
}

}  // namespace

class EphemeralSystemTest : public ::testing::Test {
 public:
  const Username kUser{"someuser"};

  EphemeralSystemTest() = default;

  void SetUp() {
    ASSERT_NO_FATAL_FAILURE(PrepareDirectoryStructure(&platform_));
    std::unique_ptr<EncryptedContainerFactory> container_factory =
        std::make_unique<EncryptedContainerFactory>(
            &platform_, std::make_unique<FakeKeyring>(),
            std::make_unique<FakeBackingDeviceFactory>(&platform_));
    vault_factory_ = std::make_unique<CryptohomeVaultFactory>(
        &platform_, std::move(container_factory));
    homedirs_ = std::make_unique<HomeDirs>(
        &platform_, std::make_unique<policy::PolicyProvider>(),
        base::BindRepeating([](const ObfuscatedUsername& unused) {}),
        vault_factory_.get());
    std::unique_ptr<MockMounterHelperInterface> mock_mount_helper_interface =
        std::make_unique<MockMounterHelperInterface>();
    mock_mount_interface_ = mock_mount_helper_interface.get();
    mount_ = new Mount(&platform_, homedirs_.get(),
                       std::move(mock_mount_helper_interface));
    SetupVFSMock();
    EXPECT_CALL(*mock_mount_interface_, CanPerformEphemeralMount)
        .WillOnce(Return(true));
  }

 protected:
  // Protected for trivial access.
  NiceMock<MockPlatform> platform_;
  std::unique_ptr<CryptohomeVaultFactory> vault_factory_;
  std::unique_ptr<HomeDirs> homedirs_;
  scoped_refptr<Mount> mount_;
  MockMounterHelperInterface* mock_mount_interface_;
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

 private:
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

TEST_F(EphemeralSystemTest, EpmeneralMount_VFSFailure) {
  // Checks the case when ephemeral statvfs call fails.
  ON_CALL(platform_, StatVFS(base::FilePath(kEphemeralCryptohomeDir), _))
      .WillByDefault(Return(false));

  ASSERT_THAT(mount_->MountEphemeralCryptohome(kUser),
              IsError(MOUNT_ERROR_FATAL));
}

TEST_F(EphemeralSystemTest, EphemeralMount_CreateSparseDirFailure) {
  // Checks the case when directory for ephemeral sparse files fails to be
  // created.
  EXPECT_CALL(platform_, CreateDirectory(EphemeralBackingFile(kUser).DirName()))
      .WillOnce(Return(false));

  ASSERT_THAT(mount_->MountEphemeralCryptohome(kUser),
              IsError(MOUNT_ERROR_KEYRING_FAILED));
}

TEST_F(EphemeralSystemTest, EphemeralMount_CreateSparseFailure) {
  // Checks the case when ephemeral sparse file fails to create.
  EXPECT_CALL(platform_, CreateSparseFile(EphemeralBackingFile(kUser), _))
      .WillOnce(Return(false));

  ASSERT_THAT(mount_->MountEphemeralCryptohome(kUser),
              IsError(MOUNT_ERROR_KEYRING_FAILED));
}

TEST_F(EphemeralSystemTest, EphemeralMount_FormatFailure) {
  // Checks that when ephemeral loop device fails to be formatted, clean up
  // happens appropriately.
  EXPECT_CALL(platform_, FormatExt4(Property(&base::FilePath::value,
                                             StartsWith(kDevLoopPrefix)),
                                    _, _))
      .WillOnce(Return(false));

  ASSERT_THAT(mount_->MountEphemeralCryptohome(kUser),
              IsError(MOUNT_ERROR_KEYRING_FAILED));
}

}  // namespace

}  // namespace cryptohome
