// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/welcome_screen_state_handler.h"

namespace rmad {

WelcomeScreenStateHandler::WelcomeScreenStateHandler(
    scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store) {}

bool WelcomeScreenStateHandler::GetNextState(RmadState* next_state) const {
  // This is currently fake.
  if (next_state) {
    *next_state = RMAD_STATE_UNKNOWN;
  }
  return true;
}

}  // namespace rmad
