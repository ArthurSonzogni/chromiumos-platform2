// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_BACKEND_TPM2_RO_DATA_H_
#define LIBHWSEC_BACKEND_TPM2_RO_DATA_H_

#include <cstdint>

#include <brillo/secure_blob.h>

#include "libhwsec/backend/backend.h"
#include "libhwsec/status.h"

namespace hwsec {

class BackendTpm2;

class RoDataTpm2 : public Backend::RoData,
                   public Backend::SubClassHelper<BackendTpm2> {
 public:
  using SubClassHelper::SubClassHelper;
  StatusOr<bool> IsReady(RoSpace space) override;
  StatusOr<brillo::Blob> Read(RoSpace space) override;
  StatusOr<brillo::Blob> Certify(RoSpace space, Key key) override;
};

}  // namespace hwsec

#endif  // LIBHWSEC_BACKEND_TPM2_RO_DATA_H_
