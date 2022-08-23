// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_BACKEND_MOCK_RANDOM_H_
#define LIBHWSEC_BACKEND_MOCK_RANDOM_H_

#include <brillo/secure_blob.h>
#include <gmock/gmock.h>

#include "libhwsec/backend/random.h"
#include "libhwsec/status.h"

namespace hwsec {

class MockRandom : public Random {
 public:
  MOCK_METHOD(StatusOr<brillo::Blob>, RandomBlob, (size_t size), (override));
  MOCK_METHOD(StatusOr<brillo::SecureBlob>,
              RandomSecureBlob,
              (size_t size),
              (override));
};

}  // namespace hwsec

#endif  // LIBHWSEC_BACKEND_MOCK_RANDOM_H_
