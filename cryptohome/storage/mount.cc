// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Contains the implementation of class Mount.

#include "cryptohome/storage/mount.h"

#include <errno.h>
#include <sys/mount.h>
#include <sys/stat.h>

#include <map>
#include <memory>
#include <set>
#include <utility>

#include <base/bind.h>
#include <base/callback_helpers.h>
#include <base/check.h>
#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/hash/sha1.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/threading/platform_thread.h>
#include <brillo/cryptohome.h>
#include <brillo/process/process.h>
#include <brillo/scoped_umask.h>
#include <brillo/secure_blob.h>
#include <chromeos/constants/cryptohome.h>

#include "cryptohome/crypto/secure_blob_util.h"
#include "cryptohome/cryptohome_common.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/dircrypto_data_migrator/migration_helper.h"
#include "cryptohome/dircrypto_util.h"
#include "cryptohome/filesystem_layout.h"
#include "cryptohome/platform.h"
#include "cryptohome/storage/homedirs.h"
#include "cryptohome/storage/mount_utils.h"
#include "cryptohome/tpm.h"
#include "cryptohome/vault_keyset.h"
#include "cryptohome/vault_keyset.pb.h"

using base::FilePath;
using base::StringPrintf;
using brillo::BlobToString;
using brillo::SecureBlob;
using brillo::cryptohome::home::GetRootPath;
using brillo::cryptohome::home::GetUserPath;
using brillo::cryptohome::home::IsSanitizedUserName;
using brillo::cryptohome::home::kGuestUserName;
using brillo::cryptohome::home::SanitizeUserName;

namespace {
constexpr bool __attribute__((unused)) MountUserSessionOOP() {
  return USE_MOUNT_OOP;
}

}  // namespace

namespace cryptohome {

void StartUserFileAttrsCleanerService(cryptohome::Platform* platform,
                                      const std::string& username) {
  std::unique_ptr<brillo::Process> file_attrs =
      platform->CreateProcessInstance();

  file_attrs->AddArg("/sbin/initctl");
  file_attrs->AddArg("start");
  file_attrs->AddArg("--no-wait");
  file_attrs->AddArg("file_attrs_cleaner_tool");
  file_attrs->AddArg(
      base::StringPrintf("OBFUSCATED_USERNAME=%s", username.c_str()));

  if (file_attrs->Run() != 0)
    PLOG(WARNING) << "Error while running file_attrs_cleaner_tool";
}

Mount::Mount(Platform* platform,
             HomeDirs* homedirs,
             bool legacy_mount,
             bool bind_mount_downloads,
             bool use_local_mounter)
    : platform_(platform),
      homedirs_(homedirs),
      legacy_mount_(legacy_mount),
      bind_mount_downloads_(bind_mount_downloads),
      dircrypto_migration_stopped_condition_(&active_dircrypto_migrator_lock_) {
  if (use_local_mounter) {
    active_mounter_.reset(
        new MountHelper(legacy_mount_, bind_mount_downloads_, platform_));
  } else {
    active_mounter_.reset(new OutOfProcessMountHelper(
        legacy_mount_, bind_mount_downloads_, platform_));
  }
}

Mount::Mount() : Mount(nullptr, nullptr) {}

Mount::~Mount() {
  if (IsMounted())
    UnmountCryptohome();
}

MountError Mount::MountEphemeralCryptohome(const std::string& username) {
  username_ = username;
  std::string obfuscated_username = SanitizeUserName(username_);

  base::ScopedClosureRunner cleanup_runner(base::BindOnce(
      base::IgnoreResult(&Mount::UnmountCryptohome), base::Unretained(this)));

  // Ephemeral cryptohome can't be mounted twice.
  CHECK(active_mounter_->CanPerformEphemeralMount());

  user_cryptohome_vault_ = homedirs_->GetVaultFactory()->Generate(
      obfuscated_username, FileSystemKeyReference(),
      EncryptedContainerType::kEphemeral);

  if (!user_cryptohome_vault_) {
    LOG(ERROR) << "Failed to generate ephemeral vault";
    return MOUNT_ERROR_FATAL;
  }

  MountError error = user_cryptohome_vault_->Setup(FileSystemKey());
  if (error != MOUNT_ERROR_NONE) {
    LOG(ERROR) << "Failed to setup ephemeral vault with error=" << error;
    user_cryptohome_vault_.reset();
    return error;
  }

  error = active_mounter_->PerformEphemeralMount(
      username, user_cryptohome_vault_->GetContainerBackingLocation());
  if (error != MOUNT_ERROR_NONE) {
    LOG(ERROR) << "PerformEphemeralMount() failed, aborting ephemeral mount";
    return error;
  }

  ignore_result(cleanup_runner.Release());

  return MOUNT_ERROR_NONE;
}

MountError Mount::MountCryptohome(
    const std::string& username,
    const FileSystemKeyset& file_system_keyset,
    const CryptohomeVault::Options& vault_options) {
  username_ = username;
  std::string obfuscated_username = SanitizeUserName(username_);

  MountError mount_error = MOUNT_ERROR_NONE;
  EncryptedContainerType vault_type = homedirs_->PickVaultType(
      obfuscated_username, vault_options, &mount_error);
  if (mount_error != MOUNT_ERROR_NONE) {
    return mount_error;
  }

  user_cryptohome_vault_ = homedirs_->GetVaultFactory()->Generate(
      obfuscated_username, file_system_keyset.KeyReference(), vault_type,
      homedirs_->KeylockerForStorageEncryptionEnabled());

  if (GetMountType() == MountType::NONE) {
    // TODO(dlunev): there should be a more proper error code set. CREATE_FAILED
    // is a temporary returned error to keep the behaviour unchanged while
    // refactoring.
    return MOUNT_ERROR_CREATE_CRYPTOHOME_FAILED;
  }

  // Set up the cryptohome vault for mount.
  mount_error = user_cryptohome_vault_->Setup(file_system_keyset.Key());
  if (mount_error != MOUNT_ERROR_NONE) {
    return mount_error;
  }

  // Ensure we don't leave any mounts hanging on intermediate errors.
  // The closure won't outlive the class so |this| will always be valid.
  // |active_mounter_| will always be valid since this callback runs in the
  // destructor at the latest.
  base::ScopedClosureRunner cleanup_runner(base::BindOnce(
      base::IgnoreResult(&Mount::UnmountCryptohome), base::Unretained(this)));

  std::string key_signature =
      SecureBlobToHex(file_system_keyset.KeyReference().fek_sig);
  std::string fnek_signature =
      SecureBlobToHex(file_system_keyset.KeyReference().fnek_sig);

  cryptohome::ReportTimerStart(cryptohome::kPerformMountTimer);
  mount_error = active_mounter_->PerformMount(GetMountType(), username_,
                                              key_signature, fnek_signature);
  if (mount_error != MOUNT_ERROR_NONE) {
    LOG(ERROR) << "MountHelper::PerformMount failed, error = " << mount_error;
    return mount_error;
  }

  cryptohome::ReportTimerStop(cryptohome::kPerformMountTimer);

  // Once mount is complete, do a deferred teardown for on the vault.
  // The teardown occurs when the vault's containers has no references ie. no
  // mount holds the containers open.
  // This is useful if cryptohome crashes: on recovery, if cryptohome decides to
  // cleanup mounts, the underlying devices (in case of dm-crypt cryptohome)
  // will be automatically torn down.

  // TODO(sarthakkukreti): remove this in favor of using the session-manager
  // as the source-of-truth during crash recovery. That would allow us to
  // reconstruct the run-time state of cryptohome vault(s) at the time of crash.
  ignore_result(user_cryptohome_vault_->SetLazyTeardownWhenUnused());

  // At this point we're done mounting.
  ignore_result(cleanup_runner.Release());

  user_cryptohome_vault_->ReportVaultEncryptionType();

  // Start file attribute cleaner service.
  StartUserFileAttrsCleanerService(platform_, obfuscated_username);

  // TODO(fqj,b/116072767) Ignore errors since unlabeled files are currently
  // still okay during current development progress.
  // Report the success rate of the restore SELinux context operation for user
  // directory to decide on the action on failure when we  move on to the next
  // phase in the cryptohome SELinux development, i.e. making cryptohome
  // enforcing.
  if (platform_->RestoreSELinuxContexts(
          GetUserDirectoryForUser(obfuscated_username), true /*recursive*/)) {
    ReportRestoreSELinuxContextResultForHomeDir(true);
  } else {
    ReportRestoreSELinuxContextResultForHomeDir(false);
    LOG(ERROR) << "RestoreSELinuxContexts("
               << GetUserDirectoryForUser(obfuscated_username) << ") failed.";
  }

  // TODO(crbug.com/1287022): Remove in M101.
  // Remove the Chrome Logs if they are too large. This is a mitigation for
  // crbug.com/1231192.
  if (!RemoveLargeChromeLogs())
    LOG(ERROR) << "Failed to remove Chrome logs";

  return MOUNT_ERROR_NONE;
}

bool Mount::UnmountCryptohome() {
  // There should be no file access when unmounting.
  // Stop dircrypto migration if in progress.
  MaybeCancelActiveDircryptoMigrationAndWait();

  active_mounter_->UnmountAll();

  // Resetting the vault teardowns the enclosed containers if setup succeeded.
  user_cryptohome_vault_.reset();

  return true;
}

void Mount::UnmountCryptohomeFromMigration() {
  active_mounter_->UnmountAll();

  // Resetting the vault teardowns the enclosed containers if setup succeeded.
  user_cryptohome_vault_.reset();
}

bool Mount::IsMounted() const {
  return active_mounter_ && active_mounter_->MountPerformed();
}

bool Mount::IsEphemeral() const {
  return GetMountType() == MountType::EPHEMERAL;
}

bool Mount::IsNonEphemeralMounted() const {
  return IsMounted() && !IsEphemeral();
}

bool Mount::OwnsMountPoint(const FilePath& path) const {
  return active_mounter_ && active_mounter_->IsPathMounted(path);
}

FilePath Mount::GetUserDirectoryForUser(
    const std::string& obfuscated_username) const {
  return ShadowRoot().Append(obfuscated_username);
}

MountType Mount::GetMountType() const {
  if (!user_cryptohome_vault_) {
    return MountType::NONE;
  }
  return user_cryptohome_vault_->GetMountType();
}

std::string Mount::GetMountTypeString() const {
  switch (GetMountType()) {
    case MountType::NONE:
      return "none";
    case MountType::ECRYPTFS:
      return "ecryptfs";
    case MountType::DIR_CRYPTO:
      return "dircrypto";
    case MountType::ECRYPTFS_TO_DIR_CRYPTO:
      return "ecryptfs-to-dircrypto";
    case MountType::EPHEMERAL:
      return "ephemeral";
    case MountType::DMCRYPT:
      return "dmcrypt";
  }
  return "";
}

bool Mount::MigrateToDircrypto(
    const dircrypto_data_migrator::MigrationHelper::ProgressCallback& callback,
    MigrationType migration_type) {
  std::string obfuscated_username = SanitizeUserName(username_);
  FilePath temporary_mount =
      GetUserTemporaryMountDirectory(obfuscated_username);
  if (!IsMounted() || GetMountType() != MountType::ECRYPTFS_TO_DIR_CRYPTO ||
      !platform_->DirectoryExists(temporary_mount) ||
      !OwnsMountPoint(temporary_mount)) {
    LOG(ERROR) << "Not mounted for eCryptfs->dircrypto migration.";
    return false;
  }
  // Do migration.
  constexpr uint64_t kMaxChunkSize = 128 * 1024 * 1024;
  dircrypto_data_migrator::MigrationHelper migrator(
      platform_, temporary_mount, GetUserMountDirectory(obfuscated_username),
      GetUserDirectoryForUser(obfuscated_username), kMaxChunkSize,
      migration_type);
  {  // Abort if already cancelled.
    base::AutoLock lock(active_dircrypto_migrator_lock_);
    if (is_dircrypto_migration_cancelled_)
      return false;
    CHECK(!active_dircrypto_migrator_);
    active_dircrypto_migrator_ = &migrator;
  }
  bool success = migrator.Migrate(callback);

  UnmountCryptohomeFromMigration();
  {  // Signal the waiting thread.
    base::AutoLock lock(active_dircrypto_migrator_lock_);
    active_dircrypto_migrator_ = nullptr;
    dircrypto_migration_stopped_condition_.Signal();
  }
  if (!success) {
    LOG(ERROR) << "Failed to migrate.";
    return false;
  }
  // Clean up.
  FilePath vault_path = GetEcryptfsUserVaultPath(obfuscated_username);
  if (!platform_->DeletePathRecursively(temporary_mount) ||
      !platform_->DeletePathRecursively(vault_path)) {
    LOG(ERROR) << "Failed to delete the old vault.";
    return false;
  }
  return true;
}

void Mount::MaybeCancelActiveDircryptoMigrationAndWait() {
  base::AutoLock lock(active_dircrypto_migrator_lock_);
  is_dircrypto_migration_cancelled_ = true;
  while (active_dircrypto_migrator_) {
    active_dircrypto_migrator_->Cancel();
    LOG(INFO) << "Waiting for dircrypto migration to stop.";
    dircrypto_migration_stopped_condition_.Wait();
    LOG(INFO) << "Dircrypto migration stopped.";
  }
}

// TODO(crbug.com/1287022): Remove in M101.
// Remove the Chrome Logs if they are too large. This is a mitigation for
// crbug.com/1231192.
bool Mount::RemoveLargeChromeLogs() const {
  base::FilePath path("/home/chronos/user/log/chrome");

  int64_t size;
  if (!platform_->GetFileSize(path, &size)) {
    LOG(ERROR) << "Failed to get the size of Chrome logs";
    return false;
  }

  // Only remove the Chrome logs if they are larger than 200 MiB.
  if (size < 200 * 1024 * 1024) {
    return true;
  }

  return platform_->DeleteFile(path);
}

}  // namespace cryptohome
