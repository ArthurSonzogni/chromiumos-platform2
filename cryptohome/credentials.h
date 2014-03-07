// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Credentials is the interface for objects that wrap up a set
// of credentials with which we can authenticate or mount.

#ifndef CRYPTOHOME_CREDENTIALS_H_
#define CRYPTOHOME_CREDENTIALS_H_

#include <chromeos/secure_blob.h>

#include "key.pb.h"

namespace cryptohome {

class Credentials {
 public:
  Credentials() {}
  virtual ~Credentials() {}

  // Returns the full user name as a std::string
  //
  // Parameters
  //
  virtual std::string username() const = 0;

  // Returns the obfuscated username, used as the name of the directory
  // containing the user's stateful data (and maybe used for other reasons
  // at some point.)
  virtual std::string GetObfuscatedUsername(
      const chromeos::Blob &system_salt) const = 0;

  // Returns the user's passkey
  //
  // Parameters
  //  passkey - A SecureBlob containing the passkey
  //
  virtual void GetPasskey(chromeos::SecureBlob* passkey) const = 0;

  // Returns the associated KeyData for the passkey, if defined.
  virtual const KeyData& key_data() const = 0;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_CREDENTIALS_H_
