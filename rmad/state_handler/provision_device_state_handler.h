// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_STATE_HANDLER_PROVISION_DEVICE_STATE_HANDLER_H_
#define RMAD_STATE_HANDLER_PROVISION_DEVICE_STATE_HANDLER_H_

#include "rmad/state_handler/base_state_handler.h"

#include <memory>
#include <utility>

#include <base/synchronization/lock.h>
#include <base/timer/timer.h>

namespace rmad {

class ProvisionDeviceStateHandler : public BaseStateHandler {
 public:
  // Report status every second.
  static constexpr base::TimeDelta kReportStatusInterval =
      base::TimeDelta::FromSeconds(1);
  // Mock provision progress. Update every 2 seconds.
  static constexpr base::TimeDelta kUpdateProgressInterval =
      base::TimeDelta::FromSeconds(2);

  explicit ProvisionDeviceStateHandler(scoped_refptr<JsonStore> json_store);

  ASSIGN_STATE(RmadState::StateCase::kProvisionDevice);
  SET_REPEATABLE;

  void RegisterSignalSender(
      std::unique_ptr<ProvisionSignalCallback> callback) override {
    provision_signal_sender_ = std::move(callback);
  }

  RmadErrorCode InitializeState() override;
  void CleanUpState() override;
  GetNextStateCaseReply GetNextStateCase(const RmadState& state) override;

 protected:
  ~ProvisionDeviceStateHandler() override = default;

 private:
  void SendStatusSignal();
  void StartStatusTimer();
  void StopStatusTimer();

  void StartProvision();
  void UpdateProgress(bool restart);

  ProvisionStatus status_;
  std::unique_ptr<ProvisionSignalCallback> provision_signal_sender_;
  base::RepeatingTimer status_timer_;
  mutable base::Lock lock_;

  // Used to mock provision progress.
  base::RepeatingTimer provision_timer_;
};

}  // namespace rmad

#endif  // RMAD_STATE_HANDLER_PROVISION_DEVICE_STATE_HANDLER_H_
