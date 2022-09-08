// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_BACKEND_TPM1_SEALING_H_
#define LIBHWSEC_BACKEND_TPM1_SEALING_H_

#include <optional>

#include <brillo/secure_blob.h>

#include "libhwsec/backend/backend.h"
#include "libhwsec/status.h"
#include "libhwsec/tss_utils/scoped_tss_type.h"

namespace hwsec {

class BackendTpm1;

class SealingTpm1 : public Backend::Sealing,
                    public Backend::SubClassHelper<BackendTpm1> {
 public:
  using SubClassHelper::SubClassHelper;
  StatusOr<bool> IsSupported() override;
  StatusOr<brillo::Blob> Seal(const OperationPolicySetting& policy,
                              const brillo::SecureBlob& unsealed_data) override;
  StatusOr<std::optional<ScopedKey>> PreloadSealedData(
      const OperationPolicy& policy, const brillo::Blob& sealed_data) override;
  StatusOr<brillo::SecureBlob> Unseal(const OperationPolicy& policy,
                                      const brillo::Blob& sealed_data,
                                      UnsealOptions options) override;

 private:
  StatusOr<ScopedTssKey> GetAuthValueKey(const brillo::SecureBlob& auth_value);
};

}  // namespace hwsec

#endif  // LIBHWSEC_BACKEND_TPM1_SEALING_H_
