// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_STATE_HANDLER_BASE_STATE_HANDLER_H_
#define RMAD_STATE_HANDLER_BASE_STATE_HANDLER_H_

#include <base/memory/ref_counted.h>
#include <base/memory/scoped_refptr.h>

#include "rmad/proto_bindings/rmad.pb.h"
#include "rmad/utils/json_store.h"

namespace rmad {

class BaseStateHandler : public base::RefCounted<BaseStateHandler> {
 public:
  explicit BaseStateHandler(scoped_refptr<JsonStore> json_store);
  virtual ~BaseStateHandler() = default;

  // Returns the RmadState that the class handles. This can be declared by the
  // macro ASSIGN_STATE(state).
  virtual RmadState::StateCase GetStateCase() const = 0;

  // TODO(chenghan): Make this a global state so states block abort permanently.
  // Returns whether it's allowed to abort the RMA process from the state. This
  // is not allowed by default, and can be set as allowed by the macro
  // SET_ALLOW_ABORT.
  virtual bool IsAllowAbort() const { return false; }

  // Store the next RmadState in the RMA flow depending on device status and
  // user input (e.g. |json_store_| content) to |next_state|. Return true if a
  // transition is valid, false if the device status is not eligible for a state
  // transition (in this case |next_state| will be the same as GetStateCase()).
  virtual RmadState::StateCase GetNextStateCase() const = 0;

  // Validates the new state and updates the stored state, triggering any work
  // required and updating the state as needed.
  virtual RmadErrorCode UpdateState(const RmadState& state) = 0;

  // Reset the state.
  // Used when transitioning to the previous state.
  virtual RmadErrorCode ResetState() = 0;

  // TODO(gavindodd): How to mock this without making it virtual?
  // Returns the RmadState proto for this state.
  virtual const RmadState& GetState() const { return state_; }

 protected:
  RmadState state_;
  scoped_refptr<JsonStore> json_store_;
};

#define ASSIGN_STATE(state) \
  RmadState::StateCase GetStateCase() const override { return state; }

#define SET_ALLOW_ABORT \
  bool IsAllowAbort() const override { return true; }

}  // namespace rmad

#endif  // RMAD_STATE_HANDLER_BASE_STATE_HANDLER_H_
