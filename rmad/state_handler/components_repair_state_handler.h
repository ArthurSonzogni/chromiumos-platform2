// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_STATE_HANDLER_COMPONENTS_REPAIR_STATE_HANDLER_H_
#define RMAD_STATE_HANDLER_COMPONENTS_REPAIR_STATE_HANDLER_H_

#include "rmad/state_handler/base_state_handler.h"

#include <memory>

#include <base/files/file_path.h>

#include "rmad/system/cryptohome_client.h"
#include "rmad/system/runtime_probe_client.h"
#include "rmad/utils/cr50_utils.h"

namespace rmad {

class ComponentsRepairStateHandler : public BaseStateHandler {
 public:
  explicit ComponentsRepairStateHandler(scoped_refptr<JsonStore> json_store);
  // Used to inject mocked |cryptohome_client_|, |runtime_probe_client_| and
  // |cr50_utils_| for testing.
  ComponentsRepairStateHandler(
      scoped_refptr<JsonStore> json_store,
      std::unique_ptr<CryptohomeClient> cryptohome_client,
      std::unique_ptr<RuntimeProbeClient> runtime_probe_client,
      std::unique_ptr<Cr50Utils> cr50_utils);

  ASSIGN_STATE(RmadState::StateCase::kComponentsRepair);
  SET_REPEATABLE;

  RmadErrorCode InitializeState() override;
  void CleanUpState() override;
  GetNextStateCaseReply GetNextStateCase(const RmadState& state) override;

  // Do not auto-transition at boot as there might be new detected components.
  GetNextStateCaseReply TryGetNextStateCaseAtBoot() override {
    return NextStateCaseWrapper(RMAD_ERROR_TRANSITION_FAILED);
  }

 protected:
  ~ComponentsRepairStateHandler() override = default;

 private:
  // Use the provided state to update every component. Return true if all the
  // components are properly updated.
  bool ApplyUserSelection(const RmadState& state);
  // Store variables that can be used by other state handlers to make decisions.
  bool StoreVars() const;

  bool active_;
  std::unique_ptr<CryptohomeClient> cryptohome_client_;
  std::unique_ptr<RuntimeProbeClient> runtime_probe_client_;
  std::unique_ptr<Cr50Utils> cr50_utils_;
};

namespace fake {

class FakeComponentsRepairStateHandler : public ComponentsRepairStateHandler {
 public:
  FakeComponentsRepairStateHandler(scoped_refptr<JsonStore> json_store,
                                   const base::FilePath& working_dir_path);

 protected:
  ~FakeComponentsRepairStateHandler() override = default;
};

}  // namespace fake

}  // namespace rmad

#endif  // RMAD_STATE_HANDLER_COMPONENTS_REPAIR_STATE_HANDLER_H_
