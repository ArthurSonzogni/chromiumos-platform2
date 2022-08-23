// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_BACKEND_MOCK_STATE_H_
#define LIBHWSEC_BACKEND_MOCK_STATE_H_

#include <gmock/gmock.h>

#include "libhwsec/backend/state.h"
#include "libhwsec/status.h"

namespace hwsec {

class MockState : public State {
 public:
  MOCK_METHOD(StatusOr<bool>, IsEnabled, (), (override));
  MOCK_METHOD(StatusOr<bool>, IsReady, (), (override));
  MOCK_METHOD(Status, Prepare, (), (override));
};

}  // namespace hwsec

#endif  // LIBHWSEC_BACKEND_MOCK_STATE_H_
