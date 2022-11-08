// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FRONTEND_U2FD_FRONTEND_H_
#define LIBHWSEC_FRONTEND_U2FD_FRONTEND_H_

#include <brillo/secure_blob.h>

#include "libhwsec/backend/key_management.h"
#include "libhwsec/frontend/frontend.h"
#include "libhwsec/status.h"
#include "libhwsec/structures/key.h"

namespace hwsec {

class U2fFrontend : public Frontend {
 public:
  using CreateKeyResult = KeyManagement::CreateKeyResult;

  ~U2fFrontend() override = default;

  // Is the security module enabled or not.
  virtual StatusOr<bool> IsEnabled() = 0;

  // Is the security module ready to use or not.
  virtual StatusOr<bool> IsReady() = 0;

  // Generates an RSA signing key pair in the hardware backed security module.
  //   auth_value - Authorization data which will be associated with the key.
  virtual StatusOr<CreateKeyResult> GenerateRSASigningKey(
      const brillo::SecureBlob& auth_value) = 0;

  // Retrieves the public components of an RSA key pair.
  virtual StatusOr<RSAPublicInfo> GetRSAPublicKey(Key key) = 0;

  // Loads a key by blob into the hardware backed security module.
  //   key_blob - The key blob as provided by GenerateKey or WrapRSAKey.
  //   auth_value - Authorization data for the key.
  // Returns true on success.
  virtual StatusOr<ScopedKey> LoadKey(const brillo::Blob& key_blob,
                                      const brillo::SecureBlob& auth_value) = 0;

  // Generates a RSA digital signature.
  //   key - The key handle that derived from ScopedKey.
  //   data - The data to sign.
  // Returns true on success.
  virtual StatusOr<brillo::Blob> RSASign(Key key, const brillo::Blob& data) = 0;
};

}  // namespace hwsec

#endif  // LIBHWSEC_FRONTEND_U2FD_FRONTEND_H_
