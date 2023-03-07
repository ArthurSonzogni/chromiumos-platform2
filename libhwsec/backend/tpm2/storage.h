// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_BACKEND_TPM2_STORAGE_H_
#define LIBHWSEC_BACKEND_TPM2_STORAGE_H_

#include <cstdint>

#include <brillo/secure_blob.h>

#include "libhwsec/backend/storage.h"
#include "libhwsec/proxy/proxy.h"
#include "libhwsec/status.h"

namespace hwsec {

class StorageTpm2 : public Storage {
 public:
  StorageTpm2(org::chromium::TpmManagerProxyInterface& tpm_manager,
              org::chromium::TpmNvramProxyInterface& tpm_nvram)
      : tpm_manager_(tpm_manager), tpm_nvram_(tpm_nvram) {}

  StatusOr<ReadyState> IsReady(Space space) override;
  Status Prepare(Space space, uint32_t size) override;
  StatusOr<brillo::Blob> Load(Space space) override;
  Status Store(Space space, const brillo::Blob& blob) override;
  Status Lock(Space space, LockOptions options) override;
  Status Destroy(Space space) override;
  StatusOr<bool> IsWriteLocked(Space space) override;

 private:
  org::chromium::TpmManagerProxyInterface& tpm_manager_;
  org::chromium::TpmNvramProxyInterface& tpm_nvram_;
};

}  // namespace hwsec

#endif  // LIBHWSEC_BACKEND_TPM2_STORAGE_H_
