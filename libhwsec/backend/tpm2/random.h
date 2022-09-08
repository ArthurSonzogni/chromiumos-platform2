// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_BACKEND_TPM2_RANDOM_H_
#define LIBHWSEC_BACKEND_TPM2_RANDOM_H_

#include <brillo/secure_blob.h>

#include "libhwsec/backend/backend.h"
#include "libhwsec/status.h"

namespace hwsec {

class BackendTpm2;

class RandomTpm2 : public Backend::Random,
                   public Backend::SubClassHelper<BackendTpm2> {
 public:
  using SubClassHelper::SubClassHelper;
  StatusOr<brillo::Blob> RandomBlob(size_t size) override;
  StatusOr<brillo::SecureBlob> RandomSecureBlob(size_t size) override;
};

}  // namespace hwsec

#endif  // LIBHWSEC_BACKEND_TPM2_RANDOM_H_
