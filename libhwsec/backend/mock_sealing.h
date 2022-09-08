// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_BACKEND_MOCK_SEALING_H_
#define LIBHWSEC_BACKEND_MOCK_SEALING_H_

#include <optional>

#include <brillo/secure_blob.h>
#include <gmock/gmock.h>

#include "libhwsec/backend/sealing.h"
#include "libhwsec/status.h"
#include "libhwsec/structures/key.h"
#include "libhwsec/structures/operation_policy.h"

namespace hwsec {

class MockSealing : public Sealing {
 public:
  MOCK_METHOD(StatusOr<bool>, IsSupported, (), (override));
  MOCK_METHOD(StatusOr<brillo::Blob>,
              Seal,
              (const OperationPolicySetting& policy,
               const brillo::SecureBlob& unsealed_data),
              (override));
  MOCK_METHOD(StatusOr<std::optional<ScopedKey>>,
              PreloadSealedData,
              (const OperationPolicy& policy, const brillo::Blob& sealed_data),
              (override));
  MOCK_METHOD(StatusOr<brillo::SecureBlob>,
              Unseal,
              (const OperationPolicy& policy,
               const brillo::Blob& sealed_data,
               UnsealOptions options),
              (override));
};

}  // namespace hwsec

#endif  // LIBHWSEC_BACKEND_MOCK_SEALING_H_
