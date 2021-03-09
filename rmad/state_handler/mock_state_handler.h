// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_STATE_HANDLER_MOCK_STATE_HANDLER_H_
#define RMAD_STATE_HANDLER_MOCK_STATE_HANDLER_H_

#include "rmad/state_handler/base_state_handler.h"

#include <gmock/gmock.h>

namespace rmad {

class MockStateHandler : public BaseStateHandler {
 public:
  explicit MockStateHandler(JsonStore* json_store)
      : BaseStateHandler(json_store) {}
  virtual ~MockStateHandler() = default;

  MOCK_METHOD(RmadState, GetState, (), (const, override));
  MOCK_METHOD(RmadState, GetNextState, (), (const, override));
};

}  // namespace rmad

#endif  // RMAD_STATE_HANDLER_MOCK_STATE_HANDLER_H_
