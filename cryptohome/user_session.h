// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_USER_SESSION_H_
#define CRYPTOHOME_USER_SESSION_H_

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
  UserSession();
  UserSession(
      HomeDirs* homedirs,
      KeysetManagement* keyset_management,
      UserOldestActivityTimestampManager* user_activity_timestamp_manager,
      Pkcs11TokenFactory* pkcs11_token_factory,
      const brillo::SecureBlob& salt,
      const scoped_refptr<Mount> mount);
  virtual ~UserSession();

  // Disallow Copy/Move/Assign
  UserSession(const UserSession&) = delete;
  UserSession(const UserSession&&) = delete;
  void operator=(const UserSession&) = delete;
  void operator=(const UserSession&&) = delete;

  // Accessors to the mount object.
  // TODO(dlunev): ideally we shouldn't have these accessors and
  // Service/UserDataAuth shall operate on UserSession object only.
  scoped_refptr<Mount> GetMount() { return mount_; }
  const scoped_refptr<Mount> GetMount() const { return mount_; }

  // Mounts disk backed vault for the given username with the supplied file
  // system keyset.
  MountError MountVault(const std::string username,
                        const FileSystemKeyset& fs_keyset,
                        const CryptohomeVault::Options& vault_options);

  // Creates and mounts a ramdisk backed ephemeral session for the given user.
  MountError MountEphemeral(const std::string username);

  // Creates and mounts a ramdisk backed ephemeral session for an anonymous
  // user.
  MountError MountGuest();

  // Unmounts the session.
  bool Unmount();

  // Update the timestamp of the last user activity.
  bool UpdateActivityTimestamp(int time_shift_sec);

  // Returns status string of the proxied Mount objest.
  //
  // The returned object is a dictionary whose keys describe the mount. Current
  // keys are: "keysets", "mounted", "owner", "enterprise", and "type".
  base::Value GetStatus() const;

  // Returns the WebAuthn secret and clears it from memory.
  std::unique_ptr<brillo::SecureBlob> GetWebAuthnSecret();

  // Returns the WebAuthn secret hash.
  const brillo::SecureBlob& GetWebAuthnSecretHash() const;

  // Sets credentials current session can be re-authenticated with.
  // Returns false in case anything went wrong in setting up new re-auth state.
  bool SetCredentials(const Credentials& credentials);

  // Sets credentials current session can be re-authenticated with.
  void SetCredentials(AuthSession* auth_session);

  // Checks that the session belongs to the obfuscated_user.
  bool VerifyUser(const std::string& obfuscated_username) const;

  // Verifies credentials against store re-auth state. Returns true if the
  // credentials were successfully re-authenticated against the saved re-auth
  // state.
  bool VerifyCredentials(const Credentials& credentials) const;

  // Returns key_data of the current session credentials.
  const KeyData& key_data() const { return key_data_; }

  Pkcs11Token* GetPkcs11Token() { return pkcs11_token_.get(); }

 private:
  // Computes a public derivative from |fek| and |fnek| for u2fd to fetch.
  void PrepareWebAuthnSecret(const brillo::SecureBlob& fek,
                             const brillo::SecureBlob& fnek);

  // Clears the WebAuthn secret if it's not read yet.
  void ClearWebAuthnSecret();

  HomeDirs* homedirs_;
  KeysetManagement* keyset_management_;
  UserOldestActivityTimestampManager* user_activity_timestamp_manager_;
  Pkcs11TokenFactory* pkcs11_token_factory_;

  std::string obfuscated_username_;
  std::string username_;
  brillo::SecureBlob system_salt_;
  std::unique_ptr<CredentialVerifier> credential_verifier_;
  KeyData key_data_;

  // Secret for WebAuthn credentials.
  std::unique_ptr<brillo::SecureBlob> webauthn_secret_;
  // Hash of the WebAuthn secret.
  brillo::SecureBlob webauthn_secret_hash_;
  // Timer for clearing the WebAuthn secret.
  base::OneShotTimer clear_webauthn_secret_timer_;

  scoped_refptr<cryptohome::Mount> mount_;
  std::unique_ptr<Pkcs11Token> pkcs11_token_;

  friend class UserDataAuthTestTasked;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_USER_SESSION_H_
