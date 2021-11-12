// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_STATE_HANDLER_WRITE_PROTECT_DISABLE_PHYSICAL_STATE_HANDLER_H_
#define RMAD_STATE_HANDLER_WRITE_PROTECT_DISABLE_PHYSICAL_STATE_HANDLER_H_

#include "rmad/state_handler/base_state_handler.h"

#include <memory>
#include <utility>

#include <base/timer/timer.h>

#include "rmad/system/cryptohome_client.h"
#include "rmad/system/power_manager_client.h"
#include "rmad/utils/cr50_utils.h"
#include "rmad/utils/crossystem_utils.h"

namespace rmad {

class WriteProtectDisablePhysicalStateHandler : public BaseStateHandler {
 public:
  // Poll every 2 seconds.
  static constexpr base::TimeDelta kPollInterval =
      base::TimeDelta::FromSeconds(2);
  // Wait for 5 seconds before rebooting.
  static constexpr base::TimeDelta kRebootDelay =
      base::TimeDelta::FromSeconds(5);

  explicit WriteProtectDisablePhysicalStateHandler(
      scoped_refptr<JsonStore> json_store);
  // Used to inject mock |cr50_utils_|, |crossystem_utils_|,
  // and |power_manager_client_| for testing.
  WriteProtectDisablePhysicalStateHandler(
      scoped_refptr<JsonStore> json_store,
      std::unique_ptr<Cr50Utils> cr50_utils,
      std::unique_ptr<CrosSystemUtils> crossystem_utils,
      std::unique_ptr<PowerManagerClient> power_manager_client,
      std::unique_ptr<CryptohomeClient> cryptohome_client);

  ASSIGN_STATE(RmadState::StateCase::kWpDisablePhysical);
  SET_REPEATABLE;

  void RegisterSignalSender(
      std::unique_ptr<base::RepeatingCallback<bool(bool)>> callback) override {
    write_protect_signal_sender_ = std::move(callback);
  }

  RmadErrorCode InitializeState() override;
  void CleanUpState() override;
  GetNextStateCaseReply GetNextStateCase(const RmadState& state) override;

 protected:
  ~WriteProtectDisablePhysicalStateHandler() override = default;

 private:
  bool IsHwwpDisabled() const;
  bool IsFactoryModeTried() const;
  void PollUntilWriteProtectOff();
  void CheckWriteProtectOffTask();

  std::unique_ptr<Cr50Utils> cr50_utils_;
  std::unique_ptr<CrosSystemUtils> crossystem_utils_;
  std::unique_ptr<PowerManagerClient> power_manager_client_;
  std::unique_ptr<CryptohomeClient> cryptohome_client_;
  std::unique_ptr<base::RepeatingCallback<bool(bool)>>
      write_protect_signal_sender_;
  base::RepeatingTimer timer_;
};

}  // namespace rmad

#endif  // RMAD_STATE_HANDLER_WRITE_PROTECT_DISABLE_PHYSICAL_STATE_HANDLER_H_
