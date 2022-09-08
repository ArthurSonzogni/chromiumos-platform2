// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_BACKEND_TPM2_DERIVING_H_
#define LIBHWSEC_BACKEND_TPM2_DERIVING_H_

#include <brillo/secure_blob.h>

#include "libhwsec/backend/backend.h"
#include "libhwsec/backend/tpm2/key_management.h"
#include "libhwsec/status.h"

namespace hwsec {

class BackendTpm2;

class DerivingTpm2 : public Backend::Deriving,
                     public Backend::SubClassHelper<BackendTpm2> {
 public:
  using SubClassHelper::SubClassHelper;
  StatusOr<brillo::Blob> Derive(Key key, const brillo::Blob& blob) override;
  StatusOr<brillo::SecureBlob> SecureDerive(
      Key key, const brillo::SecureBlob& blob) override;

 private:
  StatusOr<brillo::SecureBlob> DeriveRsaKey(const KeyTpm2& key_data,
                                            const brillo::SecureBlob& blob);
  StatusOr<brillo::SecureBlob> DeriveEccKey(const KeyTpm2& key_data,
                                            const brillo::SecureBlob& blob);
};

}  // namespace hwsec

#endif  // LIBHWSEC_BACKEND_TPM2_DERIVING_H_
