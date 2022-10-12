// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_BACKEND_TPM2_SIGNING_H_
#define LIBHWSEC_BACKEND_TPM2_SIGNING_H_

#include <brillo/secure_blob.h>
#include <trunks/tpm_generated.h>

#include "libhwsec/backend/backend.h"
#include "libhwsec/backend/tpm2/key_management.h"
#include "libhwsec/status.h"

namespace hwsec {

class BackendTpm2;

class SigningTpm2 : public Backend::Signing,
                    public Backend::SubClassHelper<BackendTpm2> {
 public:
  using SubClassHelper::SubClassHelper;
  StatusOr<brillo::Blob> Sign(Key key,
                              const brillo::Blob& data,
                              const SigningOptions& options) override;
  StatusOr<brillo::Blob> RawSign(Key key,
                                 const brillo::Blob& data,
                                 const SigningOptions& options) override;
  Status Verify(Key key, const brillo::Blob& signed_data) override;

  // Using the decrypt workaround to sign the data with RSA key.
  StatusOr<brillo::Blob> RawSignRsaWithDecrypt(trunks::TPM_ALG_ID padding,
                                               const KeyTpm2& key_data,
                                               const brillo::Blob& data,
                                               const SigningOptions& options);
};

}  // namespace hwsec

#endif  // LIBHWSEC_BACKEND_TPM2_SIGNING_H_
