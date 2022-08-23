// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_BACKEND_MOCK_DA_MITIGATION_H_
#define LIBHWSEC_BACKEND_MOCK_DA_MITIGATION_H_

#include <gmock/gmock.h>

#include "libhwsec/status.h"

namespace hwsec {

class MockDAMitigation : public DAMitigation {
 public:
  MOCK_METHOD(StatusOr<bool>, IsReady, (), (override));
  MOCK_METHOD(StatusOr<DAMitigationStatus>, GetStatus, (), (override));
  MOCK_METHOD(Status, Mitigate, (), (override));
};

}  // namespace hwsec

#endif  // LIBHWSEC_BACKEND_MOCK_DA_MITIGATION_H_
