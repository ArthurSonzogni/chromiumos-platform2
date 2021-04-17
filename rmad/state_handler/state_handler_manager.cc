// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/state_handler_manager.h"

#include <utility>

#include <base/check.h>
#include <base/memory/scoped_refptr.h>

#include "rmad/state_handler/welcome_screen_state_handler.h"

namespace rmad {

StateHandlerManager::StateHandlerManager(scoped_refptr<JsonStore> json_store)
    : json_store_(json_store) {}

void StateHandlerManager::RegisterStateHandler(
    scoped_refptr<BaseStateHandler> handler) {
  RmadState::StateCase state = handler->GetStateCase();
  auto res = state_handler_map_.insert(std::make_pair(state, handler));
  // Check if there are StateId collisions.
  DCHECK(res.second) << "Registered handlers should have unique RmadStates.";
}

void StateHandlerManager::InitializeStateHandlers() {
  // TODO(gavindodd): Some form of validation of state loaded from the store is
  // needed. e.g. RMA abortable state must match what is expected by the
  // current position in the state flow, but depends on some state in the
  // history.
  // Maybe initializing states in history order would help?
  RegisterStateHandler(
      base::MakeRefCounted<WelcomeScreenStateHandler>(json_store_));
}

scoped_refptr<BaseStateHandler> StateHandlerManager::GetStateHandler(
    RmadState::StateCase state) const {
  auto it = state_handler_map_.find(state);
  if (it == state_handler_map_.end()) {
    // Unregistered RmadState, return an empty pointer.
    return scoped_refptr<BaseStateHandler>(nullptr);
  }
  return it->second;
}

}  // namespace rmad
