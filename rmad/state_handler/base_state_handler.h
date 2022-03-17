// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_STATE_HANDLER_BASE_STATE_HANDLER_H_
#define RMAD_STATE_HANDLER_BASE_STATE_HANDLER_H_

#include <base/callback.h>
#include <base/memory/ref_counted.h>
#include <base/memory/scoped_refptr.h>

#include "rmad/metrics/metrics_constants.h"
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

  // Returns the RmadState that the class handles. This can be declared by the
  // macro ASSIGN_STATE(state).
  virtual RmadState::StateCase GetStateCase() const = 0;

  // TODO(chenghan): How to mock this without making it virtual?
  // Returns the RmadState proto for this state.
  virtual const RmadState& GetState(bool do_task = false) const;

  // Returns whether the state is repeatable. A state is repeatable if it can be
  // run multiple times. For instance, a state that only shows system info, or
  // some calibration that can be done multiple times. We shouldn't visit an
  // unrepeatable state twice, unless we restart the RMA process again. This can
  // be set by the macro SET_REPEATABLE and SET_UNREPEATABLE.
  virtual bool IsRepeatable() const = 0;

  // Initialize the state. Used when entering or returning to the state.
  virtual RmadErrorCode InitializeState() = 0;

  // Clean up the state. Called when leaving the state.
  virtual void CleanUpState() {}

  // Register a signal sender.
  virtual void RegisterSignalSender(
      base::RepeatingCallback<void(bool)> callback) {}

  using HardwareVerificationResultSignalCallback =
      base::RepeatingCallback<void(const HardwareVerificationResult&)>;
  virtual void RegisterSignalSender(
      HardwareVerificationResultSignalCallback callback) {}

  using UpdateRoFirmwareStatusSignalCallback =
      base::RepeatingCallback<void(UpdateRoFirmwareStatus)>;
  virtual void RegisterSignalSender(
      UpdateRoFirmwareStatusSignalCallback callback) {}

  using CalibrationOverallSignalCallback =
      base::RepeatingCallback<void(CalibrationOverallStatus)>;
  virtual void RegisterSignalSender(CalibrationOverallSignalCallback callback) {
  }

  using CalibrationComponentSignalCallback =
      base::RepeatingCallback<void(CalibrationComponentStatus)>;
  virtual void RegisterSignalSender(
      CalibrationComponentSignalCallback callback) {}

  using ProvisionSignalCallback =
      base::RepeatingCallback<void(const ProvisionStatus&)>;
  virtual void RegisterSignalSender(ProvisionSignalCallback callback) {}

  using FinalizeSignalCallback =
      base::RepeatingCallback<void(const FinalizeStatus&)>;
  virtual void RegisterSignalSender(FinalizeSignalCallback callback) {}

  // Return the next RmadState::StateCase in the RMA flow depending on device
  // status and user input (e.g. |json_store_| content). If the transition
  // fails, a corresponding RmadErrorCode is set, and |next_state_case| will be
  // the same as GetStateCase().
  virtual GetNextStateCaseReply GetNextStateCase(const RmadState& state) = 0;

  // Return the next RmadState::StateCase in the RMA flow at the boot time,
  // depending on device status.
  virtual GetNextStateCaseReply TryGetNextStateCaseAtBoot() {
    return GetNextStateCase(state_);
  }

  // Store the state to |json_store_|.
  bool StoreState();

  // Retrieve the state from |json_store_|.
  bool RetrieveState();

  // Return the state_case and store the error code and the additional activity
  // to |json_store_| if needed.
  GetNextStateCaseReply NextStateCaseWrapper(RmadState::StateCase state_case,
                                             RmadErrorCode error,
                                             AdditionalActivity activity);

  GetNextStateCaseReply NextStateCaseWrapper(RmadState::StateCase state_case);

  GetNextStateCaseReply NextStateCaseWrapper(RmadErrorCode error);

  // Store the error code to |json_store_|.
  bool StoreErrorCode(RmadErrorCode error);

  // Store the additional activity to |json_store_|.
  bool StoreAdditionalActivity(AdditionalActivity activity);

 protected:
  friend class base::RefCounted<BaseStateHandler>;
  virtual ~BaseStateHandler() = default;

  // Invoked when |GetState| is called with |do_task| = true. Can be overridden
  // by child classes.
  virtual void OnGetStateTask() const {}

  RmadState state_;
  scoped_refptr<JsonStore> json_store_;
};

#define ASSIGN_STATE(state) \
  RmadState::StateCase GetStateCase() const override { return state; }

#define SET_REPEATABLE \
  bool IsRepeatable() const override { return true; }
#define SET_UNREPEATABLE \
  bool IsRepeatable() const override { return false; }

}  // namespace rmad

#endif  // RMAD_STATE_HANDLER_BASE_STATE_HANDLER_H_
