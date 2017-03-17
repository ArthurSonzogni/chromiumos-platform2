// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Homedirs - manages the collection of user home directories on disk. When a
// homedir is actually mounted, it becomes a Mount.

#ifndef CRYPTOHOME_HOMEDIRS_H_
#define CRYPTOHOME_HOMEDIRS_H_

#include <stdint.h>

#include <memory>
#include <string>
#include <vector>

#include <base/callback.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/time/time.h>
#include <chaps/token_manager_client.h>
#include <brillo/secure_blob.h>
#include <gtest/gtest_prod.h>
#include <policy/device_policy.h>
#include <policy/libpolicy.h>

#include "cryptohome/crypto.h"
#include "cryptohome/mount_factory.h"
#include "cryptohome/vault_keyset_factory.h"

#include "rpc.pb.h"  // NOLINT(build/include)
#include "vault_keyset.pb.h"  // NOLINT(build/include)

namespace cryptohome {

const int64_t kFreeSpaceThresholdToTriggerCleanup = 1LL << 30;
const int64_t kTargetFreeSpaceAfterCleanup = 2LL << 30;
extern const char kGCacheFilesAttribute[];
extern const char kAndroidCacheFilesAttribute[];
extern const char kTrackedDirectoryNameAttribute[];

class Credentials;
class Platform;
class UserOldestActivityTimestampCache;
class VaultKeyset;

class HomeDirs {
 public:
  HomeDirs();
  virtual ~HomeDirs();

  // Initializes this HomeDirs object. Returns true for success.
  virtual bool Init(Platform* platform, Crypto* crypto,
                    UserOldestActivityTimestampCache *cache);

  // Frees disk space for unused cryptohomes. If the available disk space is
  // below |kFreeSpaceThresholdToTriggerCleanup|, attempts to free space until
  // it goes up to |kTargetFreeSpaceAfterCleanup|. Returns true if there is now
  // at least |kTargetFreeSpaceAfterCleanup|, or false otherwise.
  virtual bool FreeDiskSpace();

  // Return the available disk space in bytes for home directories, or -1 on
  // failure.
  virtual int64_t AmountOfFreeDiskSpace();

  // Removes all cryptohomes owned by anyone other than the owner user (if set),
  // regardless of free disk space.
  virtual void RemoveNonOwnerCryptohomes();

  // Returns the system salt, creating a new one if necessary. If loading the
  // system salt fails, returns false, and blob is unchanged.
  virtual bool GetSystemSalt(brillo::SecureBlob *blob);

  // Returns the owner's obfuscated username.
  virtual bool GetOwner(std::string* owner);
  virtual bool GetPlainOwner(std::string* owner);

  // Returns a list of present keyset indices for an obfuscated username.
  // There is no guarantee the keysets are valid.
  virtual bool GetVaultKeysets(const std::string& obfuscated,
                               std::vector<int>* keysets) const;

  // Outputs a list of present keysets by label for a given credential.
  // There is no guarantee the keysets are valid nor is the ordering guaranteed.
  // Returns true on success, false if no keysets are found.
  virtual bool GetVaultKeysetLabels(const Credentials& credentials,
                                    std::vector<std::string>* labels) const;

  // Returns a VaultKeyset that matches the label in Credentials.
  // If Credentials has no label or if no matching keyset is
  // found, NULL will be returned.
  //
  // The caller DOES take ownership of the returned VaultKeyset pointer.
  // There is no guarantee the keyset is valid.
  virtual VaultKeyset* GetVaultKeyset(const Credentials& creds) const;

  // Removes the cryptohome for the named user.
  virtual bool Remove(const std::string& username);

  // Renames account identified by |account_id_from| to |account_id_to|.
  // This is called when user e-mail is replaced with GaiaId as account
  // identifier.
  virtual bool Rename(const std::string& account_id_from,
                      const std::string& account_id_to);

  // Computes the size of cryptohome for the named user.
  virtual int64_t ComputeSize(const std::string& account_id);

  // Returns true if the supplied Credentials are a valid (username, passkey)
  // pair.
  virtual bool AreCredentialsValid(const Credentials& credentials);

  // Returns true if a path exists for the Credentials (username).
  virtual bool Exists(const Credentials& credentials) const;

  // Returns true if a valid keyset can be decrypted with |creds|.  If true,
  // |vk| will contain the decrypted value. If false, |vk| will contain the
  // last failed keyset attempt.
  virtual bool GetValidKeyset(const Credentials& creds, VaultKeyset* vk);

  // Returns the vault keyset path for the supplied obfuscated username.
  virtual base::FilePath GetVaultKeysetPath(const std::string& obfuscated,
                                            int index) const;

  // Adds a new vault keyset for the user using the |existing_credentials| to
  // unwrap the homedir key and the |new_credentials| to rewrap and persist to
  // disk.  The key index is return in the |index| pointer if the function
  // returns true.  |index| is not modified if the function returns false.
  // |new_data|, when provided, is copied to the key_data of the new keyset.
  // If |new_data| is provided, a best-effort attempt will be made at ensuring
  // key_data().label() is unique.
  // If |clobber| is true and there are no matching, labeled keys, then it does
  // nothing.  If there is an identically labeled key, it will overwrite it.
  virtual CryptohomeErrorCode AddKeyset(
                         const Credentials& existing_credentials,
                         const brillo::SecureBlob& new_passkey,
                         const KeyData* new_data,
                         bool clobber,
                         int* index);


  // Removes the keyset identified by |key_data| if |credentials|
  // has the remove() KeyPrivilege.  The VaultKeyset backing
  // |credentials| may be the same that |key_data| identifies.
  virtual CryptohomeErrorCode RemoveKeyset(const Credentials& credentials,
                                           const KeyData& key_data);

  // Finds and updates the keyset authenticated by |credentials| and
  // applies |changed_data| to the keyset conditionally on if
  // |authorization_signature| is needed and is valid.
  virtual CryptohomeErrorCode UpdateKeyset(
                         const Credentials& credentials,
                         const Key* changed_data,
                         const std::string& authorization_signature);

  // Returns true if the |signature| is valid over the |new_key| given
  // the AuthorizationData specification from |existing_key_data|.
  virtual bool CheckAuthorizationSignature(const KeyData& existing_key_data,
                                           const Key& new_key,
                                           const std::string& signature);

  // Removes the keyset specified by |index| from the list for the user
  // vault identified by its |obfuscated| username.
  // The caller should check credentials if the call is user-sourced.
  // TODO(wad,ellyjones) Determine a better keyset priotization and management
  //                     scheme than just integer indices, like fingerprints.
  virtual bool ForceRemoveKeyset(const std::string& obfuscated, int index);

  // Allows a keyset to be moved to a different index assuming the index can be
  // claimed for a given |obfuscated| username.
  virtual bool MoveKeyset(const std::string& obfuscated, int src, int dst);

  // Migrates the cryptohome for the supplied obfuscated username from the
  // supplied old key to the supplied new key.
  virtual bool Migrate(const Credentials& newcreds,
                       const brillo::SecureBlob& oldkey);

  // Returns the path to the user's chaps token directory.
  virtual base::FilePath GetChapsTokenDir(const std::string& username) const;

  // Returns the path to the user's legacy chaps token directory.
  virtual base::FilePath GetLegacyChapsTokenDir(
      const std::string& username) const;

  // Returns the path to the user's token salt.
  virtual base::FilePath GetChapsTokenSaltPath(
      const std::string& username) const;

  // Accessors. Mostly used for unit testing. These do not take ownership of
  // passed-in pointers.
  // TODO(wad) Should this update default_crypto_.set_platform()?
  void set_platform(Platform *value) { platform_ = value; }
  Platform* platform() { return platform_; }
  void set_shadow_root(const base::FilePath& value) { shadow_root_ = value; }
  const base::FilePath& shadow_root() const { return shadow_root_; }
  void set_enterprise_owned(bool value) { enterprise_owned_ = value; }
  bool enterprise_owned() const { return enterprise_owned_; }
  void set_policy_provider(policy::PolicyProvider* value) {
    policy_provider_ = value;
  }
  policy::PolicyProvider* policy_provider() { return policy_provider_; }
  void set_crypto(Crypto* value) { crypto_ = value; }
  Crypto* crypto() const { return crypto_; }
  void set_mount_factory(MountFactory* value) { mount_factory_ = value; }
  MountFactory* mount_factory() const { return mount_factory_; }
  void set_vault_keyset_factory(VaultKeysetFactory* value) {
    vault_keyset_factory_ = value;
  }
  VaultKeysetFactory* vault_keyset_factory() const {
    return vault_keyset_factory_;
  }

 private:
  base::TimeDelta GetUserInactivityThresholdForRemoval();
  bool AreEphemeralUsersEnabled();
  // Loads the device policy, either by initializing it or reloading the
  // existing one.
  void LoadDevicePolicy();
  // Returns the path of the specified tracked directory (i.e. a directory which
  // we can locate even when without the key).
  bool GetTrackedDirectory(const base::FilePath& user_dir,
                           const base::FilePath& tracked_dir_name,
                           base::FilePath* out);
  // GetTrackedDirectory() implementation for dircrypto.
  bool GetTrackedDirectoryForDirCrypto(
      const base::FilePath& mount_dir,
      const base::FilePath& tracked_dir_name,
      base::FilePath* out);
  typedef base::Callback<void(const base::FilePath&)> CryptohomeCallback;
  // Runs the supplied callback for every unmounted cryptohome with the user dir
  // path.
  void DoForEveryUnmountedCryptohome(const CryptohomeCallback& cryptohome_cb);
  // Returns the number of currently-mounted cryptohomes.
  int CountMountedCryptohomes() const;
  // Callback used during RemoveNonOwnerCryptohomes()
  void RemoveNonOwnerCryptohomesCallback(const base::FilePath& user_dir);
  // Callback used during FreeDiskSpace().
  void DeleteCacheCallback(const base::FilePath& user_dir);
  // Finds Drive cache directory.
  bool FindGCacheFilesDir(const base::FilePath& user_dir, base::FilePath* dir);
  // Callback used during FreeDiskSpace().
  void DeleteGCacheTmpCallback(const base::FilePath& user_dir);
  // Callback used during FreeDiskSpace().
  void DeleteAndroidCacheCallback(const base::FilePath& user_dir);
  // Recursively deletes all contents of a directory while leaving the directory
  // itself intact.
  void DeleteDirectoryContents(const base::FilePath& dir);
  // Deletes all directories under the supplied directory whose basename is not
  // the same as the obfuscated owner name.
  void RemoveNonOwnerDirectories(const base::FilePath& prefix);
  // Callback used during FreeDiskSpace() if the timestamp cache is not yet
  // initialized. Loads the last activity timestamp from the vault keyset.
  void AddUserTimestampToCacheCallback(const base::FilePath& user_dir);
  // Loads the serialized vault keyset for the supplied obfuscated username.
  // Returns true for success, false for failure.
  bool LoadVaultKeysetForUser(const std::string& obfuscated_user,
                              int index,
                              VaultKeyset* keyset) const;

  // Takes ownership of the supplied PolicyProvider. Used to avoid leaking mocks
  // in unit tests.
  void own_policy_provider(policy::PolicyProvider* value) {
    default_policy_provider_.reset(value);
    policy_provider_ = value;
  }

  std::unique_ptr<Platform> default_platform_;
  Platform* platform_;
  base::FilePath shadow_root_;
  UserOldestActivityTimestampCache* timestamp_cache_;
  bool enterprise_owned_;
  std::unique_ptr<policy::PolicyProvider> default_policy_provider_;
  policy::PolicyProvider* policy_provider_;
  Crypto* crypto_;
  std::unique_ptr<MountFactory> default_mount_factory_;
  MountFactory* mount_factory_;
  // TODO(wad) Collapse all factories into a single manufacturing plant to save
  //           some pointers.
  std::unique_ptr<VaultKeysetFactory> default_vault_keyset_factory_;
  VaultKeysetFactory* vault_keyset_factory_;
  brillo::SecureBlob system_salt_;
  chaps::TokenManagerClient chaps_client_;

  friend class HomeDirsTest;
  FRIEND_TEST(HomeDirsTest, GetTrackedDirectoryForDirCrypto);

  DISALLOW_COPY_AND_ASSIGN(HomeDirs);
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_HOMEDIRS_H_
