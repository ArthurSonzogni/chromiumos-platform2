// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_USER_SESSION_USER_SESSION_H_
#define CRYPTOHOME_USER_SESSION_USER_SESSION_H_

#include <memory>
#include <string>

#include <base/memory/ref_counted.h>
#include <base/timer/timer.h>
#include <brillo/secure_blob.h>

#include "cryptohome/auth_session.h"
#include "cryptohome/cleanup/user_oldest_activity_timestamp_manager.h"
#include "cryptohome/credential_verifier.h"
#include "cryptohome/credentials.h"
#include "cryptohome/keyset_management.h"
#include "cryptohome/pkcs11/pkcs11_token.h"
#include "cryptohome/pkcs11/pkcs11_token_factory.h"
#include "cryptohome/storage/cryptohome_vault.h"
#include "cryptohome/storage/homedirs.h"
#include "cryptohome/storage/mount.h"

namespace cryptohome {

class UserSession : public base::RefCountedThreadSafe<UserSession> {
 public:
  UserSession() = default;

  // Disallow Copy/Move/Assign
  UserSession(const UserSession&) = delete;
  UserSession(const UserSession&&) = delete;
  void operator=(const UserSession&) = delete;
  void operator=(const UserSession&&) = delete;

  // Returns whether the user session represents an active login session.
  virtual bool IsActive() const = 0;

  // Returns whether the session is for an ephemeral user.
  virtual bool IsEphemeral() const = 0;

  // Returns whether the path belong to the session.
  // TODO(dlunev): remove it once recovery logic is embedded into storage code.
  virtual bool OwnsMountPoint(const base::FilePath& path) const = 0;

  // Perform migration of the vault to a different encryption type.
  virtual bool MigrateVault(
      const dircrypto_data_migrator::MigrationHelper::ProgressCallback&
          callback,
      MigrationType migration_type) = 0;

  // Mounts disk backed vault for the given username with the supplied file
  // system keyset.
  virtual MountError MountVault(
      const std::string username,
      const FileSystemKeyset& fs_keyset,
      const CryptohomeVault::Options& vault_options) = 0;

  // Creates and mounts a ramdisk backed ephemeral session for the given user.
  virtual MountError MountEphemeral(const std::string username) = 0;

  // Creates and mounts a ramdisk backed ephemeral session for an anonymous
  // user.
  virtual MountError MountGuest() = 0;

  // Unmounts the session.
  virtual bool Unmount() = 0;

  // Returns status string of the proxied Mount objest.
  //
  // The returned object is a dictionary whose keys describe the mount. Current
  // keys are: "keysets", "mounted", "owner", "enterprise", and "type".
  virtual base::Value GetStatus() const = 0;

  // Returns the WebAuthn secret and clears it from memory.
  virtual std::unique_ptr<brillo::SecureBlob> GetWebAuthnSecret() = 0;

  // Returns the WebAuthn secret hash.
  virtual const brillo::SecureBlob& GetWebAuthnSecretHash() const = 0;

  // Returns the hibernate secret.
  virtual std::unique_ptr<brillo::SecureBlob> GetHibernateSecret() = 0;

  // Sets credentials current session can be re-authenticated with.
  // Returns false in case anything went wrong in setting up new re-auth state.
  virtual bool SetCredentials(const Credentials& credentials) = 0;

  // Sets credentials current session can be re-authenticated with.
  virtual void SetCredentials(AuthSession* auth_session) = 0;

  // Checks that the session belongs to the obfuscated_user.
  virtual bool VerifyUser(const std::string& obfuscated_username) const = 0;

  // Verifies credentials against store re-auth state. Returns true if the
  // credentials were successfully re-authenticated against the saved re-auth
  // state.
  virtual bool VerifyCredentials(const Credentials& credentials) const = 0;

  // Returns key_data of the current session credentials.
  virtual const KeyData& key_data() const = 0;

  // Returns PKCS11 token associated with the session.
  virtual Pkcs11Token* GetPkcs11Token() = 0;

  // Returns the name of the user associated with the session.
  virtual std::string GetUsername() const = 0;

  // Computes a public derivative from |fek| and |fnek| for u2fd to fetch.
  virtual void PrepareWebAuthnSecret(const brillo::SecureBlob& fek,
                                     const brillo::SecureBlob& fnek) = 0;

 protected:
  friend class base::RefCountedThreadSafe<UserSession>;
  virtual ~UserSession() = default;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_USER_SESSION_USER_SESSION_H_
