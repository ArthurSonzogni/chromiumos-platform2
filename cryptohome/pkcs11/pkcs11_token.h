// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_PKCS11_PKCS11_TOKEN_H_
#define CRYPTOHOME_PKCS11_PKCS11_TOKEN_H_

#include <brillo/secure_blob.h>

namespace cryptohome {

// Manage the user token for key store.
class Pkcs11Token {
 public:
  virtual ~Pkcs11Token() = default;

  // Inserts the user token into the key store, and open a slot for the user.
  virtual bool Insert() = 0;

  // Removes the user token from the key store, and close the slot.
  virtual void Remove() = 0;

  // Checks if the user token is installed into the key store or not.
  virtual bool IsReady() const = 0;

  // Tries to restore the user token to the key store.
  // This will be used when the key store accidentally lost the user token.
  virtual void TryRestoring() = 0;

  // Checks if the user token needs to be restored or not.
  virtual bool NeedRestore() const = 0;

  // Restores the user token with the auth data.
  virtual void RestoreAuthData(const brillo::SecureBlob& auth_data) = 0;
};
}  // namespace cryptohome

#endif  // CRYPTOHOME_PKCS11_PKCS11_TOKEN_H_
