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
#include "cryptohome/credential_verifier.h"
#include "cryptohome/credentials.h"
#include "cryptohome/keyset_management.h"
#include "cryptohome/storage/homedirs.h"
#include "cryptohome/storage/mount.h"

namespace cryptohome {

class UserSession : public base::RefCountedThreadSafe<UserSession> {
 public:
  UserSession();
  UserSession(HomeDirs* homedirs,
              KeysetManagement* keyset_management,
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

  // Mounts disk backed vault for the user of supplied credentials, if the
  // credentials are valid.
  MountError MountVault(const Credentials& credentials,
                        const Mount::MountArgs& mount_args);

  // Mounts disk backed vault for the user of supplied auth_session.
  MountError MountVault(AuthSession* auth_session,
                        const Mount::MountArgs& mount_args);

  // Creates and mounts a ramdisk backed ephemeral session for the user
  // of supplied credentials;
  MountError MountEphemeral(const Credentials& credentials);

  // Creates and mounts a ramdisk backed ephemeral session for the user
  // of supplied credentials using auth session.
  MountError MountEphemeral(AuthSession* auth_session);

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

  // Sets credentials current session can be re-authenticated with and the
  // index of the keyset those credentials belong to. Returns false in case
  // anything went wrong in setting up new re-auth state.
  bool SetCredentials(const Credentials& credentials, int key_index);

  // Sets credentials for the current session to re-authenticated with and the
  // index of the keyset those credentials belong to.
  void SetCredentials(AuthSession* auth_session);

  // Checks that the session belongs to the obfuscated_user.
  bool VerifyUser(const std::string& obfuscated_username) const;

  // Verifies credentials against store re-auth state. Returns true if the
  // credentials were successfully re-authenticated against the saved re-auth
  // state.
  bool VerifyCredentials(const Credentials& credentials) const;

  // Returns key_data of the current session credentials.
  const KeyData& key_data() const { return key_data_; }

  // Returns index of the keyset current credentials refer to.
  int key_index() const { return key_index_; }

 private:
  // Computes a public derivative from |fek| and |fnek| for u2fd to fetch.
  void PrepareWebAuthnSecret(const brillo::SecureBlob& fek,
                             const brillo::SecureBlob& fnek);

  // Clears the WebAuthn secret if it's not read yet.
  void ClearWebAuthnSecret();

  HomeDirs* homedirs_;
  KeysetManagement* keyset_management_;

  std::string obfuscated_username_;
  std::string username_;
  brillo::SecureBlob system_salt_;
  std::unique_ptr<CredentialVerifier> credential_verifier_;
  int key_index_ = -1;
  KeyData key_data_;

  // Secret for WebAuthn credentials.
  std::unique_ptr<brillo::SecureBlob> webauthn_secret_;
  // Timer for clearing the WebAuthn secret.
  base::OneShotTimer clear_webauthn_secret_timer_;

  scoped_refptr<cryptohome::Mount> mount_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_USER_SESSION_H_
