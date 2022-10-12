// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_BACKEND_MOCK_SIGNING_H_
#define LIBHWSEC_BACKEND_MOCK_SIGNING_H_

#include <brillo/secure_blob.h>
#include <gmock/gmock.h>

#include "libhwsec/backend/signing.h"
#include "libhwsec/status.h"
#include "libhwsec/structures/key.h"
#include "libhwsec/structures/operation_policy.h"

namespace hwsec {

class MockSigning : public Signing {
 public:
  MOCK_METHOD(StatusOr<brillo::Blob>,
              Sign,
              (Key key,
               const brillo::Blob& data,
               const SigningOptions& options),
              (override));
  MOCK_METHOD(StatusOr<brillo::Blob>,
              RawSign,
              (Key key,
               const brillo::Blob& data,
               const SigningOptions& options),
              (override));
  MOCK_METHOD(Status,
              Verify,
              (Key key, const brillo::Blob& signed_data),
              (override));
};

}  // namespace hwsec

#endif  // LIBHWSEC_BACKEND_MOCK_SIGNING_H_
