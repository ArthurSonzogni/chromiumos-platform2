// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_KEYSET_MANAGEMENT_H_
#define CRYPTOHOME_KEYSET_MANAGEMENT_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <brillo/secure_blob.h>
#include <cryptohome/proto_bindings/rpc.pb.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>
#include <dbus/cryptohome/dbus-constants.h>

#include "cryptohome/cleanup/user_oldest_activity_timestamp_manager.h"
#include "cryptohome/credentials.h"
#include "cryptohome/crypto.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/error/cryptohome_mount_error.h"
#include "cryptohome/flatbuffer_schemas/auth_block_state.h"
#include "cryptohome/key_objects.h"
#include "cryptohome/platform.h"
#include "cryptohome/storage/homedirs.h"
#include "cryptohome/vault_keyset.h"
#include "cryptohome/vault_keyset_factory.h"
#include "storage/file_system_keyset.h"

namespace cryptohome {

class KeysetManagement {
 public:
  using DecryptVkCallback = base::RepeatingCallback<CryptoStatus(VaultKeyset*)>;
  using EncryptVkCallback =
      base::OnceCallback<CryptohomeStatus(VaultKeyset* vk)>;

  KeysetManagement() = default;
  KeysetManagement(Platform* platform,
                   Crypto* crypto,
                   std::unique_ptr<VaultKeysetFactory> vault_keyset_factory);
  virtual ~KeysetManagement() = default;
  KeysetManagement(const KeysetManagement&) = delete;
  KeysetManagement& operator=(const KeysetManagement&) = delete;

  // Returns a list of present keyset indices for an obfuscated username.
  // There is no guarantee the keysets are valid.
  virtual bool GetVaultKeysets(const std::string& obfuscated,
                               std::vector<int>* keysets) const;

  // Outputs a list of present keysets by label for a given obfuscated username.
  // There is no guarantee the keysets are valid nor is the ordering guaranteed.
  // Returns true on success, false if no keysets are found.
  virtual bool GetVaultKeysetLabels(const std::string& obfuscated_username,
                                    bool include_le_labels,
                                    std::vector<std::string>* labels) const;

  // Outputs a map of present keysets by label and the associate key data for a
  // given obfuscated username. There is no guarantee the keysets are valid nor
  // is the ordering guaranteed. Returns true on success, false if no keysets
  // are found.
  virtual bool GetVaultKeysetLabelsAndData(
      const std::string& obfuscated_username,
      std::map<std::string, KeyData>* key_label_data) const;

  // Returns a VaultKeyset that matches the given obfuscated username and the
  // key label. If the label is empty or if no matching keyset is found, NULL
  // will be returned.
  //
  // The caller DOES take ownership of the returned VaultKeyset pointer.
  // There is no guarantee the keyset is valid.
  virtual std::unique_ptr<VaultKeyset> GetVaultKeyset(
      const std::string& obfuscated_username,
      const std::string& key_label) const;

  // Returns true if the supplied Credentials are a valid (username, passkey)
  // pair.
  virtual bool AreCredentialsValid(const Credentials& credentials);

  // Returns decrypted with |creds| keyset, or an error status with the reasons
  // if none decryptable with the provided |creds| found. NOTE: The LE
  // Credential Keysets are only considered when the key label provided via
  // |creds| is non-empty (b/202907485).
  virtual MountStatusOr<std::unique_ptr<VaultKeyset>> GetValidKeyset(
      const Credentials& creds);

  // Loads the vault keyset for the supplied obfuscated username and index.
  // Returns null on failure.
  virtual std::unique_ptr<VaultKeyset> LoadVaultKeysetForUser(
      const std::string& obfuscated_user, int index) const;

  // Checks if the directory containing user keys exists.
  virtual bool UserExists(const std::string& obfuscated_username);

  // Adds initial keyset for the credentials and wraps the file system keyset
  // provided. Returns the added keyset, or an error status on failure.
  virtual CryptohomeStatusOr<std::unique_ptr<VaultKeyset>> AddInitialKeyset(
      const Credentials& credentials,
      const FileSystemKeyset& file_system_keyset);

  // This function should be called after successful authentication.
  // Populate a value to |vault_keyset|'s reset seed if it is missing, but
  // doesn't save. Returns true if the seed is added, returns false if there is
  // no need to add the reset seed, i.e if it already exists.
  virtual bool AddResetSeedIfMissing(VaultKeyset& vault_keyset);

  // Adds randomly generated reset_seed to the vault keyset if |reset_seed_|
  // doesn't have any value.
  virtual CryptohomeErrorCode AddWrappedResetSeedIfMissing(
      VaultKeyset* vault_keyset, const Credentials& credentials);

  // Adds a new keyset to the given |vault_keyset| and persist to
  // disk. This function assumes the user is already authenticated and their
  // vault keyset with the existing credentials is unwrapped which should be
  // inputted to this function to initialize a new vault keyset. Thus,
  // GetValidKeyset() should be called prior to this function to authenticate
  // with the existing credentials. New keyset is updated to have the key data
  // from |new_credentials|. If |clobber| is true and there are no matching,
  // labeled keys, then it does nothing; if there is an identically labeled key,
  // it will overwrite it.
  virtual CryptohomeErrorCode AddKeyset(const Credentials& new_credentials,
                                        const VaultKeyset& vault_keyset,
                                        bool clobber);

  // Updates an existing |vault_keyset| with the new credentials. This function
  // assumes the user is already authenticated and their |vault_keyset| with an
  // existing credentials is unwrapped. New keyset is updated to have the key
  // data from |new_credentials|, KeyBlobs from |VaultKeyset| and is wrapped by
  // the secret in |new_credentials|.
  virtual CryptohomeErrorCode UpdateKeyset(const Credentials& new_credentials,
                                           const VaultKeyset& vault_keyset);

  // Removes the keyset identified by |key_data|.  The VaultKeyset backing
  // |credentials| may be the same that |key_data| identifies.
  virtual CryptohomeStatus RemoveKeyset(const Credentials& credentials,
                                        const KeyData& key_data);

  // Removes the keyset specified by |index| from the list for the user
  // vault identified by its |obfuscated| username.
  // The caller should check credentials if the call is user-sourced.
  // TODO(wad,ellyjones) Determine a better keyset priotization and management
  //                     scheme than just integer indices, like fingerprints.
  virtual CryptohomeStatus ForceRemoveKeyset(const std::string& obfuscated,
                                             int index);

  // Allows a keyset to be moved to a different index assuming the index can be
  // claimed for a given |obfuscated| username.
  virtual bool MoveKeyset(const std::string& obfuscated, int src, int dst);

  // Migrates the cryptohome vault keyset to a new one for the new credentials.
  virtual bool Migrate(const VaultKeyset& old_vk, const Credentials& newcreds);

  // Attempts to reset all LE credentials associated with a username, given
  // a credential |cred|.
  void ResetLECredentials(const Credentials& creds,
                          const std::string& obfuscated);

  // Attempts to reset all LE credentials associated with a username, given
  // validated VK |validated_vk|.
  void ResetLECredentialsWithValidatedVK(const VaultKeyset& validated_vk,
                                         const std::string& obfuscated);

  // Removes all LE credentials for a user with |obfuscated_username|.
  virtual void RemoveLECredentials(const std::string& obfuscated_username);

  // Returns the public mount pass key derived from username.
  virtual brillo::SecureBlob GetPublicMountPassKey(
      const std::string& account_id);

  // Get timestamp from a legacy location.
  // TODO(b/205759690, dlunev): can be removed after a stepping stone release.
  virtual base::Time GetKeysetBoundTimestamp(const std::string& obfuscated);

  // Remove legacy location for timestamp.
  // TODO(b/205759690, dlunev): can be removed after a stepping stone release.
  virtual void CleanupPerIndexTimestampFiles(const std::string& obfuscated);

  // Checks whether the keyset is up to date (e.g. has correct encryption
  // parameters, has all required fields populated etc.) and if not, updates
  // and resaves the keyset.
  // Returns OkStatus if successful or no resave needed.
  virtual CryptohomeStatus ReSaveKeysetIfNeeded(const Credentials& credentials,
                                                VaultKeyset* keyset) const;

  // Check if the vault keyset needs re-encryption.
  virtual bool ShouldReSaveKeyset(VaultKeyset* vault_keyset) const;

  // Record various metrics about all the VaultKeyset for a given user
  // obfuscated
  virtual void RecordAllVaultKeysetMetrics(const std::string& obfuscated) const;

  // ========== KeysetManagement methods with KeyBlobs ===============

  // Resaves the vault keyset with |key_blobs|, restoring on failure.
  virtual CryptohomeStatus ReSaveKeysetWithKeyBlobs(
      VaultKeyset& vault_keyset,
      KeyBlobs key_blobs,
      std::unique_ptr<AuthBlockState> auth_state) const;

  // Adds initial keyset for obfuscated username with |file_system_keyset|. Adds
  // the key data given by |key_data| and challenge credentials info given by
  // |challenge_credentials_keyset_info| to the created keyset. Wraps the keyset
  // with |key_blobs| and persists to the disk.
  virtual CryptohomeStatusOr<std::unique_ptr<VaultKeyset>>
  AddInitialKeysetWithKeyBlobs(
      const std::string& obfuscated_username,
      const KeyData& key_data,
      const std::optional<SerializedVaultKeyset_SignatureChallengeInfo>&
          challenge_credentials_keyset_info,
      const FileSystemKeyset& file_system_keyset,
      KeyBlobs key_blobs,
      std::unique_ptr<AuthBlockState> auth_state);

  // Returns decrypted with |key_blobs| keyset, or an error status with the
  // particular failure reason if none decryptable with the provided
  // |key_blobs|, |obfuscated_username| and |label|. NOTE: The LE Credential
  // Keysets are only considered when the |label| provided is non-empty
  // (b/202907485).
  virtual MountStatusOr<std::unique_ptr<VaultKeyset>>
  GetValidKeysetWithKeyBlobs(const std::string& obfuscated_username,
                             KeyBlobs key_blobs,
                             const std::optional<std::string>& label);

  // Adds a new keyset to the given |vault_keyset| and persist to
  // disk. This function assumes the user is already authenticated and their
  // old vault keyset, |old_vault_keyset| is unwrapped to initialize a new vault
  // keyset. Thus, GetValidKeyset() should be called prior to this function to
  // authenticate with the existing credentials. New keyset is generated and the
  // key data from |key_data_new| is added. New keyset is persisted to disk
  // after wrapped by |key_blobs_new| as directed by |auth_state_new|. If
  // |clobber| is true and there are no matching, labeled keys, then it does
  // nothing; if there is an identically labeled key, it will overwrite it.
  virtual CryptohomeErrorCode AddKeysetWithKeyBlobs(
      const std::string& obfuscated_username_new,
      const KeyData& key_data_new,
      const VaultKeyset& vault_keyset_old,
      KeyBlobs key_blobs_new,
      std::unique_ptr<AuthBlockState> auth_state_new,
      bool clobber);

  // Encrypts and saves a keyset with the given |key_blobs|.
  virtual CryptohomeErrorCode SaveKeysetWithKeyBlobs(
      VaultKeyset& vault_keyset,
      const KeyBlobs& key_blobs,
      const AuthBlockState& auth_state);

  // Updates an existing |vault_keyset| with the |key_data_new| from new user
  // credentials. This function assumes the user is already authenticated and
  // their |vault_keyset| with an existing credentials is unwrapped. New keyset
  // is wrapped by the |key_blobs| passed, which should be derived from the new
  // credentials.
  virtual CryptohomeErrorCode UpdateKeysetWithKeyBlobs(
      const std::string& obfuscated_username_new,
      const KeyData& key_data_new,
      const VaultKeyset& vault_keyset,
      KeyBlobs key_blobs,
      std::unique_ptr<AuthBlockState> auth_state);

 private:
  // Adds initial keyset for obfuscated username with |file_system_keyset|. Adds
  // the key data given by |key_data| and challenge credentials info given by
  // |challenge_credentials_keyset_info| to the created keyset. Wraps keyset
  // with |encrypt_vk_callback| and persists to disk.
  CryptohomeStatusOr<std::unique_ptr<VaultKeyset>> AddInitialKeysetImpl(
      const std::string& obfuscated_username,
      const KeyData& key_data,
      const std::optional<SerializedVaultKeyset_SignatureChallengeInfo>&
          challenge_credentials_keyset_info,
      const FileSystemKeyset& file_system_keyset,
      EncryptVkCallback encrypt_vk_callback);

  // Returns decrypted VaultKeyset for the obfuscated_username and label or
  // nullptr if none decryptable.
  MountStatusOr<std::unique_ptr<VaultKeyset>> GetValidKeysetImpl(
      const std::string& obfuscated_username,
      const std::optional<std::string>& label,
      DecryptVkCallback decrypt_vk_callback);

  // Generates a new keyset for |obfuscated_username_new| with |key_data_new|
  // and the filesystem key from |vault_keyset_old| and persist to disk.  If
  // |clobber| is true and there are no matching, labeled keys, then it does
  // nothing; if there is an identically labeled key, it will overwrite it.
  CryptohomeErrorCode AddKeysetImpl(const std::string& obfuscated_username_new,
                                    const KeyData& key_data_new,
                                    const VaultKeyset& vault_keyset_old,
                                    EncryptVkCallback encrypt_vk_callback,
                                    bool clobber);

  // Resaves the given |vault_keyset| with the credentials, restoring on error.
  CryptohomeStatus ReSaveKeyset(const Credentials& credentials,
                                VaultKeyset* vault_keyset) const;

  // Implements the common functionality for resaving a keyset with restore on
  // error.
  CryptohomeStatus ReSaveKeysetImpl(
      VaultKeyset& vault_keyset, EncryptVkCallback encrypt_vk_callback) const;

  // TODO(b/205759690, dlunev): can be removed after a stepping stone release.
  base::Time GetPerIndexTimestampFileData(const std::string& obfuscated,
                                          int index);

  // Records various metrics about the VaultKeyset into the VaultKeysetMetrics
  // struct.
  void RecordVaultKeysetMetrics(const VaultKeyset& vk,
                                VaultKeysetMetrics& keyset_metrics) const;

  // Attempts to reset all LE credentials associated with a username, given
  // a credential |cred| and |key_indices|.
  void ResetLECredentialsInternal(const VaultKeyset& vk,
                                  const std::string& obfuscated,
                                  const std::vector<int>& key_indices);

  Platform* platform_;
  Crypto* crypto_;
  std::unique_ptr<VaultKeysetFactory> vault_keyset_factory_;

  FRIEND_TEST(KeysetManagementTest, ReSaveOnLoadNoReSave);
  FRIEND_TEST(KeysetManagementTest, ReSaveOnLoadTestRegularCreds);
  FRIEND_TEST(KeysetManagementTest, ReSaveOnLoadTestLeCreds);
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_KEYSET_MANAGEMENT_H_
