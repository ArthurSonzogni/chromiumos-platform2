// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_BACKEND_TPM1_STORAGE_H_
#define LIBHWSEC_BACKEND_TPM1_STORAGE_H_

#include <cstdint>

#include <brillo/secure_blob.h>

#include "libhwsec/backend/backend.h"
#include "libhwsec/status.h"

namespace hwsec {

class BackendTpm1;

class StorageTpm1 : public Backend::Storage,
                    public Backend::SubClassHelper<BackendTpm1> {
 public:
  using SubClassHelper::SubClassHelper;
  StatusOr<ReadyState> IsReady(Space space) override;
  Status Prepare(Space space, uint32_t size) override;
  StatusOr<brillo::Blob> Load(Space space) override;
  Status Store(Space space, const brillo::Blob& blob) override;
  Status Lock(Space space, LockOptions options) override;
  Status Destroy(Space space) override;
  StatusOr<bool> IsWriteLocked(Space space) override;
};

}  // namespace hwsec

#endif  // LIBHWSEC_BACKEND_TPM1_STORAGE_H_
