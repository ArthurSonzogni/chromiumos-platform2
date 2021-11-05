// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_STATE_HANDLER_FINALIZE_STATE_HANDLER_H_
#define RMAD_STATE_HANDLER_FINALIZE_STATE_HANDLER_H_

#include "rmad/state_handler/base_state_handler.h"

#include <memory>
#include <utility>

#include <base/synchronization/lock.h>
#include <base/timer/timer.h>

namespace rmad {

class FinalizeStateHandler : public BaseStateHandler {
 public:
  // Report status every second.
  static constexpr base::TimeDelta kReportStatusInterval =
      base::TimeDelta::FromSeconds(1);
  // Mock finalize progress. Update every 2 seconds.
  static constexpr base::TimeDelta kUpdateProgressInterval =
      base::TimeDelta::FromSeconds(2);

  explicit FinalizeStateHandler(scoped_refptr<JsonStore> json_store);

  ASSIGN_STATE(RmadState::StateCase::kFinalize);
  SET_UNREPEATABLE;

  void RegisterSignalSender(
      std::unique_ptr<FinalizeSignalCallback> callback) override {
    finalize_signal_sender_ = std::move(callback);
  }

  RmadErrorCode InitializeState() override;
  void CleanUpState() override;
  GetNextStateCaseReply GetNextStateCase(const RmadState& state) override;

 protected:
  ~FinalizeStateHandler() override = default;

 private:
  void SendStatusSignal();
  void StartStatusTimer();
  void StopStatusTimer();

  void StartFinalize();
  void UpdateProgress(bool restart);

  FinalizeStatus status_;
  std::unique_ptr<FinalizeSignalCallback> finalize_signal_sender_;
  base::RepeatingTimer status_timer_;
  mutable base::Lock lock_;

  // Used to mock finalize progress.
  base::RepeatingTimer finalize_timer_;
};

namespace fake {

class FakeFinalizeStateHandler : public FinalizeStateHandler {
 public:
  explicit FakeFinalizeStateHandler(scoped_refptr<JsonStore> json_store);

 protected:
  ~FakeFinalizeStateHandler() override = default;
};

}  // namespace fake

}  // namespace rmad

#endif  // RMAD_STATE_HANDLER_FINALIZE_STATE_HANDLER_H_
