// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_RMAD_INTERFACE_IMPL_H_
#define RMAD_RMAD_INTERFACE_IMPL_H_

#include "rmad/rmad_interface.h"

#include <memory>
#include <vector>

#include <base/memory/scoped_refptr.h>

#include "rmad/state_handler/state_handler_manager.h"
#include "rmad/system/runtime_probe_client.h"
#include "rmad/system/shill_client.h"
#include "rmad/system/tpm_manager_client.h"
#include "rmad/utils/json_store.h"

namespace rmad {

class RmadInterfaceImpl final : public RmadInterface {
 public:
  RmadInterfaceImpl();
  // Used to inject mocked |json_store_|, |state_handler_manager_|,
  // |runtime_probe_client_|, |shill_client_| and |tpm_manager_client_|.
  RmadInterfaceImpl(scoped_refptr<JsonStore> json_store,
                    std::unique_ptr<StateHandlerManager> state_handler_manager,
                    std::unique_ptr<RuntimeProbeClient> runtime_probe_client,
                    std::unique_ptr<ShillClient> shill_client,
                    std::unique_ptr<TpmManagerClient> tpm_manager_client);
  RmadInterfaceImpl(const RmadInterfaceImpl&) = delete;
  RmadInterfaceImpl& operator=(const RmadInterfaceImpl&) = delete;

  ~RmadInterfaceImpl() override = default;

  bool Initialize() override;

  void RegisterSignalSender(
      RmadState::StateCase state_case,
      std::unique_ptr<base::RepeatingCallback<bool(bool)>> callback) override;

  void RegisterSignalSender(
      RmadState::StateCase state_case,
      std::unique_ptr<HardwareVerificationResultSignalCallback> callback)
      override;

  void RegisterSignalSender(
      RmadState::StateCase state_case,
      std::unique_ptr<CalibrationOverallSignalCallback> callback) override;

  void RegisterSignalSender(
      RmadState::StateCase state_case,
      std::unique_ptr<CalibrationComponentSignalCallback> callback) override;

  void RegisterSignalSender(
      RmadState::StateCase state_case,
      std::unique_ptr<ProvisionSignalCallback> callback) override;

  void RegisterSignalSender(
      RmadState::StateCase state_case,
      std::unique_ptr<FinalizeSignalCallback> callback) override;

  RmadState::StateCase GetCurrentStateCase() override {
    return current_state_case_;
  }
  void TryTransitionNextStateFromCurrentState() override;
  void GetCurrentState(const GetStateCallback& callback) override;
  void TransitionNextState(const TransitionNextStateRequest& request,
                           const GetStateCallback& callback) override;
  void TransitionPreviousState(const GetStateCallback& callback) override;
  void AbortRma(const AbortRmaCallback& callback) override;
  bool CanAbort() const override { return can_abort_; }

 private:
  // Get and initialize the state handler for |state case|, and store it to
  // |state_handler|. If there's no state handler for |state_case|, or the
  // initialization fails, return an error, and |state_handler| is unchanged.
  RmadErrorCode GetInitializedStateHandler(
      RmadState::StateCase state_case,
      scoped_refptr<BaseStateHandler>* state_handler) const;

  GetStateReply GetCurrentStateInternal();
  GetStateReply TransitionNextStateInternal(
      const TransitionNextStateRequest& request);

  // Store the state history to |json_store_|.
  bool StoreStateHistory();

  // Check if it's allowed to go back to the previous state.
  bool CanGoBack() const;

  // External utilities.
  scoped_refptr<JsonStore> json_store_;
  std::unique_ptr<StateHandlerManager> state_handler_manager_;
  std::unique_ptr<RuntimeProbeClient> runtime_probe_client_;
  std::unique_ptr<ShillClient> shill_client_;
  std::unique_ptr<TpmManagerClient> tpm_manager_client_;

  // Internal states.
  bool external_utils_initialized_;
  RmadState::StateCase current_state_case_;
  std::vector<RmadState::StateCase> state_history_;
  bool can_abort_;
};

}  // namespace rmad

#endif  // RMAD_RMAD_INTERFACE_IMPL_H_
