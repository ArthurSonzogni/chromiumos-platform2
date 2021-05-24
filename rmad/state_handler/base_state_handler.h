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
  // Return value for GetNextStateCase().
  struct GetNextStateCaseReply {
    RmadErrorCode error;
    RmadState::StateCase state_case;
  };

  explicit BaseStateHandler(scoped_refptr<JsonStore> json_store);
  virtual ~BaseStateHandler() = default;

  // Returns the RmadState that the class handles. This can be declared by the
  // macro ASSIGN_STATE(state).
  virtual RmadState::StateCase GetStateCase() const = 0;

  // TODO(gavindodd): How to mock this without making it virtual?
  // Returns the RmadState proto for this state.
  virtual const RmadState& GetState() const { return state_; }

  // Returns whether the state is repeatable. A state is repeatable if it can be
  // run multiple times. For instance, a state that only shows system info, or
  // some calibration that can be done multiple times. We shouldn't visit an
  // unrepeatable state twice, unless we restart the RMA process again. A state
  // is unrepeatable by default, and can be set as repeatable by the macro
  // SET_REPEATABLE.
  virtual bool IsRepeatable() const { return false; }

  // Return the next RmadState::StateCase in the RMA flow depending on device
  // status and user input (e.g. |json_store_| content). If the transition
  // fails, a corresponding RmadErrorCode is set, and |next_state_case| will be
  // the same as GetStateCase().
  virtual GetNextStateCaseReply GetNextStateCase(const RmadState& state) = 0;

  // Reset the state. Used when entering or returning to the state.
  virtual RmadErrorCode ResetState() = 0;

  // Store the state to |json_store_|.
  bool StoreState();

  // Retrieve the state from |json_store_|.
  bool RetrieveState();

 protected:
  RmadState state_;
  scoped_refptr<JsonStore> json_store_;
};

#define ASSIGN_STATE(state) \
  RmadState::StateCase GetStateCase() const override { return state; }

#define SET_REPEATABLE \
  bool IsRepeatable() const override { return true; }

}  // namespace rmad

#endif  // RMAD_STATE_HANDLER_BASE_STATE_HANDLER_H_
