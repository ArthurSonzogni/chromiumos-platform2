// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_BACKEND_MOCK_RO_DATA_H_
#define LIBHWSEC_BACKEND_MOCK_RO_DATA_H_

#include <brillo/secure_blob.h>
#include <gmock/gmock.h>

#include "libhwsec/backend/ro_data.h"
#include "libhwsec/status.h"
#include "libhwsec/structures/key.h"
#include "libhwsec/structures/space.h"

namespace hwsec {

class MockRoData : public RoData {
 public:
  MOCK_METHOD(StatusOr<bool>, IsReady, (RoSpace space), (override));
  MOCK_METHOD(StatusOr<brillo::Blob>, Read, (RoSpace space), (override));
  MOCK_METHOD(StatusOr<brillo::Blob>,
              Certify,
              (RoSpace space, Key key),
              (override));
};

}  // namespace hwsec

#endif  // LIBHWSEC_BACKEND_MOCK_RO_DATA_H_
