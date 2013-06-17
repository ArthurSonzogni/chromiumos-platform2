// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// UserSession - the UserSession class is used in re-authenticating the
// currently logged-in user.  It allows offline credentials verification
// post-login without the expense of a TPM crypto operation (when the TPM is
// used for added security).  User session works by generating a random blob and
// encrypting it using the user's credentials at login.  When an offline
// credentials check occurs for this user, UserSession attempts to decrypt the
// encrypted representation of that blob.  A successful decryption means that
// the supplied credentials are correct.

#ifndef CRYPTOHOME_USER_SESSION_H_
#define CRYPTOHOME_USER_SESSION_H_

#include <base/basictypes.h>
#include <chromeos/secure_blob.h>

#include "credentials.h"

namespace cryptohome {

class UserSession {
 public:
  UserSession();
  virtual ~UserSession();

  // Initializes the UserSession object.
  //
  // Parameters
  //   crypto - The crypto context to use for this session
  //   salt - The salt to use for the username
  virtual void Init(const chromeos::SecureBlob& salt);

  // Assigns a user to the UserSession object.  The random blob is created and
  // encrypted with the supplied credentials.
  //
  // Parameters
  //   username - The user credentials to initialize this session with
  virtual bool SetUser(const Credentials& username);

  // Resets the UserSession, clearing the current user and cipher text used for
  // verification.
  virtual void Reset();

  // Checks that the user supplied is the user associated with this session
  //
  // Parameters
  //   username - The user to check this session against
  virtual bool CheckUser(const Credentials& username) const;

  // Checks that the user's credentials successfully decrypt the ciphertext
  // associated with this session (and are therefore valid for this user).
  //
  // Parameters
  //   credentials - The user credentials to attempt decryption with
  virtual bool Verify(const Credentials& credentials) const;

  // Get the obfuscated username of this session
  //
  // Parameters
  //   username (OUT) - the username
  virtual void GetObfuscatedUsername(std::string* username) const;

  std::string username() const {
    return username_;
  }

  // Assigns a key to the UserSession object.  This indicates which key on disk
  // is associated to the UserSession.
  //
  // Parameters
  //   index - index of the vault keyset
  virtual void set_key_index(int index) { key_index_ = index; }

  // Get the current key index of this session
  int key_index() const;

 private:
  std::string obfuscated_username_;
  std::string username_;
  chromeos::SecureBlob username_salt_;
  chromeos::SecureBlob key_salt_;
  chromeos::SecureBlob cipher_;
  int key_index_;

  DISALLOW_COPY_AND_ASSIGN(UserSession);
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_USER_SESSION_H_
