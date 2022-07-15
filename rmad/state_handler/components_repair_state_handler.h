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
#include "rmad/utils/crossystem_utils.h"

namespace rmad {

class ComponentsRepairStateHandler : public BaseStateHandler {
 public:
  explicit ComponentsRepairStateHandler(
      scoped_refptr<JsonStore> json_store,
      scoped_refptr<DaemonCallback> daemon_callback);
  // Used to inject mocked |cryptohome_client_|, |runtime_probe_client_|,
  // |cr50_utils_| and |crossystem_utils_| for testing.
  explicit ComponentsRepairStateHandler(
      scoped_refptr<JsonStore> json_store,
      scoped_refptr<DaemonCallback> daemon_callback,
      std::unique_ptr<CryptohomeClient> cryptohome_client,
      std::unique_ptr<RuntimeProbeClient> runtime_probe_client,
      std::unique_ptr<Cr50Utils> cr50_utils,
      std::unique_ptr<CrosSystemUtils> crossystem_utils);

  ASSIGN_STATE(RmadState::StateCase::kComponentsRepair);
  SET_REPEATABLE;

  RmadErrorCode InitializeState() override;
  void CleanUpState() override;
  GetNextStateCaseReply GetNextStateCase(const RmadState& state) override;

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
  std::unique_ptr<CrosSystemUtils> crossystem_utils_;
};

namespace fake {

class FakeComponentsRepairStateHandler : public ComponentsRepairStateHandler {
 public:
  explicit FakeComponentsRepairStateHandler(
      scoped_refptr<JsonStore> json_store,
      scoped_refptr<DaemonCallback> daemon_callback,
      const base::FilePath& working_dir_path);

 protected:
  ~FakeComponentsRepairStateHandler() override = default;
};

}  // namespace fake

}  // namespace rmad

#endif  // RMAD_STATE_HANDLER_COMPONENTS_REPAIR_STATE_HANDLER_H_
