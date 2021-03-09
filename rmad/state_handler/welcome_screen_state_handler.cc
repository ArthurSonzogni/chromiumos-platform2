// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/welcome_screen_state_handler.h"

namespace rmad {

WelcomeScreenStateHandler::WelcomeScreenStateHandler(JsonStore* json_store)
    : BaseStateHandler(json_store) {}

RmadState WelcomeScreenStateHandler::GetNextState() const {
  // This is currently fake.
  return STATE_UNKNOWN;
}

}  // namespace rmad
