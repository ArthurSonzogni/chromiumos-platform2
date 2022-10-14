// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_USER_SESSION_REAL_USER_SESSION_H_
#define CRYPTOHOME_USER_SESSION_REAL_USER_SESSION_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <base/timer/timer.h>
#include <brillo/secure_blob.h>

#include "cryptohome/auth_session.h"
#include "cryptohome/cleanup/user_oldest_activity_timestamp_manager.h"
#include "cryptohome/credential_verifier.h"
#include "cryptohome/credentials.h"
#include "cryptohome/error/cryptohome_mount_error.h"
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
  RealUserSession() = default;
  RealUserSession(
      const std::string& username,
      HomeDirs* homedirs,
      KeysetManagement* keyset_management,
      UserOldestActivityTimestampManager* user_activity_timestamp_manager,
      Pkcs11TokenFactory* pkcs11_token_factory,
      const scoped_refptr<Mount> mount);

  RealUserSession(const RealUserSession&) = delete;
  RealUserSession(const RealUserSession&&) = delete;
  void operator=(const RealUserSession&) = delete;
  void operator=(const RealUserSession&&) = delete;

  bool IsActive() const override { return mount_->IsMounted(); }

  bool IsEphemeral() const override { return mount_->IsEphemeral(); }

  bool OwnsMountPoint(const base::FilePath& path) const override {
    return mount_->OwnsMountPoint(path);
  }

  bool MigrateVault(
      const dircrypto_data_migrator::MigrationHelper::ProgressCallback&
          callback,
      MigrationType migration_type) override {
    return mount_->MigrateEncryption(callback, migration_type);
  }

  MountStatus MountVault(
      const std::string& username,
      const FileSystemKeyset& fs_keyset,
      const CryptohomeVault::Options& vault_options) override;

  MountStatus MountEphemeral(const std::string& username) override;

  MountStatus MountGuest() override;

  bool Unmount() override;

  base::Value GetStatus() const override;

  std::unique_ptr<brillo::SecureBlob> GetWebAuthnSecret() override;

  const brillo::SecureBlob& GetWebAuthnSecretHash() const override;

  std::unique_ptr<brillo::SecureBlob> GetHibernateSecret() override;

  void AddCredentials(const Credentials& credentials) override;

  void TakeCredentialsFrom(AuthSession* auth_session) override;

  void AddCredentialVerifier(
      std::unique_ptr<CredentialVerifier> verifier) override;

  bool HasCredentialVerifier() const override;
  bool HasCredentialVerifier(const std::string& label) const override;

  std::vector<const CredentialVerifier*> GetCredentialVerifiers()
      const override;

  void RemoveCredentialVerifierForKeyLabel(
      const std::string& key_label) override;

  bool VerifyUser(const std::string& obfuscated_username) const override;

  bool VerifyCredentials(const Credentials& credentials) const override;

  const KeyData& key_data() const override { return key_data_; }

  Pkcs11Token* GetPkcs11Token() override { return pkcs11_token_.get(); }

  std::string GetUsername() const override { return username_; }

  void PrepareWebAuthnSecret(const brillo::SecureBlob& fek,
                             const brillo::SecureBlob& fnek) override;

  bool ResetApplicationContainer(const std::string& application) override;

 private:
  // Computes a public derivative from |fek| and |fnek|, and store its hash for
  // u2fd to fetch.
  void PrepareWebAuthnSecretHash(const brillo::SecureBlob& fek,
                                 const brillo::SecureBlob& fnek);

  // Clears the WebAuthn secret if it's not read yet.
  void ClearWebAuthnSecret();

  // Computes a public derivative from |fek| and |fnek| for hiberman to fetch.
  void PrepareHibernateSecret(const brillo::SecureBlob& fek,
                              const brillo::SecureBlob& fnek);

  // Clears the WebAuthn secret if it's not read yet.
  void ClearHibernateSecret();

  const std::string username_;
  const std::string obfuscated_username_;

  HomeDirs* homedirs_;
  KeysetManagement* keyset_management_;
  UserOldestActivityTimestampManager* user_activity_timestamp_manager_;
  Pkcs11TokenFactory* pkcs11_token_factory_;

  std::map<std::string, std::unique_ptr<CredentialVerifier>>
      label_to_credential_verifier_;
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
