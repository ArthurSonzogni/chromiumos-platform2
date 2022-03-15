// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_USER_SESSION_REAL_USER_SESSION_H_
#define CRYPTOHOME_USER_SESSION_REAL_USER_SESSION_H_

#include <memory>
#include <string>

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
#include "cryptohome/user_session/user_session.h"

#include "cryptohome/dircrypto_data_migrator/migration_helper.h"

namespace cryptohome {

class RealUserSession : public UserSession {
 public:
  RealUserSession();
  RealUserSession(
      HomeDirs* homedirs,
      KeysetManagement* keyset_management,
      UserOldestActivityTimestampManager* user_activity_timestamp_manager,
      Pkcs11TokenFactory* pkcs11_token_factory,
      const scoped_refptr<Mount> mount);

  // Disallow Copy/Move/Assign
  RealUserSession(const RealUserSession&) = delete;
  RealUserSession(const RealUserSession&&) = delete;
  void operator=(const RealUserSession&) = delete;
  void operator=(const RealUserSession&&) = delete;

  // Returns whether the user session represents an active login session.
  bool IsActive() const override { return mount_->IsMounted(); }

  // Returns whether the session is for an ephemeral user.
  bool IsEphemeral() const override { return mount_->IsEphemeral(); }

  // Returns whether the path belong to the session.
  // TODO(dlunev): remove it once recovery logic is embedded into storage code.
  bool OwnsMountPoint(const base::FilePath& path) const override {
    return mount_->OwnsMountPoint(path);
  }

  // Perform migration of the vault to a different encryption type.
  bool MigrateVault(
      const dircrypto_data_migrator::MigrationHelper::ProgressCallback&
          callback,
      MigrationType migration_type) override {
    return mount_->MigrateEncryption(callback, migration_type);
  }

  // Mounts disk backed vault for the given username with the supplied file
  // system keyset.
  MountError MountVault(const std::string username,
                        const FileSystemKeyset& fs_keyset,
                        const CryptohomeVault::Options& vault_options) override;

  // Creates and mounts a ramdisk backed ephemeral session for the given user.
  MountError MountEphemeral(const std::string username) override;

  // Creates and mounts a ramdisk backed ephemeral session for an anonymous
  // user.
  MountError MountGuest() override;

  // Unmounts the session.
  bool Unmount() override;

  // Returns status string of the proxied Mount objest.
  //
  // The returned object is a dictionary whose keys describe the mount. Current
  // keys are: "keysets", "mounted", "owner", "enterprise", and "type".
  base::Value GetStatus() const override;

  // Returns the WebAuthn secret and clears it from memory.
  std::unique_ptr<brillo::SecureBlob> GetWebAuthnSecret() override;

  // Returns the WebAuthn secret hash.
  const brillo::SecureBlob& GetWebAuthnSecretHash() const override;

  // Returns the hibernate secret.
  std::unique_ptr<brillo::SecureBlob> GetHibernateSecret() override;

  // Sets credentials current session can be re-authenticated with.
  // Returns false in case anything went wrong in setting up new re-auth state.
  bool SetCredentials(const Credentials& credentials) override;

  // Sets credentials current session can be re-authenticated with.
  void SetCredentials(AuthSession* auth_session) override;

  // Checks that the session belongs to the obfuscated_user.
  bool VerifyUser(const std::string& obfuscated_username) const override;

  // Verifies credentials against store re-auth state. Returns true if the
  // credentials were successfully re-authenticated against the saved re-auth
  // state.
  bool VerifyCredentials(const Credentials& credentials) const override;

  // Returns key_data of the current session credentials.
  const KeyData& key_data() const override { return key_data_; }

  // Returns PKCS11 token associated with the session.
  Pkcs11Token* GetPkcs11Token() override { return pkcs11_token_.get(); }

  // Returns the name of the user associated with the session.
  std::string GetUsername() const override { return username_; }

  // Computes a public derivative from |fek| and |fnek| for u2fd to fetch.
  void PrepareWebAuthnSecret(const brillo::SecureBlob& fek,
                             const brillo::SecureBlob& fnek) override;

 private:
  ~RealUserSession() override;

  // Clears the WebAuthn secret if it's not read yet.
  void ClearWebAuthnSecret();

  // Computes a public derivative from |fek| and |fnek| for hiberman to fetch.
  void PrepareHibernateSecret(const brillo::SecureBlob& fek,
                              const brillo::SecureBlob& fnek);

  // Clears the WebAuthn secret if it's not read yet.
  void ClearHibernateSecret();

  HomeDirs* homedirs_;
  KeysetManagement* keyset_management_;
  UserOldestActivityTimestampManager* user_activity_timestamp_manager_;
  Pkcs11TokenFactory* pkcs11_token_factory_;

  std::string obfuscated_username_;
  std::string username_;
  std::unique_ptr<CredentialVerifier> credential_verifier_;
  KeyData key_data_;

  // Secret for WebAuthn credentials.
  std::unique_ptr<brillo::SecureBlob> webauthn_secret_;
  // Hash of the WebAuthn secret.
  brillo::SecureBlob webauthn_secret_hash_;
  // Timer for clearing the WebAuthn secret.
  base::OneShotTimer clear_webauthn_secret_timer_;

  // Secret for securing hibernate images.
  std::unique_ptr<brillo::SecureBlob> hibernate_secret_;
  // Timer for clearing the hibernate secret.
  base::OneShotTimer clear_hibernate_secret_timer_;

  scoped_refptr<cryptohome::Mount> mount_;
  std::unique_ptr<Pkcs11Token> pkcs11_token_;

  friend class UserDataAuthTestTasked;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_USER_SESSION_REAL_USER_SESSION_H_
