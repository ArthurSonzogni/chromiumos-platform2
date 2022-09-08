// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_STATE_HANDLER_WRITE_PROTECT_ENABLE_PHYSICAL_STATE_HANDLER_H_
#define RMAD_STATE_HANDLER_WRITE_PROTECT_ENABLE_PHYSICAL_STATE_HANDLER_H_

#include "rmad/state_handler/base_state_handler.h"

#include <memory>
#include <utility>

#include <base/files/file_path.h>
#include <base/timer/timer.h>

#include "rmad/utils/crossystem_utils.h"
#include "rmad/utils/flashrom_utils.h"

namespace rmad {

class CrosSystemUtils;

class WriteProtectEnablePhysicalStateHandler : public BaseStateHandler {
 public:
  // Poll every 2 seconds.
  static constexpr base::TimeDelta kPollInterval = base::Seconds(2);

  explicit WriteProtectEnablePhysicalStateHandler(
      scoped_refptr<JsonStore> json_store,
      scoped_refptr<DaemonCallback> daemon_callback);
  // Used to inject mock |crossystem_utils_| and |flashrom_utils_| for testing.
  explicit WriteProtectEnablePhysicalStateHandler(
      scoped_refptr<JsonStore> json_store,
      scoped_refptr<DaemonCallback> daemon_callback,
      std::unique_ptr<CrosSystemUtils> crossystem_utils,
      std::unique_ptr<FlashromUtils> flashrom_utils);

  ASSIGN_STATE(RmadState::StateCase::kWpEnablePhysical);
  SET_UNREPEATABLE;

  RmadErrorCode InitializeState() override;
  void RunState() override;
  void CleanUpState() override;
  GetNextStateCaseReply GetNextStateCase(const RmadState& state) override;

 protected:
  ~WriteProtectEnablePhysicalStateHandler() override = default;

 private:
  void CheckWriteProtectOnTask();

  base::RepeatingTimer timer_;

  std::unique_ptr<CrosSystemUtils> crossystem_utils_;
  std::unique_ptr<FlashromUtils> flashrom_utils_;
};

}  // namespace rmad

#endif  // RMAD_STATE_HANDLER_WRITE_PROTECT_ENABLE_PHYSICAL_STATE_HANDLER_H_
