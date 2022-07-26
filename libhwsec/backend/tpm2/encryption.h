// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_BACKEND_TPM2_ENCRYPTION_H_
#define LIBHWSEC_BACKEND_TPM2_ENCRYPTION_H_

#include <brillo/secure_blob.h>

#include "libhwsec/backend/backend.h"
#include "libhwsec/status.h"
#include "libhwsec/structures/key.h"

namespace hwsec {

class BackendTpm2;

class EncryptionTpm2 : public Backend::Encryption,
                       public Backend::SubClassHelper<BackendTpm2> {
 public:
  using SubClassHelper::SubClassHelper;
  StatusOr<brillo::Blob> Encrypt(Key key,
                                 const brillo::SecureBlob& plaintext,
                                 EncryptionOptions options) override;
  StatusOr<brillo::SecureBlob> Decrypt(Key key,
                                       const brillo::Blob& ciphertext,
                                       EncryptionOptions options) override;
};

}  // namespace hwsec

#endif  // LIBHWSEC_BACKEND_TPM2_ENCRYPTION_H_
