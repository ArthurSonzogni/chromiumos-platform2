// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_BACKEND_TPM1_SIGNING_H_
#define LIBHWSEC_BACKEND_TPM1_SIGNING_H_

#include <brillo/secure_blob.h>

#include "libhwsec/backend/backend.h"
#include "libhwsec/status.h"

namespace hwsec {

class BackendTpm1;

class SigningTpm1 : public Backend::Signing,
                    public Backend::SubClassHelper<BackendTpm1> {
 public:
  using SubClassHelper::SubClassHelper;
  StatusOr<brillo::Blob> Sign(Key key,
                              const brillo::Blob& data,
                              const SigningOptions& options) override;
  StatusOr<brillo::Blob> RawSign(Key key,
                                 const brillo::Blob& data,
                                 const SigningOptions& options) override;
  Status Verify(Key key, const brillo::Blob& signed_data) override;
};

}  // namespace hwsec

#endif  // LIBHWSEC_BACKEND_TPM1_SIGNING_H_
