// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_STATE_HANDLER_REPAIR_COMPLETE_STATE_HANDLER_H_
#define RMAD_STATE_HANDLER_REPAIR_COMPLETE_STATE_HANDLER_H_

#include "rmad/state_handler/base_state_handler.h"

#include <memory>

#include <base/files/file_path.h>
#include <base/timer/timer.h>

#include "rmad/system/power_manager_client.h"

namespace rmad {

class RepairCompleteStateHandler : public BaseStateHandler {
 public:
  // Wait for 5 seconds before reboot/shutdown/cutoff.
  static constexpr base::TimeDelta kShutdownDelay =
      base::TimeDelta::FromSeconds(5);

  explicit RepairCompleteStateHandler(scoped_refptr<JsonStore> json_store);
  // Used to inject |working_dir_path_| and mocked |power_manager_client_| for
  // testing.
  RepairCompleteStateHandler(
      scoped_refptr<JsonStore> json_store,
      const base::FilePath& working_dir_path,
      std::unique_ptr<PowerManagerClient> power_manager_client);

  ASSIGN_STATE(RmadState::StateCase::kRepairComplete);
  SET_UNREPEATABLE;

  RmadErrorCode InitializeState() override;
  GetNextStateCaseReply GetNextStateCase(const RmadState& state) override;

 protected:
  ~RepairCompleteStateHandler() override = default;

 private:
  void Reboot();
  void Shutdown();
  void Cutoff();

  base::FilePath working_dir_path_;
  std::unique_ptr<PowerManagerClient> power_manager_client_;
  base::OneShotTimer timer_;
};

namespace fake {

class FakeRepairCompleteStateHandler : public RepairCompleteStateHandler {
 public:
  FakeRepairCompleteStateHandler(scoped_refptr<JsonStore> json_store,
                                 const base::FilePath& working_dir_path);

 protected:
  ~FakeRepairCompleteStateHandler() override = default;
};

}  // namespace fake

}  // namespace rmad

#endif  // RMAD_STATE_HANDLER_REPAIR_COMPLETE_STATE_HANDLER_H_
