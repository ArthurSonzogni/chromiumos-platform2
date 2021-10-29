// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Mount - class for managing cryptohome user keys and mounts.  In Chrome OS,
// users are managed on top of a shared unix user, chronos.  When a user logs
// in, cryptohome mounts their encrypted home directory to /home/chronos/user,
// and Chrome does a profile switch to that directory.  All user data in their
// home directory is transparently encrypted, providing protection against
// offline theft.  On logout, the mount point is removed.

#ifndef CRYPTOHOME_STORAGE_MOUNT_H_
#define CRYPTOHOME_STORAGE_MOUNT_H_

#include <memory>
#include <string>
#include <vector>

#include <base/callback.h>
#include <base/files/file_path.h>
#include <base/macros.h>
#include <base/memory/ref_counted.h>
#include <base/synchronization/condition_variable.h>
#include <base/synchronization/lock.h>
#include <base/time/time.h>
#include <base/values.h>
#include <brillo/secure_blob.h>
#include <chromeos/dbus/service_constants.h>
#include <gtest/gtest_prod.h>
#include <policy/device_policy.h>
#include <policy/libpolicy.h>

#include "cryptohome/dircrypto_data_migrator/migration_helper.h"
#include "cryptohome/migration_type.h"
#include "cryptohome/platform.h"
#include "cryptohome/storage/cryptohome_vault.h"
#include "cryptohome/storage/file_system_keyset.h"
#include "cryptohome/storage/homedirs.h"
#include "cryptohome/storage/mount_constants.h"
#include "cryptohome/storage/mount_helper.h"
#include "cryptohome/storage/out_of_process_mount_helper.h"
#include "cryptohome/vault_keyset.h"
#include "cryptohome/vault_keyset.pb.h"

namespace cryptohome {

class ChapsClientFactory;

// The Mount class handles mounting/unmounting of the user's cryptohome
// directory.
class Mount : public base::RefCountedThreadSafe<Mount> {
 public:
  // Called before mount cryptohome.
  using PreMountCallback = base::RepeatingCallback<void()>;

  struct MountArgs {
    bool create_if_missing = false;
    // Whether the mount has to be ephemeral.
    bool is_ephemeral = false;
    // When creating a new cryptohome from scratch, use ecryptfs.
    bool create_as_ecryptfs = false;
    // Forces dircrypto, i.e., makes it an error to mount ecryptfs.
    bool force_dircrypto = false;
    // Enables version 2 fscrypt interface.
    bool enable_dircrypto_v2 = false;
    // Mount the existing ecryptfs vault to a temporary location while setting
    // up a new dircrypto directory.
    bool to_migrate_from_ecryptfs = false;
  };

  // Sets up Mount with the default locations, username, etc., as defined above.
  Mount();
  Mount(Platform* platform, HomeDirs* homedirs);
  Mount(const Mount&) = delete;
  Mount& operator=(const Mount&) = delete;

  virtual ~Mount();

  // Gets the uid/gid of the default user.
  virtual bool Init(bool use_init_namespace = false);

  // Attempts to mount the cryptohome for the given username
  //
  // Parameters
  //   username - name of the user to mount
  //   file_system_keys - file system encryption keys of the user
  //   mount_args - The options for the call to mount:
  //                * Whether to create the cryptohome if it doesn't exist.
  //                * Whether to ensure that the mount is ephemeral.
  //   is_pristine - Whether it is the first mount of the vault.
  //   error - The specific error condition on failure
  virtual bool MountCryptohome(const std::string& username,
                               const FileSystemKeyset& file_system_keys,
                               const MountArgs& mount_args,
                               bool is_pristine,
                               MountError* error);

  // Attempts to mount an ephemeral cryptohome for the given username.
  //
  // Parameters
  //   username - name of the user to mount
  virtual MountError MountEphemeralCryptohome(const std::string& username);

  // Unmounts any mount at the cryptohome mount point
  virtual bool UnmountCryptohome();

  // Checks whether the mount point currently has a cryptohome mounted for the
  // current user.
  virtual bool IsMounted() const;

  // Returns true if the mount is ephemeral;
  virtual bool IsEphemeral() const;

  // Checks whether the mount point currently has a cryptohome mounted for the
  // current user that is not ephemeral.
  //
  virtual bool IsNonEphemeralMounted() const;

  // Get the HomeDirs instance
  virtual HomeDirs* homedirs() { return homedirs_; }

  // Returns associated platform object
  virtual Platform* platform() { return platform_; }

  virtual const base::FilePath& mount_point() const { return mount_point_; }

  // Return the the mount type as a string.
  virtual std::string GetMountTypeString() const;

  // Returns true if this Mount instances owns the mount path.
  virtual bool OwnsMountPoint(const base::FilePath& path) const;

  // Migrates the data from eCryptfs to dircrypto.
  // Call MountCryptohome with to_migrate_from_ecryptfs beforehand.
  // If |migration_type| is MINIMAL, no progress reporting will be done and only
  // allowlisted paths will be migrated.
  virtual bool MigrateToDircrypto(
      const dircrypto_data_migrator::MigrationHelper::ProgressCallback&
          callback,
      MigrationType migration_type);

  // Cancels the active dircrypto migration if there is, and wait for it to
  // stop.
  void MaybeCancelActiveDircryptoMigrationAndWait();

  void set_legacy_mount(bool legacy) { legacy_mount_ = legacy; }
  void set_bind_mount_downloads(bool bind) { bind_mount_downloads_ = bind; }

 protected:
  friend class ChapsDirectoryTest;

 private:
  // Gets the directory in the shadow root where the user's salt, key, and vault
  // are stored.
  //
  // Parameters
  //   obfuscated_username - Obfuscated username field of the Credentials
  base::FilePath GetUserDirectoryForUser(
      const std::string& obfuscated_username) const;

  // Gets the directory to mount the user's ephemeral cryptohome at.
  //
  // Parameters
  //   obfuscated_username - Obfuscated username field of the credentials.
  base::FilePath GetUserEphemeralMountDirectory(
      const std::string& obfuscated_username) const;

  // Returns the path of a user passthrough inside a vault
  //
  // Parameters
  //   vault - vault path
  base::FilePath VaultPathToUserPath(const base::FilePath& vault) const;

  // Returns the path of a root passthrough inside a vault
  //
  // Parameters
  //   vault - vault path
  base::FilePath VaultPathToRootPath(const base::FilePath& vault) const;

  // Returns the mounted userhome path for ephemeral user
  // (e.g. /home/.shadow/.../ephemeral-mount/user)
  //
  // Parameters
  //   obfuscated_username - Obfuscated username field of the credentials.
  base::FilePath GetMountedEphemeralUserHomePath(
      const std::string& obfuscated_username) const;

  // Returns the mounted roothome path for ephemeral user (
  // e.g. /home/.shadow/.../ephemeral-mount/root)
  //
  // Parameters
  //   obfuscated_username - Obfuscated username field of the credentials.
  base::FilePath GetMountedEphemeralRootHomePath(
      const std::string& obfuscated_username) const;

  // Checks Chaps Directory and makes sure that it has the correct
  // permissions, owner uid and gid. If any of these values are
  // incorrect, the correct values are set. If the directory
  // does not exist, it is created and initialzed with the correct
  // values. If the directory or its attributes cannot be checked,
  // set or created, a fatal error has occured and the function
  // returns false.
  //
  // Parameters
  //   dir - directory to check
  bool SetupChapsDirectory(const base::FilePath& dir);

  // A special of UnmountCryptohome to be called from the migration path.
  void UnmountCryptohomeFromMigration();

  // Return the the mount type as a string.
  MountType GetMountType() const;

  // The uid of the shared user.  Ownership of the user's vault is set to this
  // uid.
  uid_t default_user_;

  // The uid of the chaps user. Ownership of the user's PKCS #11 token directory
  // is set to this uid.
  uid_t chaps_user_;

  // The gid of the shared user.  Ownership of the user's vault is set to this
  // gid.
  gid_t default_group_;

  // The gid of the shared access group.  Ownership of the user's home and
  // Downloads directory to this gid.
  gid_t default_access_group_;

  // The file path to mount cryptohome at.  Defaults to /home/chronos/user
  base::FilePath mount_point_;

  // The platform-specific calls
  Platform* platform_;

  // HomeDirs encapsulates operations on Cryptohomes at rest.
  HomeDirs* homedirs_;

  // Name of the user the mount belongs to.
  std::string username_;

  // Whether to mount the legacy homedir or not (see MountLegacyHome)
  bool legacy_mount_;

  // Whether to bind mount Downloads/.
  bool bind_mount_downloads_;

  dircrypto_data_migrator::MigrationHelper* active_dircrypto_migrator_ =
      nullptr;
  bool is_dircrypto_migration_cancelled_ = false;
  base::Lock active_dircrypto_migrator_lock_;
  base::ConditionVariable dircrypto_migration_stopped_condition_;

  // |mounter_| encapsulates mount(2)/umount(2) operations required to perform
  // and tear down cryptohome mounts. It performs these operations in-process.
  std::unique_ptr<MountHelper> mounter_;
  std::unique_ptr<OutOfProcessMountHelper> out_of_process_mounter_;
  MountHelperInterface* active_mounter_;

  // Represents the user's cryptohome vault.
  std::unique_ptr<CryptohomeVault> user_cryptohome_vault_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_STORAGE_MOUNT_H_
