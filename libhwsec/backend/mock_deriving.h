// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_BACKEND_MOCK_DERIVING_H_
#define LIBHWSEC_BACKEND_MOCK_DERIVING_H_

#include <cstdint>

#include <brillo/secure_blob.h>
#include <gmock/gmock.h>

#include "libhwsec/backend/deriving.h"
#include "libhwsec/status.h"
#include "libhwsec/structures/key.h"

namespace hwsec {

class MockDeriving : public Deriving {
 public:
  MOCK_METHOD(StatusOr<brillo::Blob>,
              Derive,
              (Key key, const brillo::Blob& blob),
              (override));
  MOCK_METHOD(StatusOr<brillo::SecureBlob>,
              SecureDerive,
              (Key key, const brillo::SecureBlob& blob),
              (override));
};

}  // namespace hwsec

#endif  // LIBHWSEC_BACKEND_MOCK_DERIVING_H_
