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
#include <google/protobuf/util/message_differencer.h>

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
using google::protobuf::util::MessageDifferencer;

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

Mount::Mount(Platform* platform, HomeDirs* homedirs)
    : platform_(platform),
      homedirs_(homedirs),
      legacy_mount_(true),
      bind_mount_downloads_(true),
      dircrypto_migration_stopped_condition_(&active_dircrypto_migrator_lock_) {
}

Mount::Mount() : Mount(nullptr, nullptr) {}

Mount::~Mount() {
  if (IsMounted())
    UnmountCryptohome();
}

bool Mount::Init(bool use_local_mounter) {
  if (use_local_mounter) {
    active_mounter_.reset(
        new MountHelper(legacy_mount_, bind_mount_downloads_, platform_));
    return true;
  }

  active_mounter_.reset(new OutOfProcessMountHelper(
      legacy_mount_, bind_mount_downloads_, platform_));

  return true;
}

MountError Mount::MountEphemeralCryptohome(const std::string& username) {
  username_ = username;
  std::string obfuscated_username = SanitizeUserName(username_);

  base::ScopedClosureRunner cleanup_runner(base::BindOnce(
      base::IgnoreResult(&Mount::UnmountCryptohome), base::Unretained(this)));

  // Ephemeral cryptohome can't be mounted twice.
  CHECK(active_mounter_->CanPerformEphemeralMount());

  MountError error = MOUNT_ERROR_NONE;
  CryptohomeVault::Options vault_options = {
      .force_type = EncryptedContainerType::kEphemeral,
  };

  user_cryptohome_vault_ = homedirs_->GenerateCryptohomeVault(
      obfuscated_username, FileSystemKeyReference(), vault_options,
      /*is_pristine=*/true, &error);

  if (error != MOUNT_ERROR_NONE || !user_cryptohome_vault_) {
    LOG(ERROR) << "Failed to generate ephemeral vault with error=" << error;
    return error != MOUNT_ERROR_NONE ? error : MOUNT_ERROR_FATAL;
  }

  error = user_cryptohome_vault_->Setup(FileSystemKey(), /*create=*/true);
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

MountError Mount::MountCryptohome(const std::string& username,
                                  const FileSystemKeyset& file_system_keyset,
                                  const Mount::MountArgs& mount_args,
                                  bool is_pristine) {
  username_ = username;
  std::string obfuscated_username = SanitizeUserName(username_);

  CryptohomeVault::Options vault_options;
  if (mount_args.force_dircrypto) {
    // If dircrypto is forced, it's an error to mount ecryptfs home unless
    // we are migrating from ecryptfs.
    vault_options.block_ecryptfs = true;
  } else if (mount_args.create_as_ecryptfs) {
    vault_options.force_type = EncryptedContainerType::kEcryptfs;
  }

  vault_options.migrate = mount_args.to_migrate_from_ecryptfs;

  MountError mount_error = MOUNT_ERROR_NONE;
  user_cryptohome_vault_ = homedirs_->GenerateCryptohomeVault(
      obfuscated_username, file_system_keyset.KeyReference(), vault_options,
      is_pristine, &mount_error);
  if (mount_error != MOUNT_ERROR_NONE) {
    return mount_error;
  }

  if (GetMountType() == MountType::NONE) {
    // TODO(dlunev): there should be a more proper error code set. CREATE_FAILED
    // is a temporary returned error to keep the behaviour unchanged while
    // refactoring.
    return MOUNT_ERROR_CREATE_CRYPTOHOME_FAILED;
  }

  // Set up the cryptohome vault for mount.
  mount_error =
      user_cryptohome_vault_->Setup(file_system_keyset.Key(), is_pristine);
  if (mount_error != MOUNT_ERROR_NONE) {
    return mount_error;
  }

  // Ensure we don't leave any mounts hanging on intermediate errors.
  // The closure won't outlive the class so |this| will always be valid.
  // |active_mounter_| will always be valid since this callback runs in the
  // destructor at the latest.
  base::ScopedClosureRunner cleanup_runner(base::BindOnce(
      base::IgnoreResult(&Mount::UnmountCryptohome), base::Unretained(this)));

  // Mount cryptohome
  // /home/.shadow: owned by root
  // /home/.shadow/$hash: owned by root
  // /home/.shadow/$hash/vault: owned by root
  // /home/.shadow/$hash/mount: owned by root
  // /home/.shadow/$hash/mount/root: owned by root
  // /home/.shadow/$hash/mount/user: owned by chronos
  // /home/chronos: owned by chronos
  // /home/chronos/user: owned by chronos
  // /home/user/$hash: owned by chronos
  // /home/root/$hash: owned by root

  std::string key_signature =
      SecureBlobToHex(file_system_keyset.KeyReference().fek_sig);
  std::string fnek_signature =
      SecureBlobToHex(file_system_keyset.KeyReference().fnek_sig);

  MountHelper::Options mount_opts = {GetMountType(),
                                     mount_args.to_migrate_from_ecryptfs};

  cryptohome::ReportTimerStart(cryptohome::kPerformMountTimer);
  mount_error = active_mounter_->PerformMount(
      mount_opts, username_, key_signature, fnek_signature, is_pristine);
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
  if (!IsMounted() || GetMountType() != MountType::DIR_CRYPTO ||
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

}  // namespace cryptohome
