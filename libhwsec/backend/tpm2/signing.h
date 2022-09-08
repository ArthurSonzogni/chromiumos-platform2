// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_BACKEND_TPM2_SIGNING_H_
#define LIBHWSEC_BACKEND_TPM2_SIGNING_H_

#include <brillo/secure_blob.h>

#include "libhwsec/backend/backend.h"
#include "libhwsec/status.h"

namespace hwsec {

class BackendTpm2;

class SigningTpm2 : public Backend::Signing,
                    public Backend::SubClassHelper<BackendTpm2> {
 public:
  using SubClassHelper::SubClassHelper;
  StatusOr<brillo::Blob> Sign(const OperationPolicy& policy,
                              Key key,
                              const brillo::Blob& data) override;
  Status Verify(const OperationPolicy& policy,
                Key key,
                const brillo::Blob& signed_data) override;
};

}  // namespace hwsec

#endif  // LIBHWSEC_BACKEND_TPM2_SIGNING_H_
