// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_MOCK_RMAD_INTERFACE_H_
#define RMAD_MOCK_RMAD_INTERFACE_H_

#include "rmad/rmad_interface.h"

#include <memory>

#include <gmock/gmock.h>

namespace rmad {

class MockRmadInterface : public RmadInterface {
 public:
  MockRmadInterface() = default;
  virtual ~MockRmadInterface() = default;

  MOCK_METHOD(void,
              RegisterSignalSender,
              (RmadState::StateCase,
               std::unique_ptr<base::RepeatingCallback<bool(bool)>>),
              (override));
  MOCK_METHOD(void,
              RegisterSignalSender,
              (RmadState::StateCase,
               std::unique_ptr<CalibrationSignalCallback>),
              (override));

  MOCK_METHOD(RmadState::StateCase, GetCurrentStateCase, (), (override));
  MOCK_METHOD(void, TryTransitionNextStateFromCurrentState, (), (override));
  MOCK_METHOD(void, GetCurrentState, (const GetStateCallback&), (override));
  MOCK_METHOD(void,
              TransitionNextState,
              (const TransitionNextStateRequest&, const GetStateCallback&),
              (override));
  MOCK_METHOD(void,
              TransitionPreviousState,
              (const GetStateCallback&),
              (override));
  MOCK_METHOD(void, AbortRma, (const AbortRmaCallback&), (override));
  MOCK_METHOD(bool, AllowAbort, (), (const, override));
  MOCK_METHOD(void, GetLogPath, (const GetLogPathCallback&), (override));
};

}  // namespace rmad

#endif  // RMAD_MOCK_RMAD_INTERFACE_H_
